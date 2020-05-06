/*****************************************************************************/
// File: jpx.cpp [scope = APPS/JP2]
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
   Implements the internal machinery whose external interfaces are defined
in the compressed-io header file, "jpx.h".
******************************************************************************/

#include <assert.h>
#include <string.h>
#include <math.h>
#include "kdu_utils.h"
#include "jpx.h"
#include "jpx_local.h"
#include "kdu_file_io.h"
#include "kdu_client_window.h"
using namespace kd_supp_local;


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
     kdu_error _name("E(jpx.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(jpx.cpp)",_id);
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

#define JX_PI 3.141592653589793

/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   jpx_add_box_descriptions                         */
/*****************************************************************************/

static bool jx_textualizer_copt(jp2_input_box *, kdu_message &, bool, int);
static bool jx_textualizer_iset(jp2_input_box *, kdu_message &, bool, int);

namespace kdu_supp {

void
  jpx_add_box_descriptions(jp2_box_textualizer &textualizer)
{
  textualizer.add_box_type(jp2_comp_options_4cc,NULL,jx_textualizer_copt);
  textualizer.add_box_type(jp2_comp_instruction_set_4cc,NULL,
                           jx_textualizer_iset);
}
  
} // namespace kdu_supp


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                jx_check_metanode_before_add                        */
/*****************************************************************************/

static void jx_check_metanode_before_add_child(jx_metanode *state)
{
  if (state == NULL)
    { KDU_ERROR_DEV(e,0x02050901); e <<
      KDU_TXT("Trying to add new metadata to a `jpx_metanode' interface "
              "which is empty!");
    }
  if (state->flags & JX_METANODE_WRITTEN)
    { 
      if (state->parent == NULL)
        state->flags &= ~JX_METANODE_WRITTEN;
      else
        { KDU_ERROR(e,0x16081203); e <<
          KDU_TXT("Trying to add descendants to a metadata node that has "
                  "already been written to the output JPX file.");
        }
    }
  if ((state->manager->target != NULL) &&
      state->manager->target->check_header_or_metadata_in_progress())
    { KDU_ERROR(e,0x16081204); e <<
      KDU_TXT("Trying to add metadata to a `jpx_target' object while an "
              "incomplete sequence of calls to `jpx_target::write_headers' "
              "or `jpx_target::write_metadata' is in progress.  You must "
              "continue to call such functions until they return NULL, "
              "before adding new content.");
    }
  if ((state->rep_id == JX_CROSSREF_NODE) && (state->crossref != NULL) &&
      (state->crossref->link_type != JPX_METANODE_LINK_NONE) &&
      (state->crossref->link_type != JPX_GROUPING_LINK))
    { KDU_ERROR(e,0x02050902); e <<
      KDU_TXT("Trying to add descendants to a metadata node (`jpx_metanode') "
              "which is currently identified as a non-grouping link node.  "
              "Any link node (node represented by a cross-reference to "
              "another node in the metadata hierarchy) which has descendants "
              "must be a grouping link node -- one with link-type "
              "`JPX_GROUPING_LINK'.");
    }
}

/*****************************************************************************/
/* STATIC              jx_check_metanode_before_change                       */
/*****************************************************************************/

static void jx_check_metanode_before_change(jx_metanode *state)
{
  if (state == NULL)
    { KDU_ERROR_DEV(e,0x16081205); e <<
      KDU_TXT("Trying to change metadata via a `jpx_metanode' interface "
              "which is empty!");
    }
  if (state->flags & JX_METANODE_WRITTEN)
    { KDU_ERROR(e,0x16081206); e <<
      KDU_TXT("Trying to change a metadata node that has "
              "already been written to the output JPX file.");
    }
  if ((state->manager->target != NULL) &&
      state->manager->target->check_header_or_metadata_in_progress())
    { KDU_ERROR(e,0x16081207); e <<
      KDU_TXT("Trying to change metadata within a `jpx_target' object while "
              "an incomplete sequence of calls to `jpx_target::write_headers' "
              "or `jpx_target::write_metadata' is in progress.  You must "
              "continue to call such functions until they return NULL, "
              "before changing existing content.");
    }
}

/*****************************************************************************/
/* STATIC                        jx_midpoint                                 */
/*****************************************************************************/

static kdu_coords
  jx_midpoint(const kdu_coords &v1, const kdu_coords &v2)
{
  kdu_long val;
  kdu_coords result;
  val = v1.x; val += v2.x; result.x = (int)((val+1)>>1);
  val = v1.y; val += v2.y; result.y = (int)((val+1)>>1);
  return result;
}

/*****************************************************************************/
/* STATIC                    jx_project_to_line                              */
/*****************************************************************************/

static bool
  jx_project_to_line(kdu_coords v1, kdu_coords v2, kdu_coords &point)
  /* This function projects the `point', as found on entry, to the nearest
     point on the line which intersects locations `v1' and `v2', returning
     the projected point (rounded to the nearest integer coordinates) via
     `point'.  If `v1' and `v2' are identical, the function returns false,
     doing nothing to `point'.  Otherwise, it returns true. */
{
  if (v1 == v2)
    return false;
  
  // First find t such that the vector from point to v1+t(v2-v1) is orthogonal
  // to the line from v1 to v2.  Then evaluate v1+t(v2-v1).
  double Ax=v1.x, Ay=v1.y, Bx=v2.x, By=v2.y;
  double den = (Bx-Ax)*(Bx-Ax) + (By-Ay)*(By-Ay);
  double num = (point.x-Ax)*(Bx-Ax) + (point.y-Ay)*(By-Ay);
  double t = num/den; // `den' cannot be 0 because `v1' != `v2'
  double x = Ax+t*(Bx-Ax), y = Ay+t*(By-Ay);
  point.x = (int) floor(0.5+x);
  point.y = (int) floor(0.5+y);
  return true;
}

/*****************************************************************************/
/* STATIC               jx_find_path_edge_intersection                       */
/*****************************************************************************/

static bool
  jx_find_path_edge_intersection(kdu_coords *A, kdu_coords *B, kdu_coords *C,
                                 double delta, kdu_coords *pt)
  /* On entry, the first 3 arguments define two connected line segments:
     (A,B) and (B,C).  If `C' is NULL, line segment (B,C) is taken to be
     parallel to line segment (A,B) -- see below.  If either line segment
     has zero length, the function returns false.  The function displaces
     each line segment by an amount delta in the rightward perpendicular
     direction and finds their new intersection, rounding its coordinates to
     the nearest integer and returning them via `pt'.  If the line segments
     happen to be parallel, the function simply displaces point B by `delta'
     in the rightward direction, perpendicular to (A,B), rounds its
     coordinates and returns them via `pt'.  The function returns true in
     all cases, except if A=B or B=C, as indicated earlier, or if the
     intersection lies beyond pont A or beyond point C. */
{
  if ((A == NULL) || (B == NULL))
    return false;
  double ABx, ABy, BCx, BCy;
  BCx=ABx=B->x-A->x;
  BCy=ABy=B->y-A->y;
  if (C != NULL)
    { 
      BCx=C->x-B->x;
      BCy=C->y-B->y;
    }
  double AB_len = sqrt(ABx*ABx+ABy*ABy);
  double BC_len = sqrt(BCx*BCx+BCy*BCy);
  if ((AB_len < 0.1) || (BC_len < 0.1))
    return false;
  double p1_x=B->x-ABy*delta/AB_len, p1_y=B->y+ABx*delta/AB_len;
  double p2_x=B->x-BCy*delta/BC_len, p2_y=B->y+BCx*delta/BC_len;
  
  // Now solve p1 - t*AB = p2 + u*BC for some t<1 and u<1.
  // That is, p1-p2 = AB*t + BC*u.
  double m11=ABx, m12=BCx, m21=ABy, m22=BCy;
  double det = m11*m22-m12*m21;
  if ((det < 0.1) && (det > -0.1))
    { // The lines are parallel or almost parallel.
      pt->x = (int) floor(0.5+0.5*(p1_x+p2_x));
      pt->y = (int) floor(0.5+0.5*(p1_y+p2_y));
      return true;
    }
  double t = (m22*(p1_x-p2_x) - m12*(p1_y-p2_y)) / det;
  double u = (m11*(p1_y-p2_y) - m21*(p1_x-p2_x)) / det;
  pt->x = (int) floor(0.5 + p1_x-t*ABx);
  pt->y = (int) floor(0.5 + p1_y-t*ABy);
  return ((t < 1) && (u < 1));
}

/*****************************************************************************/
/* STATIC                 jx_check_line_intersection                         */
/*****************************************************************************/

static bool
  jx_check_line_intersection(const kdu_coords &A, const kdu_coords &B,
                             bool open_at_A, bool open_at_B,
                             const kdu_coords &C, const kdu_coords &D)
  /* This function determines whether or not the line segment from A->B
     intersects with (or even touches) the line segment from C->D.  Both
     line segments are assumed to have non-zero length (i.e., A != B and
     C != D).  The lines could potentially be parallel, in which case, the
     function still checks whether they intersect or touch.  If `open_at_A'
     is true, the single point A is not considered to be long to the line
     segment A->B, so the function can return false even if A belongs to
     C->D.  Similarly, if `open_at_B' is true, the single point B is not
     considered to belong to the line segment A->B and B is allowed to
     touch C->D. */
{
  // Write x = B + t'(A-B) for a point in [A,B] where 0 <= t' <= 1.
  // Write y = D + u'(C-D) for a point in [C,D] where 0 <= u' <= 1.
  // So if x = y for some t and u, we must have:
  // [(A-B)  (D-C)] * [t' u']^t = (D-B).
  kdu_long AmB_x = A.x-B.x, AmB_y=A.y-B.y;
  kdu_long DmC_x = D.x-C.x, DmC_y=D.y-C.y;
  kdu_long det = AmB_x*DmC_y - DmC_x*AmB_y;
  kdu_long DmB_x=D.x-B.x, DmB_y = D.y-B.y;
  kdu_long t = DmC_y*DmB_x - DmC_x*DmB_y; // t = t'*det
  kdu_long u = AmB_x*DmB_y - AmB_y*DmB_x; // u = u'*det
  if (det == 0)
    { // Line segments [A,B] and [C,D] are parallel.  It is still possible
      // to have an intersection, although rather unlikely.
      if (AmB_x == 0)
        { // The line segments are co-linear if their x-coords agree
          if (C.x != A.x)
            return false; // not co-linear
          assert(AmB_y != 0);
          // If t' = DmB_y/AmB_y and u' = CmB_y/AmB_y, then intersection
          // occurs if: a) 0 <= t' <= 1 (with strict inequalities if open at
          // B and A, respectively); b) 0 <= u' <= 1 (with strict inequalities
          // if open at B and A, respectively); c) t' <= 0 and u' >= 1; or
          // d) u' <= 0 and t' >= 1.
          det = AmB_y;
          t = DmB_y; // t = t'*det
          u = C.y-B.y; // u = u'*det
        }
      else
        { // Let t' = DmB_x / AmB_x and u' = CmB_x / AmB_x.  Then the line
          // segments are co-linear if t = DmB_y / AmB_y.  That is, we must
          // have t'*AmB_y = DmB_y.
          det = AmB_x;
          t = DmB_x; // t = t'*det
          u = C.x-B.x; // u = u'*det
          if ((t*AmB_y) != (det*DmB_y))
            return false; // Line segments are not co-linear
          // If we get here, then intersetion ocurrs under the same four
          // conditions (a) through (d) which were described above for the
          // AmB_x = 0 case.
        }
      if (det < 0)
        { det = -det; t = -t; u = -u; }
      if (((t >= 0) && !(open_at_B && (t==0))) &&
          ((t <= det) && !(open_at_A && (t==det))))
        return true;
      else if (((u >= 0) && !(open_at_B && (u==0))) &&
               ((u <= det) && !(open_at_A && (u==det))))
        return true;
      else if (((t <= 0) && (u >= det)) || ((u <= 0) && (t >= det)))
        return true; // Cases (c) and (d)
    }
  else
    { // Regular case of non-parallel lines
      if (det < 0)
        { det=-det; t=-t; u=-u; }
      if ((u < 0) || (u > det))
        return false;
      if ((t < 0) || (open_at_B && (t == 0)))
        return false;
      if ((t > det) || (open_at_A && (t == det)))
        return false;
      return true;
    }
  return false;
}

/* ========================================================================= */
/*                       Internal Textualizer Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     jx_textualizer_copt                            */
/*****************************************************************************/

static bool
  jx_textualizer_copt(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_uint32 height=0, width=0;
  kdu_byte loop=0;
  if (!(box->read(height) && box->read(width) && box->read(loop)))
    return false;
  msg << "<width> " << width << " </width>\n";
  msg << "<height> " << height << " </height>\n";
  msg << "<loop> " << (int) loop << " </loop>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jx_textualizer_iset                            */
/*****************************************************************************/

static bool
  jx_textualizer_iset(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_uint16 flags=0, rept=0;
  kdu_uint32 tick=0;
  if (!(box->read(flags) && box->read(rept) && box->read(tick)))
    return false;
  bool have_target_pos = ((flags & 1) != 0);
  bool have_target_size = ((flags & 2) != 0);
  bool have_life_persist = ((flags & 4) != 0);
  bool have_source_region = ((flags & 32) != 0);
  bool have_orientation = ((flags & 64) != 0);
  if (!(have_target_pos || have_target_size ||
        have_life_persist || have_source_region || have_orientation))
    return false;
  msg << "<rept> " << rept << " </rept>\n";
  msg << "<tick> " << tick << " </rept>\n";
  while (true)
    { 
      kdu_uint32 X0=0, Y0=0;
      if (have_target_pos && !(box->read(X0) && box->read(Y0)))
        break;
      kdu_uint32 XS=0, YS=0;
      if (have_target_size && !(box->read(XS) && box->read(YS)))
        break;
      kdu_uint32 life=0, next_reuse=0;
      if (have_life_persist && !(box->read(life) && box->read(next_reuse)))
        break;
      kdu_uint32 XC=0, YC=0, WC=0, HC=0;
      if (have_source_region &&
          !(box->read(XC) && box->read(YC) && box->read(WC) && box->read(HC)))
        break;
      kdu_uint32 rot=0;
      if (have_orientation && !box->read(rot))
        break;
      msg << "<instruction>\n";
      msg << "    <persist> "
          << ((life & 0x80000000)?"yes":"no") << " </persist>\n";
      msg << "    <life> " << (life & 0x7FFFFFFF) << " </life>\n";
      msg << "    <reuse> " << next_reuse << " </reuse>\n";
      if (have_source_region)
        { 
          msg << "    <Xcrop> "<<XC<<" </Xcrop><Ycrop> "<<YC<<" </Ycrop>\n";
          msg << "    <Wcrop> "<<WC<<" </Wcrop><Hcrop> "<<HC<<" </Hcrop>\n";
        }
      if (have_target_pos)
        msg << "    <Xtgt> "<<X0<<" </Xtgt><Ytgt> "<<Y0<<" </Ytgt>\n";
      if (have_target_size)
        msg << "    <Wtgt> "<<XS<<" </Wtgt><Htgt> "<<YS<<" </Htgt>\n";
      if (have_orientation)
        msg << "    <rot> " << rot << " </rot>\n";
      msg << "</instruction>\n";
    }
  return true;
}


/* ========================================================================= */
/*                              jx_fragment_lst                              */
/* ========================================================================= */

/*****************************************************************************/
/*                           jx_fragment_lst::init                           */
/*****************************************************************************/

bool
  jx_fragment_lst::init(jp2_input_box *flst, bool allow_errors)
{
  assert(flst->get_box_type() == jp2_fragment_list_4cc);
  reset();
  kdu_uint16 nf;
  if (!flst->read(nf))
    {
      if (!allow_errors)
        return false;
      else
        {
          KDU_ERROR(e,0); e <<
          KDU_TXT("Error encountered reading fragment list (flst) box.  "
                  "Unable to read the initial fragment count.");
        }
    }
  
  jpx_fragment_list ifc((jx_fragment_list *) this);
  for (; nf > 0; nf--)
    {
      kdu_uint16 url_idx=0;
      kdu_uint32 off1=0, off2=0, len=0;
      if (!(flst->read(off1) && flst->read(off2) && flst->read(len) &&
            flst->read(url_idx)))
        {
          if (!allow_errors)
            return false;
          else
            {
              KDU_ERROR(e,1); e <<
              KDU_TXT("Error encountered reading fragment list (flst) "
                      "box.  Contents of box terminated prematurely.");
            }
        }
      kdu_long offset = (kdu_long) off2;
#ifdef KDU_LONG64
      offset += ((kdu_long) off1) << 32;
#endif // KDU_LONG64
      ifc.add_fragment((int) url_idx,offset,(kdu_long) len);
    }
  flst->close();
  return true;
}

/*****************************************************************************/
/*                        jx_fragment_lst::parse_ftbl                        */
/*****************************************************************************/

bool
  jx_fragment_lst::parse_ftbl(jp2_input_box &box)
{
  if (box.get_box_type() != jp2_fragment_table_4cc)
    return false;
  if (!box.is_complete())
    return false;
  jp2_input_box sub_box;
  while (sub_box.open(&box))
    { 
      if (sub_box.get_box_type() != jp2_fragment_list_4cc)
        { // Skip irrelevant sub-box
          sub_box.close();
          continue;
        }
      if (!sub_box.is_complete())
        return false;
      this->init(&sub_box);
      return true;
    }
  KDU_ERROR(e,0x23071201); e <<
  KDU_TXT("Encountered Fragment Table (FTBL) box that does not contain "
          "a Fragment List sub-box.  File is invalid.");
  return false;  
}

/*****************************************************************************/
/*                     jx_fragment_lst::count_box_frags                      */
/*****************************************************************************/

int
  jx_fragment_lst::count_box_frags() const
{
  int num_frags = 0;
  if (url == JX_FRAGLIST_URL_LIST)
    { 
      jx_frag *frag = frag_list;
      for (; frag != NULL; frag=frag->next)
        { 
          num_frags++;
#ifdef KDU_LONG64
          if ((frag->length >> 32) > 0)
            num_frags += (int)((frag->length-1) / 0x00000000FFFFFFFF);
#endif // KDU_LONG64
        }
    }
  else if (url <= JX_FRAGLIST_URL_MAX)
    { 
      num_frags = 1;
#ifdef KDU_LONG64
      if (length_high != 0)
        { 
          kdu_long len = length_high;
          len <<= 32;
          len += length_low;
          num_frags += (int)((len-1) / 0x00000000FFFFFFFF);
        }
#endif
    }
  return num_frags;
}

/*****************************************************************************/
/*                         jx_fragment_lst::save_box                         */
/*****************************************************************************/

void
  jx_fragment_lst::save_box(jp2_output_box *super_box, int flst_len)
{
  int num_frags = count_box_frags();
  assert(num_frags > 0);
  if (flst_len > 0)
    { 
      int extra_len = flst_len - calculate_box_length();
      int extra_frags = extra_len / 14;
      assert((extra_len >= 0) && (extra_len == (extra_frags*14)));
      num_frags += extra_frags;
    }
  if (num_frags >= (1<<16))
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("Trying to write too many fragments to a fragment "
        "list (flst) box.  Maximum number of fragments is 65535, but note "
        "that each written fragment must have a length < 2^32 bytes.  Very "
        "long fragments may thus need to be split, creating the appearance "
        "of a very large number of fragments.");
    }
  jp2_output_box flst;
  flst.open(super_box,jp2_fragment_list_4cc);
  flst.write((kdu_uint16) num_frags);
  kdu_long offset=0, remaining_length=0;
  kdu_uint16 url_idx = 0;
  jx_frag *frag = NULL;
  if (url == JX_FRAGLIST_URL_LIST)
    { 
      assert(frag_list != NULL);
      frag = frag_list->next;
      offset = frag_list->offset;
      remaining_length = frag_list->length;
      url_idx = frag_list->url_idx;
    }
  else
    { 
      assert(url <= JX_FRAGLIST_URL_MAX);
      offset = frag_pos;
      remaining_length = (kdu_long) length_low;
#ifdef KDU_LONG64
      remaining_length += ((kdu_long) length_high) << 32;
#endif
      url_idx = this->url;
    }
  
  while (true)
    { 
      do {
        kdu_uint32 frag_length = (kdu_uint32) remaining_length;
#ifdef KDU_LONG64
        if ((remaining_length >> 32) > 0)
          frag_length = 0xFFFFFFFF;
        flst.write((kdu_uint32)(offset>>32));
        flst.write((kdu_uint32) offset);
#else // !KDU_LONG64
        flst.write((kdu_uint32) 0);
        flst.write((kdu_uint32) offset);
#endif // !KDU_LONG64
        flst.write(frag_length);
        flst.write((kdu_uint16) url_idx);
        remaining_length -= (kdu_long) frag_length;
        offset += (kdu_long) frag_length;
        num_frags--;
      } while (remaining_length > 0);
      if (frag == NULL)
        break;
      offset = frag->offset;
      remaining_length = frag->length;
      url_idx = frag->url_idx;
     frag = frag->next;
    } 

  if (flst_len > 0)
    for (; num_frags > 0; num_frags--)
      { // Write empty fragments to make up the required box length 
        flst.write((kdu_uint32)(offset>>32));
        flst.write((kdu_uint32) offset);
        flst.write((kdu_uint32) 0);
        flst.write((kdu_uint16) url_idx);
      }
  flst.close();
  if (flst_len > 0)
    assert(flst_len == (int) flst.get_box_length());
  assert(num_frags == 0);
}

/*****************************************************************************/
/*                          jx_fragment_lst::finalize                        */
/*****************************************************************************/

void
  jx_fragment_lst::finalize(jp2_data_references data_references)
{
  if (url == JX_FRAGLIST_URL_LIST)
    { 
      jx_frag *frag=frag_list;
      for (; frag != NULL; frag=frag->next)
        if (data_references.get_url(frag->url_idx) == NULL)
          break;
      if (frag == NULL)
        return;
    }
  else if ((url == 0) || (url > JX_FRAGLIST_URL_MAX) ||
           (data_references.get_url(url) != NULL))
    return;

  KDU_ERROR(e,3); e <<
  KDU_TXT("Attempting to read or write a fragment list "
          "box which refers to one or more URL's, not found in the "
          "associated data references object "
          "(see `jpx_target::access_data_references').");
}


/* ========================================================================= */
/*                              jpx_fragment_list                            */
/* ========================================================================= */

/*****************************************************************************/
/*                        jpx_fragment_list::add_fragment                    */
/*****************************************************************************/

void
  jpx_fragment_list::add_fragment(int url_idx, kdu_long offset,
                                  kdu_long length)
{
  assert(state != NULL);
  if ((url_idx < 0) || (url_idx >= (1<<16)))
    { KDU_ERROR_DEV(e,0x22071201); e <<
      KDU_TXT("The URL index passed to `jpx_fragment_list::add_fragment' "
              "must lie in the range 0 to 65535.");
    }
  if (state->is_empty())
    { // Start from scratch
      state->reset();
      state->length_low = (kdu_uint32) length;
#ifdef KDU_LONG64
      state->length_high = (kdu_uint16)(length>>32);
#endif      
      if (url_idx <= JX_FRAGLIST_URL_MAX)
        { 
          state->url = (kdu_uint16) url_idx;
          state->frag_pos = offset;
        }
      else
        { 
          state->url = JX_FRAGLIST_URL_LIST;
          state->frag_list = new jx_frag;
          state->frag_list->offset = offset;
          state->frag_list->length = length;
          state->frag_list->url_idx = (kdu_uint16) url_idx;
          state->frag_list->next = NULL;
        }
    }
  else
    { 
      kdu_long initial_total_length = this->get_total_length();
      kdu_long new_total_length = initial_total_length + length;
      state->length_low = (kdu_uint32) new_total_length;
#ifdef KDU_LONG64
      state->length_high = (kdu_uint16)(new_total_length>>32);
#endif
    if (state->url == JX_FRAGLIST_URL_LIST)
      { // Build onto existing list
        jx_frag *frag = state->frag_list;
        assert(frag != NULL);
        while (frag->next != NULL)
          frag = frag->next;
        if ((frag->url_idx == (kdu_uint16) url_idx) &&
            ((frag->offset+frag->length) == offset))
          frag->length += length;
        else
          { 
            frag = frag->next = new jx_frag;
            frag->offset = offset;
            frag->length = length;
            frag->url_idx = (kdu_uint16) url_idx;
            frag->next = NULL;
          }
      }
    else if (state->url <= JX_FRAGLIST_URL_MAX)
      { 
        if ((state->url != (kdu_uint16) url_idx) ||
            ((state->frag_pos+initial_total_length) != offset))
          { // Need to migrate to using a list
            jx_frag *frag = new jx_frag;
            frag->offset = state->frag_pos;
            frag->length = initial_total_length;
            frag->url_idx = state->url;
            state->frag_list = frag;
            state->url = JX_FRAGLIST_URL_LIST;
            frag = frag->next = new jx_frag;
            frag->offset = offset;
            frag->length = length;
            frag->url_idx = (kdu_uint16) url_idx;
            frag->next = NULL;
          }
      }
    else
      assert(0);
    }
}

/*****************************************************************************/
/*                      jpx_fragment_list::get_total_length                  */
/*****************************************************************************/

kdu_long
  jpx_fragment_list::get_total_length() const
{
  kdu_long result = 0;
  if (state != NULL)
    { 
      result = state->length_low;
#ifdef KDU_LONG64
      result += ((kdu_long) state->length_high) << 32;
#endif
    }
  return result;
}

/*****************************************************************************/
/*                      jpx_fragment_list::get_num_fragments                 */
/*****************************************************************************/

int
  jpx_fragment_list::get_num_fragments() const
{
  if ((state == NULL) || state->is_empty())
    return 0;
  int result = 0;
  if (state->url == JX_FRAGLIST_URL_LIST)
    for (jx_frag *frag=state->frag_list; frag != NULL; frag=frag->next)
      result++;
  else if (state->url <= JX_FRAGLIST_URL_MAX)
    result = 1;
  return result;
}

/*****************************************************************************/
/*                        jpx_fragment_list::get_fragment                    */
/*****************************************************************************/

bool
  jpx_fragment_list::get_fragment(int frag_idx, int &url_idx,
                                  kdu_long &offset, kdu_long &length) const
{
  if ((state == NULL) || state->is_empty() || (frag_idx < 0))
    return false;
  if (state->url == JX_FRAGLIST_URL_LIST)
    { 
      jx_frag *frag = state->frag_list;
      for (; (frag != NULL) && (frag_idx > 0); frag=frag->next)
        frag_idx--;
      if (frag == NULL)
        return false;
      url_idx = frag->url_idx;
      offset = frag->offset;
      length = frag->length;
      return true;
    }
  else if ((frag_idx == 0) && (state->url <= JX_FRAGLIST_URL_MAX))
    { 
      url_idx = state->url;
      offset = state->frag_pos;
      length = state->length_low;
#ifdef KDU_LONG64
      length += ((kdu_long) state->length_high) << 32;
#endif
      return true;
    }
  else
    return false;
}

/*****************************************************************************/
/*                     jpx_fragment_list::locate_fragment                    */
/*****************************************************************************/

int
  jpx_fragment_list::locate_fragment(kdu_long pos,
                                     kdu_long &bytes_into_frag) const
{
  if (pos < 0)
    return -1;
  if (state->url == JX_FRAGLIST_URL_LIST)
    { 
      int idx = 0;
      jx_frag *frag=state->frag_list;
      for (; frag != NULL; frag=frag->next, idx++)
        { 
          bytes_into_frag = pos;
          pos -= frag->length;
          if (pos < 0)
            return idx;
        }
    }
  else if (state->url <= JX_FRAGLIST_URL_MAX)
    { 
      bytes_into_frag = pos;
      pos -= (kdu_long) state->length_low;
#ifdef KDU_LONG64
      pos -= ((kdu_long) state->length_high) << 32;
#endif
      if (pos < 0)
        return 0;
    }
  return -1;
}

/*****************************************************************************/
/*                   jpx_fragment_list::any_local_fragments                  */
/*****************************************************************************/

bool
  jpx_fragment_list::any_local_fragments()
{
  if ((state == NULL) || state->is_empty())
    return false;
  if (state->url == JX_FRAGLIST_URL_LIST)
    { 
      jx_frag *frag = state->frag_list;
      for (; frag != NULL; frag=frag->next)
        if (frag->url_idx == 0)
          return true;
    }
  else if (state->url < JX_FRAGLIST_URL_MAX)
    return (state->url == 0);
  return false;
}


/* ========================================================================= */
/*                               jpx_input_box                               */
/* ========================================================================= */

/*****************************************************************************/
/*                       jpx_input_box::jpx_input_box                        */
/*****************************************************************************/

jpx_input_box::jpx_input_box()
{
  frag_idx=last_url_idx=-1;  frag_start=frag_lim=0;
  url_pos=last_url_pos=0; flst_src=NULL;  frag_file=NULL;
  path_buf=NULL; max_path_len = 0;
}

/*****************************************************************************/
/*                         jpx_input_box::open_next                          */
/*****************************************************************************/

bool jpx_input_box::open_next()
{
  if ((flst_src != NULL) || (original_box_length == 0))
    return false;
  else
    return jp2_input_box::open_next();
}

/*****************************************************************************/
/*                          jpx_input_box::open_as                           */
/*****************************************************************************/

bool
  jpx_input_box::open_as(jpx_fragment_list frag_list,
                         jp2_data_references data_references,
                         jp2_family_src *ultimate_src,
                         kdu_uint32 box_type)
{
  if (is_open)
    { KDU_ERROR_DEV(e,4); e <<
        KDU_TXT("Attempting to call `jpx_input_box::open_as' without "
        "first closing the box.");
    }
  if (ultimate_src == NULL)
    { KDU_ERROR_DEV(e,5); e <<
        KDU_TXT("You must supply a non-NULL `ultimate_src' argument "
        "to `jpx_input_box::open_as'.");
    }
  if ((frag_list.state == NULL) || frag_list.state->is_empty())
    { KDU_ERROR_DEV(e,0x23071205); e <<
      KDU_TXT("You must supply a non-empty fragment list in calls "
              "to `jpx_input_box::open_as'.");
    }
  
  this->locator = jp2_locator();
  this->super_box = NULL;
  this->src = NULL;
  
  this->box_type = box_type;
  this->original_box_length = 0;
  this->original_header_length = 0;
  this->original_pos_offset = 0;
  this->next_box_offset = 0;
  this->contents_start = 0;
  this->contents_lim = 0;
  this->bin_id = -1;
  this->codestream_min = this->codestream_lim = this->codestream_id = -1;
  this->bin_class = 0;
  this->can_dereference_contents = false;
  this->rubber_length = false;
  this->is_open = true;
  this->is_locked = false;
  this->capabilities = 0;
  this->pos = 0;
  this->partial_word_bytes = 0;
  frag_file = NULL;
  frag_idx = -1;
  last_url_idx = -1;
  frag_start = frag_lim = url_pos = last_url_pos = 0;
  
  bool hdr_complete;
  if ((codestream_id =
       frag_list.state->get_incremental_codestream(hdr_complete)) >= 0)
    { // Open as incremental codestream
      this->src = ultimate_src;
      codestream_min = codestream_id;
      codestream_lim = codestream_id+1;
      assert(box_type == jp2_codestream_4cc); // We should only be able to
        // reach here if the function is being called internally from
        // within `jpx_codestream_source::open_stream'.
      bin_class = KDU_MAIN_HEADER_DATABIN;
      bin_id = 0;
      contents_lim = KDU_LONG_MAX;
      original_box_length = 0; // Cannot use this object for navigation
      capabilities = KDU_SOURCE_CAP_CACHED | KDU_SOURCE_CAP_SEEKABLE;
    }
  else
    { // Open to read from fragment list
      this->frag_list = frag_list;
      this->flst_src = ultimate_src;
      if (flst_src->cache == NULL)
        this->data_references = data_references;
              // Prevents any data being read at all if the fragment
              // list belongs to a caching data source.  This is the safest
              // way to ensure that we don't have to wait (possibly
              // indefinitely) for a data references box to be delivered
              // by a JPIP server.  JPIP servers should not send fragment
              // lists anyway; they should use the special JPIP equivalent
              // box mechanism.
      original_box_length = contents_lim = frag_list.get_total_length();
      capabilities = KDU_SOURCE_CAP_SEQUENTIAL | KDU_SOURCE_CAP_SEEKABLE;
    }
  return true;
}

/*****************************************************************************/
/*                           jpx_input_box::close                            */
/*****************************************************************************/

bool
  jpx_input_box::close()
{
  if (frag_file != NULL)
    { fclose(frag_file); frag_file = NULL; }
  if (path_buf != NULL)
    { delete[] path_buf; path_buf = NULL; }
  max_path_len = 0;
  frag_idx = -1;
  last_url_idx = -1;
  frag_start = frag_lim = url_pos = last_url_pos = 0;
  flst_src = NULL;
  frag_list = jpx_fragment_list(NULL);
  data_references = jp2_data_references(NULL);
  return jp2_input_box::close();
}

/*****************************************************************************/
/*                           jpx_input_box::seek                             */
/*****************************************************************************/

bool
  jpx_input_box::seek(kdu_long offset)
{
  if ((flst_src == NULL) || (contents_block != NULL))
    return jp2_input_box::seek(offset);
  assert(contents_start == 0);
  if (pos == offset)
    return true;
  kdu_long old_pos = pos;
  if (offset < 0)
    pos = 0;
  else
    pos = (offset < contents_lim)?offset:contents_lim;
  if ((frag_idx >= 0) && (pos >= frag_start) && (pos < frag_lim))
    { // Seeking within existing fragment
      url_pos += (pos-old_pos);
    }
  else
    { // Force re-determination of the active fragment
      frag_idx = -1;
      frag_start = frag_lim = url_pos = 0;
    }
  return true;
}

/*****************************************************************************/
/*                           jpx_input_box::read                             */
/*****************************************************************************/

int
  jpx_input_box::read(kdu_byte *buf, int num_bytes)
{
  if ((!is_open) || is_locked)
    { KDU_ERROR_DEV(e,0x05091201); e <<
      KDU_TXT("Attempt to read from a JP2 box which is either not open "
              "or has an open sub-box.");
    }
  if (contents_block != NULL)
    { // Reproduce the code from `jp2_input_box::read' for max speed
      kdu_long max_bytes = contents_lim - pos;
      if (max_bytes < (kdu_long) num_bytes)
        num_bytes = (int) max_bytes;
      if (num_bytes <= 0)
        return 0;
      memcpy(buf,contents_block+(pos-contents_start),(size_t) num_bytes);
      pos += num_bytes;
      return num_bytes;
    }
  if (flst_src == NULL)
    return jp2_input_box::read(buf,num_bytes);

  int result = 0;
  while (num_bytes > 0)
    {
      if ((frag_idx < 0) || (pos >= frag_lim))
        { // Find the fragment containing `pos'
          int url_idx;
          kdu_long bytes_into_fragment, frag_length;
          frag_idx = frag_list.locate_fragment(pos,bytes_into_fragment);
          if ((frag_idx < 0) ||
              !frag_list.get_fragment(frag_idx,url_idx,url_pos,frag_length))
            { // Cannot read any further
              frag_idx = -1;
              return result;
            }
          url_pos += bytes_into_fragment;
          frag_start = pos - bytes_into_fragment;
          frag_lim = frag_start + frag_length;
          if (url_idx != last_url_idx)
            { // Change the URL
              if (frag_file != NULL)
                {
                  fclose(frag_file);
                  frag_file = NULL;
                  last_url_idx = -1;
                }
              const char *file_url = "";
              if (url_idx != 0)
                {
                  if ((!data_references) ||
                      ((file_url =
                        data_references.get_file_url(url_idx)) == NULL) ||
                      ((*file_url != '\0') &&
                       ((frag_file=url_fopen(file_url)) == NULL)))
                    { // Cannot read any further
                      frag_idx = -1;
                      return result;
                    }
                }
              last_url_pos = 0;
              last_url_idx = url_idx;
            }
        }
      if (url_pos != last_url_pos)
        {
          if (frag_file != NULL) // Don't have to seek `kdu_family_src'
            kdu_fseek(frag_file,url_pos);
          last_url_pos = url_pos;
        }

      int xfer_bytes = num_bytes;
      if ((pos+xfer_bytes) > frag_lim)
        xfer_bytes = (int)(frag_lim-pos);
      if (frag_file != NULL)
        { // Read from `frag_file'
          xfer_bytes = (int) fread(buf,1,(size_t) xfer_bytes,frag_file);
        }
      else
        { // Read from `flst_src'
          if (flst_src->cache != NULL)
            return result;
          flst_src->acquire_lock();
          if (flst_src->last_read_pos != url_pos)
            {
              if (flst_src->fp != NULL)
                kdu_fseek(flst_src->fp,url_pos);
              else if (flst_src->indirect != NULL)
                flst_src->indirect->seek(url_pos);
            }
          if (flst_src->fp != NULL)
            xfer_bytes = (int) fread(buf,1,(size_t) xfer_bytes,flst_src->fp);
          else if (flst_src->indirect != NULL)
            xfer_bytes = flst_src->indirect->read(buf,xfer_bytes);
          flst_src->last_read_pos = url_pos + xfer_bytes;
          flst_src->release_lock();
        }

      if (xfer_bytes == 0)
        break; // Cannot read any further

      num_bytes -= xfer_bytes;
      result += xfer_bytes;
      buf += xfer_bytes;
      pos += xfer_bytes;
      url_pos += xfer_bytes;
      last_url_pos += xfer_bytes;
    }
  return result;
}

/*****************************************************************************/
/*                          jpx_input_box::url_fopen                         */
/*****************************************************************************/

FILE *
  jpx_input_box::url_fopen(const char *path)
{
  // First step is to determine whether or not this is a relative URL
  bool is_absolute = false;
  if ((path[0] == '.') && ((path[1] == '/') || (path[1] == '\\')))
    path += 2; // Walk over relative prefix
  else
    is_absolute = true;
  if (!is_absolute)
    {
      const char *rel_fname = flst_src->get_filename();
      if (rel_fname == NULL)
        return NULL;
      int len = ((int)(strlen(rel_fname) + strlen(path))) + 2;
      if (len > max_path_len)
        {
          max_path_len += len;
          if (path_buf != NULL)
            delete[] path_buf;
          path_buf = new char[max_path_len];
        }
      strcpy(path_buf,rel_fname);
      char *cp = path_buf+strlen(path_buf);
      while ((cp > path_buf) && (cp[-1] != '/') && (cp[-1] != '\\'))
        cp--;
      strcpy(cp,path);
      path = path_buf;
    }
  return fopen(path,"rb");
}


/* ========================================================================= */
/*                              jx_compatibility                             */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_compatibility::init_ftyp                        */
/*****************************************************************************/

bool
  jx_compatibility::init_ftyp(jp2_input_box *ftyp)
{
  assert(ftyp->get_box_type() == jp2_file_type_4cc);
  kdu_uint32 brand, minor_version, compat;
  ftyp->read(brand); ftyp->read(minor_version);

  bool jp2_compat=false, jpx_compat=false, jpxb_compat=false;
  while (ftyp->read(compat))
    if (compat == jp2_brand)
      jp2_compat = true;
    else if (compat == jpx_brand)
      jpx_compat = true;
    else if (compat == jpx_baseline_brand)
      jpxb_compat = jpx_compat = true;

  if (!ftyp->close())
    { KDU_ERROR(e,6); e <<
        KDU_TXT("JP2-family data source contains a malformed file type box.");
    }
  if (!(jp2_compat || jpx_compat))
    return false;
  this->is_jp2 = (brand == jp2_brand) || (!jpx_compat);
  this->is_jp2_compatible = jp2_compat;
  this->is_jpxb_compatible = jpxb_compat;
  this->have_rreq_box = false; // Until we find one
  return true;
}

/*****************************************************************************/
/*                        jx_compatibility::init_rreq                        */
/*****************************************************************************/

void
  jx_compatibility::init_rreq(jp2_input_box *rreq)
{
  assert(rreq->get_box_type() == jp2_reader_requirements_4cc);
  kdu_byte m, byte, mask_length = 0;
  int n, shift, m_idx;
  rreq->read(mask_length);
  for (shift=24, m_idx=0, m=0; (m < mask_length) && (m < 32); m++, shift-=8)
    {
      if (shift < 0)
        { shift=24; m_idx++; }
      rreq->read(byte);
      fully_understand_mask[m_idx] |= (((kdu_uint32) byte) << shift);
    }
  for (shift=24, m_idx=0, m=0; (m < mask_length) && (m<32); m++, shift-=8)
    {
      if (shift < 0)
        { shift=24; m_idx++; }
      rreq->read(byte);
      decode_completely_mask[m_idx] |= (((kdu_uint32) byte) << shift);
    }

  kdu_uint16 nsf;
  if (!rreq->read(nsf))
    { KDU_ERROR(e,7); e <<
        KDU_TXT("Malformed reader requirements (rreq) box found in "
        "JPX data source.  Box terminated unexpectedly.");
    }
  have_rreq_box = true;
  num_standard_features = max_standard_features = (int) nsf;
  standard_features = new jx_feature[max_standard_features];
  has_pixel_format = false;
  for (n=0; n < num_standard_features; n++)
    { 
      jx_feature *fp = standard_features + n;
      rreq->read(fp->feature_id);
      for (shift=24, m_idx=0, m=0; (m < mask_length) && (m<32); m++, shift-=8)
        {
          if (shift < 0)
            { shift=24; m_idx++; }
          rreq->read(byte);
          fp->mask[m_idx] |= (((kdu_uint32) byte) << shift);
        }
      fp->supported = true;
      if (fp->feature_id == JPX_SF_CODESTREAM_FRAGMENTS_REMOTE)
        fp->supported = false; // The only one we know we don't support.
      if ((fp->feature_id == JPX_SF_PIXEL_FORMAT_FIXPOINT) ||
          (fp->feature_id == JPX_SF_PIXEL_FORMAT_FLOAT) ||
          (fp->feature_id == JPX_SF_PIXEL_FORMAT_SPLIT_EXP))
        has_pixel_format = true;
    }

  kdu_uint16 nvf;
  if (!rreq->read(nvf))
    { KDU_ERROR(e,8); e <<
        KDU_TXT("Malformed reader requirements (rreq) box found in "
        "JPX data source.  Box terminated unexpectedly.");
    }
  num_vendor_features = max_vendor_features = (int) nvf;
  vendor_features = new jx_vendor_feature[max_vendor_features];
  for (n=0; n < num_vendor_features; n++)
    {
      jx_vendor_feature *fp = vendor_features + n;
      if (rreq->read(fp->uuid,16) != 16)
        { KDU_ERROR(e,9); e <<
            KDU_TXT("Malformed reader requirements (rreq) box found "
            "in JPX data source. Box terminated unexpectedly.");
        }
      for (shift=24, m_idx=0, m=0; (m < mask_length) && (m<32); m++, shift-=8)
        {
          if (shift < 0)
            { shift=24; m_idx++; }
          if (!rreq->read(byte))
            { KDU_ERROR(e,10); e <<
                KDU_TXT("Malformed reader requirements (rreq) box "
                "found in JPX data source. Box terminated unexpectedly.");
            }
          fp->mask[m_idx] |= (((kdu_uint32) byte) << shift);
        }
      fp->supported = false;
    }
  if (!rreq->close())
    { KDU_ERROR(e,11); e <<
        KDU_TXT("Malformed reader requirements (rreq) box "
        "found in JPX data source.  Box appears to be too long.");
    }
}

/*****************************************************************************/
/*                     jx_compatibility::copy_from                           */
/*****************************************************************************/

void
  jx_compatibility::copy_from(jx_compatibility *src)
{
  jpx_compatibility src_ifc(src);
  is_jp2_compatible = is_jp2_compatible && src->is_jp2_compatible;
  is_jpxb_compatible = is_jpxb_compatible && src->is_jpxb_compatible;
  no_extensions = no_extensions &&
    src_ifc.check_standard_feature(JPX_SF_CODESTREAM_NO_EXTENSIONS);
  no_opacity = no_opacity &&
    src_ifc.check_standard_feature(JPX_SF_NO_OPACITY);
  no_fragments = no_fragments &&
    src_ifc.check_standard_feature(JPX_SF_CODESTREAM_CONTIGUOUS);
  no_scaling = no_scaling &&
    src_ifc.check_standard_feature(JPX_SF_NO_SCALING);
  has_pixel_format = has_pixel_format ||
    src_ifc.check_standard_feature(JPX_SF_PIXEL_FORMAT_FIXPOINT) ||
    src_ifc.check_standard_feature(JPX_SF_PIXEL_FORMAT_FLOAT) ||
    src_ifc.check_standard_feature(JPX_SF_PIXEL_FORMAT_SPLIT_EXP);
  single_stream_layers = single_stream_layers &&
    src_ifc.check_standard_feature(JPX_SF_ONE_CODESTREAM_PER_LAYER);
  int n, k;
  for (n=0; n < src->num_standard_features; n++)
    {
      jx_feature *fp, *feature = src->standard_features + n;
      if ((feature->feature_id == JPX_SF_CODESTREAM_NO_EXTENSIONS) ||
          (feature->feature_id == JPX_SF_NO_OPACITY) ||
          (feature->feature_id == JPX_SF_CODESTREAM_CONTIGUOUS) ||
          (feature->feature_id == JPX_SF_NO_SCALING) ||
          (feature->feature_id == JPX_SF_ONE_CODESTREAM_PER_LAYER))
        continue; // The above are special "negative" features
      for (fp=standard_features, k=0; k < num_standard_features; k++, fp++)
        if (fp->feature_id == feature->feature_id)
          break;
      if (k == num_standard_features)
        { // Add a new feature
          if (k == max_standard_features)
            {
              max_standard_features += k + 10;
              fp = new jx_feature[max_standard_features];
              for (k=0; k < num_standard_features; k++)
                fp[k] = standard_features[k];
              if (standard_features != NULL)
                delete[] standard_features;
              standard_features = fp;
              fp += k;
            }
          num_standard_features++;
          fp->feature_id = feature->feature_id;
        }
      for (k=0; k < 8; k++)
        {
          fp->fully_understand[k] |= feature->fully_understand[k];
          fp->decode_completely[k] |= feature->decode_completely[k];
          fully_understand_mask[k] |= fp->fully_understand[k];
          decode_completely_mask[k] |= fp->decode_completely[k];
        }
    }

  for (n=0; n < src->num_vendor_features; n++)
    {
      jx_vendor_feature *fp, *feature = src->vendor_features + n;
      for (fp=vendor_features, k=0; k < num_vendor_features; k++, fp++)
        if (memcmp(fp->uuid,feature->uuid,16) == 0)
          break;
      if (k == num_vendor_features)
        { // Add a new feature
          if (k == max_vendor_features)
            {
              max_vendor_features += k + 10;
              fp = new jx_vendor_feature[max_vendor_features];
              for (k=0; k < num_vendor_features; k++)
                fp[k] = vendor_features[k];
              if (vendor_features != NULL)
                delete[] vendor_features;
              vendor_features = fp;
              fp += k;
            }
          num_vendor_features++;
          memcpy(fp->uuid,feature->uuid,16);
        }
      for (k=0; k < 8; k++)
        {
          fp->fully_understand[k] |= feature->fully_understand[k];
          fp->decode_completely[k] |= feature->decode_completely[k];
          fully_understand_mask[k] |= fp->fully_understand[k];
          decode_completely_mask[k] |= fp->decode_completely[k];
        }
    }
}

/*****************************************************************************/
/*                  jx_compatibility::add_standard_feature                   */
/*****************************************************************************/

void
  jx_compatibility::add_standard_feature(kdu_uint16 feature_id,
                                         bool add_to_both)
{
  int n;
  jx_feature *fp = standard_features;
  for (n=0; n < num_standard_features; n++, fp++)
    if (fp->feature_id == feature_id)
      break;
  if (n < num_standard_features)
    return; // Feature requirements already set by application

  // Create the feature
  if (max_standard_features == n)
    {
      max_standard_features += n + 10;
      fp = new jx_feature[max_standard_features];
      for (n=0; n < num_standard_features; n++)
        fp[n] = standard_features[n];
      if (standard_features != NULL)
        delete[] standard_features;
      standard_features = fp;
      fp += n;
    }
  num_standard_features++;
  fp->feature_id = feature_id;
  
  // Find an unused "fully understand" sub-expression index and use it
  int m;
  kdu_uint32 mask=0, subx;

  for (m=0; m < 8; m++)
    if ((mask = fully_understand_mask[m]) != 0xFFFFFFFF)
      break;
  if (m < 8)
    { // Otherwise, we are all out of sub-expressions: highly unlikely.
      for (subx=1<<31; subx & mask; subx>>=1);
      fully_understand_mask[m] |= subx;
      fp->fully_understand[m] |= subx;
    }

  if (add_to_both)
    { // Find an unused "decode completely" sub-expression index and use it
      for (m=0; m < 8; m++)
        if ((mask = decode_completely_mask[m]) != 0xFFFFFFFF)
          break;
      if (m < 8)
        { // Otherwise, we are all out of sub-expressions: highly unlikely.
          for (subx=1<<31; subx & mask; subx>>=1);
          decode_completely_mask[m] |= subx;
          fp->decode_completely[m] |= subx;
        }
    }
  
  // Check for pixel format features
  if ((feature_id == JPX_SF_PIXEL_FORMAT_FIXPOINT) ||
      (feature_id == JPX_SF_PIXEL_FORMAT_FLOAT) ||
      (feature_id == JPX_SF_PIXEL_FORMAT_SPLIT_EXP))
    has_pixel_format = true;

  // Check for features which will negate any of the special negative features
  if ((feature_id == JPX_SF_OPACITY_NOT_PREMULTIPLIED) ||
      (feature_id == JPX_SF_OPACITY_BY_CHROMA_KEY))
    no_opacity = false;
  if ((feature_id == JPX_SF_CODESTREAM_FRAGMENTS_INTERNAL_AND_ORDERED) ||
      (feature_id == JPX_SF_CODESTREAM_FRAGMENTS_INTERNAL) ||
      (feature_id == JPX_SF_CODESTREAM_FRAGMENTS_LOCAL) ||
      (feature_id == JPX_SF_CODESTREAM_FRAGMENTS_REMOTE))
    no_fragments = false;
  if ((feature_id == JPX_SF_SCALING_WITHIN_LAYER) ||
      (feature_id == JPX_SF_SCALING_BETWEEN_LAYERS))
    no_scaling = false;
  if (feature_id == JPX_SF_MULTIPLE_CODESTREAMS_PER_LAYER)
    single_stream_layers = false;
}

/*****************************************************************************/
/*                       jx_compatibility::save_boxes                        */
/*****************************************************************************/

void
  jx_compatibility::save_boxes(jx_target *owner)
{
  // Start by writing the file-type box
  owner->open_top_box(&out,jp2_file_type_4cc);
  assert(!is_jp2); // We can read JP2 or JPX, but only write JPX.
  out.write(jpx_brand);
  out.write((kdu_uint32) 0);
  out.write(jpx_brand);
  if (is_jp2_compatible)
    out.write(jp2_brand);
  if (is_jpxb_compatible)
    out.write(jpx_baseline_brand);
  out.close();

  // Now for the reader requirements box.  First, set negative features
  assert(have_rreq_box);
  if (no_extensions)
    add_standard_feature(JPX_SF_CODESTREAM_NO_EXTENSIONS);
  if (no_opacity)
    add_standard_feature(JPX_SF_NO_OPACITY);
  if (no_fragments)
    add_standard_feature(JPX_SF_CODESTREAM_CONTIGUOUS);
  if (no_scaling)
    add_standard_feature(JPX_SF_NO_SCALING);
  if (single_stream_layers)
    add_standard_feature(JPX_SF_ONE_CODESTREAM_PER_LAYER);
  
  // Next, we need to merge the fully understand and decode completely
  // sub-expressions, which may require quite a bit of manipulation.
  kdu_uint32 new_fumask[8] = {0,0,0,0,0,0,0,0};
  kdu_uint32 new_dcmask[8] = {0,0,0,0,0,0,0,0};
  int k, n, src_word, dst_word, src_shift, dst_shift;
  kdu_uint32 src_mask, dst_mask;
  int total_mask_bits=0;
  for (src_word=dst_word=0, src_mask=dst_mask=(1<<31); src_word < 8; )
    {
      if (fully_understand_mask[src_word] & src_mask)
        {
          for (n=0; n < num_standard_features; n++)
            {
              jx_feature *fp = standard_features + n;
              if (fp->fully_understand[src_word] & src_mask)
                fp->mask[dst_word] |= dst_mask;
            }
          for (n=0; n < num_vendor_features; n++)
            {
              jx_vendor_feature *fp = vendor_features + n;
              if (fp->fully_understand[src_word] & src_mask)
                fp->mask[dst_word] |= dst_mask;
            }
          new_fumask[dst_word] |= dst_mask;
          total_mask_bits++;
          if ((dst_mask >>= 1) == 0)
            { dst_mask = 1<<31; dst_word++; }
        }
      if ((src_mask >>= 1) == 0)
        { src_mask = 1<<31; src_word++; }
    }

  for (src_word=0, src_shift=31, src_mask=(1<<31); src_word < 8; )
    {
      if (decode_completely_mask[src_word] & src_mask)
        { // Scan existing sub-expressions looking for one which matches
          bool expressions_match = false;
          for (k=0, dst_word=0, dst_shift=31, dst_mask=(1<<31);
               k < total_mask_bits; k++)
            {
              expressions_match=true;
              for (n=0; (n < num_standard_features) && expressions_match; n++)
                {
                  jx_feature *fp = standard_features + n;
                  if ((((fp->decode_completely[src_word] >> src_shift) ^
                        (fp->mask[dst_word] >> dst_shift)) & 1) != 0)
                    expressions_match = false;
                }
              for (n=0; (n < num_vendor_features) && expressions_match; n++)
                {
                  jx_vendor_feature *fp = vendor_features + n;
                  if ((((fp->decode_completely[src_word] >> src_shift) ^
                        (fp->mask[dst_word] >> dst_shift)) & 1) != 0)
                    expressions_match = false;
                }
              dst_shift--; dst_mask >>= 1;
              if (dst_shift < 0)
                { dst_mask = 1<<31; dst_shift=31; dst_word++; }
            }
          if (!expressions_match)
            {
              if (total_mask_bits == 256)
                continue; // Not enough bits to store this new sub-expression
              total_mask_bits++; // This is a new sub-expression
            }
          new_dcmask[dst_word] |= dst_mask;
        }
      src_shift--; src_mask >>= 1;
      if (src_shift < 0)
        { src_mask = 1<<31; src_shift=31; src_word++; }
    }

  for (n=0; n < 8; n++)
    {
      fully_understand_mask[n] = new_fumask[n];
      decode_completely_mask[n] = new_dcmask[n];
    }

  // Now we are ready to write the reader requirements box
  owner->open_top_box(&out,jp2_reader_requirements_4cc);
  kdu_byte mask_length = (kdu_byte)((total_mask_bits+7)>>3);
  out.write(mask_length);
  for (k=mask_length, src_word=0, src_shift=24; k > 0; k--, src_shift-=8)
    {
      if (src_shift < 0)
        { src_shift = 24; src_word++; }
      out.write((kdu_byte)(fully_understand_mask[src_word] >> src_shift));
    }
  for (k=mask_length, src_word=0, src_shift=24; k > 0; k--, src_shift-=8)
    {
      if (src_shift < 0)
        { src_shift = 24; src_word++; }
      out.write((kdu_byte)(decode_completely_mask[src_word] >> src_shift));
    }
  out.write((kdu_uint16) num_standard_features);
  for (n=0; n < num_standard_features; n++)
    {
      jx_feature *fp = standard_features + n;
      out.write(fp->feature_id);
      for (k=mask_length, src_word=0, src_shift=24; k > 0; k--, src_shift-=8)
        {
          if (src_shift < 0)
            { src_shift = 24; src_word++; }
          out.write((kdu_byte)(fp->mask[src_word] >> src_shift));
        }
    }
  out.write((kdu_uint16) num_vendor_features);
  for (n=0; n < num_vendor_features; n++)
    {
      jx_vendor_feature *fp = vendor_features + n;
      for (k=0; k < 16; k++)
        out.write(fp->uuid[k]);

      for (k=mask_length, src_word=0, src_shift=24; k > 0; k--, src_shift-=8)
        {
          if (src_shift < 0)
            { src_shift = 24; src_word++; }
          out.write((kdu_byte)(fp->mask[src_word] >> src_shift));
        }
    }
  out.close();
}


/* ========================================================================= */
/*                             jpx_compatibility                             */
/* ========================================================================= */

/*****************************************************************************/
/*                         jpx_compatibility::is_jp2                         */
/*****************************************************************************/

bool
  jpx_compatibility::is_jp2() const
{
  return (state != NULL) && state->is_jp2;
}

/*****************************************************************************/
/*                    jpx_compatibility::is_jp2_compatible                   */
/*****************************************************************************/

bool
  jpx_compatibility::is_jp2_compatible() const
{
  return (state != NULL) && state->is_jp2_compatible;
}

/*****************************************************************************/
/*                    jpx_compatibility::is_jpxb_compatible                  */
/*****************************************************************************/

bool
  jpx_compatibility::is_jpxb_compatible() const
{
  return (state != NULL) && state->is_jpxb_compatible;
}

/*****************************************************************************/
/*               jpx_compatibility::has_reader_requirements_box              */
/*****************************************************************************/

bool
  jpx_compatibility::has_reader_requirements_box() const
{
  return (state != NULL) && state->have_rreq_box;
}

/*****************************************************************************/
/*                  jpx_compatibility::check_standard_feature                */
/*****************************************************************************/

bool
  jpx_compatibility::check_standard_feature(kdu_uint16 feature_id) const
{
  if ((state == NULL) || !state->have_rreq_box)
    return false;
  for (int n=0; n < state->num_standard_features; n++)
    if (state->standard_features[n].feature_id == feature_id)
      return true;
  return false;
}

/*****************************************************************************/
/*                   jpx_compatibility::check_vendor_feature                 */
/*****************************************************************************/

bool
  jpx_compatibility::check_vendor_feature(kdu_byte uuid[]) const
{
  if ((state == NULL) || !state->have_rreq_box)
    return false;
  for (int n=0; n < state->num_vendor_features; n++)
    if (memcmp(state->vendor_features[n].uuid,uuid,16) == 0)
      return true;
  return false;
}

/*****************************************************************************/
/*                   jpx_compatibility::get_standard_feature                 */
/*****************************************************************************/

bool
  jpx_compatibility::get_standard_feature(int which,
                                          kdu_uint16 &feature_id) const
{
  if ((state==NULL) || (!state->have_rreq_box) ||
      (which >= state->num_standard_features) || (which < 0))
    return false;
  feature_id = state->standard_features[which].feature_id;
  return true;
}

/*****************************************************************************/
/*              jpx_compatibility::get_standard_feature (extended)           */
/*****************************************************************************/

bool
  jpx_compatibility::get_standard_feature(int which, kdu_uint16 &feature_id,
                                          bool &is_supported) const
{
  if ((state==NULL) || (!state->have_rreq_box) ||
      (which >= state->num_standard_features) || (which < 0))
    return false;
  feature_id = state->standard_features[which].feature_id;
  is_supported = state->standard_features[which].supported;
  return true;
}

/*****************************************************************************/
/*                    jpx_compatibility::get_vendor_feature                  */
/*****************************************************************************/

bool
  jpx_compatibility::get_vendor_feature(int which, kdu_byte uuid[]) const
{
  if ((state==NULL) || (!state->have_rreq_box) ||
      (which >= state->num_vendor_features) || (which < 0))
    return false;
  memcpy(uuid,state->vendor_features[which].uuid,16);
  return true;
}

/*****************************************************************************/
/*               jpx_compatibility::get_vendor_feature (extended)            */
/*****************************************************************************/

bool
  jpx_compatibility::get_vendor_feature(int which, kdu_byte uuid[],
                                        bool &is_supported) const
{
  if ((state==NULL) || (!state->have_rreq_box) ||
      (which >= state->num_vendor_features) || (which < 0))
    return false;
  memcpy(uuid,state->vendor_features[which].uuid,16);
  is_supported = state->vendor_features[which].supported;
  return true;
}

/*****************************************************************************/
/*               jpx_compatibility::set_standard_feature_support             */
/*****************************************************************************/

void
  jpx_compatibility::set_standard_feature_support(kdu_uint16 feature_id,
                                                  bool is_supported)
{
  int n;
  if ((state != NULL) && state->have_rreq_box)
    {
      for (n=0; n < state->num_standard_features; n++)
        if (feature_id == state->standard_features[n].feature_id)
          {
            state->standard_features[n].supported = is_supported;
            break;
          }
    }
}

/*****************************************************************************/
/*                jpx_compatibility::set_vendor_feature_support              */
/*****************************************************************************/

void
  jpx_compatibility::set_vendor_feature_support(const kdu_byte uuid[],
                                                bool is_supported)
{
  int n;
  if ((state != NULL) && state->have_rreq_box)
    {
      for (n=0; n < state->num_vendor_features; n++)
        if (memcmp(uuid,state->vendor_features[n].uuid,16) == 0)
          {
            state->vendor_features[n].supported = is_supported;
            break;
          }
    }
}

/*****************************************************************************/
/*                  jpx_compatibility::test_fully_understand                 */
/*****************************************************************************/

bool
  jpx_compatibility::test_fully_understand() const
{
  if (state == NULL)
    return false;
  if (!state->have_rreq_box)
    return true;

  int n, m;
  kdu_uint32 mask[8] = {0,0,0,0,0,0,0,0};
  for (n=0; n < state->num_standard_features; n++)
    {
      jx_compatibility::jx_feature *fp = state->standard_features + n;
      if (fp->supported)
        for (m=0; m < 8; m++)
          mask[m] |= fp->mask[m];
    }
  for (n=0; n < state->num_vendor_features; n++)
    {
      jx_compatibility::jx_vendor_feature *fp = state->vendor_features + n;
      if (fp->supported)
        for (m=0; m < 8; m++)
          mask[m] |= fp->mask[m];
    }
  for (m=0; m < 8; m++)
    if ((mask[m] & state->fully_understand_mask[m]) !=
        state->fully_understand_mask[m])
      return false;
  return true;
}

/*****************************************************************************/
/*                 jpx_compatibility::test_decode_completely                 */
/*****************************************************************************/

bool
  jpx_compatibility::test_decode_completely() const
{
  if (state == NULL)
    return false;
  if (!state->have_rreq_box)
    return true;

  int n, m;
  kdu_uint32 mask[8] = {0,0,0,0,0,0,0,0};
  for (n=0; n < state->num_standard_features; n++)
    {
      jx_compatibility::jx_feature *fp = state->standard_features + n;
      if (fp->supported)
        for (m=0; m < 8; m++)
          mask[m] |= fp->mask[m];
    }
  for (n=0; n < state->num_vendor_features; n++)
    {
      jx_compatibility::jx_vendor_feature *fp = state->vendor_features + n;
      if (fp->supported)
        for (m=0; m < 8; m++)
          mask[m] |= fp->mask[m];
    }
  for (m=0; m < 8; m++)
    if ((mask[m] & state->decode_completely_mask[m]) !=
        state->decode_completely_mask[m])
      return false;
  return true;
}

/*****************************************************************************/
/*                       jpx_compatibility::copy                             */
/*****************************************************************************/

void
  jpx_compatibility::copy(jpx_compatibility src)
{
  state->copy_from(src.state);
}

/*****************************************************************************/
/*               jpx_compatibility::set_used_standard_feature                */
/*****************************************************************************/

void
  jpx_compatibility::set_used_standard_feature(kdu_uint16 feature_id,
                                               kdu_byte fusx_idx,
                                               kdu_byte dcsx_idx)
{
  if (state == NULL)
    return;
  state->have_rreq_box = true; // Just in case

  int n;
  jx_compatibility::jx_feature *fp = state->standard_features;
  for (n=0; n < state->num_standard_features; n++, fp++)
    if (fp->feature_id == feature_id)
      break;
  if (n == state->num_standard_features)
    { // Create the feature
      if (state->max_standard_features == n)
        {
          state->max_standard_features += n + 10;
          fp = new jx_compatibility::jx_feature[state->max_standard_features];
          for (n=0; n < state->num_standard_features; n++)
            fp[n] = state->standard_features[n];
          if (state->standard_features != NULL)
            delete[] state->standard_features;
          state->standard_features = fp;
          fp += n;
        }
      state->num_standard_features++;
    }

  fp->feature_id = feature_id;
  if (fusx_idx < 255)
    fp->fully_understand[fusx_idx >> 5] |= (1 << (31-(fusx_idx & 31)));
  if (dcsx_idx < 255)
    fp->decode_completely[dcsx_idx >> 5] |= (1 << (31-(dcsx_idx & 31)));
}

/*****************************************************************************/
/*                jpx_compatibility::set_used_vendor_feature                 */
/*****************************************************************************/

void
  jpx_compatibility::set_used_vendor_feature(const kdu_byte uuid[],
                                             kdu_byte fusx_idx,
                                             kdu_byte dcsx_idx)
{
  if (state == NULL)
    return;
  state->have_rreq_box = true; // Just in case

  int n;
  jx_compatibility::jx_vendor_feature *fp = state->vendor_features;
  for (n=0; n < state->num_vendor_features; n++, fp++)
    if (memcpy(fp->uuid,uuid,16) == 0)
      break;
  if (n == state->num_vendor_features)
    { // Create the feature
      if (state->max_vendor_features == n)
        {
          state->max_vendor_features += n + 10;
          fp = new
            jx_compatibility::jx_vendor_feature[state->max_vendor_features];
          for (n=0; n < state->num_vendor_features; n++)
            fp[n] = state->vendor_features[n];
          if (state->vendor_features != NULL)
            delete[] state->vendor_features;
          state->vendor_features = fp;
          fp += n;
        }
      state->num_vendor_features++;
    }

  memcpy(fp->uuid,uuid,16);
  if (fusx_idx < 255)
    {
      fp->fully_understand[fusx_idx>>5] |= (1<<(31-(fusx_idx & 31)));
      state->fully_understand_mask[fusx_idx>>5] |= (1<<(31-(fusx_idx & 31)));
    }
  if (dcsx_idx < 255)
    {
      fp->decode_completely[dcsx_idx>>5] |= (1<<(31-(dcsx_idx & 31)));
      state->decode_completely_mask[dcsx_idx>>5] |= (1<<(31-(dcsx_idx & 31)));
    }
}


/* ========================================================================= */
/*                                  jpx_frame                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          jpx_frame::get_frame_idx                         */
/*****************************************************************************/

int jpx_frame::get_frame_idx() const
{ 
  return (state==NULL)?-1:(state->frame_idx+state_params.rep_idx);
}

/*****************************************************************************/
/*                          jpx_frame::get_track_idx                         */
/*****************************************************************************/

kdu_uint32 jpx_frame::get_track_idx(bool &last_in_context) const
{
  last_in_context = true;
  if (state == NULL)
    return 0;
  jx_composition *comp = state->owner;
  if (comp->track_next != NULL)
    last_in_context = false;
  return comp->track_idx;
}

/*****************************************************************************/
/*                          jpx_frame::access_next                           */
/*****************************************************************************/

jpx_frame jpx_frame::access_next(kdu_uint32 track_idx, bool must_exist)
{
  jpx_frame result;
  if (state == NULL)
    return result;
  jx_frame *tgt_frame = state;
  int tgt_rep_idx = state_params.rep_idx+1;
  int tgt_frame_idx = tgt_frame->frame_idx + tgt_rep_idx;
  jx_composition *comp = tgt_frame->owner;
  if ((track_idx < comp->track_idx) ||
      ((track_idx > comp->track_idx) && (comp->track_next != NULL)))
    return result; // Incompatible track
  int known_layers = 0;
  if (comp->total_frames == 0)
    { 
      if (comp->track_idx == 0)
        { // Find `known_layers' from num top-level compositing layers
          known_layers = comp->abs_layers_per_rep;
          while (comp->abs_layer_reps == 0)
            { 
              known_layers = comp->source->get_num_top_layers();
              if (!comp->source->parse_next_top_level_box())
                break;
            }
        }
      else
        { 
          assert(comp->container_source->indefinitely_repeated());
          bool all_found = comp->source->find_all_streams();
          int reps = comp->container_source->get_known_reps();
          known_layers = reps*comp->abs_layers_per_rep;
          if (all_found)
            comp->total_frames = comp->count_frames(known_layers);
        }
    }
  tgt_frame_idx -= comp->first_frame_idx;
  if (comp->total_frames > 0)
    { // Number of frames is known
      if (tgt_frame_idx >= comp->total_frames)
        { // Need to advance `comp' object
          if (track_idx == 0)
            return result; // Track-0 limited to the global composition box
          if (comp->next_in_track == NULL)
            comp->source->find_all_container_info();
          comp = comp->next_in_track;
          if (comp == NULL)
            return result; // Container not yet encountered
          while ((comp->track_next != NULL) &&
                 (comp->track_next->track_idx <= track_idx))
            comp = comp->track_next;
          if (!comp->finish())
            return result; // Empty interface
          tgt_frame = comp->head;
          tgt_frame_idx = 0;
          tgt_rep_idx = 0;
        }
      else if ((tgt_frame->repeat_count >= 0) &&
               (tgt_rep_idx > tgt_frame->repeat_count))
        { 
          tgt_frame = tgt_frame->next;
          tgt_rep_idx = 0;
        }
    }
  else
    { // Use `known_layers' to determine whether or not track exists
      if ((tgt_frame->repeat_count >= 0) &&
          (tgt_rep_idx > tgt_frame->repeat_count))
        { 
          tgt_frame = tgt_frame->next;
          tgt_rep_idx = 0;
          if ((tgt_frame == NULL) ||
              (must_exist &&
               (tgt_frame->max_initial_layer_idx >= known_layers)))
            return result; // Empty interface
        }
      else if (must_exist &&
               ((tgt_frame->max_repd_layer_idx >= 0) &&
                ((tgt_frame->max_repd_layer_idx +
                  tgt_rep_idx*tgt_frame->increment) >= known_layers)))
        return result; // Empty interface
    }
  result = jpx_frame(tgt_frame,tgt_rep_idx,(state_params.persistents != 0));
  return result;
}

/*****************************************************************************/
/*                          jpx_frame::access_prev                           */
/*****************************************************************************/

jpx_frame jpx_frame::access_prev(kdu_uint32 track_idx, bool must_exist)
{
  jpx_frame result;
  if (state == NULL)
    return result;
  jx_frame *tgt_frame = state;
  int tgt_rep_idx = state_params.rep_idx-1;
  int tgt_frame_idx = tgt_frame->frame_idx + tgt_rep_idx;
  if (tgt_frame_idx < 0)
    return result; // Frame does not exist
  jx_composition *comp = tgt_frame->owner;
  if ((track_idx < comp->track_idx) ||
      ((track_idx > comp->track_idx) && (comp->track_next != NULL)))
    return result; // Incompatible track
  if (tgt_rep_idx < 0)
    { 
      tgt_frame = tgt_frame->prev;
      if (tgt_frame == NULL)
        { 
          comp = comp->prev_in_track;
          if (comp == NULL)
            return result; // Invalid frame -- we are already at the start
          while ((comp->track_next != NULL) &&
                 (comp->track_next->track_idx <= track_idx))
            comp = comp->track_next;
          if (!comp->finish())
            return result; // Need to wait for more data
          tgt_frame = comp->tail;
          while ((tgt_rep_idx = (tgt_frame_idx - tgt_frame->frame_idx)) < 0)
            { 
              tgt_frame = tgt_frame->prev;
              assert(tgt_frame != NULL);
            }
        }
      else
        tgt_rep_idx = tgt_frame->repeat_count;
    }
  result = jpx_frame(tgt_frame,tgt_rep_idx,(state_params.persistents != 0));
  return result;
}

/*****************************************************************************/
/*                        jpx_frame::get_global_info                         */
/*****************************************************************************/

int jpx_frame::get_global_info(kdu_coords &size) const
{
  if ((state == NULL) || (state->owner == NULL))
    return -1;
  size = state->owner->size;
  return state->owner->loop_count;
}

/*****************************************************************************/
/*                            jpx_frame::get_info                            */
/*****************************************************************************/

int jpx_frame::get_info(kdu_long &start_time, kdu_long &duration) const
{
  if (state == NULL)
    return 0;
  duration = state->duration;
  start_time = state->start_time + state_params.rep_idx*state->duration;
  return (state_params.persistents)?(state->total_instructions):
                                    (state->num_instructions);
}

/*****************************************************************************/
/*                         jpx_frame::is_persistent                          */
/*****************************************************************************/

bool jpx_frame::is_persistent() const
{ 
  if (state == NULL)
    return false;
  return state->persistent;
}

/*****************************************************************************/
/*               jpx_frame::get_num_persistent_instructions                  */
/*****************************************************************************/

int jpx_frame::get_num_persistent_instructions() const
{
  if ((state==NULL) || (!state_params.persistents) ||
      (state->last_persistent_frame==NULL))
    return 0;
  return state->last_persistent_frame->total_instructions;
}

/*****************************************************************************/
/*                        jpx_frame::get_instruction                         */
/*****************************************************************************/

bool
  jpx_frame::get_instruction(int which, int &layer_idx,
                             kdu_dims &source_dims, kdu_dims &target_dims,
                             jpx_composited_orientation &orientation) const
{
  if ((state == NULL) || (which < 0))
    return false;
  int instance = state_params.rep_idx;
  jx_frame *scan = state;
  if (state_params.persistents)
    { 
      while (scan->last_persistent_frame != NULL)
        { 
          int delta = which - scan->last_persistent_frame->total_instructions;
          if (delta < 0)
            { 
              scan = scan->last_persistent_frame;
              instance = scan->repeat_count;
              if ((instance < 0) ||
                  ((scan->frame_idx+instance) >= scan->owner->total_frames))
                { // Persistent frame must be last one from top-level comp box
                  instance = scan->owner->total_frames-1-scan->frame_idx;
                  assert(instance >= 0);
                }
            }
          else
            { 
              which = delta;
              break;
            }
        }
    }
  if (which >= scan->num_instructions)
    return false;
  jx_instruction *ip;
  for (ip=scan->head; which > 0; which--, ip=ip->next)
    assert(ip != NULL);

  int rel_layer_idx = ip->layer_idx + instance*ip->increment;
  jx_composition *comp = scan->owner;
  if (comp->abs_layer_start == 0)
    layer_idx = rel_layer_idx; // Top-level box; layer index is absolute
  else
    { // JPX container; need to map layer index
      int r = rel_layer_idx / comp->abs_layers_per_rep;
      layer_idx = (comp->abs_layer_start + r*comp->abs_layer_rep_stride + 
                   rel_layer_idx - r*comp->abs_layers_per_rep);
    }
  source_dims = ip->source_dims;
  target_dims = ip->target_dims;
  orientation = ip->orientation;
  return true;
}

/*****************************************************************************/
/*                jpx_frame::find_last_instruction_for_layer                 */
/*****************************************************************************/

int jpx_frame::find_last_instruction_for_layer(int layer_idx,
                                               int lim_inst_idx) const
{
  if (state == NULL)
    return -1;
  int idx = ((state_params.persistents)?
             (state->total_instructions):(state->num_instructions));
  if (lim_inst_idx <= 0)
    lim_inst_idx = idx;
  int instance = state_params.rep_idx;
  jx_frame *scan = state;
  while (scan != NULL)
    { 
      int start_idx = idx - state->total_instructions;
      if (start_idx >= lim_inst_idx)
        idx -= state->total_instructions; // Completely skip `scan'
      else
        { 
          jx_instruction *ip;
          for (ip=scan->tail; ip != NULL; ip=ip->prev, idx--)
            if ((idx < lim_inst_idx) &&
                ((ip->layer_idx + instance*ip->increment) == layer_idx))
              return idx-1;
        }
      if (!state_params.persistents)
        break;
      if ((scan = scan->last_persistent_frame) != NULL)
        { 
          instance = scan->repeat_count;
          if ((instance < 0) ||
              ((scan->frame_idx+instance) >= scan->owner->total_frames))
            { // Persistent frame must be last one from top-level comp box
              instance = scan->owner->total_frames-1-scan->frame_idx;
              assert(instance >= 0);
            }
        }
    }
  return -1;
}

/*****************************************************************************/
/*                       jpx_frame::get_original_iset                        */
/*****************************************************************************/

bool
  jpx_frame::get_original_iset(int which, int &iset_idx, int &inum_idx) const
{
  if ((state == NULL) || (which < 0))
    return false;
  jx_frame *scan = state;
  if (state_params.persistents)
    { 
      while (scan->last_persistent_frame != NULL)
        { 
          int delta = which - scan->last_persistent_frame->total_instructions;
          if (delta < 0)
            scan = scan->last_persistent_frame;
          else
            { 
              which = delta;
              break;
            }
        }
    }
  if (which >= scan->num_instructions)
    return false;
  jx_instruction *ip;
  for (ip=scan->head; which > 0; which--, ip=ip->next)
    assert(ip != NULL);
  iset_idx = ip->iset_idx;
  inum_idx = ip->inum_idx;
  return true;
}

/*****************************************************************************/
/*                         jpx_frame::get_old_ref                            */
/*****************************************************************************/

jx_frame *jpx_frame::get_old_ref(int &instruction, int &instance) const
{
  if ((state == NULL) || (instruction < 0))
    return NULL;
  instance = state_params.rep_idx;
  jx_frame *scan = state;
  if (state_params.persistents)
    { 
      while (scan->last_persistent_frame != NULL)
        { 
          int delta = (instruction -
                       scan->last_persistent_frame->total_instructions);
          if (delta < 0)
            { 
              scan = scan->last_persistent_frame;
              instance = scan->repeat_count;
              if ((instance < 0) ||
                  ((scan->frame_idx+instance) >= scan->owner->total_frames))
                { // Persistent frame must be last one from top-level comp box
                  instance = scan->owner->total_frames-1-scan->frame_idx;
                  assert(instance >= 0);
                }
            }
          else
            { 
              instruction = delta;
              break;
            }
        }
    }
  if (instruction >= scan->num_instructions)
    scan = NULL;
  return scan;
}


/* ========================================================================= */
/*                              jx_composition                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_composition::add_frame                          */
/*****************************************************************************/

void
  jx_composition::add_frame()
{
  if (tail == NULL)
    {
      head = tail = new jx_frame(this);
      return;
    }
  if (tail->persistent)
    last_persistent_frame = tail;
  tail->next = new jx_frame(this);
  tail->next->prev = tail;
  tail = tail->next;
  tail->last_persistent_frame = last_persistent_frame;
  last_frame_max_lookahead = max_lookahead;
}

/*****************************************************************************/
/*                   jx_composition::donate_composition_box                  */
/*****************************************************************************/

void
  jx_composition::donate_composition_box(jp2_input_box &src)
{
  if (comp_in.exists())
    { KDU_WARNING(w,0); w <<
        KDU_TXT("JPX data source appears to contain multiple "
        "composition boxes!! This is illegal.  "
        "All but first will be ignored.");
      return;
    }
  comp_in.transplant(src);
  num_parsed_iset_boxes = 0;
  finish();
}

/*****************************************************************************/
/*                   jx_composition::donate_instruction_box                  */
/*****************************************************************************/

void
  jx_composition::donate_instruction_box(jp2_input_box &box,
                                         int iset_idx)
{
  assert(abs_layer_rep_stride == 0); // `set_layer_mapping' not yet called
  if (pending_isets == NULL)
    { 
      this->num_parsed_iset_boxes = iset_idx; // Make sure counters are
          // up-to-date so that any parsed instructions for this object
          // receive correctly initialized `iset_idx' members, measured
          // relative to the start of the JPX container.
      if (box.is_complete())
        { 
          parse_iset_box(box);
          box.close();
          return;
        }
      pending_isets = last_pending_iset = new jx_pending_box;
    }
  else
    { 
      assert(last_pending_iset->next == NULL); // `finish' not yet called
      last_pending_iset = last_pending_iset->next = new jx_pending_box;
    }
  last_pending_iset->box.transplant(box);
}

/*****************************************************************************/
/*                           jx_composition::finish                          */
/*****************************************************************************/

bool
  jx_composition::finish()
{
  if (is_complete)
    return true;
  if (finish_in_progress)
    return false; // Prevent recursive calls

  finish_in_progress = true;
  if (container_source != NULL)
    { 
      assert((track_idx != 0) && (prev_in_track != NULL));
      
      // Start by walking backwards to find the `first_frame_idx' and
      // `size' members -- ultimately this requires us to finish the
      // top-level `jx_composition' object, if it has not already happened
      // and also to count its frames.
      if (first_frame_idx < 1)
        { 
          jx_composition *scan = this->prev_in_track;
          while ((scan != NULL) && (scan->container_source != NULL) &&
                 (scan->first_frame_idx < 1))
            scan = scan->prev_in_track;
          assert(scan != NULL);
          if (scan->container_source == NULL)
            { // We have go right back to the top-level composition object
              assert(scan->abs_layers_per_rep > 0); // `set_layer_mapping' must
                                                    // have been called already
              if (!scan->finish())
                { 
                  finish_in_progress = false;
                  return false;
                }
              assert(scan->total_frames > 0); // Should have been set within
                                              // `set_layer_mapping' or finish
            }
          scan->propagate_frame_and_track_info();
          assert((first_frame_idx >= 1) && (size.x > 0) && (size.y > 0));
        }
      
      // Now make sure we have all the Instruction Set boxes available
      if ((abs_layers_per_rep == 0) && container_source->finish())
        assert(abs_layers_per_rep > 0);
      finish_in_progress = false; // No further risk of recursive calls
      if (abs_layers_per_rep == 0)
        return false;
      while ((last_pending_iset = pending_isets) != NULL)
        { 
          if (!pending_isets->box.is_complete())
            return false;
          parse_iset_box(pending_isets->box);
          pending_isets = last_pending_iset->next;
          delete last_pending_iset;
        }
    }
  else
    { // Finishing a top-level Composition box
      while ((!comp_in) && !source->is_top_level_complete())
        if (!source->parse_next_top_level_box())
          break;
      finish_in_progress = false; // No further risk of recursive calls
      if (!comp_in)
        return (is_complete = source->is_top_level_complete());
      assert((track_idx == 0) && (prev_in_track == NULL));
  
      assert(comp_in.get_box_type() == jp2_composition_4cc);
      if (!comp_in.is_complete())
        return false;
  
      while (sub.exists() || sub.open(&comp_in))
        { 
          if (sub.get_box_type() == jp2_comp_options_4cc)
            { 
              if (!sub.is_complete())
                break;
              kdu_uint32 height=0, width=0;
              kdu_byte loop=0;
              if (!(sub.read(height) && sub.read(width) &&
                    sub.read(loop) && (height>0) && (width>0)))
                { KDU_ERROR(e,12); e <<
                  KDU_TXT("Malformed Composition Options (copt) box "
                          "found in JPX data source.  Insufficient or illegal "
                          "field values encountered.  The height and width "
                          "parameters must also be non-zero.");
                }
              if ((width == 0) || (height == 0))
                { KDU_ERROR(e,0x31071202); e <<
                  KDU_TXT("Invalid Composition Options box encountered "
                          "in JPX file -- height and width parameters "
                          "must both be non-zero.");
                }
              if ((width | height) & 0x80000000)
                { 
                  if (width & 0x80000000) width = INT_MAX;
                  if (height & 0x80000000) height = INT_MAX;
                  KDU_WARNING(w,0x31071201); w <<
                  KDU_TXT("Composition Options box supplies composition "
                          "dimensions that are too large to be properly "
                          "represented internally by Kakadu!  Values are "
                          "being truncated; unexpected rendering artefacts "
                          "may result.");
                }
              size.x = (int) width;
              size.y = (int) height;
              if (loop == 255)
                loop_count = 0;
              else
                loop_count = ((int) loop) + 1;
            }
          else if (sub.get_box_type() == jp2_comp_instruction_set_4cc)
            { 
              if (!sub.is_complete())
                break;
              parse_iset_box(sub);          
            }
          sub.close();
        }
      if (sub.exists())
        return false;
      
      comp_in.close();
    }

  is_complete = true;
  assign_layer_indices();
  process_instructions();
  if (abs_layers_per_rep > 0)
    { // `set_layer_mapping' has been called already
      if (container_source == NULL)
        { // Top level Composition box
          assert(total_frames == 0); // Not yet set
          assert(abs_layers_per_rep == source->get_num_top_layers());
          total_frames = count_frames(abs_layers_per_rep);
        }
      else if (total_frames > 0)
        { // Container identified the number of frames, but we still need to
          // call `count_frames' in order to measure the `total_duration'.
          int count = count_frames(abs_layers_per_rep*abs_layer_reps);
          assert(count <= total_frames);
          if (count < total_frames)
            { KDU_ERROR(e,0x06081201); e <<
              KDU_TXT("Compositing Layer Extensions Info box identifies a "
                      "frame count for its presentation tracks which is "
                      "larger than the number of frames that can be "
                      "synthesized using the compositing layers and "
                      "compositing instructions recorded in its parent "
                      "Compositing Layer Extensions box.");
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                      jx_composition::parse_iset_box                       */
/*****************************************************************************/

void
 jx_composition::parse_iset_box(jp2_input_box &box)
{
  kdu_uint16 flags, rept=0;
  kdu_uint32 tick=0;
  if (!(box.read(flags) && box.read(rept) && box.read(tick)))
    { KDU_ERROR(e,13); e <<
      KDU_TXT("Malformed Instruction Set (inst) box "
              "found in JPX data source.  Insufficient fields "
              "encountered.");
    }
  
  bool have_target_pos = ((flags & 1) != 0);
  bool have_target_size = ((flags & 2) != 0);
  bool have_life_persist = ((flags & 4) != 0);
  bool have_source_region = ((flags & 32) != 0);
  bool have_orientation = ((flags & 64) != 0);
  if (!(have_target_pos || have_target_size ||
        have_life_persist || have_source_region || have_orientation))
    { 
      box.close();
      num_parsed_iset_boxes++;
      return;
    }
  kdu_long start_pos = box.get_pos();
  for (int repeat_count=rept; repeat_count >= 0; repeat_count--)
    { 
      int inum_idx = 0;
      jx_frame *start_frame = tail;
      jx_instruction *start_tail = (tail==NULL)?NULL:(tail->tail);
      while (parse_instruction(have_target_pos,have_target_size,
                               have_life_persist,
                               have_source_region,
                               have_orientation,tick,box))
        { 
          tail->tail->iset_idx = num_parsed_iset_boxes;
          tail->tail->inum_idx = inum_idx++;
        }
      if (box.get_remaining_bytes() > 0)
        { KDU_ERROR(e,14); e <<
          KDU_TXT("Malformed Instruction Set (inst) box "
                  "encountered in JPX data source.  Box appears "
                  "to be too long.");
        }
      box.seek(start_pos); // In case we need to repeat it all
      if (repeat_count < 2)
        continue;
    
      /* At this point, we have only to see if we can handle
         repeats by adding a repeat count to the last frame.  For
         this to work, we need to know that this instruction set
         represents exactly one frame, that the frame is
         non-persistent, and that the `next_use' pointers in the
         frame either point beyond the repeat sequence, or to the
         immediate next repetition instance.  Even then, we
         cannot be sure that the first frame has exactly the same
         number of reused layers as the remaining ones, so we will
         have to make a copy of the frame and repeat only the
         second copy. */
      if (tail == start_frame)
        continue; // Instruction set did not create a new frame
      if ((tail->duration == 0) && !tail->pause)
        continue; // Instruction set did not complete a frame
      if ((start_frame == NULL) && (tail != head))
        continue; // Instruction set created multiple frames
      if ((start_frame != NULL) &&
          ((tail != start_frame->next) ||
           (start_frame->tail != start_tail)))
        continue; // Instruction set contributed to multiple frames
    
      jx_instruction *ip;
      int max_repeats = INT_MAX;
      if (last_frame_max_lookahead >= tail->num_instructions)
        max_repeats = (last_frame_max_lookahead / tail->num_instructions) - 1;
      if (max_repeats == 0)
        continue;
      int remaining = tail->num_instructions;
      int reused_layers_in_frame = 0;
      for (ip=tail->head; ip != NULL; ip=ip->next, remaining--)
        { 
          if (ip->next_reuse == tail->num_instructions)
            reused_layers_in_frame++; // Re-used in next frame
          else if (ip->next_reuse != 0)
            break;
        }
      if (ip != NULL)
        continue; // No repetitions
          
      // If we get here, we can repeat at most `max_repeats' times
      assert(remaining == 0);
      assert(max_repeats >= 0);
      if (max_repeats <= 1)
        continue; // Need at least 2 repetitions, since we cannot
                  // reliably start repetition until 2'nd instance
      start_frame = tail;
      add_frame();
      repeat_count--;
      if (max_repeats < INT_MAX)
        max_repeats--;
      max_lookahead -= tail->num_instructions;
      tail->increment = start_frame->num_instructions-reused_layers_in_frame;
      tail->persistent = start_frame->persistent;
      tail->duration = start_frame->duration;
      tail->pause = start_frame->pause;
      for (ip=start_frame->head; ip != NULL; ip=ip->next)
        { 
          jx_instruction *inst=tail->add_instruction(ip->visible);
          inst->layer_idx = -1; // Evaluate this later on
          inst->iset_idx = ip->iset_idx;
          inst->inum_idx = ip->inum_idx;
          inst->source_dims = ip->source_dims;
          inst->target_dims = ip->target_dims;
          inst->orientation = ip->orientation;
          if ((inst->next_reuse=ip->next_reuse) == 0)
            inst->increment = tail->increment;
        }
      if (max_repeats == INT_MAX)
        { // No restriction on number of repetitions
          if (rept == 0xFFFF)
            tail->repeat_count = -1; // Repeat indefinitely
          else
            tail->repeat_count = repeat_count;
          repeat_count = 0; // Finish looping
        }
      else
        { 
          if (rept == 0xFFFF)
            tail->repeat_count = max_repeats;
          else if (repeat_count > max_repeats)
            { 
              tail->repeat_count = max_repeats;
              repeat_count -= tail->repeat_count;
            }
          else
            { 
              tail->repeat_count = repeat_count;
              repeat_count = 0;
            }
        }
      max_lookahead -= tail->repeat_count*tail->num_instructions;
      if (tail->repeat_count < 0)
        max_lookahead = 0;
    }
  num_parsed_iset_boxes++;  
}

/*****************************************************************************/
/*                     jx_composition::parse_instruction                     */
/*****************************************************************************/

bool
  jx_composition::parse_instruction(bool have_target_pos,
                                    bool have_target_size,
                                    bool have_life_persist,
                                    bool have_source_region,
                                    bool have_orientation,
                                    kdu_uint32 tick, jp2_input_box &box)
{
  if (!(have_target_pos || have_target_size ||
        have_life_persist || have_source_region || have_orientation))
    return false;

  kdu_dims source_dims, target_dims;
  if (have_target_pos)
    {
      kdu_uint32 X0, Y0;
      if (!box.read(X0))
        return false;
      if (!box.read(Y0))
        { KDU_ERROR(e,15); e <<
            KDU_TXT("Malformed Instruction Set (inst) box "
            "found in JPX data source.  Terminated unexpectedly.");
        }
      target_dims.pos.x = (int) X0;
      target_dims.pos.y = (int) Y0;
    }
  else
    target_dims.pos = kdu_coords(0,0);

  if (have_target_size)
    {
      kdu_uint32 XS, YS;
      if (!(box.read(XS) || have_target_pos))
        return false;
      if (!box.read(YS))
        { KDU_ERROR(e,16); e <<
            KDU_TXT("Malformed Instruction Set (inst) box "
            "found in JPX data source.  Terminated unexpectedly.");
        }
      target_dims.size.x = (int) XS;
      target_dims.size.y = (int) YS;
    }
  else
    target_dims.size = size;

  bool persistent = true;
  kdu_uint32 life=0, next_reuse=0;
  if (have_life_persist)
    {
      if (!(box.read(life) || have_target_pos || have_target_size))
        return false;
      if (!box.read(next_reuse))
        { KDU_ERROR(e,17); e <<
            KDU_TXT("Malformed Instruction Set (inst) box "
            "found in JPX data source.  Terminated unexpectedly.");
        }
      if (life & 0x80000000)
        life &= 0x7FFFFFFF;
      else
        persistent = false;
    }

  if (have_source_region)
    {
      kdu_uint32 XC=0, YC=0, WC=0, HC=0;
      if (!(box.read(XC) || have_target_pos || have_target_size ||
            have_life_persist))
        return false;
      if (!(box.read(YC) && box.read(WC) && box.read(HC)))
        { KDU_ERROR(e,18); e <<
            KDU_TXT("Malformed Instruction Set (inst) box "
            "found in JPX data source.  Terminated unexpectedly.");
        }
      source_dims.pos.x = (int) XC;
      source_dims.pos.y = (int) YC;
      source_dims.size.x = (int) WC;
      source_dims.size.y = (int) HC;
    }
  
  jpx_composited_orientation orientation;
  if (have_orientation)
    { 
      kdu_uint32 rot;
      if (!box.read(rot))
        return false;
      if (rot != 0)
        { 
          int r_val = ((int) (rot & ~((kdu_uint32) 16))) - 1;
          if ((r_val < 0) || (r_val > 3))
            { KDU_ERROR(e,0x26021101); e <<
              KDU_TXT("Malformed Instruction Set (inst) box "
                      "found in JPX data source.  ROT parameter must either "
                      "be 0 or else take values in the range 1 to 4 or "
                      "17 to 20.");
            }
          orientation.init(r_val,((rot & 16) != 0));
        }
    }

  if ((tail == NULL) || (tail->duration != 0) || tail->pause)
    add_frame();
  jx_instruction *inst = tail->add_instruction((life != 0) || persistent);
  inst->source_dims = source_dims;
  inst->target_dims = target_dims;
  inst->orientation = orientation;
  inst->layer_idx = -1; // Evaluate this later on.
  inst->next_reuse = (int) next_reuse;
  max_lookahead--;
  if (inst->next_reuse > max_lookahead)
    max_lookahead = inst->next_reuse;
  if (life == 0x7FFFFFFF)
    { tail->pause = true; tail->duration = 0; }
  else
    { 
      tail->pause = false;
      tail->duration = ((kdu_long) life) * ((kdu_long) tick);
    }
  tail->persistent = persistent;
  
  return true;
}

/*****************************************************************************/
/*                    jx_composition::assign_layer_indices                   */
/*****************************************************************************/

void
  jx_composition::assign_layer_indices()
{
  jx_frame *fp, *fpp;
  jx_instruction *ip, *ipp;
  int inum, reuse, layer_idx=0, frame_new_layers;
  for (fp=head; fp != NULL;
       layer_idx+=(fp->repeat_count * frame_new_layers), fp=fp->next)
    for (inum=0, frame_new_layers=0, ip=fp->head;
         ip != NULL; ip=ip->next, inum++)
      {
        if (ip->layer_idx < 0)
          { 
            ip->layer_idx = layer_idx++;
            frame_new_layers++;
          }
        if ((reuse=ip->next_reuse) > 0)
          {
            for (ipp=ip, fpp=fp; reuse > 0; reuse--)
              {
                ipp = ipp->next;
                if (ipp == NULL)
                  {
                    if ((fpp->repeat_count > 0) && (fp != fpp))
                      { // Reusing across a repeated sequence of frames
                        reuse -= fpp->repeat_count*fpp->num_instructions;
                        if (reuse <= 0)
                          { KDU_ERROR(e,19); e <<
                              KDU_TXT("Illegal re-use "
                              "count found in a compositing instruction "
                              "within the JPX composition box.  The specified "
                              "re-use counts found in the box lead to "
                              "multiple conflicting definitions for the "
                              "compositing layer which should be used by a "
                              "particular instruction.");
                          }
                      }
                    fpp=fpp->next;
                    if (fpp == NULL)
                      break;
                    ipp = fpp->head;
                  }
              }
            if ((ipp != NULL) && (reuse == 0))
              ipp->layer_idx = ip->layer_idx;
          }
      }
}

/*****************************************************************************/
/*                    jx_composition::process_instructions                   */
/*****************************************************************************/

void
  jx_composition::process_instructions()
{
  jx_frame *global_persistent_frame = NULL;
  int global_persistent_instructions = 0;
  if (this->container_source != NULL)
    { 
      jx_composition *global = source->get_composition();
      if (global->tail->persistent)
        global_persistent_frame = global->tail;
      else
        global_persistent_frame = global->last_persistent_frame;
      while ((global_persistent_frame != NULL) &&
             (global_persistent_frame->frame_idx >= global->total_frames))
        global_persistent_frame =
          global_persistent_frame->last_persistent_frame;
      if (global_persistent_frame != NULL)
        global_persistent_instructions =
          global_persistent_frame->total_instructions;
    }
  
  bool warning_issued = false;
  int next_frame_idx = this->first_frame_idx;
  kdu_long cum_start_time = this->start_time;
  jx_frame *fp, *fnext;
  for (fp=head; fp != NULL; fp=fnext)
    { 
      fnext = fp->next;
      if ((fp->repeat_count < 0) && (fp->increment == 0))
        { 
          if (!warning_issued)
            { 
              KDU_WARNING(w,0x01081201); w <<
              KDU_TXT("Problem interpreting compositing instructions found "
                      "within a Composition box or Compositing Layer "
                      "Extensions box.  The instructions in question may "
                      "be legal but describe an endless repetition of "
                      "frames, each of which renders exactly the same "
                      "content!  This will cause problems for the "
                      "internal implementation, so the repetition information "
                      "is being modified -- this may produce an incomplete or "
                      "incorrectly interpreted animation.");
            }
          warning_issued = true;
          fp->repeat_count = 0;
        }
      
      fp->start_time = cum_start_time;
      fp->frame_idx = next_frame_idx;
      if (fp->repeat_count >= 0)
        { 
          next_frame_idx += 1+fp->repeat_count;
          cum_start_time += fp->duration * (kdu_long)(1+fp->repeat_count);
        }
      
      jx_instruction *ip, *inext;
      for (ip=fp->head; ip != NULL; ip=inext)
        { 
          inext = ip->next;
          if (ip->visible)
            { 
              if (fp->min_layer_idx < 0)
                fp->min_layer_idx = fp->max_initial_layer_idx = ip->layer_idx;
              else if (ip->layer_idx < fp->min_layer_idx)
                fp->min_layer_idx = ip->layer_idx;
              else if (ip->layer_idx > fp->max_initial_layer_idx)
                fp->max_initial_layer_idx = ip->layer_idx;
              if ((ip->increment > 0) &&
                  (ip->layer_idx > fp->max_repd_layer_idx))
                fp->max_repd_layer_idx = ip->layer_idx;
            }
          else
            { 
              fp->num_instructions--;
              if (ip->prev == NULL)
                { 
                  assert(fp->head == ip);
                  fp->head = inext;
                }
              else
                ip->prev->next = inext;
              if (inext == NULL)
                { 
                  assert(fp->tail == ip);
                  fp->tail = ip->prev;
                }
              else
                inext->prev = ip->prev;
              delete ip;
            }
        }

      if (fp->head == NULL)
        { // Empty frame detected
          assert(fp->num_instructions == 0);
          if (fp->prev == NULL)
            {
              assert(head == fp);
              head = fnext;
            }
          else
            {
              fp->prev->next = fnext;
              fp->prev->duration += fp->duration;
              fp->prev->pause |= fp->pause;
            }
          if (fnext == NULL)
            {
              assert(tail == fp);
              tail = fp->prev;
            }
          else
            fnext->prev = fp->prev;
          delete fp;
        }
      else
        { 
          fp->total_instructions = fp->num_instructions;
          if (fp->last_persistent_frame != NULL)
            fp->total_instructions +=
              fp->last_persistent_frame->total_instructions;
          else if (global_persistent_frame != NULL)
            { 
              fp->last_persistent_frame = global_persistent_frame;
              fp->total_instructions += global_persistent_instructions;
            }
        }
    }
}

/*****************************************************************************/
/*              jx_composition::propagate_frame_and_track_info               */
/*****************************************************************************/

void jx_composition::propagate_frame_and_track_info()
{
  assert((size.x > 0) && (size.y > 0) &&
         ((container_source == NULL) || (first_frame_idx > 0)));
  if (total_frames < 1)
    return;
  int frame_count = this->first_frame_idx + this->total_frames;
  jx_composition *cscan = last_in_track;
  assert(cscan != NULL);
  if ((cscan != this) &&
      (max_tracks_noted < cscan->container_source->get_max_tracks()))
    { // Start propagation from scratch
      max_tracks_noted = last_in_track->container_source->get_max_tracks();
      cscan = this->next_in_track;
    }
  else if (cscan == this)
    cscan = this->next_in_track;
  else
    { 
      frame_count = cscan->first_frame_idx + cscan->total_frames;
      cscan = cscan->next_in_track;
    }
  for (; cscan != NULL; cscan=cscan->next_in_track)
    { 
      this->last_in_track = cscan;
      jx_composition *tscan = cscan;
      for (; tscan != NULL; tscan=tscan->track_next)
        { 
          tscan->size = this->size;
          tscan->loop_count = this->loop_count;
          tscan->first_frame_idx = frame_count;
        }
      frame_count += cscan->total_frames;
      if (max_tracks_noted < cscan->container_source->get_max_tracks())
        max_tracks_noted = cscan->container_source->get_max_tracks();
    }
}

/*****************************************************************************/
/*                       jx_composition::count_frames                        */
/*****************************************************************************/

int jx_composition::count_frames(int known_layers, kdu_long max_duration)
{ 
  int count = 0;
  int frames_left = total_frames;
  kdu_long duration = 0;
  jx_frame *fp=head;
  assert(max_duration >= 0);
  for (; fp != NULL; fp=fp->next)
    { 
      int max_repeat = fp->repeat_count;
      if (fp->max_initial_layer_idx >= known_layers)
        break; // Frame cannot be accommodated at all by `known_layers'
      if (max_repeat < 0)
        { // Indefinite repetition
          assert((fp->increment > 0) && (fp->max_repd_layer_idx >= 0));
          int delta = known_layers - fp->max_repd_layer_idx;
          assert(delta > 0); // Or else above test for max_initial_layer failed
          max_repeat = (delta-1) / fp->increment;
        }
      else if ((max_repeat != 0) && (fp->max_repd_layer_idx >= 0))
        { 
          assert(fp->increment > 0);
          int delta = known_layers - fp->max_repd_layer_idx;
          assert(delta > 0); // Or else above test for max_initial_layer failed
          if (delta <= (fp->increment*max_repeat))
            max_repeat = (delta-1) / fp->increment;
        }
      if ((frames_left > 0) && (max_repeat >= frames_left))
        max_repeat = frames_left-1;
      count += 1+max_repeat;
      duration += fp->duration * (1+max_repeat);
      if (duration > max_duration)
        { // Adjust `count' downwards
          assert(fp->duration > 0);
          kdu_long delta_t = duration - max_duration;
          count -= 1 + (int)((delta_t-1) / fp->duration);
          break;
        }
      else if (max_repeat != fp->repeat_count)
        break;
    }
  if (fp == NULL)
    total_frames = count; // Frames were not truncated
  else if ((count == 0) && (fp->duration <= max_duration))
    { // We can assume the first frame exists, even if we have not found the
      // compositing layers yet.
      count = 1;
      duration = fp->duration;
    }
  if (duration > total_duration)
    total_duration = duration;
  return count;
}

/*****************************************************************************/
/*                 jx_composition::need_more_instructions                    */
/*****************************************************************************/

bool
  jx_composition::need_more_instructions(int min_frame_idx, int max_frame_idx)
{
  assert(this->prev_in_track == NULL); // Must be starting with top-level obj
  if (!(is_complete || finish()))
    return false; // Need all of the top-level Composition box's instructions
  if (total_frames > 0)
    { // Number of top-level frames is known
      if ((max_frame_idx >= 0) && (max_frame_idx < total_frames))
        return false; // We have all the information we need
    }
  else
    { 
      if (max_frame_idx >= 0)
        { // Need to figure out how many top-level frames we know about already
          int known_layers = source->get_num_top_layers();
          if (max_frame_idx < count_frames(known_layers))
            return false; // We have all the information we need
        }
      else
        return true; // Have to assume we need all instructions from all
                     // remaining containers, because cannot yet work out
                     // the relationship between frame indices and containers.
    }
  this->propagate_frame_and_track_info();
  jx_composition *cscan;
  for (cscan=next_in_track; cscan != NULL; cscan=cscan->next_in_track)
    { 
      if ((cscan->total_frames > 0) &&
          (min_frame_idx >= (cscan->total_frames+cscan->first_frame_idx)))
        continue; // Not interested in this container's tracks
      jx_composition *tscan;
      for (tscan=cscan; tscan != NULL; tscan=tscan->track_next)
        if (!(tscan->is_complete || tscan->finish()))
          return true; 
      if ((max_frame_idx >= 0) &&
          (max_frame_idx < (cscan->total_frames+cscan->first_frame_idx)))
        return false; // No need to look any further
    }
  
  // If we get here, we still have not found all the frames of interest, but
  // we have run out of containers.  Moreover, we have not encountered a
  // container with indefinite repetitions.  It follows that there may be
  // more containers (with unparsed instructions) in the source, so long as
  // the top level of the file is not yet complete.
  return !source->is_top_level_complete();
}

/*****************************************************************************/
/*                        jx_composition::find_frame                         */
/*****************************************************************************/

jx_frame *
  jx_composition::find_frame(int frame_idx, int known_layers, int &repeat_idx)
{
  if (frame_idx == 0)
    known_layers = INT_MAX;
  for (jx_frame *fp=head; fp != NULL; fp=fp->next)
    { 
      if ((fp->repeat_count >= 0) && (frame_idx > fp->repeat_count))
        { 
          frame_idx -= 1+fp->repeat_count;
          continue;
        }
      if (fp->max_initial_layer_idx >= known_layers)
        return NULL;
      if (fp->max_repd_layer_idx >= 0)
        { 
          int delta = known_layers - fp->max_repd_layer_idx;
          assert(delta > 0); // Or else `max_initial_layer_idx' test failed
          if ((frame_idx*fp->increment) >= delta)
            return NULL;
        }
      repeat_idx = frame_idx;
      return fp;
    }
  return NULL;
}

/*****************************************************************************/
/*                        jx_composition::find_match                         */
/*****************************************************************************/

jx_frame *
  jx_composition::find_match(const int layers[], int num_layers,
                             int &repeat_idx, int &instruction_idx,
                             jx_frame *start_frm, int min_rept,
                             int max_container_rep, bool match_all)
{
  if (abs_layer_start > 0)
    assert((abs_layers_per_rep > 0) &&
           (abs_layers_per_rep <= abs_layer_rep_stride));
  jx_frame *fp = start_frm;
  if (fp == NULL)
    { 
      fp = head;
      min_rept = 0;
    }
  else
    assert(fp->owner == this);
  bool have_possible_layers = true; // Until proven otherwise
  for (; (fp != NULL) && have_possible_layers; fp=fp->next, min_rept=0)
    { 
      int max_repeat = fp->repeat_count;
      if (max_repeat < 0)
        max_repeat = INT_MAX;
      int best_instruction_idx = -1; // Becomes non-negative if we find a match
      int best_repeat_idx = -1; // Becomes non-negqtive if we find a match
      have_possible_layers = false;
      for (int w=0; w < num_layers; w++)
        { 
          int idx_increment=0, idx=layers[w];
          int lcm_idx=1, lcm_rept=1, lcm;
          if (abs_layer_start > 0)
            { // Convert `idx' to a relative layer index
              if ((idx -= abs_layer_start) < 0)
                { // This layer does not belong to this composition object
                  if (match_all)
                    return NULL;
                  continue;
                }
              if ((max_container_rep > 0) && (idx < abs_layers_per_rep))
                {  // Can match instructions whose relative layer differs from
                   // `idx' by any multiple of `idx_increment'.  We also need
                   // to compute `lcm_idx' and `lcm_rept' such that
                   // lcm_idx*idx_increment = lcm_rept*fp->increment = lcm.
                  idx_increment = abs_layers_per_rep;
                  if (fp->increment != 0)
                    { 
                      for (lcm_idx=1, lcm=idx_increment;
                           (lcm % fp->increment) != 0; lcm_idx++)
                        lcm += abs_layers_per_rep;
                      lcm_rept = lcm / fp->increment;
                    }
                }
              else
                { 
                  int rep = idx / abs_layer_rep_stride;
                  idx -= rep * abs_layer_rep_stride;
                  if (idx >= abs_layers_per_rep)
                    { // This layer does not belong to this composition object
                      if (match_all)
                        return NULL;
                      continue; 
                    }
                  idx += rep * abs_layers_per_rep;
                }
            }
          have_possible_layers = true;
          
          int icount=0, rept=0;
          jx_instruction *ip=fp->head;
          for (; ip != NULL; ip=ip->next, icount++)
            { 
              int delta = idx - ip->layer_idx;
              if (ip->increment == 0)
                { // All repetitions of the frame use the same layer index
                  rept = min_rept; // If we have a match this will be it
                  if (delta == 0)
                    break; // Found a match
                  if ((idx_increment > 0) && (delta < 0))
                    { 
                      int k = (-delta) / idx_increment; // Container rep idx
                      if ((k <= max_container_rep) &&
                          ((k*idx_increment+delta) == 0))
                        break; // Found a match
                    }
                }
              else if (idx_increment == 0)
                { // No container replication
                  rept = 0;
                  if (delta >= ip->increment)
                    { 
                      rept = delta / ip->increment;
                      delta -= rept*ip->increment;
                    }
                  if ((delta==0) && (rept >= min_rept) && (rept <= max_repeat))
                    break; // Found a match
                }
              else
                { // Difficult case of both container & instruction replication
                  rept = 0;
                  int k = 0; // Will hold container rep idx
                  while ((delta != 0) && (rept <= lcm_rept))
                    if (delta < 0)
                      { delta += idx_increment; k++; }
                    else
                      { delta -= ip->increment; rept++; }
                  if (delta != 0)
                    continue;
                  // At this point, `rept' is the minimum repeat index for a
                  // match and `k' is the associated container repetition index
                  if (rept < min_rept)
                    { 
                      int num_lcms = 1 + ((min_rept-rept-1) / lcm_rept);
                      rept += num_lcms * lcm_rept;
                      k += num_lcms * lcm_idx;
                    }
                  if ((rept <= max_repeat) && (k <= max_container_rep))
                    break; // Found a match
                }
            }
          
          if (ip != NULL)
            { // This layer matches
              if ((best_repeat_idx < 0) ||
                  ((rept < best_repeat_idx) && !match_all))
                { // We have found a first or a better match
                  best_repeat_idx = rept;
                  best_instruction_idx = icount;
                  if ((rept == min_rept) && !match_all)
                    break; // Cannot do any better than this
                }
              else if (match_all && (rept != repeat_idx))
                { // Inconsistent repetition indices for different layers;
                  // this test is not necessarily comprehensive.  It may be
                  // that a larger repetition index can work for everybody,
                  // but this could lead to a truly massive search to
                  // account for a very weird case.
                  best_repeat_idx = best_instruction_idx = -1;
                  break;
                }
            }
          else if (match_all)
            { // At least one layer does not match any repetition of the frame
              best_repeat_idx = best_instruction_idx = -1;
              break;
            }
        }
      
      if (best_repeat_idx >= 0)
        { 
          assert(best_instruction_idx >= 0);
          repeat_idx = best_repeat_idx;
          instruction_idx = best_instruction_idx;
          return fp;
        }
    }
  return NULL;
}

/*****************************************************************************/
/*                      jx_composition::reverse_match                        */
/*****************************************************************************/

jx_frame *
  jx_composition::reverse_match(const int layers[], int num_layers,
                                int &repeat_idx, int &instruction_idx,
                                jx_frame *start_frm, int max_rept,
                                int max_container_rep, bool match_all)
{
  if (abs_layer_start > 0)
    assert((abs_layers_per_rep > 0) &&
           (abs_layers_per_rep <= abs_layer_rep_stride));
  
  // Start by figuring out where to start the search -- this is more tricky
  // than in the case of the forward search implemented by `find_match'.
  jx_frame *fp = start_frm;
  if (fp == NULL)
    { 
      fp = tail;
      max_rept = 0;
      if (fp->repeat_count >= 0)
        max_rept = fp->repeat_count;
      else if (fp->max_repd_layer_idx >= 0) // Frame contains repeated layers
        { // Figure out `max_rept' based on the last compositing layer index
          int w, idx, max_rel_layer = 0;
          for (w=0; w < num_layers; w++)
            { 
              idx = layers[w];
              if (abs_layer_start > 0)
                { // Convert `idx' to a relative layer index
                  if ((idx -= abs_layer_start) < 0)
                    continue; // Does not belong to this track
                  if (idx < abs_layers_per_rep)
                    idx += max_container_rep * abs_layers_per_rep;
                  else
                    { 
                      int rep = idx / abs_layer_rep_stride;
                      idx -= rep * abs_layer_rep_stride;
                      if ((idx < 0) || (idx >= abs_layers_per_rep))
                        continue; // Does not belong to this track
                      idx += rep * abs_layers_per_rep;
                    }
                }
              if (idx > max_rel_layer)
                max_rel_layer = idx;
            }
          int delta = max_rel_layer - fp->min_layer_idx; // Conservative
          if (delta >= fp->increment)
            max_rept = 1 + ((delta-1) / fp->increment);
        }
    }
  else
    assert(fp->owner == this);
  
  // Now for the search itself
  bool have_possible_layers = true; // Until proven otherwise
  for (; (fp != NULL) && have_possible_layers;
       fp=fp->prev, max_rept=fp->repeat_count)
    { 
      assert(max_rept >= 0); // Multiple fp's with indefinite repetition???
      int best_instruction_idx = -1; // Becomes non-negative if we find a match
      int best_repeat_idx = -1; // Becomes non-negative if we find a match
      have_possible_layers = false;
      for (int w=0; w < num_layers; w++)
        { 
          int idx_increment=0, idx=layers[w];
          int lcm_idx=1, lcm_rept=1, lcm;
          if (abs_layer_start > 0)
            { // Convert `idx' to a relative layer index
              if ((idx -= abs_layer_start) < 0)
                { // This layer does not belong to this composition
                  if (match_all)
                    return NULL;
                  continue;
                }
              if ((max_container_rep > 0) && (idx < abs_layers_per_rep))
                {  // Can match instructions whose relative layer differs from
                  // `idx' by any multiple of `idx_increment'.  We also need
                  // to compute `lcm_idx' and `lcm_rept' such that
                  // lcm_idx*idx_increment = lcm_rept*fp->increment = lcm.
                  idx_increment = abs_layers_per_rep;
                  if (fp->increment != 0)
                    { 
                      for (lcm_idx=1, lcm=idx_increment;
                           (lcm % fp->increment) != 0; lcm_idx++)
                        lcm += abs_layers_per_rep;
                      lcm_rept = lcm / fp->increment;
                    }
                }
              else
                { 
                  int rep = idx / abs_layer_rep_stride;
                  idx -= rep * abs_layer_rep_stride;
                  if (idx >= abs_layers_per_rep)
                    { // This layer does not belong to this composition
                      if (match_all)
                        return NULL;
                      continue; 
                    }
                  idx += rep * abs_layers_per_rep;
                }
            }
          have_possible_layers = true;
          
          int icount=0, rept=0;
          jx_instruction *ip=fp->head;
          for (; ip != NULL; ip=ip->next, icount++)
            { 
              int delta = idx - ip->layer_idx;
              if (ip->increment == 0)
                { // All repetitions of the frame use the same layer index
                  rept = max_rept; // If we have a match this will be it
                  if (delta == 0)
                    break; // Found a match
                  if ((idx_increment > 0) && (delta < 0))
                    { 
                      int k = (-delta) / idx_increment; // Container rep idx
                      if ((k <= max_container_rep) &&
                          ((k*idx_increment+delta) == 0))
                        break; // Found a match
                    }
                }
              else if (idx_increment == 0)
                { // No container replication
                  rept = 0;
                  if (delta >= ip->increment)
                    { 
                      rept = delta / ip->increment;
                      delta -= rept*ip->increment;
                    }
                  if ((delta==0) && (rept <= max_rept))
                    break; // Found a match
                }
              else
                { // Difficult case of both container & instruction replication
                  rept = 0;
                  int k = 0; // Will hold container rep idx
                  while ((delta != 0) && (rept <= lcm_rept))
                    if (delta < 0)
                      { delta += idx_increment; k++; }
                    else
                      { delta -= ip->increment; rept++; }
                  if (delta != 0)
                    continue;
                  // At this point, `rept' is the minimum repeat index for a
                  // match and `k' is the associated container repetition index
                  if ((rept > max_rept) || (k > max_container_rep))
                    continue; // Not an acceptable match
                  if (rept < max_rept)
                    { // Add as may LCM's as possible
                      int num_lcms = (max_rept-rept) / lcm_rept;
                      rept += num_lcms * lcm_rept;
                      k += num_lcms * lcm_idx;
                      if (k > max_container_rep)
                        { // Max LCM's to add is bounded by max container reps
                          num_lcms = 1 + (k-max_container_rep-1) / lcm_idx;
                          rept -= num_lcms * lcm_rept;
                          k -= num_lcms * lcm_idx;
                        }
                      assert((rept <= max_rept) && (k <= max_container_rep));
                    }
                  break; // Found a match
                }
            }          
          
          if (ip != NULL)
            { // This layer matches
              if ((best_repeat_idx < 0) ||
                  ((rept > best_repeat_idx) && !match_all))
                { // We have found a first or a better match
                  best_repeat_idx = rept;
                  best_instruction_idx = icount;
                  if ((rept == max_rept) && !match_all)
                    break; // Cannot do any better than this
                }
              else if (match_all && (rept != repeat_idx))
                { // Inconsistent repetition indices for different layers;
                  // this test is not necessarily comprehensive.  It may be
                  // that a larger repetition index can work for everybody,
                  // but this could lead to a truly massive search to
                  // account for a very weird case.                  
                  best_repeat_idx = best_instruction_idx = -1;
                  break;
                }
            }
          else if (match_all)
            { // At least one layer does not match any repetition of the frame
              best_repeat_idx = best_instruction_idx = -1;
              break;
            }
        }
      
      if (best_repeat_idx >= 0)
        { 
          assert(best_instruction_idx >= 0);
          repeat_idx = best_repeat_idx;
          instruction_idx = best_instruction_idx;
          return fp;
        }
    }
  return NULL;
}

/*****************************************************************************/
/*                         jx_composition::finalize                          */
/*****************************************************************************/

void
  jx_composition::finalize()
{
  if (is_complete)
    return;
  is_complete = true;
  
  if (prev_in_track != NULL)
    { 
      this->loop_count = prev_in_track->loop_count;
      this->size = prev_in_track->size;
    }

  if (head == NULL)
    return;

  assert(abs_layers_per_rep > 0); // Otherwise `set_layer_mapping' not called
  int num_layers = abs_layers_per_rep * abs_layer_reps; // May be 0 if inside
                                        // an indefinitely repeated container
  
  // Start by going through all frames and instructions, verifying that each
  // frame has at least one instruction, expanding repeated frames (unless
  // `abs_layer_reps' is 0), and terminating the sequence once we find the
  // first frame which uses a non-existent compositing layer.
  jx_frame *fp;
  jx_instruction *ip;
  bool keep_repeat = false;
  for (fp=head; fp != NULL; fp=fp->next)
    { 
      if (fp->head == NULL)
        { KDU_ERROR_DEV(e,20); e <<
            KDU_TXT("You must add at least one compositing "
            "instruction to every frame created using "
            "`jpx_composition::add_frame'.");
        }
      if ((fp->repeat_count != 0) && !keep_repeat)
        { // Split off the first frame from the rest of the repeating sequence
          jx_frame *new_fp = new jx_frame(this);
          new_fp->persistent = fp->persistent;
          new_fp->duration = fp->duration;
          new_fp->pause = fp->pause;
          if (fp->repeat_count < 0)
            new_fp->repeat_count = -1;
          else
            new_fp->repeat_count = fp->repeat_count-1;
          keep_repeat = ((new_fp->next == NULL) && (abs_layer_reps == 0));
             // We can keep the repeat on the last frame, if we are in the
             // final container with an unknown number of repetitions.
          fp->repeat_count = 0;
          jx_instruction *new_ip;
          for (ip=fp->head; ip != NULL; ip=ip->next)
            { 
              assert(ip->visible);
              new_ip = new_fp->add_instruction(true);
              new_ip->layer_idx = ip->layer_idx + ip->increment;
              new_ip->increment = ip->increment;
              new_ip->source_dims = ip->source_dims;
              new_ip->target_dims = ip->target_dims;
              new_ip->orientation = ip->orientation;
            }
          new_fp->next = fp->next;
          new_fp->prev = fp;
          fp->next = new_fp;
          if (new_fp->next != NULL)
            new_fp->next->prev = new_fp;
          else
            {
              assert(fp == tail);
              tail = new_fp;
            }
        }
      else if (fp->repeat_count > 0xFFFF)
        { 
          assert(keep_repeat && (fp->next == NULL));
          fp->repeat_count = -1;
        }
      bool have_invalid_layer = false;
      for (ip=fp->head; ip != NULL; ip=ip->next)
        {
          ip->next_reuse = 0; // Just in case not properly initialized
          if ((ip->layer_idx < 0) ||
              ((abs_layer_reps > 0) && // Otherwise inside an indefinite track
               (ip->layer_idx >= num_layers)))
            have_invalid_layer = true;
          kdu_coords lim = ip->target_dims.pos + ip->target_dims.size;
          if (lim.x > size.x)
            size.x = lim.x;
          if (lim.y > size.y)
            size.y = lim.y;
        }
      if (have_invalid_layer && (fp != head))
        break;

      if (abs_layer_reps > 0)
        { 
          total_frames++;
          if (fp->repeat_count >= 0)
            total_duration += fp->duration * (1 + (kdu_long) fp->repeat_count);
        }
    }

  if (fp != NULL)
    { // Delete this and all subsequent frames; can't play them
      tail = fp->prev;
      assert(tail != NULL);
      while ((fp=tail->next) != NULL)
        {
          tail->next = fp->next;
          delete fp;
        }
    }

  // Now introduce invisibles so as to get the layer sequence right
  // At this point, we will set `first_use' to true whenever we encounter
  // an instruction which uses a compositing layer for the first time.
  // This will help make the ensuing algorithm for finding the `next_reuse'
  // links more efficient.
  int layer_idx=0;
  for (fp=head; fp != NULL; fp=fp->next)
    {
      for (ip=fp->head; ip != NULL; ip=ip->next)
        {
          while (ip->layer_idx > layer_idx)
            {
              jx_instruction *new_ip = new jx_instruction;
              new_ip->visible = false;
              new_ip->layer_idx = layer_idx++;
              new_ip->first_use = true;
              new_ip->next = ip;
              new_ip->prev = ip->prev;
              ip->prev = new_ip;
              if (ip == fp->head)
                {
                  fp->head = new_ip;
                  assert(new_ip->prev == NULL);
                }
              else
                new_ip->prev->next = new_ip;
              fp->num_instructions++;
            }
          if (ip->layer_idx == layer_idx)
            {
              ip->first_use = true;
              layer_idx++;
            }
        }
    }
  
  if ((layer_idx < num_layers) && (abs_layer_start > 0))
    { KDU_ERROR(e,0x01071201); e <<
      KDU_TXT("You have defined one or more presentation tracks for a "
              "JPX container, added through `jpx_target::add_container', "
              "but at least one of these presentation tracks does not "
              "contain sufficient composition instructions to use all "
              "of the container's compositing layers that are assigned "
              "to the presentation track.  The resulting JPX file would "
              "not be compliant with AMD3 of IS15444-2.");
    }

  // Finally, figure out the complete set of `next_reuse' links by walking
  // backwards through the entire sequence of instructions
  jx_frame *fscan;
  jx_instruction *iscan;
  int igap;
  for (fp=tail; fp != NULL; fp=fp->prev)
    {
      for (ip=fp->tail; ip != NULL; ip=ip->prev)
        {
          if (ip->first_use)
            continue;
          iscan = ip->prev;
          fscan = fp;
          igap = 1;
          while ((iscan == NULL) || (iscan->layer_idx != ip->layer_idx))
            {
              if (iscan == NULL)
                {
                  fscan = fscan->prev;
                  if (fscan == NULL)
                    break;
                  iscan = fscan->tail;
                }
              else
                {
                  iscan = iscan->prev;
                  igap++;
                }
            }
          assert(iscan != NULL); // Should not be possible if above algorithm
                     // for assigning invisible instructions worked correctly
          iscan->next_reuse = igap;
        }
    }
  
  if (tail->repeat_count != 0)
    { // We split off the first instance of a final repeating frame so that
      // we could figure out invisibles and re-use counts.  We can now
      // copy the re-use factors across to the actual repeating part.
      assert(keep_repeat);
      fp = tail->prev;
      for (ip=fp->head, iscan=tail->head; ip != NULL;
           ip=ip->next, iscan=iscan->next)
        iscan->next_reuse = ip->next_reuse;
    }
}

/*****************************************************************************/
/*                    jx_composition::adjust_compatibility                   */
/*****************************************************************************/

void
  jx_composition::adjust_compatibility(jx_compatibility *compatibility)
{
  if (!is_complete)
    finalize();
  if (abs_layer_start != 0)
    return; // Composition compatibility adjustments are all made from the
            // top-level `jx_composition' object.  If there are presentation
            // tracks in JPX containers, these will be discovered by following
            // the links created during container finalization.  Of course,
            // this mechanism cannot incorporate the impact of containers
            // added after the first call to `jx_target::write_headers', but
            // this is the case for all objects that might impact
            // compatibility.

  jx_frame *fp;
  jx_instruction *ip;

  if ((head == NULL) || (head->head == head->tail))
    compatibility->add_standard_feature(JPX_SF_COMPOSITING_NOT_REQUIRED);
  assert(abs_layers_per_rep > 0); // Otherwise `set_layer_mapping' not called
  int num_layers = abs_layers_per_rep * abs_layer_reps;
  if (head == NULL)
    {
      if (num_layers > 1)
        compatibility->add_standard_feature(
          JPX_SF_MULTIPLE_LAYERS_NO_COMPOSITING_OR_ANIMATION);
      return;
    }
  compatibility->add_standard_feature(JPX_SF_COMPOSITING_USED);
  if (head == tail)
    compatibility->add_standard_feature(JPX_SF_NO_ANIMATION);
  else
    { // Check first layer coverage, layer reuse and frame persistence
      bool first_layer_covers = true;
      bool layers_reused = false;
      bool frames_all_persistent = true;
      for (jx_composition *cscan=this; cscan != NULL;
           cscan=cscan->next_in_track)
        for (jx_composition *tscan=cscan; tscan != NULL;
             tscan=tscan->track_next)
          for (fp=tscan->head; fp != NULL; fp=fp->next)
            { 
              if ((fp->head->layer_idx != 0) ||
                  (fp->head->target_dims.pos.x != 0) ||
                  (fp->head->target_dims.pos.y != 0) ||
                  (fp->head->target_dims.size != size))
                first_layer_covers = false;
              for (ip=fp->head; ip != NULL; ip=ip->next)
                if (ip->next_reuse > 0)
                  layers_reused = true;
              if (!fp->persistent)
                frames_all_persistent = false;
            }
      if (first_layer_covers)
        compatibility->add_standard_feature(
          JPX_SF_ANIMATED_COVERED_BY_FIRST_LAYER);
      else
        compatibility->add_standard_feature(
          JPX_SF_ANIMATED_NOT_COVERED_BY_FIRST_LAYER);
      if (layers_reused)
        compatibility->add_standard_feature(JPX_SF_ANIMATED_LAYERS_REUSED);
      else
        compatibility->add_standard_feature(JPX_SF_ANIMATED_LAYERS_NOT_REUSED);
      if (frames_all_persistent)
        compatibility->add_standard_feature(
          JPX_SF_ANIMATED_PERSISTENT_FRAMES);
      else
        compatibility->add_standard_feature(
          JPX_SF_ANIMATED_NON_PERSISTENT_FRAMES);
    }

  // Check for scaling
  bool scaling_between_layers=false;
  for (fp=head; fp != NULL; fp=fp->next)
    for (ip=fp->head; ip != NULL; ip=ip->next)
      if (ip->source_dims.size != ip->target_dims.size)
        scaling_between_layers = true;
  if (scaling_between_layers)
    compatibility->add_standard_feature(JPX_SF_SCALING_BETWEEN_LAYERS);
}

/*****************************************************************************/
/*                         jx_composition::save_box                          */
/*****************************************************************************/

void
  jx_composition::save_box(jx_target *owner)
{
  if (!is_complete)
    finalize();

  if (head == NULL)
    return; // No composition information provided; do not write the box.

  owner->open_top_box(&comp_out,jp2_composition_4cc);
  jp2_output_box sub;

  sub.open(&comp_out,jp2_comp_options_4cc);
  kdu_uint32 height = (kdu_uint32) size.y;
  kdu_uint32 width = (kdu_uint32) size.x;
  kdu_byte loop = (kdu_byte)(loop_count-1);
  sub.write(height);
  sub.write(width);
  sub.write(loop);
  sub.close();
  
  save_instructions(&comp_out);
  comp_out.close();
}

/*****************************************************************************/
/*                     jx_composition::save_instructions                     */
/*****************************************************************************/

void
  jx_composition::save_instructions(jp2_output_box *super_box)
{
  jx_frame *fp, *fnext;
  jx_instruction *ip;
  int iset_idx=0;
  for (fp=head; fp != NULL; fp=fnext, iset_idx++)
    { 
      jp2_output_box sub;
      sub.open(super_box,jp2_comp_instruction_set_4cc);
      kdu_uint16 flags = 39; // Record all fields except possibly orientation
      kdu_uint32 tick = 1; // All other values are utterly unnecessary!
      kdu_uint16 rept = (kdu_uint16) fp->repeat_count;
      
      // See if instructions need to include orientation field
      bool have_orientation = false;
      for (ip=fp->head; ip != NULL; ip=ip->next)
        if (ip->orientation.is_non_trivial())
          { 
            have_orientation = true;
            flags |= 64;
            break;
          }
      fnext = fp->next;
      if (rept == 0)
        { // Calculate number of repetitions by scanning future frames for
          // similarities and merging them in.
          for (; (fnext != NULL) && (fnext->repeat_count == 0);
               fnext=fnext->next, rept++)
            { 
              if ((rept == 0xFFFE) ||
                  (fnext->duration != fp->duration) ||
                  (fnext->pause != fp->pause) ||
                  (fnext->persistent != fp->persistent) ||
                  (fnext->num_instructions != fp->num_instructions))
                break;
              jx_instruction *ip2;
              for (ip=fp->head, ip2=fnext->head;
                   (ip != NULL) && (ip2 != NULL);
                   ip=ip->next, ip2=ip2->next)
                if ((ip->next_reuse != ip2->next_reuse) ||
                    (ip->visible != ip2->visible) ||
                    (ip->source_dims != ip2->source_dims) ||
                    (ip->target_dims != ip2->target_dims) ||
                    (ip->orientation != ip2->orientation) ||
                    !ip2->first_use)
                  break;
              if ((ip != NULL) || (ip2 != NULL))
                break; // Frame is not compatible with previous one
            }
        }
      
      sub.write(flags);
      sub.write(rept);
      sub.write(tick);
      int inum_idx = 0;
      for (ip=fp->head; ip != NULL; ip=ip->next)
        { // Record instruction
          kdu_uint32 life = 0;
          ip->iset_idx = iset_idx;
          ip->inum_idx = inum_idx++;
          if (ip != fp->tail)
            { // Non-terminal instructions in frame have duration=0
              if (ip->visible)
                life = 0x80000000; // Add `persists' flag
            }
          else
            { // Terminate frame
              if (fp->persistent)
                {
                  life = 0x80000000;
                  assert(ip->visible); // Can't have invisibles at end of
                                       // persistent frame
                }
              if (fp->pause)
                life |= 0x7FFFFFFF;
              else if (fp->duration > 0x7FFFFFFE)
                life |= 0x7FFFFFFE; // Avoid misleading value
              else
                life |= ((kdu_uint32) fp->duration) & 0x7FFFFFFF;
            }

          sub.write((kdu_uint32) ip->target_dims.pos.x);
          sub.write((kdu_uint32) ip->target_dims.pos.y);
          sub.write((kdu_uint32) ip->target_dims.size.x);
          sub.write((kdu_uint32) ip->target_dims.size.y);
          sub.write(life);
          sub.write((kdu_uint32) ip->next_reuse);
          sub.write((kdu_uint32) ip->source_dims.pos.x);
          sub.write((kdu_uint32) ip->source_dims.pos.y);
          sub.write((kdu_uint32) ip->source_dims.size.x);
          sub.write((kdu_uint32) ip->source_dims.size.y);
          if (have_orientation)
            { // Write the 32-bit orientation field
              kdu_uint32 rot_val=1;
              bool hflip = ip->orientation.hflip;
              if (ip->orientation.transpose)
                { 
                  if (ip->orientation.vflip)
                    rot_val = 4; // 270 degrees clockwise
                  else
                    { rot_val = 2; hflip = !hflip; } // 90 degrees clockwise
                }
              else if (ip->orientation.vflip)
                { rot_val = 3; hflip = !hflip; } // 180 degrees clockwise
              else
                rot_val = 1; // 0 degrees rotation
              if (hflip)
                rot_val |= 16;
              sub.write(rot_val);
            }
        }
      sub.close();
    }
}


/* ========================================================================= */
/*                             jpx_composition                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      jpx_composition::get_global_info                     */
/*****************************************************************************/

int
  jpx_composition::get_global_info(kdu_coords &size) const
{
  if ((state == NULL) || (state->size.x < 1) || (state->size.y < 1))
    return -1;
  size = state->size;
  return state->loop_count;
}

/*****************************************************************************/
/*                      jpx_composition::get_track_idx                       */
/*****************************************************************************/

kdu_uint32 jpx_composition::get_track_idx()
{
  return (state == NULL)?0:state->track_idx;
}

/*****************************************************************************/
/*                      jpx_composition::count_tracks                        */
/*****************************************************************************/

bool jpx_composition::count_tracks(kdu_uint32 &count, bool global_only)
{
  count = 0;
  if ((state == NULL) || (state->source == NULL) ||
      (state->container_source != NULL) || !state->is_complete)
    return false;
  if ((state->size.x < 1) || (state->size.y < 1))
    return true;
  bool count_known = state->source->find_all_container_info();
  if (state->total_frames > 0)
    state->propagate_frame_and_track_info();
  count = state->max_tracks_noted;
  if ((count == 0) && !global_only)
    count = 1;
  if (count_known)
    count_known = true;
  return count_known;
}

/*****************************************************************************/
/*                   jpx_composition::count_track_frames                     */
/*****************************************************************************/

bool
  jpx_composition::count_track_frames(kdu_uint32 track_idx, int &count)
{
  count = 0;
  if ((state == NULL) || (state->source == NULL) ||
      (state->container_source != NULL) || !state->is_complete)
    return false;
  if ((state->size.x < 1) || (state->size.y < 1))
    return true;
  
  bool containers_known = state->source->find_all_container_info();
  if (state->total_frames > 0)
    state->propagate_frame_and_track_info();
  if ((track_idx == 0) || (state->total_frames == 0))
    { // If `state->total_frames' is 0 we have not found any JPX containers
      // yet, so there is no point in looking past `state'
      if (state->abs_layers_per_rep == 0)
        { // Number of top-level frames cannot yet be known
          int known_layers = state->source->get_num_top_layers();
          count = state->count_frames(known_layers);
        }
      else
        count = state->total_frames;      
      if (state->total_frames > 0)
        return ((track_idx==0) || containers_known);
      else
        return false;
    }
  
  jx_composition *last = state->last_in_track;
  assert(last != NULL);
  count = last->first_frame_idx;
  bool count_known = containers_known;
  if (last->total_frames == 0)
    { 
      int known_layers = 0;
      assert(last->container_source != NULL);
      while ((last->track_idx != track_idx) && (last->track_next != NULL))
        last = last->track_next;
      if (!last->finish())
        return false; // Can't count frames without parsed instructions
      assert(containers_known); // We must have found all containers
      count_known = state->source->find_all_streams();
      int reps = last->container_source->get_known_reps();
      known_layers = reps*last->abs_layers_per_rep;
      int last_frames = last->count_frames(known_layers);
      if (count_known)
        last->total_frames = last_frames;
      else if ((last->total_frames > 0) && (last->total_frames==last_frames))
        count_known = true;
      count += last_frames;
    }
  else
    count += last->total_frames;
  return count_known;
}

/*****************************************************************************/
/*                    jpx_composition::count_track_time                      */
/*****************************************************************************/

bool
  jpx_composition::count_track_time(kdu_uint32 track_idx, kdu_long &count)
{
  count = 0;
  if ((state == NULL) || (state->source == NULL) ||
      (state->container_source != NULL) || !state->is_complete)
    return false;
  if ((state->size.x < 1) || (state->size.y < 1))
    return true;
  
  bool containers_known = state->source->find_all_container_info();
  if (state->total_frames > 0)
    state->propagate_frame_and_track_info();
  if ((track_idx == 0) || (state->total_frames == 0))
    { // If `state->total_frames' is 0 we have not found any JPX containers
      // yet, so there is no point in looking past `state'
      if (state->abs_layers_per_rep == 0)
        { // Number of top-level frames cannot yet be known
          int known_layers = state->source->get_num_top_layers();
          state->count_frames(known_layers);
        }
      count = state->total_duration;
      if (state->total_frames > 0)
        return ((track_idx == 0) || containers_known);
      else
        return false;
    }

  jx_composition *last = state->last_in_track;
  assert(last != NULL);
  count = last->start_time;
  bool count_known = containers_known;
  if (last->total_frames == 0)
    { 
      int known_layers = 0;
      assert(last->container_source != NULL);
      while ((last->track_idx != track_idx) && (last->track_next != NULL))
        last = last->track_next;
      if (!last->finish())
        return false; // Can't count frames without parsed instructions
      assert(containers_known); // We must have found all containers
      count_known = state->source->find_all_streams();
      int reps = last->container_source->get_known_reps();
      known_layers = reps*last->abs_layers_per_rep;
      int last_frames = last->count_frames(known_layers);
      if (count_known)
        last->total_frames = last_frames;
      else if ((last->total_frames > 0) && (last->total_frames==last_frames))
        count_known = true;
    }
  count += last->total_duration;
  return count_known;
}

/*****************************************************************************/
/*              jpx_composition::count_track_frames_before_time              */
/*****************************************************************************/

bool
  jpx_composition::count_track_frames_before_time(kdu_uint32 track_idx,
                                                  kdu_long max_end_time,
                                                  int &count)
{
  count = 0;
  if ((state == NULL) || (state->source == NULL) ||
      (state->container_source != NULL) || !state->is_complete)
    return false;
  if ((state->size.x < 1) || (state->size.y < 1))
    return true;

  state->source->find_all_container_info();
  if (state->total_frames > 0)
    state->propagate_frame_and_track_info();
  
  int known_layers = 0;
  jx_composition *comp = state;
  bool may_need_more_boxes = false;
  if ((track_idx > 0) && (state->total_frames == 0))
    may_need_more_boxes = !state->source->find_all_container_info();
  if ((track_idx == 0) || (state->total_frames == 0))
    { // If `state->total_frames' is 0, we cannot have found any JPX
      // containers yet, so no need to look past `state'.
      known_layers = state->abs_layers_per_rep;
      if (known_layers == 0)
        known_layers = comp->source->get_num_top_layers();
    }
  else
    { 
      assert(comp->abs_layer_reps > 0);
      while ((comp->total_frames > 0) &&
             (max_end_time >= (comp->start_time+comp->total_duration)))
        { 
          if (comp->next_in_track == NULL)
            may_need_more_boxes =
              !state->source->find_all_container_info();
          if ((comp->next_in_track == NULL) ||
              (comp->next_in_track->start_time > max_end_time))
            break;
          comp = comp->next_in_track;
          while ((comp->track_idx < track_idx) && (comp->track_next != NULL))
            comp = comp->track_next;
        }
      count = comp->first_frame_idx;
      max_end_time -= comp->start_time;
      assert(max_end_time >= 0);
      if (!comp->finish())
        return false;
      if ((comp->total_frames == 0) && (max_end_time >= comp->total_duration))
        { 
          assert(comp->container_source->indefinitely_repeated());
          if (!state->source->find_all_streams())
            may_need_more_boxes = true;
        }
      int reps = 1;
      if (comp->container_source != NULL)
        reps = comp->container_source->get_known_reps();
      known_layers = reps*comp->abs_layers_per_rep;
    }
  count += comp->count_frames(known_layers,max_end_time);
  return ((comp->total_duration > max_end_time) || !may_need_more_boxes);
}

/*****************************************************************************/
/*                       jpx_composition::access_frame                       */
/*****************************************************************************/

jpx_frame
  jpx_composition::access_frame(kdu_uint32 track_idx, int frame_idx,
                                bool must_exist, bool include_persistents)
{
  jpx_frame result;
  if ((state == NULL) || (state->source == NULL) ||
      (frame_idx < 0) || (state->container_source != NULL) ||
      (!state->is_complete) || (state->size.x < 1) || (state->size.y < 1))
    return result; // Empty interface

  state->source->find_all_container_info();
  state->propagate_frame_and_track_info();
  assert(state->abs_layer_reps > 0);
  jx_composition *comp = state;
  while ((comp->total_frames > 0) &&
         (frame_idx >= (comp->first_frame_idx+comp->total_frames)))
    { 
      if (track_idx == 0)
        return result; // Empty interface -- not considering global tracks
      if (comp->next_in_track == NULL)
        return result; // Empty interface
      comp = comp->next_in_track;
      while ((comp->track_idx < track_idx) && (comp->track_next != NULL))
        comp = comp->track_next;
    }
  frame_idx -= comp->first_frame_idx;
  assert(frame_idx >= 0);
  if (!comp->finish())
    return result; // Instructions not yet available for `comp'
      
  int known_layers=0;
  if (comp->track_idx == 0)
    { // Top-level composition object
      known_layers = comp->abs_layers_per_rep;
      if (known_layers == 0)
        known_layers = state->source->get_num_top_layers();
      if ((state->total_frames > 0) && (frame_idx >= state->total_frames))
        return result; // Empty interface
      if (!must_exist)
        known_layers = INT_MAX; // No constraints imposed on `find_frame'
    }
  else
    { // Container-embedded composition information
      if (comp->total_frames == 0)
        { 
          assert(comp->container_source->indefinitely_repeated());
          bool all_found = state->source->find_all_streams();
          int reps = comp->container_source->get_known_reps();
          known_layers = reps*comp->abs_layers_per_rep;
          if (all_found)
            comp->total_frames = comp->count_frames(known_layers);
          else if (!must_exist)
            known_layers = INT_MAX; // No constraints on `find_frame'
        }
      else if (comp->abs_layer_reps == 0)
        { // Must have figured out number of frames for an indefinitely
          // repeated final container
          assert(comp->next_in_track == NULL);
          known_layers = INT_MAX;
        }
      else
        { // We must know how many layers there are in the JPX container
          if (frame_idx >= comp->total_frames)
            return result; // Cannot find frame; return empty interface
          known_layers = comp->abs_layer_reps * comp->abs_layers_per_rep;
        }
    }
  int repeat_idx=0;
  jx_frame *frm = comp->find_frame(frame_idx,known_layers,repeat_idx);
  if (frm != NULL)
    result = jpx_frame(frm,repeat_idx,include_persistents);
  else if ((comp->total_frames > 0) && (frame_idx < comp->total_frames))
    { KDU_ERROR(e,0x31071203); e <<
      KDU_TXT("Inconsistency detected between frame count recorded in "
              "Compositing Layer Extensions Info box and the number "
              "of frames that can actually be formed from the "
              "compositing instructions found within the "
              "Compositing Layer Extensions box.");
    }
  return result;
}

/*****************************************************************************/
/*                     jpx_composition::find_layer_match                     */
/*****************************************************************************/

int
  jpx_composition::find_layer_match(jpx_frame &frame_ifc, int &inst_idx,
                                    kdu_uint32 tgt_track_idx,
                                    const int layers[], int num_layers,
                                    int container_id, bool include_persistents,
                                    int flags)
{
  if ((state == NULL) || (state->source == NULL) || (num_layers < 1) ||
      (layers == NULL) || (state->container_source != NULL) ||
      (!state->is_complete) || (state->size.x < 1) || (state->size.y < 1))
    return -1;
  bool match_all = (flags & JPX_FRAME_MATCH_ALL_LAYERS) != 0;
  bool later_tracks = (flags & JPX_FRAME_MATCH_LATER_TRACKS) != 0;
  bool reverse_search = (flags & JPX_FRAME_MATCH_REVERSE) != 0;
  
  jpx_frame start_frame = frame_ifc; // Make copy of the entry state
  if (start_frame.exists())
    { // Make sure that `tgt_track_idx' is compatible with `start_frame'
      jx_composition *comp = start_frame.state->owner;
      if (comp->track_idx > tgt_track_idx)
        { 
          if (!later_tracks)
            return -1;
          tgt_track_idx = comp->track_idx; // Must match this one at least
        }
      if (comp->track_next != NULL)
        { 
          if (comp->track_idx < tgt_track_idx)
            return -1;
        }
    }
  
  // Start by figuring out what type of compositing layers we have
  bool containers_known = state->source->find_all_container_info();
  state->propagate_frame_and_track_info();
  int num_top_layers = state->source->get_num_top_layers();
  int n, first_layer_idx, last_layer_idx, first_container_layer_idx=-1;
  first_layer_idx = last_layer_idx = layers[0];
  for (n=0; n < num_layers; n++)
    { 
      if (layers[n] < first_layer_idx)
        first_layer_idx = layers[n];
      else if (layers[n] > last_layer_idx)
        last_layer_idx = layers[n];
      if ((layers[n] >= num_top_layers) &&
          ((first_container_layer_idx < 0) ||
           (layers[n] < first_container_layer_idx)))
        {
          first_container_layer_idx = layers[n];
          if (container_id >= 0)
            match_all = false;
        }
      // Note: it is possible that we do not yet know about all the top-level
      // layers, but in that case we have not encountered any containers yet,
      // so it does not matter what the `first_container_layer' value is.
    }
  
  // Walk through contexts in order
  jx_composition *comp=NULL;
  if (start_frame.state != NULL)
    comp = start_frame.state->owner;
  else if (reverse_search)
    comp = state->last_in_track;
  if (comp == NULL)
    comp = state;
  jx_frame *frame = NULL;
  int instance_idx = 0;
  int instruction_idx = 0;
  for (; (frame == NULL) && (comp != NULL);
       comp = (reverse_search)?(comp->prev_in_track):(comp->next_in_track))
    { 
      int max_container_rep = 0;
      jx_composition *matching_comp = NULL;
      if (comp->container_source == NULL)
        { 
          if (first_layer_idx >= num_top_layers)
            continue;
          matching_comp = comp;
        }
      else
        { // Looking into a JPX container
          int min_layer = comp->container_source->get_first_base_layer();
          int max_layer = comp->container_source->get_last_layer();
          if (!reverse_search)
            { 
              if (min_layer > last_layer_idx)
                break; // No point in looking at later JPX containers
              if (first_container_layer_idx > max_layer)
                continue; // Move on to later JPX containers
            }
          else
            { 
              if (min_layer > last_layer_idx)
                continue; // Move on to earlier JPX containers or top-level
              if (first_layer_idx > max_layer)
                break; // No point in looking at earlier contexts
            }
          if (container_id == comp->container_source->get_id())
            { 
              if (comp->container_source->indefinitely_repeated())
                comp->source->find_all_streams();
              max_container_rep = comp->container_source->get_known_reps()-1;
              for (n=0; (n < num_layers) && match_all; n++)
                { // See if we do in fact have container base layers; if so
                  // we should remove the `match_all' condition.
                  int idx = layers[n];
                  idx -= comp->container_source->get_first_base_layer();
                  if ((idx >= 0) &&
                      (idx < comp->container_source->get_num_base_layers()))
                    match_all = false;
                }
            }
          while ((comp->track_idx < tgt_track_idx) &&
                 (comp->track_next != NULL))
            comp = comp->track_next;
          jx_composition *scan = comp;
          for (; (scan != NULL) && (matching_comp == NULL);
               scan = scan->track_next)
            { // See if `scan' is suitable
              if ((scan->track_idx > tgt_track_idx) && !later_tracks)
                break;
              for (n=0; n < num_layers; n++)
                { 
                  int delta = layers[n] - scan->abs_layer_start;
                  if ((delta < 0) || (layers[n] > max_layer))
                    continue;
                  delta = delta % scan->abs_layer_rep_stride;
                  if (delta < scan->abs_layers_per_rep)
                    { // Found a match
                      matching_comp = scan;
                      break;
                    }
                }
            }
        }
      if (matching_comp == NULL)
        continue;
      if (!matching_comp->finish())
        return 0; // Have to come back later
      if ((start_frame.state != NULL) &&
          (matching_comp != start_frame.state->owner))
        { start_frame.state = NULL; start_frame.state_params.rep_idx = 0; }
      if (reverse_search)
        frame = matching_comp->reverse_match(layers,num_layers,instance_idx,
                                             instruction_idx,start_frame.state,
                                             start_frame.state_params.rep_idx,
                                             max_container_rep,match_all);
      else
        frame = matching_comp->find_match(layers,num_layers,instance_idx,
                                          instruction_idx,start_frame.state,
                                          start_frame.state_params.rep_idx,
                                          max_container_rep,match_all);
    }
  
  if (frame != NULL)
    { 
      frame_ifc = jpx_frame(frame,instance_idx,include_persistents);
      inst_idx = instruction_idx;
      if (include_persistents && (frame->last_persistent_frame != NULL))
        inst_idx += frame->last_persistent_frame->total_instructions;
      return 1;
    }
  else
    return (containers_known || reverse_search)?-1:0;
}

/*****************************************************************************/
/*                   jpx_composition::find_numlist_match                     */
/*****************************************************************************/

int
  jpx_composition::find_numlist_match(jpx_frame &frame, int &inst_idx,
                                      kdu_uint32 track_idx,
                                      jpx_metanode numlist,
                                      int max_inferred_layers,
                                      bool include_persistents,
                                      int flags)
{
  bool rres=false;
  int num_streams=0, num_layers=0;
  if (!numlist.get_numlist_info(num_streams,num_layers,rres))
    return -1;
  const int *layers = numlist.get_numlist_layers();
  int *inferred_layers = NULL;
  if (num_layers < 1)
    { 
      if ((max_inferred_layers <= 0) || (num_streams < 1) ||
          (state->source == NULL))
        return -1;
      flags &= ~JPX_FRAME_MATCH_ALL_LAYERS;
      inferred_layers = new int[max_inferred_layers];
      layers = inferred_layers;
      for (int n=0; (n<num_streams) && (num_layers<max_inferred_layers); n++)
        { 
          int rep_idx, c = numlist.get_numlist_codestream(n);
          jx_codestream_source *elt = state->source->get_codestream(c,rep_idx);
          if (elt == NULL)
            continue;
          jpx_codestream_source cs(elt,rep_idx);
          int l_idx = -1;
          while ((num_layers < max_inferred_layers) &&
                 ((l_idx = cs.enum_layer_ids(l_idx)) >= 0))
            inferred_layers[num_layers++] = l_idx;
        }
    }
  int result=-1, container_id = numlist.get_container_id();
  if (num_layers > 0)
    { 
      try { // Protect `inferred_layers' array
        result = find_layer_match(frame,inst_idx,track_idx,layers,num_layers,
                                  container_id,include_persistents,flags);
      } catch (...) {
        if (inferred_layers != NULL)
          delete[] inferred_layers;
        throw;
      }
    }
  if (inferred_layers != NULL)
    delete[] inferred_layers;
  return result;
}

/*****************************************************************************/
/*                      jpx_composition::get_next_frame                      */
/*****************************************************************************/

jx_frame *
  jpx_composition::get_next_frame(jx_frame *last_frame) const
{
  if (state == NULL)
    return NULL;
  if (last_frame == NULL)
    return state->head;
  else if (last_frame->owner == state)
    return last_frame->next;
  else
    return NULL;
}

/*****************************************************************************/
/*                      jpx_composition::get_prev_frame                      */
/*****************************************************************************/

jx_frame *
  jpx_composition::get_prev_frame(jx_frame *last_frame) const
{
  if (state == NULL)
    return NULL;
  if (last_frame == NULL)
    return NULL;
  else if (last_frame->owner == state)
    return last_frame->prev;
  else
    return NULL;
}

/*****************************************************************************/
/*                      jpx_composition::get_frame_info                      */
/*****************************************************************************/

void
  jpx_composition::get_frame_info(jx_frame *frame_ref, int &num_instructions,
                                  int &duration, int &repeat_count,
                                  bool &is_persistent) const
{
  if ((state == NULL) || (frame_ref == NULL))
    { num_instructions = 0; return; }
  num_instructions = frame_ref->num_instructions;
  duration = (int) frame_ref->duration;
  repeat_count = frame_ref->repeat_count;
  is_persistent = frame_ref->persistent;
}

/*****************************************************************************/
/*                 jpx_composition::get_last_persistent_frame                */
/*****************************************************************************/

jx_frame *
  jpx_composition::get_last_persistent_frame(jx_frame *frame_ref) const
{
  if ((state == NULL) || (frame_ref == NULL) || (frame_ref->owner != state))
    return NULL;
  return frame_ref->last_persistent_frame;
}

/*****************************************************************************/
/*                      jpx_composition::get_instruction                     */
/*****************************************************************************/

bool
  jpx_composition::get_instruction(jx_frame *frame, int instruction_idx,
                                   int &layer_idx, int &increment,
                                   bool &is_reused, kdu_dims &source_dims,
                                   kdu_dims &target_dims,
                                   jpx_composited_orientation &orientation)
                                   const
{
  if ((state == NULL) || (frame == NULL) || (frame->owner != state))
    return false;
  if ((instruction_idx < 0) || (instruction_idx >= frame->num_instructions))
    return false;
  jx_instruction *ip;
  for (ip=frame->head; instruction_idx > 0; instruction_idx--, ip=ip->next)
    assert(ip != NULL);
  layer_idx = ip->layer_idx;
  increment = ip->increment;
  is_reused = (ip->next_reuse != 0);
  source_dims = ip->source_dims;
  target_dims = ip->target_dims;
  orientation = ip->orientation;
  return true;
}

/*****************************************************************************/
/*                     jpx_composition::get_original_iset                    */
/*****************************************************************************/

bool
  jpx_composition::get_original_iset(jx_frame *frame, int instruction_idx,
                                     int &iset_idx, int &inum_idx) const
{
  if ((state == NULL) || (frame == NULL) || (frame->owner != state))
    return false;
  if ((instruction_idx < 0) || (instruction_idx >= frame->num_instructions))
    return false;
  jx_instruction *ip;
  for (ip=frame->head; instruction_idx > 0; instruction_idx--, ip=ip->next)
    assert(ip != NULL);
  iset_idx = ip->iset_idx;
  inum_idx = ip->inum_idx;
  return true;
}

/*****************************************************************************/
/*                      jpx_composition::access_owner                        */
/*****************************************************************************/

jpx_composition jpx_composition::access_owner(jx_frame *frame)
{
  jpx_composition result;
  if ((state != NULL) && (frame != NULL))
    result = jpx_composition(frame->owner);
  return result;
}

/*****************************************************************************/
/* STATIC          jpx_composition::get_interface_for_frame                  */
/*****************************************************************************/

jpx_frame
  jpx_composition::get_interface_for_frame(jx_frame *frame,
                                           int iteration_idx,
                                           bool include_persistents)
{
  jpx_frame result;
  if ((frame == NULL) || (iteration_idx < 0) ||
      ((frame->repeat_count >= 0) && (iteration_idx > frame->repeat_count)))
    return result;
  jx_composition *comp = frame->owner;
  int frame_idx = frame->frame_idx + iteration_idx;
  assert(frame_idx >= comp->first_frame_idx);
  if ((comp->total_frames == 0) ||
      ((frame_idx-comp->first_frame_idx) < comp->total_frames))
    result = jpx_frame(frame,iteration_idx,include_persistents);
  return result;
}

/*****************************************************************************/
/*                    jpx_composition::map_rel_layer_idx                     */
/*****************************************************************************/

int
  jpx_composition::map_rel_layer_idx(int rel_idx)
{
  if (state == NULL)
    return -1;
  if (state->abs_layer_start == 0)
    { // Top-level box; ignore stride
      if ((state->abs_layers_per_rep == 0) ||
          (rel_idx < state->abs_layers_per_rep))
        return rel_idx;
      else
        return state->abs_layers_per_rep-1;
    }
  assert(state->abs_layers_per_rep > 0);
  int r = rel_idx / state->abs_layers_per_rep;
  return (state->abs_layer_start + r*state->abs_layer_rep_stride +
          rel_idx - r*state->abs_layers_per_rep);
}

/*****************************************************************************/
/*                        jpx_composition::add_frame                         */
/*****************************************************************************/

jx_frame *
  jpx_composition::add_frame(int duration, int repeat_count,
                             bool is_persistent)
{
  if (state == NULL)
    return NULL;
  if (state->is_complete)
    { KDU_ERROR_DEV(e,0x02071203); e <<
      KDU_TXT("Attempting to add frames to a JPX composition after the "
              "composition has been finalized for writing.");
    }
  state->add_frame();
  state->tail->duration = duration;
  state->tail->pause = (duration == 0);
  state->tail->repeat_count = repeat_count;
  state->tail->persistent = is_persistent;
  return state->tail;
}

/*****************************************************************************/
/*                     jpx_composition::add_instruction                      */
/*****************************************************************************/

int
  jpx_composition::add_instruction(jx_frame *frame, int layer_idx,
                                   int increment, kdu_dims source_dims,
                                   kdu_dims target_dims,
                                   jpx_composited_orientation orientation)
{
  if (state == NULL)
    return -1;
  if (state->is_complete)
    { KDU_ERROR_DEV(e,0x02071204); e <<
      KDU_TXT("Attempting to add instructions to a JPX composition after the "
              "composition has been finalized for writing.");
    }
  jx_instruction *inst = frame->add_instruction(true);
  inst->layer_idx = layer_idx;
  inst->increment = increment;
  inst->source_dims = source_dims;
  inst->target_dims = target_dims;
  inst->orientation = orientation;
  return frame->num_instructions-1;
}

/*****************************************************************************/
/*                     jpx_composition::set_loop_count                       */
/*****************************************************************************/

void
  jpx_composition::set_loop_count(int count)
{
  if ((count < 0) || (count > 255))
    { KDU_ERROR_DEV(e,22); e <<
        KDU_TXT("Illegal loop count supplied to "
        "`jpx_composition::set_loop_count'.  Legal values must be in the "
        "range 0 (indefinite looping) to 255 (max explicit repetitions).");
    }
  state->loop_count = count;
}

/*****************************************************************************/
/*                           jpx_composition::copy                           */
/*****************************************************************************/

void
  jpx_composition::copy(jpx_composition src)
{
  assert((state != NULL) && (src.state != NULL));
  jx_frame *sp, *dp;
  jx_instruction *spi, *dpi;

  for (sp=src.state->head; sp != NULL; sp=sp->next)
    { 
      if (sp->head == NULL)
        continue;
      state->add_frame();
      dp = state->tail;
      dp->duration = sp->duration;
      dp->pause = sp->pause;
      dp->repeat_count = sp->repeat_count;
      dp->persistent = sp->persistent;
      for (spi=sp->head; spi != NULL; spi=spi->next)
        { 
          dpi = dp->add_instruction(true);
          dpi->layer_idx = spi->layer_idx;
          dpi->increment = spi->increment;
          dpi->source_dims = spi->source_dims;
          dpi->target_dims = spi->target_dims;
          dpi->orientation = spi->orientation;
        }
    }
}


/* ========================================================================= */
/*                            jpx_frame_expander                             */
/* ========================================================================= */

/*****************************************************************************/
/*                  jpx_frame_expander::~jpx_frame_expander                  */
/*****************************************************************************/

jpx_frame_expander::~jpx_frame_expander()
{
  if (members != NULL)
    delete[] members;
}

/*****************************************************************************/
/* STATIC       jpx_frame_expander::test_codestream_visibility               */
/*****************************************************************************/

int
  jpx_frame_expander::test_codestream_visibility(jpx_source *source,
                                                 jpx_frame frame,
                                                 int codestream_idx,
                                                 jpx_metanode numlist,
                                                 const int *layer_indices,
                                                 int num_layer_indices,
                                                 kdu_dims &composition_region,
                                                 kdu_dims codestream_roi,
                                                 bool ignore_use_in_alpha,
                                                 int initial_matches_to_skip)
{
  kdu_long start_time, duration;
  int num_instructions = frame.get_info(start_time,duration);
  if (num_instructions <= 0)
    return -1;
  if (numlist.exists() && (numlist.get_numlist_layer(0) < 0))
    numlist = jpx_metanode(); // Avoid numlist tests
  
  kdu_dims comp_dims; frame.get_global_info(comp_dims.size);
  if (composition_region.is_empty())
    composition_region = comp_dims;
  jpx_codestream_source stream =
    source->access_codestream(codestream_idx,false);
  if (stream.exists())
    {
      jp2_dimensions cs_dims = stream.access_dimensions();
      if (cs_dims.exists())
        {
          kdu_dims codestream_dims;
          codestream_dims.size = cs_dims.get_size();
          if (!codestream_roi.is_empty())
            codestream_roi &= codestream_dims;
          else
            codestream_roi = codestream_dims;
          if (codestream_roi.is_empty())
            return -1; // No intersection with codestream image region
        }
    }
  
  int max_compositing_layers;
  bool max_layers_known =
    source->count_compositing_layers(max_compositing_layers);

  // In the following, we repeatedly cycle through the frame members (layers)
  // starting from the top-most layer.  We keep track of the frame member
  // in which the codestream was found in the last iteration of the loop,
  // if any, along with the region which it occupies on the composition
  // surface.  This is done via the following four variables.  While examining
  // members which come before (above in the composition sequence) this
  // last member, we determine whether or not they hide it and how much
  // of it they hide.  We examine members beyond the last member in order
  // to find new uses of the codestream, as required by the
  // `initial_matches_to_skip' argument.
  int last_member_idx=-1; // We haven't found any match yet
  int last_layer_idx=-1; // Compositing layer associated with last member
  kdu_dims last_composition_region;

  int member_idx = 0;
  bool done = false;
  while (!done)
    { 
      int n, layer_idx = -1;
      kdu_dims source_dims, target_dims;
      jpx_composited_orientation orientation;
      jpx_layer_source layer;
      for (n=num_instructions-1; n >= 0; n--, member_idx++)
        { 
          if (member_idx == last_member_idx)
            { // Time to assess whether or not the last member is visible
              if (!last_composition_region.is_empty())
                { 
                  if (initial_matches_to_skip == 0)
                    { 
                      composition_region = last_composition_region;
                      return last_layer_idx;
                    }
                  else
                    initial_matches_to_skip--;
                }
              last_member_idx = -1;
              last_layer_idx = -1;
              last_composition_region = kdu_dims();
              continue; // Move on to the next member
            }
          
          source_dims = target_dims = kdu_dims();
          frame.get_instruction(n,layer_idx,source_dims,target_dims,
                                orientation);
          bool possible_match = true;
          if (layer_indices != NULL)
            { 
              possible_match = false;
              for (int pm=0; pm < num_layer_indices; pm++)
                if (layer_indices[pm] == layer_idx)
                  { possible_match = true; break; }
            }
          if (possible_match && numlist.exists())
            possible_match = numlist.test_numlist_layer(layer_idx);
          if ((layer_idx < 0) ||
              (max_layers_known &&
               (layer_idx >= max_compositing_layers)))
            return -1; // Layer does not exist; will never be able to open
                       // this frame, so return -1.
              
          layer = source->access_layer(layer_idx,false);
          if (source_dims.is_empty() && layer.exists())
            source_dims.size = layer.get_layer_size();
          if (target_dims.is_empty())
            { 
              target_dims.size = source_dims.size;
              if (orientation.transpose)
                target_dims.size.transpose();
            }

          jp2_channels channels;
          if (layer.exists())
            channels = layer.access_channels();
          if (!channels.exists())
            { // No codestream associations available
              if (!(possible_match && last_composition_region.is_empty()))
                continue; // Assume this is a transparent covering layer
              else
                break; // Assume we have a match
            }
          else
            { // Codestream associatiations and opacity info are available
              int c, num_colours = channels.get_num_colours();
              int comp_idx, lut_idx, str_idx, fmt_val, key_val;
              if (!last_composition_region.is_empty())
                { // See if this layer covers `last_composition_region'
                  assert(last_member_idx >= 0);
                  for (c=0; c < num_colours; c++)
                    if (channels.get_opacity_mapping(c,comp_idx,lut_idx,
                                                     str_idx,fmt_val) ||
                        channels.get_premult_mapping(c,comp_idx,lut_idx,
                                                     str_idx,fmt_val) ||
                        channels.get_chroma_key(0,key_val))
                      break; // Layer not completely opaque
                  if (c == num_colours)
                    { // Layer is opaque; let's test for intersection
                      kdu_dims isect = last_composition_region & target_dims;
                      if (isect.size.x == last_composition_region.size.x)
                        { // Horizontal coverage is complete
                          if (isect.pos.y == last_composition_region.size.y)
                            { // Top part is completely covered
                              last_composition_region.pos.y += isect.size.y;
                              last_composition_region.size.y -= isect.size.y;
                            }
                          else if ((isect.pos.y+isect.size.y) ==
                                   (last_composition_region.pos.y +
                                    last_composition_region.size.y))
                            { // Bottom part is completely covered
                              last_composition_region.size.y -= isect.size.y; 
                            }
                        }
                      else if (isect.size.y == last_composition_region.size.y)
                        { // Vertical coverage is complete
                          if (isect.pos.x == last_composition_region.size.x)
                            { // Left part is complete covered
                              last_composition_region.pos.x += isect.size.x;
                              last_composition_region.size.x -= isect.size.x;
                            }
                          else if ((isect.pos.x+isect.size.x) ==
                                   (last_composition_region.pos.x +
                                    last_composition_region.size.x))
                            { // Right part is completely covered
                              last_composition_region.size.x -= isect.size.x; 
                            }
                        }
                    }
                }
              else if ((last_member_idx < 0) && possible_match)
                { // Looking for a new member which contains the codestream
                  for (c=0; c < num_colours; c++)
                    { 
                      if (channels.get_colour_mapping(c,comp_idx,lut_idx,
                                                      str_idx,fmt_val) &&
                          (str_idx == codestream_idx))
                        break;
                      if (ignore_use_in_alpha)
                        continue;
                      if (channels.get_opacity_mapping(c,comp_idx,lut_idx,
                                                       str_idx,fmt_val) &&
                          (str_idx == codestream_idx))
                        break;
                      if (channels.get_premult_mapping(c,comp_idx,lut_idx,
                                                       str_idx,fmt_val) &&
                          (str_idx == codestream_idx))
                        break;
                    }
                  if (c < num_colours)
                    break; // Found matching codestream
                }
            }
        }
      
      if (n < 0)
        break; // Failed to find a match.
      
      // Found a new member; set up the `last_composition_region',
      // `last_member_idx' and `last_layer_idx' values
      // then start another iteration through the members
      if (!layer.exists())
        { // Cannot access the layer itself, so we can't be sure how to
          // map codestream regions to the composition surface.  Assume
          // the entire compositing layer is of interest for now.
          if (target_dims.is_empty())
            target_dims = comp_dims; // Assume layer covers whole surface
          last_composition_region = target_dims;
        }
      else
        { 
          kdu_coords align, sampling, denominator;
          int c, str_idx;
          for (c=0; (str_idx =
                     layer.get_codestream_registration(c,align,sampling,
                                                       denominator)) >= 0;
               c++)
            if (str_idx == codestream_idx)
              break;
          if (str_idx < 0)
            { // Failed to identify the codestream; makes no sense really
              assert(0);
              return -1;
            }
          // Now convert `codestream_roi' into a region on the compositing
          // surface, storing this region in `last_composition_region'
          if (!codestream_roi)
            last_composition_region = target_dims;
          else
            { 
              kdu_coords min = codestream_roi.pos;
              kdu_coords lim = min + codestream_roi.size;
              if ((denominator.x > 0) && (denominator.y > 0))
                { 
                  min.x = long_floor_ratio(((kdu_long) min.x)*sampling.x,
                                           denominator.x);
                  min.y = long_floor_ratio(((kdu_long) min.y)*sampling.y,
                                           denominator.y);
                  lim.x = long_ceil_ratio(((kdu_long) lim.x)*sampling.x,
                                          denominator.x);
                  lim.y = long_ceil_ratio(((kdu_long) lim.y)*sampling.y,
                                          denominator.y);
                }
              last_composition_region.pos = min;
              last_composition_region.size = lim - min;
              last_composition_region &= source_dims;
              last_composition_region.pos -= source_dims.pos;
              if (orientation.transpose)
                { 
                  last_composition_region.transpose();
                  source_dims.transpose();
                }
              if (orientation.hflip)
                last_composition_region.pos.x =
                  (source_dims.size.x - last_composition_region.size.x -
                   last_composition_region.pos.x);
              if (orientation.vflip)
                last_composition_region.pos.y =
                  (source_dims.size.y - last_composition_region.size.y -
                   last_composition_region.pos.y);
              if (target_dims.size != source_dims.size)
                { // Need to further scale the region
                  min = last_composition_region.pos;
                  lim = min + last_composition_region.size;
                  min.x=long_floor_ratio(((kdu_long) min.x)*target_dims.size.x,
                                         source_dims.size.x);
                  min.y=long_floor_ratio(((kdu_long) min.y)*target_dims.size.y,
                                         source_dims.size.y);
                  lim.x=long_ceil_ratio(((kdu_long) lim.x)*target_dims.size.x,
                                        source_dims.size.x);
                  lim.y=long_ceil_ratio(((kdu_long) lim.y)*target_dims.size.x,
                                        source_dims.size.x);
                  last_composition_region.pos = min;
                  last_composition_region.size = lim - min;
                }
              last_composition_region.pos += target_dims.pos;
              last_composition_region &= target_dims; // Just to be sure
            }
        }
          
      last_composition_region &= composition_region;
      last_member_idx = member_idx;
      last_layer_idx = layer_idx;
      member_idx = 0;
    }

  return -1; // Didn't find a match
}

/*****************************************************************************/
/*                      jpx_frame_expander::construct                        */
/*****************************************************************************/

bool
  jpx_frame_expander::construct(jpx_source *source, jpx_frame frame,
                                kdu_dims region_of_interest)
{
  this->non_covering_members = false;
  this->num_members = 0;
  kdu_long start_time, duration;
  int n, m, num_instructions = frame.get_info(start_time,duration);
  if (num_instructions <= 0)
    return true;
  kdu_dims comp_dims; frame.get_global_info(comp_dims.size);
  if (region_of_interest.is_empty())
    region_of_interest = comp_dims;
  this->frame = frame;

  kdu_dims opaque_dims;
  kdu_dims opaque_roi; // Amount of region of interest which is opaque
  int max_compositing_layers=0;
  bool max_layers_known =
    source->count_compositing_layers(max_compositing_layers);

  for (n=num_instructions-1; n >= 0; n--)
    { 
      kdu_dims source_dims, target_dims;
      jpx_composited_orientation orientation;
      int layer_idx=-1;
      frame.get_instruction(n,layer_idx,source_dims,target_dims,orientation);
      if ((layer_idx < 0) ||
          (max_layers_known && (layer_idx >= max_compositing_layers)))
        { // Cannot open frame
          num_members = 0;
          return true;
        }

      jpx_layer_source layer = source->access_layer(layer_idx,false);
      if (source_dims.is_empty() && layer.exists())
        source_dims.size = layer.get_layer_size();
      if (target_dims.is_empty())
        { 
          target_dims.size = source_dims.size;
          if (orientation.transpose)
            target_dims.size.transpose();
        }
      kdu_dims test_dims = target_dims & comp_dims;
      kdu_dims test_roi = target_dims & region_of_interest;
      if ((!target_dims.is_empty()) &&
          ((opaque_dims & test_dims) == test_dims))
        continue; // Globally invisible

      // Add a new member
      if (max_members == num_members)
        { 
          int new_max_members = max_members + num_members+8;
          jx_frame_member *tmp = new jx_frame_member[new_max_members];
          for (int m=0; m < num_members; m++)
            tmp[m] = members[m];
          if (members != NULL)
            delete[] members;
          max_members = new_max_members;
          members = tmp;
        }
      jx_frame_member *mem = members + (num_members++);
      mem->instruction_idx = n;
      mem->layer_idx = layer_idx;
      mem->source_dims = source_dims;
      mem->target_dims = target_dims;
      mem->orientation = orientation;
      mem->covers_composition = (test_dims == comp_dims);
      mem->layer_is_accessible = layer.exists();
      mem->may_be_visible_under_roi =
        (target_dims.is_empty() ||
         ((!test_roi.is_empty()) && ((test_roi & opaque_roi) != test_roi)));
      mem->is_opaque = false; // Until proven otherwise
      if (layer.exists())
        { // Determine opacity of layer
          jp2_channels channels = layer.access_channels();
          int comp_idx, lut_idx, str_idx, fmt;
          kdu_int32 key_val;
          mem->is_opaque =
            !(channels.get_opacity_mapping(0,comp_idx,lut_idx,str_idx,fmt) ||
              channels.get_premult_mapping(0,comp_idx,lut_idx,str_idx,fmt) ||
              channels.get_chroma_key(0,key_val));
        }

      // Update the opaque regions and see if whole composition is covered
      if (mem->is_opaque)
        { 
          if (test_dims.area() > opaque_dims.area())
            opaque_dims = test_dims;
          if (test_roi.area() > opaque_roi.area())
            opaque_roi = test_roi;
          if (opaque_dims == comp_dims)
            break; // Can stop looking for more instructions
        }
    }

  // Now go back over things to see if some extra members can be removed
  bool can_access_all_members = true;
  for (n=0; n < num_members; n++)
    {
      if (!members[n].may_be_visible_under_roi)
        {
          if (!members[n].layer_is_accessible)
            { // May still need to include this member, unless included already
              int layer_idx = members[n].layer_idx;
              for (m=0; m < num_members; m++)
                if ((members[m].layer_idx == layer_idx) &&
                    ((m < n) || members[m].may_be_visible_under_roi))
                  { // We can eliminate the member instruction
                    members[n].layer_is_accessible = true; // Forces removal
                    break;
                  }
            }
          if (members[n].layer_is_accessible)
            {
              num_members--;
              for (m=n; m < num_members; m++)
                members[m] = members[m+1];
              n--; // So that we consider the next member correctly
              continue;
            }
        }
      if (!members[n].layer_is_accessible)
        can_access_all_members = false;
      else if (!members[n].covers_composition)
        non_covering_members = true;
    }

  return can_access_all_members;
}

/*****************************************************************************/
/* STATIC    jpx_frame_expander::test_codestream_visibility (opaque refs)    */
/*****************************************************************************/

int
  jpx_frame_expander::test_codestream_visibility(jpx_source *source,
                                                 jx_frame *frame_ref,
                                                 int iteration_idx,
                                                 bool follow_persistence,
                                                 int codestream_idx,
                                                 const int *layer_indices,
                                                 int num_layer_indices,
                                                 kdu_dims &composition_region,
                                                 kdu_dims codestream_roi,
                                                 bool ignore_use_in_alpha,
                                                 int initial_matches_to_skip)
{
  jpx_frame frame_ifc;
  jpx_composition comp = source->access_composition();
  if (comp.exists())
    frame_ifc = comp.get_interface_for_frame(frame_ref,iteration_idx,
                                             follow_persistence);
  return test_codestream_visibility(source,frame_ifc,codestream_idx,
                                    jpx_metanode(),layer_indices,
                                    num_layer_indices,composition_region,
                                    codestream_roi,ignore_use_in_alpha,
                                    initial_matches_to_skip);
}

/*****************************************************************************/
/*              jpx_frame_expander::construct (opaque refs)                  */
/*****************************************************************************/

bool
  jpx_frame_expander::construct(jpx_source *source, jx_frame *frame_ref,
                                int iteration_idx, bool follow_persistence,
                                kdu_dims region_of_interest)
{
  jpx_frame frame_ifc;
  jpx_composition comp = source->access_composition();
  if (comp.exists())
    frame_ifc = comp.get_interface_for_frame(frame_ref,iteration_idx,
                                             follow_persistence);
  return construct(source,frame_ifc,region_of_interest);
}


/* ========================================================================= */
/*                                jx_numlist                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          jx_numlist::~jx_numlist                          */
/*****************************************************************************/

jx_numlist::~jx_numlist()
{
  unlink();
  if ((codestream_indices != NULL) &&
      (codestream_indices != &max_codestream_idx))
    delete[] codestream_indices;
  if ((layer_indices != NULL) && (layer_indices != &max_layer_idx))
    delete[] layer_indices;
}

/*****************************************************************************/
/*                            jx_numlist::equals                             */
/*****************************************************************************/

bool jx_numlist::equals(const jx_numlist *src) const
{ 
  if ((num_codestreams != src->num_codestreams) ||
      (num_compositing_layers != src->num_compositing_layers) ||
      (non_base_codestreams != src->non_base_codestreams) ||
      (non_base_layers != src->non_base_layers) ||
      (rendered_result != src->rendered_result))
    return false;
  
  if (num_codestreams > 0)
    { 
      if (max_codestream_idx != src->max_codestream_idx)
        return false;
      if (num_codestreams > 1)
        { // Need to check individual indices
          for (int n=0; n < num_codestreams; n++)
            if (codestream_indices[n] != src->codestream_indices[n])
              return false;
        }
    }
  
  if (num_compositing_layers > 0)
    { 
      if (max_layer_idx != src->max_layer_idx)
          return false;
      if (num_compositing_layers > 1)
        { // Need to check individual indices
          for (int n=0; n < num_compositing_layers; n++)
            if (layer_indices[n] != src->layer_indices[n])
              return false;
        }
    }
  
  if ((non_base_codestreams < num_codestreams) ||
      (non_base_layers < num_compositing_layers))
    assert(container == src->container);

  return true;
}

/*****************************************************************************/
/*                        jx_numlist::add_codestream                         */
/*****************************************************************************/

void
  jx_numlist::add_codestream(int idx, bool relative_to_container)
{
  bool is_base = false;
  if (container != NULL)
    { 
      if (relative_to_container)
        idx = container->convert_relative_to_base_codestream_id(idx);
      else
        idx = container->validate_and_normalize_codestream_id(idx);
      is_base = (idx >= container->get_first_base_codestream());
    }
  if (idx & 0xFF000000)
    invalid_index_error();
  if (num_codestreams == 0)
    { 
      assert(codestream_indices == NULL);
      max_codestreams = num_codestreams = 1;
      codestream_indices = &max_codestream_idx;
      max_codestream_idx = idx;
      non_base_codestreams = (is_base)?0:1;
      return;
    }
  
  int n, m;
  for (n=0; n < num_codestreams; n++)
    if (codestream_indices[n] >= idx)
      { 
        if (codestream_indices[n] == idx)
          return; // Already exists
        break;
      }
  
  if (num_codestreams >= max_codestreams)
    { // Allocate appropriate array
      int new_max_codestreams = 2*max_codestreams + 6;
      int *tmp = new int[new_max_codestreams];
      for (int k=0; k < num_codestreams; k++)
        tmp[k] = codestream_indices[k];
      if (codestream_indices != &max_codestream_idx)
        delete[] codestream_indices;
      codestream_indices = tmp;
      max_codestreams = new_max_codestreams;
    }

  if (n == num_codestreams)
    max_codestream_idx = idx;
  else
    for (m=num_codestreams; m > n; m--)
      codestream_indices[m] = codestream_indices[m-1];
  codestream_indices[n] = idx;
  num_codestreams++;
  if (!is_base)
    non_base_codestreams++;
}

/*****************************************************************************/
/*                    jx_numlist::add_compositing_layer                      */
/*****************************************************************************/

void
  jx_numlist::add_compositing_layer(int idx, bool relative_to_container)
{
  bool is_base = false;
  if (container != NULL)
    { 
      if (relative_to_container)
        idx = container->convert_relative_to_base_layer_id(idx);
      else
        idx = container->validate_and_normalize_layer_id(idx);
      is_base = (idx >= container->get_first_base_layer());
    }
  if (idx & 0xFF000000)
    invalid_index_error();
  if (num_compositing_layers == 0)
    { 
      assert(layer_indices == NULL);
      max_compositing_layers = num_compositing_layers = 1;
      layer_indices = &max_layer_idx;
      max_layer_idx = idx;
      non_base_layers = (is_base)?0:1;
      return;
    }
  
  int n, m;
  for (n=0; n < num_compositing_layers; n++)
    if (layer_indices[n] >= idx)
      { 
        if (layer_indices[n] == idx)
          return; // Already exists
        break;
      }
  
  if (num_compositing_layers >= max_compositing_layers)
    { // Allocate appropriate array
      int new_max_layers = 2*max_compositing_layers + 6;
      int *tmp = new int[new_max_layers];
      for (int k=0; k < num_compositing_layers; k++)
        tmp[k] = layer_indices[k];
      if (layer_indices != &max_layer_idx)
        delete[] layer_indices;
      layer_indices = tmp;
      max_compositing_layers = new_max_layers;
    }

  if (n == num_compositing_layers)
    max_layer_idx = idx;
  else
    for (m=num_compositing_layers; m > n; m--)
      layer_indices[m] = layer_indices[m-1];
  layer_indices[n] = idx;
  num_compositing_layers++;
  if (!is_base)
    non_base_layers++;
}

/*****************************************************************************/
/*                            jx_numlist::write                              */
/*****************************************************************************/

void
  jx_numlist::write(jp2_output_box &box)
{
  int n, idx;
  if (container == NULL)
    { // No need to adjust compositing layer or codestream indices
      for (n=0; n < num_codestreams; n++)
        { 
          idx = codestream_indices[n];
          if (idx & 0xFF000000)
            invalid_index_error();
          box.write((kdu_uint32)(idx | 0x01000000));
        }
      for (n=0; n < num_compositing_layers; n++)
        { 
          idx = layer_indices[n];
          if (idx & 0xFF000000)
            invalid_index_error();
          box.write((kdu_uint32)(idx | 0x02000000));
        }
    }
  else
    { 
      int top_indices, first_base_idx;
      top_indices = container->get_num_top_codestreams();
      first_base_idx = container->get_first_base_codestream();
      for (n=0; n < num_codestreams; n++)
        { 
          idx = codestream_indices[n];
          if (idx >= top_indices)
            { 
              idx -= first_base_idx;
              assert(n >= non_base_codestreams);
              assert((idx >= 0) &&
                     (idx < container->get_num_base_codestreams()));
              idx += top_indices;
            }
          else
            assert(n < non_base_codestreams);
          if (idx & 0xFF000000)
            invalid_index_error();
          box.write((kdu_uint32)(idx | 0x01000000));
        }
      top_indices = container->get_num_top_layers();
      first_base_idx = container->get_first_base_layer();
      for (n=0; n < num_compositing_layers; n++)
        { 
          idx = layer_indices[n];
          if (idx >= top_indices)
            { 
              idx -= first_base_idx;
              assert(n >= non_base_layers);
              assert((idx >= 0) &&
                     (idx < container->get_num_base_layers()));
              idx += top_indices;
            }
          else
            assert(n < non_base_layers);
          if (idx & 0xFF000000)
            invalid_index_error();
          box.write((kdu_uint32)(idx | 0x02000000));
        }
    }
  if (rendered_result)
    box.write((kdu_uint32) 0x00000000);
}

/*****************************************************************************/
/*                      jx_numlist::invalid_index_error                      */
/*****************************************************************************/

void jx_numlist::invalid_index_error()
{
  KDU_ERROR(e,0x11081201); e <<
  KDU_TXT("Number lists cannot record compositing layer or codestream indices "
          "greater than or equal to 2^24.");
}

/*****************************************************************************/
/*                           jx_numlist::check_match                         */
/*****************************************************************************/

bool jx_numlist::check_match(int stream_idx, int layer_idx,
                             int base_stream_idx, int base_layer_idx,
                             bool rres, bool exclude_region_numlists,
                             bool ignore_missing_numlist_categories)
{
  if (exclude_region_numlists &&
      (metanode->flags & JX_METANODE_HAS_ROI_CHILD) &&
      !(metanode->flags & JX_METANODE_HAS_NON_ROI_CHILD))
    return false;
  if (rres && !rendered_result)
    return false;
  if (layer_idx >= 0)
    { 
      if (num_compositing_layers > 0)
        { 
          if (layer_idx < layer_indices[0])
            return false;
          if ((container != NULL) && (base_layer_idx >= 0))
            { // Must match one of the numlist's base layer indices
              int n;
              for (n=non_base_layers; n < num_compositing_layers; n++)
                if (layer_indices[n] >= base_layer_idx)
                  break;
              if ((n == num_compositing_layers) ||
                  (layer_indices[n] != base_layer_idx))
                return false; // Not a match
            }
          else
            { // Must match one of the numlist's non-base layers
              int n;
              for (n=0; n < non_base_layers; n++)
                if (layer_indices[n] >= layer_idx)
                  break;
              if ((n == non_base_layers) ||
                  (layer_indices[n] != layer_idx))
                return false;
            }
        }
      else if (!ignore_missing_numlist_categories)
        return false;
    }
  if (stream_idx >= 0)
    { 
      if (num_codestreams > 0)
        { 
          if (stream_idx < codestream_indices[0])
            return false;
          if ((container != NULL) && (base_stream_idx >= 0))
            { // Must match one of the numlist's base codestream indices
              int n;
              for (n=non_base_codestreams; n < num_codestreams; n++)
                if (codestream_indices[n] >= base_stream_idx)
                  break;
              if ((n == num_codestreams) ||
                  (codestream_indices[n] != base_stream_idx))
                return false;
            }
          else
            { // Must match one of the numlist's non-base codestreams
              int n;
              for (n=0; n < non_base_codestreams; n++)
                if (codestream_indices[n] >= stream_idx)
                  break;
              if ((n == non_base_codestreams) ||
                  (codestream_indices[n] != stream_idx))
                return false;
            }
        }
      else if (!ignore_missing_numlist_categories)
        return false;
    }
  return true; // All tests passed
}

/*****************************************************************************/
/*                             jx_numlist::unlink                            */
/*****************************************************************************/

void jx_numlist::unlink()
{ 
  int lib_idx;
  if ((first_identical == this) && (next_identical == NULL))
    { // No other identical numlists
      if (region_library != NULL)
        { 
          assert(region_library->representative_numlist == this);
          region_library->representative_numlist = NULL;
          delete region_library;
          region_library = NULL;
        }
      for (lib_idx=0; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
        { 
          jx_numlist_cluster *cluster = numlist_cluster[lib_idx];
          if (cluster != NULL)
            { 
              assert((cluster->level==0) && (cluster->library_idx==lib_idx));
              if (prev_in_cluster[lib_idx] == NULL)
                { 
                  assert(this == cluster->numlists);
                  cluster->numlists = next_in_cluster[lib_idx];
                }
              else
                prev_in_cluster[lib_idx]->next_in_cluster[lib_idx] =
                  next_in_cluster[lib_idx];
              if (next_in_cluster[lib_idx] != NULL)
                next_in_cluster[lib_idx]->prev_in_cluster[lib_idx] =
                  prev_in_cluster[lib_idx];
              numlist_cluster[lib_idx] = NULL;
            }
          next_in_cluster[lib_idx] = prev_in_cluster[lib_idx] = NULL;
          if ((cluster != NULL) && (cluster->numlists == NULL))
            { 
              jx_numlist_library *library = cluster->library;
              library->remove_cluster(cluster);
            }
        }
    }
  else if (first_identical == this)
    { // We are only unlinking the first of a larger list of
      // identical numlists -- we have to attach the next element of
      // this list into any numlist clusters to which we currently belong.
      jx_numlist *sibling = next_identical;
      for (lib_idx=0; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
        { 
          jx_numlist_cluster *cluster = numlist_cluster[lib_idx];
          sibling->numlist_cluster[lib_idx] = cluster;
          if (cluster != NULL)
            { // Have to change the library links to refer to `sibling'
              sibling->prev_in_cluster[lib_idx] = prev_in_cluster[lib_idx];
              sibling->next_in_cluster[lib_idx] = next_in_cluster[lib_idx];
              if (prev_in_cluster[lib_idx] == NULL)
                { 
                  assert(cluster->numlists == this);
                  cluster->numlists = sibling;
                }
              else
                { 
                  assert(prev_in_cluster[lib_idx]->next_in_cluster[lib_idx] ==
                         this);
                  prev_in_cluster[lib_idx]->next_in_cluster[lib_idx] = sibling;
                }
              if (next_in_cluster[lib_idx] != NULL)
                { 
                  assert(next_in_cluster[lib_idx]->prev_in_cluster[lib_idx] ==
                         this);
                  next_in_cluster[lib_idx]->prev_in_cluster[lib_idx] = sibling;
                }
            }
          numlist_cluster[lib_idx] = NULL;
          prev_in_cluster[lib_idx] = next_in_cluster[lib_idx] = NULL;
        }
      if ((sibling->region_library = this->region_library) != NULL)
        { 
          this->region_library = NULL;
          assert(sibling->region_library->representative_numlist == this);
          sibling->region_library->representative_numlist = sibling;
        }
      jx_numlist *scan;
      for (scan=sibling; scan != NULL; scan=scan->next_identical)
        scan->first_identical = sibling;
    }
  else
    { // We are unlinking a non-initial member of a larger list of
      // numlists.  Only need to reconnect our predecessor's `next_identical'
      // link.
      jx_numlist *scan;
      for (scan=first_identical; scan != NULL; scan=scan->next_identical)
        if (scan->next_identical == this)
          { 
            scan->next_identical = this->next_identical;
            break;
          }
      assert(scan != NULL);
    }

  this->first_identical = this;
  this->next_identical = NULL;
}


/* ========================================================================= */
/*                           jx_numlist_library                              */
/* ========================================================================= */

/*****************************************************************************/
/*                 jx_numlist_library::~jx_numlist_library                   */
/*****************************************************************************/

jx_numlist_library::~jx_numlist_library()
{
  int lib_idx;
  jx_numlist_cluster *elt;
  for (lib_idx = 0; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
    while ((elt=clusters[lib_idx]) != NULL)
      { 
        remove_cluster(elt);
        assert(clusters[lib_idx] != elt);
      }
}

/*****************************************************************************/
/*                         jx_numlist_library::add                           */
/*****************************************************************************/

void jx_numlist_library::add(jx_numlist *obj)
{
  int lib_idx;
  assert(obj->first_identical != NULL); // Must never be NULL
  bool linked_in = (obj->first_identical != obj);
  for (lib_idx=0; (lib_idx < JX_LIBRARY_NL_COUNT) && !linked_in; lib_idx++)
    if (obj->numlist_cluster[lib_idx] != NULL)
      linked_in = true;
  if (linked_in)
    return;

  for (lib_idx=0; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
    { 
      int range_min, range_lim;
      if (lib_idx == JX_LIBRARY_NLL)
        { 
          if (obj->non_base_layers == 0)
            continue; // No contribution to this library
          range_min = obj->layer_indices[0];
          range_lim = 1 + obj->layer_indices[obj->non_base_layers-1];
        }
      else if (lib_idx == JX_LIBRARY_C_NLL)
        { 
          if (obj->non_base_layers == obj->num_compositing_layers)
            continue;
          assert(obj->container != NULL);
          range_min = obj->layer_indices[obj->non_base_layers];
          range_lim = 1 + obj->max_layer_idx;
        }
      else if (lib_idx == JX_LIBRARY_NLC)
        { 
          if (obj->non_base_codestreams == 0)
            continue; // No contribution to this library
          range_min = obj->codestream_indices[0];
          range_lim = 1 + obj->codestream_indices[obj->non_base_codestreams-1];
        }
      else if (lib_idx == JX_LIBRARY_C_NLC)
        { 
          if (obj->non_base_codestreams == obj->num_codestreams)
            continue;
          assert(obj->container != NULL);
          range_min = obj->codestream_indices[obj->non_base_codestreams];
          range_lim = 1 + obj->max_codestream_idx;          
        }
      else if (lib_idx == JX_LIBRARY_NLR)
        { 
          if (!obj->rendered_result)
            continue;
          range_min = 0;
          range_lim = 1;
        }
      else
        continue; // Not a recognized library type for this function
      
      int range_size = range_lim - range_min;
      int log_range = 0;
      if (range_size > 0) // Just in case
        while (((range_size-1) >> log_range) > 0)
          log_range++; // `dim' must be > 2^log_range
      
      jx_numlist_cluster *prev, *scan;
      for (prev=NULL, scan=clusters[lib_idx]; scan != NULL;
           prev=scan, scan=scan->next)
        if (scan->log_range <= log_range)
          break; // Root clusters organized by decreasing size
      
      jx_numlist_cluster *cluster_root = scan;
      if ((cluster_root == NULL) || (cluster_root->log_range != log_range))
        { // Create a new cluster root
          jx_numlist_cluster *elt = new jx_numlist_cluster(this,lib_idx);
          elt->log_range = log_range;
          elt->level = 0; // Start off with everything at level 0
          elt->range_min = range_min;
          elt->range_lim = range_lim;
          elt->parent = NULL;
          elt->next = scan;
          if (prev == NULL)
            this->clusters[lib_idx] = elt;
          else
            prev->next = elt;
          cluster_root = elt;
        }
      
      bool augmented_root_cluster = false;
      jx_numlist_cluster *cluster = cluster_root;
      while (cluster->level > 0)
        { 
          assert((cluster->level >= 3) && (cluster->log_range == log_range));
          int gap = 1 << (log_range+cluster->level-3);
          int new_range_min = range_min & ~(gap-1);
          int new_range_lim = new_range_min + gap + (1<<log_range) - 1;
          for (prev=NULL, scan=cluster->descendants; scan != NULL;
               prev=scan, scan=scan->next)
            { 
              assert(scan->level == (cluster->level-3));
              if (scan->range_min >= new_range_min)
                break;
            }
          if ((scan == NULL) || (scan->range_min != new_range_min))
            { // Create a new descendant of `cluster'
              jx_numlist_cluster *elt = new jx_numlist_cluster(this,lib_idx);
              elt->log_range = log_range;
              elt->level = cluster->level-3;
              elt->range_min = new_range_min;
              elt->range_lim = new_range_lim;
              elt->parent = cluster;
              elt->next = scan;
              if (prev == NULL)
                cluster->descendants = elt;
              else
                prev->next = elt;
              if (cluster == cluster_root)
                augmented_root_cluster = true;
              scan = elt;
            }
          cluster = scan;
        }
      
      // We have to see if there already is an identical numlist
      jx_numlist *nl;
      for (nl=cluster->numlists; nl != NULL; nl=nl->next_in_cluster[lib_idx])
        if (nl->equals(obj))
          break;
      if (nl != NULL)
        { 
          assert(nl->first_identical == nl);
          assert(!linked_in);
          obj->first_identical = nl;
          obj->next_identical = nl->next_identical;
          nl->next_identical = obj;
          break; // No need to consider any other sub-libraries
        }
      else
        { // Hook `obj' into the `cluster->numlists' list & update global info
          linked_in = true;
          obj->first_identical = obj;
          obj->next_identical = NULL;
          obj->numlist_cluster[lib_idx] = cluster;
          obj->prev_in_cluster[lib_idx] = NULL;
          if ((obj->next_in_cluster[lib_idx] = cluster->numlists) != NULL)
            obj->next_in_cluster[lib_idx]->prev_in_cluster[lib_idx] = obj;
          cluster->numlists = obj;
          if (cluster == cluster_root)
            augmented_root_cluster = true;
          if (range_min < cluster_root->range_min)
            cluster_root->range_min = range_min;
          if (range_lim > cluster_root->range_lim)
            cluster_root->range_lim = range_lim;
        }

      // Finally, see if we need to split the root cluster
      if (augmented_root_cluster)
        check_split_root_cluster(cluster_root);
    }
}

/*****************************************************************************/
/*              jx_numlist_library::check_split_root_cluster                 */
/*****************************************************************************/

void jx_numlist_library::check_split_root_cluster(jx_numlist_cluster *root)
{
  assert(root->library == this);
  int lib_idx = root->library_idx;
  if (lib_idx == JX_LIBRARY_NLR)
    return; // No possibility to cluster the degenerate NLR sub-library
  int gap = 1 << (root->log_range + root->level);
  int container_range_size = gap + (1<<root->log_range) - 1;
      // Gap between range_min and range_lim for the new clusters we might
      // create to hold the current descendants of `root'.
  
  if (root->level > 0)
    { 
      if ((root->level+root->log_range) >=27)
        return; // Don't create levels with a total range greater than 2^27
      assert(!(root->level % 3)); // All level values should be divisible by 3
      int num_children = 0;
      jx_numlist_cluster *child, *next_child;
      for (child=root->descendants; child != NULL; child=child->next)
        num_children++;
      if (num_children <= 8)
        return;
      root->level += 3;
      child = root->descendants; root->descendants = NULL;
      for (; child != NULL; child=next_child)
        { // Put `child' into a new or existing intermediate cluster
          next_child = child->next;
          int container_range_min = child->range_min & ~(gap-1);
          int container_range_lim = container_range_min + container_range_size;
          jx_numlist_cluster *scan, *prev;
          for (prev=NULL, scan=root->descendants; scan != NULL;
               prev=scan, scan=scan->next)
            if (scan->range_min >= container_range_min)
                break;
          if ((scan == NULL) || (scan->range_min != container_range_min))
            { // Create a new descendant of `root'
              jx_numlist_cluster *elt = new jx_numlist_cluster(this,lib_idx);
              elt->log_range = root->log_range;
              elt->level = root->level-3;
              elt->range_min = container_range_min;
              elt->range_lim = container_range_lim;
              elt->parent = root;
              elt->next = scan;
              if (prev == NULL)
                root->descendants = elt;
              else
                prev->next = elt;
              scan = elt;
            }
          jx_numlist_cluster *cluster = scan;
          child->parent = cluster;
          for (prev=NULL, scan=cluster->descendants;
               scan != NULL; prev=scan, scan=scan->next)
            { // Find out where to place `child' within the descendants of
              // the intermediate `cluster'
              assert(scan->level == child->level);
              if (scan->range_min >= child->range_min)
                break;
            }
          assert((scan == NULL) || (scan->range_min != child->range_min));
          child->next = scan;
          if (prev == NULL)
            cluster->descendants = child;
          else
            prev->next = child;
        }
      check_split_root_cluster(root); // Do it again, if necessary
    }
  else
    { // See if we need to split level 0 for the first time
      int container_range_min = root->range_min & ~(gap-1);
      int container_range_lim = container_range_min + container_range_size;
      if (container_range_lim >= root->range_lim)
        return; // No point in splitting the root up -- won't help retrieval
      int num_children=0;
      jx_numlist *child, *next_child;
      for (child=root->numlists; child != NULL;
           child=child->next_in_cluster[lib_idx])
        num_children++;
      if (num_children <= 8)
        return; // No point in splitting the root up -- may get up to 8 new
                // clusters
      root->level = 3;
      child = root->numlists; root->numlists = NULL;
      for (; child != NULL; child=next_child)
        { // Put `child' into a new or existing intermediate cluster
          next_child = child->next_in_cluster[lib_idx];
          assert(child == child->first_identical);
          if (lib_idx == JX_LIBRARY_NLL)
            { 
              assert(child->non_base_layers > 0);
              container_range_min = child->layer_indices[0];
            }
          else if (lib_idx == JX_LIBRARY_C_NLL)
            { 
              assert(child->num_compositing_layers > child->non_base_layers);
              container_range_min =
                child->layer_indices[child->non_base_layers];
            }          
          else if (lib_idx == JX_LIBRARY_NLC)
            { 
              assert(child->non_base_codestreams > 0);
              container_range_min = child->codestream_indices[0];
            }
          else if (lib_idx == JX_LIBRARY_C_NLC)
            { 
              assert(child->num_codestreams > child->non_base_codestreams);
              container_range_min =
                child->codestream_indices[child->non_base_codestreams];
            }
          else
            assert(0);
          container_range_min &= ~(gap-1);
          container_range_lim = container_range_min + container_range_size;
          jx_numlist_cluster *scan, *prev;
          for (prev=NULL, scan=root->descendants; scan != NULL;
               prev=scan, scan=scan->next)
            if (scan->range_min >= container_range_min)
              break;
          if ((scan == NULL) || (scan->range_min != container_range_min))
            { // Create a new descendant of `root'
              jx_numlist_cluster *elt = new jx_numlist_cluster(this,lib_idx);
              elt->log_range = root->log_range;
              elt->level = 0;
              elt->range_min = container_range_min;
              elt->range_lim = container_range_lim;
              elt->parent = root;
              elt->next = scan;
              if (prev == NULL)
                root->descendants = elt;
              else
                prev->next = elt;
              scan = elt;
            }
          jx_numlist_cluster *cluster = scan;
          child->numlist_cluster[lib_idx] = cluster;
          child->prev_in_cluster[lib_idx] = NULL;
          if ((child->next_in_cluster[lib_idx] = cluster->numlists) != NULL)
            child->next_in_cluster[lib_idx]->prev_in_cluster[lib_idx] = child;
          cluster->numlists = child;
        }
    }
}

/*****************************************************************************/
/*                    jx_numlist_library::remove_cluster                     */
/*****************************************************************************/

void jx_numlist_library::remove_cluster(jx_numlist_cluster *cluster)
{
  assert(cluster->library == this);
  if (cluster->level < 0)
    return; // Special flag indicating that removal is already in progress
  int lib_idx = cluster->library_idx;
  assert((lib_idx >= 0) && (lib_idx < JX_LIBRARY_NL_COUNT));
  
  int level = cluster->level; // Save `level' value so we can use it below
  cluster->level = -1; // Avoid trying to remove ourselves recursively
  
  // Start by removing descendants, if there are any
  if (level == 0)
    { 
      while (cluster->numlists != NULL)
        { 
          jx_numlist *elt = cluster->numlists;
          assert(cluster == elt->numlist_cluster[lib_idx]);

          // For efficiency, it is best to first remove any non-initial
          // elements from the list of identical numlists -- that avoids
          // the need to constantly change the head of the list.
          while (elt->next_identical != NULL)
            elt->next_identical->unlink();
          elt->unlink();
          assert(cluster->numlists != elt);
          assert(elt->metanode != NULL);
          if (elt->metanode->numlist != elt)
            { // Temporary `jx_numlist' copy needs to be destroyed
              assert((elt->codestream_indices ==
                      elt->metanode->numlist->codestream_indices) &&
                     (elt->layer_indices ==
                      elt->metanode->numlist->layer_indices));
              elt->codestream_indices = NULL; // Aliased arrays; be sure not
              elt->layer_indices = NULL;      // to delete them
              delete elt;
            }
        }
    }
  else
    { 
      while (cluster->descendants != NULL)
        { 
          jx_numlist_cluster *elt = cluster->descendants;
          remove_cluster(elt);
          assert(cluster->descendants != elt);
        }
    }
  cluster->level = level; // Restore `level' value
  
  // Now unlink and delete the current cluster
  jx_numlist_cluster *parent = cluster->parent;
  jx_numlist_cluster *prev=NULL;
  jx_numlist_cluster *scan=((parent==NULL)?clusters[lib_idx]:
                            parent->descendants);
  for (; scan != NULL; prev=scan, scan=scan->next)
    if (scan == cluster)
      { 
        if (prev != NULL)
          prev->next = scan->next;
        else if (parent != NULL)
          parent->descendants = scan->next;
        else
          this->clusters[lib_idx] = scan->next;
        break;
      }
  assert(scan != NULL); // Should have found ourselves on list
  
  // Now remove parent, if it exists
  if ((parent != NULL) && (parent->descendants == NULL))
    remove_cluster(parent);
  
  cluster->parent = cluster->next = NULL;
  delete cluster;
}

/*****************************************************************************/
/*                   jx_numlist_library::enumerate_matches                   */
/*****************************************************************************/

jx_numlist *
  jx_numlist_library::enumerate_matches(jx_numlist *prev_elt,
                                        int stream_idx, int layer_idx,
                                        int base_stream_idx,
                                        int base_layer_idx,
                                        bool rendered_result,
                                        bool ignore_missing_info,
                                        bool only_non_roi_numlists)
{
  int lib_vals[JX_LIBRARY_NL_COUNT]; // Values matched for each sub-library
  int lib_idx;
  for (lib_idx=0; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
    { 
      if (lib_idx == JX_LIBRARY_NLL)
        lib_vals[lib_idx] = layer_idx;
      else if (lib_idx == JX_LIBRARY_NLC)
        lib_vals[lib_idx] = stream_idx;
      else if (lib_idx == JX_LIBRARY_C_NLL)
        lib_vals[lib_idx] = base_layer_idx;
      else if (lib_idx == JX_LIBRARY_C_NLC)
        lib_vals[lib_idx] = base_stream_idx;
      else if (lib_idx == JX_LIBRARY_NLR)
        lib_vals[lib_idx] = (rendered_result)?0:-1;
      else
        lib_vals[lib_idx] = -1; // Just in case
    }
  if (!ignore_missing_info)
    { // All conditions must be matched, so we don't need to search all libs
      if (layer_idx >= 0)
        { // Search only in the NLL and C_NLL sub-libraries
          lib_vals[JX_LIBRARY_NLC] = lib_vals[JX_LIBRARY_C_NLC] = -1;
          lib_vals[JX_LIBRARY_NLR] = -1;
        }
      else if (stream_idx >= 0)
        { // Search only in the NLC and C_NLC sub-libraries
          lib_vals[JX_LIBRARY_NLR] = -1;
        }
    }

  jx_numlist_cluster *cluster = NULL;
  bool advance_to_next_cluster = false;
  lib_idx = 0;
  if (prev_elt != NULL)
    { // Find library and cluster to which we belong
      for (; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
        { 
          if (lib_vals[lib_idx] < 0)
            continue; // Not a relevant library for this query
          cluster = prev_elt->numlist_cluster[lib_idx];
          if (cluster == NULL)
            continue;
          assert((cluster->library==this) &&
                 (cluster->library_idx==lib_idx));
          // At this point, we can be certain that `cluster' was the
          // one in which `prev_elt' was found, assuming the search
          // parameters have not changed.  This is because the C_NLL and C_NLC
          // libraries have smaller indices than NLL and NLC.  If `lib_idx'
          // is equal to `JX_LIBRARY_C_NLL', `prev_elt' must be embedded in
          // a container and `base_layer_idx' >= 0, meaning that `layer_idx'
          // is not a top-level compositing layer, so the match could only
          // have occurred with the container-defined base layer.  Similarly,
          // if `lib_idx' is equal to `JX_LIBRARY_C_NLC', `prev_elt' must be
          // embedded in a container and `base_stream_idx' >= 0, meaning that
          // `stream_idx' is not a top-level codestrem, so the match could
          // only have occurred with the container-defined codestream.
          
          jx_numlist *elt = prev_elt->next_in_cluster[lib_idx];
          for (; elt != NULL; elt=elt->next_in_cluster[lib_idx])
            { // See if `elt' is a candidate
              // See if a matching `elt' would have been matched in an
              // earlier `lib_idx'.  The following logic depends upon the
              // specific definitions of the `JX_LIBRARY_...' values.
              if ((lib_idx > 0) && (lib_vals[0] >= 0) &&
                  (elt->numlist_cluster[0] != NULL))
                continue; // Would have matched C_NLL, if a match at all
              if ((lib_idx > 1) && (lib_vals[1] >= 0) &&
                  (elt->numlist_cluster[1] != NULL))
                continue; // Would have matched C_NLC, if a match at all
              if ((lib_idx > 2) && (lib_vals[2] >= 0) &&
                  (elt->num_compositing_layers > 0))
                continue; // Would have matched C_NLL or NLL, if a match at all
              if ((lib_idx > 3) && (lib_vals[3] >= 0) &&
                  (elt->num_codestreams > 0))
                continue; // Would have matched C_NLC or NLC, if a match at all
              if (elt->check_match(stream_idx,layer_idx,base_stream_idx,
                                   base_layer_idx,rendered_result,
                                   only_non_roi_numlists,ignore_missing_info))
                return elt;
            }
          advance_to_next_cluster = true;
          break;
        }
    }
  
  for (; lib_idx < JX_LIBRARY_NL_COUNT; lib_idx++)
    { 
      int lib_val = lib_vals[lib_idx];
      if (!advance_to_next_cluster)
        cluster = this->clusters[lib_idx];
      if ((lib_val < 0) || (cluster == NULL))
        { 
          assert(!advance_to_next_cluster);
          continue; // Not a relevant library to search
        }
      assert(cluster->library_idx == lib_idx);
      
      while (cluster != NULL)
        { 
          if (advance_to_next_cluster)
            { 
              while ((cluster->next == NULL) && (cluster->parent != NULL))
                cluster = cluster->parent;
              cluster = cluster->next;
              advance_to_next_cluster = false;
            }
          else if ((cluster->range_min > lib_val) ||
                   (cluster->range_lim <= lib_val))
            advance_to_next_cluster = true;
          else if (cluster->level > 0)
            { // Descend and perform the test again
              assert(cluster->descendants != NULL); // No empty clusters
              cluster = cluster->descendants;
            }
          else
            { 
              assert(cluster->numlists != NULL); // No empty clusters
              jx_numlist *elt = cluster->numlists;
              for (; elt != NULL; elt=elt->next_in_cluster[lib_idx])
                { 
                  // See if a matching `elt' would have been matched in an
                  // earlier `lib_idx'.  The following logic depends upon the
                  // specific definitions of the `JX_LIBRARY_...' values.
                  if ((lib_idx > 0) && (lib_vals[0] >= 0) &&
                      (elt->numlist_cluster[0] != NULL))
                    continue; // Would have matched C_NLL, if a match at all
                  if ((lib_idx > 1) && (lib_vals[1] >= 0) &&
                      (elt->numlist_cluster[1] != NULL))
                    continue; // Would have matched C_NLC, if a match at all
                  if ((lib_idx > 2) && (lib_vals[2] >= 0) &&
                      (elt->num_compositing_layers > 0))
                    continue; // Would have matched C_NLL or NLL, if a match
                  if ((lib_idx > 3) && (lib_vals[3] >= 0) &&
                      (elt->num_codestreams > 0))
                    continue; // Would have matched C_NLC or NLC, if a match
                  if (elt->check_match(stream_idx,layer_idx,base_stream_idx,
                                       base_layer_idx,rendered_result,
                                       only_non_roi_numlists,
                                       ignore_missing_info))
                    return elt;
                }
              advance_to_next_cluster = true;
            }
        }
    }

  return NULL; // If we get here, we have scanned everything
}


/* ========================================================================= */
/*                                 jpx_roi                                   */
/* ========================================================================= */

/*****************************************************************************/
/*                        jpx_roi::init_quadrilateral                        */
/*****************************************************************************/

void
  jpx_roi::init_quadrilateral(kdu_coords v1, kdu_coords v2,
                              kdu_coords v3, kdu_coords v4,
                              bool coded, kdu_byte priority)
{
  is_elliptical = false; flags = JPX_QUADRILATERAL_ROI;
  is_encoded = coded; coding_priority = priority;
  elliptical_skew.x = elliptical_skew.y = 0;
  vertices[0]=v1; vertices[1]=v2; vertices[2]=v3; vertices[3]=v4;
  int v, top_v=0, min_x=v1.x, max_x=v1.x, min_y=v1.y, max_y=v1.y;
  for (v=1; v < 4; v++)
    {
      if (vertices[v].x < min_x)
        min_x = vertices[v].x;
      else if (vertices[v].x > max_x)
        max_x = vertices[v].x;
      if (vertices[v].y < min_y)
        { min_y = vertices[v].y; top_v = v; }
      else if (vertices[v].y > max_y)
        max_y = vertices[v].y;
    }
  while (top_v > 0)
    {
      v1 = vertices[0];
      for (v=0; v < 3; v++)
        vertices[v] = vertices[v+1];
      vertices[3] = v1;
      top_v--;
    }
  region.pos.x = min_x;  region.size.x = max_x + 1 - min_x;
  region.pos.y = min_y;  region.size.y = max_y + 1 - min_y;
  if ((vertices[0].x == min_x) && (vertices[0].y == min_y) &&
      (vertices[1].x == max_x) && (vertices[1].y == min_y) &&
      (vertices[2].x == max_x) && (vertices[2].y == max_y) &&
      (vertices[3].x == min_x) && (vertices[3].y == max_y))
    flags = 0; // Region is a simple rectangle
}

/*****************************************************************************/
/*                          jpx_roi::init_ellipse                            */
/*****************************************************************************/

void
  jpx_roi::init_ellipse(kdu_coords centre, kdu_coords extent,
                        kdu_coords skew, bool coded, kdu_byte priority)
{
  if (extent.x < 1) extent.x = 1;
  if (extent.y < 1) extent.y = 1;
  is_elliptical = true; flags = 0;  
  is_encoded = coded; coding_priority = priority;
  region.size.x = (extent.x<<1)+1;
  region.size.y = (extent.y<<1)+1;
  region.pos.x = centre.x - extent.x;
  region.pos.y = centre.y - extent.y;
  elliptical_skew = skew;
  double gamma;
  if (!compute_gamma_and_extent(gamma,extent))
    { // Need to regenerate compatible skew values
      skew.x = (int) floor(0.5+gamma*extent.x);
      skew.y = (int) floor(0.5+gamma*extent.y);
    }
  if (skew.x <= -extent.x) skew.x = 1-extent.x;
  if (skew.x >= extent.x) skew.x = extent.x-1;
  if (skew.y <= -extent.y) skew.y = 1-extent.y;
  if (skew.y >= extent.y) skew.y = extent.y-1;
  elliptical_skew = skew;
}

/*****************************************************************************/
/*                    jpx_roi::compute_gamma_and_extent                      */
/*****************************************************************************/

bool
  jpx_roi::compute_gamma_and_extent(double &gamma, kdu_coords &extent) const
{
  assert(is_elliptical);
  kdu_coords skew = elliptical_skew;
  extent.x = region.size.x>>1;
  extent.y = region.size.y>>1;
  if (extent.x < 1) extent.x = 1;
  if (extent.y < 1) extent.y = 1;
  if (skew.x < -extent.x)
    skew.x = 1-extent.x;
  else if (skew.x > extent.x)
    skew.x = extent.x-1;
  if (skew.y < -extent.y)
    skew.y = 1-extent.y;
  else if (skew.y > extent.y)
    skew.y = extent.y-1;  
  double inv_Wo = 1.0/extent.x, inv_Ho = 1.0/extent.y;
  
     /* Discussion:
        We should have `skew.x' and `skew.y' within +/-0.5 of Sx and Sy, where
        Sx=gamma*Wo and Sy=gamma*Ho.  Suppose that this is true.  Then
        gamma must lie in the intervals (skew.x-0.5)/Wo to (skew.x+0.5)/Wo
        and (skew.y-0.5)/Ho to (skew.y+0.5)/Ho. If these intervals
        intersect, we take gamma to be the value in the mid-point of the
        intersection. */

  double lb1 = (skew.y-0.5)*inv_Ho; // One lower bound
  double lb2 = (skew.x-0.5)*inv_Wo; // The other lower bound
  double ub1 = (skew.y+0.5)*inv_Ho; // One upper bound
  double ub2 = (skew.x+0.5)*inv_Wo; // The other upper bound
  double lb = (lb1>lb2)?lb1:lb2;
  double ub = (ub1<ub2)?ub1:ub2;
  if ((lb <= (ub+0.0001)) &&
      (skew.x > -extent.x) && (skew.x < extent.x) &&
      (skew.y > -extent.y) && (skew.y < extent.y))
    { 
      gamma = (lb+ub)*0.5;
      return (skew == elliptical_skew);
    }
  
  // If we get here, we have to synthesize a gamma value from the inconsistent
  // `skew.x' and `skew.y' values.  We do this in such a way that the
  // synthesized gamma can be multiplied by Ho and Wo and rounded to the
  // nearest integer so as to derive `skew.x' and `skew.y' values which are
  // both consistent and legal.  This means that the derived `skew.x' and
  // `skew.y' values using this method must have absolute values which are
  // smaller than `extent.x' and `extent.y', respectively.
  ub1 = (extent.x+0.4)*inv_Wo; // So rounding stays within skew_range.x
  ub2 = (extent.y+0.4)*inv_Ho; // So rounding stays within skew_range.y
  ub = (ub1 < ub2)?ub1:ub2;
  assert(ub >= 0.0);
  if ((skew.x ^ skew.y) < 0)
    gamma = 0.0; // Incompatible signs!!
  else
    gamma = sqrt((skew.x*inv_Wo) * (skew.y*inv_Ho));
  if (gamma > ub)
    gamma = ub;
  assert(gamma < 1.0);
  if ((skew.x+skew.y) < 0)
    gamma = -gamma;
  return false;
}

/*****************************************************************************/
/*                          jpx_roi::init_ellipse                            */
/*****************************************************************************/

void
  jpx_roi::init_ellipse(kdu_coords centre, const double axis_extents[2],
                        double tan_theta, bool coded, kdu_byte priority)
{
  // Here is how the algorithm here works:
  //    First, we recognize that the boundary of the initial cardinally aligned
  // ellipse is described by:
  //       x = U * sin(phi), y = V * cos(phi)
  // where (x,y) is the displacement of a boundary point from the centre, phi
  // is an angular parameter, and V=axis_extents[0] and U=axis_extents[1].
  //    Next, we recognize that after rotation of the ellipse, the boundary
  // points are given by:
  //       xr = x*cos(theta)-y*sin(theta);  yr = y*cos(theta)+x*sin(theta).
  // Then the top/bottom boundaries of the oriented ellipse's bounding box are
  // encountered at parameter phi for which (d yr)/(d phi) = 0.  Now some
  // elementary maths yields
  //       tan(phi) = (U/V) * tan(theta).
  // Thence, we find that
  //       Ho = +/-yr(phi) = V*cos(phi)cos(theta) + U*sin(phi)sin(theta)
  //       skew.x = +/-xr(phi) = V*cos(phi)sin(theta) - U*sin(phi)cos(theta)
  //                           = U*sin(theta)cos(phi)*[V/U - U/V]
  // The left/right boundaries of the oriented ellipse's bounding box are
  // encountered at parameter phi for which (d xr)/(d phi) = 0.  Write this
  // value of phi as psi (to avoid confusion).  Then some elementary maths
  // yields
  //       cot(psi) = -(V/U) * tan(theta) = -(V^2 / U^2) tan(phi)
  // It is convenient to define zeta = psi +/- pi/2 such that |zeta| < pi
  // and
  //       tan(zeta) = -cot(psi).
  // Then
  //       tan(zeta) = V/U * tan(theta) = (V^2 / U^2) tan(phi)
  // Thence, we find that
  //       Wo = +/-xr(psi) = U*cos(zeta)cos(theta) + V*sin(zeta)sin(theta)
  // and
  //       skew.y = +/-yr(psi) = V*sin(zeta)*cos(theta)-U*cos(zeta)*sin(theta)
  //                           = V*sin(theta)cos(zeta)*[V/U - U/V]
  // In the above, we have chosen the signs carefully so as to satisfy the
  // requirements that Ho and Wo must be positive, and skew.x and skew.y
  // must each have the same sign as theta if U < V and the opposite sign to
  // theta if U > V.
  //   The above equations can actually be simplified to the following:
  //       Ho = V*cos(theta)/cos(phi)
  //       Wo = U*cos(theta)/cos(zeta)
  // with
  //       tan(phi) = U/V * tan(theta)    and     tan(zeta) = V/U * tan(theta)
  // and
  //       skew.x = Wo * gamma     and     skew.y = Ho * gamma
  // with
  //       gamma = tan(theta) * cos(phi) * cos(zeta) * [V/U - U/V]
  
  double V = axis_extents[0], U = axis_extents[1];
  if (U < 0.25) U = 0.25;
  if (V < 0.25) V = 0.25;
  if (tan_theta < -1.0)
    tan_theta = -1.0;
  else if (tan_theta > 1.0)
    tan_theta = 1.0;
  double tan_phi  = U * tan_theta / V;
  double tan_zeta = V * tan_theta / U;
  double cos_phi = sqrt(1.0 / (1.0 + tan_phi*tan_phi));
  double cos_zeta = sqrt(1.0 / (1.0 + tan_zeta*tan_zeta));
  double cos_theta = sqrt(1.0 / (1.0 + tan_theta*tan_theta));
  double Ho = V*cos_theta/cos_phi;
  double Wo = U*cos_theta/cos_zeta;
  double gamma = tan_theta * cos_phi * cos_zeta * (V/U - U/V);
  double skew_x = Wo * gamma;
  double skew_y = Ho * gamma;
  
  kdu_coords extent, skew;
  extent.x = (int) floor(Wo+0.5);
  extent.y = (int) floor(Ho+0.5);
  skew.x = (int) floor(skew_x+0.5);
  skew.y = (int) floor(skew_y+0.5);
  init_ellipse(centre,extent,skew,coded,priority);
}

/*****************************************************************************/
/*                          jpx_roi::get_ellipse                             */
/*****************************************************************************/

bool
  jpx_roi::get_ellipse(kdu_coords &centre, double axis_extents[2],
                       double &tan_theta) const
{
  if (!is_elliptical) return false;

  // This function essentially inverts the mathematical process used by
  // the second form of the `init_ellipse' function.  The inversion is
  // not so straightforward, but the following results hold:
  //    Let R=Wo/Ho be the known ratio between the width and the height of
  // the bounding box of the oriented ellipse.  Also, let rho=U/V be the
  // (initially unknown) ratio between the width and the height of the
  // cardinally aligned ellipse prior to clockwise rotation through angle
  // theta.  Also, let gamma be the skewing parameter, whose value is
  // derived in the `init_ellipse' function to be:
  //         gamma = tan(theta) * cos(phi) * cos(zeta) * [1/rho - rho]
  // where
  //         tan(phi) = rho * tan(theta)     and
  //         tan(zeta) = 1/rho * tan(theta).
  // The values of Wo, Ho and gamma are recovered by calling the
  // internal helper function, `compute_gamma_and_extent'.  Using the
  // relationships deried above for the second `init_ellipse' function, it
  // is known that
  //          R = rho * cos(phi) / cos(zeta).
  // Thus far, things are quite simple.  After quite some manipulation, it
  // can be shown that the solution for rho and tan(theta) is given by
  //          rho = sqrt[Q +/- sqrt(Q^2 - 1)]
  //          tan(theta) = (R^2 - rho^2) / ((1 + rho^2) * R * gamma)
  // where
  //          Q = (R^4 + 2*gamma^2*R^2 + 1) / (2*R^2*(1-gamma^2))
  // Moreover, the two solutions for Q (due to the +/- terms in the above
  // equation) correspond to reciprocal values for rho (in which U is
  // interchanged with V) and values of theta which are separated by pi/2.
  // In other words, the two solutions for Q simply correspond to different
  // interpretations concerning which of the two elliptical axes is initially
  // considered to lie in the horizontal direction (prior to clockwise
  // rotation).  According to the definition of the present function, we
  // pick the solution for which -pi/4 <= theta <= pi/4, i.e., for which
  // -1 <= tan(theta) <= 1.
  //    Note that once we have tan(theta) and rho, we can compute
  //           tan(phi) = rho * tan(theta)
  //           tan(zeta) = 1/rho * tan(theta)
  //           axis_extents[0] = Ho * cos(phi)/cos(theta)  and
  //           axis_extents[1] = Wo * cos(zeta)/cos(theta).
  
  centre.x = region.pos.x + (region.size.x>>1);
  centre.y = region.pos.y + (region.size.y>>1);
  kdu_coords extent;
  double gamma;
  compute_gamma_and_extent(gamma,extent);
  double gamma_sq = gamma*gamma;
  if ((elliptical_skew == kdu_coords()) || (gamma_sq <= 0.0))
    {
      tan_theta = 0.0;
      axis_extents[0] = extent.y;
      axis_extents[1] = extent.x;
    }
  else
    { 
      double Wo = extent.x, Ho = extent.y;
      double R = Wo / Ho;
      double R_sq = R*R;
      double Q = ((R_sq*R_sq + 2.0*gamma_sq*R_sq + 1.0) /
                  (2.0*R_sq*(1.0-gamma_sq)));
      double rho_sq = Q + sqrt(Q*Q-1.0);
      tan_theta = (R_sq - rho_sq) / ((1.0 + rho_sq) * R * gamma);
      if ((tan_theta < -1.0) || (tan_theta > 1.0))
        {
          rho_sq = 1.0 / rho_sq;
          tan_theta = (R_sq - rho_sq) / ((1.0 + rho_sq) * R * gamma);
        }
      double rho = sqrt(rho_sq);
      double tan_phi = rho * tan_theta;
      double tan_zeta = tan_theta / rho;
      axis_extents[0] = Ho * sqrt((1.0 + tan_theta*tan_theta) /
                                  (1.0 + tan_phi*tan_phi));
      axis_extents[1] = Wo * sqrt((1.0 + tan_theta*tan_theta) /
                                  (1.0 + tan_zeta*tan_zeta));
    }
  return true;
}

/*****************************************************************************/
/*                        jpx_roi::fix_inconsistencies                       */
/*****************************************************************************/

void jpx_roi::fix_inconsistencies()
{
  if (region.size.x < 1) region.size.x = 1;
  if (region.size.y < 1) region.size.y = 1;
  if (is_elliptical)
    {
      region.size.x |= 1;  region.size.y |= 1;
      if (elliptical_skew.x || elliptical_skew.y)
        {
          double gamma;  kdu_coords extent;
          if ((!compute_gamma_and_extent(gamma,extent)) ||
              (elliptical_skew.x <= -extent.x) ||
              (elliptical_skew.x >= extent.x) ||
              (elliptical_skew.y <= -extent.y) ||
              (elliptical_skew.y >= extent.y))
            {
              elliptical_skew.x = (int) floor(0.5+gamma*extent.x);
              elliptical_skew.y = (int) floor(0.5+gamma*extent.y);
            }
        }
    }
  else if (flags & JPX_QUADRILATERAL_ROI)
    init_quadrilateral(vertices[0],vertices[1],vertices[2],vertices[3],
                       is_encoded,coding_priority);
}

/*****************************************************************************/
/*                            jpx_roi::clip_region                           */
/*****************************************************************************/

void jpx_roi::clip_region()
{
  kdu_coords min=region.pos, max=min+region.size-kdu_coords(1,1);
  if (min.x < 0) min.x = 0;
  if (min.y < 0) min.y = 0;
  if (max.x < min.x) max.x = min.x;
  if (max.y < min.y) max.y = min.y;
  if (max.x > 0x7FFFFFFE) max.x = 0x7FFFFFFE;
  if (max.y > 0x7FFFFFFE) max.y = 0x7FFFFFFE;
  if (min.x > max.x) min.x = max.x;
  if (min.y > max.y) min.y = max.y;
  kdu_dims new_region;
  new_region.pos = min;
  new_region.size = max-min+kdu_coords(1,1);
  if (is_elliptical)
    {
      kdu_coords new_centre, new_extent;
      new_extent.x = new_region.size.x >> 1;
      new_extent.y = new_region.size.y >> 1;
      new_centre = new_region.pos + new_extent;
      if (new_extent.x < 1) new_extent.x = 1;
      if (new_extent.y < 1) new_extent.y = 1;
      min = new_centre - new_extent;
      max = new_centre + new_extent;
      if (max.x > 0x7FFFFFFE)
        new_centre.x = 0x7FFFFFFE - new_extent.x;
      else if (min.x < 0)
        new_centre.x = new_extent.x;
      if (max.y > 0x7FFFFFFE)
        new_centre.y = 0x7FFFFFFE - new_extent.y;
      else if (min.y < 0)
        new_centre.y = new_extent.y;
      new_region.pos = new_centre - new_extent;
      new_region.size = new_extent + new_extent + kdu_coords(1,1);
      if (new_region == region)
        return;
      if (elliptical_skew.x || elliptical_skew.y)
        {
          // Recompute the skew parameters
          kdu_coords extent, skew=elliptical_skew;
          double gamma;
          compute_gamma_and_extent(gamma,extent);
          double x = gamma*new_extent.x, y=gamma*new_extent.y;
          skew.x = (int) floor(x+0.5);
          skew.y = (int) floor(y+0.5);
          if (skew.x <= -new_extent.x) skew.x = 1-new_extent.x;
          if (skew.x >= new_extent.x) skew.x = new_extent.x-1;
          if (skew.y <= -new_extent.y) skew.y = 1-new_extent.y;
          if (skew.y >= new_extent.y) skew.y = new_extent.y-1;
          elliptical_skew = skew;
        }
      region = new_region;
    }
  else if (!(flags & JPX_QUADRILATERAL_ROI))
    { // Simple rectangular region
      region = new_region;
    }
  else
    { 
      int p;
      for (p=0; p < 4; p++)
        { 
          if (vertices[p].x < 0) vertices[p].x = 0;
          if (vertices[p].x > 0x7FFFFFFE) vertices[p].x = 0x7FFFFFFE;
          if (vertices[p].y < 0) vertices[p].y = 0;
          if (vertices[p].y > 0x7FFFFFFE) vertices[p].y = 0x7FFFFFFE;
          if (p == 0)
            min = max = vertices[p];
          else
            { 
              if (vertices[p].x < min.x) min.x = vertices[p].x;
              if (vertices[p].x > max.x) max.x = vertices[p].x;
              if (vertices[p].y < min.y) min.y = vertices[p].y;
              if (vertices[p].y > max.y) max.y = vertices[p].y;
            }
        }
      region.pos = min;
      region.size = max-min+kdu_coords(1,1);
    }
}


/*****************************************************************************/
/*                          jpx_roi::check_geometry                          */
/*****************************************************************************/

bool
  jpx_roi::check_geometry() const
{
  // Start by checking that the bounding box is legal
  if ((region.pos.x < 0) || (region.pos.y < 0) ||
      (region.size.x <= 0) || (region.size.y <= 0) ||
      ((region.pos.x+region.size.x) < 0) ||
      ((region.pos.y+region.size.y) < 0))
    return false;
  
  if (is_elliptical)
    { // Check the skew parameters
      kdu_coords extent;
      extent.x = region.size.x >> 1;  extent.y = region.size.y >> 1;
      if ((extent.x < 1) || (extent.y < 1) ||
          (region.size.x != (2*extent.x+1)) ||
          (region.size.y != (2*extent.y+1)) ||
          (elliptical_skew.x <= -extent.x) ||
          (elliptical_skew.x >= extent.x) ||
          (elliptical_skew.y <= -extent.y) ||
          (elliptical_skew.y >= extent.y))
        return false;
      if (elliptical_skew.x || elliptical_skew.y)
        { 
          double gamma;
          if (!compute_gamma_and_extent(gamma,extent))
            return false;
        }
      return true;
    }
  if (!(flags & JPX_QUADRILATERAL_ROI))
    return true;
  
  int p;
  for (p=0; p < 4; p++)
    if ((vertices[p].x < 0) || (vertices[p].y < 0) ||
        (vertices[p].x > 0x7FFFFFFE) || (vertices[p].y > 0x7FFFFFFE))
      return false;
  
  /* To determine whether the vertices are ordered in a clockwise direction,
     we measure the orthogonal (righward) distance of the points
     A = vertices[1] and B = vertices[3] from the directed line segment
     (U -> W) where U = vertices[0] and W = vertices[2].  The orientation is
     clockwise if and only if B lies at least as far to the right as A.
        We use the following mathematical result to assess rightward orthogonal
     distance.  If U, W and Z are three vectors, a perpendicular vector in
     the rightward direction from line segment (U -> W) is V, where
     Vy = Wx-Ux and Vx = -(Wy-Uy).  The rightward displacement of Z relative
     to the directed line segment (U -> W) is then given by
       d = [(Wx-Ux)(Zy-Uy) - (Wy-Uy)(Zx-Ux)] / |V|
   */
  kdu_long dx = vertices[2].x; dx -= vertices[0].x; // this is Wx-Ux
  kdu_long dy = vertices[2].y; dy -= vertices[0].y; // this is Wy-Uy
  kdu_coords a_vec = vertices[1] - vertices[0]; // (Z-U) for point A
  kdu_coords b_vec = vertices[3] - vertices[0]; // (Z-U) for point B
  if ((dx*(b_vec.y-a_vec.y) - dy*(b_vec.x-a_vec.x)) < 0)
    return false;

  if (check_edge_intersection(0,vertices[2],vertices[3]))
    return false; // Checks intersection between edges 0-1 and 2-3
  if (check_edge_intersection(3,vertices[1],vertices[2]))
    return false; // Checks intersection between edges 3-0 and 1-2
  
  return true;
}

/*****************************************************************************/
/*                     jpx_roi::check_edge_intersection                      */
/*****************************************************************************/

bool
  jpx_roi::check_edge_intersection(int n, kdu_coords C, kdu_coords D) const
{
  kdu_coords A = vertices[n], B = vertices[(n+1) & 3];
  kdu_long AmB_x=A.x-B.x, AmB_y=A.y-B.y;
  kdu_long DmC_x=D.x-C.x, DmC_y=D.y-C.y;
  kdu_long AmC_x=A.x-C.x, AmC_y=A.y-C.y;
  
  kdu_long det = AmB_x * DmC_y - DmC_x * AmB_y;
  kdu_long t_val = DmC_y*AmC_x - DmC_x * AmC_y;
  kdu_long u_val = AmB_x*AmC_y - AmB_y * AmC_x;
  if (det < 0.0)
    { det = -det; t_val = -t_val; u_val = -u_val; }
  return ((t_val > 0) && (t_val < det) && (u_val > 0) && (u_val < det));
}

/*****************************************************************************/
/*                          jpx_roi::measure_span                            */
/*****************************************************************************/

int
  jpx_roi::measure_span(double &tightest_width, double &tightest_length) const
{
  if (is_elliptical)
    {
      kdu_coords centre;
      double axis_extents[2], tan_theta;
      get_ellipse(centre,axis_extents,tan_theta);
      if (axis_extents[0] < axis_extents[1])
        {
          tightest_width = 1+2*axis_extents[0];
          tightest_length = 1+2*axis_extents[1];
          return 1;
        }
      else
        {
          tightest_width = 1+2*axis_extents[1];
          tightest_length = 1+2*axis_extents[0];
          return 0;          
        }
    }
  if (!(flags & JPX_QUADRILATERAL_ROI))
    {
      if (region.size.x < region.size.y)
        { // Longer dimension is aligned vertically
          tightest_width = region.size.x; tightest_length = region.size.y;
          return 1;
        }
      else
        { // Longer dimension is aligned horizontally
          tightest_width = region.size.y; tightest_length = region.size.x;
          return 0;
        }
    }

  int n, tightest_n=0;
  tightest_width = tightest_length = 0.0;
  
  for (n=0; n < 4; n++)
    { // Consider the edge U->W running from vertex[n] to vertex[(n+1) mod 4]
      
      /* We use the following mathematical result to assess rightward
         orthogonal distance.  A perpendicular vector in the rightward
         direction from line segment (U -> W) is V, where Vy = Wx-Ux and
         Vx = -(Wy-Uy).  The rightward displacement of a third point Z,
         relative to the directed line segment (U -> W) is then given by
         d = [(Wx-Ux)(Zy-Uy) - (Wy-Uy)(Zx-Ux)] / |V|. */
      
      int m = (n+1) & 3;
      if (vertices[m] == vertices[n])
        continue;
      double Ux=vertices[n].x,     Uy=vertices[n].y;
      double WUx=vertices[m].x-Ux, WUy=vertices[m].y-Uy;
      double inv_V_mag = 1.0 / sqrt(WUx*WUx + WUy*WUy);
           // This is both the reciprocal of the length of vector V and of
           // of vector U->W.
      
      double ZUx, ZUy, d1, d2;
      m = (m+1) & 3; // Consider the next vertex as Z
      ZUx=vertices[m].x-Ux;  ZUy=vertices[m].y-Uy;
      d1 = (WUx*ZUy - WUy*ZUx) * inv_V_mag;
      m = (m+1) & 3; // Consider the final vertex as Z
      ZUx=vertices[m].x-Ux;  ZUy=vertices[m].y-Uy;
      d2 = (WUx*ZUy - WUy*ZUx) * inv_V_mag;
      
      double width;
      if (d1 <= d2)
        {
          if (d1 < 0.0)
            width = (d2 > 0.0)?(d2-d1):-d1;
          else
            width = d2;
        }
      else
        { // d2 < d1
          if (d2 < 0.0)
            width = (d1 > 0.0)?(d1-d2):-d2;
          else
            width = d1;
        }
      
      if ((n > 0) && (width >= tightest_width))
        continue; // Not a candidate
      
      tightest_width = width;
      tightest_n = n;

      // Calculate length of this bounding rectangle by projecting each
      // vertex onto the vector U->W.  The unit vector in this direction has
      // coordinates (WUx*inv_V_mag, WUy*inv_V_mag).
      double proj, proj_min=0.0, proj_max=0.0;
      for (int p=0; p < 4; p++)
        {
          proj = WUx*vertices[p].x + WUy*vertices[p].y;
          if (p == 0)
            proj_min = proj_max = proj;
          else if (proj < proj_min)
            proj_min = proj;
          else if (proj > proj_max)
            proj_max = proj;
        }
      tightest_length = (proj_max-proj_min) * inv_V_mag;
    }
  tightest_width += 1.0;  tightest_length += 1.0; // Account for pixel size
  return tightest_n;
}

/*****************************************************************************/
/*                          jpx_roi::measure_area                            */
/*****************************************************************************/

double
  jpx_roi::measure_area(double &centroid_x, double &centroid_y) const
{
  centroid_x = region.pos.x + 0.5*(region.size.x-1);
  centroid_y = region.pos.y + 0.5*(region.size.y-1);
  double area = (double) region.area();  
  if (is_elliptical)
    {
      kdu_coords extent; double gamma;
      compute_gamma_and_extent(gamma,extent);
      area = JX_PI * (extent.x+0.5) * (extent.y+0.5) * sqrt(1-gamma*gamma);
      return area;
    }
  if (!(flags & JPX_QUADRILATERAL_ROI))
    return area;

  // If we get here, we have a general quadrilateral.  We analyze this by
  // dividing it into 3 trapezoids, each of which have their top and bottom
  // edges aligned horizontally.
  jx_trapezoid trap[3];
  trap[0].init(vertices[0],vertices[3],vertices[0],vertices[1]);
  if ((vertices[2].y < vertices[1].y) && (vertices[2].y < vertices[3].y))
    { // Vertex 2 lies above vertices 1 and 3 which join to the top
      kdu_long V1m0_y=((kdu_long) vertices[1].y) - ((kdu_long) vertices[0].y);
      kdu_long V1m0_x=((kdu_long) vertices[1].x) - ((kdu_long) vertices[0].x);
      kdu_long V2m0_y=((kdu_long) vertices[2].y) - ((kdu_long) vertices[0].y);
      kdu_long V2m0_x=((kdu_long) vertices[2].x) - ((kdu_long) vertices[0].x);
      kdu_long V3m0_y=((kdu_long) vertices[3].y) - ((kdu_long) vertices[0].y);
      kdu_long V3m0_x=((kdu_long) vertices[3].x) - ((kdu_long) vertices[0].x);
      if (((V2m0_y*V3m0_x) <= (V2m0_x*V3m0_y)) &&
          ((V2m0_y*V1m0_x) >= (V2m0_x*V1m0_y))) 
        { // Vertex 2 lies between the left & right edges of trap-0
          trap[0].max_y = vertices[2].y;
          trap[1].init(vertices[0],vertices[3],vertices[2],vertices[3]);
          trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
        }
      else if (vertices[3].y < vertices[1].y)
        { // Vertex 2 lies to the left of the left edge of trap-0
          trap[1].init(vertices[2],vertices[1],vertices[2],vertices[3]);
          trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
          trap[2].min_y = vertices[3].y;
        }
      else
        { // Vertex 2 lies to the right of the right edge of trap-0
          trap[1].init(vertices[2],vertices[1],vertices[2],vertices[3]);
          trap[2].init(vertices[0],vertices[3],vertices[2],vertices[3]);
          trap[2].min_y = vertices[1].y;
        }
    }
  else if (vertices[3].y < vertices[1].y)
    { // Vertex 3 lies above vertices 1 and 3
      trap[1].init(vertices[3],vertices[2],vertices[0],vertices[1]);
      if (vertices[2].y < vertices[1].y)
        trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
      else
        trap[2].init(vertices[3],vertices[2],vertices[1],vertices[2]);
    }
  else
    { // Vertex 1 lies above vertices 2 and 3
      trap[1].init(vertices[0],vertices[3],vertices[1],vertices[2]);
      if (vertices[2].y < vertices[3].y)
        trap[2].init(vertices[0],vertices[3],vertices[2],vertices[3]);
      else
        trap[2].init(vertices[3],vertices[2],vertices[1],vertices[2]);    
    }
  
  double cumulative_area=0.0;
  double area_weighted_cx=0.0, area_weighted_cy=0.0;
  for (int t=0; t < 3; t++)
    {
      if (trap[t].min_y > trap[t].max_y)
        continue;
      int min_y = trap[t].min_y, max_y = trap[t].max_y;
      kdu_coords delta_L=trap[t].left2 - trap[t].left1;
      kdu_coords delta_R=trap[t].right2 - trap[t].right1;
      double grad_L = delta_L.x / (double) delta_L.y;
      double grad_R = delta_R.x / (double) delta_R.y;
      double x1_L = trap[t].left1.x + grad_L*(min_y-trap[t].left1.y);
      double x1_R = trap[t].right1.x + grad_R*(min_y-trap[t].right1.y);
      double height = max_y-min_y;
      double x2_L = x1_L + grad_L * height;
      double x2_R = x1_R + grad_R * height;
      double width1 = x1_R-x1_L, width2 = x2_R-x2_L;
      double local_area = (height+1.0) * (1.0 + 0.5 * (width1+width2));
      cumulative_area += local_area;
      double delta_width = width2-width1;
      double delta_cy_over_height =
        (0.5*width1 + (1/3.0)*delta_width) / (width1 + 0.5*delta_width);
      double x1 = 0.5*(x1_L+x1_R), x2 = 0.5*(x2_L+x2_R);
      double cx = x1 + (x2-x1) * delta_cy_over_height;
      double cy = min_y + height * delta_cy_over_height;
      area_weighted_cx += cx*local_area;
      area_weighted_cy += cy*local_area;
    }
  
  if (cumulative_area > 0.0)
    { // Otherwise, the quadrilateral has height 0 and the parameters
      // previously computed from the bounding box are correct.
      area = cumulative_area;
      centroid_x = area_weighted_cx * (1.0 / cumulative_area);
      centroid_y = area_weighted_cy * (1.0 / cumulative_area);
    }
  return area;
}

/*****************************************************************************/
/*                            jpx_roi::contains                              */
/*****************************************************************************/

bool
  jpx_roi::contains(kdu_coords point) const
{
  kdu_coords off = point - region.pos;
  if ((off.x < 0) || (off.x >= region.size.x) ||
      (off.y < 0) || (off.y >= region.size.y))
    return false;
  if (is_elliptical)
    {
      kdu_coords extent;
      double gamma;
      compute_gamma_and_extent(gamma,extent);
      double Ho=extent.y, Wo=extent.x;
      double Wi = Wo*sqrt(1.0-gamma*gamma);
      double alpha = gamma * Wo / Ho;
      double y = off.y - Ho;
      double x = off.x - Wo + alpha*y;
        // Now we just need to check whether
        //       x^2/Wi^2 + y^2/Ho^2 <= 1
        // Equivalently, we check whether
        //       x^2 * Ho^2 + y^2 * Wi^2 <= Wi^2 * Ho^2
        // To make the test a little more resilient to rounding errors, we
        // will allow for the point to be up to half a sample away from the
        // boundary, this means that we first need to bring x and y in towards
        // (0,0) by 0.5.
    
      if (x > 0.5)
        x -= 0.5;
      else if (x < -0.5)
        x += 0.5;
      if (y > 0.5)
        y -= 0.5;
      else if (y < -0.5)
        y += 0.5;
      double Wi_sq=Wi*Wi, Ho_sq = Ho*Ho;
      if ((x*x*Ho_sq + y*y*Wi_sq) > Ho_sq * Wi_sq)
        return false;
    }
  if (!(flags & JPX_QUADRILATERAL_ROI))
    return true;
  
  // If we get here, we need to determine membership of a general
  // quadrilateral defined by the `vertices' member.  We will do this by
  // dividing the quadrilateral into 3 trapezoids, each of which have
  // their top and bottom edges aligned horizontally.
  jx_trapezoid trap[3];
  trap[0].init(vertices[0],vertices[3],vertices[0],vertices[1]);
  if ((vertices[2].y < vertices[1].y) && (vertices[2].y < vertices[3].y))
    { // Vertex 2 lies above vertices 1 and 3 which join to the top
      kdu_long V1m0_y=((kdu_long) vertices[1].y) - ((kdu_long) vertices[0].y);
      kdu_long V1m0_x=((kdu_long) vertices[1].x) - ((kdu_long) vertices[0].x);
      kdu_long V2m0_y=((kdu_long) vertices[2].y) - ((kdu_long) vertices[0].y);
      kdu_long V2m0_x=((kdu_long) vertices[2].x) - ((kdu_long) vertices[0].x);
      kdu_long V3m0_y=((kdu_long) vertices[3].y) - ((kdu_long) vertices[0].y);
      kdu_long V3m0_x=((kdu_long) vertices[3].x) - ((kdu_long) vertices[0].x);
      if (((V2m0_y*V3m0_x) <= (V2m0_x*V3m0_y)) &&
          ((V2m0_y*V1m0_x) >= (V2m0_x*V1m0_y))) 
        { // Vertex 2 lies between the left & right edges of trap-0
          trap[0].max_y = vertices[2].y;
          trap[1].init(vertices[0],vertices[3],vertices[2],vertices[3]);
          trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
        }
      else if (vertices[3].y < vertices[1].y)
        { // Vertex 2 lies to the left of the left edge of trap-0
          trap[1].init(vertices[2],vertices[1],vertices[2],vertices[3]);
          trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
          trap[2].min_y = vertices[3].y;
        }
      else
        { // Vertex 2 lies to the right of the right edge of trap-0
          trap[1].init(vertices[2],vertices[1],vertices[2],vertices[3]);
          trap[2].init(vertices[0],vertices[3],vertices[2],vertices[3]);
          trap[2].min_y = vertices[1].y;
        }
    }
  else if (vertices[3].y < vertices[1].y)
    { // Vertex 3 lies above vertices 1 and 3
      trap[1].init(vertices[3],vertices[2],vertices[0],vertices[1]);
      if (vertices[2].y < vertices[1].y)
        trap[2].init(vertices[2],vertices[1],vertices[0],vertices[1]);
      else
        trap[2].init(vertices[3],vertices[2],vertices[1],vertices[2]);
    }
  else
    { // Vertex 1 lies above vertices 2 and 3
      trap[1].init(vertices[0],vertices[3],vertices[1],vertices[2]);
      if (vertices[2].y < vertices[3].y)
        trap[2].init(vertices[0],vertices[3],vertices[2],vertices[3]);
      else
        trap[2].init(vertices[3],vertices[2],vertices[1],vertices[2]);    
    }
  for (int t=0; t < 3; t++)
    {
      if ((point.y < trap[t].min_y) || (point.y > trap[t].max_y))
        continue;
      double x1, x2, y1, y2, x, y=point.y;
          
      x = point.x + 0.5; // Right edge of pixel must be insider border
      x1 = trap[t].left1.x;  x2 = trap[t].left2.x;
      if (trap[t].left1.y < trap[t].left2.y)
        {
          x-= x1;  y1 = trap[t].left1.y;  y2 = trap[t].left2.y;
          if (((x * (y2-y1)) < ((y-0.5-y1)*(x2-x1))) &&
              ((x * (y2-y1)) < ((y+0.5-y1)*(x2-x1))))
            continue; // Right edge of point lies entirely to left of border
        }
      else if ((x < x1) && (x < x2))
        continue; // Point lies to left of horiz. left edge segment
          
      x = point.x - 0.5; // Left edge of pixel must be insider border
      x1 = trap[t].right1.x;  x2 = trap[t].right2.x;
      if (trap[t].right1.y < trap[t].right2.y)
        {
          x -= x1;  y1 = trap[t].right1.y;  y2 = trap[t].right2.y;
          if (((x * (y2-y1)) > ((y-0.5-y1)*(x2-x1))) &&
              ((x * (y2-y1)) > ((y+0.5-y1)*(x2-x1))))
            continue; // Left edge of point lies entirely to right of border
        }
      else if ((x > x1) && (x > x2))
        continue; // Point lies to right of horiz. right edge segment
    
      return true; // If we get here, the point belongs to trapezoid trap[t].
    }
  return false;
}

/*****************************************************************************/
/*                    jpx_roi::find_boundary_projection                      */
/*****************************************************************************/

int
  jpx_roi::find_boundary_projection(double x0, double y0,
                                    double &xp, double &yp,
                                    double max_distance,
                                    double tolerance) const
{
  double max_dist_sq = max_distance*max_distance;
  if ((!is_elliptical) && !(flags & JPX_QUADRILATERAL_ROI))
    { // Start with rectangular regions
      double mid_x = region.pos.x + 0.5*(region.size.x-1);
      double mid_y = region.pos.y + 0.5*(region.size.y-1);
      int result;
      if (x0 < mid_x)
        { xp = region.pos.x; result = 0; }
      else
        { xp = region.pos.x+region.size.x-1; result = 1; }
      if (y0 < mid_y)
        yp = region.pos.y;
      else
        { yp = region.pos.y+region.size.y-1; result = 3-result; }
      double dist_sq = (xp-x0)*(xp-x0) + (yp-y0)*(yp-y0);
      return (dist_sq > max_dist_sq)?-1:result;
    }
  if (flags & JPX_QUADRILATERAL_ROI)
    { // General quadrilaterals are not too bad; project onto each edge in turn
      // For an edge (A,B), we first find t such that the vector from (x0,y0)
      // to A+t(B-A) is orthogonal to (A,B); after that the projection onto
      // line segment (A,B) is: A+t(B-A) if 0 < t < 1; A if t<=0; B if t>=1.
      // The equation we need to solve is:
      //    (x0-Ax-t(Bx-Ax))*(Bx-Ax) + (y0-Ay-t(By-Ay))*(By-Ay) = 0.
      // That is,
      //    (x0-Ax)(Bx-Ax) + (y0-Ay)(By-Ay) = t*[(Bx-Ax)^2 + (By-Ay)^2].
      int p, best_p=0;
      double best_dist_sq=0.0;
      for (p=0; p < 4; p++)
        { 
          double Ax=vertices[p].x, Ay=vertices[p].y;
          double Bx=vertices[(p+1)&3].x, By=vertices[(p+1)&3].y;
          double den = (Bx-Ax)*(Bx-Ax) + (By-Ay)*(By-Ay);
          double num = (x0-Ax)*(Bx-Ax) + (y0-Ay)*(By-Ay);
          double t;
          if (num <= 0.0)
            t = 0.0; // Solution would involve t <= 0
          else if (num >= den)
            t = 1.0; // Solution would involve t >= 1
          else
            t = num/den; // Division is safe since above tests failed
          double x = Ax+t*(Bx-Ax);
          double y = Ay+t*(By-Ay);
          double dist_sq = (x0-x)*(x0-x) + (y0-y)*(y0-y);
          if ((p==0) || (dist_sq < best_dist_sq))
            { xp = x; yp = y; best_dist_sq = dist_sq; best_p = p; }
        }
      return (best_dist_sq > max_dist_sq)?-1:best_p;
    }
  
  /* If we get here, we are dealing with an ellipse; these are hard.
     Here is the theory:
        First, let `a' be the horizontal skewing parameter and Ho and
     Wi be half the exterior height and half the interior width,
     respectively.  The interior of the ellipse is described by
        (x+ay)^2 / Wi^2 + y^2 / Ho^2 <= 1
     where (x,y) is expressed relative to the centre of the ellipse.
     Equivalently,
        A(x+ay)^2 + By^2 <= C, with A=Ho^2, B=Wi^2 and C=Ho^2*Wi^2.
     So, assuming we have already subtracted the ellipse's centre coordinates
     from x0 and y0, we want to find (x,y) which minimizes
        (x-x0)^2+(y-y0)^2, subject to A(x+ay)^2 + By^2 = C.
        Using the method of Lagrange multipliers, our problem is equivalent to
     minimizing
        (x-x0)^2+(y-y0)^2 + lambda * [ A(x+ay)^2 + By^2 - C]
     for some lambda, where lambda is positive if (x0,y0) lies outside the
     ellipse and negative if (x0,y0) lies inside the ellipse.  The solution
     to this quadratic problem is readily obtained by differentiation.
     Specifically, we must have
        (x-x0) + lambda * A(x+ay) = 0
        (y-y0) + lambda * Aa(x+ay) + lambda * B * y = 0
     So
        | 1+lambda*A          lambda*A*a        |   | x |   | x0 |
        |                                       | * |   | = |
        | lambda*A*a   1+lambda*a^2*A+lambda*B  |   | y |   | y0 |
     which is readily solved to yield
                  | 1+lambda*(a^2*A+B)   -lambda*A*a |
        (x,y)^t = |    -lambda*A*a        1+lambda*A | * (x0,y0)^2
                  ------------------------------------
             (1+lambda*A)(1+lambda*(a^2*A+B))-(lambda*A*a)^2
     Now all we need to do is search for a value of lambda which yields
     a solution to A(x+ay)^2 + By^2 <= C.  The implementation here, uses
     a bisection search, starting with lambda=0 (to determine whether or
     not (x0,y0) lies inside the ellipse).  This approach is fine since
     we do not need very high accuracy.  Once the value of
     |A(x+ay)^2 + By^2 - C| is smaller than (2A|x+ay|+2B|y|)*`tolerance',
     we can stop.
  */
     
  kdu_coords extent, centre;
  centre.x = region.pos.x + (region.size.x>>1);
  centre.y = region.pos.y + (region.size.y>>1);
  double gamma;
  compute_gamma_and_extent(gamma,extent);
  double ho = extent.y;
  double ho_sq = ho*ho;
  double wo = extent.x;
  double wi_sq = wo*wo*(1.0-gamma*gamma);
  double alpha = elliptical_skew.x / ho;
  x0 -= centre.x;  y0 -= centre.y;
  double A = ho_sq, B = wi_sq, C = ho_sq*wi_sq;
  
  double min_lambda=0.0, max_lambda=0.0;
  double Cval = A*(x0+alpha*y0)*(x0+alpha*y0) + B*y0*y0;
  bool have_both_bounds = false;
  bool inside_ellipse = false;
  if (Cval < C) // (x0,y0) is inside the ellipse
    { min_lambda = -1.0/(A+B); inside_ellipse = true; }
  else // (x0,y0) is outside the ellipes
    { max_lambda = 1.0/(A+B); inside_ellipse = false; }
  double Ctol = 0.0; // Recalculate this inside the loop below
  if (tolerance <= 0.0)
    tolerance = 0.01; // Just in case
  double dist_sq = 0.0; // Computed as we go
  do {
    double lambda = (min_lambda+max_lambda)*0.5;
    double A11=1+lambda*A, A12=lambda*A*alpha, A22=1+lambda*(alpha*alpha*A+B);
    double det = A11*A22 - A12*A12;
    if (det == 0.0)
      return -1; // This can only potentially happen if (x0,y0) is inside
    double inv_det = 1.0/det;
    xp = (A22*x0-A12*y0)*inv_det;
    yp = (A11*y0-A12*x0)*inv_det;
    dist_sq = (xp-x0)*(xp-x0) + (yp-y0)*(yp-y0);
    double x_term = xp+alpha*yp;
    double x_sensitivity = A*x_term, y_sensitivity = B*yp;
    Cval = x_term*x_sensitivity + yp*y_sensitivity;
    Ctol = (x_sensitivity<0.0)?(-x_sensitivity):x_sensitivity;
    Ctol += (y_sensitivity<0.0)?(-y_sensitivity):y_sensitivity;
    if (Ctol < 0.1)
      Ctol = 0.1; // Just in case ellipse has zero size or something like that
    Ctol *= tolerance;
    if (!have_both_bounds)
      {
        if (inside_ellipse)
          {
            if (Cval < C)
              min_lambda *= 2.0; // Still inside the ellipse
            else
              { have_both_bounds=true; min_lambda=lambda; }
          }
        else
          { 
            if (Cval > C)
              max_lambda *= 2.0; // Still outside the ellipse
            else
              { have_both_bounds=true; max_lambda=lambda; }
          }
      }
    else
      {
        if (Cval < C)
          max_lambda = lambda;
        else
          min_lambda = lambda;
        if (inside_ellipse && (Cval < C) && (dist_sq > max_dist_sq))
          return -1; // Projection onto a smaller ellipse already too far away
        else if ((!inside_ellipse) && (Cval > C) && (dist_sq > max_dist_sq))
          return -1; // Projection onto a larger ellipse already too far away
      }
  } while ((Cval < (C-Ctol)) || (Cval > (C+Ctol)));
  xp += centre.x;
  yp += centre.y;
  
  return (dist_sq > max_dist_sq)?-1:0;
}


/* ========================================================================= */
/*                              jpx_roi_editor                               */
/* ========================================================================= */

/*****************************************************************************/
/*                          jpx_roi_editor::reset                            */
/*****************************************************************************/

void jpx_roi_editor::reset()
{
  region_idx = edge_idx = anchor_idx = -1;
  mode = JPX_EDITOR_VERTEX_MODE; num_regions = 0;
  jpx_roi_editor *scan;
  while ((scan=prev) != NULL)
    { prev=scan->prev; scan->is_current=false; delete scan; }
  while ((scan=next) != NULL)
    { next=scan->next; scan->is_current=false; delete scan; }
  num_undo_elements = 0;
  path_edge_flags_valid = shared_edge_flags_valid = false;
  num_scribble_points = num_subsampled_scribble_points = 0;
}

/*****************************************************************************/
/*                    jpx_roi_editor::set_max_undo_history                   */
/*****************************************************************************/

void jpx_roi_editor::set_max_undo_history(int history)
{
  max_undo_elements = history;
  if (num_undo_elements > history)
    {
      num_undo_elements = history;
      jpx_roi_editor *scan, *last=this;
      for (; history > 0; history--, last=last->prev);
      while ((scan=last->prev) != NULL)
        {
          last->prev = scan->prev;
          scan->is_current = false;
          delete scan;
        }
    }
}

/*****************************************************************************/
/*                           jpx_roi_editor::undo                            */
/*****************************************************************************/

kdu_dims jpx_roi_editor::undo()
{
  kdu_dims result = cancel_selection();
  jpx_roi_editor *undo_elt = prev;
  if (undo_elt == NULL)
    return result;

  kdu_dims bb;
  this->get_bounding_box(bb,true);
  result.augment(bb);
  
  // Make a copy of `this'
  jpx_roi_editor save_this = *this;
  save_this.is_current = false; // So destructor will do no harm
  
  // Make `undo_elt' current
  *this = *undo_elt;
  this->is_current = true;
  this->max_undo_elements = save_this.max_undo_elements;
  this->num_undo_elements = save_this.num_undo_elements-1;
  this->mode = save_this.mode;
  if (this->prev != NULL)
    this->prev->next = this;
  
  // Move `save_this' into the most immediate redo position, re-using the
  // `undo_elt' record for this purpose.
  jpx_roi_editor *redo_elt = undo_elt;
  *redo_elt = save_this;
  this->next = redo_elt;
  redo_elt->prev = this;
  if (redo_elt->next != NULL)
    redo_elt->next->prev = redo_elt;
  
  this->get_bounding_box(bb,true);
  result.augment(bb);
  path_edge_flags_valid = shared_edge_flags_valid = false;
  return result;  
}

/*****************************************************************************/
/*                           jpx_roi_editor::redo                            */
/*****************************************************************************/

kdu_dims jpx_roi_editor::redo()
{
  kdu_dims result = cancel_selection();
  jpx_roi_editor *redo_elt = next;
  if (redo_elt == NULL)
    return result;
  
  kdu_dims bb;
  this->get_bounding_box(bb,true);
  result.augment(bb);
  
  // Make a copy of `this'
  jpx_roi_editor save_this = *this;
  save_this.is_current = false; // So destructor will do no harm
  
  // Make `redo_elt' current
  *this = *redo_elt;
  this->is_current = true;
  this->max_undo_elements = save_this.max_undo_elements;
  this->num_undo_elements = save_this.num_undo_elements+1;
  this->mode = save_this.mode;
  if (this->next != NULL)
    this->next->prev = this;
  
  // Move `save_this' into the most immediate undo position, re-using the
  // `redo_elt' record for this purpose.
  jpx_roi_editor *undo_elt = redo_elt;
  *undo_elt = save_this;
  this->prev = undo_elt;
  undo_elt->next = this;
  if (undo_elt->prev != NULL)
    undo_elt->prev->next = undo_elt;
  
  this->get_bounding_box(bb,true);
  result.augment(bb);
  path_edge_flags_valid = shared_edge_flags_valid = false;
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::push_current_state                     */
/*****************************************************************************/

void jpx_roi_editor::push_current_state()
{
  // Remove the redo list
  jpx_roi_editor *scan;
  while ((scan=next) != NULL)
    { next = scan->next; scan->is_current=false; delete scan; }
  
  if (max_undo_elements <= 0)
    return;
  
  // See if we need to drop the least recent element from the undo list
  if (num_undo_elements >= max_undo_elements)
    {
      num_undo_elements = max_undo_elements-1;
      int count = num_undo_elements;
      jpx_roi_editor *last=this;
      for (; count > 0; count--, last=last->prev);
      while ((scan=last->prev) != NULL)
        {
          last->prev = scan->prev;
          scan->is_current = false;
          delete scan;
        }
    }
  
  // Make a new element for the undo list
  jpx_roi_editor *undo_elt = new jpx_roi_editor;
  *undo_elt = *this;
  undo_elt->is_current = false;
  if (undo_elt->prev != NULL)
    undo_elt->prev->next = undo_elt;
  undo_elt->next = this;
  prev = undo_elt;
  this->num_undo_elements++;
  
  // Remove selection properties from `undo_elt'
  undo_elt->anchor_idx = undo_elt->region_idx = undo_elt->edge_idx = -1;
  memset(undo_elt->drag_flags,0,(size_t)undo_elt->num_regions);
  undo_elt->path_edge_flags_valid = undo_elt->shared_edge_flags_valid = false;
}

/*****************************************************************************/
/*                          jpx_roi_editor::equals                           */
/*****************************************************************************/

bool jpx_roi_editor::equals(const jpx_roi_editor &rhs) const
{
  if (num_regions != rhs.num_regions)
    return false;
  for (int n=0; n < num_regions; n++)
    if (regions[n] != rhs.regions[n])
      return false;
  return true;
}

/*****************************************************************************/
/*                           jpx_roi_editor::init                            */
/*****************************************************************************/

void jpx_roi_editor::init(const jpx_roi *regs, int num_regions)
{
  if ((num_regions < 0) || (num_regions > 255))
    { KDU_ERROR_DEV(e,0x18021001); e <<
      KDU_TXT("Invalid set of ROI regions supplied to "
              "`jpx_roi_editor::init'.");
    }
  this->num_regions = num_regions;
  for (int n=0; n < num_regions; n++)
    { 
      jpx_roi *dest = this->regions+n;
      *dest = regs[n];
      update_extremities(dest);
    }
  region_idx = edge_idx = anchor_idx = -1;
  path_edge_flags_valid = shared_edge_flags_valid = false;
}

/*****************************************************************************/
/*                      jpx_roi_editor::get_bounding_box                     */
/*****************************************************************************/

bool jpx_roi_editor::get_bounding_box(kdu_dims &bb,
                                      bool include_scribble) const
{
  if (num_regions <= 0)
    return false;
  int n;
  kdu_dims result;
  for (n=0; n < num_regions; n++)
    { 
      result.augment(regions[n].region);
      for (int p=0; p < 4; p++)
        result.augment(regions[n].vertices[p]);
    }
  if (include_scribble)
    for (n=0; n < num_scribble_points; n++)
      result.augment(scribble_points[n]);
  bb = result;
  return true;
}

/*****************************************************************************/
/*                          jpx_roi_editor::set_mode                         */
/*****************************************************************************/

kdu_dims jpx_roi_editor::set_mode(jpx_roi_editor_mode mode)
{
  kdu_dims result;
  if (mode == this->mode)
    return result;
  result = cancel_selection();
  this->mode = mode;
  kdu_dims bb; get_bounding_box(bb,false);
  if (!bb.is_empty())
    result.augment(bb);
  return result;
}
  
/*****************************************************************************/
/*                     jpx_roi_editor::find_nearest_anchor                   */
/*****************************************************************************/

bool
  jpx_roi_editor::find_nearest_anchor(kdu_coords &point,
                                      bool modify_for_selection) const
{
  if (num_regions == 0)
    return false;
  kdu_coords ref=point;
  kdu_long min_dist=-1;
  for (int n=0; n < num_regions; n++)
    {
      kdu_coords v[5];
      int p, num_anchors = find_anchors(v,regions[n]);
      if ((num_anchors == 4) && regions[n].is_elliptical)
        { // Add the ellipse centre as a point to be searched
          kdu_coords extent, skew;
          regions[n].get_ellipse(v[4],extent,skew);
          num_anchors = 5;
        }
      for (p=0; p < num_anchors; p++)
        {
          if ((n == region_idx) && (p == anchor_idx) && modify_for_selection)
            continue;
          kdu_long dx=v[p].x-ref.x, dy=v[p].y-ref.y;
          kdu_long dist = dx*dx + dy*dy;
          if ((min_dist < 0) || (dist < min_dist))
            {
              point = v[p];
              min_dist = dist;
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                jpx_roi_editor::find_nearest_boundary_point                */
/*****************************************************************************/

bool
  jpx_roi_editor::find_nearest_boundary_point(kdu_coords &point,
                                              bool exclude_sel_region) const
{
  if (num_regions == 0)
    return false;
  double x0=point.x, y0=point.y;
  double nearest_xp=x0, nearest_yp=y0;
  kdu_dims bb; get_bounding_box(bb,false);
  double min_distance = (bb.size.x>bb.size.y)?bb.size.x:bb.size.y;
  bool have_something = false;
  for (int n=0; n < num_regions; n++)
    { 
      if (exclude_sel_region && (n == region_idx))
        continue;
      double xp=x0, yp=y0;
      if (regions[n].find_boundary_projection(x0,y0,xp,yp,min_distance) < 0)
        continue;
      have_something = true;
      double dx = xp-x0, dy = yp-y0;
      double distance = sqrt(dx*dx + dy*dy);
      assert(distance <= min_distance);
      min_distance = distance;
      nearest_xp = xp;
      nearest_yp = yp;
    }
  point.x = (int) floor(0.5+nearest_xp);
  point.y = (int) floor(0.5+nearest_yp);
  return have_something;
}

/*****************************************************************************/
/*                 jpx_roi_editor::find_nearest_guide_point                  */
/*****************************************************************************/

bool
  jpx_roi_editor::find_nearest_guide_point(kdu_coords &point) const
{
  if ((region_idx < 0) || (region_idx >= num_regions) || (anchor_idx < 0))
    return false;
  const jpx_roi *roi = regions+region_idx;

  if (mode == JPX_EDITOR_VERTEX_MODE)
    { // Vertex mode
      if (roi->is_elliptical)
        { 
          kdu_coords test = point;
          if (jx_project_to_line(roi->vertices[anchor_idx],
                                 roi->vertices[(anchor_idx+2)&3],test) &&
              (test != anchor_point))
            { point = test; return true; }
          else
            return false;
        }
      
      // If we get here, we are considering a quadrilateral region's guidelines
      kdu_long dx, dy, dist_sq, min_dist_sq=-1;
      kdu_coords src = point;
      for (int e=0; e < 2; e++)
        { // Candidates are the edges running through the anchor point
          kdu_coords test=src;
          if (!jx_project_to_line(roi->vertices[anchor_idx],
                                  roi->vertices[(anchor_idx+2*e-1)&3],test))
            continue;
          if (test == anchor_point)
            continue;
          dx = test.x - (kdu_long) src.x;  dy = test.y - (kdu_long) src.y;
          dist_sq = dx*dx + dy*dy;
          if ((min_dist_sq < 0) || (dist_sq < min_dist_sq))
            { point = test; min_dist_sq = dist_sq; }
        }
      return (min_dist_sq >= 0);
    }
  else
    { // Skeleton and path modes are very similar
      if (roi->is_elliptical)
        return false;

      // If we get here, we are considering a quadrilateral region's guidelines
      kdu_long dx, dy, dist_sq, min_dist_sq=-1;
      kdu_coords src = point;
      for (int e=0; e < 3; e++)
        { // Three candidate guidelines
          if ((e != 1) && (mode != JPX_EDITOR_SKELETON_MODE))
            continue; // Path mode has only one guideline, rather than 3
          kdu_coords from = roi->vertices[(anchor_idx-1+e)&3];
          kdu_coords to = roi->vertices[(anchor_idx+e)&3];
          kdu_coords disp; // Displacement for edge to make it run thru anchor
          if (e == 0)
            disp = anchor_point - to;
          else if (e == 2)
            disp = anchor_point - from;
          from += disp; to += disp;
          kdu_coords test=src;
          if (!jx_project_to_line(from,to,test))
            continue;
          if (test == anchor_point)
            continue;
          dx = test.x - (kdu_long) src.x;  dy = test.y - (kdu_long) src.y;
          dist_sq = dx*dx + dy*dy;
          if ((min_dist_sq < 0) || (dist_sq < min_dist_sq))
            { point = test; min_dist_sq = dist_sq; }          
        }
      return (min_dist_sq >= 0);
    }
  
  return false;
}

/*****************************************************************************/
/*                       jpx_roi_editor::select_anchor                       */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::select_anchor(kdu_coords point, bool advance)
{
  kdu_dims result;
  int n, p, num_anchors;
  kdu_coords anchors[4];
  
  // First figure out what to do with an existing selection
  if ((anchor_idx >= 0) && (region_idx >= 0) && (region_idx < num_regions))
    {
      num_anchors = find_anchors(anchors,regions[region_idx]);
      if ((anchor_idx < num_anchors) && (anchors[anchor_idx] == point))
        {
          if (!advance)
            {
              anchor_point = point;
              return result; // No change, leave selection exactly as is
            }
        }
      else
        result = cancel_selection();
    }
  else
    result = cancel_selection();

  if (anchor_idx < 0)
    memset(drag_flags,0,(size_t) num_regions);
  else
    {
      result = cancel_drag();
      anchor_point = dragged_point = point;
      jpx_roi *roi = regions+region_idx;
      kdu_dims edge_region = get_edge_region(roi,edge_idx);
      result.augment(edge_region);
    }
  assert(dragged_point == anchor_point);

  // At this point, we are either finding a new anchor point from scratch
  // (`anchor_idx' < 0), or we are advancing an existing anchor selection
  // (identified by `anchor_idx', `region_idx' and `edge_idx').
  
  bool trying_to_advance = (anchor_idx >= 0);
  for (n=0; n < num_regions; n++)
    {
      num_anchors = find_anchors(anchors,regions[n]);
      jpx_roi *roi = regions+n;
      for (p=0; p < num_anchors; p++)
        {
          if (point == anchors[p])
            {
              result.augment(point);
              if (anchor_idx < 0)
                {
                  anchor_idx = p;
                  region_idx = n;
                  edge_idx = find_next_anchor_edge();
                }
              else if ((anchor_idx == p) && (region_idx == n))
                {
                  if ((edge_idx = find_next_anchor_edge()) < 0)
                    { 
                      anchor_idx = region_idx = -1;
                      memset(drag_flags,0,(size_t) num_regions);
                      continue;
                    }
                }
              else
                continue;

              anchor_point = dragged_point = point;
              if (mode == JPX_EDITOR_VERTEX_MODE)
                { // Only the anchor point moves
                  drag_flags[n] |= (kdu_byte)(1<<p);
                  if (!roi->is_elliptical)
                    set_drag_flags_for_vertex(point);
                }
              else if (roi->is_elliptical)
                { // The entire ellipse moves
                  drag_flags[n] |= 0x0F;
                  set_drag_flags_for_boundary(roi);
                }
              else
                { // The vertices defining the edge containing the anchor move
                  drag_flags[n] |= (kdu_byte)((1<<p) | (1<<((p+1)&3)));
                  set_drag_flags_for_vertex(roi->vertices[p]);
                  set_drag_flags_for_vertex(roi->vertices[(p+1)&3]);
                  set_drag_flags_for_midpoint(point);
                }
              kdu_dims edge_region = get_edge_region(roi,edge_idx);
              result.augment(edge_region);
              return result;
            }
        }
    }
  if ((anchor_idx < 0) && trying_to_advance)
    { // Give it another go
      kdu_dims second_result = select_anchor(point,false);
      if (!second_result.is_empty())
        result.augment(second_result);
    }
  return result;
}

/*****************************************************************************/
/*                      jpx_roi_editor::cancel_selection                     */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::cancel_selection()
{
  kdu_dims result = cancel_drag();
  if ((region_idx >= 0) && (region_idx < num_regions) &&
      (anchor_idx >= 0) && (anchor_idx < 4))
    { 
      jpx_roi *sel_roi = regions + region_idx;
      result.augment(anchor_point);
      if (sel_roi->is_elliptical)
        { 
          update_extremities(sel_roi);
          result.augment(sel_roi->region);
        }
      else
        { 
          kdu_coords from, to;
          get_edge_vertices(sel_roi,edge_idx,from,to);
          result.augment(from);
          result.augment(to);
        }
    }
  region_idx = edge_idx = anchor_idx = -1;
  anchor_point = dragged_point = kdu_coords();
  memset(drag_flags,0,(size_t) num_regions);
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::drag_selected_anchor                  */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::drag_selected_anchor(kdu_coords new_point)
{
  if ((region_idx < 0) || (anchor_idx < 0) || (region_idx >= num_regions))
    return kdu_dims();
  if (new_point == dragged_point)
    return kdu_dims();
    
  kdu_dims result = cancel_drag();
  dragged_point = new_point;
  kdu_dims new_region = cancel_drag(); // Useful way to find affected region
  dragged_point = new_point;
  if (!new_region.is_empty())
    result.augment(new_region);
  return result;
}

/*****************************************************************************/
/*                  jpx_roi_editor::can_move_selected_anchor                 */
/*****************************************************************************/

bool
  jpx_roi_editor::can_move_selected_anchor(kdu_coords new_point,
                                           bool check_roid_limit) const
{
  if ((anchor_idx < 0) || (anchor_idx >= 4) ||
      (region_idx < 0) || (region_idx >= num_regions))
    return false;

  kdu_coords disp = new_point - anchor_point;
  if ((disp.x == 0) && (disp.y == 0))
    return false;
  if (regions[region_idx].is_elliptical && (mode == JPX_EDITOR_VERTEX_MODE))
    { // Displacement needs to be even to be sure of having an effect, since
      // ellipse locations are determined by the centres.
      disp.x = (disp.x>0)?(disp.x+(disp.x&1)):(disp.x-(disp.x&1));
      disp.y = (disp.y>0)?(disp.y+(disp.y&1)):(disp.y-(disp.y&1));
      new_point = anchor_point + disp;
    }

  int num_roid_elts = 0;
  for (int n=0; n < num_regions; n++)
    { 
      if (drag_flags[n] != 0)
        { 
          jpx_roi test_roi = regions[n];
          move_vertices(&test_roi,drag_flags[n],disp);
          if (!test_roi.check_geometry())
            return false; // Failed to create a valid region
          num_roid_elts += (test_roi.is_simple())?1:2;
        }
      else
        num_roid_elts += (regions[n].is_simple())?1:2;
    }
  if (check_roid_limit && (num_roid_elts > 255))
    return false;
  return true;
}

/*****************************************************************************/
/*                    jpx_roi_editor::move_selected_anchor                   */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::move_selected_anchor(kdu_coords new_point)
{
  kdu_dims result = cancel_drag();
  if (!can_move_selected_anchor(new_point,false))
    return result;
  if (!can_move_selected_anchor(new_point,true))
    { KDU_WARNING(w,0x09031001); w <<
      KDU_TXT("ROI shape edit will cause the maximum number of regions in "
              "the JPX ROI Description box to be exceeded.  Delete or "
              "simplify some regions to simple rectangles or ellipses.");
      return result;
    }
  
  kdu_coords disp = new_point - anchor_point;
  if ((disp.x == 0) && (disp.y == 0))
    return result;
  if (regions[region_idx].is_elliptical && (mode == JPX_EDITOR_VERTEX_MODE))
    { // Displacement needs to be even to be sure of having an effect, since
      // ellipse locations are determined by the centres.
      disp.x = (disp.x>0)?(disp.x+(disp.x&1)):(disp.x-(disp.x&1));
      disp.y = (disp.y>0)?(disp.y+(disp.y&1)):(disp.y-(disp.y&1));
      new_point = anchor_point + disp;
    }
  push_current_state();
  
  // Now perform the move itself
  int n;
  kdu_dims existing_bb; get_bounding_box(existing_bb,false);
  result.augment(existing_bb);
  
  // Now consider all the other regions
  for (n=0; n < num_regions; n++)
    if (drag_flags[n] != 0)
      move_vertices(regions+n,drag_flags[n],disp);
  kdu_dims new_bb;
  get_bounding_box(new_bb,false);
  result.augment(new_bb);

  find_nearest_anchor(new_point,false);
  select_anchor(new_point,true);
  dragged_point = anchor_point;
  path_edge_flags_valid = shared_edge_flags_valid = false;
  
  result.augment(remove_duplicates());
  return result;
}

/*****************************************************************************/
/*                        jpx_roi_editor::cancel_drag                        */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::cancel_drag()
{
  if ((anchor_idx < 0) || (anchor_idx >= 4) ||
      (region_idx < 0) || (region_idx >= num_regions) ||
      (anchor_point == dragged_point))
    return kdu_dims(); // Nothing to do; no drag happening
  kdu_coords disp = dragged_point - anchor_point;
  kdu_dims result; result.pos = dragged_point; result.size = kdu_coords(1,1);
  
  int n, p;
  kdu_byte mask;
  for (n=0; n < num_regions; n++)
    {
      if (drag_flags[n] == 0)
        continue; // No points of this region affected by dragging
      if (regions[n].is_elliptical)
        { // Entire dragged ellipse needs to be included in redraw region
          jpx_roi roi_copy = regions[n];
          if (drag_flags[n] != 0)
            move_vertices(&roi_copy,drag_flags[n],disp);
          result.augment(roi_copy.region);
        }
      else
        { // Only the dragged edges need to be included
          for (p=0, mask=1; p < 4; p++, mask<<=1)
            if (drag_flags[n] & mask)
              {
                result.augment(regions[n].vertices[p] + disp);
                for (int i=-1; i <= 1; i+=2)
                  { 
                    int ep = (p+i) & 3;
                    kdu_coords end_point = regions[n].vertices[ep];
                    if (drag_flags[n] & (kdu_byte)(1<<ep))
                      end_point += disp;
                    result.augment(end_point);
                  }
              }
        }
    }
  
  dragged_point = anchor_point;
  return result;
}

/*****************************************************************************/
/*                       jpx_roi_editor::get_selection                       */
/*****************************************************************************/

int jpx_roi_editor::get_selection(kdu_coords &point, int &num_instances) const
{
  if ((region_idx < 0) || (region_idx >= num_regions) || (anchor_idx < 0))
    return -1;
  point = this->anchor_point;
  num_instances = 0;
  int n, p;
  for (n=0; n < num_regions; n++)
    {
      kdu_coords anchors[4];
      int num_anchors = find_anchors(anchors,regions[n]);
      for (p=0; p < num_anchors; p++)
        if (anchors[p] == point)
          num_instances++;
    }
  return region_idx;
}

/*****************************************************************************/
/*                   jpx_roi_editor::split_selected_anchor                   */
/*****************************************************************************/

kdu_dims jpx_roi_editor::split_selected_anchor()
{
  kdu_dims result;
  if ((anchor_idx < 0) || (region_idx < 0) || (region_idx >= num_regions))
    return result;
  
  push_current_state();
  
  kdu_coords disp, v[4];
  int n, p, num_anchors, trial;
  bool unique = false;
  for (trial=0; (trial < 9) && !unique; trial++)
    { 
      jpx_roi *sel_roi = regions+region_idx;
      switch (trial % 3) {
        case 0: disp.x=0; break;
        case 1: disp.x=1; break;
        case 2: disp.x=-1; break;
      };
      switch ((trial / 3) % 3) {
        case 0: disp.y=0; break;
        case 1: disp.y=1; break;
        case 2: disp.y=-1; break;
      };
      
      kdu_coords new_point = anchor_point + disp;
      for (unique=true, n=0; (n < num_regions) && unique; n++)
        { 
          num_anchors = find_anchors(v,regions[n]);
          for (p=0; p < num_anchors; p++)
            if ((v[p] == new_point) &&
                ((p != anchor_idx) || (n != region_idx)))
              break;
          if (p < num_anchors)
            unique = false;
        }
      if (unique && (disp.x || disp.y))
        { 
          p = anchor_idx;
          jpx_roi new_roi = *sel_roi;
          kdu_byte dflags = 0;
          if (new_roi.is_elliptical)
            dflags = 0x0F; // Move the entire ellipse -- only safe action
          else if (mode == JPX_EDITOR_VERTEX_MODE)
            dflags = (kdu_byte)(1<<p);
          else
            dflags = (kdu_byte)((1<<p) | (1<<((p+1)&3)));
          move_vertices(&new_roi,dflags,disp);
          if (!new_roi.check_geometry())
            { unique = false; continue; }
          int save_edge_idx = edge_idx;
          result = cancel_selection();
          *sel_roi = new_roi;
          select_anchor(new_point,false);
          if (edge_idx != save_edge_idx)
            select_anchor(new_point,true);
          if (edge_idx != save_edge_idx)
            select_anchor(new_point,false);
          result.augment(sel_roi->region);
        }
    }
  
  path_edge_flags_valid = shared_edge_flags_valid = false;
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::clear_scribble_points                  */
/*****************************************************************************/

kdu_dims jpx_roi_editor::clear_scribble_points()
{
  if (num_scribble_points != 0)
    push_current_state();
  kdu_dims result;
  for (int n=0; n < num_scribble_points; n++)
    result.augment(scribble_points[n]);
  num_scribble_points = num_subsampled_scribble_points = 0;
  return result;
}

/*****************************************************************************/
/*                      jpx_roi_editor::add_scribble_point                   */
/*****************************************************************************/

kdu_dims jpx_roi_editor::add_scribble_point(kdu_coords point)
{
  kdu_dims result;
  
  // First see if we have to sub-sample the existing points
  if (num_scribble_points == JX_ROI_SCRIBBLE_POINTS)
    {
      if (num_subsampled_scribble_points >= (num_scribble_points-1))
        num_subsampled_scribble_points = 0; // Start sub-sampling from scratch
      int p, n=num_subsampled_scribble_points-1;;
      result.augment(scribble_points[n-1]);
      for (; n < num_scribble_points; n++)
        { // Remove the point at location `n'
          result.augment(scribble_points[n]);
          num_scribble_points--;
          for (p=n; p < num_scribble_points; p++)
            scribble_points[p] = scribble_points[p+1];
        }
      num_subsampled_scribble_points = num_scribble_points;
    }
  
  // Now look for loops to eliminate;
  bool possible_loops = true;
  while ((num_scribble_points > 1) && possible_loops)
    { 
      kdu_coords last_point = scribble_points[num_scribble_points-1];
      result.augment(last_point);
      if (point == last_point)
        return result;
      
      // Let line segment A->B be defined by `point' and `last_point', and
      // scan through all the other line segments, denoting them C->D.
      // Note that the last line segment scanned has D = B, but it is
      // sufficient to check for an intersection between the half-open
      // line segments [A,B) and [C,D], since the `last_point' B was
      // checked for intersection when it was added.  This ensures that
      // we never misdetect an intersection between the new line segment
      // and the last one.
      int p, n;
      for (n=0; n < (num_scribble_points-1); n++)
        if (jx_check_line_intersection(point,last_point,false,true,
                                       scribble_points[n],
                                       scribble_points[n+1]))
          break;
      if (n == (num_scribble_points-1))
        possible_loops = false;
      else
        { // Found a loop
          if (n > (num_scribble_points/2))
            { // Eliminate everything from point n+1 to the end
              for (p=n+1; p < num_scribble_points; p++)
                result.augment(scribble_points[p]);
              num_scribble_points = n+1;
            }
          else
            { // Eliminate everything up to point n.
              for (p=0; p <= n; p++)
                result.augment(scribble_points[p]);
              for (n++; p < num_scribble_points; p++)
                scribble_points[p-n] = scribble_points[p];
              num_scribble_points -= n;
            }
        }
    }
  
  result.augment(point);
  scribble_points[num_scribble_points++] = point;
  return result;
}

/*****************************************************************************/
/*                   jpx_roi_editor::convert_scribble_path                   */
/*****************************************************************************/

kdu_dims jpx_roi_editor::convert_scribble_path(bool replace_content,
                                               int conversion_flags,
                                               double accuracy)
{
  if (num_scribble_points <= 1)
    return kdu_dims();
  
  // First convert `accuracy' into a reasonable `max_sq_dist' value
  int n;
  kdu_long max_sq_dist = 1;
  if (accuracy < 1.0)
    { 
      kdu_coords min=scribble_points[0], max=min;
      for (n=1; n < num_scribble_points; n++)
        { 
          if (scribble_points[n].x < min.x)
            min.x = scribble_points[n].x;
          else if (scribble_points[n].x > max.x)
            max.x = scribble_points[n].x;
          if (scribble_points[n].y < min.y)
            min.y = scribble_points[n].y;
          else if (scribble_points[n].y > max.y)
            max.y = scribble_points[n].y;
        }
      kdu_coords red_size = max-min;
      red_size.x = (red_size.x+3)>>2;  red_size.y = (red_size.y+3)>>2;
      max_sq_dist = red_size.x;  max_sq_dist *= red_size.y;
      if (accuracy > 0.0)
        { 
          double max_val = (double) max_sq_dist;
          double scale = exp(accuracy * log(max_val));
          max_sq_dist = (kdu_long)(0.5 + max_val/scale);
        }
    }
  
  jx_scribble_converter *converter = new jx_scribble_converter;
  bool want_fill = ((conversion_flags & JPX_EDITOR_FLAG_FILL) != 0);
  if (want_fill && (num_scribble_points > 2) &&
      (scribble_points[0] != scribble_points[num_scribble_points-1]))
    { // Temporarily backup the scribble points, so we can close the loop
      // before initializing the converter
      int npoints = num_scribble_points;
      kdu_coords *backup = new kdu_coords[npoints];
      memcpy(backup,scribble_points,sizeof(kdu_coords)*((size_t) npoints));
      add_scribble_point(scribble_points[0]);
      converter->init(scribble_points,num_scribble_points,want_fill);
      num_scribble_points = npoints;
      memcpy(scribble_points,backup,sizeof(kdu_coords)*((size_t) npoints));
      delete[] backup;
    }
  else
    converter->init(scribble_points,num_scribble_points,want_fill);
  
  jx_path_filler *path_filler = NULL;
  try {
    if (!converter->find_polygon_edges(max_sq_dist))
      throw ((int) 0);
    if (!converter->find_boundary_vertices())
      throw ((int) 0);
    int needed_regions = 0;
    if (want_fill)
      { 
        path_filler = new jx_path_filler;
        if (!path_filler->init(converter->boundary_vertices,
                               converter->num_boundary_vertices))
          throw ((int) 0);
        if (!path_filler->process())
          { KDU_WARNING(w,0x05051001); w <<
            KDU_TXT("Unable to completely fill the region bounded by the "
                    "scribble path; this is probably an internal flaw; "
                    "you could either fill the unfilled portion manually "
                    "or try again with a different complexity setting.");
          }
        needed_regions = path_filler->num_regions;
      }
    else
      { 
        needed_regions = (converter->num_boundary_vertices-1);
        if (conversion_flags & JPX_EDITOR_FLAG_ELLIPSES)
          needed_regions += converter->num_boundary_vertices;
      }
    if ((needed_regions < 1) || (needed_regions > 255) ||
        (replace_content && (needed_regions > (255-this->num_regions))))
      throw ((int) 0);
  } catch (...) {
    if (converter != NULL)
      delete converter;
    if (path_filler != NULL)
      delete path_filler;
    return kdu_dims();
  }

  // If we get here, we are committed to making the changes
  push_current_state();
  kdu_dims result;
  if (replace_content)    
    { 
      get_bounding_box(result,false);
      result.augment(cancel_selection());
      num_regions = 0;
    }
  
  path_edge_flags_valid = shared_edge_flags_valid = false;
  if (path_filler != NULL)
    { 
      for (n=0; n < path_filler->num_regions; n++, num_regions++)
        { 
          assert(num_regions < 255);
          jpx_roi *roi = regions + num_regions;
          kdu_coords *vertices = path_filler->region_vertices + 4*n;
          roi->init_quadrilateral(vertices[0],vertices[1],
                                  vertices[2],vertices[3]);
          update_extremities(roi);  
          result.augment(roi->region);
        }
      delete path_filler;
    }
  else
    { // Generate a path formed by degenerate quadrilateral line segments
      jpx_roi *roi;
      for (n=1; n < converter->num_boundary_vertices; n++)
        {
          assert(num_regions < 255);
          roi = regions + (num_regions++);
          roi->init_quadrilateral(converter->boundary_vertices[n-1],
                                  converter->boundary_vertices[n],
                                  converter->boundary_vertices[n],
                                  converter->boundary_vertices[n-1]);
          update_extremities(roi);
          result.augment(roi->region);
        }
      if (conversion_flags & JPX_EDITOR_FLAG_ELLIPSES)
        { 
          for (n=0; n < converter->num_boundary_vertices; n++)
            { 
              assert(num_regions < 255);
              roi = regions + (num_regions++);
              roi->init_ellipse(converter->boundary_vertices[n],
                                kdu_coords(1,1));
              update_extremities(roi);
              result.augment(roi->region);
            }
        }
    }

  delete converter;
  return result;
}

/*****************************************************************************/
/*                         jpx_roi_editor::get_anchor                        */
/*****************************************************************************/

int jpx_roi_editor::get_anchor(kdu_coords &pt, int which,
                               bool selected_region_only, bool dragged) const
{
  if ((which < 0) ||
      (dragged && ((anchor_idx < 0) || (dragged_point == anchor_point))))
    return 0;
  int n=0, lim_n=num_regions;
  if (selected_region_only || dragged)
    {
      if ((region_idx < 0) || (region_idx >= num_regions))
        return 0;
      n = region_idx; lim_n = n+1;
    }
  for (; n < lim_n; n++)
    { 
      kdu_coords anchors[4];
      int num_anchors = find_anchors(anchors,regions[n]);
      if (dragged)
        {
          if (which != 0)
            return 0;
          which = anchor_idx;
        }
      else if (which >= num_anchors)
        { 
          which -= num_anchors;
          continue;
        }
      int flags = JPX_EDITOR_FLAG_NZ;
      if ((n == region_idx) && (which == anchor_idx))
        flags |= JPX_EDITOR_FLAG_SELECTED;
      if (regions[n].is_encoded)
        flags |= JPX_EDITOR_FLAG_ENCODED;
      pt = anchors[which];
      if (dragged)
        pt += (dragged_point - anchor_point);
      return flags;
    }
  return 0;
}

/*****************************************************************************/
/*                          jpx_roi_editor::get_edge                         */
/*****************************************************************************/

int jpx_roi_editor::get_edge(kdu_coords &from, kdu_coords &to, int which,
                             bool selected_region_only, bool dragged,
                             bool want_shared_flag)
{
  if ((which < 0) ||
      (dragged && ((anchor_idx < 0) || (dragged_point == anchor_point))))
    return 0;
  if (want_shared_flag && !shared_edge_flags_valid)
    find_shared_edge_flags();
  kdu_coords disp = dragged_point - anchor_point;
  int n=0, lim_n=num_regions;
  if (selected_region_only)
    { 
      if ((region_idx < 0) || (region_idx >= num_regions))
        return 0;
      n = region_idx; lim_n = n+1;
    }
  for (; n < lim_n; n++)
    { 
      if (dragged && (drag_flags[n] == 0))
        continue;
      int flags = JPX_EDITOR_FLAG_NZ;
      if (regions[n].is_encoded)
        flags |= JPX_EDITOR_FLAG_ENCODED;
      if (regions[n].is_elliptical)
        { 
          if (which >= 2)
            { which-=2; continue; }
          jpx_roi roi_copy = regions[n];
          if (dragged)
            move_vertices(&roi_copy,drag_flags[n],disp);
          get_edge_vertices(&roi_copy,(which+1),from,to);
          if ((n == region_idx) && (edge_idx == (which+1)))
            flags |= JPX_EDITOR_FLAG_SELECTED;
        }
      else
        {
          if (dragged)
            {
              int p;
              for (p=0; p < 4; p++)
                {
                  kdu_byte mask1 = (kdu_byte)(1<<p);
                  kdu_byte mask2 = (p==3)?((kdu_byte) 1):(mask1<<1);
                  if (!(drag_flags[n] & (mask1 | mask2)))
                    continue;
                  if (which > 0)
                    { which--; continue; }
                  from = regions[n].vertices[p];
                  to = regions[n].vertices[(p+1)&3];
                  if ((n == region_idx) && (p == edge_idx))
                    flags |= JPX_EDITOR_FLAG_SELECTED;
                  if (want_shared_flag && (shared_edge_flags[n] & mask1))
                    flags |= JPX_EDITOR_FLAG_SHARED;
                  if (drag_flags[n] & mask1)
                    from += disp;
                  if (drag_flags[n] & mask2)
                    to += disp;
                  break;
                }
              if (p == 4)
                continue;
            }
          else
            { 
              if (which >= 4)
                { which -= 4; continue; }
              if ((n == region_idx) && (which == edge_idx))
                flags |= JPX_EDITOR_FLAG_SELECTED;
              if (want_shared_flag &&
                  (shared_edge_flags[n] & ((kdu_byte)(1<<which))))
                flags |= JPX_EDITOR_FLAG_SHARED;
              from = regions[n].vertices[which];
              to = regions[n].vertices[(which+1)&3];
              if (which == edge_idx)
                flags |= JPX_EDITOR_FLAG_SELECTED;
            }
        }
      return flags;
    }
  return 0;
}

/*****************************************************************************/
/*                        jpx_roi_editor::get_curve                          */
/*****************************************************************************/

int jpx_roi_editor::get_curve(kdu_coords &centre, kdu_coords &extent,
                              kdu_coords &skew, int which,
                              bool selected_region_only, bool dragged) const
{
  if ((which < 0) ||
      (dragged && ((anchor_idx < 0) || (dragged_point == anchor_point))))
    return 0;
  kdu_coords disp = dragged_point - anchor_point;
  int n=0, lim_n=num_regions;
  if (selected_region_only)
    { 
      if ((region_idx < 0) || (region_idx >= num_regions))
        return 0;
      n = region_idx; lim_n = n+1;
    }
  for (; n < lim_n; n++)
    { 
      if (!regions[n].is_elliptical)
        continue;
      if (dragged && (drag_flags[n] == 0))
        continue;
      if (which > 0)
        { which--; continue; }
      if (!dragged)
        regions[n].get_ellipse(centre,extent,skew);
      else
        {
          jpx_roi roi_copy = regions[n];
          move_vertices(&roi_copy,drag_flags[n],disp);
          roi_copy.get_ellipse(centre,extent,skew);
        }
      int flags = JPX_EDITOR_FLAG_NZ;
      if (regions[n].is_encoded)
        flags |= JPX_EDITOR_FLAG_ENCODED;
      if ((n == region_idx) && (edge_idx ==  0))
        flags |= JPX_EDITOR_FLAG_SELECTED;
      return flags;
    }
  return 0;
}

/*****************************************************************************/
/*                 jpx_roi_editor::get_path_segment_for_region               */
/*****************************************************************************/

bool jpx_roi_editor::get_path_segment_for_region(int idx, kdu_coords &from,
                                                 kdu_coords &to)
{
  if ((idx < 0) || (idx >= num_regions))
    return false;
  if (!path_edge_flags_valid)
    find_path_edge_flags();
  kdu_byte edges = path_edge_flags[idx];
  kdu_byte type = (edges>>6);  edges &= 0x0F;
  if (type == 0)
    return false;
  jpx_roi *roi = regions + idx;
  if (type == 3)
    {
      assert(roi->is_elliptical);
      from.x = to.x = roi->region.pos.x + (roi->region.size.x>>1);
      from.y = to.y = roi->region.pos.y + (roi->region.size.y>>1);
    }
  else if (type == 1)
    {
      from = jx_midpoint(roi->vertices[0],roi->vertices[1]);
      to = jx_midpoint(roi->vertices[2],roi->vertices[3]);
    }
  else if (type == 2)
    {
      from = jx_midpoint(roi->vertices[1],roi->vertices[2]);
      to = jx_midpoint(roi->vertices[3],roi->vertices[0]);
    }
  return true;
}

/*****************************************************************************/
/*                      jpx_roi_editor::get_path_segment                     */
/*****************************************************************************/

int jpx_roi_editor::get_path_segment(kdu_coords &from, kdu_coords &to,
                                     int which)
{
  if (which <  0)
    return 0;
  if (!path_edge_flags_valid)
    find_path_edge_flags();
  
  int n;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        continue;
      kdu_byte edges = path_edge_flags[n];
      kdu_byte type = (edges>>6);  edges &= 0x0F;
      if (type == 1)
        { // This region is a path brick running from edge 0 to edge 2
          if (which > 0)
            { which--; continue; }
          from = jx_midpoint(roi->vertices[0],roi->vertices[1]);
          to = jx_midpoint(roi->vertices[2],roi->vertices[3]);
          return JPX_EDITOR_FLAG_NZ;
        }
      else if (type == 2)
        { // This region is a path brick running from edge 1 to edge 3
          if (which > 0)
            { which--; continue; }
          from = jx_midpoint(roi->vertices[1],roi->vertices[2]);
          to = jx_midpoint(roi->vertices[3],roi->vertices[0]);
          return JPX_EDITOR_FLAG_NZ;
        }
    }
  return 0;
}

/*****************************************************************************/
/*                         jpx_roi_editor::enum_paths                        */
/*****************************************************************************/

int jpx_roi_editor::enum_paths(kdu_uint32 path_flags[],
                               kdu_byte path_members[],
                               kdu_coords &path_start,
                               kdu_coords &path_end)
{
  if (!path_edge_flags_valid)
    find_path_edge_flags();
  int n, m, num_members=0;
  while (num_members < 255)
    { // Loop until we cannot add any more segments on
      
      // Start by looking for a matching path brick; but we need to be
      // careful to stop if we encounter a junction.
      kdu_uint32 mask, found_mask=1, *pflags, *found_pflags=NULL;
      kdu_coords ep1, ep2, found_ep1, found_ep2;
      int found_brick_idx = -1;
      bool add_to_tail = false;
      for (mask=1, pflags=path_flags, n=0;
           n < num_regions; n++, mask<<=1)
        { 
          if (mask == 0) { mask=1; pflags++; }
          if ((*pflags & mask) || regions[n].is_elliptical ||
              !get_path_segment_for_region(n,ep1,ep2))
            continue;
          if ((num_members == 0) || (ep1 == path_end) || (ep2 == path_end) ||
              (ep1 == path_start) || (ep2 == path_start))
            { 
              if (found_brick_idx < 0)
                { 
                  found_brick_idx = n;
                  found_pflags=pflags;  found_mask=mask;
                  found_ep1=ep1;  found_ep2=ep2;
                  if (num_members == 0)
                    break; // No need to check for junctions yet
                  add_to_tail = (ep1 == path_end) || (ep2 == path_end);
                }
              else if ((add_to_tail &&
                        ((ep1==path_end) || (ep2==path_end))) ||
                       ((!add_to_tail) &&
                        ((ep1==path_start) || (ep2==path_start))))
                { // Junction encountered
                  found_pflags = NULL;
                  break;
                }
            }
        }
      
      if ((found_brick_idx < 0) || (found_pflags == NULL))
        break; // We're all done
          
      *found_pflags |= found_mask;
      if (add_to_tail)
        { 
          for (m=num_members++; m > 0; m--)
            path_members[m] = path_members[m-1];
          path_members[0] = (kdu_byte) found_brick_idx;
          path_end = (found_ep1==path_end)?found_ep2:found_ep1;
        }
      else
        { 
          if (num_members == 0)
            { path_start = found_ep1; path_end = found_ep2; }
          else
            path_start = (found_ep1==path_start)?found_ep2:found_ep1;
          path_members[num_members++] = (kdu_byte) found_brick_idx;
        }
          
      // Finish off this iteration by looking for ellipses
      for (mask=1, pflags=path_flags, n=0;
           (n < num_regions) && (num_members < 255);
           n++, mask<<=1)
        { 
          if (mask == 0) { mask=1; pflags++; }
          if ((*pflags & mask) || ((path_edge_flags[n] >> 6) != 3) ||
              !regions[n].is_elliptical)
            continue;
          kdu_coords centre = regions[n].region.pos;
          centre.x += regions[n].region.size.x >> 1;
          centre.y += regions[n].region.size.y >> 1;
          if (centre == path_start)
            { 
              for (m=num_members++; m > 0; m--)
                path_members[m] = path_members[m-1];
              path_members[0] = (kdu_byte) n;
              *pflags |= mask;
            }
          else if (centre == path_end)
            { 
              path_members[num_members++] = (kdu_byte) n;
              *pflags |= mask;
            }
        }
    }
  return num_members;
}

/*****************************************************************************/
/*                    jpx_roi_editor::fill_closed_paths                      */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::fill_closed_paths(bool &success, int required_member_idx)
{
  // Start by collecting the paths
  jx_path_filler *scan, *head=NULL;
  
  kdu_byte path_members[256];
  kdu_uint32 path_flags[8]={0,0,0,0,0,0,0,0};
  kdu_coords path_start, path_end;
  int num_members, n;
  while ((num_members=enum_paths(path_flags,path_members,
                                 path_start,path_end)) > 0)
    { 
      if (path_start != path_end)
        continue;
      if (required_member_idx >= 0)
        { 
          for (n=0; n < num_members; n++)
            if (required_member_idx == (int) path_members[n])
              break;
          if (n == num_members)
            continue;
        }
      scan = new jx_path_filler;
      if (!scan->init(this,num_members,path_members,path_start))
        {
          delete scan;
          continue;
        }
      scan->next = head;
      head = scan;
    }

  // At this point, we see if any of the paths discovered so far completely
  // enclose other paths.  If so, the inner path is considered to provide
  // a hole in the interior of the outer path.  To make the concept more
  // definitive, each `jx_path_filler' object has a `container' pointer,
  // which is set to point to another `jx_path_filler' object which contains
  // it.  Paths which are contained may not contain other paths, since they
  // are considered holes in their containing path, so the containment
  // relationships between all paths must be established to set up these
  // pointers correctly.  At that point, we can move the boundary elements
  // from a contained `jx_path_filler' object into its container, reversing
  // the sense of the interior and exterior edges associated with each
  // path brick.  The contained path filler retains its own recollection of
  // the original path members which is was built from, so they can be
  // removed from the collection.
  for (scan=head; scan != NULL; scan=scan->next)
    { // First find the most immediate container for each path, if any.
      jx_path_filler *ct;
      for (ct=head; ct != NULL; ct=ct->next)
        if ((ct != scan) && ct->contains(scan))
          {
            if (scan->container == NULL)
              scan->container = ct;
            else if (scan->container->contains(ct))
              scan->container = ct;
          }
    }
  bool done=false;
  while (!done)
    { // Now progressively remove the "contained" status of paths which are
      // doubly contained.  This needs to be progressive so that if A contains
      // B which contains C which contains D which contains E which contains
      // F, we will be left with A, C and E as outer paths and B, D and F as
      // contained paths.
      for (done=true, scan=head; scan != NULL; scan=scan->next)
        if ((scan->container != NULL) &&
            (scan->container->container != NULL) &&
            (scan->container->container->container == NULL))
          { done = false; scan->container = NULL; }
    }
  for (scan=head; scan != NULL; scan=scan->next)
    {
      if (scan->container != NULL)
        continue;
      for (jx_path_filler *hole=head; hole != NULL; hole=hole->next)
        if (hole->container == scan)
          {
            scan->import_internal_boundary(hole);
            for (jx_path_filler *exc=hole->next; exc != NULL; exc=exc->next)
              if ((exc->container == scan) && exc->intersects(hole))
                exc->container = NULL; // Remove intersecting holes
          }
    }
  
  success = true;
  if (head == NULL)
    return kdu_dims(); // No closed paths
  
  // Cancel the selection and remove the regions we have moved to the
  // path filling objects.  To do this, we make use of the fact that each
  // `jx_path_filler' object keeps a collection of flag bits corresponding
  // to the original locations of all quadrilateral regions with which it
  // was initialized.
  kdu_dims result = cancel_selection();
  path_edge_flags_valid = shared_edge_flags_valid = false;
  bool state_pushed = false;
  for (n=0; n < 8; n++)
    path_flags[n] = 0;
  for (scan=head; scan != NULL; scan=scan->next)
    scan->get_original_path_members(path_flags);
  kdu_uint32 mask=1, *pflags=path_flags;
  for (n=0; n < num_regions; n++, mask<<=1)
    {
      if (mask == 0) { mask=1; pflags++; }
      if (!(*pflags & mask))
        continue; // This region is not being deleted
      if (!state_pushed)
        { 
          push_current_state();
          state_pushed = true;
        }
      for (int m=n+1; m < num_regions; m++)
        regions[m-1] = regions[m];
      num_regions--;
      n--; // So it stays the same on the next loop iteration
    }

  // Now we are ready to actually fill the paths one by one, adding the
  // resulting regions back in
  for (scan=head; scan != NULL; scan=scan->next)
    { 
      if (scan->container != NULL)
        continue; // This path filler provides a hole for its container.
      if (!scan->process())
        success = false;
      if ((num_regions+scan->num_regions) > 255)
        { success = false; break; }
      for (n=0; n < scan->num_regions; n++, num_regions++)
        { 
          jpx_roi *roi = regions+num_regions;
          kdu_coords *vertices = scan->region_vertices + 4*n;
          roi->init_quadrilateral(vertices[0],vertices[1],
                                  vertices[2],vertices[3]);
          roi->clip_region();
          update_extremities(roi);
        }
    }
  
  // Clean up all the path filling resources
  while ((scan=head) != NULL)
    { head=scan->next; delete scan; }
  
  kdu_dims bb; get_bounding_box(bb,false);
  result.augment(bb);
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::set_path_thickness                     */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::set_path_thickness(int thickness, bool &success)
{
  if (thickness < 1)
    thickness = 1;
  if (!path_edge_flags_valid)
    find_path_edge_flags();
  
  kdu_dims bb;
  get_bounding_box(bb,false);

  bool any_change = false;
  push_current_state();
  
  // Start by modifying any open ends of path bricks, along with ellipses
  // whose centres coincide with path segment end points.
  int p, n;
  success = true;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      kdu_byte edges = path_edge_flags[n];
      kdu_byte type = edges >> 6;  edges&=0x0F;
      if (type == 0)
        continue;
      if ((type == 3) && roi->is_elliptical)
        { // Force ellipse to a circle of the required diameter
          kdu_coords extent, centre = roi->region.pos;
          centre.x += (roi->region.size.x>>1);
          centre.y += (roi->region.size.y>>1);
          extent.x = extent.y = (thickness-1)>>1;
          roi->init_ellipse(centre,extent,kdu_coords(),
                            roi->is_encoded,roi->coding_priority);
          any_change = true;
          continue;
        }
      assert((type == 1) || (type == 2));
      jpx_roi copy = *roi;
      bool modified_something = false;
      for (p=0; p < 4; p++)
        { 
          if (edges & (kdu_byte)(1<<p))
            continue; // Looking only for open ends
          if (((type == 1) && (p & 1)) || ((type == 2) && !(p & 1)))
            continue; // Not one of the edges defining the path segment
          kdu_coords p0, p1, ep0, ep1;
          p0 = jx_midpoint(roi->vertices[p],roi->vertices[(p+1)&3]);
          p1 = jx_midpoint(roi->vertices[(p+2)&3],roi->vertices[(p+3)&3]);
          double delta = 0.5*(thickness-1);
          do {
            if (!(jx_find_path_edge_intersection(&p1,&p0,NULL,-delta,&ep0) &&
                  jx_find_path_edge_intersection(&p1,&p0,NULL,delta,&ep1)))
              delta = -1.0; // Just a way of indicating a problem
            delta *= 1.1; // In case we need to iterate
          } while ((delta > 0.0) && (ep0 == ep1));
          if (delta < 0.0)
            { success = false; continue; }
          modified_something = true;
          kdu_coords adj = p0 - jx_midpoint(ep0,ep1);
          ep0 += adj;  ep1 += adj;
          copy.vertices[p] = ep0;  copy.vertices[(p+1)&3] = ep1;
          copy.flags |= JPX_QUADRILATERAL_ROI; // Ensure vertices count
        }
      if (modified_something)
        { 
          if (copy.check_geometry())
            { *roi = copy; any_change = true; }
          else
            success = false;
        }
    }
  
  // Finally, adjust connected ends of path bricks
  int m, q=0;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      kdu_byte edges = path_edge_flags[n];
      kdu_byte type = edges >> 6;  edges&=0x0F;
      if ((type == 0) || (type == 3) || (edges == 0))
        continue;
      for (p=0; p < 4; p++)
        { 
          if (!(edges & (kdu_byte)(1<<p)))
            continue;
          kdu_coords v1=roi->vertices[p];
          kdu_coords v2=roi->vertices[(p+1)&3];
          for (m=0; m < num_regions; m++)
            {
              if ((m == n) || (path_edge_flags[m] == 0))
                continue;
              for (q=0; q < 4; q++)
                if ((path_edge_flags[m] & (kdu_byte)(1<<q)) &&
                    (((regions[m].vertices[q]==v2) &&
                      (regions[m].vertices[(q+1)&3]==v1)) ||
                     ((regions[m].vertices[q]==v1) &&
                      (regions[m].vertices[(q+1)&3]==v2))))
                  break;
              if (q < 4)
                break;
            }
          if (m == num_regions)
            continue; // Should not happen
          jpx_roi *nbr = regions+m;
          path_edge_flags[m] &= ~(1<<q); // Cancel edge flag in our neighbour
          path_edge_flags[n] &= ~(1<<p); // Cancel the edge flag here
          kdu_coords p0, p1, p2, ep0, ep1;
          p0 = jx_midpoint(roi->vertices[p],roi->vertices[(p+1)&3]);
          p1 = jx_midpoint(roi->vertices[(p+2)&3],roi->vertices[(p+3)&3]);
          p2 = jx_midpoint(nbr->vertices[(q+2)&3],nbr->vertices[(q+3)&3]);
          double delta = 0.5*(thickness-1);
          do {
            if (!(jx_find_path_edge_intersection(&p1,&p0,&p2,-delta,&ep0) &&
                  jx_find_path_edge_intersection(&p1,&p0,&p2,delta,&ep1)))
              delta = -1.0; // Just a way of indicating a problem
            delta *= 1.1; // In case we need to iterate
          } while ((delta > 0.0) && (ep0 == ep1));
          if (delta < 0.0)
            { success = false; continue; }
          jpx_roi roi_copy = *roi;
          jpx_roi nbr_copy = *nbr;
          kdu_coords adj =
            (jx_midpoint(roi->vertices[p],roi->vertices[(p+1)&3]) -
             jx_midpoint(ep0,ep1));
          ep0 += adj;  ep1 += adj;
          roi_copy.vertices[p] = ep0;
          roi_copy.vertices[(p+1)&3] = ep1;
          roi_copy.flags |= JPX_QUADRILATERAL_ROI; // Ensure vertices count
          nbr_copy.vertices[q] = ep1;
          nbr_copy.vertices[(q+1)&3] = ep0;
          nbr_copy.flags |= JPX_QUADRILATERAL_ROI; // Ensure vertices count
          if (roi_copy.check_geometry() && nbr_copy.check_geometry())
            { *roi = roi_copy; *nbr = nbr_copy; any_change = true; }
          else
            success = false;
        }
    }
  
  kdu_dims result;
  path_edge_flags_valid = shared_edge_flags_valid = false;
  if (any_change)
    {
      for (n=0; n < num_regions; n++)
        { // We need to do this, since we have been moving vertices directly;
          // we can't fix inconsistencies as we go, since this may rotate the
          // vertices and hence interfere with the interpretation of the
          // `path_edge_flags'.
          regions[n].fix_inconsistencies();
          regions[n].clip_region();
          update_extremities(regions+n);
        }
      result.augment(bb);
      get_bounding_box(bb,false);
      result.augment(bb);
    }
  else
    undo(); // Undo the effect of `push_current_state'
  return result;
}

/*****************************************************************************/
/*                   jpx_roi_editor::find_path_edge_flags                    */
/*****************************************************************************/

void jpx_roi_editor::find_path_edge_flags()
{
  if (path_edge_flags_valid)
    return;
  
  memset(path_edge_flags,0,(size_t) num_regions);
  int n, m, p, q;
  kdu_byte mask;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        continue; // Elliptical regions do not contribute path segments
      kdu_byte edges=0; // For edges shared with other quadrilaterals
      kdu_byte multiple_edges=0; // For edges shared with multiple quads
      kdu_byte singular_edges=0; // For edges which are a single point
      kdu_byte elliptical_centres=0; // For edges shared with ellipses
      int ellipse_ids[4]={0,0,0,0}; // Id's of ellipses which are shared
      int num_real_edges=4;
      for (p=0; p < 4; p++)
        if (roi->vertices[p] == roi->vertices[(p+1)&3])
          num_real_edges--;
      if (num_real_edges == 0)
        continue; // Singularities cannot be path bricks
      assert(num_real_edges >= 2);
      if (num_real_edges == 3)
        continue; // Triangles cannot be path bricks
      for (m=0; m < num_regions; m++)
        { 
          if (m == n)
            continue;
          jpx_roi *nbr = regions + m;
          if (!nbr->region.intersects(roi->region))
            continue;
          for (mask=1, p=0; p < 4; p++, mask<<=1)
            { 
              kdu_coords v1=roi->vertices[p], v2=roi->vertices[(p+1)&3];
              if ((num_real_edges == 2) && (v1 != v2))
                continue; // Otherwise path segment would have length 0
              if (nbr->is_elliptical)
                { 
                  double cx=0.5*v1.x+0.5*v2.x, cy=0.5*v1.y+0.5*v2.y;
                  cx -= (nbr->region.pos.x+(nbr->region.size.x>>1));
                  cy -= (nbr->region.pos.y+(nbr->region.size.y>>1));
                  if ((-0.51 < cx) && (cx < 0.51) &&
                      (-0.51 < cy) && (cy < 0.51))
                    { 
                      elliptical_centres |= mask;
                      ellipse_ids[p] = m;
                    }
                  continue;
                }
              if (v1 == v2)
                singular_edges |= mask;
              for (q=0; q < 4; q++)
                { 
                  if ((v1 == nbr->vertices[q]) &&
                      ((v2 == nbr->vertices[(q-1)&3]) ||
                       ((v1 != v2) && (v2 == nbr->vertices[(q+1)&3]))))
                    { // Found a shared edge
                      if (edges & mask)
                        multiple_edges |= mask;
                      edges |= mask;
                    }
                }
            }
        }
      
      if (multiple_edges & ~(singular_edges|elliptical_centres))
        continue; // Don't allow multiply shared edges unless they are single
                  // points or elliptical centres
      
      edges |= elliptical_centres; // Now holds the "compatible edges"
      kdu_byte non_ellipse_compatible_edges =
        edges & (~elliptical_centres) & ~(singular_edges & multiple_edges);
        // These are the edges we are actually going to flag.  By not flagging
        // elliptical centres or singular edges which are multiply connected,
        // we ensure that these edges will be squared up and regenerated in
        // isolation by `set_path_thickness'.
      
      kdu_coords v1, v2; // Collect end-points of the path segment
      if (edges == 0)
        { // This region is a potential path brick defined by opposing edges
          kdu_long dx, dy, len_sq[2];
          for (p=0; p < 2; p++)
            {
              v1 = jx_midpoint(roi->vertices[p],roi->vertices[p+1]);
              v2 = jx_midpoint(roi->vertices[p+2],roi->vertices[(p+3)&3]);
              dx = v1.x-v2.x;  dy = v1.y-v2.y;
              len_sq[p] = dx*dx + dy*dy;
            }
          if (len_sq[0] >= len_sq[1])
            {
              path_edge_flags[n] = 0x40;
              edges = 1; // For the membership tests below
            }
          else
            {
              path_edge_flags[n] = 0x80;
              edges = 2; // For the membership tests below
            }
        }
      else if ((edges & 5) && !(edges & 10))
        { // This region is a potential path brick defined by edge 0 and edge 2
          path_edge_flags[n] = non_ellipse_compatible_edges | 0x40;
        }
      else if ((edges & 10) && !(edges & 5))
        { // This region is a potential path brick defined by edge 1 and edge 3
          path_edge_flags[n] = non_ellipse_compatible_edges | 0x80;
        }
      else
        continue; // Not a path brick
      
      // Now check whether the path segment is contained within the region.
      if (edges & 5)
        { p = 0; elliptical_centres &= 5; }
      else
        { p = 1; elliptical_centres &= 10; }
      v1 = jx_midpoint(roi->vertices[p],roi->vertices[p+1]);
      v2 = jx_midpoint(roi->vertices[p+2],roi->vertices[(p+3)&3]);
      if (roi->check_edge_intersection((p-1)&3,v1,v2) ||
          roi->check_edge_intersection((p+1)&3,v1,v2))
        { // Not a path brick
          path_edge_flags[n] = 0;
          continue;
        }
      
      // Finally, see about marking ellipses which coincide with us
      for (mask=1, p=0; p < 4; p++, mask<<=1)
        if (elliptical_centres & mask)
          { 
            m = ellipse_ids[p];
            assert((m >= 0) && (m < num_regions) && regions[m].is_elliptical);
            path_edge_flags[m] = 0xC0;
          }
    }

  path_edge_flags_valid = true;
}

/*****************************************************************************/
/*                  jpx_roi_editor::find_shared_edge_flags                   */
/*****************************************************************************/

void jpx_roi_editor::find_shared_edge_flags()
{
  if (shared_edge_flags_valid)
    return;
  memset(shared_edge_flags,0,(size_t) num_regions);
  int n, m, p, q;
  kdu_byte mask;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        continue; // Elliptical regions do not share edges
      kdu_byte edges=0; // For edges shared with other quadrilaterals
      for (m=0; m < num_regions; m++)
        { 
          if (m == n)
            continue;
          jpx_roi *nbr = regions + m;
          if (nbr->is_elliptical || !nbr->region.intersects(roi->region))
            continue;
          for (mask=1, p=0; p < 4; p++, mask<<=1)
            if (!(edges & mask))
              { 
                kdu_coords v1=roi->vertices[p], v2=roi->vertices[(p+1)&3];
                for (q=0; q < 4; q++)
                  { 
                    if ((v1 == nbr->vertices[q]) &&
                        ((v2 == nbr->vertices[(q-1)&3]) ||
                         (v2 == nbr->vertices[(q+1)&3])))
                      edges |= mask;
                  }
              }
        }
      shared_edge_flags[n] = edges;
    }
  shared_edge_flags_valid = true;
}

/*****************************************************************************/
/*                      jpx_roi_editor::find_anchors                         */
/*****************************************************************************/

int
  jpx_roi_editor::find_anchors(kdu_coords anchors[], const jpx_roi &roi) const
{
  if (mode == JPX_EDITOR_VERTEX_MODE)
    { // Anchors are vertices or elliptical extremities
      for (int p=0; p < 4; p++)
        anchors[p] = roi.vertices[p];
      return 4;
    }
  else
    { // Anchors are edge mid-points or elliptical centre
      if (roi.is_elliptical)
        {
          anchors[0].x = roi.region.pos.x + (roi.region.size.x>>1);
          anchors[0].y = roi.region.pos.y + (roi.region.size.y>>1);
          return 1;
        }
      else
        {
          for (int p=0; p < 4; p++)
            anchors[p] = jx_midpoint(roi.vertices[p],roi.vertices[(p+1)&3]);
          return 4;
        }
    }
}

/*****************************************************************************/
/*                 jpx_roi_editor::set_drag_flags_for_vertex                 */
/*****************************************************************************/

void jpx_roi_editor::set_drag_flags_for_vertex(kdu_coords v)
{
  int n, p;
  for (n=0; n < num_regions; n++)
    {
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        { // See if `v' lies essentially on the boundary of the ellipse
          if (drag_flags[n] & 0x0F)
            continue; // Must be dragging/moving an ellipse anchor directly
          double x0=v.x, y0=v.y, xp, yp;
          if (roi->find_boundary_projection(x0,y0,xp,yp,0.98) < 0)
            continue;
          drag_flags[n] = 0x0F;
          set_drag_flags_for_boundary(roi);
        }
      else
        {
          kdu_byte mask;
          for (mask=1, p=0; p < 4; p++, mask<<=1)
            if (((drag_flags[n] & mask) == 0) && (roi->vertices[p] == v))
              drag_flags[n] |= mask;
        }
    }
}

/*****************************************************************************/
/*                jpx_roi_editor::set_drag_flags_for_midpoint                */
/*****************************************************************************/

void jpx_roi_editor::set_drag_flags_for_midpoint(kdu_coords point)
{
  int n, p;
  for (n=0; n < num_regions; n++)
    {
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        continue;
      kdu_coords anchors[4];
      int num_anchors = find_anchors(anchors,*roi);
      if ((num_anchors != 4) || (mode == JPX_EDITOR_VERTEX_MODE))
        return;
      for (p=0; p < 4; p++)
        if (anchors[p] == point)
          {
            kdu_byte mask = (kdu_byte)(1<<p);
            if (!(drag_flags[n] & mask))
              {
                drag_flags[n] |= mask;
                set_drag_flags_for_vertex(roi->vertices[p]);
              }
            mask = (kdu_byte)(1<<((p+1)&3));
            if (!(drag_flags[n] & mask))
              {
                drag_flags[n] |= mask;
                set_drag_flags_for_vertex(roi->vertices[p]);
              }
          }            
    }
}

/*****************************************************************************/
/*               jpx_roi_editor::set_drag_flags_for_boundary                 */
/*****************************************************************************/

void jpx_roi_editor::set_drag_flags_for_boundary(const jpx_roi *elliptical_roi)
{
  int n, p;
  for (n=0; n < num_regions; n++)
    {
      jpx_roi *roi = regions + n;
      if (roi->is_elliptical)
        continue;
      kdu_byte mask;
      for (mask=1, p=0; p < 4; p++, mask<<=1)
        if ((drag_flags[n] & mask) == 0)
          {
            double x0=roi->vertices[p].x, y0=roi->vertices[p].y, xp, yp;
            if (elliptical_roi->find_boundary_projection(x0,y0,xp,yp,
                                                         0.98) >= 0)
              drag_flags[n] |= mask;
          }
    }
}


/*****************************************************************************/
/*                     jpx_roi_editor::remove_duplicates                     */
/*****************************************************************************/

kdu_dims jpx_roi_editor::remove_duplicates()
{
  kdu_dims result;
  int n, m, p, q;
  for (n=0; n < num_regions; n++)
    { 
      jpx_roi *roi = regions + n;
      int duplicate_idx = -1;
      for (m=n+1; m < num_regions; m++)
        { 
          jpx_roi *roi2 = regions + m;
          if (roi2->is_elliptical != roi->is_elliptical)
            continue;
          if (*roi2 == *roi)
            { 
              duplicate_idx = (m==region_idx)?n:m;
              break;
            }
          if (!roi->is_elliptical)
            { // See if one region's edges are a subset of the other's
              for (p=0; p < 4; p++)
                { 
                  kdu_coords *v1=roi2->vertices+p;
                  kdu_coords *v2=roi2->vertices+((p+1)&3);
                  if (*v1 == *v2)
                    continue;
                  for (q=0; q < 4; q++)
                    { 
                      kdu_coords *w1=roi->vertices+q;
                      kdu_coords *w2=roi->vertices+((q+1)&3);
                      if (((*v1 == *w1) && (*v2 == *w2)) ||
                          ((*v1 == *w2) && (*v2 == *w1)))
                        break; // Edge p of `roi2' is matched in `roi'
                    }
                  if (q == 4)
                    break; // This edge was not matched
                }
              if (p == 4)
                { // All edges of `roi2' were matched in `roi'
                  duplicate_idx = m;
                  break;
                }
              
              for (p=0; p < 4; p++)
                { 
                  kdu_coords *v1=roi->vertices+p;
                  kdu_coords *v2=roi->vertices+((p+1)&3);
                  if (*v1 == *v2)
                    continue;
                  for (q=0; q < 4; q++)
                    { 
                      kdu_coords *w1=roi2->vertices+q;
                      kdu_coords *w2=roi2->vertices+((q+1)&3);
                      if (((*v1 == *w1) && (*v2 == *w2)) ||
                          ((*v1 == *w2) && (*v2 == *w1)))
                        break; // Edge p of `roi' is matched in `roi2'
                    }
                  if (q == 4)
                    break; // This edge was not matched
                }
              if (p == 4)
                { // All edges of `roi' were matched in `roi2'
                  duplicate_idx = n;
                  break;
                }
            }
        }
      if (duplicate_idx < 0)
        continue;
      path_edge_flags_valid = shared_edge_flags_valid = false;
      if (duplicate_idx == region_idx)
        result = cancel_selection();
      num_regions--;
      for (m=duplicate_idx; m < num_regions; m++)
        { 
          regions[m] = regions[m+1];
          drag_flags[m] = drag_flags[m+1];
        }
      if (duplicate_idx == n)
        n--; // So we correctly consider the next region. 
    }
  return result;  
}

/*****************************************************************************/
/*                   jpx_roi_editor::update_extremities                      */
/*****************************************************************************/

void
  jpx_roi_editor::update_extremities(jpx_roi *roi, kdu_coords *pt,
                                     int pt_idx)
{
  if (roi->is_elliptical)
    { 
      kdu_coords centre;
      double tan_theta, axis_extents[2];
      roi->get_ellipse(centre,axis_extents,tan_theta);
      if (pt != NULL)
        { // The region is perfectly circular.  In this case, the extremities
          // are not well-defined, so we can choose `tan_theta' such that
          // one of the extremities is aligned as closely as possible to `pt'.
          int dx = pt->x - centre.x;
          int dy = pt->y - centre.y;
          double tan_theta2 = 0.0;
          if ((dx != 0) && (dy != 0))
            {
              if (dy < 0)
                { dy = -dy; dx = -dx; }
              if ((dx >= -dy) && (dx <= dy))
                tan_theta2 = -((double) dx) / ((double) dy);
              else
                tan_theta2 = ((double) dy) / ((double) dx);
            }
          if (fabs(tan_theta-tan_theta2) > fabs(tan_theta+1.0/tan_theta2))
            tan_theta = -1.0/tan_theta2;
          else
            tan_theta = tan_theta2;
        }
      
      double x[4], y[4];
      double cos_theta = 1.0 / sqrt(1 + tan_theta*tan_theta);
      double sin_theta = tan_theta * cos_theta;
      x[0] = centre.x + sin_theta*axis_extents[0];
      x[2] = centre.x - sin_theta*axis_extents[0];
      y[0] = centre.y - cos_theta*axis_extents[0];
      y[2] = centre.y + cos_theta*axis_extents[0];
      x[1] = centre.x + cos_theta*axis_extents[1];
      x[3] = centre.x - cos_theta*axis_extents[1];
      y[1] = centre.y + sin_theta*axis_extents[1];
      y[3] = centre.y - sin_theta*axis_extents[1];
      
      int p;
      int rotate = 0;
      if ((pt != NULL) && (pt_idx >= 0) && (pt_idx <= 3))
        { 
          double min_dist_sq=0.0;
          for (p=0; p < 4; p++)
            { 
              double dx=x[p]-pt->x, dy=y[p]-pt->y;
              double dist_sq = dx*dx + dy*dy;
              if ((p == 0) || (dist_sq < min_dist_sq))
                { min_dist_sq = dist_sq; rotate = p-pt_idx; }
            }
        }
      for (p=0; p < 4; p++)
        { 
          roi->vertices[p].x = (int) floor(x[(p+rotate)&3]+0.5);
          roi->vertices[p].y = (int) floor(y[(p+rotate)&3]+0.5);
        }
    }
  else if (!(roi->flags & JPX_QUADRILATERAL_ROI))
    {
      roi->vertices[0] = roi->region.pos;
      roi->vertices[1].x = roi->region.pos.x + roi->region.size.x - 1;
      roi->vertices[1].y = roi->vertices[0].y;
      roi->vertices[2].x = roi->vertices[1].x;
      roi->vertices[2].y = roi->region.pos.y + roi->region.size.y - 1;
      roi->vertices[3].x = roi->region.pos.x;
      roi->vertices[3].y = roi->vertices[2].y;
    }
}

/*****************************************************************************/
/*                    jpx_roi_editor::get_edge_region                        */
/*****************************************************************************/

kdu_dims
  jpx_roi_editor::get_edge_region(const jpx_roi *roi, int edge) const
{
  kdu_coords v1, v2;
  if (roi->is_elliptical)
    { 
      if (edge == 0)
        return roi->region;
      else if (edge == 1)
        { v1=roi->vertices[3]; v2=roi->vertices[1]; }
      else if (edge == 2)
        { v1=roi->vertices[0]; v2=roi->vertices[2]; }
      else
        return kdu_dims();
    }
  else if ((edge < 0) || (edge >= 4))
    return kdu_dims();
  else
    { v1=roi->vertices[edge]; v2=roi->vertices[(edge+1)&3]; }
  kdu_dims result;
  if (v1.x < v2.x)
    { result.pos.x = v1.x; result.size.x = v2.x+1-v1.x; }
  else
    { result.pos.x = v2.x; result.size.x = v1.x+1-v2.x; }
  if (v1.y < v2.y)
    { result.pos.y = v1.y; result.size.y = v2.y+1-v1.y; }
  else
    { result.pos.y = v2.y; result.size.y = v1.y+1-v2.y; }
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::get_edge_vertices                      */
/*****************************************************************************/

void jpx_roi_editor::get_edge_vertices(const jpx_roi *roi, int edge,
                                       kdu_coords &from, kdu_coords &to) const
{
  kdu_coords centre;
  centre.x = roi->region.pos.x + (roi->region.size.x>>1);
  centre.y = roi->region.pos.y + (roi->region.size.y>>1);
  if (roi->is_elliptical)
    { 
      if (edge == 1)
        { from=roi->vertices[3]; to=roi->vertices[1]; }
      else if (edge == 2)
        { from=roi->vertices[0]; to=roi->vertices[2]; }
      else
        from = to = centre;
    }
  else if ((edge < 0) || (edge >= 4))
    from = to = centre;
  else
    { from=roi->vertices[edge]; to=roi->vertices[(edge+1)&3]; }
}

/*****************************************************************************/
/*                  jpx_roi_editor::find_next_anchor_edge                    */
/*****************************************************************************/

int jpx_roi_editor::find_next_anchor_edge()
{
  if ((anchor_idx < 0) || (region_idx < 0) || (region_idx >= num_regions))
    return -1; 
  int result=0;
  if (regions[region_idx].is_elliptical)
    {
      if (mode == JPX_EDITOR_VERTEX_MODE)
        { // There is only one edge, corresponding to the ellipse itself
          result = (edge_idx < 0)?0:-1;
        }
      else if (mode == JPX_EDITOR_SKELETON_MODE)
        { // Two selectable edges, corresponding to the horizontal and vertical
          // axes of the ellipse; these are assigned indices of 1 and 2,
          // respectively.
          if (edge_idx < 0)
            result = 1;
          else
            result = (edge_idx == 1)?2:-1;
          if ((result == 1) && (regions[region_idx].region.size.x <= 1))
            result = 2;
          if ((result == 2) && (regions[region_idx].region.size.y <= 1))
            result = -1;
          return result;
        }
      else
        return -1; // No selectable edges for ellipses in path mode
    }
  else
    { // Quadrilateral region is selected
      if (mode == JPX_EDITOR_VERTEX_MODE)
        { // There are up to two edges per anchor point
          if (edge_idx < 0)
            result = anchor_idx;
          else
            result = (edge_idx == anchor_idx)?((anchor_idx-1)&3):-1;
          if ((result == anchor_idx) &&
              (regions[region_idx].vertices[anchor_idx] ==
               regions[region_idx].vertices[(anchor_idx+1)&3]))
            result = (anchor_idx-1) & 3;
          if ((result == ((anchor_idx-1)&3)) &&
              (regions[region_idx].vertices[anchor_idx] ==
               regions[region_idx].vertices[(anchor_idx-1)&3]))
            result = -1;
        }
      else
        { // There is only one edge per anchor point
          result = (edge_idx < 0)?anchor_idx:-1;
          if (regions[region_idx].vertices[anchor_idx] ==
              regions[region_idx].vertices[(anchor_idx+1)&3])
            result = -1;
        }
    }
  return result;
}

/*****************************************************************************/
/*                      jpx_roi_editor::move_vertices                        */
/*****************************************************************************/

void
  jpx_roi_editor::move_vertices(jpx_roi *roi, kdu_byte dflags,
                                kdu_coords disp) const
{
  dflags &= 0x0F; // Just in case
  if (dflags == 0)
    return; // Nothing to move
  
  int last_moved_extremity = 0;
  int p;
  kdu_byte mask;
  kdu_coords v[4];
  for (p=0; p < 4; p++)
    v[p] = roi->vertices[p];
  for (mask=1, p=0; p < 4; p++, mask<<=1)
    if (dflags & mask)
      { v[p] += disp; last_moved_extremity = p; }
  if (roi->is_elliptical)
    {
      if (dflags == 0x0F)
        { // Move the entire ellipse -- easy
          roi->region.pos += disp;
          update_extremities(roi);
        }
      else
        { // Should be moving exactly one vertex
          assert((dflags==1) || (dflags==2) || (dflags==4) || (dflags==8));
          kdu_coords axis_vecs[2] =  {v[2]-v[0], v[1]-v[3]}; // vert then horz
          double x, y, axis_extents[2];
          for (p=0; p < 2; p++)
            { 
              if ((axis_vecs[p].x == 0) && (axis_vecs[p].y == 0))
                return; // Can't perform the move
              x = axis_vecs[p].x; y = axis_vecs[p].y;
              axis_extents[p] = 0.5*sqrt(x*x + y*y);
            }
          
          // Determine which axis is being dragged
          p = (last_moved_extremity & 1);
          kdu_coords centre = jx_midpoint(v[last_moved_extremity],
                                          v[(last_moved_extremity+2)&3]);
          if (axis_vecs[p].y < 0)
            { axis_vecs[p].x=-axis_vecs[p].x; axis_vecs[p].y=-axis_vecs[p].y; }
          double tan_theta = 0.0;
          int near_vertical_axis = 0;
          if (axis_vecs[p].y == 0)
            { // The axis being aligned is horizontal
              near_vertical_axis = 1-p;
              tan_theta = 0.0;
            }
          else
            { 
              tan_theta = -axis_vecs[p].x / (double) axis_vecs[p].y;
              if ((tan_theta < -1.0) || (tan_theta > 1.0))
                { tan_theta = -1.0 / tan_theta; near_vertical_axis = 1-p; }
              else
                near_vertical_axis = p;
            }
          if (near_vertical_axis != 0)
            { double tmp=axis_extents[0];
              axis_extents[0]=axis_extents[1]; axis_extents[1]=tmp; }
          roi->init_ellipse(centre,axis_extents,tan_theta,
                            roi->is_encoded,roi->coding_priority);
          update_extremities(roi,&v[last_moved_extremity],
                             last_moved_extremity);
        }
    }
  else
    { 
      roi->init_quadrilateral(v[0],v[1],v[2],v[3],roi->is_encoded,
                              roi->coding_priority);
      if (!roi->check_geometry())
        { // Try reversing vertices
          kdu_coords tmp=roi->vertices[1];
          roi->vertices[1]=roi->vertices[3];
          roi->vertices[3]=tmp;
        }
      update_extremities(roi);
    }
}

/*****************************************************************************/
/*                   jpx_roi_editor::delete_selected_region                  */
/*****************************************************************************/

kdu_dims jpx_roi_editor::delete_selected_region()
{
  kdu_dims result;
  if ((anchor_idx < 0) || (region_idx < 0) || (region_idx >= num_regions) ||
      (num_regions < 2))
    return result;
  
  push_current_state();
  
  int n = region_idx;
  result = cancel_selection();  
  result.augment(regions[n].region);
  num_regions--;
  for (; n < num_regions; n++)
    regions[n] = regions[n+1];
  path_edge_flags_valid = shared_edge_flags_valid = false;
  
  // Augment by new bounding box because path segments may have changed a lot
  kdu_dims bb;
  get_bounding_box(bb,false);
  result.augment(bb);
  
  return result;
}

/*****************************************************************************/
/*                    jpx_roi_editor::measure_complexity                     */
/*****************************************************************************/

double jpx_roi_editor::measure_complexity() const
{
  int n, element_count = 0;
  for (n=0; n < num_regions; n++)
    element_count += (regions[n].is_simple())?1:2;
  return (element_count==255)?1.0:(element_count*(1.0/255.0));
}

/*****************************************************************************/
/*                         jpx_roi_editor::add_region                        */
/*****************************************************************************/

kdu_dims jpx_roi_editor::add_region(bool ellipses, kdu_dims visible_frame)
{
  kdu_dims result = cancel_drag();
  if ((anchor_idx < 0) || (region_idx < 0) || (region_idx >= num_regions))
    return result;
  if (num_regions >= 255)
    return result; // No space for more regions
  if (measure_complexity() > 0.99)
    return result;
  
  kdu_coords new_selection_anchor_point = anchor_point;

  push_current_state();
  
  // Now add the new region
  jpx_roi *src_roi = regions+region_idx;
  jpx_roi new_roi;
  
  if (mode == JPX_EDITOR_PATH_MODE)
    { // In path mode, we always add at least a quadrilateral region.
      int n, p=0;
      bool have_ellipse=false; // True if there is an elliptical centre already
      kdu_coords centre, extent, skew, disp, ep0, ep1, ep2, ep3;
         // The created path segment will extent from `centre' to
         // `centre'+`disp'.  The edge which passes through `centre' will have
         // coordinates `ep0' and `ep1'.
      kdu_coords v[4];
      if (src_roi->get_ellipse(centre,extent,skew))
        { // Look for a quadrilateral
          have_ellipse = true;
          for (n=0; n < num_regions; n++)
            if (!regions[n].is_elliptical)
              { 
                find_anchors(v,regions[n]);
                for (p=0; p < 4; p++)
                  if (v[p] == centre)
                    break;
                if (p < 4)
                  break;
              }
          if (n < num_regions)
            { // Found one
              ep0 = regions[n].vertices[p];
              ep1 = regions[n].vertices[(p+1)&3];
              disp = anchor_point - v[(p+2)&3];
            }
          else
            { // Base the new quadrilateral upon the bounding box of the
              // ellipse
              ep0 = ep1 = centre;
              if (extent.x < extent.y)
                { ep0.x-=extent.x;  ep1.x+=extent.x;  disp.y = 2*extent.y; }
              else
                { ep0.y-=extent.y;  ep1.y+=extent.y;  disp.x = 2*extent.x; }
            }
        }
      else
        { // Look for an existing ellipse
          ep0 = src_roi->vertices[anchor_idx];
          ep1 = src_roi->vertices[(anchor_idx+1)&3];
          if (get_path_segment_for_region(region_idx,ep2,ep3) &&
              (ep2 != anchor_point) && (ep3 != anchor_point))
            { // Unusual case in which selected anchor is not a path end-point
              disp.x = ep1.y-ep0.y;  disp.y = ep0.x-ep1.x;
              disp.x = (disp.x>0)?((disp.x+7)>>2):(disp.x>>2);
              disp.y = (disp.y>0)?((disp.y+7)>>2):(disp.y>>2);
              ep2 = ep0 + disp;  ep3 = ep1 + disp;
              if (!visible_frame.is_empty())
                { 
                  visible_frame.clip_point(ep2); visible_frame.clip_point(ep3);
                  disp = jx_midpoint(ep2-ep1,ep3-ep0);
                }
              ep0 += disp;  ep1 += disp;
            }
          disp = anchor_point -
            jx_midpoint(src_roi->vertices[(anchor_idx+2)&3],
                        src_roi->vertices[(anchor_idx+3)&3]);
          for (n=0; n < num_regions; n++)
            if (regions[n].get_ellipse(centre,extent,skew) &&
                (centre == anchor_point))
              { have_ellipse = true; break; }
        }
      
      double dx=ep1.x-ep0.x, dy=ep1.y-ep0.y;
      double hlen = 0.5*sqrt(dx*dx+dy*dy);
      if ((num_regions < 254) && ellipses && !have_ellipse)
        { // Start by adding a circle
          centre = anchor_point;
          extent.x = extent.y = (int) floor(0.5+hlen);
          new_roi.init_ellipse(centre,extent,skew);
          new_roi.clip_region();
          if (new_roi.check_geometry())
            { 
              update_extremities(&new_roi);
              regions[num_regions++] = new_roi;
              have_ellipse = true;
              path_edge_flags_valid = shared_edge_flags_valid = false;
            }
        }
      
      // Now add the quadrilateral region
      ep2=ep1+disp; ep3=ep0+disp;
      if ((disp != kdu_coords()) && !visible_frame.is_empty())
        {
          visible_frame.clip_point(ep2); visible_frame.clip_point(ep3);
          disp = jx_midpoint(ep2-ep1,ep3-ep0);
          ep2 = ep1+disp;  ep3=ep0+disp;
        }
      new_roi.init_quadrilateral(ep0,ep1,ep2,ep3);
      if (!new_roi.check_geometry())
        { // Reverse ep0 and ep1
          ep2=ep0; ep0=ep1; ep1=ep2; ep2=ep1+disp; ep3=ep0+disp; 
        }
      if (have_ellipse)
        { 
          kdu_coords mp0=jx_midpoint(ep0,ep1), mp1=mp0+disp;
          do {
            if (!(jx_find_path_edge_intersection(&mp0,&mp1,NULL,-hlen,&ep2) &&
                  jx_find_path_edge_intersection(&mp0,&mp1,NULL,hlen,&ep3)))
              hlen = -1.0;
            hlen *= 1.1; // In case we need to iterate
          } while ((hlen > 0.0) && (ep2 == ep3));
          ep0 = ep3-disp;  ep1 = ep2-disp;
        }
      new_roi.init_quadrilateral(ep0,ep1,ep2,ep3);
      new_selection_anchor_point = jx_midpoint(ep2,ep3);
    }
  else if (ellipses)
    { 
      kdu_coords centre, extent, skew;
      if (src_roi->is_elliptical)
        { // Reproduce the existing ellipse at a different location
          src_roi->get_ellipse(centre,extent,skew);
          centre = src_roi->vertices[anchor_idx];
        }
      else
        { 
          kdu_coords from, to;
          get_edge_vertices(src_roi,edge_idx,from,to);
          centre.x = (from.x+to.x)>>1;
          centre.y = (from.y+to.y)>>1;
          double dx=from.x-to.x, dy=from.y-to.y;
          int len = (int)(0.5 + 0.5*sqrt(dx*dx+dy*dy));
          if (len < 5) len = 5;
          extent.x = extent.y = len;
        }
      new_roi.init_ellipse(centre,extent,skew);
    }
  else
    {
      if (regions[region_idx].is_elliptical && (edge_idx == 0))
        { // Make the new region equal to the ellipse's bounding box
          new_roi.init_rectangle(src_roi->region);
        }
      else if ((!src_roi->is_elliptical) && (mode != JPX_EDITOR_VERTEX_MODE))
        { // Add a quadrangle by mirroring the line joining the selected
          // anchor point to its opposite anchor point.
          kdu_coords v[4];
          find_anchors(v,*src_roi);
          kdu_coords from=src_roi->vertices[anchor_idx];
          kdu_coords to=src_roi->vertices[(anchor_idx+1)&3];
          kdu_coords vec = v[anchor_idx] - v[(anchor_idx+2) & 3];
          v[0] = to;
          v[1] = from;
          v[2] = from + vec;
          v[3] = to + vec;
          if (!visible_frame.is_empty())
            { 
              visible_frame.clip_point(v[2]);  visible_frame.clip_point(v[3]);
              vec = jx_midpoint(v[2]-from,v[3]-to);
              v[2] = from + vec; v[3] = to + vec;
            }
          new_roi.init_quadrilateral(v[0],v[1],v[2],v[3]);
        }
      else if ((!src_roi->is_elliptical) &&
               (((src_roi->vertices[anchor_idx] ==
                  src_roi->vertices[(anchor_idx+1) & 3]) &&
                 (src_roi->vertices[(anchor_idx+2)&3] ==
                  src_roi->vertices[(anchor_idx+3)&3])) ||
                ((src_roi->vertices[anchor_idx] ==
                  src_roi->vertices[(anchor_idx-1) & 3]) &&
                 (src_roi->vertices[(anchor_idx+2)&3] ==
                  src_roi->vertices[(anchor_idx+1)&3]))))
        { // Add a line by mirroring the line joining the anchor point
          // to its opposite point.
          kdu_coords v[2];
          v[0] = src_roi->vertices[anchor_idx];
          v[1] = src_roi->vertices[(anchor_idx+2)&3];
          kdu_coords vec = v[0] - v[1];  v[1] = v[0] + vec;
          if (!visible_frame.is_empty())
            visible_frame.clip_point(v[1]);
          new_roi.init_quadrilateral(v[0],v[0],v[1],v[1]);
        }
      else
        { 
          kdu_coords from, to;
          get_edge_vertices(src_roi,edge_idx,from,to);
          if ((edge_idx < 0) || (from == to))
            { // No active edge.; add a 5x5 square centred at the anchor point
              kdu_dims rect;
              rect.pos = anchor_point - kdu_coords(2,2);
              rect.size = kdu_coords(5,5);
              new_roi.init_rectangle(rect);
            }
          else
            { // Add a square which shares an edge with the active one
              kdu_coords v[4];
              v[0] = to;
              v[1] = from;
              kdu_coords vec; vec.x = -(from.y-to.y); vec.y = (from.x-to.x);
              v[2] = from + vec;
              v[3] = to + vec;
              new_roi.init_quadrilateral(v[0],v[1],v[2],v[3]);
            }
        }
    }
      
  new_roi.clip_region();
  if (!new_roi.check_geometry())
    return result;
  update_extremities(&new_roi);
  regions[num_regions++] = new_roi;
  path_edge_flags_valid = shared_edge_flags_valid = false;
  
  // Augment by new bounding box because path segments may have changed a lot
  kdu_dims bb;
  get_bounding_box(bb,false);
  result.augment(bb);
  
  if (new_selection_anchor_point != anchor_point)
    select_anchor(new_selection_anchor_point,false);
  remove_duplicates();

  return result;
}


/* ========================================================================= */
/*                          jx_scribble_converter                            */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_scribble_converter::init                        */
/*****************************************************************************/

void jx_scribble_converter::init(const kdu_coords *points, int num_points,
                                 bool want_fill)
{
  assert(num_points <= JX_ROI_SCRIBBLE_POINTS);
  if ((num_points > 1) && (points[0] == points[num_points-1]))
    num_points--;
  memcpy(scribble_points,points,sizeof(kdu_coords)*((size_t)num_points));
  this->num_scribble_points = num_points;
  this->num_boundary_vertices = 0;
  int n;
  jx_scribble_segment *seg;
  free_segments = segments = NULL;
  for (n=511; n >= 0; n--)
    { 
      seg = seg_store + n;
      seg->next = free_segments;
      free_segments = seg;
      seg->scribble_points = scribble_points;
      seg->num_scribble_points = num_scribble_points;
    }
  segments = seg = get_free_seg();
  seg->set_associated_points(0,num_scribble_points);
  if (want_fill)
    seg->prev = seg->next = seg; // Cyclically connected list of segments
}

/*****************************************************************************/
/*              jx_scribble_converter::find_boundary_vertices                */
/*****************************************************************************/

bool jx_scribble_converter::find_boundary_vertices()
{
  if (segments == NULL)
    return false;
  int n = 0;
  jx_scribble_segment *seg = segments;
  boundary_vertices[n++] = seg->seg_start;
  do {
    if ((n == 512) || !(seg->is_line || seg->is_ellipse))
      return false;
    boundary_vertices[n++] = seg->seg_end;
    seg = seg->next;
  } while ((seg != NULL) && (seg != segments));
  num_boundary_vertices = n;
  return true;
}

/*****************************************************************************/
/*                jx_scribble_converter::find_polygon_edges                  */
/*****************************************************************************/

bool jx_scribble_converter::find_polygon_edges(kdu_long max_sq_dist)
{
  assert(segments != NULL);
  jx_scribble_segment *seg = segments;
  do {
    if (!(seg->is_line || seg->is_ellipse))
      { // This segment holds an uncommitted set of scribble points.  We need
        // to build a piecewise linear approximation for it.
        if (seg->num_seg_points <= 0)
          return false; // Should not happen
        if ((seg->prev != NULL) &&
            (seg->prev->is_line || seg->prev->is_ellipse))
          seg->seg_start = seg->prev->seg_end;
        else
          seg->seg_start = *(seg->get_point(0));
        kdu_coords seg_end, v, w;
        int num_confirmed_seg_points=0;
        int n, p;
        for (n=0; n < seg->num_seg_points; n++)
          { // Try ending the segment at the n'th scribble point associated
            // with `seg'.
            if ((n == (seg->num_seg_points-1)) && (seg->next != NULL) &&
                (seg->next != seg))
              seg_end = seg->next->seg_start; // Must join to next segment
            else
              seg_end = *(seg->get_point(n));
            for (p=0; p <= n; p++) // Need to check point `n' as well
              { 
                v = w = *(seg->get_point(p));
                jx_project_to_line(seg->seg_start,seg_end,w);
                kdu_long dx=v.x-w.x, dy=v.y-w.y;
                if ((dx*dx+dy*dy) > max_sq_dist)
                  break;
              }
            if (p <= n)
              break; // Unable to extend segment end point
            
            if (n > 0)
              { // Next, check for intersections with other segments or
                // scribble points which have not yet been approximated.
                jx_scribble_segment *scan=segments;
            
                for (; scan != NULL;
                     scan=(scan->next==segments)?NULL:scan->next)
                  { 
                    const kdu_coords *from, *to;
                    if (scan->is_ellipse)
                      { 
                        continue; // Later, we will need to check this too
                      }
                    else if (scan->is_line)
                      { 
                        assert(scan != seg);
                        from = &(scan->seg_start); to = &(scan->seg_end);
                        if (jx_check_line_intersection(seg->seg_start,seg_end,
                                                       true,true,*from,*to))
                          break;
                      }
                    else
                      { // Check for intersections with scribble points
                        p = (scan==seg)?(n+1):0; // Idx of first point to check
                        if ((p == 0) && (scan->prev != NULL))
                          from = &(scan->prev->seg_end);
                        else if (p == 0)
                          { from = scan->get_point(p); p++; }
                        else
                          from = scan->get_point(p-1);
                        for (; (to = scan->get_point(p)) != NULL; p++, from=to)
                          if (jx_check_line_intersection(seg->seg_start,
                                                         seg_end,true,true,
                                                         *from,*to))
                            break;
                        if (to != NULL)
                          break;
                        if ((scan->next != NULL) &&
                            jx_check_line_intersection(seg->seg_start,seg_end,
                                                       true,true,*from,
                                                       scan->next->seg_start))
                          break;
                      }
                  }
                if (scan != NULL)
                  break; // Intersection detected
              }
            
            // If we get here, we have been able to extend the segment to
            // include the n'th point currently associated with `seg'.
            num_confirmed_seg_points = n+1;
            seg->seg_end = seg_end;
          }
 
        if (seg->next == seg)
          { // We are operating on the first and only segment in a cyclic
            // path.  In cyclic paths, disjoint association is accomplished
            // by enforcing the rule that the first segment is not associated
            // with the first scribble point.  It was to begin with, so that
            // we could get a line segment started, but now we want to remove
            // the association with the first point.  We do this my making
            // some minor adjustments so as to ensure that the following
            // statements automatically create the new segment.
            if (num_confirmed_seg_points < 2)
              return false; // Should not be possible; cyclic path with 1 point
            seg->first_seg_point++;
            num_confirmed_seg_points--;
          }
        if (num_confirmed_seg_points < seg->num_seg_points)
          { // Need to create at least one more segment
            jx_scribble_segment *new_seg = get_free_seg();
            if (new_seg == NULL)
              return false; // Exceeded available segment resources
            new_seg->set_associated_points(seg->first_seg_point +
                                           num_confirmed_seg_points,
                                           seg->num_seg_points -
                                           num_confirmed_seg_points);
            seg->num_seg_points = num_confirmed_seg_points;
            new_seg->prev = seg;
            new_seg->next = seg->next;
            seg->next = new_seg;
            if (seg->prev == seg)
              seg->prev = new_seg;
          }
        seg->is_line = true;
      }
    seg = seg->next;
  } while ((seg != NULL) && (seg != segments));
  return true;
}
  

/* ========================================================================= */
/*                              jx_path_filler                               */
/* ========================================================================= */

/*****************************************************************************/
/*                     jx_path_filler::init (first form)                     */
/*****************************************************************************/

bool
  jx_path_filler::init(jpx_roi_editor *editor, int num_path_members,
                       kdu_byte *path_members, kdu_coords path_start)
{
  if (num_path_members > JXPF_MAX_REGIONS)
    return false;
  
  // Start by filling out the `original_path_members' flags and the
  // `tmp_path_vertices' array, so we can determine where the external
  // boundaries are.  At the same time, we will move the quadrilateral
  // vertices into the `region_vertices' array.
  if (num_path_members > 256)
    return false; // Otherwise we exceed storage in `tmp_path_vertices' array
  next = NULL;
  container = NULL;
  num_tmp_path_vertices = num_regions = 0;
  memset(original_path_members,0,sizeof(kdu_uint32)*8);
  tmp_path_vertices[num_tmp_path_vertices++] = path_start;
  
  int n, p;
  for (n=0; n < num_path_members; n++)
    { 
      kdu_coords ep1, ep2;
      int idx = path_members[n];
      if (!editor->get_path_segment_for_region(idx,ep1,ep2))
        continue; // Should not happen
      const jpx_roi *roi = editor->get_region(idx);
      if ((roi == NULL) || roi->is_elliptical)
        continue;
      kdu_uint32 mask=(kdu_uint32)(1<<(idx & 31));
      original_path_members[idx>>5] |= mask;
      if (ep1 == ep2)
        continue;
      for (p=0; p < 4; p++)
        region_vertices[4*num_regions+p]=roi->vertices[p];
      num_regions++;
      path_start = (ep1==path_start)?ep2:ep1;
      tmp_path_vertices[num_tmp_path_vertices++] = path_start;
    }
  
  int sense = examine_path(tmp_path_vertices, num_tmp_path_vertices);
  if (sense == 0)
    return false; // Not an acceptable path

  // Rotate the vertices of the quadrilaterals so that they agree with the
  // direction of the path and also determine which direction the interior
  // of the path lies in.
  for (n=0; n < num_regions; n++)
    { 
      kdu_coords *vertices=region_vertices+4*n; // Points to 4 vertices
      kdu_coords *path_eps=tmp_path_vertices+n; // Points to 2 path end-points

      // First rotate the vertices so that path_eps[0] is the mid-point of
      // vertices 0 and 1 and path_eps[1] is the mid-point of vertices 2 and 3.
      int p0=-1;
      kdu_coords mp[4];
      for (p=0; p < 4; p++)
        { 
          mp[p] = jx_midpoint(vertices[p],vertices[(p+1)&3]);
          if (mp[p] == path_eps[0])
            p0 = p;
        }
      if ((p0 < 0) || (mp[(p0+2)&3] != path_eps[1]))
        return false;
      for (; p0 > 0; p0--)
        { // Rotate until `p0' = 0
          mp[0] = vertices[0];
          vertices[0] = vertices[1];
          vertices[1] = vertices[2];
          vertices[2] = vertices[3];
          vertices[3] = mp[0];
        }
    }
  
  // Now use the `sense' of the path to figure out where the
  // external edges and the shared edges are; we also adjust the interior
  // vertices of path bricks which cross only at their mid-points so that
  // all boundary points of the polygonal boundary to be filled correspond
  // to region vertices.
  kdu_coords prev_edge[2]; // Vertices at the end of the previous region
  kdu_coords first_edge[2]; // Vertices at the start of the first region
  prev_edge[0] = region_vertices[4*num_regions-2]; // Previous vertex 2
  prev_edge[1] = region_vertices[4*num_regions-1]; // Previous vertex 3
  first_edge[0] = region_vertices[0]; // First vertex 0
  first_edge[1] = region_vertices[1]; // First vertex 1
  int prev_n = num_regions-1;
  for (n=0; n < num_regions; prev_n=n, n++)
    {
      kdu_coords *vertices = region_vertices+4*n; // Points to four vertices
      kdu_coords *next_edge = (n<(num_regions-1))?(vertices+4):first_edge;
             // `next_edge' gives the first two vertices of the next edge
      int next_n = n+1;
      next_n = (next_n==num_regions)?0:next_n;
      kdu_coords *path_eps=tmp_path_vertices+n; // Points to 2 path end-points
      
      if (sense > 0)
        { // Outer edge runs from vertex 1 to vertex 2 (this is edge 1)
          region_edges[4*n+1] = -1; // Edge 1 is external
          region_edges[4*n+3] = JXPF_INTERNAL_EDGE; // Edge 3 internal unshared
          if (vertices[1] != prev_edge[0])
            { // Move vertex 0 to the path segment end-point
              vertices[0] = path_eps[0];
              region_edges[4*n+0] = -1; // Edge 0 is external
            }
          else if (vertices[0] != prev_edge[1])
            region_edges[4*n+0]=JXPF_INTERNAL_EDGE; // Edge 0 internal unshared
          else
            region_edges[4*n+0] = 4*prev_n+2; // Edge 0 shared with prev edge 2
          prev_edge[0] = vertices[2]; // Prepare for the next iteration
          prev_edge[1] = vertices[3];
          if (vertices[2] != next_edge[1])
            { // Move vertex 3 to the path segment end-point and fix it
              vertices[3] = path_eps[1];
              region_edges[4*n+2] = -1; // Edge 2 is external
            }
          else if (vertices[3] != next_edge[0])
            region_edges[4*n+2]=JXPF_INTERNAL_EDGE; // Edge 2 internal unshared
          else
            region_edges[4*n+2] = 4*next_n+0; // Edge 2 shared with next edge 0
        }
      else
        { // Outer edge runs from vertex 3 to vertex 0 (this is edge 3)
          region_edges[4*n+3] = -1; // Edge 3 is external
          region_edges[4*n+1] = JXPF_INTERNAL_EDGE; // Edge 1 internal unshared
          if (vertices[0] != prev_edge[1])
            { // Move vertex 1 to the path segment end-point and fix it
              vertices[1] = path_eps[0];
              region_edges[4*n+0] = -1; // Edge 0 is external
            }
          else if (vertices[1] != prev_edge[0])
            region_edges[4*n+0]=JXPF_INTERNAL_EDGE; // Edge 0 internal unshared
          else
            region_edges[4*n+0] = 4*prev_n+2; // Edge 0 shared with prev edge 2
          prev_edge[0] = vertices[2]; // Prepare for the next iteration
          prev_edge[1] = vertices[3];
          if (vertices[3] != next_edge[0])
            { // Move vertex 2 to the path segment end-point and fix it
              vertices[2] = path_eps[1];
              region_edges[4*n+2] = -1; // Edge 2 is external
            }
          else if (vertices[2] != next_edge[1])
            region_edges[4*n+2]=JXPF_INTERNAL_EDGE; // Edge 2 internal unshared
          else
            region_edges[4*n+2] = 4*next_n+0; // Edge 2 shared with next edge 0
        }
    }
  return true;
}

/*****************************************************************************/
/*                    jx_path_filler::init (second form)                     */
/*****************************************************************************/

bool
  jx_path_filler::init(const kdu_coords *src_vertices, int num_src_vertices)
{
  // Start by resetting various members
  next = NULL;
  container = NULL;
  num_tmp_path_vertices = num_regions = 0;
  memset(original_path_members,0,sizeof(kdu_uint32)*8);

  // See if the vertices form an allowable polygonal boundary and find the
  // sense of the boundary.
  if ((num_src_vertices < 3) || (num_src_vertices > JXPF_MAX_REGIONS))
    return false;
  int sense = examine_path(src_vertices,num_src_vertices);
  if (sense == 0)
    return false;
  
  // Now set up one initial quadrilateral for each edge of the polygon. We
  // could use less regions than this, of course, but starting with more
  // regions (and hence more free edges) allows the path filling algorithm
  // to find better filling strategies -- otherwise, we tend to get very
  // skewed regions which cost more to paint.
  int n;
  for (n=0; n < (num_src_vertices-1); n++, num_regions++)
    {
      assert(num_regions < JXPF_MAX_REGIONS);
      kdu_coords *vertices = region_vertices + 4*num_regions;
      int *edges = region_edges + 4*num_regions;
      vertices[0] = vertices[3] = src_vertices[n];
      vertices[1] = vertices[2] = src_vertices[n+1];
      if (sense > 0)
        { // Vertices run clockwise around boundary
          edges[0] = -1;  edges[2] = JXPF_INTERNAL_EDGE;
        }
      else
        { // Vertices run anti-clockwise around boundary
          edges[2] = -1; edges[0] = JXPF_INTERNAL_EDGE;
        }
      int next_region_idx = (n<(num_src_vertices-2))?(n+1):0;
      int prev_region_idx = (n==0)?(num_src_vertices-2):(n-1);
      edges[1] = 4*next_region_idx+3;
      edges[3] = 4*prev_region_idx+1;
    }
  if (!check_integrity())
    { assert(0); return false; }
  return true;
}

/*****************************************************************************/
/*                       jx_path_filler::examine_path                        */
/*****************************************************************************/

int jx_path_filler::examine_path(const kdu_coords *vertices, int num_vertices)
{
  // Make sure the vertices form a closed loop
  if ((num_vertices < 3) || (vertices[0] != vertices[num_vertices-1]))
    return 0;
  
  // Next, check whether or not the path intersects itself.  Intersections
  // at path segment end-points are also problematic, so long as we check
  // only path segment end-points which are not adjacent to one another.
  int n, p;
  for (n=2; n < (num_vertices-1); n++)
    {
      kdu_coords ep1 = vertices[n], ep2 = vertices[n+1];
      p = (n==(num_vertices-2))?1:0;
      for (; p < (n-1); p++)
        {
          kdu_coords pA = vertices[p], pB = vertices[p+1];
          if ((pA == ep1) || (pA == ep2) || (pB == ep1) || (pB == ep2))
            return 0; // Non-adjacent segs shouldn't have common end-points
          // Need to know if (ep1->ep2) crosses (pA->pB).  The code here is
          // essentially reproduced from `jpx_roi::check_edge_intersection'.
          kdu_long AmB_x=pA.x-pB.x, AmB_y=pA.y-pB.y;
          kdu_long DmC_x=ep2.x-ep1.x, DmC_y=ep2.y-ep1.y;
          kdu_long AmC_x=pA.x-ep1.x, AmC_y=pA.y-ep1.y;
          kdu_long det = AmB_x * DmC_y - DmC_x * AmB_y;
          kdu_long t = DmC_y*AmC_x - DmC_x * AmC_y;
          kdu_long u = AmB_x*AmC_y - AmB_y * AmC_x;
          if (det < 0) { det=-det; t=-t; u=-u; }
          if ((t > 0) && (t < det) && (u > 0) && (u < det))
            return 0; // The path segments cross each other
        }
    }
  
  // Work out the direction of the boundary.  There are many ways to determine
  // the direction (or sense) of a boundary.  In the following, we do this by
  // projecting a vector in the rightward and leftward direction from the
  // centre of each boundary segment and determining whether or not it
  // intersects any other boundary segment.  If there is no intersection
  // in some direction, that path segment indicates the sense of the boundary,
  // "almost certainly".  The only caveat to this is that an intersection
  // might be missed if the path segment's mid-point is rounded to one of
  // its end-points and that end-point happens to be shared with an
  // adjacent segment where an intersection would be found with the true
  // mid-point.  We could track down this special case by considering two
  // different rounding policies for the mid-point, but we prefer simply to
  // count the number of missing intersections on the left and the number
  // of missing intersections on the right and take the majority vote.
  int sense = 0;
  for (n=0; n < (num_vertices-1); n++)
    {
      const kdu_coords *path_eps = vertices+n; // Points to 2 path end-points
      kdu_coords rvec; // Rightward pointing vector as we follow path segment
      rvec.x = path_eps[0].y-path_eps[1].y;
      rvec.y = path_eps[1].x-path_eps[0].x;
      kdu_coords pA = jx_midpoint(path_eps[0],path_eps[1]);
      bool path_lies_to_left=false, path_lies_to_right=false;
      for (p=n+1; (p=(p<(num_vertices-1))?p:0) != n; p++)
        { // Consider all the other segments
          // Need to know if (pA -> pA+/-rvec) crosses
          // [pC->pD] where pC and pD are the end-points of path segment p.
          // That is, we want to solve pA+t*rvec = pC+u*(pD-pC) for u in
          // the closed interval [0,1]. If such a u exists, we are
          // interested in the sign of t.  That is, we have
          // pC-pA = t*rvec+u*(pC-pD).
          kdu_coords pC = vertices[p], pD = vertices[p+1];
          kdu_long CmD_x=pC.x-pD.x, CmD_y=pC.y-pD.y;
          kdu_long CmA_x=pC.x-pA.x, CmA_y=pC.y-pA.y;
          kdu_long det = rvec.x*CmD_y - rvec.y*CmD_x;
          kdu_long t_by_det = CmD_y*CmA_x - CmD_x*CmA_y;
          kdu_long u_by_det = rvec.x*CmA_y - rvec.y*CmA_x;
          if (det < 0)
            { det=-det; t_by_det=-t_by_det; u_by_det=-u_by_det; }
          if ((u_by_det < 0) || (u_by_det > det))
            continue; // u does not belong to [0,1).
          if (t_by_det < 0)
            path_lies_to_left = true;
          else if (t_by_det > 0)
            path_lies_to_right = true;
        }
      if (path_lies_to_left != path_lies_to_right)
        { // This segment readily identifies the sense of the path
          if (path_lies_to_right)
            { // We must be traversing the path clockwise
              sense++;
            }
          else
            { // We must be traversing the path anti-clockwise
              sense--;
            }
        }
    }
  return sense;
}

/*****************************************************************************/
/*                  jx_path_filler::import_internal_boundary                 */
/*****************************************************************************/

void jx_path_filler::import_internal_boundary(const jx_path_filler *src)
{
  if (((src->num_regions + this->num_regions) > JXPF_MAX_REGIONS) ||
      !contains(src))
    return;
  int n, offset=4*this->num_regions;
  this->num_regions += src->num_regions;
  for (n=0; n < (4*src->num_regions); n++)
    { 
      this->region_vertices[offset+n] = src->region_vertices[n];
      int idx = src->region_edges[n];
      if (idx < 0)
        idx = JXPF_INTERNAL_EDGE;
      else if (idx >= JXPF_INTERNAL_EDGE)
        idx = -1;
      else
        idx += offset;
      this->region_edges[offset+n] = idx;
    }
}

/*****************************************************************************/
/*                         jx_path_filler::contains                          */
/*****************************************************************************/

bool jx_path_filler::contains(const jx_path_filler *src) const
{
  if (intersects(src))
    return false;
  
  int n, p, src_edge_idx=0;
  const kdu_coords *src_vertices = src->region_vertices;
  for (n=0; n < src->num_regions; n++, src_vertices+=4)
    for (p=0; p < 4; p++, src_edge_idx++)
      {
        if (src->region_edges[src_edge_idx] >= 0)
          continue; // Not an external edge
        const kdu_coords *pA = src_vertices+p, *pB=src_vertices+((p+1)&3);
        kdu_coords src_rvec; // Right pointing vector as we follow the src edge
        src_rvec.x = pA->y-pB->y;  src_rvec.y = pB->x-pA->x;
        // We need to know if (pA -> pA+t*src_rvec) crosses [pC->pD] for any
        // negative value of t, where [pC->pD] is some external edge of the
        // current object.  If not, the `src' object is not completely
        // contained within us.  More to the point, we need to know that the
        // smallest value of |t| for which such an intersection occurs
        // corresponds to intersection from the inside of the current object.
        // The intersection occurs from the inside if the inner product between
        // `rvec' and a rightward pointing vector from the intersecting
        // external edge of the current object is positive.
        int m, q, edge_idx=0;
        const kdu_coords *vertices=this->region_vertices;
        double min_abs_t=-1.0;
        kdu_long ip_for_min_abs_t=-1; // Inner prod at min |t| should be +ve
        for (m=0; m < num_regions; m++, vertices+=4)
          { 
            for (q=0; q < 4; q++, edge_idx++)
              { 
                if (region_edges[edge_idx] >= 0)
                  continue; // Not an external edge
                const kdu_coords *pC=vertices+q, *pD=vertices+((q+1)&3);
                kdu_long CmD_x=pC->x-pD->x, CmD_y=pC->y-pD->y;
                kdu_long CmA_x=pC->x-pA->x, CmA_y=pC->y-pA->y;
                kdu_long det = src_rvec.x*CmD_y - src_rvec.y*CmD_x;
                kdu_long t_by_det = CmD_y*CmA_x - CmD_x*CmA_y;
                kdu_long u_by_det = src_rvec.x*CmA_y - src_rvec.y*CmA_x;
                if (det < 0)
                  { det=-det; t_by_det=-t_by_det; u_by_det=-u_by_det; }
                if ((u_by_det >= 0) && (u_by_det <= det) &&
                    (det > 0) && (t_by_det < 0))
                  { // We have an intersection
                    double abs_t = -((double) t_by_det) / ((double) det);
                    if ((min_abs_t > 0.0) && (abs_t > min_abs_t))
                      continue;
                    min_abs_t = abs_t;
                    kdu_coords rvec;
                    rvec.x = pC->y-pD->y;  rvec.y = pD->x-pC->x;
                    ip_for_min_abs_t =(((kdu_long) rvec.x)*src_rvec.x +
                                       ((kdu_long) rvec.y)*src_rvec.y);
                  }                
              }
          }
        if (ip_for_min_abs_t < 0)
          return false; // Not contained
      }
  return true;
}

/*****************************************************************************/
/*                        jx_path_filler::intersects                         */
/*****************************************************************************/

bool jx_path_filler::intersects(const jx_path_filler *src) const
{
  int n, p;
  const kdu_coords *vertices=region_vertices;
  for (n=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++)
      {
        const kdu_coords *v1=vertices+p, *v2=vertices+((p+1)&3);
        int m, q;
        const kdu_coords *src_vertices=src->region_vertices;
        for (m=0; m < src->num_regions; m++, src_vertices+=4)
          for (q=0; q < 4; q++)
            {
              const kdu_coords *w1=src_vertices+q, *w2=src_vertices+((q+1)&3);
                 // Need to know if (w1->w2) crosses (v1->v2).  The code is
                 // essentially that of `jpx_roi::check_edge_intersection'.
              kdu_long AmB_x=v1->x-v2->x, AmB_y=v1->y-v2->y;
              kdu_long DmC_x=w2->x-w1->x, DmC_y=w2->y-w1->y;
              kdu_long AmC_x=v1->x-w1->x, AmC_y=v1->y-w1->y;
              kdu_long det = AmB_x*DmC_y - DmC_x*AmB_y;
              kdu_long t_by_det = DmC_y*AmC_x - DmC_x * AmC_y;
              kdu_long u_by_det = AmB_x*AmC_y - AmB_y * AmC_x;
              if (det < 0)
                { det=-det; t_by_det=-t_by_det; u_by_det=-u_by_det; }
              if ((t_by_det >= 0) && (t_by_det <= det) && (det > 0) &&
                  (u_by_det >= 0) && (u_by_det <= det))
                return true;
            }
      }
  return false;    
}

/*****************************************************************************/
/*                 jx_path_filler::check_boundary_violation                  */
/*****************************************************************************/

bool jx_path_filler::check_boundary_violation(const kdu_coords *C,
                                              const kdu_coords *D) const
{
  int n, p, edge_idx=0;
  const kdu_coords *vertices = region_vertices;
  for (n=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++, edge_idx++)
      {
        if (region_edges[edge_idx] >= 0)
          continue; // Not an external edge
        kdu_coords pA = vertices[p], pB = vertices[(p+1)&3];
        // Need to know if (v0->v1) crosses (pA->pB).  The code here is
        // essentially reproduced from `jpx_roi::check_edge_intersection'.
        kdu_long AmB_x=pA.x-pB.x, AmB_y=pA.y-pB.y;
        kdu_long DmC_x=D->x-C->x, DmC_y=D->y-C->y;
        kdu_long AmC_x=pA.x-C->x, AmC_y=pA.y-C->y;
        kdu_long det = AmB_x * DmC_y - DmC_x * AmB_y;
        kdu_long t = DmC_y*AmC_x - DmC_x * AmC_y;
        kdu_long u = AmB_x*AmC_y - AmB_y * AmC_x;
        if (det < 0) { det=-det; t=-t; u=-u; }
        if ((t > 0) && (t < det) && (u > 0) && (u < det))
          return true; // The line segments cross each other
      }
  return false;
}

/*****************************************************************************/
/*                 jx_path_filler::check_boundary_violation                  */
/*****************************************************************************/

bool jx_path_filler::check_boundary_violation(const jpx_roi &roi) const
{
  int n, p, q, edge_idx=0;
  kdu_coords v[4];
  roi.get_quadrilateral(v[0],v[1],v[2],v[3]);
  const kdu_coords *vertices = region_vertices;
  for (n=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++, edge_idx++)
      { 
        if (region_edges[edge_idx] >= 0)
          continue; // Not an external edge
        kdu_coords pA = vertices[p], pB = vertices[(p+1)&3];
        int u_type[4] = {0,0,0,0};
           // The above two arrays are used to record information discovered
           // regarding intersections between each edge and the infinitely
           // extended line which passes through pA and pB.  Specifically, for
           // the q'th edge, running from v1_q=v[q] to v2_q=v[(q+1)&3], let
           // x be the intersection point such that x=v1_q+t(v2_q-v1_q) and
           // x=pA+u(pB-pA).  We consider an intersection to exist if t > 0
           // and t <= 1 -- that is, we count intersections only with the first
           // of the two end-points of edge q.  We record u_type[q] = 0 if
           // there is no intersection, u_type[q] = 1 if the intersection
           // corresponds to u <= 0, u_type[q] = 3 if the intersection
           // corresponds to u >= 1 and u_type[q] = 2 if the intersection
           // corresponds to 0 < u < 1.  For a boundary violation, we need
           // two distinct values of q, such that u_type[q1] and u_type[q2]
           // are both non-zero and have different values.
        for (q=0; q < 4; q++)
          { // Fill out the u_type values.  To find the values of u and t,
            // note that (v1_q-v2_q  pB-pA)*(t u)^t = v1_q-pA.
            kdu_long V1m2_x = v[q].x-v[(q+1)&3].x;
            kdu_long V1m2_y = v[q].y-v[(q+1)&3].y;
            kdu_long BmA_x = pB.x-pA.x, BmA_y = pB.y-pA.y;
            kdu_long V1mA_x = v[q].x-pA.x, V1mA_y = v[q].y-pA.y;
            kdu_long det = V1m2_x*BmA_y - V1m2_y*BmA_x;
            kdu_long t = BmA_y*V1mA_x - BmA_x*V1mA_y;
            kdu_long u = V1m2_x*V1mA_y - V1m2_y*V1mA_x;
            if (det == 0)
              continue; // Lines are parallel; intersections are not a problem
            if (det < 0)
              { det = -det; t = -t; u = -u; }
            if ((t <= 0) || (t > det))
              continue; // No intersection with (v1_q,v2_q].
            if (u <= 0)
              u_type[q] = 1;
            else if (u >= det)
              u_type[q] = 3;
            else
              u_type[q] = 2;
            
            // Now see if we have another intersection -- for violation
            if ((q == 0) || (u_type[q] == 0))
              continue;
            int ref_q;
            for (ref_q=0; ref_q < q; ref_q++)
              if ((u_type[ref_q] != 0) && (u_type[ref_q] != u_type[q]))
                return true; // Found boundary violation
          }
      }
  return false;
}
  
/*****************************************************************************/
/*                          jx_path_filler::process                          */
/*****************************************************************************/

bool jx_path_filler::process()
{
  bool early_finish = false;
  while (join()) {
    if (early_finish) return true;
  }
  while (simplify()) {
    if (early_finish) return true;
  }
  int num_internal_edges = count_internal_edges();
  if (num_internal_edges > 0)
    { // Simplification may have split apart a zero length edge, rendering it
      // internal, so some extra joining may be in order.
      while (join()) {
        if (early_finish) return true;
      }
      num_internal_edges = count_internal_edges();
    }
  
  if (num_internal_edges > 0)
    { 
      if (early_finish)
        return true;
      while (fill_interior()) {
        if (early_finish)
          return true;
        join();
      }
      if (early_finish)
        return true;
      while (simplify());
      while (join());
      num_internal_edges = count_internal_edges();
    }
  if (num_internal_edges == 1)
    assert(0); // This never makes sense
  return (num_internal_edges == 0);
}

/*****************************************************************************/
/*                    jx_path_filler::check_integrity                        */
/*****************************************************************************/

bool jx_path_filler::check_integrity()
{
  bool result = true;
  int n, m, num_edges=num_regions<<2;
  for (n=0; n < num_edges; n++)
    if (((m=region_edges[n]) < 0) || (m == JXPF_INTERNAL_EDGE))
      continue;
    else if ((m > JXPF_MAX_VERTICES) || (region_edges[m] != n)) 
      {
        result = false;
        break;
      }
  return result;
}

/*****************************************************************************/
/*                  jx_path_filler::count_internal_edges                     */
/*****************************************************************************/

int jx_path_filler::count_internal_edges()
{
  int n, p, edge_idx, count=0;
  kdu_coords *vertices=region_vertices;
  for (n=edge_idx=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++, edge_idx++)
      {
        assert(region_edges[edge_idx] <= JXPF_INTERNAL_EDGE);
        if ((region_edges[edge_idx] == JXPF_INTERNAL_EDGE) &&
            (vertices[p] != vertices[(p+1)&3]))
          count++;
      }
  return count;
}

/*****************************************************************************/
/*                           jx_path_filler::join                            */
/*****************************************************************************/

bool jx_path_filler::join()
{
  bool any_change = false;
  int n, p, edge_idx=0;
  kdu_coords *vertices = region_vertices;
  for (n=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++, edge_idx++)
      { 
        assert(region_edges[edge_idx] <= JXPF_INTERNAL_EDGE);
        if (region_edges[edge_idx] != JXPF_INTERNAL_EDGE)
          continue; // This edge is already either external or shared
        const kdu_coords *ep[2] = // End-points of the `edge_idx' edge
          { vertices+p, vertices+((p+1)&3) };
        
        int best_join_idx = -1; // Best edge to join to
        bool best_one_vertex_exact = false;
        kdu_long best_dist_sq=0; // For finding best edge
        const kdu_coords *best_ep[2]={NULL,NULL};// Best substitutes for *ep[i]
        kdu_coords *join_vertices = region_vertices;
        int m, q, join_idx=0; // Stands for candidate edge to "join" to
        for (m=0; m < num_regions; m++, join_vertices+=4)
          for (q=0; q < 4; q++, join_idx++)
            {
              if (m == n)
                continue; // Only join to another region
              assert(region_edges[join_idx] <= JXPF_INTERNAL_EDGE);
              if (region_edges[join_idx] != JXPF_INTERNAL_EDGE)
                continue; // This edge is not available for sharing
              kdu_coords *jp[2] = // Reversed end-points of the `join_idx' edge
                { join_vertices+((q+1)&3), join_vertices+q};
              if (!check_vertex_changes_for_edge(edge_idx,jp[0],jp[1]))
                continue;
              kdu_long dx, dy, dist_sq=0;
              bool one_vertex_exact=false;
              for (int i=0; i < 2; i++)
                {
                  dx = ep[i]->x - jp[i]->x; dy = ep[i]->y - jp[i]->y;
                  if ((dx|dy) == 0)
                    one_vertex_exact = true;
                  dist_sq += dx*dx + dy*dy;
                }
              if ((best_join_idx < 0) ||
                  (one_vertex_exact && !best_one_vertex_exact) ||
                  ((one_vertex_exact == best_one_vertex_exact) &&
                   (dist_sq < best_dist_sq)))
                { best_join_idx=join_idx; best_dist_sq = dist_sq;
                  best_one_vertex_exact = one_vertex_exact;
                  best_ep[0]=jp[0]; best_ep[1]=jp[1]; }
            }

        if (best_join_idx >= 0)
          { // Perform the join
            any_change = true;
            apply_vertex_changes_for_edge(edge_idx,best_ep[0],best_ep[1]);
            region_edges[edge_idx] = best_join_idx;
            region_edges[best_join_idx] = edge_idx;
          }
      }
  
  return any_change;
}

/*****************************************************************************/
/*                         jx_path_filler::simplify                          */
/*****************************************************************************/

bool jx_path_filler::simplify()
{
  bool any_change = false;
  int n, p, edge_idx;
  
  kdu_coords *vertices = region_vertices;
  for (n=0; n < num_regions; n++, vertices+=4)
    { 
      if (remove_degenerate_region(n))
        { 
          n--; vertices-=4; // So loop correctly visits the next region
          continue;
        }
      
      // Let's see if two adjacent triangles can be converted to a single
      // quadrilateral.
      int tri_edges1[3], tri_edges2[3];
      if (is_region_triangular(n,tri_edges1))
        { 
          int nbr_idx=-1;
          for (p=0; p < 3; p++)
            {
              nbr_idx = region_edges[tri_edges1[p]];
              if ((nbr_idx >= 0) && (nbr_idx != JXPF_INTERNAL_EDGE) &&
                  is_region_triangular(nbr_idx>>2,tri_edges2))
                break;
            }
          if (p < 3)
            { // Found two adjacent triangles
              int k = nbr_idx>>2; // k is the index of the second region
              int te1_next = tri_edges1[(p==2)?0:(p+1)]; // Non-shared edges
              int te1_prev = tri_edges1[(p==0)?2:(p-1)]; // from triangle 1
              for (p=0; p < 3; p++)
                if (tri_edges2[p] == nbr_idx)
                  break; // Found the common edge in the second triangle
              assert(p < 3);
              int te2_next = tri_edges2[(p==2)?0:(p+1)]; // Non-shared edges
              int te2_prev = tri_edges2[(p==0)?2:(p-1)]; // from triangle 2
              int quad_edges[4] = {te1_next,te1_prev,te2_next,te2_prev};
                    // Clockwise ordered edges for the combined quadrilateral
              int nbr_edges[4];
              kdu_coords quad_vertices[4];
              for (p=0; p < 4; p++)
                { // Find the connectivity of the existing edges we are
                  // replacing.
                  quad_vertices[p] = region_vertices[quad_edges[p]];
                  nbr_edges[p] = region_edges[quad_edges[p]];
                  assert((nbr_edges[p] < 0) ||
                         (nbr_edges[p] == JXPF_INTERNAL_EDGE) ||
                         (region_edges[nbr_edges[p]] == quad_edges[p]));
                  nbr_idx = nbr_edges[p]>>2;
                  if ((nbr_idx == n) || (nbr_idx == k))
                    break; // More than one of the triangular edges in common
                }
              if (p == 4)
                { // Now we are committed to reducing the triangles to a quad
                  remove_edge_references_to_region(k); // So can reuse region k
                  int k_by_4 = k<<2;
                  for (p=0; p < 4; p++)
                    { // Write the new quadrilateral into region k
                      region_vertices[k_by_4+p] = quad_vertices[p];
                      region_edges[k_by_4+p] = nbr_edges[p];
                      if ((nbr_edges[p] >= 0) &&
                          (nbr_edges[p] != JXPF_INTERNAL_EDGE))
                        region_edges[nbr_edges[p]] = k_by_4+p;
                    }
                  remove_region(n);
                  n--; vertices-=4; // So loop correctly visits the next region
                  continue; // Go back to the main loop
                }
            }
        }
      
      // Let's try and make the region degenerate by squashing one vertex
      // to its the opposite one.
      for (edge_idx=4*n, p=0; p < 4; p++, edge_idx++)
        { // Consider moving vertices[p]          
          if (vertices[p] == vertices[(p-1)&3])
            continue; // We don't want to split up already joined vertices
          const kdu_coords *ep[2]={vertices+((p+2)&3),vertices+((p+1)&3)};
             // These are the new end-points we are considering for the edge
          if (vertices[p] == *ep[1])
            { // The region is triangular
              if (!check_vertex_changes_for_edge(edge_idx,ep[0],ep[0]))
                ep[0] = vertices+((p-1)&3); // Collapse the other way
              ep[1] = ep[0]; // Joined vertices move together
            }
          if (!check_vertex_changes_for_edge(edge_idx,ep[0],ep[1]))
            continue; // The move is not possible
          
          // If we get here, we can collapse region n
          apply_vertex_changes_for_edge(edge_idx,ep[0],ep[1]);
          break;
        }
      if ((p < 4) && remove_degenerate_region(n))
        { 
          any_change = true;
          n--; vertices-=4; // So loop correctly visits the next region
        }
    }
  return any_change;
}

/*****************************************************************************/
/*                       jx_path_filler::fill_interior                       */
/*****************************************************************************/

bool jx_path_filler::fill_interior()
{
  int n, p, edge_idx;
  
  kdu_coords *vertices = region_vertices;
  for (n=edge_idx=0; n < num_regions; n++, vertices+=4)
    for (p=0; p < 4; p++, edge_idx++)
      {
        if ((region_edges[edge_idx] != JXPF_INTERNAL_EDGE) ||
            (vertices[p] == vertices[(p+1)&3]))
          continue; // Not an interior edge
        // Now look for other internal edges which share the same end-points
        int m, q, alt_edge_idx;
        int nbr_edge_idx[2]={-1,-1};
        kdu_coords *alt_vertices=region_vertices;
        for (m=alt_edge_idx=0; m < num_regions; m++, alt_vertices+=4)
          for (q=0; q < 4; q++, alt_edge_idx++)
            {
              if ((alt_edge_idx == edge_idx) ||
                  (region_edges[alt_edge_idx] != JXPF_INTERNAL_EDGE) ||
                  (alt_vertices[q] == alt_vertices[(q+1)&3]))
                continue;
              if (alt_vertices[q] == vertices[(p+1)&3])
                nbr_edge_idx[0] = alt_edge_idx;
              if (alt_vertices[(q+1)&3] == vertices[p])
                nbr_edge_idx[1] = alt_edge_idx;
            }
        if ((nbr_edge_idx[0] >= 0) && (nbr_edge_idx[1] >= 0) &&
            add_quadrilateral(nbr_edge_idx[0],edge_idx,nbr_edge_idx[1]))
          return true;
        else if ((nbr_edge_idx[0] >= 0) &&
                 add_triangle(nbr_edge_idx[0],edge_idx))
          return true;
        else if ((nbr_edge_idx[1] >= 0) &&
                 add_triangle(edge_idx,nbr_edge_idx[1]))
          return true;
      }
  return false;
}

/*****************************************************************************/
/*              jx_path_filler::check_vertex_changes_for_edge                */
/*****************************************************************************/

bool jx_path_filler::check_vertex_changes_for_edge(int edge_idx,
                                                   const kdu_coords *v0,
                                                   const kdu_coords *v1,
                                                   int initial_edge_idx) const
{
  if (edge_idx == initial_edge_idx)
    return true;
  int n=edge_idx>>2, p=edge_idx&3;
  const kdu_coords *vertices = region_vertices + (n<<2);
  bool is_changed[2] = {vertices[p]!=*v0, vertices[(p+1)&3]!=*v1};
  if (!(is_changed[0] || is_changed[1]))
    return true;
  if (region_edges[edge_idx] < 0)
    return false; // Cannot move the edge, because it is external
  jpx_roi test_roi;
  test_roi.init_quadrilateral(vertices[(p-1)&3],*v0,*v1,vertices[(p+2)&3]);
  if (!test_roi.check_geometry())
    return false; // Not a valid quadrilateral
  if (check_boundary_violation(test_roi))
    return false;
  
  if (initial_edge_idx < 0)
    { // Need to explore the effect on any neighbour of `edge_idx' itself
      initial_edge_idx = edge_idx;
      if ((region_edges[edge_idx] < 0) ||
          check_boundary_violation(v0,v1) ||
          ((region_edges[edge_idx] != JXPF_INTERNAL_EDGE) &&
           !check_vertex_changes_for_edge(region_edges[edge_idx],v1,v0,
                                          initial_edge_idx)))
        return false;
    }

  // Now explore the edge and its neighbours to see if the move is allowed.
  int test_edge_idx = (n<<2) + ((p-1)&3); // Neighbouring edge to the left
  if (is_changed[0] && (test_edge_idx != initial_edge_idx))
    { 
      const kdu_coords *ep[2] = {vertices+((p-1)&3),v0};
      if ((region_edges[test_edge_idx] < 0) ||
          check_boundary_violation(ep[0],ep[1]) ||
          ((region_edges[test_edge_idx] != JXPF_INTERNAL_EDGE) &&
           !check_vertex_changes_for_edge(region_edges[test_edge_idx],
                                          ep[1],ep[0],initial_edge_idx)))
        return false;
    }
  test_edge_idx = (n<<2) + ((p+1)&3); // Neighbouring edge to the right
  if (is_changed[1] && (test_edge_idx != initial_edge_idx))
    { 
      const kdu_coords *ep[2] = {v1,vertices+((p+2)&3)};
      if ((region_edges[test_edge_idx] < 0) ||
          check_boundary_violation(ep[0],ep[1]) ||
          ((region_edges[test_edge_idx] != JXPF_INTERNAL_EDGE) &&
           !check_vertex_changes_for_edge(region_edges[test_edge_idx],
                                          ep[1],ep[0],initial_edge_idx)))
        return false;
    }
  
  // If we get here, all the tests have succeeded
  return true;
}

/*****************************************************************************/
/*              jx_path_filler::apply_vertex_changes_for_edge                */
/*****************************************************************************/

void jx_path_filler::apply_vertex_changes_for_edge(int edge_idx,
                                                   const kdu_coords *v0,
                                                   const kdu_coords *v1)
{
  assert((edge_idx >= 0) && (edge_idx < (num_regions*4)));
  int n=edge_idx>>2, p=edge_idx&3;
  kdu_coords *vertices = region_vertices+(n<<2);
  bool is_changed[2] = {vertices[p]!=*v0, vertices[(p+1)&3]!=*v1};
  if (!(is_changed[0] || is_changed[1]))
    return;
  assert(region_edges[edge_idx] >= 0);
  vertices[p] = *v0; vertices[(p+1)&3] = *v1;

  assert(region_edges[edge_idx] <= JXPF_INTERNAL_EDGE);
  if (region_edges[edge_idx] != JXPF_INTERNAL_EDGE)
    apply_vertex_changes_for_edge(region_edges[edge_idx],v1,v0);
  
  // Now propagate the change to neighbouring regions with which we share
  // an edge.
  int test_edge_idx = edge_idx + ((p-1)&3)-p; // Neighbouring edge to the left
  if (is_changed[0])
    { 
      const kdu_coords *ep[2] = {vertices+((p-1)&3), v0};
      assert(region_edges[test_edge_idx] <= JXPF_INTERNAL_EDGE);
      if (region_edges[test_edge_idx] != JXPF_INTERNAL_EDGE)
        apply_vertex_changes_for_edge(region_edges[test_edge_idx],ep[1],ep[0]);
    }
  test_edge_idx = (n<<2) + ((p+1)&3); // Neighbouring edge to the right
  if (is_changed[1])
    { 
      const kdu_coords *ep[2] = {v1, vertices+((p+2)&3)};
      assert(region_edges[test_edge_idx] <= JXPF_INTERNAL_EDGE);
      if (region_edges[test_edge_idx] != JXPF_INTERNAL_EDGE)
        apply_vertex_changes_for_edge(region_edges[test_edge_idx],ep[1],ep[0]);
    }
}

/*****************************************************************************/
/*                  jx_path_filler::is_region_triangular                     */
/*****************************************************************************/

bool jx_path_filler::is_region_triangular(int idx, int edges[])
{
  if ((idx < 0) || (idx == JXPF_INTERNAL_EDGE))
    return false;
  int k, p, base_n=4*idx;
  for (p=k=0; p < 4; p++)
    if (region_vertices[base_n+p] != region_vertices[base_n+((p+1)&3)])
      {
        if (k == 3) return false;
        edges[k++] = base_n+p;
      }
  return (k == 3);
}

/*****************************************************************************/
/*            jx_path_filler::remove_edge_references_to_region               */
/*****************************************************************************/

void jx_path_filler::remove_edge_references_to_region(int idx)
{
  int n, num_edges=num_regions<<2, base_idx=idx<<2;
  for (n=0; n < num_edges; n++)
    if ((region_edges[n] >= base_idx) && (region_edges[n] < (base_idx+4)))
      region_edges[n] = JXPF_INTERNAL_EDGE;
}

/*****************************************************************************/
/*                 jx_path_filler::remove_degenerate_region                  */
/*****************************************************************************/

bool jx_path_filler::remove_degenerate_region(int idx)
{
  if ((idx < 0) || (idx >= num_regions))
    return false;
  int base_idx = idx<<2;
  kdu_coords *vertices = region_vertices + base_idx;
  int e1[2], e2[2]; // Edges e1[i] and e2[i] are on top of each other
  if (vertices[0] == vertices[2])
    { // Edges 0->1 and 1->2 are overlapping, as are edges 2->3 and 3->0
      e1[0] = base_idx+0; e2[0] = base_idx+1;
      e1[1] = base_idx+2; e2[1] = base_idx+3;
    }
  else if (vertices[1] == vertices[3])
    { // Edges 1->2 and 2->3 are overlapping, as are edges 3->0 and 0->1
      e1[0] = base_idx+1; e2[0] = base_idx+2;
      e1[1] = base_idx+3; e2[1] = base_idx+0;
    }
  else if ((vertices[0]==vertices[1]) && (vertices[2]==vertices[3]))
    { // Edges 1->2 and 3->0 are overlapping
      e1[0] = base_idx+1; e2[0] = base_idx+3;
      e1[1] = e2[1] = -1;
    }
  else if ((vertices[1]==vertices[2]) && (vertices[3]==vertices[0]))
    { // Edges 2->3 and 0->1 are overlapping
      e1[0] = base_idx+2; e2[0] = base_idx+0;
      e1[1] = e2[1] = -1;
    }
  else
    return false;
  
  int p;
  for (p=0; p < 2; p++)
    { 
      if (e1[p] < 0)
        continue;
      if (region_edges[e1[p]] < 0)
        { int tmp=e1[p]; e1[p]=e2[p]; e2[p]=tmp; }
      if (region_edges[e1[p]] < 0)
        return false; // Both sides of the degenerate region are external
      if ((region_edges[e2[p]] < 0) &&
          (region_edges[e1[p]] == JXPF_INTERNAL_EDGE))
        return false; // Cannot transfer external edge to another region
    }
  
  // Now we are committed to removing the region; first exchange edge
  // connectivity
  for (p=0; p < 2; p++)
    { 
      if (e1[p] < 0)
        continue;
      int nbr1=region_edges[e1[p]], nbr2=region_edges[e2[p]];
      assert(nbr1 >= 0);
      if (nbr1 != JXPF_INTERNAL_EDGE)
        region_edges[nbr1] = nbr2;
      if ((nbr2 >= 0) && (nbr2 != JXPF_INTERNAL_EDGE))
        region_edges[nbr2] = nbr1;
      region_edges[e1[p]]=region_edges[e2[p]]=JXPF_INTERNAL_EDGE; // Not in use
    }
  for (p=0; p < 4; p++)
    { // Look for zero length edges which still think they are shared
      int nbr = region_edges[base_idx+p];
      if (nbr == JXPF_INTERNAL_EDGE) continue;
      region_edges[base_idx+p] = JXPF_INTERNAL_EDGE;
      if (nbr >= 0)
        region_edges[nbr] = JXPF_INTERNAL_EDGE;
    }
  
  // Now at last we can eliminate the region
  remove_region(idx);
  return true;
}

/*****************************************************************************/
/*                     jx_path_filler::remove_region                         */
/*****************************************************************************/

void jx_path_filler::remove_region(int idx)
{
  if ((idx < 0) || (idx >= JXPF_MAX_VERTICES))
    return;
  int base_idx = idx<<2;
  int n, p, *edges=region_edges;
  kdu_coords *vertices=region_vertices;
  for (n=0; n < idx; n++, edges+=4, vertices+=4)
    for (p=0; p < 4; p++)
      if ((edges[p] != JXPF_INTERNAL_EDGE) && (edges[p] >= base_idx))
        { // Have to adjust the edge reference
          if (edges[p] < (base_idx+4))
            { 
              assert(vertices[p] == vertices[(p+1)&3]);
              edges[p] = JXPF_INTERNAL_EDGE;
            }
          else
            edges[p] -= 4;
        }
  num_regions--;
  for (; n < num_regions; n++, edges+=4, vertices+=4)
    for (p=0; p < 4; p++)
      { 
        vertices[p] = vertices[p+4];
        if (((edges[p] = edges[p+4]) != JXPF_INTERNAL_EDGE) &&
            (edges[p] >= base_idx))
          { 
            if (edges[p] < (base_idx+4))
              { 
                assert(vertices[4+p] == vertices[4+((p+1)&3)]);
                edges[p] = JXPF_INTERNAL_EDGE;
              }
            else
              edges[p] -= 4;
          }
      }
}

/*****************************************************************************/
/*                    jx_path_filler::add_quadrilateral                      */
/*****************************************************************************/

bool jx_path_filler::add_quadrilateral(int shared_edge1, int shared_edge2,
                                       int shared_edge3)
{
  if (num_regions >= JXPF_MAX_REGIONS)
    return false;
  int shared_edges[3] = {shared_edge1,shared_edge2,shared_edge3};
  int p, shared_ends[3]; // Indices of the second vertex associated with edge
  for (p=0; p < 3; p++)
    shared_ends[p] = (shared_edges[p] & ~3) + (((shared_edges[p] & 3)+1)&3);
  assert(region_vertices[shared_edges[0]]==region_vertices[shared_ends[1]]);
  assert(region_vertices[shared_edges[1]]==region_vertices[shared_ends[2]]);
  kdu_coords vertices[4] =
    { region_vertices[shared_ends[0]], region_vertices[shared_ends[1]],
      region_vertices[shared_ends[2]], region_vertices[shared_edges[2]] };
  jpx_roi test_roi;
  test_roi.init_quadrilateral(vertices[0],vertices[1],vertices[2],vertices[3]);
  if (!test_roi.check_geometry())
    return false;
  if (check_boundary_violation(test_roi))
    return false;
  int n = num_regions++;
  int base_idx = n<<2;
  for (p=0; p < 4; p++)
    region_vertices[base_idx+p] = vertices[p];
  for (p=0; p < 3; p++)
    { 
      region_edges[base_idx+p] = shared_edges[p];
      region_edges[shared_edges[p]] = base_idx+p;
    }
  region_edges[base_idx+3] = JXPF_INTERNAL_EDGE;
  return true;
}

/*****************************************************************************/
/*                      jx_path_filler::add_triangle                         */
/*****************************************************************************/

bool jx_path_filler::add_triangle(int shared_edge1, int shared_edge2)
{
  if (num_regions >= JXPF_MAX_REGIONS)
    return false;
  int shared_edges[2] = {shared_edge1,shared_edge2};
  int p, shared_ends[2]; // Indices of the second vertex associated with edge
  for (p=0; p < 2; p++)
    shared_ends[p] = (shared_edges[p] & ~3) + (((shared_edges[p] & 3)+1)&3);
  assert(region_vertices[shared_edges[0]]==region_vertices[shared_ends[1]]);
  kdu_coords vertices[3] =
    { region_vertices[shared_ends[0]], region_vertices[shared_ends[1]],
      region_vertices[shared_edges[1]] };
  jpx_roi test_roi;
  test_roi.init_quadrilateral(vertices[0],vertices[1],vertices[2],vertices[2]);
  if (!test_roi.check_geometry())
    return false;
  if (check_boundary_violation(test_roi))
    return false;
  int n = num_regions++;
  int base_idx = n<<2;
  for (p=0; p < 3; p++)
    region_vertices[base_idx+p] = vertices[p];
  region_vertices[base_idx+3] = vertices[2]; // Double up last vertex
  for (p=0; p < 2; p++)
    { 
      region_edges[base_idx+p] = shared_edges[p];
      region_edges[shared_edges[p]] = base_idx+p;
    }
  region_edges[base_idx+2] = region_edges[base_idx+3] = JXPF_INTERNAL_EDGE;
  return true;
}


/* ========================================================================= */
/*                                jx_regions                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                       jx_regions::set_num_regions                         */
/*****************************************************************************/

void
  jx_regions::set_num_regions(int num)
{
  if (num < 0)
    num = 0;
  if (num <= max_regions)
    num_regions = num;
  else if (num == 1)
    {
      assert((regions == NULL) && (max_regions == 0));
      max_regions = num_regions = 1;
      regions = &bounding_region;
    }
  else
    {
      int new_max_regions = max_regions + num;
      jpx_roi *tmp = new jpx_roi[new_max_regions];
      for (int n=0; n < num_regions; n++)
        tmp[n] = regions[n];
      if ((regions != NULL) && (regions != &bounding_region))
        delete[] regions;
      regions = tmp;
      max_regions = new_max_regions;
      num_regions = num;
    }
}

/*****************************************************************************/
/*                            jx_regions::write                              */
/*****************************************************************************/

void
  jx_regions::write(jp2_output_box &box)
{
  int n, num_rois=0;
  for (n=0; (n < num_regions) && (num_rois < 255); n++, num_rois++)
    if ((regions[n].flags & JPX_QUADRILATERAL_ROI) ||
        (regions[n].is_elliptical && (regions[n].elliptical_skew.x ||
                                      regions[n].elliptical_skew.y)))
      {
        if (num_rois >= 254)
          break;
        num_rois++;
      }
  if (n < num_regions)
    { KDU_WARNING(w,0x11011002); w <<
      KDU_TXT("Cannot write all component regions to a single containing "
              "ROI Description (`roid') description box.  The JPX file "
              "format imposes a limit of 255 individual regions, but note "
              "that general quadrilaterals and oriented ellipses each "
              "consume 2 regions from this limit.");
    }
  box.write((kdu_byte) num_rois);
  jpx_roi *rp;
  for (rp=regions; n > 0; n--, rp++)
    {
      if (rp->is_elliptical)
        { // Write elliptical region
          kdu_coords centre, extent, skew;
          rp->get_ellipse(centre,extent,skew);
          box.write((kdu_byte)((rp->is_encoded)?1:0));
          box.write((kdu_byte) 1);
          box.write((kdu_byte) rp->coding_priority);
          box.write(centre.x);
          box.write(centre.y);
          box.write(extent.x);
          box.write(extent.y);
          if (skew.x || skew.y)
            {
              if (skew.x <= -extent.x) skew.x = 1-extent.x;
              if (skew.x >= extent.x) skew.x = extent.x-1;
              if (skew.y <= -extent.y) skew.y = 1-extent.y;
              if (skew.y >= extent.y) skew.y = extent.y-1;
              box.write((kdu_byte)((rp->is_encoded)?1:0));
              box.write((kdu_byte) 3);
              box.write((kdu_byte) rp->coding_priority);
              box.write(centre.x+skew.x);
              box.write(centre.y-skew.y);
              box.write((int) 1);
              box.write((int) 1);
            }
        }
      else
        { // Write rectangular region
          box.write((kdu_byte)((rp->is_encoded)?1:0));
          box.write((kdu_byte) 0);
          box.write((kdu_byte) rp->coding_priority);
          box.write(rp->region.pos.x);
          box.write(rp->region.pos.y);
          box.write(rp->region.size.x);
          box.write(rp->region.size.y);
          if (rp->flags & JPX_QUADRILATERAL_ROI)
            {
              kdu_dims inner_rect;
              int Rtyp = encode_general_quadrilateral(*rp,inner_rect);
              box.write((kdu_byte)((rp->is_encoded)?1:0));
              box.write((kdu_byte) Rtyp);
              box.write((kdu_byte) rp->coding_priority);
              box.write(inner_rect.pos.x);
              box.write(inner_rect.pos.y);
              box.write(inner_rect.size.x);
              box.write(inner_rect.size.y);
            }
        }
    }
}

/*****************************************************************************/
/*                            jx_regions::read                               */
/*****************************************************************************/

bool jx_regions::read(jp2_input_box &box)
{
  kdu_byte num=0; box.read(num);
  set_num_regions((int) num);
  int n;
  kdu_dims rect;
  kdu_coords bound_min, bound_lim, min, lim;
  for (n=0; n < num_regions; n++)
    {
      kdu_byte Rstatic=0, Rtyp=0, Rcp=0;
      kdu_uint32 Rlcx=0, Rlcy=0, Rwidth=0, Rheight=0;
      if (!(box.read(Rstatic) && box.read(Rtyp) &&
            box.read(Rcp) && box.read(Rlcx) && box.read(Rlcy) &&
            box.read(Rwidth) && box.read(Rheight) && (Rstatic < 2)))
        { KDU_WARNING(w,0x26100802); w <<
          KDU_TXT("Malformed ROI Descriptions (`roid') "
                  "box encountered in JPX data source.");
          return false;
        }
      rect.pos.x = (int) Rlcx;
      rect.pos.y = (int) Rlcy;
      rect.size.x = (int) Rwidth;
      rect.size.y = (int) Rheight;
      if (Rtyp == 0)
        { // Simple rectangle 
          regions[n].init_rectangle(rect,(Rstatic==1),Rcp);
        }
      else if (Rtyp == 1)
        { // Simple ellipse
          kdu_coords centre = rect.pos, extent = rect.size;
          regions[n].init_ellipse(centre,extent,kdu_coords(),
                                  (Rstatic==1),Rcp);
          rect = regions[n].region;
        }
      else if (Rtyp == 3)
        { // Oriented elliptical refinement
          kdu_coords centre, extent, skew;
          if ((n == 0) || (!regions[n-1].get_ellipse(centre,extent,skew)) ||
              (skew.x != 0) || (skew.y != 0) ||
              ((skew.x = rect.pos.x-centre.x) <= -extent.x) ||
              ((skew.y = centre.y-rect.pos.y) <= -extent.y) ||
              (skew.x >= extent.x) || (skew.y >= extent.y))
            { KDU_WARNING(w,0x0803001); w <<
              KDU_TXT("Malformed oriented elliptical region refinement "
                      "information found in ROI Descriptions (`roid') "
                      "box in JPX data source.  Oriented ellipses require "
                      "two consecutive regions, the first of which is a "
                      "simple ellipse, while the second has a centre which "
                      "lies within (but not on) the bounding rectangle "
                      "associated with the first ellipse.");
              return false;
            }
          num_regions--; n--;
          regions[n].init_ellipse(centre,extent,skew,regions[n].is_encoded,
                                  regions[n].coding_priority);
          rect = regions[n].region;
        }
      else if (((Rtyp & 1) == 0) && ((Rtyp & 7) >= 2) && ((Rtyp & 48) <= 32))
        { // Quadrilateral refinement
          kdu_dims outer_rect;
          if ((n == 0) || (!regions[n-1].get_rectangle(outer_rect)) ||
              ((rect & outer_rect) != rect))
            { KDU_WARNING(w,0x09011001); w <<
              KDU_TXT("Malformed quadrilateral region of interest "
                      "information found in ROI Descriptions (`roid') "
                      "box in JPX data source.  Generic "
                      "quadrilaterals require two consecutive, "
                      "regions, the first of which is a simple rectangle, "
                      "while the second identifies a second rectangular "
                      "region, contained within the first.");
              return false;
            }
          num_regions--; n--;
          int A = (Rtyp >> 6) & 3;
          int B = (Rtyp >> 4) & 3;
          int C = (Rtyp >> 3) & 1;
          int D = (Rtyp >> 1) & 3;
          if (!promote_roi_to_general_quadrilateral(regions[n],rect,A,B,C,D))
            return false;
          continue;
        }
      else
        { // Skip over unrecognized region type codes to allow for future
          // enhancements.
          num_regions--; n--; continue;
        }
      min = rect.pos;
      lim = min + rect.size;
      if (n == 0)
        { bound_min = min; bound_lim = lim; }
      else
        { // Grow `bound' to include `rect'
          if (min.x < bound_min.x)
            bound_min.x = min.x;
          if (min.y < bound_min.y)
            bound_min.y = min.y;
          if (lim.x > bound_lim.x)
            bound_lim.x = lim.x;
          if (lim.y > bound_lim.y)
            bound_lim.y = lim.y;
        }
    }
  bounding_region.region.pos = bound_min;
  bounding_region.region.size = bound_lim - bound_min;
  double wd_max = 0.1;
  for (n=0; n < num_regions; n++)
    {
      double dummy, wd=0.0;
      regions[n].measure_span(wd,dummy);
      wd_max = (wd > wd_max)?wd:wd_max;
    }
  max_width = (int) ceil(wd_max);
  return true;
}

/*****************************************************************************/
/* STATIC      jx_regions::promote_roi_to_general_quadrilateral              */
/*****************************************************************************/

bool
  jx_regions::promote_roi_to_general_quadrilateral(jpx_roi &roi,
                                                   kdu_dims inner_rect,
                                                   int A, int B, int C, int D)
{
  A &= 3; B &= 3; D &= 3; C &= 1; // Just to make sure there are no disasters
  kdu_coords v[4];
  v[0] = roi.region.pos;
  v[1] = inner_rect.pos;
  v[2] = v[1] + inner_rect.size - kdu_coords(1,1);
  v[3] = v[0] + roi.region.size - kdu_coords(1,1);
  assert((v[0].x <= v[1].x) && (v[0].y <= v[1].y) &&
         (v[1].x <= v[2].x) && (v[1].y <= v[2].y) &&
         (v[2].x <= v[3].x) && (v[2].y <= v[3].y));

  int B1 = B+1+C; if (B1 > 2) B1-=3;
  int B2 = B+2-C; if (B2 > 2) B2-=3;
  int a=A, b=(A+1+B)&3, c=(A+1+B1)&3, d=(A+1+B2)&3;
  int x[4] = {v[0].x,v[1].x,v[2].x,v[3].x};
  v[0].x = x[a]; v[1].x = x[b]; v[2].x = x[c]; v[3].x = x[d];
  
  roi.vertices[0] = v[0];
  switch (D) {
    case 1:
      roi.vertices[1]=v[1]; roi.vertices[2]=v[2]; roi.vertices[3]=v[3];
      break;
    case 2:
      roi.vertices[1]=v[2]; roi.vertices[2]=v[3]; roi.vertices[3]=v[1];
      break;      
    case 3:
      roi.vertices[1]=v[3]; roi.vertices[2]=v[1]; roi.vertices[3]=v[2];
      break;
    default: return false;
  };
  roi.flags = JPX_QUADRILATERAL_ROI;
  if (roi.check_geometry())
    return true;
  
  // Reverse the vertex order, keeping the top vertex at index 0
  kdu_coords tmp = roi.vertices[1];
  roi.vertices[1] = roi.vertices[3];
  roi.vertices[3] = tmp;
  if (!roi.check_geometry())
    { KDU_WARNING(w,0x11011001); w <<
      KDU_TXT("Illegal quadrilateral vertices encountered while reading an "
              "ROI Description (`roid') box from a JPX source.  Quadrilateral "
              "edges cross through each other!");
      return false;
    }
  return true;
}

/*****************************************************************************/
/* STATIC           jx_regions::encode_general_quadrilateral                 */
/*****************************************************************************/

int
  jx_regions::encode_general_quadrilateral(jpx_roi &roi, kdu_dims &inner_rect)
{
  kdu_coords tmp, v[4] =
    { roi.vertices[0],roi.vertices[1],roi.vertices[2],roi.vertices[3] };
  
  // Start by sorting the vertices into increasing order of their Y coordinate,
  // keeping track of the connectivity.  Note that v[0] is already the
  // top-most vertex so it will not move.
  
  assert((v[0].y <= v[1].y) && (v[0].y <= v[2].y) && (v[0].y <= v[3].y));
  int n, opp_idx=2; // v[opp_idx] is the vertex opposite v[0]
  bool done = false;
  while (!done)
    { // Perform bubble sort on the y coordinate
      done = true;
      for (n=1; n < 3; n++)
        if (v[n].y > v[n+1].y)
          { // Swap v[n] with v[n+1]
            done = false;
            if (n == opp_idx)
              opp_idx = n+1;
            else if ((n+1) == opp_idx)
              opp_idx = n;
            tmp = v[n]; v[n] = v[n+1]; v[n+1] = tmp;
          }
    }
  
  // Now find the sequence of the x coordinates
  int seq[4]={0,1,2,3};
  done = false;
  while (!done)
    { // Perform bubble sort on the x coordinate
      done = true;
      for (n=0; n < 3; n++)
        if (v[seq[n]].x > v[seq[n+1]].x)
          { 
            done = false;
            int t=seq[n]; seq[n] = seq[n+1]; seq[n+1] = t;
          }
    }
  
  // Now find the bit-fields and inner rectangle
  inner_rect.pos.y = v[1].y;
  inner_rect.size.y = v[2].y + 1 - v[1].y;
  inner_rect.pos.x = v[seq[1]].x;
  inner_rect.size.x = v[seq[2]].x + 1 - v[seq[1]].x;
  int iseq[4]={0,0,0,0}; // Reverse permutation
  for (n=0; n < 4; n++)
    iseq[seq[n]] = n;
  int A = iseq[0];
  int B = ((iseq[1]-iseq[0])&3) - 1;
  int C = ((iseq[2]-iseq[0])&3) - ((iseq[1]-iseq[0])&3); C=(C<0)?(C+2):(C-1);
  int D = (opp_idx==1)?3:(opp_idx-1);
  return (A<<6) + (B<<4) + (C<<3) + (D<<1);
}

/*****************************************************************************/
/*                             jx_regions::unlink                            */
/*****************************************************************************/

void jx_regions::unlink()
{ 
  jx_region_cluster *cluster = region_cluster;
  if (region_cluster != NULL)
    { 
      assert(cluster->level <= 0);
      if (prev_in_cluster == NULL)
        { 
          assert(this == cluster->regions);
          cluster->regions = next_in_cluster;
        }
      else
        prev_in_cluster->next_in_cluster = next_in_cluster;
      if (next_in_cluster != NULL)
        next_in_cluster->prev_in_cluster = prev_in_cluster;
      cluster->library->num_elts--;
    }
  region_cluster = NULL;
  next_in_cluster = prev_in_cluster = NULL;
  if ((cluster != NULL) && (cluster->regions == NULL) &&
      (cluster->level == 0))
    { // Note `level' < 0 if caller is we were called from `remove_cluster'
      jx_region_library *library = cluster->library;
      library->remove_cluster(cluster);
      if ((library->clusters == NULL) &&
          (library->representative_numlist != NULL))
        { // Delete the library
          assert(library->representative_numlist->region_library == library);
          library->representative_numlist->region_library = NULL;
          library->representative_numlist = NULL;
          delete library;
        }
    }
}


/* ========================================================================= */
/*                            jx_region_library                              */
/* ========================================================================= */

/*****************************************************************************/
/*                  jx_region_library::~jx_region_library                    */
/*****************************************************************************/

jx_region_library::~jx_region_library()
{
  assert(representative_numlist == NULL); // Must have been removed by caller
  jx_region_cluster *elt;
  while ((elt=clusters) != NULL)
    { 
      remove_cluster(elt);
      assert(clusters != elt);
    }
}

/*****************************************************************************/
/*                          jx_region_library::add                           */
/*****************************************************************************/

void jx_region_library::add(jx_regions *obj, bool temporary)
{ 
  kdu_dims region = obj->bounding_region.region;
  int dim = (region.size.x > region.size.y)?region.size.x:region.size.y;
  int log_size = 0;
  if (dim > 0)
    while (((dim-1) >> log_size) > 0)
      log_size++; // `dim' must be > 2^log_size
  jx_region_cluster *prev, *scan;
  for (prev=NULL, scan=clusters; scan != NULL; prev=scan, scan=scan->next)
    if (scan->log_size <= log_size)
      break; // Root clusters organized by decreasing size
  
  jx_region_cluster *cluster_root = scan;
  if ((cluster_root == NULL) || (cluster_root->log_size < log_size))
    { // Create a new cluster root
      jx_region_cluster *elt = new jx_region_cluster(this);
      elt->log_size = log_size;
      elt->level = 0; // Start off with everything at level 0
      elt->cover = region;
      elt->parent = NULL;
      elt->next = scan;
      if (prev == NULL)
        this->clusters = elt;
      else
        prev->next = elt;
      cluster_root = elt;
    }
  
  bool augmented_root_cluster = false;
  jx_region_cluster *cluster = cluster_root;
  while (cluster->level > 0)
    { 
      assert(cluster->level >= 2);
      int gap = 1 << (cluster->log_size+cluster->level-2);
      kdu_dims cover;
      cover.pos.x = region.pos.x & ~(gap-1);
      cover.pos.y = region.pos.y & ~(gap-1);
      cover.size.x = cover.size.y = gap + (1<<log_size) - 1;
      for (prev=NULL, scan=cluster->descendants; scan != NULL;
           prev=scan, scan=scan->next)
        { 
          assert(scan->level == (cluster->level-2));
          if ((scan->cover.pos.y > cover.pos.y) ||
              ((scan->cover.pos.y == cover.pos.y) &&
               (scan->cover.pos.x >= cover.pos.x)))
            break;
        }
      if ((scan == NULL) || (scan->cover.pos != cover.pos))
        { // Create a new descendant of `cluster'
          jx_region_cluster *elt = new jx_region_cluster(this);
          elt->log_size = log_size;
          elt->level = cluster->level-2;
          elt->cover = cover;
          elt->parent = cluster;
          elt->next = scan;
          if (prev == NULL)
            cluster->descendants = elt;
          else
            prev->next = elt;
          if (cluster == cluster_root)
            augmented_root_cluster = true;
          scan = elt;
        }
      cluster = scan;
    }
  
  // Hook `obj' into the `scan->regions' list and update global info
  if (temporary)
    { 
      jx_regions *copy = new jx_regions(obj->metanode);
      copy->num_regions = obj->num_regions;
      copy->max_regions = obj->max_regions;
      copy->bounding_region = obj->bounding_region;
      copy->regions = obj->regions;
      copy->max_width = obj->max_width;
      obj = copy;
    }
  obj->region_cluster = cluster;
  obj->prev_in_cluster = NULL;
  if ((obj->next_in_cluster = cluster->regions) != NULL)
    obj->next_in_cluster->prev_in_cluster = obj;
  cluster->regions = obj;
  if (cluster == cluster_root)
    augmented_root_cluster = true;
  cluster_root->cover.augment(region);
  this->num_elts++;
  
  // Finally, see if we need to split the root cluster
  if (augmented_root_cluster)
    check_split_root_cluster(cluster_root);
}

/*****************************************************************************/
/*              jx_region_library::check_split_root_cluster                  */
/*****************************************************************************/

void jx_region_library::check_split_root_cluster(jx_region_cluster *root)
{
  assert(root->library == this);
  int gap = 1 << (root->log_size + root->level);
  kdu_dims cover; // The cover region for any intermediate descendants
  cover.size.x = cover.size.y = gap + (1<<root->log_size) - 1;
  
  if (root->level > 0)
    { 
      if ((root->level+root->log_size) >= 28)
        return; // Don't create a levels with size > 2^28
      assert(!(root->level & 1)); // All our level values should be even
      int num_children = 0;
      jx_region_cluster *child, *next_child;
      for (child=root->descendants; child != NULL; child=child->next)
        num_children++;
      if (num_children <= 16)
        return;
      root->level += 2;
      child = root->descendants; root->descendants = NULL;
      for (; child != NULL; child=next_child)
        { // Put `child' into a new or existing intermediate cluster
          next_child = child->next;
          cover.pos.x = child->cover.pos.x & ~(gap-1);
          cover.pos.y = child->cover.pos.y & ~(gap-1);
          jx_region_cluster *scan, *prev;
          for (prev=NULL, scan=root->descendants; scan != NULL;
               prev=scan, scan=scan->next)
            { 
              if ((scan->cover.pos.y > cover.pos.y) ||
                  ((scan->cover.pos.y == cover.pos.y) &&
                   (scan->cover.pos.x >= cover.pos.x)))
                break;
            }
          if ((scan == NULL) || (scan->cover.pos != cover.pos))
            { // Create a new descendant of `root'
              jx_region_cluster *elt = new jx_region_cluster(this);
              elt->log_size = root->log_size;
              elt->level = root->level-2;
              elt->cover = cover;
              elt->parent = root;
              elt->next = scan;
              if (prev == NULL)
                root->descendants = elt;
              else
                prev->next = elt;
              scan = elt;
            }
          jx_region_cluster *cluster = scan;
          child->parent = cluster;
          for (prev=NULL, scan=cluster->descendants;
               scan != NULL; prev=scan, scan=scan->next)
            { // Find out where to place `child' within the descendants of
              // the intermediate `cluster'
              assert(scan->level == child->level);
              if ((scan->cover.pos.y > child->cover.pos.y) ||
                  ((scan->cover.pos.y == child->cover.pos.y) &&
                   (scan->cover.pos.x >= child->cover.pos.x)))
                break;
            }
          assert((scan == NULL) || (scan->cover.pos != child->cover.pos));
          child->next = scan;
          if (prev == NULL)
            cluster->descendants = child;
          else
            prev->next = child;
        }
      check_split_root_cluster(root); // Do it again, if necessary
    }
  else
    { // See if we need to split level 0 for the first time
      cover.pos.x = root->cover.pos.x & ~(gap-1);
      cover.pos.y = root->cover.pos.y & ~(gap-1);
      if (cover.contains(root->cover))
        return; // No point in splitting the root up -- won't help retrieval
      int num_children=0;
      jx_regions *child, *next_child;
      for (child=root->regions; child != NULL; child=child->next_in_cluster)
        num_children++;
      if (num_children <= 16)
        return; // No point in splitting the root up -- may get up to 16
                // new clusters
      root->level = 2;
      child = root->regions; root->regions = NULL;
      for (; child != NULL; child=next_child)
        { // Put `child' into a new or existing intermediate cluster
          next_child = child->next_in_cluster;
          cover.pos.x = child->bounding_region.region.pos.x & ~(gap-1);
          cover.pos.y = child->bounding_region.region.pos.y & ~(gap-1);
          jx_region_cluster *scan, *prev;
          for (prev=NULL, scan=root->descendants; scan != NULL;
               prev=scan, scan=scan->next)
            { 
              if ((scan->cover.pos.y > cover.pos.y) ||
                  ((scan->cover.pos.y == cover.pos.y) &&
                   (scan->cover.pos.x >= cover.pos.x)))
                break;
            }
          if ((scan == NULL) || (scan->cover.pos != cover.pos))
            { // Create a new descendant of `root'
              jx_region_cluster *elt = new jx_region_cluster(this);
              elt->log_size = root->log_size;
              elt->level = 0;
              elt->cover = cover;
              elt->parent = root;
              elt->next = scan;
              if (prev == NULL)
                root->descendants = elt;
              else
                prev->next = elt;
              scan = elt;
            }
          jx_region_cluster *cluster = scan;
          child->region_cluster = cluster;
          child->prev_in_cluster = NULL;
          if ((child->next_in_cluster = cluster->regions) != NULL)
            child->next_in_cluster->prev_in_cluster = child;
          cluster->regions = child;
        }
    }
}

/*****************************************************************************/
/*                    jx_region_library::remove_cluster                      */
/*****************************************************************************/

void jx_region_library::remove_cluster(jx_region_cluster *cluster)
{
  assert(cluster->library == this);
  if (cluster->level < 0)
    return; // Special flag indicating that removal is already in progress
  
  int level = cluster->level; // Save `level' value so we can use it below
  cluster->level = -1; // Avoid trying to remove ourselves recursively

  // Start by removing descendants, if there are any
  if (level == 0)
    { 
      while (cluster->regions != NULL)
        { 
          jx_regions *elt = cluster->regions;
          cluster->regions->unlink();
          assert(cluster->regions != elt);
          assert(elt->metanode != NULL);
          if (elt->metanode->regions != elt)
            { // Temporary `jx_regions' copy needs to be destroyed
              assert(elt->regions == elt->metanode->regions->regions);
              elt->regions = NULL; // Aliased array; be sure not to delete it
              delete elt;
            }
        }
    }
  else
    { 
      while (cluster->descendants != NULL)
        { 
          jx_region_cluster *elt = cluster->descendants;
          remove_cluster(elt);
          assert(cluster->descendants != elt);
        }
    }
  cluster->level = level; // Restore `level' value

  // Now unlink and delete the current cluster
  jx_region_cluster *parent = cluster->parent;
  jx_region_cluster *prev=NULL;
  jx_region_cluster *scan=(parent==NULL)?this->clusters:parent->descendants;
  for (; scan != NULL; prev=scan, scan=scan->next)
    if (scan == cluster)
      { 
        if (prev != NULL)
          prev->next = scan->next;
        else if (parent != NULL)
          parent->descendants = scan->next;
        else
          this->clusters = scan->next;
        break;
      }
  assert(scan != NULL); // Should have found ourselves on list

  // Now remove parent, if it exists
  if ((parent != NULL) && (parent->descendants == NULL))
    remove_cluster(parent);

  cluster->parent = cluster->next = NULL;
  delete cluster;
  
  if (this->clusters == NULL)
    assert(num_elts == 0);
}

/*****************************************************************************/
/*                      jx_region_library::enum_elts                         */
/*****************************************************************************/

jx_regions *jx_region_library::enum_elts(jx_regions *prev_elt)
{ 
  jx_region_cluster *cluster = NULL;
  if ((prev_elt != NULL) && ((cluster=prev_elt->region_cluster) != NULL))
    { 
      jx_regions *elt = prev_elt->next_in_cluster;
      if (elt != NULL)
        return elt;
      while ((cluster->next == NULL) && (cluster->parent != NULL))
        cluster = cluster->parent;
      cluster = cluster->next;
      if (cluster == NULL)
        return NULL; // We have scanned through everything
    }
  else
    cluster = this->clusters;
  while (cluster->level > 0)
    { 
      assert(cluster->descendants != NULL); // Should not have empty clusters
      cluster = cluster->descendants;
    }
  assert((cluster->level == 0) && (cluster->regions != NULL));
  return cluster->regions;
}

/*****************************************************************************/
/*                  jx_region_library::enumerate_matches                     */
/*****************************************************************************/

jx_regions *
  jx_region_library::enumerate_matches(jx_regions *prev_elt,
                                       kdu_dims region, int min_size)
{
  jx_region_cluster *cluster = this->clusters;
  bool advance_to_next_cluster = false;
  if ((prev_elt != NULL) && (prev_elt->region_cluster != NULL))
    { 
      cluster = prev_elt->region_cluster;
      assert(cluster->library == this);
      jx_regions *elt = prev_elt->next_in_cluster;
      for (; elt != NULL; elt=elt->next_in_cluster)
        if ((elt->max_width >= min_size) &&
            elt->bounding_region.region.intersects(region))
          return elt; // This one is a match
      advance_to_next_cluster = true;
    }
  while (cluster != NULL)
    { 
      if (advance_to_next_cluster)
        { 
          while ((cluster->next == NULL) && (cluster->parent != NULL))
            cluster = cluster->parent;
          cluster = cluster->next;                  
          advance_to_next_cluster = false;
        }
      else if ((cluster->parent == NULL) &&
               ((1 << cluster->log_size) < min_size))
        break; // Cluster roots organized from largest to smallest
      else if (!cluster->cover.intersects(region))
        advance_to_next_cluster = true;
      else if (cluster->level > 0)
        { // Descend and perform the test again
          assert(cluster->descendants!=NULL); // Should not have empty clusters
          cluster = cluster->descendants;
        }
      else
        { 
          assert(cluster->regions != NULL); // Should not have empty clusters
          jx_regions *elt = cluster->regions;
          for (; elt != NULL; elt=elt->next_in_cluster)
            if ((elt->max_width >= min_size) &&
                elt->bounding_region.region.intersects(region))
              return elt; // This one is a match
          advance_to_next_cluster = true;
        }
    }
  
  return NULL; // If we get here, we have scanned everything
}


/* ========================================================================= */
/*                               jx_crossref                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          jx_crossref::link_found                          */
/*****************************************************************************/

void jx_crossref::link_found()
{
  assert(metaloc != NULL);
  link = metaloc->target;
  if ((link != NULL) && (link_type == JPX_METANODE_LINK_NONE))
    { // Determine the link type
      if (owner->flags & JX_METANODE_FIRST_SUBBOX)
        link_type = JPX_GROUPING_LINK;
      else if (link->flags & JX_METANODE_FIRST_SUBBOX)
        { // Might be an alternate parent link; need to determine whether the
          // cross-reference referred to the asoc box itself or the first
          // sub-box.
          jx_metaloc_manager &metaloc_manager=owner->manager->metaloc_manager;
          kdu_long asoc_pos = metaloc->get_loc()-8;
          jx_metaloc *asoc_metaloc=metaloc_manager.get_locator(asoc_pos,false);
          if (asoc_metaloc == NULL)
            asoc_metaloc = metaloc_manager.get_locator(asoc_pos-=8,false);
          if ((asoc_metaloc != NULL) &&
              (asoc_metaloc->target==metaloc->target))
            link_type = JPX_ALTERNATE_PARENT_LINK;
          else
            link_type = JPX_ALTERNATE_CHILD_LINK;
        }
      else
        link_type = JPX_ALTERNATE_CHILD_LINK;
    }
  metaloc = NULL;
  if (owner->flags & JX_METANODE_UNRESOLVED_LINK)
    { // Otherwise, the cross-reference node has only just been encountered 
      owner->flags &= ~JX_METANODE_UNRESOLVED_LINK;
      owner->check_parsing_complete();
      owner->append_to_touched_list(true);
    }
  if (!(link->flags & JX_METANODE_WRITTEN))
    link->manager->note_unwritten_link_target(link);
  else if ((link->flags & JX_METANODE_PRESERVE) &&
           (link->preserve_state != NULL))
    this->fill_write_info(link->preserve_state);
}

/*****************************************************************************/
/*                            jx_crossref::unlink                            */
/*****************************************************************************/

void jx_crossref::unlink()
{
  jx_crossref *scan, *prev;
  if (link != NULL)
    { // We must be on a list headed by `link->linked_from'
      scan = link->linked_from;
    }
  else if (metaloc != NULL)
    { // We must be on a list headed by `metaloc->target->crossref'
      scan = metaloc->target->crossref;
      assert((scan->link == NULL) && (scan->metaloc == metaloc));
    }
  else
    return;
  for (prev=NULL; scan != NULL; prev=scan, scan=scan->next_link)
    if (scan == this)
      {
        if (prev == NULL)
          { // Change the head of the list
            if (link != NULL)
              link->linked_from = scan->next_link;
            else if (scan->next_link != NULL)
              metaloc->target = scan->next_link->owner;
            else
              metaloc->target = NULL;
          }
        else
          prev->next_link = scan->next_link;
        break;
      }
  assert(scan != NULL);
  link = NULL;
  metaloc = NULL;
  link_type = JPX_METANODE_LINK_NONE;
  next_link = NULL;
}

/*****************************************************************************/
/*                     jx_crossref::fill_write_info                          */
/*****************************************************************************/

void jx_crossref::fill_write_info(jx_metapres *pres)
{
  if ((pres->asoc_contents_len < 0) ||
      (link_type != JPX_ALTERNATE_CHILD_LINK))
    { // Link to the contents of the box (as opposed to an asoc container)
      box_type = link->box_type;
      frag_list.set_link_region(pres->contents_pos,pres->contents_len);
    }
  else
    { // Link to the contents of the asoc container
      box_type = jp2_association_4cc;
      frag_list.set_link_region(pres->asoc_contents_pos,
                                pres->asoc_contents_len);
    }
}


/* ========================================================================= */
/*                               jx_metaparse                                */
/* ========================================================================= */

/*****************************************************************************/
/*                     jx_metaparse::add_incomplete_child                    */
/*****************************************************************************/

void jx_metaparse::add_incomplete_child(jx_metanode *child)
{
  assert(child->parse_state != NULL);
  if ((child->parse_state->incomplete_next = incomplete_descendants) != NULL)
    incomplete_descendants->parse_state->incomplete_prev = child;
  incomplete_descendants = child;
}

/*****************************************************************************/
/*                   jx_metaparse::remove_incomplete_child                   */
/*****************************************************************************/

void jx_metaparse::remove_incomplete_child(jx_metanode *child)
{
  jx_metaparse *ps = child->parse_state;
  assert(ps != NULL);
  jx_incomplete_metanode_walker *walker = ps->incomplete_walker;
  if (walker != NULL)
    { 
      assert(walker->node == child);
      walker->node = NULL;
      ps->incomplete_walker = NULL;
      if (ps->incomplete_next != NULL)
        walker->attach(ps->incomplete_next);
    }
  if (ps->incomplete_prev == NULL)
    { 
      assert(child == incomplete_descendants);
      incomplete_descendants = ps->incomplete_next;
    }
  else
    { 
      assert(ps->incomplete_prev->parse_state->incomplete_next == child);
      ps->incomplete_prev->parse_state->incomplete_next = ps->incomplete_next;
    }
  if (ps->incomplete_next != NULL)
    { 
      assert(ps->incomplete_next->parse_state->incomplete_prev == child);
      ps->incomplete_next->parse_state->incomplete_prev = ps->incomplete_prev;
    }
  
  ps->incomplete_prev = NULL;
  ps->incomplete_next = NULL;
}


/* ========================================================================= */
/*                           jx_metagroup_writer                             */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_metagroup_writer::init                          */
/*****************************************************************************/

void jx_metagroup_writer::init(int num_boxes_to_expect, bool use_free_asocs,
                               bool is_top_level)
{
  // Start by closing up all existing groups
  jx_metagroup_list *elt;
  while ((elt=active) != NULL)
    { 
      active = elt->parent;
      elt->grp_box.close();
      delete elt;
    }
  
  // Now initialize the parameters
  group_threshold = (is_top_level)?2:8;
  if (num_boxes_to_expect < group_threshold)
    group_size = 0; // No grouping
  else
    group_size = num_boxes_to_expect;
  box_idx = 0;
  if (use_free_asocs)
    grp_box_type = jp2_association_4cc;
  else
    grp_box_type = jp2_group_4cc;
}

/*****************************************************************************/
/*                       jx_metagroup_writer::advance                        */
/*****************************************************************************/

void jx_metagroup_writer::advance()
{
  box_idx++;
  jx_metagroup_list *elt;
  while ((active != NULL) && (box_idx >= active->lim_box_in_grp))
    { // Close `active' group
      elt = active;
      active = elt->parent;
      elt->grp_box.close();
      delete elt;
    }
}

/*****************************************************************************/
/*                     jx_metagroup_writer::get_container                    */
/*****************************************************************************/

jp2_output_box *jx_metagroup_writer::get_container(jp2_output_box *super_box,
                                                   jx_meta_manager *manager,
                                                   kdu_long &file_pos)
{ 
  if ((active == NULL) &&
      (box_idx < (group_size-1)) && // i.e., > 1 box left to write
      (group_size >= group_threshold)) // Sub-division considered justified
    { // We need to add a grouping box
      active = new jx_metagroup_list;
      active->parent = NULL;
      active->level = 0;
      active->first_box_in_grp = box_idx;
      if (group_threshold < 8)
        active->lim_box_in_grp = group_size; // Don't fan out at top
      else
        active->lim_box_in_grp = box_idx+((group_size+2)>>2); // Fan out into 4
      if (active->lim_box_in_grp > group_size)
        active->lim_box_in_grp = group_size;
      assert(active->lim_box_in_grp >
             active->first_box_in_grp); // Not a singleton group
      if (super_box != NULL)
        active->grp_box.open(super_box,grp_box_type);
      else
        file_pos =
          manager->meta_target->open_top_box(active->grp_box,grp_box_type,
                                             manager->simulation_phase);
      file_pos += active->grp_box.use_long_header();
            // So we can be sure of the box header length.
      if (grp_box_type == jp2_association_4cc)
        { // Write the initial free box, of size 0
          jp2_output_box free_box;
          free_box.open(&active->grp_box,jp2_free_4cc);
          free_box.close();
          file_pos += free_box.get_box_length();
        }
    }
  int active_size;
  while ((active != NULL) &&
         (box_idx < (active->lim_box_in_grp-1)) && // i.e., > 1 box left
         ((active_size = active->lim_box_in_grp-active->first_box_in_grp) > 8))
    { // We need to add a new descendant to `active'
      jx_metagroup_list *elt = new jx_metagroup_list;
      elt->parent = active;
      elt->level = active->level + 1;
      elt->first_box_in_grp = box_idx;
      elt->lim_box_in_grp = box_idx + (active_size/4); // Divide `active' by 4
      if (elt->lim_box_in_grp > active->lim_box_in_grp)
        elt->lim_box_in_grp = active->lim_box_in_grp;
      assert(elt->lim_box_in_grp > elt->first_box_in_grp); // Not a singleton
      active = elt;
      active->grp_box.open(&(active->parent->grp_box),grp_box_type);
      file_pos += active->grp_box.use_long_header();
            // So we can be sure of the box header length.
      if (grp_box_type == jp2_association_4cc)
        { // Write the initial free box, of size 0
          jp2_output_box free_box;
          free_box.open(&active->grp_box,jp2_free_4cc);
          free_box.close();
          file_pos += free_box.get_box_length();
        }
    }
                        
  if (active != NULL)
    return &(active->grp_box);
  else
    return super_box; // If `super_box' is NULL, caller will open top level box
}


/* ========================================================================= */
/*                               jx_metanode                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_metanode::~jx_metanode                          */
/*****************************************************************************/

jx_metanode::~jx_metanode()
{
  assert((flags & JX_METANODE_DELETED) &&
         (head == NULL) && (tail == NULL) && (parent == NULL) &&
         (linked_from == NULL) &&
         (prev_sibling == NULL)); // We only use `next_sibling' to link the
                                  // `deleted_nodes' list.
  switch (rep_id) {
    case JX_REF_NODE: if (ref != NULL) delete ref; break;
    case JX_NUMLIST_NODE: if (numlist != NULL) delete numlist; break;
    case JX_ROI_NODE: if (regions != NULL) delete regions; break;
    case JX_LABEL_NODE: if (label != NULL) delete[] label; break;
    case JX_CROSSREF_NODE: if (crossref != NULL) delete crossref; break;
    case JX_NULL_NODE: break;
    default: assert(0); break;
    };

  if (flags & JX_METANODE_EXISTING)
    { 
      if (parse_state != NULL)
        { delete parse_state; parse_state = NULL; }
    }
  else if ((flags & JX_METANODE_PRESERVE) && (flags & JX_METANODE_WRITTEN))
    { 
      if (preserve_state != NULL)
        { delete preserve_state; preserve_state = NULL; }
    }
  else if (write_state != NULL)
    { delete write_state; write_state = NULL; }
}

/*****************************************************************************/
/*                    jx_metanode::remove_empty_shell                        */
/*****************************************************************************/

void jx_metanode::remove_empty_shell()
{
  assert((parse_state == NULL) && (rep_id == 0));
  if (parent == NULL)
    return; // Just in case we accidentally invoked this on the root node
  
  unlink_parent(true);
  assert((linked_from == NULL) && (parent == NULL) &&
         (next_sibling == NULL) && (prev_sibling == NULL) &&
         (head == NULL) && (tail == NULL) && (prev_touched == NULL) &&
         (next_touched == NULL) && (this != manager->touched_head));
  flags |= JX_METANODE_DELETED;
  delete this; // We can safely do this only because we have been careful
               // not to allow any references to nodes with `rep_id'=0
               // persist.  The most subtle of these references are those
               // from `jx_metaloc' and `jx_crossref' objects.  See
               // `finish_reading' to see how these are removed, in cases
               // where the references were created mistakenly.
}

/*****************************************************************************/
/*                       jx_metanode::safe_delete                           */
/*****************************************************************************/

void
  jx_metanode::safe_delete()
{
  if (flags & JX_METANODE_DELETED)
    return; // Already deleted
  flags |= JX_METANODE_DELETED; // Avoid any chance of recursive entry here
  
  // Now finish deleting ourself  
  unlink_parent();
  assert((parent == NULL) && (next_sibling == NULL) && (prev_sibling == NULL));
  if ((rep_id == JX_NUMLIST_NODE) && (numlist != NULL))
    numlist->unlink();
  else if ((rep_id == JX_ROI_NODE) && (regions != NULL))
    regions->unlink();
  else if ((rep_id == JX_CROSSREF_NODE) && (crossref != NULL))
    {
      delete crossref; // Automatically unlinks it
      crossref = NULL;
    }
  
  // Now apply `safe_delete' to all nodes which link to us
  while (linked_from != NULL)
    linked_from->owner->safe_delete();
  
  append_to_touched_list(false); // Better to touch node before descendants
  
  // Recursively apply `safe_delete' to all descendants
  while (head != NULL)
    head->safe_delete();
  assert(tail == NULL);
  if (flags & JX_METANODE_EXISTING)
    { 
      if (parse_state != NULL)
        { delete parse_state; parse_state = NULL; }
    }
  else if ((flags & JX_METANODE_PRESERVE) && (flags & JX_METANODE_WRITTEN))
    { 
      if (preserve_state != NULL)
        { delete preserve_state; preserve_state = NULL; }
    }
  else if (write_state != NULL)
    { delete write_state; write_state = NULL; }
  next_sibling = manager->deleted_nodes;
  manager->deleted_nodes = this;
}

/*****************************************************************************/
/*                       jx_metanode::unlink_parent                          */
/*****************************************************************************/

void
  jx_metanode::unlink_parent(bool from_empty_shell)
{
  if (parent != NULL)
    { // Unlink from sibling list
      if (parent->parent == NULL)
        { // Top-level node may be referenced from a JPX container or the
          // `manager'
          manager->note_metanode_unlinked(this);
          if (is_top_container_numlist())
            numlist->container->note_metanode_unlinked(this);
        }
      
      if (prev_sibling == NULL)
        {
          assert(this == parent->head);
          parent->head = next_sibling;
        }
      else
        prev_sibling->next_sibling = next_sibling;
      if (next_sibling == NULL)
        {
          assert(this == parent->tail);
          parent->tail = prev_sibling;
        }
      else
        next_sibling->prev_sibling = prev_sibling;
      if ((flags & JX_METANODE_EXISTING) && (parse_state != NULL))
        { 
          assert(!(flags & JX_METANODE_IS_COMPLETE));
          bool check_parent_complete = false;
          if (!(parent->flags & JX_METANODE_DELETED))
            { 
              assert((parent->flags & JX_METANODE_EXISTING) &&
                     (parent->parse_state != NULL) &&
                     (parent->parse_state->incomplete_descendants != NULL));
              parent->parse_state->remove_incomplete_child(this);
              if (parse_state->is_generator)
                { 
                  assert(parent->parse_state->num_possible_generators > 0);
                  parent->parse_state->num_possible_generators--;
                }
              if (parent->parse_state->incomplete_descendants == NULL)
                check_parent_complete = true;
            }
          delete parse_state;
          parse_state = NULL;
          if (check_parent_complete)
            parent->check_parsing_complete();
        }
      parent->check_roi_child_flags();
      jx_metanode *old_parent = parent;
      parent = next_sibling = prev_sibling = NULL;
      if (!from_empty_shell)
        { 
          old_parent->flags |= JX_METANODE_CHILD_REMOVED;
          old_parent->append_to_touched_list(false);
        }
    }
  else
    { 
      assert((next_sibling == NULL) && (prev_sibling == NULL));  
      if (this == manager->tree)
        manager->tree = NULL;
    }
}

/*****************************************************************************/
/*                 jx_metanode::delete_useless_numlists                      */
/*****************************************************************************/

void
  jx_metanode::delete_useless_numlists()
{
  jx_metanode *p_scan, *scan;
  for (scan=this; scan != NULL; scan=p_scan)
    { 
      p_scan = scan->parent;
      if ((scan->rep_id != JX_NUMLIST_NODE) ||
          (scan->head != NULL) ||
          ((scan->flags & JX_METANODE_EXISTING) &&
           !(scan->flags & JX_METANODE_DESCENDANTS_KNOWN)) ||
          (scan->linked_from != NULL) || (p_scan == NULL))
        break; // We are not a number list that is suitable for auto-delete
      if ((p_scan->parent != NULL) && (p_scan->rep_id != JX_NUMLIST_NODE))
        break; // Our parent is not the root node and not a number list so
               // we are providing some specialization of the parent or
               // serving as a potentially useful link to a reference to an
               // image entity that can help guide an interactive user.
      scan->safe_delete();
    }
}

/*****************************************************************************/
/*                  jx_metanode::append_to_touched_list                      */
/*****************************************************************************/

void
  jx_metanode::append_to_touched_list(bool recursive)
{
  if ((parent != NULL) && !is_externally_visible())
    return;
  if ((box_type != 0) && (flags & JX_METANODE_BOX_COMPLETE))
    {
      // Unlink from touched list if required
      if (manager->touched_head == this)
        {
          assert(prev_touched == NULL);
          manager->touched_head = next_touched;
        }
      else if (prev_touched != NULL)
        prev_touched->next_touched = next_touched;
      if (manager->touched_tail == this)
        {
          assert(next_touched == NULL);
          manager->touched_tail = prev_touched;
        }
      else if (next_touched != NULL)
        next_touched->prev_touched = prev_touched;
  
      // Now link onto tail of touched list
      next_touched = NULL;
      if ((prev_touched = manager->touched_tail) == NULL)
        {
          assert(manager->touched_head == NULL);
          manager->touched_head = manager->touched_tail = this;
        }
      else
        manager->touched_tail = manager->touched_tail->next_touched = this;
    }

  if ((parent != NULL) && (parent->flags & (JX_METANODE_ANCESTOR_CHANGED |
                                            JX_METANODE_CONTENTS_CHANGED)))
    flags |= JX_METANODE_ANCESTOR_CHANGED;
  
  jx_metanode *scan;
  for (scan=head; (scan != NULL) && recursive; scan=scan->next_sibling)
    scan->append_to_touched_list(true);
}

/*****************************************************************************/
/*                   jx_metanode::place_on_touched_list                      */
/*****************************************************************************/

void jx_metanode::place_on_touched_list()
{
  if ((box_type != 0) && (flags & JX_METANODE_BOX_COMPLETE))
    {
      // Check if we are already on the touched list
      if ((manager->touched_head == this) || (prev_touched != NULL))
        return;
      assert((prev_touched == NULL) && (next_touched == NULL) &&
             (manager->touched_tail != this));
      
      // Link onto tail of touched list
      next_touched = NULL;
      if ((prev_touched = manager->touched_tail) == NULL)
        {
          assert(manager->touched_head == NULL);
          manager->touched_head = manager->touched_tail = this;
        }
      else
        manager->touched_tail = manager->touched_tail->next_touched = this;
    }
}

/*****************************************************************************/
/*                       jx_metanode::insert_child                           */
/*****************************************************************************/

void
  jx_metanode::insert_child(jx_metanode *child, jx_metanode *insert_after,
                            jp2_locator loc)
{
  if (loc.is_null())
    child->sequence_index = ++(manager->last_sequence_index);
  else
    { 
      child->sequence_index = loc.get_file_pos();
      if (child->sequence_index > manager->last_sequence_index)
        manager->last_sequence_index = child->sequence_index;
      child->flags |= JX_METANODE_EXISTING;
      assert(child->parse_state == NULL);
      if (!(child->flags & JX_METANODE_IS_COMPLETE))
        child->parse_state = new jx_metaparse;
    }
  child->prev_sibling = insert_after;
  if (insert_after == NULL)
    { 
      child->next_sibling = head;
      head = child;
    }
  else
    { 
      child->next_sibling = insert_after->next_sibling;
      insert_after->next_sibling = child;
    }
  if (child->next_sibling == NULL)
    tail = child;
  else
    child->next_sibling->prev_sibling = child;
  child->parent = this;
  if (child->flags & JX_METANODE_EXISTING)
    { 
      assert((flags & JX_METANODE_EXISTING) && (parse_state != NULL));
      if (!(child->flags & JX_METANODE_IS_COMPLETE))
        parse_state->add_incomplete_child(child);
    }
  if (child->box_type == jp2_roi_description_4cc)
    flags |= JX_METANODE_HAS_ROI_CHILD;
  else if ((child->box_type != 0) && (child->box_type != jp2_group_4cc) &&
           (child->box_type != jp2_free_4cc))
    flags |= JX_METANODE_HAS_NON_ROI_CHILD;
  if (manager->target != NULL)
    { // We may need to adjust write-state pointers in `manager'
      assert(!(child->flags & JX_METANODE_WRITTEN));
      jx_metanode *unwritten_link_tgt = child->find_link_target();
      if (unwritten_link_tgt != NULL)
        manager->note_unwritten_link_target(unwritten_link_tgt);
      if (this->parent == NULL)
        { // Top level node; may need to adjust `manager->first_unwritten'
          assert((child->next_sibling == NULL) ||
                 !(child->next_sibling->flags & JX_METANODE_WRITTEN));
          // The caller should have made sure we are not moving around a node
          // that has already been written.  Also, nodes are always inserted
          // at the end of the parents' sibling list, except where they are
          // inserted at the end of an unwritten container's embedded metanodes
          // list or immediately before an unwritten container's embedded
          // metanodes list.  In all of these cases, either `child'
          // immediately precedes `manager->first_unwritten', or else it comes
          // later.
          if (manager->first_unwritten == NULL)
            { 
              manager->first_unwritten = child;
              manager->tree->flags &= ~JX_METANODE_WRITTEN;
              assert(manager->tree->write_state == NULL);
            }
          else if (child->next_sibling == manager->first_unwritten)
            manager->first_unwritten = child;
        }
    }
}

/*****************************************************************************/
/*                   jx_metanode::read_and_insert_child                      */
/*****************************************************************************/

void jx_metanode::read_and_insert_child(jp2_input_box &box,
                                        int databin_nesting_level)
{
  jx_metanode *new_node = new jx_metanode(manager);
  jp2_locator loc = box.get_locator();
  insert_child(new_node,tail,loc);
  new_node->donate_input_box(box,databin_nesting_level);
  if (new_node->finish_reading() && (new_node->rep_id == 0) &&
      (new_node->parse_state == NULL))
    new_node->remove_empty_shell(); // Deletes `new_node' 
}

/*****************************************************************************/
/*                   jx_metanode::check_roi_child_flags                      */
/*****************************************************************************/

void jx_metanode::check_roi_child_flags()
{
  flags &= ~(JX_METANODE_HAS_NON_ROI_CHILD | JX_METANODE_HAS_ROI_CHILD);
  for (jx_metanode *scan=head; scan != NULL; scan=scan->next_sibling)
    if (scan->box_type == jp2_roi_description_4cc)
      flags |= JX_METANODE_HAS_ROI_CHILD;
    else if ((scan->box_type != 0) && (scan->box_type != jp2_group_4cc) &&
             (scan->box_type != jp2_free_4cc))
      flags |= JX_METANODE_HAS_NON_ROI_CHILD;
}

/*****************************************************************************/
/*                jx_metanode::check_container_compatibility                 */
/*****************************************************************************/

bool jx_metanode::check_container_compatibility(jx_container_base *container)
{
  if (container == NULL)
    return true; // Always compatible
  if ((rep_id == JX_NUMLIST_NODE) && (numlist != NULL))
    { 
      if (container == numlist->container)
        return true; // No change in container embedding for this node or any
                     // of its descendants
      int n, idx;
      if (numlist->num_codestreams > 0)
        { 
          int top_indices = container->get_num_top_codestreams();
          int first_base = container->get_first_base_codestream();
          int last_idx = container->get_last_codestream();
          for (n=0; n < numlist->num_codestreams; n++)
            if (((idx = numlist->codestream_indices[n]) >= top_indices) &&
                ((idx < first_base) || (idx > last_idx)))
              return false;
        }
      if (numlist->num_compositing_layers > 0)
        { 
          int top_indices = container->get_num_top_layers();
          int first_base = container->get_first_base_layer();
          int last_idx = container->get_last_layer();
          for (n=0; n < numlist->num_compositing_layers; n++)
            if (((idx = numlist->layer_indices[n]) >= top_indices) &&
                ((idx < first_base) || (idx > last_idx)))
              return false;          
        }
    }
  for (jx_metanode *scan=head; scan != NULL; scan=scan->next_sibling)
    if (!scan->check_container_compatibility(container))
      return false;
  return true;
}

/*****************************************************************************/
/*                      jx_metanode::change_container                        */
/*****************************************************************************/

void
  jx_metanode::change_container(jx_container_base *container)
{
  if ((rep_id == JX_NUMLIST_NODE) && (numlist != NULL))
    { 
      if (container == numlist->container)
        return; // No change in container embedding for this node or any
                // of its descendants
      numlist->unlink();
      numlist->container = container;
      int num_indices, tbuf[1], *indices=NULL;
      try {
        if ((indices=numlist->extract_codestreams(num_indices,tbuf)) != NULL)
          { 
            for (int n=0; n < num_indices; n++)
              numlist->add_codestream(indices[n],false);
            if (indices != tbuf)
              { delete[] indices; indices = NULL; }
          }
        if ((indices=numlist->extract_layers(num_indices,tbuf)) != NULL)
          { 
            for (int n=0; n < num_indices; n++)
              numlist->add_compositing_layer(indices[n],false);
            if (indices != tbuf)
              { delete[] indices; indices = NULL; }
          }
      } catch (...) {
        assert(0); // Should have called `check_container_compatibility' first
        if ((indices != NULL) && (indices != tbuf))
          { delete[] indices; indices = NULL; }
        throw;
      }
      manager->link_to_libraries(this);
      if (container != NULL)
        { 
          this->flags |= JX_METANODE_CONTENTS_CHANGED;
          this->append_to_touched_list(false);
        }
    }
  for (jx_metanode*scan=head; scan != NULL; scan=scan->next_sibling)
    scan->change_container(container);
}

/*****************************************************************************/
/*               jx_metanode::move_old_top_container_numlist                 */
/*****************************************************************************/

void
  jx_metanode::move_old_top_container_numlist(jx_container_base *container)
{
  if ((flags & (JX_METANODE_EXISTING | JX_METANODE_WRITTEN)) ||
      (parent == NULL) || (parent->parent != NULL) ||
      (container == NULL) || container->parsed || container->written)
    return; // Not top level
  if (this == container->first_metanode)
    { // Easily moved out of the range delimited by first/last_metanode
      if (this == container->last_metanode)
        container->first_metanode = container->last_metanode = NULL;
      else
        container->first_metanode = next_sibling;
    }
  else if (this == container->last_metanode)
    { // Also easily moved out of the range delimited by first/last_metanode
      container->last_metanode = prev_sibling;
    }
  else if (container->last_metanode != NULL)
    { // Need to move the node immediately after `container->last_metanode'
      jx_metanode *saved_parent = this->parent;
      this->unlink_parent();
      saved_parent->insert_child(this,container->last_metanode);
    }
}

/*****************************************************************************/
/*                       jx_metanode::find_container                         */
/*****************************************************************************/

jx_container_base *jx_metanode::find_container()
{
  for (jx_metanode *scan=this; scan != NULL; scan=scan->parent)
    if ((scan->rep_id == JX_NUMLIST_NODE) && (scan->numlist != NULL))
      return scan->numlist->container;
  return NULL;
}

/*****************************************************************************/
/*                     jx_metanode::find_link_target                         */
/*****************************************************************************/

jx_metanode *jx_metanode::find_link_target()
{
  if ((linked_from != NULL) || (flags & JX_METANODE_PRESERVE))
    return this;
  jx_metanode *result=NULL, *scan;
  for (scan=head; scan != NULL; scan=scan->next_sibling)
    if ((result = scan->find_link_target()) != NULL)
      break;
  return result;
}
  
/*****************************************************************************/
/*                         jx_metanode::add_numlist                          */
/*****************************************************************************/

jx_metanode *
  jx_metanode::add_numlist(int num_codestreams, const int *codestream_indices,
                           int num_layers, const int *layer_indices,
                           bool applies_to_rendered_result,
                           jx_container_base *container, jp2_locator loc,
                           bool no_touch)
{
  bool adding_container_metanode = false;
  jx_metanode *insert_after = tail;
  if ((container != NULL) && (parent == NULL) && !container->parsed)
    { 
      if (container->written)
        { KDU_ERROR(e,0x016081209); e <<
          KDU_TXT("Attempting to embed a new number list within a JPX "
                  "container that has already been written to its output "
                  "file.");
        }
      adding_container_metanode = true;
      if (container->last_metanode != NULL)
        insert_after = container->last_metanode;
      else
        { // Need to find a good place to insert this node
          assert(container->first_metanode == NULL);
          jx_container_base *scan = container->next;
          for (; scan != NULL; scan=scan->next)
            if (scan->first_metanode != NULL)
              { 
                insert_after = scan->first_metanode->prev_sibling;
                break;
              }
        }
    }
  jx_metanode *node = new jx_metanode(manager);
  node->box_type = jp2_number_list_4cc;
  node->flags |= JX_METANODE_BOX_COMPLETE;
  insert_child(node,insert_after,loc);
       // Note: important to set `box_type' before above call
  node->rep_id = JX_NUMLIST_NODE;
  node->numlist = new jx_numlist(node,container);
  jx_numlist *nl = node->numlist;
  int n;
  for (n=0; n < num_codestreams; n++)
    nl->add_codestream(codestream_indices[n],false);
  for (n=0; n < num_layers; n++)
    nl->add_compositing_layer(layer_indices[n],false);
  nl->rendered_result = applies_to_rendered_result;
  node->manager->link_to_libraries(node);
  if (!no_touch)
    node->append_to_touched_list(false);
  if (adding_container_metanode)
    { 
      if (container->first_metanode == NULL)
        container->first_metanode = node;
      container->last_metanode = node;
    }
  return node;
}

/*****************************************************************************/
/*                 jx_metanode::count_numlist_descendants                    */
/*****************************************************************************/

bool jx_metanode::count_numlist_descendants(int &count)
{
  if (!(flags & JX_METANODE_BOX_COMPLETE))
    return false;
  if (rep_id == JX_NUMLIST_NODE)
    count++;
  bool result = ((flags & JX_METANODE_DESCENDANTS_KNOWN) != 0);
  for (jx_metanode *scan=head; scan != NULL; scan=scan->next_sibling)
    if (!scan->count_numlist_descendants(count))
      result = false;
  return result;
}

/*****************************************************************************/
/*                   jx_metanode::check_parsing_complete                     */
/*****************************************************************************/

void
  jx_metanode::check_parsing_complete()
{
  if (flags & JX_METANODE_IS_COMPLETE)
    { 
      assert(parse_state == NULL);
      return;
    }

  assert((flags & JX_METANODE_BOX_COMPLETE) &&
         (flags & JX_METANODE_EXISTING) && (parse_state != NULL));
    // We don't parse any descendants until primary box is complete
  if (!(flags & JX_METANODE_DESCENDANTS_KNOWN))
    { 
      if ((parse_state->num_possible_generators > 0) ||
          !(flags & JX_METANODE_CONTAINER_KNOWN))
        return; // Don't yet know enough to assess completion of descendants
      flags |= JX_METANODE_DESCENDANTS_KNOWN;
      if ((rep_id != 0) && !parse_state->is_generator)
        this->place_on_touched_list();
    }
  assert(parse_state->num_possible_generators == 0);
  if (flags & JX_METANODE_UNRESOLVED_LINK)
    return; // Not ready to mark as complete yet
  jx_metanode *scan = this;
  while ((scan != NULL) &&
         (!(scan->flags & (JX_METANODE_IS_COMPLETE |
                           JX_METANODE_UNRESOLVED_LINK))) &&
         (scan->flags & JX_METANODE_DESCENDANTS_KNOWN) &&
         (scan->parse_state->incomplete_descendants == NULL))
    { 
      scan->flags |= JX_METANODE_IS_COMPLETE;
      if (scan->parse_state->is_generator)
        { 
          assert((scan->parent != NULL) &&
                 (scan->parent->parse_state->num_possible_generators > 0));
          scan->parent->parse_state->num_possible_generators--;
        }
      jx_metanode *sp = scan->parent;
      if (sp != NULL)
        {
          sp->parse_state->remove_incomplete_child(scan);
          if ((sp->parse_state->incomplete_descendants == NULL) &&
              (sp->parse_state->num_possible_generators == 0) &&
              (sp->flags & JX_METANODE_CONTAINER_KNOWN))
            { 
              sp->flags |= JX_METANODE_DESCENDANTS_KNOWN;
              if (!sp->parse_state->is_generator)
                sp->place_on_touched_list();
            }
        }
      delete scan->parse_state;
      scan->parse_state = NULL;
      scan = scan->parent;
    }
}

/*****************************************************************************/
/*                      jx_metanode::donate_input_box                        */
/*****************************************************************************/

void
  jx_metanode::donate_input_box(jp2_input_box &src, int databin_nesting_level)
{
  assert((parse_state != NULL) && (parse_state->read == NULL));
  assert(parent->parse_state != NULL);
  assert(!(flags & (JX_METANODE_BOX_COMPLETE | JX_METANODE_CONTAINER_KNOWN |
                    JX_METANODE_DESCENDANTS_KNOWN)));
  assert(flags & JX_METANODE_EXISTING); // Verifies `insert_child' was called
                                        // with a non-empty `loc' argument
  if (src.get_remaining_bytes() >= 0) // Not rubber length
    parse_state->metanode_span = src.get_box_bytes();
  box_type = src.get_box_type();
  if ((box_type == jp2_association_4cc) || (box_type == jp2_group_4cc))
    flags |= JX_METANODE_FIRST_SUBBOX;
  
  // Add a locator for the new box.
  if ((box_type != jp2_cross_reference_4cc) && (box_type != jp2_group_4cc))
    { // Do not allow links to cross-references or groups
      kdu_long file_pos = src.get_locator().get_file_pos();
      file_pos += src.get_box_header_length();
      jx_metaloc *metaloc=manager->metaloc_manager.get_locator(file_pos,true);
      if ((metaloc->target != NULL) &&
          (metaloc->target->rep_id == JX_CROSSREF_NODE) &&
          (metaloc->target->crossref->metaloc == metaloc))
        linked_from = metaloc->target->crossref;
      metaloc->target = this;
      for (jx_crossref *scan=linked_from; scan != NULL; scan=scan->next_link)
        scan->link_found();
    }

  parse_state->read = new jx_metaread;
  
  if ((box_type == jp2_group_4cc) && (src.get_remaining_bytes() == 0))
    box_type = jp2_free_4cc; // We cannot treat this empty grouping box in the
      // same way as other grouping boxes, because it may propagate an entirely
      // empty box all the way up to its parent (if it lies within its own
      // grouping box) which would eventually cause the descendant count to
      // reduce from what it may have been when the application last checked.
      // This is unacceptable.  Instead, we make empty grouping boxes look like
      // free boxes to the application -- there should be no harm in this.  In
      // any case, it is blatantly stupid for a content generator to create an
      // empty grouping box -- indeed it might even be illegal.
  if ((box_type == jp2_association_4cc) || (box_type == jp2_group_4cc))
    { 
      jp2_locator loc = src.get_locator();
      parse_state->asoc_databin_id = loc.get_databin_id();
      parse_state->asoc_nesting_level = databin_nesting_level;
      loc = src.get_contents_locator();
      parse_state->box_databin_id = loc.get_databin_id();
      if (parse_state->box_databin_id == parse_state->asoc_databin_id)
        parse_state->box_nesting_level = databin_nesting_level+1;
      else
        parse_state->box_nesting_level = 0;
      parse_state->read->asoc.transplant(src);
      if (box_type == jp2_association_4cc)
        { 
          box_type = 0; // Because we don't yet know the node's box-type
          if (manager->flatten_free_asocs)
            { // Mark the node as a generator for now, until we know box-type
              parse_state->is_generator = true;
              parent->parse_state->num_possible_generators++;
            }
        }
      else
        { 
          flags |= JX_METANODE_BOX_COMPLETE;
          parse_state->is_generator = true;
          parent->parse_state->num_possible_generators++;
        }
    }
  else
    {
      jp2_locator loc = src.get_locator();
      parse_state->asoc_databin_id = -1;
      parse_state->box_databin_id = loc.get_databin_id();
      parse_state->box_nesting_level = databin_nesting_level;
      parse_state->read->box.transplant(src);
      flags |= JX_METANODE_CONTAINER_KNOWN;
    }
  
  // Note: we used to call `finish_reading' directly from here, but now we
  // have the caller invoke that function.  This helps to raise awareness of
  // the need to also invoke `remove_empty_shell' if the new node
  // becomes an empty shell after the call to `finish_reading'.  That function
  // is not safe to invoke from within the object itself since it may delete
  // the object.
}

/*****************************************************************************/
/*                       jx_metanode::finish_reading                         */
/*****************************************************************************/

bool
  jx_metanode::finish_reading(kdu_long link_pos)
{
  if (!(flags & JX_METANODE_EXISTING))
    return false;

  if (parse_state == NULL)
    {
      assert(flags & JX_METANODE_IS_COMPLETE);
      return true;
    }
  
  if (parent == NULL)
    { // We are at the root.
      jx_source *source = manager->source;
      while (!source->is_top_level_complete())
        if (!source->parse_next_top_level_box())
          break;
      return false;
    }
  
  jx_metaread *read_state = parse_state->read;
  if (read_state == NULL)
    { 
      assert((flags & JX_METANODE_BOX_COMPLETE) &&
             (flags & JX_METANODE_CONTAINER_KNOWN));
      return false;
    }
  
  if (!(flags & JX_METANODE_BOX_COMPLETE))
    { 
      jx_crossref *cref;
      int box_len = 0;
      jp2_locator box_loc;
      if (!read_state->box)
        { // We still have to open the first sub-box
          assert(read_state->asoc.exists());
          read_state->box.open(&read_state->asoc);
          if (!read_state->box)
            return false;
          box_type = read_state->box.get_box_type();
          box_loc = read_state->box.get_locator();
          box_len = (int) read_state->box.get_remaining_bytes();
          if (((box_type == jp2_free_4cc) || (box_type == jp2_group_4cc)) &&
              manager->flatten_free_asocs)
            { // Treat free asoc boxes as grouping boxes.  Also, treat asoc
              // boxes whose first sub-box is a group box as if it were a
              // free asoc box (by this we mean an asoc box with a free first
              // sub-box).  In both cases, the box is treated as a confirmed
              // generator.
              assert(parse_state->is_generator);
              read_state->box.close();
              box_type = jp2_group_4cc; // All generators given this box type
            }
          else if ((box_type == jp2_number_list_4cc) && (box_len == 0))
            { // Treat empty number list box as a grouping box, since it
              // has no other meaning.
              assert(parse_state->is_generator);
              read_state->box.close();
              box_type = jp2_group_4cc; // All generators given this box type
            }
          else if (parse_state->is_generator)
            { // Until this point we thought this node might be a possible
              // generator of new descendants as a free-asoc group
              parse_state->is_generator = false;
              assert(parent->parse_state->num_possible_generators > 0);
              parent->parse_state->num_possible_generators--;
              if (parent->parse_state->num_possible_generators == 0)
                parent->check_parsing_complete();
            }
          
          // Add a locator for the newly opened box
          if ((box_type != jp2_group_4cc) && (box_type != jp2_free_4cc) &&
              (box_type != jp2_cross_reference_4cc))
            { // Don't allow links to any of the above box types
              kdu_long file_pos = box_loc.get_file_pos();
              file_pos += read_state->box.get_box_header_length();
              jx_metaloc *metaloc =
                manager->metaloc_manager.get_locator(file_pos,true);
              jx_crossref *new_linkers = NULL;
              if ((metaloc->target != NULL) &&
                  (metaloc->target->rep_id == JX_CROSSREF_NODE) &&
                  (metaloc->target->crossref->metaloc == metaloc))
                {
                  new_linkers = metaloc->target->crossref;
                  if (linked_from == NULL)
                    linked_from = new_linkers;
                  else
                    {
                      jx_crossref *lfs=linked_from;
                      for (; lfs->next_link != NULL; lfs=lfs->next_link);
                      lfs->next_link = new_linkers;
                    }
                }
              metaloc->target = this;
              for (cref=new_linkers; cref != NULL; cref=cref->next_link)
                cref->link_found();
            }
        }
      
      if (parse_state->is_generator)
        { // Generators are to be flattened and eventually removed by calling
          // `remove_empty_shell'.  For this reason, we cannot tolerate links
          // to a generator, either from `jx_metaloc' or `jx_crossref'.  If
          // there are already links of this form, they must be removed now.
          // We will only have to do this once, because generators get
          // marked immediately as `JX_METANODE_BOX_COMPLETE' so we can
          // focus on their descendants alone.
          flags |= JX_METANODE_BOX_COMPLETE;
          read_state->box.close(); // Might already have been closed
          kdu_long file_pos = read_state->asoc.get_locator().get_file_pos();
          file_pos += read_state->asoc.get_box_header_length();
          jx_metaloc *metaloc =
            manager->metaloc_manager.get_locator(file_pos,true);
          if (metaloc != NULL)
            { // Actually, this locator should always exist, since we created
              // it inside `donate_input_box'.
              assert(metaloc->target == this);
              metaloc->target = NULL;
            }
          jx_crossref *cref;
          while ((cref=linked_from) != NULL)
            { // Remove the link and change link type to JPX_METANODE_LINK_NONE
              linked_from = cref->next_link;
              assert(cref->link == this);
              cref->link = NULL;
              cref->link_type = JPX_METANODE_LINK_NONE;
              cref->next_link = NULL;
            }
        }
      else
        { // Need to inspect the contents of `read_state->box'
          assert(read_state->box.exists());
          if (!read_state->box.is_complete())
            return false;
          if (box_len == 0)
            { // Still have to find `box_len' and `box_loc'
              box_len = (int) read_state->box.get_remaining_bytes();
              box_loc = read_state->box.get_locator();
            }
          flags |= JX_METANODE_BOX_COMPLETE;
          if ((box_type == jp2_number_list_4cc) && (box_len > 0))
            { 
              rep_id = JX_NUMLIST_NODE;
              jx_container_base *container = this->find_container();
              numlist = new jx_numlist(this,container);
              if ((box_len & 3) != 0)
                { KDU_WARNING(w,0x26100801); w <<
                  KDU_TXT("Malformed Number List (`nlst') box "
                          "found in JPX data source.  Box body does not "
                          "contain a whole number of 32-bit "
                          "integers.");
                  safe_delete();
                  return true;
                }
              box_len >>= 2;
              for (; box_len > 0; box_len--)
                { 
                  kdu_uint32 idx; read_state->box.read(idx);
                  if (idx == 0)
                    numlist->rendered_result = true;
                  else if ((idx & 0xFF000000) == 0x01000000)
                    numlist->add_codestream((int)(idx & 0x00FFFFFF),
                                            true);
                  else if ((idx & 0xFF000000) ==  0x02000000)
                    numlist->add_compositing_layer((int)(idx & 0x00FFFFFF),
                                                   true);
                }
            }
          else if ((box_type == jp2_roi_description_4cc) && (box_len > 1))
            { 
              rep_id = JX_ROI_NODE;
              regions = new jx_regions(this);
              if (!regions->read(read_state->box))
                { 
                  safe_delete();
                  return true;
                }
            }
          else if ((box_type == jp2_label_4cc) &&
                   (box_len >= 0) && (box_len < 8192))
            { 
              label = new char[box_len+1];
              rep_id = JX_LABEL_NODE;
              read_state->box.read((kdu_byte *) label,box_len);
              label[box_len] = '\0';
            }
          else if (box_type == jp2_cross_reference_4cc)
            { 
              rep_id = JX_CROSSREF_NODE;
              crossref = new jx_crossref(this);
              jp2_input_box flst;
              bool success = (read_state->box.read(crossref->box_type) &&
                              flst.open(&(read_state->box)) &&
                              (flst.get_box_type() == jp2_fragment_list_4cc) &&
                              crossref->frag_list.init(&flst,false));
              flst.close();
              if (!success)
                { KDU_WARNING(w,0x19040901); w <<
                  KDU_TXT("Malformed Cross Reference (`cref') box "
                          "found in JPX data source.");
                  safe_delete();
                  return true;
                }
              if ((crossref->box_type != jp2_group_4cc) &&
                  (crossref->box_type != jp2_free_4cc) &&
                  manager->test_box_filter(crossref->box_type))
                { 
                  kdu_long link_pos, link_len;
                  link_pos = crossref->frag_list.get_link_region(link_len);
                  if (link_pos > 0)
                    { 
                      jx_metaloc *metaloc = // Set `crossref->metaloc' later
                        manager->metaloc_manager.get_locator(link_pos,true);
                      if (metaloc->target == NULL)
                        { // Temporarily make `metaloc' point to ourself
                          crossref->metaloc = metaloc;
                          metaloc->target = this;
                          flags |= JX_METANODE_UNRESOLVED_LINK;
                        }
                      else if ((metaloc->target->rep_id == JX_CROSSREF_NODE) &&
                               (metaloc->target->crossref != NULL) &&
                               (metaloc->target->crossref->metaloc == metaloc))
                        { 
                          crossref->append_to_list(metaloc->target->crossref);
                          crossref->metaloc = metaloc;
                          flags |= JX_METANODE_UNRESOLVED_LINK;
                        }
                      else if (metaloc->target->flags & JX_METANODE_DELETED)
                        { // We will not be able to complete the link
                          safe_delete();
                          return true;
                        }
                      else
                        { 
                          if (metaloc->target->linked_from == NULL)
                            metaloc->target->linked_from = crossref;
                          else
                            crossref->append_to_list(
                                                metaloc->target->linked_from);
                          crossref->metaloc = metaloc; // So `link_found' works
                          crossref->link_found();
                        }
                    }
                }
            }
          else if (box_type == jp2_uuid_4cc)
            { 
              rep_id = JX_REF_NODE;
              ref = new jx_metaref;
              ref->src = manager->ultimate_src;
              ref->src_loc = box_loc;
              read_state->box.read(ref->data,16);
            }
          else
            { 
              rep_id = JX_REF_NODE;
              ref = new jx_metaref;
              ref->src = manager->ultimate_src;
              ref->src_loc = box_loc;
            }
          
          read_state->box.close(); // It may already have been closed
          if (parent != NULL)
            { 
              if (box_type == jp2_roi_description_4cc)
                parent->flags |= JX_METANODE_HAS_ROI_CHILD;
              else if (box_type != jp2_free_4cc)
                parent->flags |= JX_METANODE_HAS_NON_ROI_CHILD;
              parent->place_on_touched_list();
            }
          this->append_to_touched_list(false); // Force to end of list
          for (cref=linked_from; cref != NULL; cref=cref->next_link)
            cref->owner->append_to_touched_list(true);
              // Force descendants of newly connected linkers to end of list
          
          manager->link_to_libraries(this);
        }
    }
  
  while (!(flags & JX_METANODE_CONTAINER_KNOWN))
    { 
      assert((parse_state != NULL) && (read_state == parse_state->read));
      if (read_state->codestream_source != NULL)
        { 
          if (!read_state->codestream_source->finish(true))
            return false;
          assert(parse_state != NULL); // Because we passed true to above func
          continue;
        }
      if (read_state->layer_source != NULL)
        { 
          if (!read_state->layer_source->finish(true))
            return false;
          assert(parse_state != NULL); // Because we passed true to above func
          continue;
        }
      if (read_state->container_source != NULL)
        { 
          if (!read_state->container_source->finish(true))
            return false;
          assert(parse_state != NULL); // Because we passed true to above func
          continue;
        }

      assert(read_state->asoc.exists());
      assert(!read_state->box);
      if (read_state->box.open(&read_state->asoc))
        { 
          kdu_uint32 subbox_type = read_state->box.get_box_type();
          if (manager->test_box_filter(subbox_type))
            { // Need to keep the subbox
              jx_metanode *node = new jx_metanode(manager);
              if (!parse_state->is_generator)
                this->insert_child(node,this->tail,
                                   read_state->box.get_locator());
              else
                { // Make sub-box a sibling, placing it immediately before
                  // ourself.
                  parent->insert_child(node,this->prev_sibling,
                                       read_state->box.get_locator());
                  assert(node->next_sibling == this);
                }
              node->donate_input_box(read_state->box,
                                     parse_state->box_nesting_level);
              assert(!read_state->box.exists());
              if (link_pos >= 0)
                { // We might not need to execute `finish_reading' right now
                  kdu_long offs = link_pos - node->sequence_index;
                  if (offs < 0)
                    return false; // Already read further than we need to
                  if ((offs > node->parse_state->metanode_span) &&
                      (node->parse_state->metanode_span >= 0))
                    continue; // This node does not contain `link_pos'
                }
              if (node->finish_reading() && (node->rep_id == 0) &&
                  (node->parse_state == NULL))
                node->remove_empty_shell(); // Deletes `node'
            }
          else
            read_state->box.close(); // Skip this sub-box
        }
      else if (read_state->asoc.get_remaining_bytes() == 0)
        flags |= JX_METANODE_CONTAINER_KNOWN;
      else
        return false;
    }

  // If we get here, we have finished reading from the asoc/grp container or
  // we have finished reading the leaf node
  delete parse_state->read;
  parse_state->read = NULL;
  this->check_parsing_complete();
  
  return true;
}

/*****************************************************************************/
/*                       jx_metanode::make_complete                          */
/*****************************************************************************/

void jx_metanode::make_complete()
{ 
  if (!(flags & JX_METANODE_EXISTING))
    return;
  assert(flags & JX_METANODE_BOX_COMPLETE);
  if (flags & JX_METANODE_UNRESOLVED_LINK)
    { 
      assert((rep_id == JX_CROSSREF_NODE) && (crossref != NULL));
      if (crossref->metaloc != NULL)
        crossref->unlink();
      flags &= ~JX_METANODE_UNRESOLVED_LINK;
      this->check_parsing_complete();
    }
  jx_metanode *scan, *next;
  for (scan=head; (scan != NULL) && (parse_state != NULL); scan=next)
    { 
      next = scan->next_sibling;
      if (scan->flags & JX_METANODE_IS_COMPLETE)
        continue;
      if (scan->parse_state->is_generator ||
          !(scan->flags & JX_METANODE_BOX_COMPLETE))
        scan->unlink_parent();
      else
        scan->make_complete();
    }
  assert(scan->flags & JX_METANODE_IS_COMPLETE);
}
  
/*****************************************************************************/
/*                        jx_metanode::load_recursive                        */
/*****************************************************************************/

bool jx_metanode::load_recursive(kdu_long link_pos)
{
  if ((flags & JX_METANODE_EXISTING) &&
      !((flags & JX_METANODE_BOX_COMPLETE) &&
        (flags & JX_METANODE_CONTAINER_KNOWN)))
    { 
      assert(parse_state != NULL);
      if (finish_reading(link_pos) && (rep_id == 0) && (parse_state == NULL))
        { 
          this->remove_empty_shell();
          return true; // Deleted ourself!! Must not touch anything else
        }
    }
  
  if ((parse_state == NULL) || !(flags & JX_METANODE_EXISTING))
    return true;

  jx_metanode *scan=parse_state->incomplete_descendants;
  if (scan == NULL)
    return false;
  jx_incomplete_metanode_walker walker;
  walker.attach(scan);
  try { // Make absolutely sure the `walker' detaches, no matter what
    while ((scan = walker.advance()) != NULL)
      { 
        assert(scan->parse_state != NULL); // Otherwise should not be on list
        if (link_pos >= 0)
          { // `link_pos' lies within the span of this node.  Note that
            // `link_pos' corresponds to the first byte of the contents of
            // some box of interest, whereas `jx_metanode::sequence_index'
            // holds the absolute file location of the first byte in the header
            // of a node's box (or asoc container, if there is one).
            kdu_long offs = link_pos - scan->sequence_index;
            if ((offs <= 0) || ((offs > scan->parse_state->metanode_span) &&
                                (scan->parse_state->metanode_span >= 0)))
              continue;
          }
      
        // If we get here, it is time to try to load `scan'.  Note, however,
        // that this may not only cause `scan' to become complete (thus
        // dropping it from the incomplete list) but it might also cause other
        // nodes to become complete due to the resolution of a previous
        // unresolved link.  In particular, it is possible that `next'
        // references a box that gets dropped from the list in this way; there
        // is no possibility, however, that `next' gets deleted, only that its
        // `check_parsing_complete' function calls
        // `next->parse_state->remove_incomplete_child' and then
        // deletes `next->parse_state' setting it to NULL.  If this happens,
        // the simplest thing is to go back and scan again from the start of
        // the current object's incomplete-descendants list.
        scan->load_recursive(link_pos);
      }
  } catch (...) {
    walker.detach();
  }
  assert(!walker);
  return (this->parse_state == NULL);
}

/*****************************************************************************/
/*                         jx_metanode::find_path_to                         */
/*****************************************************************************/

jx_metanode *
  jx_metanode::find_path_to(jx_metanode *tgt, int descending_flags,
                            int ascending_flags, int num_excl,
                            const kdu_uint32 *excl_types,
                            const int *excl_flags,
                            bool unify_groups)
{
  assert((flags & JX_METANODE_LOOP_DETECTION) == 0);
  if (this == tgt)
    return this;
  if (unify_groups)
    {
      jx_metanode *alt_tgt=NULL, *alt_this=NULL;
      if ((tgt->rep_id == JX_CROSSREF_NODE) && (tgt->crossref != NULL) &&
          (tgt->crossref->link_type == JPX_GROUPING_LINK) &&
          (tgt->crossref->link != NULL) &&
          (tgt->crossref->link->flags & JX_METANODE_BOX_COMPLETE))
        alt_tgt = tgt->crossref->link;
      if ((this->rep_id == JX_CROSSREF_NODE) && (this->crossref != NULL) &&
          (this->crossref->link_type == JPX_GROUPING_LINK) &&
          (this->crossref->link != NULL) &&
          (this->crossref->link->flags & JX_METANODE_BOX_COMPLETE))
        alt_this = this->crossref->link;
      if ((alt_tgt == this) || (alt_this == tgt) ||
          ((alt_tgt != NULL) && (alt_tgt == alt_this)))
        return this;
    }

  if (!(flags & JX_METANODE_BOX_COMPLETE))
    return NULL;
  
  this->flags |= JX_METANODE_LOOP_DETECTION; // Mark this node to detect cycles
  
  // Consider parent
  if ((parent != NULL) && (ascending_flags & JPX_PATH_TO_DIRECT))
    {
      if ((parent == tgt) ||
          ((!(parent->flags & JX_METANODE_LOOP_DETECTION)) &&
           (parent->find_path_to(tgt,0,ascending_flags,num_excl,
                                 excl_types,excl_flags,unify_groups) != NULL)))
        { // Found a path; first segment of path is `parent'
          this->flags &= ~JX_METANODE_LOOP_DETECTION;
          return parent;
        }
    }
  
  // Consider descendants as direct matches or forward links
  int direct_descent = (descending_flags & JPX_PATH_TO_DIRECT);
  int forward_descent = (descending_flags & JPX_PATH_TO_FORWARD);
  int forward_ascent = (ascending_flags & JPX_PATH_TO_FORWARD);
  if (direct_descent || forward_descent || forward_ascent)
    {
      jx_metanode *scan;
      for (scan=head; scan != NULL; scan=scan->next_sibling)
        {
          if (scan == tgt)
            break; // Found a match
          if (scan->flags & JX_METANODE_LOOP_DETECTION)
            continue;
          if ((scan->head != NULL) && direct_descent &&
              (scan->find_path_to(tgt,descending_flags,ascending_flags,
                                  num_excl,excl_types,excl_flags,
                                  unify_groups) != NULL))
            break; // Found a path; first segment of path is `scan'
          if ((scan->rep_id == JX_CROSSREF_NODE) && (scan->crossref != NULL) &&
              (scan->crossref->link != NULL))
            {
              jx_metanode *link = scan->crossref->link;
              if (link->flags & JX_METANODE_LOOP_DETECTION)
                continue; // We have visited this link before!
              if ((num_excl > 0) &&
                  link->check_path_exclusion(num_excl,excl_types,excl_flags))
                continue; // We are about to visit an excluded link
              if (forward_descent &&
                  (scan->crossref->link_type == JPX_ALTERNATE_CHILD_LINK) &&
                  ((link == tgt) ||
                   (link->find_path_to(tgt,descending_flags,ascending_flags,
                                       num_excl,excl_types,excl_flags,
                                       unify_groups)!=NULL)))
                { // Found a path; first segment of path is `link'
                  scan = link; break;
                }
              if (forward_ascent &&
                  (scan->crossref->link_type == JPX_ALTERNATE_PARENT_LINK) &&
                  ((link == tgt) ||
                   (link->find_path_to(tgt,0,ascending_flags,num_excl,
                                       excl_types,excl_flags,
                                       unify_groups) != NULL)))
                { // Found a path; first segment of path is `link'
                  scan = link; break;
                }
            }
        }
      if (scan != NULL)
        { // Found a path; remove the LOOP_DETECTION flag before returning
          this->flags &= ~JX_METANODE_LOOP_DETECTION;
          return scan; 
        }
    }

  // Consider reverse links
  int reverse_descent = (descending_flags & JPX_PATH_TO_REVERSE);
  int reverse_ascent = (ascending_flags & JPX_PATH_TO_REVERSE);
  if (reverse_descent || reverse_ascent)
    {
      jx_crossref *scan;
      for (scan=linked_from; scan != NULL; scan=scan->next_link)
        {
          jx_metanode *linker = scan->owner;
          if (linker->flags & JX_METANODE_LOOP_DETECTION)
            continue; // We have visited this linker before!
          if ((num_excl > 0) &&
              linker->check_path_exclusion(num_excl,excl_types,excl_flags))
            continue; // We are about to visit an excluded linker
          if (reverse_descent &&
              (scan->link_type == JPX_ALTERNATE_PARENT_LINK) &&
              ((linker == tgt) ||
               (linker->find_path_to(tgt,descending_flags,ascending_flags,
                                     num_excl,excl_types,excl_flags,
                                     unify_groups) != NULL)))
            break; // Found a path
          if (reverse_ascent &&
              (scan->link_type == JPX_ALTERNATE_CHILD_LINK) &&
              ((linker == tgt) ||
               (linker->find_path_to(tgt,0,ascending_flags,num_excl,
                                     excl_types,excl_flags,
                                     unify_groups) != NULL)))
            break; // Found a path
        }
      if (scan != NULL)
        { // Found a path; remove the LOOP_DETECTION flag before returning
          this->flags &= ~JX_METANODE_LOOP_DETECTION;
          return scan->owner;
        }
    }
  
  this->flags &= ~JX_METANODE_LOOP_DETECTION;
  return NULL;
}

/*****************************************************************************/
/*                     jx_metanode::check_path_exclusion                     */
/*****************************************************************************/

bool
  jx_metanode::check_path_exclusion(int num_exclusion_types,
                                    const kdu_uint32 *exclusion_box_types,
                                    const int *exclusion_flags)
{
  int n;
  for (n=0; n < num_exclusion_types; n++)
    {
      kdu_uint32 test_box_type = exclusion_box_types[n];
      if ((exclusion_flags[n] & JPX_PATH_TO_EXCLUDE_BOX) &&
          (this->box_type == test_box_type))
        return true;
      if (exclusion_flags[n] & JPX_PATH_TO_EXCLUDE_PARENTS)
        for (jx_metanode *scan=parent; scan != NULL; scan=scan->parent)
          if (scan->box_type == test_box_type)
            return true;
    }
  return false;
}

/*****************************************************************************/
/*                      jx_metanode::clear_write_state                       */
/*****************************************************************************/

void
  jx_metanode::clear_write_state(bool reset_links)
{
  assert(!(flags & JX_METANODE_EXISTING));
  if (flags & JX_METANODE_WRITTEN)
    assert(write_state == NULL); // Verify no confusion with `preserve_state'
  flags &= ~JX_METANODE_WRITTEN;
  if (write_state != NULL)
    { 
      delete write_state;
      write_state = NULL;
    }
  
  if (reset_links)
    for (jx_crossref *cref=linked_from; cref != NULL; cref=cref->next_link)
      { 
        cref->box_type = 0;
        cref->frag_list.reset();
      }
  
  jx_metanode *scan;
  if (parent == NULL)
    { 
      for (scan=manager->first_unwritten;
           scan != NULL;
           scan=(scan==manager->last_to_write)?NULL:scan->next_sibling)
        scan->clear_write_state(reset_links);
    }
  else
    { 
      for (scan=head; scan != NULL; scan=scan->next_sibling)
        scan->clear_write_state(reset_links);
    }
}

/*****************************************************************************/
/*                            jx_metanode::write                             */
/*****************************************************************************/

jp2_output_box *
  jx_metanode::write(jp2_output_box *super_box, int *i_param,
                     void **addr_param, kdu_long &file_pos)
{
  assert(!(flags & JX_METANODE_EXISTING));
  if ((parent != NULL) && !this->can_write())
    return NULL; // Not tree root and not a regular node we can write
  assert(!(flags & JX_METANODE_WRITTEN));
  if (write_state == NULL)
    write_state = new jx_metawrite;
  bool is_top_level = (super_box == NULL);
  bool skip_and_flatten = this->is_empty_numlist();
  
  jx_metawrite &ws = *write_state;
  
  jx_crossref *cref;
  if ((parent != NULL) &&
      !(ws.asoc.exists() || ws.box.exists() || skip_and_flatten))
    { // Haven't started writing anything yet.
      bool need_asoc_box = false;
      if ((head != NULL) ||
          ((rep_id == JX_CROSSREF_NODE) &&
           (crossref->link_type == JPX_GROUPING_LINK)))
        need_asoc_box = true;
      else
        { // See if we are linked as an alternate parent
          for (cref=linked_from; cref != NULL; cref=cref->next_link)
            if (cref->link_type == JPX_ALTERNATE_PARENT_LINK)
              { need_asoc_box = true; break; }
        }
      if (need_asoc_box)
        { // Need to write an association box to hold contents
          if (super_box != NULL)
            ws.asoc.open(super_box,jp2_association_4cc);
          else
            file_pos =
              manager->meta_target->open_top_box(ws.asoc,jp2_association_4cc,
                                                 manager->simulation_phase);
          file_pos += ws.asoc.use_long_header(); // So we can be sure of the
                                                 // box header length.
        }
    }
  if (ws.asoc.exists())
    super_box = &ws.asoc;

  if (ws.active_descendant == NULL)
    { // See if we need to write the box contents
      ws.start_pos = file_pos;
      if (ws.box.exists())
        { // Must be returning after having the application complete this box
          assert((rep_id == JX_REF_NODE) && (ref->src == NULL));
        }
      else if ((parent != NULL) && !skip_and_flatten)
        { 
          if (super_box != NULL)
            ws.box.open(super_box,box_type);
          else
            file_pos =
              manager->meta_target->open_top_box(ws.box,box_type,
                                                 manager->simulation_phase);
          if (rep_id == JX_ROI_NODE)
            { // Write an ROI description box
              assert(box_type == jp2_roi_description_4cc);
              regions->write(ws.box);
            }
          else if (rep_id == JX_NUMLIST_NODE)
            { // Write a number list box
              assert(box_type == jp2_number_list_4cc);
              numlist->write(ws.box);
            }
          else if (rep_id == JX_LABEL_NODE)
            { // Write a label box
              assert(box_type == jp2_label_4cc);
              ws.box.write((kdu_byte *) label,(int) strlen(label));
            }
          else if (rep_id == JX_REF_NODE)
            {
              if (ref->src != NULL)
                { // Copy source box.
                  jp2_input_box src;
                  src.open(ref->src,ref->src_loc);
                  kdu_long body_bytes = src.get_remaining_bytes();
                  ws.box.set_target_size(body_bytes);
                  int xfer_bytes;
                  kdu_byte buf[1024];
                  while ((xfer_bytes=src.read(buf,1024)) > 0)
                    {
                      body_bytes -= xfer_bytes;
                      ws.box.write(buf,xfer_bytes);
                    }
                  src.close();
                  assert(body_bytes == 0);
                }
              else
                { // Application must write this box
                  if (i_param != NULL)
                    *i_param = ref->i_param;
                  if (addr_param != NULL)
                    *addr_param = ref->addr_param;
                  return &(ws.box);
                }
            }
          else if (rep_id == JX_CROSSREF_NODE)
            {
              assert(crossref != NULL);
              ws.box.write(crossref->box_type);
              if (crossref->box_type == 0)
                { // Set up dummy frag list with correct size for simulation
                  assert(manager->simulation_phase != 0);
                  crossref->frag_list.set_link_region(0,0);
                }
              crossref->frag_list.save_box(&(ws.box));
            }
          else
            assert(0);
        }
      
      if ((parent != NULL) && !skip_and_flatten)
        { 
          ws.box.close();
          file_pos += ws.box.get_box_length();
        }
      
      // Now get ready to write descendants
      bool have_container_descendants=false;
      int non_roi_descendants=0;
      jx_metanode *scan;
      for (scan=(parent==NULL)?manager->first_unwritten:head;
           scan != NULL;
           scan=(scan==manager->last_to_write)?NULL:scan->next_sibling)
        { 
          if (scan->rep_id == JX_ROI_NODE)
            ws.region_library.add(scan->regions,true); // Add as temp copy
          else if (scan->is_top_container_numlist())
            { 
              if (!have_container_descendants)
                ws.active_descendant = scan;
              have_container_descendants = true;
            }
          else if (scan->can_write())
            { 
              if ((non_roi_descendants == 0) && !have_container_descendants)
                ws.active_descendant = scan;
              non_roi_descendants++;
            }
        }
      if (non_roi_descendants)
        { 
          if (manager->group_with_free_asocs)
            ws.group_writer.init(0,true,false); // Use free-asocs only for ROI's
          else
            ws.group_writer.init(non_roi_descendants,false,is_top_level);
        }
      else if (ws.region_library.num_elts > 0)
        { 
          ws.group_writer.init(ws.region_library.num_elts,
                               manager->group_with_free_asocs,is_top_level);
          ws.active_roi = ws.region_library.enum_elts(NULL);
          ws.active_descendant = ws.active_roi->metanode;
        }
    }

  while (ws.active_descendant != NULL)
    { 
      jx_container_target *container_tgt = NULL;
      if (ws.active_descendant->is_top_container_numlist())
        { 
          container_tgt = (jx_container_target *)
            ws.active_descendant->numlist->container;
          jp2_output_box *jclx_box=NULL;
          jp2_output_box *interrupt =
            container_tgt->write_jclx(i_param,addr_param,
                                      manager->simulation_phase,&file_pos,
                                      &jclx_box);
          if (interrupt != NULL)
            return interrupt;
          assert((jclx_box != NULL) && jclx_box->exists());
          interrupt =
            ws.active_descendant->write(jclx_box,i_param,addr_param,file_pos);
          if (interrupt != NULL)
            return interrupt;
          if (ws.active_descendant == container_tgt->last_metanode)
            jclx_box->close();
        }
      else
        { 
          jp2_output_box *container =
            ws.group_writer.get_container(super_box,manager,file_pos);
          jp2_output_box *interrupt =
            ws.active_descendant->write(container,i_param,addr_param,file_pos);
          if (interrupt != NULL)
            return interrupt;
          ws.group_writer.advance();
        }
      
      // Advance the `ws.active_descendant' pointer
      jx_metanode *scan=ws.active_descendant; // We have to advance this
      scan = (scan==manager->last_to_write)?NULL:scan->next_sibling;
      ws.active_descendant = NULL; // Until we find a non-NULL one
      if (container_tgt != NULL)
        { // Advance to the next container-embedded number list, if possible
          assert((parent == NULL) && (ws.active_roi == NULL));
          for (; scan != NULL;
               scan=(scan==manager->last_to_write)?NULL:scan->next_sibling)
            if (scan->is_top_container_numlist())
              break;
          ws.active_descendant = scan;
          if (scan == NULL)
            scan = manager->first_unwritten;
        }
      if (ws.active_descendant == NULL)
        { 
          if (ws.active_roi == NULL)
            { // Advance to next non-ROI descendant
              for (; scan != NULL;
                   scan=(scan==manager->last_to_write)?NULL:scan->next_sibling)
                if (scan->can_write() && (scan->rep_id != JX_ROI_NODE) &&
                    !scan->is_top_container_numlist())
                  break;
              if (scan != NULL)
                ws.active_descendant = scan;
              else if (ws.region_library.num_elts > 0)
                { 
                  ws.group_writer.init(ws.region_library.num_elts,
                                       manager->group_with_free_asocs,
                                       is_top_level);
                  ws.active_roi = ws.region_library.enum_elts(NULL);
                  ws.active_descendant = ws.active_roi->metanode;
                }
            }
          else if ((ws.active_roi =
                    ws.region_library.enum_elts(ws.active_roi)) != NULL)
            ws.active_descendant = ws.active_roi->metanode;
        }
    }
  
  kdu_long asoc_contents_len = -1;
  if (ws.asoc.exists())
    { 
      ws.asoc.close();
      asoc_contents_len = ws.asoc.get_box_length() -
        ws.asoc.get_header_length();
      assert(file_pos == (ws.start_pos + asoc_contents_len));
    }
  
  // If we get here, we have finished writing this node and all of its
  // descendants.  We are now in a position to set the crossref address and
  // length associated with any link nodes that reference us.
  jx_metapres *pres = NULL;
  if (!skip_and_flatten)
    { 
      for (cref=linked_from; cref != NULL; cref=cref->next_link)
        if (cref->box_type == 0)
          { 
            if ((asoc_contents_len < 0) ||
                (cref->link_type != JPX_ALTERNATE_CHILD_LINK))
              { // Link to the contents of `ws.box'
                cref->box_type = this->box_type;
                kdu_long hdr_len = ws.box.get_header_length();
                kdu_long content_len = ws.box.get_box_length() - hdr_len;
                cref->frag_list.set_link_region(ws.start_pos+hdr_len,
                                                content_len);
              }
            else
              { // Link to the contents of `ws.asoc'
                cref->box_type = jp2_association_4cc;
                cref->frag_list.set_link_region(ws.start_pos,
                                                asoc_contents_len);
              }
          }
      
      if ((flags & JX_METANODE_PRESERVE) && (manager->simulation_phase == 0))
        { 
          pres = new jx_metapres;
          pres->asoc_contents_pos = ws.start_pos;
          pres->asoc_contents_len = asoc_contents_len;
          kdu_long hdr_len = ws.box.get_header_length();
          pres->contents_pos = ws.start_pos + hdr_len;
          pres->contents_len = ws.box.get_box_length() - hdr_len;
        }
    }
  delete write_state;
  write_state = NULL;
  assert(preserve_state == NULL);
  preserve_state = pres;
  flags |= JX_METANODE_WRITTEN;
  
  return NULL; // All done
}


/* ========================================================================= */
/*                              jpx_metanode                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                      jpx_metanode::get_numlist_info                       */
/*****************************************************************************/

bool
  jpx_metanode::get_numlist_info(int &num_codestreams, int &num_layers,
                                 bool &applies_to_rendered_result) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return false;
  assert(state->flags & JX_METANODE_BOX_COMPLETE);
  num_codestreams = state->numlist->num_codestreams;
  num_layers = state->numlist->num_compositing_layers;
  applies_to_rendered_result = state->numlist->rendered_result;
  return true;
}

/*****************************************************************************/
/*                     jpx_metanode::get_container_id                        */
/*****************************************************************************/

int
  jpx_metanode::get_container_id()
{
  jx_container_base *elt;
  if ((state == NULL) || ((elt = state->find_container()) == NULL))
    return -1;
  return elt->get_id();
}

/*****************************************************************************/
/*                    jpx_metanode::get_container_lmap                       */
/*****************************************************************************/

int
  jpx_metanode::get_container_lmap(int *base, int *span)
{
  jx_container_base *elt;
  if ((state == NULL) || ((elt = state->find_container()) == NULL))
    return 0;
  if (base != NULL)
    *base = elt->get_first_base_layer();
  if (span != NULL)
    *span = elt->get_num_base_layers();
  return (elt->indefinitely_repeated())?-1:elt->get_known_reps();
}

/*****************************************************************************/
/*                    jpx_metanode::get_container_cmap                       */
/*****************************************************************************/

int
  jpx_metanode::get_container_cmap(int *base, int *span)
{
  jx_container_base *elt;
  if ((state == NULL) || ((elt = state->find_container()) == NULL))
    return 0;
  if (base != NULL)
    *base = elt->get_first_base_codestream();
  if (span != NULL)
    *span = elt->get_num_base_codestreams();
  return (elt->indefinitely_repeated())?-1:elt->get_known_reps();
}

/*****************************************************************************/
/*                jpx_metanode::get_container_codestream_rep                 */
/*****************************************************************************/

int
  jpx_metanode::get_container_codestream_rep(int stream_idx) const
{
  jx_container_base *elt;
  if ((state == NULL) || ((elt = state->find_container()) == NULL) ||
      ((stream_idx -= elt->get_first_base_codestream()) < 0) ||
      (elt->get_num_base_codestreams() < 1))
    return 0;
  int rep_idx = stream_idx / elt->get_num_base_codestreams();
  if ((rep_idx >= elt->get_known_reps()) && !elt->indefinitely_repeated())
    rep_idx = 0;
  return rep_idx;
}

/*****************************************************************************/
/*                  jpx_metanode::get_container_layer_rep                    */
/*****************************************************************************/

int
  jpx_metanode::get_container_layer_rep(int layer_idx) const
{
  jx_container_base *elt;
  if ((state == NULL) || ((elt = state->find_container()) == NULL) ||
      ((layer_idx -= elt->get_first_base_layer()) < 0))
    return 0;
  int rep_idx = layer_idx / elt->get_num_base_layers();
  if ((rep_idx >= elt->get_known_reps()) && !elt->indefinitely_repeated())
    rep_idx = 0;
  return rep_idx;
}

/*****************************************************************************/
/*                  jpx_metanode::get_numlist_codestreams                    */
/*****************************************************************************/

const int *
  jpx_metanode::get_numlist_codestreams() const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return NULL;
  return state->numlist->codestream_indices;
}

/*****************************************************************************/
/*                     jpx_metanode::get_numlist_layers                      */
/*****************************************************************************/

const int *
  jpx_metanode::get_numlist_layers() const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return NULL;
  return state->numlist->layer_indices;
}

/*****************************************************************************/
/*                jpx_metanode::count_numlist_codestreams                    */
/*****************************************************************************/

bool
  jpx_metanode::count_numlist_codestreams(int &count)
{
  count = 0;
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return true;
  jx_numlist *nl = state->numlist;
  count = nl->num_codestreams;
  int base_span = nl->num_codestreams - nl->non_base_codestreams;
  if (base_span < 1)
    return true;
  assert(nl->container != NULL);
  bool count_known = true;
  if (nl->container->indefinitely_repeated())
    { 
      jx_source *source = state->manager->source;
      if (source != NULL)
        count_known = source->find_all_streams();
    }
  int num_reps = nl->container->get_known_reps();
  if (num_reps > 1)
    count += (num_reps-1)*base_span;
  return count_known;
}

/*****************************************************************************/
/*                   jpx_metanode::count_numlist_layers                      */
/*****************************************************************************/

bool
  jpx_metanode::count_numlist_layers(int &count)
{
  count = 0;
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return true;
  jx_numlist *nl = state->numlist;
  count = nl->num_compositing_layers;
  int base_span = nl->num_compositing_layers - nl->non_base_layers;
  if (base_span < 1)
    return true;
  assert(nl->container != NULL);
  bool count_known = true;
  if (nl->container->indefinitely_repeated())
    { 
      jx_source *source = state->manager->source;
      if (source != NULL)
        count_known = source->find_all_streams();
    }
  int num_reps = nl->container->get_known_reps();
  if (num_reps > 1)
    count += (num_reps-1)*base_span;
  return count_known;
}

/*****************************************************************************/
/*                    jpx_metanode::test_numlist_stream                      */
/*****************************************************************************/

bool
  jpx_metanode::test_numlist_stream(int stream_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE) ||
      (stream_idx < 0))
    return false;
  jx_numlist *nl = state->numlist;
  if ((nl->num_codestreams < 1) || (stream_idx < nl->codestream_indices[0]))
    return false;
  int n, non_base = nl->non_base_codestreams;
  if ((non_base < nl->num_codestreams) &&
      (stream_idx >= nl->codestream_indices[non_base]))
    { // Not a top-level codestream
      assert(nl->container != NULL);
      int num_base_streams = nl->container->get_num_base_codestreams();
      assert(num_base_streams > 0);
      int first_base = nl->container->get_first_base_codestream();
      int delta = stream_idx - first_base;
      int rep_idx = delta / num_base_streams;
      if ((rep_idx >= nl->container->get_known_reps()) &&
          !nl->container->indefinitely_repeated())
        return false;
      stream_idx -= rep_idx*num_base_streams;
      for (n=non_base; n < nl->num_codestreams; n++)
        if (nl->codestream_indices[n] >= stream_idx) // Elements are ordered
          return (nl->codestream_indices[n] == stream_idx);
    }
  else
    { 
      for (n=0; n < nl->non_base_codestreams; n++)
        if (nl->codestream_indices[n] >= stream_idx) // Elements are ordered
          return (nl->codestream_indices[n] == stream_idx);
    }
  return false;
}

/*****************************************************************************/
/*                    jpx_metanode::test_numlist_layer                       */
/*****************************************************************************/

bool
  jpx_metanode::test_numlist_layer(int layer_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE) ||
      (layer_idx < 0))
    return false;
  jx_numlist *nl = state->numlist;
  if ((nl->num_compositing_layers < 1) || (layer_idx < nl->layer_indices[0]))
    return false;
  int n, non_base = nl->non_base_layers;
  if ((non_base < nl->num_compositing_layers) &&
      (layer_idx >= nl->layer_indices[non_base]))
    { // Not a top-level compositing layer
      assert(nl->container != NULL);
      int num_base_layers = nl->container->get_num_base_layers();
      assert(num_base_layers > 0);
      int first_base = nl->container->get_first_base_layer();
      int delta = layer_idx - first_base;
      int rep_idx = delta / num_base_layers;
      if ((rep_idx >= nl->container->get_known_reps()) &&
          !nl->container->indefinitely_repeated())
        return false;
      layer_idx -= rep_idx*num_base_layers;
      for (n=non_base; n < nl->num_compositing_layers; n++)
        if (nl->layer_indices[n] >= layer_idx) // Elements are ordered
          return (nl->layer_indices[n] == layer_idx);
    }
  else
    { 
      for (n=0; n < nl->non_base_layers; n++)
        if (nl->layer_indices[n] >= layer_idx) // Elements are ordered
          return (nl->layer_indices[n] == layer_idx);
    }
  return false;
}

/*****************************************************************************/
/*                  jpx_metanode::get_numlist_codestream                     */
/*****************************************************************************/

int
  jpx_metanode::get_numlist_codestream(int which, int rep_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE) || (which < 0))
    return -1;
  jx_numlist *nl = state->numlist;
  int result = -1;
  if ((rep_idx < 0) && (which >= nl->num_codestreams))
    { // Special case, in which `which' enumerates all possible matches
      int rep_span = nl->num_codestreams - nl->non_base_codestreams;
      if (rep_span > 0)
        { 
          assert(nl->container != NULL);
          int delta = which - nl->non_base_codestreams;
          rep_idx = delta / rep_span;
          which -= rep_idx * rep_span;
        }
    }
  if (which < nl->num_codestreams)
    { 
      result = nl->codestream_indices[which];
      if ((rep_idx > 0) && (nl->container != NULL))
        result = nl->container->map_codestream_id(result,rep_idx,true);
    }
  return result;
}

/*****************************************************************************/
/*                     jpx_metanode::get_numlist_layer                       */
/*****************************************************************************/

int
  jpx_metanode::get_numlist_layer(int which, int rep_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE) || (which < 0))
    return -1;
  jx_numlist *nl = state->numlist;
  int result = -1;
  if ((rep_idx < 0) && (which >= nl->num_compositing_layers))
    { // Special case, in which `which' enumerates all possible matches
      int rep_span = nl->num_compositing_layers - nl->non_base_layers;
      if (rep_span > 0)
        { 
          assert(nl->container != NULL);
          int delta = which - nl->non_base_layers;
          rep_idx = delta / rep_span;
          which -= rep_idx * rep_span;
        }
    }
  if (which < nl->num_compositing_layers)
    { 
      result = nl->layer_indices[which];
      if ((rep_idx > 0) && (nl->container != NULL))
        result = nl->container->map_codestream_id(result,rep_idx,true);
    }
  return result;
}

/*****************************************************************************/
/*                  jpx_metanode::find_numlist_codestream                    */
/*****************************************************************************/

int
  jpx_metanode::find_numlist_codestream(int stream_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return -1;
  jx_numlist *nl = state->numlist;
  int n;
  if ((nl->non_base_codestreams > 0) &&
      (stream_idx <= nl->codestream_indices[nl->non_base_codestreams-1]))
    { // Not a container-replicated codestream
      for (n=0; n < nl->non_base_codestreams; n++)
        if (nl->codestream_indices[n] >= stream_idx)
          { 
            if (nl->codestream_indices[n] == stream_idx) return n;
            break;
          }
    }
  else if (nl->container != NULL)
    { 
      stream_idx =
        nl->container->validate_and_normalize_codestream_id(stream_idx,true);
      for (n=nl->non_base_codestreams; n < nl->num_codestreams; n++)
        if (nl->codestream_indices[n] >= stream_idx)
          { 
            if (nl->codestream_indices[n] == stream_idx) return n;
            break;
          }
    }
  return -1;
}

/*****************************************************************************/
/*                    jpx_metanode::find_numlist_layer                       */
/*****************************************************************************/

int
  jpx_metanode::find_numlist_layer(int layer_idx) const
{
  if ((state == NULL) || (state->rep_id != JX_NUMLIST_NODE))
    return -1;
  jx_numlist *nl = state->numlist;
  int n;
  if ((nl->non_base_layers > 0) &&
      (layer_idx <= nl->layer_indices[nl->non_base_layers-1]))
    { // Not a container-replicated layer
      for (n=0; n < nl->non_base_layers; n++)
        if (nl->layer_indices[n] >= layer_idx)
          { 
            if (nl->layer_indices[n] == layer_idx) return n;
            break;
          }
    }
  else if (nl->container != NULL)
    { 
      layer_idx =
        nl->container->validate_and_normalize_layer_id(layer_idx,true);
      for (n=nl->non_base_layers; n < nl->num_compositing_layers; n++)
        if (nl->layer_indices[n] >= layer_idx)
          { 
            if (nl->layer_indices[n] == layer_idx) return n;
            break;
          }
    }
  return -1;
}

/*****************************************************************************/
/*                jpx_metanode::find_next_identical_numlist                  */
/*****************************************************************************/

jpx_metanode jpx_metanode::find_next_identical_numlist()
{
  jx_numlist *result=NULL;
  if ((state != NULL) && ((state->rep_id == JX_NUMLIST_NODE) ||
                          (state->numlist != NULL)))
    result = state->numlist->next_identical;
  jx_metanode *node = (result==NULL)?NULL:result->metanode;
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                jpx_metanode::find_first_identical_numlist                 */
/*****************************************************************************/

jpx_metanode jpx_metanode::find_first_identical_numlist()
{
  jx_numlist *result=NULL;
  if ((state != NULL) && ((state->rep_id == JX_NUMLIST_NODE) ||
                          (state->numlist != NULL)))
    { 
      result = state->numlist->first_identical;
      assert(result != NULL);
    }
  jx_metanode *node = (result==NULL)?NULL:result->metanode;
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                    jpx_metanode::get_numlist_container                    */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_numlist_container()
{
  jx_metanode *node;
  for (node=state; node != NULL; node=node->parent)
    { 
      if (node->rep_id == JX_NUMLIST_NODE)
        break;
    }
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                      jpx_metanode::compare_numlists                       */
/*****************************************************************************/

int
  jpx_metanode::compare_numlists(const jpx_metanode rhs) const
{
  jx_numlist *llst=NULL, *rlst=NULL;
  jx_metanode *scan;
  for (scan=this->state; scan != NULL; scan=scan->parent)
    if (scan->rep_id == JX_NUMLIST_NODE)
      { llst=scan->numlist; break; }
  for (scan=rhs.state; scan != NULL; scan=scan->parent)
    if (scan->rep_id == JX_NUMLIST_NODE)
      { rlst=scan->numlist; break; }
  if (llst == NULL)
    return (rlst == NULL)?0:-1;
  if (rlst == NULL)
    return 1;
  int left_container_id=-1, right_container_id=-1;
  if (llst->container != NULL)
    left_container_id = llst->container->get_id();
  if (rlst->container != NULL)
    right_container_id = rlst->container->get_id();
  if (left_container_id != right_container_id)
    return (left_container_id - right_container_id);
  if (llst->rendered_result != rlst->rendered_result)
    return (llst->rendered_result)?-1:1;
  int n, delta;
  for (n=0; (n < llst->num_compositing_layers) &&
            (n < rlst->num_compositing_layers); n++)
    if ((delta = llst->layer_indices[n]-rlst->layer_indices[n]) != 0)
      return delta;
  if ((delta = llst->num_compositing_layers-rlst->num_compositing_layers) != 0)
    return delta;
  for (n=0; (n < llst->num_codestreams) && (n < rlst->num_codestreams); n++)
    if ((delta = llst->codestream_indices[n]-rlst->codestream_indices[n]) != 0)
      return delta;
  return (llst->num_codestreams - rlst->num_codestreams);
}

/*****************************************************************************/
/*                  jpx_metanode::count_numlist_descendants                  */
/*****************************************************************************/

bool jpx_metanode::count_numlist_descendants(int &count)
{
  count = 0;
  if (state == NULL)
    return true; // We know that there are no descendants of empty interface
  return state->count_numlist_descendants(count);
}

/*****************************************************************************/
/*                      jpx_metanode::get_num_regions                        */
/*****************************************************************************/

int
  jpx_metanode::get_num_regions() const
{
  if ((state == NULL) || (state->rep_id != JX_ROI_NODE))
    return 0;
  assert(state->flags & JX_METANODE_BOX_COMPLETE);
  return state->regions->num_regions;
}

/*****************************************************************************/
/*                        jpx_metanode::get_regions                          */
/*****************************************************************************/

jpx_roi *jpx_metanode::get_regions()
{
  if ((state == NULL) || (state->rep_id != JX_ROI_NODE))
    return NULL;
  return state->regions->regions;
}

/*****************************************************************************/
/*                         jpx_metanode::get_region                          */
/*****************************************************************************/

jpx_roi
  jpx_metanode::get_region(int which) const
{
  jpx_roi result;
  if ((state != NULL) && (state->rep_id == JX_ROI_NODE) &&
      (which >= 0) && (which < state->regions->num_regions))
    result = state->regions->regions[which];
  return result;
}

/*****************************************************************************/
/*                         jpx_metanode::get_width                           */
/*****************************************************************************/

int
  jpx_metanode::get_width() const
{
  int result = 0;
  if ((state != NULL) && (state->rep_id == JX_ROI_NODE) &&
      (state->regions->num_regions > 0))
    result = state->regions->max_width;
  return result;
}
  
/*****************************************************************************/
/*                      jpx_metanode::get_bounding_box                       */
/*****************************************************************************/

kdu_dims
  jpx_metanode::get_bounding_box() const
{
  kdu_dims result;
  if ((state != NULL) && (state->rep_id == JX_ROI_NODE) &&
      (state->regions->num_regions > 0))
    result = state->regions->bounding_region.region;
  return result;
}


/*****************************************************************************/
/*                        jpx_metanode::test_region                          */
/*****************************************************************************/

bool
  jpx_metanode::test_region(kdu_dims region) const
{
  if ((state == NULL) || (state->rep_id != JX_ROI_NODE))
    return false;
  int n;
  for (n=0; n < state->regions->num_regions; n++)
    if (state->regions->regions[n].region.intersects(region))
      return true;
  return false;
}

/*****************************************************************************/
/*                jpx_metanode::get_has_dependent_roi_nodes                  */
/*****************************************************************************/

bool
  jpx_metanode::has_dependent_roi_nodes() const
{
  if ((state == NULL) ||
      ((state->rep_id == JX_NUMLIST_NODE) &&
       (state->numlist->num_codestreams > 0)))
    return false;
  jx_metanode *scan = state->head;
  for (; scan != NULL; scan=scan->next_sibling)
    if (scan->rep_id == JX_ROI_NODE)
      return true;
    else if ((scan->rep_id == JX_NUMLIST_NODE) &&
             (scan->numlist->num_codestreams > 0))
      continue; // numlist with codestreams breaks dependencies on `state'
    else if (scan->head != NULL)
      { 
        jpx_metanode child(scan);
        if (child.has_dependent_roi_nodes())
          return true;
      }
  return false;
}

/*****************************************************************************/
/*                       jpx_metanode::get_box_type                          */
/*****************************************************************************/

kdu_uint32
  jpx_metanode::get_box_type() const
{
  return (state == NULL)?0:(state->box_type);
}

/*****************************************************************************/
/*                         jpx_metanode::get_label                           */
/*****************************************************************************/

const char *
  jpx_metanode::get_label() const
{
  if ((state == NULL) || (state->rep_id != JX_LABEL_NODE))
    return NULL;
  return state->label;
}

/*****************************************************************************/
/*                         jpx_metanode::get_uuid                            */
/*****************************************************************************/

bool
  jpx_metanode::get_uuid(kdu_byte uuid[]) const
{
  if ((state == NULL) || (state->box_type != jp2_uuid_4cc) ||
      (state->rep_id != JX_REF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return false;
  memcpy(uuid,state->ref->data,16);
  return true;
}

/*****************************************************************************/
/*                        jpx_metanode::is_xmp_uuid                          */
/*****************************************************************************/

bool
  jpx_metanode::is_xmp_uuid() const
{
  if ((state == NULL) || (state->box_type != jp2_uuid_4cc) ||
      (state->rep_id != JX_REF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return false;
  kdu_byte xmp_uuid[16] =
    {0xBE,0x7A,0xCF,0xCB,0x97,0xA9,0x42,0xE8,
     0x9C,0x71,0x99,0x94,0x91,0xE3,0xAF,0xAC};
  return (memcmp(xmp_uuid,state->ref->data,16) == 0);
}

/*****************************************************************************/
/*                        jpx_metanode::is_iptc_uuid                         */
/*****************************************************************************/

bool
  jpx_metanode::is_iptc_uuid() const
{
  if ((state == NULL) || (state->box_type != jp2_uuid_4cc) ||
      (state->rep_id != JX_REF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return false;
  kdu_byte iptc_uuid[16] =
    {0x33,0xC7,0xA4,0xD2,0xB8,0x1D,0x47,0x23,
     0xA0,0xBA,0xF1,0xA3,0xE0,0x97,0xAD,0x38};
  return (memcmp(iptc_uuid,state->ref->data,16) == 0);
}

/*****************************************************************************/
/*                      jpx_metanode::is_geojp2_uuid                         */
/*****************************************************************************/

bool
  jpx_metanode::is_geojp2_uuid() const
{
  if ((state == NULL) || (state->box_type != jp2_uuid_4cc) ||
      (state->rep_id != JX_REF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return false;
  kdu_byte geojp2_uuid[16] =
    {0xB1,0x4B,0xF8,0xBD,0x08,0x3D,0x4B,0x43,
     0xA5,0xAE,0x8C,0xD7,0xD5,0xA6,0xCE,0x03};
  return (memcmp(geojp2_uuid,state->ref->data,16) == 0);
}


/*****************************************************************************/
/*                    jpx_metanode::get_cross_reference                      */
/*****************************************************************************/

kdu_uint32
  jpx_metanode::get_cross_reference(jpx_fragment_list &frags) const
{
  if ((state == NULL) || (state->rep_id != JX_CROSSREF_NODE) ||
      (state->crossref == NULL) || (state->crossref->box_type == 0))
    return 0;
  frags = jpx_fragment_list(&(state->crossref->frag_list));
  return state->crossref->box_type;
}

/*****************************************************************************/
/*                         jpx_metanode::get_link                            */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_link(jpx_metanode_link_type &link_type,
                         bool try_to_resolve)
{
  jx_metanode *result = NULL;
  link_type = JPX_METANODE_LINK_NONE;
  if ((state != NULL) && (state->rep_id == JX_CROSSREF_NODE) &&
      (state->crossref != NULL))
    { 
      jx_crossref *cref = state->crossref;
      link_type = cref->link_type;
      if ((cref->metaloc != NULL) && try_to_resolve &&
          (state->flags & JX_METANODE_EXISTING))
        { 
          assert(link_type == JPX_METANODE_LINK_NONE);
          kdu_long link_pos = cref->metaloc->get_loc();
          state->manager->tree->load_recursive(link_pos);
          if (cref->metaloc != NULL)
            link_type = JPX_METANODE_LINK_PENDING;
          else
            link_type = cref->link_type; // It may have changed
        }
      result = cref->link;
      if ((result != NULL) && (result->flags & JX_METANODE_EXISTING) &&
          !(result->flags & JX_METANODE_BOX_COMPLETE))
        { 
          if (result->finish_reading() && (result->rep_id == 0) &&
              (result->parse_state == NULL))
            result->remove_empty_shell();
          result = cref->link; // May possibly have become NULL
          if ((result != NULL) && !(result->flags & JX_METANODE_BOX_COMPLETE))
            result = NULL;
        }
      if ((result != NULL) && (result->rep_id == 0))
        { // Link to an object that will never have an internal
          // representation -- typically a grouping node for structuring.
          cref->unlink();
          result = NULL;
          link_type = JPX_METANODE_LINK_NONE;
        }
    }
  return jpx_metanode(result);
}

/*****************************************************************************/
/*                       jpx_metanode::enum_linkers                          */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::enum_linkers(jpx_metanode last_linker)
{
  if ((state == NULL) || (state->linked_from == NULL))
    return jpx_metanode();
  jx_crossref *cref = state->linked_from;
  if ((last_linker.state != NULL) &&
      (last_linker.state->rep_id == JX_CROSSREF_NODE) &&
      (last_linker.state->crossref != NULL) &&
      (!(last_linker.state->flags & JX_METANODE_DELETED)) &&
      (last_linker.state->crossref->link == state))
    cref = last_linker.state->crossref->next_link;
  
  return jpx_metanode((cref==NULL)?NULL:cref->owner);
}

/*****************************************************************************/
/*                       jpx_metanode::get_existing                          */
/*****************************************************************************/

jp2_locator
  jpx_metanode::get_existing(jp2_family_src * &src)
{
  src = NULL;
  if ((state == NULL) || (state->rep_id != JX_REF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return jp2_locator();
  src = state->ref->src;
  return state->ref->src_loc;
}

/*****************************************************************************/
/*                        jpx_metanode::get_delayed                          */
/*****************************************************************************/

bool
  jpx_metanode::get_delayed(int &i_param, void * &addr_param) const
{
  if ((state == NULL) || (state->rep_id != JX_REF_NODE) ||
      (state->ref->src != NULL))
    return false;
  i_param = state->ref->i_param;
  addr_param = state->ref->addr_param;
  return true;
}

/*****************************************************************************/
/*                     jpx_metanode::count_descendants                       */
/*****************************************************************************/

bool
  jpx_metanode::count_descendants(int &count)
{
  if (state == NULL)
    return false;
  assert((state->parent == NULL) || (state->rep_id != 0));
     // We must never allow the application to obtain a reference to a
     // structuring node that we can flatten.

  count = 0;
  if ((state->flags & JX_METANODE_EXISTING) &&
      !(state->flags & JX_METANODE_DESCENDANTS_KNOWN))
    { 
      if (!(state->flags & JX_METANODE_CONTAINER_KNOWN))
        { 
          state->finish_reading();
          if (state->flags & JX_METANODE_DELETED)
            return 0; // Could happen
        }
      jx_metanode *scan=state->head, *last_counted=NULL, *next;
      for (; scan != NULL; scan=next)
        { // Try to read and flatten any grouping sub-boxes we may have while
          // we are counting.
          next = scan->next_sibling; // In case `scan' is deleted
          if ((scan->flags & JX_METANODE_EXISTING) &&
              (scan->parse_state != NULL) && scan->parse_state->is_generator)
            { // We don't count these, but we do have to count any extra
              // nodes that they generate
              if (scan->finish_reading() && (scan->rep_id == 0) &&
                  (scan->parse_state == NULL))
                scan->remove_empty_shell(); // Deletes `scan'
              jx_metanode *new_scan = state->head;
              if (last_counted != NULL)
                new_scan = last_counted->next_sibling;
              while ((new_scan != scan) && (new_scan != next))
                { 
                  count++;
                  last_counted = new_scan;
                  new_scan = new_scan->next_sibling;
                }
            }
          else
            { 
              count++;
              last_counted = scan;
            }
        }
      return (state->flags & JX_METANODE_DESCENDANTS_KNOWN)?true:false;
    }
  else
    { // Descendants are all known, so there are no generators left.  Just
      // count all nodes
      jx_metanode *scan;
      for (scan=state->head; scan != NULL; scan=scan->next_sibling)
        count++;
      return true;
    }
}

/*****************************************************************************/
/*                      jpx_metanode::get_descendant                         */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_descendant(int which)
{
  if ((state == NULL) || (which < 0))
    return jpx_metanode(NULL);
  assert((state->parent == NULL) || (state->rep_id != 0));
     // We should never let the application obtain a reference to a
     // structuring node that could be flattened.
  jx_metanode *node=state->head;
  for (; node != NULL; node = node->next_sibling)
    { 
      if ((node->flags & JX_METANODE_EXISTING) &&
          (node->parse_state != NULL) && node->parse_state->is_generator)
        continue; // Don't count generators at all
      if ((--which) < 0)
        break;
    }
  if ((node != NULL) && !node->is_externally_visible())
    node = NULL; // Never let the application obtain a reference to a
                 // structuring node or an incomplete node.
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                  jpx_metanode::find_descendant_by_type                    */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::find_descendant_by_type(int which, int num_box_types,
                                        const kdu_uint32 box_types[])
{
  if ((state == NULL) || (which < 0))
    return jpx_metanode(NULL);
  jx_metanode *node=state->head;
  for (; node != NULL; node = node->next_sibling)
    {
      bool found_match = (num_box_types == 0);
      for (int b=0; (!found_match) && (b < num_box_types); b++)
        found_match = (box_types[b] == node->box_type);
      if (found_match && node->is_externally_visible() && ((--which) < 0))
        break;
    }
  return jpx_metanode(node);  
}

/*****************************************************************************/
/*                    jpx_metanode::get_next_descendant                      */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_next_descendant(jpx_metanode ref, int limit_cmd,
                                    const kdu_uint32 *box_types)
{
  if (state == NULL)
    return jpx_metanode(NULL);
  assert((state->parent == NULL) || (state->rep_id != 0));
  jx_metanode **node_ref = NULL;
  if (ref.state == NULL)
    node_ref = &(state->head);
  else if (ref.state->parent == state)
    node_ref = &(ref.state->next_sibling);
  
  if ((node_ref != NULL) && (*node_ref == NULL) &&
      (state->flags & JX_METANODE_EXISTING) &&
      !(state->flags & JX_METANODE_CONTAINER_KNOWN))
    { // Maybe we can read more boxes from the current container.  We will
      // only try to do this once, so as to avoid redundant processing.
      state->finish_reading(); // Also works for the root of the hierarchy
    }
  
  jx_metanode *node = NULL;
  while ((node_ref != NULL) && ((node = *node_ref) != NULL))
    { 
      if ((node->flags & JX_METANODE_EXISTING) && (node->parse_state != NULL))
        { // We may have some more parsing to do
          if (node->parse_state->is_generator ||
              !node->is_externally_visible())
            { 
              if (node->finish_reading() && (node->rep_id == 0) &&
                  (node->parse_state == NULL))
                node->remove_empty_shell(); // Deletes `node'
              node = *node_ref;
            }
        }
      
      // Now see if we are ready to return yet, or if instead we should
      // advance `node_ref'.
      node_ref = NULL;
      if (node != NULL)
        { 
          bool node_acceptable = node->is_externally_visible();
          if (node_acceptable && (limit_cmd > 0))
            { // Check box types
              node_acceptable = false;
              if (box_types != NULL)
                for (int c=0; (c < limit_cmd) && !node_acceptable; c++)
                  if (node->box_type == box_types[c])
                    node_acceptable = true;
            }
          if (!node_acceptable)
            { 
              if (limit_cmd != 0)
                node_ref = &(node->next_sibling);
              node = NULL;
            }
        }
    }
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                    jpx_metanode::get_prev_descendant                      */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_prev_descendant(jpx_metanode ref, int limit_cmd,
                                    const kdu_uint32 *box_types)
{
  if (state == NULL)
    return jpx_metanode(NULL);
  assert((state->parent == NULL) || (state->rep_id != 0));

  if ((ref.state == NULL) && (state->tail == NULL) &&
      (state->flags & JX_METANODE_EXISTING) &&
      !(state->flags & JX_METANODE_CONTAINER_KNOWN))
    { // Maybe we can read more boxes from the current container.
      state->finish_reading(); // Also works for the root of the hierarchy
    }
  
  jx_metanode **node_ref = NULL;
  if (ref.state == NULL)
    node_ref = &(state->tail);
  else if (ref.state->parent == state)
    node_ref = &(ref.state->prev_sibling);
  
  jx_metanode *node = NULL;
  while ((node_ref != NULL) && ((node = *node_ref) != NULL))
    { 
      if ((node->flags & JX_METANODE_EXISTING) && (node->parse_state != NULL))
        { // We may have some more parsing to do
          if (node->parse_state->is_generator ||
              !node->is_externally_visible())
            { 
              if (node->finish_reading() && (node->rep_id == 0) &&
                  (node->parse_state == NULL))
                node->remove_empty_shell(); // Deletes `node'
              node = *node_ref;
            }
        }
      
      // Now see if we are ready to return yet, or if instead we should
      // advance `node_ref'.
      node_ref = NULL;
      if (node != NULL)
        { 
          bool node_acceptable = node->is_externally_visible();
          if (node_acceptable && (limit_cmd > 0))
            { // Check box types
              node_acceptable = false;
              if (box_types != NULL)
                for (int c=0; (c < limit_cmd) && !node_acceptable; c++)
                  if (node->box_type == box_types[c])
                    node_acceptable = true;
            }
          if (!node_acceptable)
            { 
              if (limit_cmd != 0)
                node_ref = &(node->prev_sibling);
              node = NULL;
            }
        }
    }
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                jpx_metanode::check_descendants_complete                   */
/*****************************************************************************/

bool
  jpx_metanode::check_descendants_complete(int num_box_types,
                                           const kdu_uint32 box_types[])
                                           const
{
  if (!(state->flags & JX_METANODE_EXISTING))
    return true;
  if ((state == NULL) || (!(state->flags & JX_METANODE_BOX_COMPLETE)) ||
      (!(state->flags & JX_METANODE_DESCENDANTS_KNOWN)))
    return false;
  jx_metanode *child=state->head;
  for (; child != NULL; child=child->next_sibling)
    if ((!(child->flags & JX_METANODE_BOX_COMPLETE)) ||
        ((child->rep_id == JX_CROSSREF_NODE) &&
         (child->crossref->link == NULL)))
      { // See if this child has one of the box types that are of interest
        if ((num_box_types == 0) || (child->box_type == 0) ||
            (child->rep_id == 0))
          return false;
        for (int b=0; b < num_box_types; b++)
          if (child->box_type == box_types[b])
            return false;
      }
  return true;
}

/*****************************************************************************/
/*                    jpx_metanode::get_sequence_index                       */
/*****************************************************************************/

kdu_long
  jpx_metanode::get_sequence_index() const
{
  if (state == NULL)
    return 0;
  else
    return state->sequence_index;
}

/*****************************************************************************/
/*                        jpx_metanode::get_parent                           */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::get_parent()
{
  return jpx_metanode((state==NULL)?NULL:state->parent);
}

/*****************************************************************************/
/*                      jpx_metanode::find_path_to                           */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::find_path_to(jpx_metanode target, int descending_flags,
                             int ascending_flags, int num_exclusion_categories,
                             const kdu_uint32 exclusion_box_types[],
                             const int exclusion_flags[],
                             bool unify_groups)
{
  jx_metanode *result;
  if ((state == NULL) || (target.state == NULL))
    result = NULL;
  else if ((((ascending_flags | descending_flags) &
             (JPX_PATH_TO_FORWARD | JPX_PATH_TO_REVERSE)) == 0) &&
           !unify_groups)
    { // Perform simple search for hierarchically related nodes
      jx_metanode *scan;
      result = (state == target.state)?state:NULL;
      if (ascending_flags & JPX_PATH_TO_DIRECT)
        for (scan=state; (scan != NULL) && (result==NULL); scan=scan->parent)
          if (scan == target.state)
            result = state->parent;
      if (descending_flags & JPX_PATH_TO_DIRECT)
        for (scan=target.state;
             (scan != NULL) && (result==NULL); scan=scan->parent)
          if (scan->parent == state)
            result = scan;
    }
  else
    result = state->find_path_to(target.state,descending_flags,ascending_flags,
                                 num_exclusion_categories,
                                 exclusion_box_types,exclusion_flags,
                                 unify_groups);
  return jpx_metanode(result);
    
}

/*****************************************************************************/
/*                      jpx_metanode::change_parent                          */
/*****************************************************************************/

bool
  jpx_metanode::change_parent(jpx_metanode new_parent)
{
  jx_check_metanode_before_change(state);
  if ((new_parent.state == NULL) ||
      (state->manager != new_parent.state->manager))
    { KDU_ERROR_DEV(e,0x11081204); e <<
      KDU_TXT("`jpx_metanode::change_parent' may not be used to move a "
              "metanode to an empty interface or between the metadata "
              "hierarchies managed by different objects.");
    }
  jx_check_metanode_before_add_child(new_parent.state);
  assert(state->flags & JX_METANODE_BOX_COMPLETE);
  if (new_parent.state == state->parent)
    return false;
  for (jx_metanode *test=new_parent.state; test != NULL; test=test->parent)
    if (test == state)
      return false;  
  if ((state->flags & JX_METANODE_EXISTING) && (state->parse_state != NULL))
    state->make_complete();
  jx_container_base *old_container=state->find_container();
  jx_container_base *new_container=new_parent.state->find_container();
  if ((old_container != new_container) &&
      !state->check_container_compatibility(new_container))
    { KDU_ERROR(e,0x11081205); e <<
      KDU_TXT("Atempting to move a metadata node across as a descendant "
              "of new metadata node that is embedded within a JPX "
              "container that is not compatible with the node to be "
              "moved or one of its descendants -- number list nodes "
              "may be embedded within JPX containers only if the "
              "compositing layers and/or codestreams that they reference "
              "are either top-level layers/codestreams or else "
              "layers/codestreams defined by the relevant container.");
    }
  state->flags &= ~JX_METANODE_EXISTING;
  jx_metanode *old_parent = state->parent;
  state->unlink_parent();
  if (state->rep_id == JX_ROI_NODE)
    state->regions->unlink();
  new_parent.state->insert_child(state,new_parent.state->tail);
  if (state->rep_id == JX_ROI_NODE)
    state->manager->link_to_libraries(state);
  state->flags |= JX_METANODE_ANCESTOR_CHANGED;
  state->append_to_touched_list(true);
  if (old_container != new_container)
    state->change_container(new_container);
  if (old_parent != NULL)
    old_parent->delete_useless_numlists();
  return true;
}

/*****************************************************************************/
/*                        jpx_metanode::add_numlist                          */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_numlist(int num_codestreams, const int *codestream_indices,
                            int num_layers, const int *layer_indices,
                            bool applies_to_rendered_result,
                            int container_id)
{
  jx_check_metanode_before_add_child(state);
  jx_container_base *container = NULL;
  if (container_id >= 0)
    { 
      if (state->parent != NULL)
        { KDU_ERROR_DEV(e,0x10081201); e <<
          KDU_TXT("Attempting to embed a new number list within a JPX "
                  "container via `jpx_metanode::add_numlist' -- this is "
                  "only allowed for number lists that will appear at the "
                  "top level of the metadata hierarchy.");
        }
      container = state->manager->find_container(container_id);
      if ((container == NULL) ||
          !container->check_compatibility(num_codestreams,codestream_indices,
                                          num_layers,layer_indices,false))
        { KDU_ERROR_DEV(e,0x10081202); e <<
          KDU_TXT("Attempting to embed a new number list within a JPX "
                  "container which either does not exist or is not "
                  "compatible with the compositing layer and/or codestream "
                  "indices to be recorded in the number list.");
        }
    }
  else
    container = state->find_container();
  jx_metanode *node = state->add_numlist(num_codestreams,codestream_indices,
                                         num_layers,layer_indices,
                                         applies_to_rendered_result,
                                         container);
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                        jpx_metanode::add_regions                          */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_regions(int num_regions, const jpx_roi *regions)
{
  jx_check_metanode_before_add_child(state);
  jx_metanode *node = new jx_metanode(state->manager);
  node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                  JX_METANODE_DESCENDANTS_KNOWN);
  node->box_type = jp2_roi_description_4cc;
  state->insert_child(node,state->tail); // Increments completed descendants
  node->rep_id = JX_ROI_NODE;
  node->regions = new jx_regions(node);
  node->regions->set_num_regions(num_regions);
  node->append_to_touched_list(false);
  
  int n;
  kdu_dims rect;
  kdu_coords bound_min, bound_lim, min, lim;
  for (n=0; n < num_regions; n++)
    {
      node->regions->regions[n] = regions[n];
      node->regions->regions[n].fix_inconsistencies();
      rect = node->regions->regions[n].region;
      min = rect.pos;
      lim = min + rect.size;
      if (n == 0)
        { bound_min = min; bound_lim = lim; }
      else
        { // Grow `bound' to include `rect'
          if (min.x < bound_min.x)
            bound_min.x = min.x;
          if (min.y < bound_min.y)
            bound_min.y = min.y;
          if (lim.x > bound_lim.x)
            bound_lim.x = lim.x;
          if (lim.y > bound_lim.y)
            bound_lim.y = lim.y;
        }
    }
  node->regions->bounding_region.region.pos = bound_min;
  node->regions->bounding_region.region.size = bound_lim - bound_min;
  double wd_max = 0.1;
  for (n=0; n < num_regions; n++)
    {
      double dummy, wd=0.0;
      node->regions->regions[n].measure_span(wd,dummy);
      wd_max = (wd > wd_max)?wd:wd_max;
    }
  node->regions->max_width = (int) ceil(wd_max);
  node->manager->link_to_libraries(node);
  return jpx_metanode(node);
}
  
/*****************************************************************************/
/*                         jpx_metanode::add_label                           */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_label(const char *text)
{
  jx_check_metanode_before_add_child(state);
  jx_metanode *node = new jx_metanode(state->manager);
  node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                  JX_METANODE_DESCENDANTS_KNOWN);
  node->box_type = jp2_label_4cc;
  state->insert_child(node,state->tail); // Increments completed descendants
  node->label = new char[strlen(text)+1];
  strcpy(node->label,text);
  node->rep_id = JX_LABEL_NODE;
  node->append_to_touched_list(false);
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                      jpx_metanode::change_to_label                        */
/*****************************************************************************/

void
  jpx_metanode::change_to_label(const char *text)
{
  jx_check_metanode_before_change(state);
  jx_container_base *old_numlist_container = NULL;
  switch (state->rep_id) {
    case JX_REF_NODE:
      if (state->ref != NULL)
        { 
          delete state->ref;
          state->ref = NULL;
        }
      break;
    case JX_NUMLIST_NODE:
      if (state->numlist != NULL)
        { 
          old_numlist_container = state->numlist->container;
          state->numlist->unlink();
          delete state->numlist;
          state->numlist = NULL;
        }
      break;
    case JX_ROI_NODE:
      if (state->regions != NULL)
        { 
          state->regions->unlink();
          delete state->regions;
          state->regions = NULL;
        }
      break;
    case JX_LABEL_NODE:
      if (state->label != NULL)
        { 
          delete[] state->label;
          state->label = NULL;
        }
      break;
    case JX_CROSSREF_NODE:
      if (state->crossref != NULL)
        { 
          delete state->crossref;
          state->crossref = NULL;
        }
      break;
    default:
      assert(0);
      break;
  };
  
  state->box_type = jp2_label_4cc;
  state->label = new char[strlen(text)+1];
  strcpy(state->label,text);  
  state->rep_id = JX_LABEL_NODE;
  
  if ((state->flags & JX_METANODE_EXISTING) && (state->parse_state != NULL))
    state->make_complete();
  state->flags &= ~JX_METANODE_EXISTING;
  state->flags |= JX_METANODE_CONTENTS_CHANGED;
  state->append_to_touched_list(true);
  jx_crossref *cref;
  for (cref=state->linked_from; cref != NULL; cref=cref->next_link)
    { 
      cref->box_type = state->box_type;
      cref->owner->append_to_touched_list(true);
    }
  if (state->parent != NULL)
    state->parent->check_roi_child_flags();
  if (old_numlist_container != state->find_container())
    { 
      state->change_container(NULL);
      state->move_old_top_container_numlist(old_numlist_container);
    }
}

/*****************************************************************************/
/*                        jpx_metanode::add_delayed                          */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_delayed(kdu_uint32 box_type, int i_param, void *addr_param)
{
  jx_check_metanode_before_add_child(state);
  jx_metanode *node = new jx_metanode(state->manager);
  node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                  JX_METANODE_DESCENDANTS_KNOWN |
                  JX_METANODE_CONTENTS_CHANGED);
  node->box_type = box_type;
  state->insert_child(node,state->tail); // Increments completed descendants
  node->rep_id = JX_REF_NODE;
  node->ref = new jx_metaref;
  node->ref->i_param = i_param;
  node->ref->addr_param = addr_param;
  node->append_to_touched_list(false);
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                     jpx_metanode::change_to_delayed                       */
/*****************************************************************************/

void
  jpx_metanode::change_to_delayed(kdu_uint32 box_type, int i_param,
                                  void *addr_param)
{
  jx_check_metanode_before_change(state);
  jx_container_base *old_numlist_container = NULL;
  switch (state->rep_id) {
    case JX_REF_NODE:
      if (state->ref != NULL)
        { 
          delete state->ref;
          state->ref = NULL;
        }
      break;
    case JX_NUMLIST_NODE:
      if (state->numlist != NULL)
        { 
          old_numlist_container = state->numlist->container;
          state->numlist->unlink();
          delete state->numlist;
          state->numlist = NULL;
        }
      break;
    case JX_ROI_NODE:
      if (state->regions != NULL)
        { 
          state->regions->unlink();
          delete state->regions;
          state->regions = NULL;
        }
      break;
    case JX_LABEL_NODE:
      if (state->label != NULL)
        { 
          delete[] state->label;
          state->label = NULL;
        }
      break;
    case JX_CROSSREF_NODE:
      if (state->crossref != NULL)
        { 
          delete state->crossref;
          state->crossref = NULL;
        }
      break;
    default:
      assert(0);
      break;
  };
  state->box_type = box_type;
  state->rep_id = JX_REF_NODE;
  state->ref = new jx_metaref;
  state->ref->i_param = i_param;
  state->ref->addr_param = addr_param;  
  if ((state->flags & JX_METANODE_EXISTING) && (state->parse_state != NULL))
    state->make_complete();
  state->flags &= ~JX_METANODE_EXISTING;
  state->flags |= JX_METANODE_CONTENTS_CHANGED;
  state->append_to_touched_list(true);
  jx_crossref *cref;
  for (cref=state->linked_from; cref != NULL; cref=cref->next_link)
    { 
      cref->owner->append_to_touched_list(true);
      cref->box_type = state->box_type;
    }
  if (state->parent != NULL)
    state->check_roi_child_flags();
  if (old_numlist_container != state->find_container())
    { 
      state->change_container(NULL);
      state->move_old_top_container_numlist(old_numlist_container);
    }  
}

/*****************************************************************************/
/*                         jpx_metanode::add_link                            */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_link(jpx_metanode target, jpx_metanode_link_type link_type,
                         bool avoid_duplicates)
{
  jx_check_metanode_before_add_child(state);
  if ((target.state == NULL) || (target.state->flags & JX_METANODE_DELETED))
    { KDU_ERROR_DEV(e,0x21040901); e <<
      KDU_TXT("Trying to add a metadata link to a metadata node which "
              "appears to have been deleted.");
    }
  if ((target.state->rep_id == JX_CROSSREF_NODE) &&
      ((target.state->crossref->link_type != JPX_GROUPING_LINK) ||
       (link_type != JPX_ALTERNATE_CHILD_LINK)))
    { KDU_ERROR(e,0x01060901); e <<
      KDU_TXT("Trying to add a metadata link to a metadata node which itself "
              "is a link.  This is legal only if the target node has "
              "the \"Grouping\" link-type (then the cross-reference can "
              "refer to its containing `asoc' box) and then only if the link "
              "you are trying to add has the \"Alternate Child\" link "
              "type (otherwise the cross-reference cannot refer to the "
              "`asoc' box).");
    }
  if (target.state->is_empty_numlist())
    { KDU_ERROR(e,0x19081201); e <<
      KDU_TXT("Trying to add a metadata link to an empty number list -- these "
              "will not be assigned a representation in a generated JPX "
              "file.");
    }
  jx_metapres *pres=NULL;
  if (!(target.state->flags & JX_METANODE_WRITTEN))
    target.state->manager->note_unwritten_link_target(target.state);
  else if (target.state->flags & JX_METANODE_PRESERVE)
    pres = target.state->preserve_state;
  else
    { KDU_ERROR_DEV(e,0x16081210); e <<
      KDU_TXT("Attempting to add a metadata link (cross reference box) to a "
              "metanode that has already been written and has not been "
              "marked for target address preservation.  You can fix this "
              "by calling `jpx_metanode::preserve_for_links'.");
    }

  jx_metanode *node = NULL;
  if (avoid_duplicates)
    for (node=state->head; node != NULL; node=node->next_sibling)
      if ((node->rep_id == JX_CROSSREF_NODE) && (node->crossref != NULL) &&
          (node->crossref->link == target.state) &&
          (node->crossref->link_type == link_type))
        return jpx_metanode(node);
  
  node = new jx_metanode(state->manager);
  node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                  JX_METANODE_DESCENDANTS_KNOWN |
                  JX_METANODE_CONTENTS_CHANGED);
  node->box_type = jp2_cross_reference_4cc;
  node->rep_id = JX_CROSSREF_NODE;
  node->crossref = new jx_crossref(node);
  node->crossref->link = target.state;
  node->crossref->link_type = link_type;
  if (pres != NULL)
    node->crossref->fill_write_info(pres);
  state->insert_child(node,state->tail);
  if (target.state->linked_from == NULL)
    target.state->linked_from = node->crossref;
  else
    node->crossref->append_to_list(target.state->linked_from);
  node->append_to_touched_list(false);
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                      jpx_metanode::change_to_link                         */
/*****************************************************************************/

void
  jpx_metanode::change_to_link(jpx_metanode target,
                               jpx_metanode_link_type link_type)
{ 
  jx_check_metanode_before_change(state);
  if ((target.state == NULL) || (target.state->flags & JX_METANODE_DELETED))
    { KDU_ERROR_DEV(e,0x01060902); e <<
      KDU_TXT("Trying to convert a metadata node into a link which points to "
              "another metadata node that appears to have been deleted.");
    }
  if (state->linked_from != NULL)
    { // See if the linkers are compatible
      bool is_compatible = (link_type == JPX_GROUPING_LINK);
      jx_crossref *cref;
      for (cref=state->linked_from; is_compatible && (cref != NULL);
           cref=cref->next_link)
        if (cref->link_type != JPX_ALTERNATE_CHILD_LINK)
          is_compatible = false;
      if (!is_compatible)
        { KDU_ERROR(e,0x01050901); e <<
          KDU_TXT("Attempting to convert a `jpx_metanode' entry in the "
                  "metadata hierarchy into a link, whereas there are other "
                  "nodes which are links and point to this node.  This is "
                  "legal only if the converted node is to have the "
                  "\"Grouping\" link-type and each of the existing linking "
                  "nodes has \"Alternate-child\" link-type -- otherwise the "
                  "representation within the JPX file format would require a "
                  "cross-reference box to point to another cross-reference "
                  "box.");
        }
    }
  if (target.state == state)
    { KDU_ERROR(e,0x01060903); e <<
      KDU_TXT("Attempting to convert a `jpx_metanode' entry in the "
              "metadata hierarchy into a link which points to itself.  "
              "This cannot be achieved legally, since a link node can "
              "be referenced by another link node only if the referenced "
              "node has the \"Grouping\" link-type and the referring "
              "node has the \"Alternate-child\" link-type.");
    }
  else if ((target.state->rep_id == JX_CROSSREF_NODE) &&
           ((target.state->crossref->link_type != JPX_GROUPING_LINK) ||
            (link_type != JPX_ALTERNATE_CHILD_LINK)))
    { KDU_ERROR(e,0x01050902); e <<
      KDU_TXT("Attempting to convert a `jpx_metanode' entry in the metadata "
              "hierarchy into a link to another node which is itself a link "
              "or other form of cross-reference box.  This is legal only if "
              "the target node has the \"Grouping\" link type and node being "
              "converted is being assigned an \"Alternate-child\" link "
              "type -- otherwise, the representation within the JPX file "
              "format would require a cross-reference box "
              "to point to another cross-reference box.");
      }
  if ((link_type != JPX_GROUPING_LINK) && (state->head != NULL))
    { KDU_ERROR(e,0x02050903); e <<
      KDU_TXT("Attempting to convert a `jpx_metanode' entry in the metadata "
              "hierarchy into a non-grouping link, where the node being "
              "converted already has descendants.  Link nodes which have "
              "descendants are automatically interpreted as grouping links.");
    }
  if (target.state->is_empty_numlist())
    { KDU_ERROR(e,0x19081202); e <<
      KDU_TXT("Trying to convert a `jpx_metanode' entry in the metadata "
              "hierarchy into a link to an empty number list -- these "
              "will not be assigned a representation in a generated JPX "
              "file.");
    }
  
  jx_metapres *pres=NULL;
  if (!(target.state->flags & JX_METANODE_WRITTEN))
    target.state->manager->note_unwritten_link_target(target.state);
  else if (target.state->flags & JX_METANODE_PRESERVE)
    pres = target.state->preserve_state;
  else
    { KDU_ERROR_DEV(e,0x16081211); e <<
      KDU_TXT("Attempting to create a link (cross reference box) to a "
              "metanode that has already been written and has not been "
              "marked for target address preservation.  You can fix this "
              "by calling `jpx_metanode::preserve_for_links'.");
    }

  jx_container_base *old_numlist_container = NULL;
  switch (state->rep_id) {
    case JX_REF_NODE:
      if (state->ref != NULL)
        { 
          delete state->ref;
          state->ref = NULL;
        }
      break;
    case JX_NUMLIST_NODE:
      if (state->numlist != NULL)
        { 
          old_numlist_container = state->numlist->container;
          state->numlist->unlink();
          delete state->numlist;
          state->numlist = NULL;
        }
      break;
    case JX_ROI_NODE:
      if (state->regions != NULL)
        { 
          state->regions->unlink();
          delete state->regions;
          state->regions = NULL;
        }
      break;
    case JX_LABEL_NODE:
      if (state->label != NULL)
        { 
          delete[] state->label;
          state->label = NULL;
        }
      break;
    case JX_CROSSREF_NODE:
      if (state->crossref != NULL)
        { 
          delete state->crossref;
          state->crossref = NULL;
        }
      break;
    default:
      assert(0);
      break;
  };
  
  state->box_type = jp2_cross_reference_4cc;
  state->rep_id = JX_CROSSREF_NODE;
  state->crossref = new jx_crossref(state);
  state->crossref->link = target.state;
  state->crossref->link_type = link_type;
  if (pres != NULL)
    state->crossref->fill_write_info(pres);
  if (target.state->linked_from == NULL)
    target.state->linked_from = state->crossref;
  else
    state->crossref->append_to_list(target.state->linked_from);
  if ((state->flags & JX_METANODE_EXISTING) && (state->parse_state != NULL))
    state->make_complete();
  state->flags &= ~JX_METANODE_EXISTING;
  state->flags |= JX_METANODE_CONTENTS_CHANGED;
  state->append_to_touched_list(true);
  jx_crossref *cref;
  for (cref=state->linked_from; cref != NULL; cref=cref->next_link)
    { 
      cref->owner->append_to_touched_list(true);
      cref->box_type = state->box_type;
    }  
  if (state->parent != NULL)
    state->parent->check_roi_child_flags();
  if (old_numlist_container != state->find_container())
    { 
      state->change_container(NULL);
      state->move_old_top_container_numlist(old_numlist_container);
    }
}

/*****************************************************************************/
/*                    jpx_metanode::preserve_for_links                       */
/*****************************************************************************/

void jpx_metanode::preserve_for_links()
{
  if ((state == NULL) || (state->manager->target == NULL))
    return;
  if ((state->flags & JX_METANODE_DELETED) ||
      (state->flags & JX_METANODE_WRITTEN))
    { KDU_ERROR_DEV(e,0x17081201); e <<
      KDU_TXT("Call to `jpx_metanode::preserve_links' is too late -- the node "
              "has already been deleted or written to the file!");
    }
  state->manager->note_unwritten_link_target(state);
  state->flags |= JX_METANODE_PRESERVE;
}

/*****************************************************************************/
/*                          jpx_metanode::add_copy                           */
/*****************************************************************************/

jpx_metanode
  jpx_metanode::add_copy(jpx_metanode src, bool recursive,
                         bool link_to_internal_copies)
{
  jx_check_metanode_before_add_child(state);
  assert((state != NULL) && (src.state != NULL));

  jpx_metanode result;
  jx_metaloc_manager &metaloc_manager = state->manager->metaloc_manager; 
  bool different_manager = (src.state->manager != state->manager);
  bool link_to_copies = link_to_internal_copies || different_manager;

  if (src.state->rep_id == JX_NUMLIST_NODE)
    { 
      int src_container_id = -1;
      if (state->parent == NULL)
        src_container_id = src.get_container_id();
      result = add_numlist(src.state->numlist->num_codestreams,
                           src.state->numlist->codestream_indices,
                           src.state->numlist->num_compositing_layers,
                           src.state->numlist->layer_indices,
                           src.state->numlist->rendered_result,
                           src_container_id);
    }
  else if (src.state->rep_id == JX_ROI_NODE)
    result = add_regions(src.state->regions->num_regions,
                         src.state->regions->regions);
  else if (src.state->rep_id == JX_LABEL_NODE)
    result = add_label(src.state->label);
  else if (src.state->rep_id == JX_REF_NODE)
    {
      jx_metanode *node = new jx_metanode(state->manager);
      node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                      JX_METANODE_DESCENDANTS_KNOWN);
      node->box_type = src.state->box_type;
      state->insert_child(node,state->tail);
      node->rep_id = JX_REF_NODE;
      node->ref = new jx_metaref;
      *(node->ref) = *(src.state->ref);
      node->append_to_touched_list(false);
      result = jpx_metanode(node);
    }
  else if (src.state->rep_id == JX_CROSSREF_NODE)
    { 
      if (src.state->crossref == NULL)
        return result;
      jx_metanode *src_link = src.state->crossref->link;
      if (src_link == NULL)
        return result; // Do not copy cross-reference nodes except known links
      jx_metanode *node = new jx_metanode(state->manager);
      node->flags |= (JX_METANODE_IS_COMPLETE | JX_METANODE_BOX_COMPLETE |
                      JX_METANODE_DESCENDANTS_KNOWN);
      node->box_type = src.state->box_type;
      state->insert_child(node,state->tail);
      node->rep_id = JX_CROSSREF_NODE;
      node->crossref = new jx_crossref(node);
      node->crossref->box_type = src.state->crossref->box_type;
      node->crossref->link_type = src.state->crossref->link_type;
      node->append_to_touched_list(false);
      if (!link_to_copies)
        {
          node->crossref->link = src_link;
          node->crossref->append_to_list(src_link->linked_from);
        }
      else
        {
          jx_metaloc *metaloc =
            metaloc_manager.get_copy_locator(src_link,true);
          if (metaloc->target == NULL)
            { // Temporarily make `metaloc' point to ourselves
              node->crossref->metaloc = metaloc;
              metaloc->target = node;
            }
          else if ((metaloc->target->rep_id == JX_CROSSREF_NODE) &&
                   (metaloc->target->crossref != NULL) &&
                   (metaloc->target->crossref->metaloc == metaloc))
            {
              node->crossref->append_to_list(metaloc->target->crossref);
              node->crossref->metaloc = metaloc;
            }
          else if (metaloc->target->flags & JX_METANODE_DELETED)
            { // We will not be able to complete the link
              node->safe_delete();
              return result; // This condition is extremely unlikely, since
                             // everything is normally copied at once.
            }
          else
            {
              if (metaloc->target->linked_from == NULL)
                metaloc->target->linked_from = node->crossref;
              else
                node->crossref->append_to_list(metaloc->target->linked_from);
              node->crossref->metaloc = metaloc; // So `link_found' works
              node->crossref->link_found();
            }
        }
      result = jpx_metanode(node);
    }
  else if (src.state->rep_id == JX_NULL_NODE)
    return result; // Return empty interface
  else
    assert(0);
  
  if (link_to_copies && (src.state->linked_from != NULL))
    { // Source object has links so we should set ourselves up to be able to
      // accept links -- perhaps there are even some waiting.  To do this, we
      // associate a `metaloc' object with the address of the node from which
      // we were copied.
      jx_crossref *scan;
      jx_metaloc *metaloc = metaloc_manager.get_copy_locator(src.state,true);
      if ((metaloc->target != NULL) &&
          (metaloc->target->rep_id == JX_CROSSREF_NODE) &&
          (metaloc->target->crossref->metaloc == metaloc))
        result.state->linked_from = metaloc->target->crossref;
      metaloc->target = result.state;
      for (scan=result.state->linked_from; scan != NULL; scan=scan->next_link)
        scan->link_found();
    }
  
  if (recursive)
    { 
      src.state->finish_reading();
      assert((src.state->parent == NULL) || (src.state->rep_id != 0));
        // Proves that we don't need to consider calling `remove_empty_shell'
      jx_metanode *last=NULL, *scan=src.state->head;
      while (scan != NULL)
        { 
          jx_metanode *next = scan->next_sibling;
          if (scan->finish_reading() && (scan->rep_id==0) &&
              (scan->parse_state == NULL))
            scan->remove_empty_shell();
          scan = (last == NULL)?(src.state->head):(last->next_sibling);
            // In case `scan' was deleted or parsing `scan' caused new
            // descendants to be inserted ahead of it -- for generators
          for (; scan != next; scan=scan->next_sibling)
            { 
              last = scan;
              if ((scan->rep_id != 0) &&
                  !(scan->flags & JX_METANODE_BOX_COMPLETE))
                continue; // Don't copy structuring nodes, or other nodes which
                          // have no representation of their own.
              result.add_copy(jpx_metanode(scan),true,link_to_internal_copies);
            }
        }
    }
  return result;
}

/*****************************************************************************/
/*                        jpx_metanode::delete_node                          */
/*****************************************************************************/

void
  jpx_metanode::delete_node()
{
  if (state == NULL)
    { 
      assert(0);
      return;
    }
  jx_metanode *parent = state->parent;
  state->safe_delete(); // Does all the unlinking and puts it on deleted list
  if (parent != NULL)
    parent->delete_useless_numlists();
  state = NULL;
}

/*****************************************************************************/
/*                        jpx_metanode::is_changed                           */
/*****************************************************************************/

bool
  jpx_metanode::is_changed() const
{
  return ((state != NULL) &&
          ((state->flags & JX_METANODE_CONTENTS_CHANGED) ||
           ((state->rep_id == JX_CROSSREF_NODE) &&
            (state->crossref != NULL) && (state->crossref->link != NULL) &&
            (state->crossref->link->flags & JX_METANODE_CONTENTS_CHANGED))));
}

/*****************************************************************************/
/*                     jpx_metanode::ancestor_changed                        */
/*****************************************************************************/

bool
  jpx_metanode::ancestor_changed() const
{
  return ((state != NULL) && (state->flags & JX_METANODE_ANCESTOR_CHANGED));  
}

/*****************************************************************************/
/*                       jpx_metanode::child_removed                         */
/*****************************************************************************/

bool
  jpx_metanode::child_removed() const
{
  return ((state != NULL) && (state->flags & JX_METANODE_CHILD_REMOVED));  
}

/*****************************************************************************/
/*                        jpx_metanode::is_deleted                           */
/*****************************************************************************/

bool
  jpx_metanode::is_deleted() const
{
  return ((state == NULL) || (state->flags & JX_METANODE_DELETED));  
}

/*****************************************************************************/
/*                          jpx_metanode::touch                              */
/*****************************************************************************/

void
  jpx_metanode::touch()
{
  if (state != NULL)
    state->append_to_touched_list(true);
}

/*****************************************************************************/
/*                       jpx_metanode::set_state_ref                         */
/*****************************************************************************/

void
  jpx_metanode::set_state_ref(void *ref)
{
  if (state != NULL)
    state->app_state_ref = ref;
}

/*****************************************************************************/
/*                       jpx_metanode::get_state_ref                         */
/*****************************************************************************/

void *
  jpx_metanode::get_state_ref()
{
  return (state==NULL)?NULL:(state->app_state_ref);
}

/*****************************************************************************/
/*                       jpx_metanode::generate_metareq                      */
/*****************************************************************************/

int
  jpx_metanode::generate_metareq(kdu_window *client_window,
                                 int num_box_types,
                                 const kdu_uint32 *box_types,
                                 int num_descend_types,
                                 const kdu_uint32 *descend_types,
                                 bool priority, kdu_int32 max_descend_depth,
                                 int qualifier)
{
  if ((state == NULL) || !(state->flags & JX_METANODE_EXISTING))
    return 0;
  if (state->flags & JX_METANODE_IS_COMPLETE)
    return 0; // All data under this node is already available
  if ((state->parent != NULL) && !(state->flags & JX_METANODE_CONTAINER_KNOWN))
    { // Get the most up-to-date version of the box.  This is more likely to
      // be a waste of processing time if we are already at the root of the
      // metadata hierarchy, because then we need to examine all incomplete
      // codestream and compositing layer header boxes of which there may be
      // a huge number.  That is why we exclude the case where `parent'==NULL.
      assert(state->rep_id != 0); // No need for `remove_empty_shell'
      state->finish_reading();
    }
  
  int num_metareqs = 0;
  if (!(state->flags & JX_METANODE_CONTAINER_KNOWN))
    { 
      if (state->parent == NULL)
        { // Root node is different
          client_window->add_metareq(0,qualifier,priority,0,false,0,0);
        }
      else
        { 
          assert(state->parse_state != NULL);
          client_window->add_metareq(0,qualifier,priority,0,false,
                                     state->parse_state->box_databin_id,
                                     state->parse_state->box_nesting_level);
        }
      num_metareqs++;
    }
  
  jx_metanode *child;
  kdu_long last_asoc_databin_id = -1; // Helps avoid duplicate requests
  int last_asoc_nesting_level = -1;
  kdu_uint32 last_full_box_type = 0;  // These are the box-type, the data-bin
  kdu_long last_full_databin_id = -1; // and the maximum depth used in the
  int last_full_nesting_level = -1;   // last metareq for full box contents.
  for (child=state->head; child != NULL; child=child->next_sibling)
    { 
      if (child->parse_state == NULL)
        continue;
      if (child->parse_state->is_generator)
        { 
          client_window->add_metareq(0,qualifier,priority,0,false,
                                     child->parse_state->box_databin_id,
                                     child->parse_state->box_nesting_level);
          client_window->add_metareq(jp2_association_4cc,qualifier,
                                     priority,16,false,
                                     child->parse_state->box_databin_id,
                                     child->parse_state->box_nesting_level);
          num_metareqs++;
          continue;
        }
      
      if (child->is_empty_numlist())
        { // Treat empty numlist as an extension of the top level
          jpx_metanode child_metanode(child);
          num_metareqs += 
            child_metanode.generate_metareq(client_window,num_box_types,
                                            box_types,num_descend_types,
                                            descend_types,priority,
                                            max_descend_depth,qualifier);
        }

      if ((child->flags & JX_METANODE_BOX_COMPLETE) &&
          !(child->flags & JX_METANODE_UNRESOLVED_LINK))
        continue;
      if (child->box_type == 0)
        { // Must be an asoc box whose first sub-box is not yet available
          if ((child->parse_state->asoc_databin_id == last_asoc_databin_id) &&
              (child->parse_state->asoc_nesting_level ==
               last_asoc_nesting_level))
            continue;
          last_asoc_databin_id = child->parse_state->asoc_databin_id;
          last_asoc_nesting_level = child->parse_state->asoc_nesting_level;
          client_window->add_metareq(jp2_association_4cc,qualifier,
                                     priority,16,false,
                                     last_asoc_databin_id,
                                     last_asoc_nesting_level);
          num_metareqs++;
          continue;
        }
      if ((child->box_type != last_full_box_type) ||
          (child->parse_state->box_databin_id != last_full_databin_id) ||
          (child->parse_state->box_nesting_level != last_full_nesting_level))
        { // See if this is a type we actually need
          if (num_box_types > 0)
            { 
              int b;
              for (b=0; b < num_box_types; b++)
                if (box_types[b] == child->box_type)
                  break;
              if (b == num_box_types)
                continue;
            }
          last_full_box_type = child->box_type;
          last_full_databin_id = child->parse_state->box_databin_id;
          last_full_nesting_level=child->parse_state->box_nesting_level;
          client_window->add_metareq(last_full_box_type,qualifier,
                                     priority,INT_MAX,false,
                                     last_full_databin_id,
                                     last_full_nesting_level);
          num_metareqs++;
          break;
        }
    }
  
  if ((num_descend_types == 0) || (max_descend_depth <= 0))
    return num_metareqs;
  
  for (child=state->head; child != NULL; child=child->next_sibling)
    {
      if ((child->box_type == 0) || (child->parse_state == NULL) ||
          (child->rep_id == 0) ||
          child->is_empty_numlist()) // We already visited empty numlists above 
        continue;
    
      if ((child->flags & JX_METANODE_BOX_COMPLETE) &&
          (child->flags & JX_METANODE_DESCENDANTS_KNOWN) &&
          (child->parse_state->incomplete_descendants == NULL))
        continue;
      for (int b=0; b < num_descend_types; b++)
        if (child->box_type == descend_types[b])
          {
            jpx_metanode child_metanode(child);
            num_metareqs +=
              child_metanode.generate_metareq(client_window,num_box_types,
                                              box_types,num_descend_types,
                                              descend_types,priority,
                                              max_descend_depth-1,
                                              qualifier);
          }
    }  

  return num_metareqs;
}

/*****************************************************************************/
/*                   jpx_metanode::generate_link_metareq                     */
/*****************************************************************************/

int
  jpx_metanode::generate_link_metareq(kdu_window *client_window,
                                      int num_box_types,
                                      const kdu_uint32 *box_types,
                                      int num_descend_types,
                                      const kdu_uint32 *descend_types,
                                      bool priority,
                                      kdu_int32 max_descend_depth,
                                      int qualifier)
{
  if ((state == NULL) || (state->rep_id != JX_CROSSREF_NODE) ||
      !(state->flags & JX_METANODE_EXISTING))
    return 0;
  jx_crossref *cref = state->crossref;
  if (cref == NULL)
    return 0;
  jx_metanode *tgt = cref->link;
  if (tgt == NULL)
    return 0;
  
  kdu_uint32 box_type = tgt->box_type;
  if ((box_type == 0) && (cref->box_type != jp2_association_4cc))
    box_type = cref->box_type;

  bool tgt_box_of_interest = true;
  bool descend_through_tgt_box = false;
  if (box_type != 0)
    {
      int b;
      if (num_box_types > 0)
        for (tgt_box_of_interest=false, b=0; b < num_box_types; b++)
          if (box_types[b] == box_type)
            { tgt_box_of_interest = true; break; }
      if (tgt_box_of_interest && (box_type == tgt->box_type) &&
          (max_descend_depth > 0))
        for (b=0; b < num_descend_types; b++)
          if (box_types[b] == box_type)
            descend_through_tgt_box = true;
    }
  if (!tgt_box_of_interest)
    return 0;
  
  if (!(tgt->flags & JX_METANODE_BOX_COMPLETE))
    { // We should at least have another go at parsing the target node
      if (tgt->finish_reading() && (tgt->rep_id == 0) &&
          (tgt->parse_state == NULL))
        { 
          tgt->remove_empty_shell();
          return 0;
        }
    }
  
  int num_metareqs = 0;
  if (!(tgt->flags & JX_METANODE_BOX_COMPLETE))
    { // Need to generate requests for the target box itself
      assert(tgt->parse_state != NULL);
      if (box_type != 0)
        client_window->add_metareq(box_type,qualifier,priority,INT_MAX,false,
                                   tgt->parse_state->box_databin_id,
                                   tgt->parse_state->box_nesting_level);
      else
        { // Must be missing the first sub-box header of an association box
          assert(tgt->parse_state->asoc_databin_id >= 0);
          client_window->add_metareq(jp2_association_4cc,qualifier,priority,
                                     16,false, // Enough for 1st sub-box header
                                     tgt->parse_state->asoc_databin_id,
                                     tgt->parse_state->asoc_nesting_level);
        }
      num_metareqs = 1;
    }

  if ((tgt->rep_id != 0) && descend_through_tgt_box)
    {
      jpx_metanode tgt_metanode(tgt);
      num_metareqs +=
        tgt_metanode.generate_metareq(client_window,num_box_types,box_types,
                                      num_descend_types,descend_types,
                                      priority,max_descend_depth-1,
                                      qualifier);
    }
  return num_metareqs;
}


/* ========================================================================= */
/*                            jx_metaloc_manager                             */
/* ========================================================================= */

/*****************************************************************************/
/*                  jx_metaloc_manager::jx_metaloc_manager                   */
/*****************************************************************************/

jx_metaloc_manager::jx_metaloc_manager()
{
  locator_heap = NULL;
  block_heap = NULL;
  root = allocate_block(); // Allocates an empty block of references
}
  
/*****************************************************************************/
/*                  jx_metaloc_manager::~jx_metaloc_manager                  */
/*****************************************************************************/

jx_metaloc_manager::~jx_metaloc_manager()
{
  jx_metaloc_alloc *elt;
  while ((elt=locator_heap) != NULL)
    {
      locator_heap = elt->next;
      delete elt;
    }
  jx_metaloc_block_alloc *blk;
  while ((blk=block_heap) != NULL)
    {
      block_heap = blk->next;
      delete blk;
    }
}

/*****************************************************************************/
/*                      jx_metaloc_manager::get_locator                      */
/*****************************************************************************/

jx_metaloc *
  jx_metaloc_manager::get_locator(kdu_long pos, bool create_if_necessary)
{
  jx_metaloc_block *container = root;
  int idx=0;
  
  jx_metaloc *elt = container;
  while ((elt != NULL) && (elt == container))
    {
      elt = NULL;
      for (idx=container->active_elts-1; idx >= 0; idx--)
        if (container->elts[idx]->loc <= pos)
          { elt = container->elts[idx];  break; }
      if ((elt != NULL) && elt->is_block())
        container = (jx_metaloc_block *) elt; // Descend into sub-block
    }
  if ((elt != NULL) && (elt->loc == pos))
    return elt; // Found an existing match

  if (!create_if_necessary)
    return NULL;
  
  // If we get here, we have to create a new element
  // Ideally, the element goes right after location `idx' within
  // `container'; if `idx' is -ve, we need to position it before the first
  // element.  We can shuffle elements within `container' around to make this
  // possible, unless `container' is full. In that case, we want to adjust
  // things while keeping the tree as balanced as possible.  To do that, we
  // insert a new container at the same level and redistribute the branches.
  // Inserting a new container may require recursive application of the
  // procedure all the way up to the root of the tree.  These operations are
  // all accomplished by the recursive function `insert_into_metaloc_block'.
  assert(container != NULL);
  elt = allocate_locator();
  elt->loc = pos;
  insert_into_metaloc_block(container,elt,idx);
  return elt;
}

/*****************************************************************************/
/*              jx_metaloc_manager::insert_into_metaloc_block                */
/*****************************************************************************/

void
  jx_metaloc_manager::insert_into_metaloc_block(jx_metaloc_block *container,
                                                jx_metaloc *elt, int idx)
{
  assert((idx >= -1) && (idx < container->active_elts));
  int n;
  if (container->active_elts < JX_METALOC_BLOCK_DIM)
    {
      for (n=container->active_elts-1; n > idx; n--)
        container->elts[n+1] = container->elts[n]; // Shuffle existing elements
      container->active_elts++;
      container->elts[idx+1] = elt;
      if (idx == -1)
        container->loc = elt->loc; // Container location comes from first elt
      if (elt->is_block())
        ((jx_metaloc_block *) elt)->parent = container;
    }
  else
    { // Container is full, need to insert a new container at the same level
      jx_metaloc_block *new_container = allocate_block();
      if (idx == (container->active_elts-1))
        { // Don't bother redistributing elements between `container' and
          // `new_container'; we are probably just discovering elements in
          // linear order here, so it is probably wasteful to plan for fuure
          // insertions at this point.
          new_container->elts[0] = elt;
          new_container->active_elts = 1;
          new_container->loc = elt->loc;
          if (elt->is_block())
            ((jx_metaloc_block *) elt)->parent = new_container;
        }
      else
        {
          new_container->active_elts = (container->active_elts >> 1);
          container->active_elts -= new_container->active_elts;
          for (n=0; n < new_container->active_elts; n++)
            {
              jx_metaloc *moving_elt=container->elts[container->active_elts+n];
              new_container->elts[n] = moving_elt;
              if (moving_elt->is_block())
                ((jx_metaloc_block *) moving_elt)->parent = new_container;
            }
          assert((container->active_elts > 0) &&
                 (new_container->active_elts > 0));
          new_container->loc = new_container->elts[0]->loc;
          if (idx < container->active_elts)
            insert_into_metaloc_block(container,elt,idx);
          else
            insert_into_metaloc_block(new_container,elt,
                                      idx-container->active_elts);
        }

      jx_metaloc_block *parent = container->parent;
      if (parent == NULL)
        { // Create new root node
          assert(container == root);
          root = allocate_block();
          root->active_elts = 2;
          root->elts[0] = container;
          root->elts[1] = new_container;
          root->loc = container->loc;
          container->parent = root;
          new_container->parent = root;
        }
      else
        {
          for (n=0; n < parent->active_elts; n++)
            if (parent->elts[n] == container)
              { // This condition should be matched by exactly one entry; if
                // not, nothing disastrous happens, we just don't insert the
                // new container and so we lose some references, but there
                // will be no memory leaks.
                insert_into_metaloc_block(parent,new_container,n);
                break;
              }
        }
    }
}


/* ========================================================================= */
/*                           jx_meta_target_level                            */
/* ========================================================================= */

/*****************************************************************************/
/*                jx_meta_target_level::~jx_meta_target_level                */
/*****************************************************************************/

jx_meta_target_level::~jx_meta_target_level()
{
  if (group != NULL)
    delete group;
}

/*****************************************************************************/
/*             jx_meta_target_level::get_preferred_group_length              */
/*****************************************************************************/

kdu_long
  jx_meta_target_level::get_preferred_group_length(kdu_long collection_bytes,
                                            kdu_long typical_collection_bytes,
                                            kdu_long remaining_level_bytes,
                                            int num_committed_groups)
{
  assert((num_committed_groups >= 0) && (num_committed_groups < 8));
  kdu_long num_elts=1, bytes_left=remaining_level_bytes-collection_bytes;
  if (bytes_left > 0)
    num_elts += bytes_left / typical_collection_bytes;
  kdu_long group_elts = 1; // Find largest group with 4^p elements
  kdu_long group_len = typical_collection_bytes;
  while ((group_elts*(8-num_committed_groups)) < num_elts)
    { // Add another grouping level, assuming 4 sub-groups in each level
      group_len = 16 + 4*group_len; // Allows for 16-byte box headers
      group_elts *= 4;
    }
  if (group_elts == 1)
    return collection_bytes;
  group_len += collection_bytes - typical_collection_bytes;
  if (group_len > (remaining_level_bytes-8))
    { // Skip `free' box at end of current level; check group worthwhile
      group_len = remaining_level_bytes;
      if (group_len < (collection_bytes+typical_collection_bytes+16))
        group_len = collection_bytes;
    }
  return group_len;
}

/*****************************************************************************/
/*                   jx_meta_target_level::fit_collection                    */
/*****************************************************************************/

kdu_long
  jx_meta_target_level::fit_collection(kdu_long collection_bytes,
                                       kdu_long typical_collection_bytes,
                                       kdu_long preferred_level_bytes,
                                       kdu_long expected_start_pos,
                                       bool commit)
{
  if ((group != NULL) && group->is_committed())
    { // Pass request down immediately -- no need to compute anything, unless
      // the group cannot accommodate the collection, in which case we
      // delete it.
      assert(this->is_committed());
      kdu_long result =
       group->fit_collection(collection_bytes,typical_collection_bytes,
                             0,0,commit); // 3'rd & 4'th args ignored by
                                          // committed `group'
      if (result >= 0)
        return result;
      delete group; // We have no further use for the existing descendant
      group = NULL; // `group', but we might need to create another one.
    }
  
  int committed_hdr_len;
  kdu_long committed_content_len, committed_start_pos;
  if (this->is_committed())
    { 
      committed_hdr_len = this->header_length;
      committed_content_len = this->content_bytes;
      committed_start_pos = this->box_start_pos;
    }
  else
    { // Derive expected values for committed start, header & content lengths
      committed_start_pos = expected_start_pos;
      committed_content_len = preferred_level_bytes - (committed_hdr_len=8);
#ifdef KDU_LONG64
      if ((preferred_level_bytes >> 32) > 0)
        committed_content_len = preferred_level_bytes - (committed_hdr_len=16);
#endif
    }
  kdu_long remaining_content_len = committed_content_len - committed_bytes;
  if (remaining_content_len < (collection_bytes+8)) // Need room for `free' box
    return -1;

  if (commit && !this->is_committed())
    { // Commit the current object
      if (parent == NULL)
        { 
          kdu_long pos = target->open_top_box(&box,jp2_group_4cc,0);
          if (pos != committed_start_pos)
            assert(0);
        }
      else
        { 
          assert(committed_start_pos ==
                 (parent->box_start_pos + parent->header_length +
                  parent->committed_bytes));
          kdu_long bytes_left =
            parent->box.reopen(jp2_group_4cc,parent->committed_bytes);
          
          if (parent->content_bytes != (parent->committed_bytes+bytes_left))
            assert(0);
          box.open(&(parent->box),jp2_group_4cc);
        }
      if (committed_hdr_len > 8)
        box.use_long_header();
      box.write_free_and_close(committed_content_len);
      this->box_start_pos = committed_start_pos;
      this->header_length = committed_hdr_len;
      this->content_bytes = committed_content_len;
      if (parent != NULL)
        { 
          parent->num_committed_groups++;
          parent->committed_bytes += header_length+content_bytes;
          kdu_long bytes_left = parent->content_bytes-parent->committed_bytes;
          assert((bytes_left == 0) || (bytes_left >= 8));
          if (bytes_left > 0)
            parent->box.write_free_and_close(bytes_left);
          else
            parent->box.close();
        }
    }
  
  kdu_long preferred_group_len =
    get_preferred_group_length(collection_bytes,typical_collection_bytes,
                               remaining_content_len,num_committed_groups);
  kdu_long expected_next_pos = (committed_start_pos + committed_hdr_len +
                                this->committed_bytes);
  assert((group == NULL) || !group->is_committed());
  if (preferred_group_len > collection_bytes)
    { // We can afford to record the collection in a group; there might be
      // an uncommitted one already that will get resized automatically.
      assert(preferred_group_len >= (collection_bytes+8));
      if (group == NULL)
        group = new jx_meta_target_level(target,this);
      kdu_long result =
        group->fit_collection(collection_bytes,typical_collection_bytes,
                              preferred_group_len,expected_next_pos,commit);
      assert(result > 0);
      return result;
    }
  else
    { // We cannot afford to embed the collection within a group
      assert(preferred_group_len == collection_bytes);
      if (group != NULL)
        { 
          delete group;
          group = NULL;
        }
      return expected_next_pos;
    }
}

/*****************************************************************************/
/*                  jx_meta_target_level::write_collection                   */
/*****************************************************************************/

void
  jx_meta_target_level::write_collection(const jp2_output_box *src)
{
  assert(this->is_committed()); // Must be committed before writing
  if (group != NULL)
    group->write_collection(src);
  else
    { 
      kdu_long collection_bytes =
        (src->get_box_length() - src->get_header_length());
      if (collection_bytes <= 0)
        return;
      kdu_long new_committed_bytes = this->committed_bytes + collection_bytes;
      kdu_long bytes_left = content_bytes - new_committed_bytes;
      assert((bytes_left == 0) || (bytes_left >= 8));
      box.reopen(jp2_group_4cc,this->committed_bytes);
      
      // Later, we should replace the following lines with a special
      // `src->write_box' call that takes a super-box
      kdu_long buf_len = 0;
      const kdu_byte *buf = src->get_contents(buf_len);
      assert(buf_len == collection_bytes);
      box.write(buf,(int) collection_bytes);
      
      this->committed_bytes = new_committed_bytes;
      if (bytes_left > 0)
        box.write_free_and_close(bytes_left);
      else
        box.close();
    }
}


/* ========================================================================= */
/*                              jx_meta_target                               */
/* ========================================================================= */

/*****************************************************************************/
/*                       jx_meta_target::jx_meta_target                      */
/*****************************************************************************/

jx_meta_target::jx_meta_target(jx_target *tgt)
{
  this->target = tgt;
  last_simulation_phase = -1;
  collector_start_pos = 0;
  group = NULL;
  num_written_collections = 0;
  predicted_collection_bytes = 0;
  typical_collection_bytes = 0;
  group_start_pos = 0;
  preferred_group_collections = 0;
}

/*****************************************************************************/
/*                      jx_meta_target::~jx_meta_target                      */
/*****************************************************************************/

jx_meta_target::~jx_meta_target()
{
  if (group != NULL)
    delete group;
}

/*****************************************************************************/
/*                        jx_meta_target::open_top_box                       */
/*****************************************************************************/

kdu_long
  jx_meta_target::open_top_box(jp2_output_box &box, kdu_uint32 box_type,
                               int simulation_phase)
{
  if (last_simulation_phase != simulation_phase)
    { // Start collecting from scratch
      assert(last_simulation_phase < 0); // Must invoke `close_collector' first
      last_simulation_phase = simulation_phase;
      if (num_written_collections == 0)
        collector_start_pos = target->open_top_box(NULL,0,simulation_phase);
      else
        { 
          if (group == NULL)
            { 
              group = new jx_meta_target_level(target,NULL);
              preferred_group_collections = 3;
            }
          if (!group->is_committed())
            group_start_pos = target->open_top_box(NULL,0,simulation_phase);
          collector_start_pos =
            group->fit_collection(predicted_collection_bytes,
                                  typical_collection_bytes,
                                  typical_collection_bytes *
                                  preferred_group_collections,
                                  group_start_pos,false);
          if (collector_start_pos < 0)
            { // Need to open a new group
              assert(group->is_committed()); // Uncommitted groups can resize
              delete group;
              group = NULL; // Just in case
              group = new jx_meta_target_level(target,NULL);
              group_start_pos = target->open_top_box(NULL,0,simulation_phase);
              preferred_group_collections =
                1 + 2*(preferred_group_collections-1);
              collector_start_pos =
                group->fit_collection(predicted_collection_bytes,
                                      typical_collection_bytes,
                                      typical_collection_bytes *
                                      preferred_group_collections,
                                      group_start_pos,false);
              assert(collector_start_pos > 0);
            }
        }
      collector.open(jp2_group_4cc);
         // Box-type is irrelevant, but we have to give one.  In practice, we
         // are going to use `get_contents' with this box to extract the
         // collected data and write it directly within `close_collector'.
    }
  
  assert(collector_start_pos > 0);
  kdu_long box_start_pos = 0;
  collector.get_contents(box_start_pos);
  box_start_pos += collector_start_pos;
  box.open(&collector,box_type);
  return box_start_pos;
}

/*****************************************************************************/
/*                      jx_meta_target::close_collector                      */
/*****************************************************************************/

bool jx_meta_target::close_collector(bool need_correct_file_positions)
{
  if (last_simulation_phase < 0)
    return true; // Nothing collected; nothing to do
  predicted_collection_bytes =
    collector.get_box_length() - collector.get_header_length();
  if (predicted_collection_bytes > typical_collection_bytes)
    typical_collection_bytes = predicted_collection_bytes;
  
  bool success = true;
  if (group == NULL)
    { // Write collection directly to the top level of the file
      if (collector_start_pos !=
          target->open_top_box(NULL,0,last_simulation_phase))
        { // File location has changed; this should not happen when writing
          // collected data directly into the top level of the file.
          assert(0);
          if (need_correct_file_positions)
            success = false;
        }
      if ((last_simulation_phase == 0) && success)
        target->write_collected_boxes(&collector);
    }
  else
    { // Write collection to `group'
      if (!group->is_committed())
        group_start_pos = target->open_top_box(NULL,0,last_simulation_phase);      
      kdu_long pos =
        group->fit_collection(predicted_collection_bytes,
                              typical_collection_bytes,
                              typical_collection_bytes *
                              preferred_group_collections,
                              group_start_pos,(last_simulation_phase==0));
      if (pos != collector_start_pos)
        { 
          if (need_correct_file_positions)
            success = false;
          collector_start_pos = pos;
        }
      if ((last_simulation_phase == 0) && success)
        group->write_collection(&collector);
    }
          
  if (last_simulation_phase == 0)
    { // Update collection statistics going forward
      num_written_collections++;
      typical_collection_bytes +=
        (predicted_collection_bytes - typical_collection_bytes) >> 2;
    }
  
  last_simulation_phase = -1;
  collector.close();
  collector_start_pos = 0;
  
  return success;
}


/* ========================================================================= */
/*                             jx_meta_manager                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      jx_meta_manager::jx_meta_manager                     */
/*****************************************************************************/

jx_meta_manager::jx_meta_manager()
{
  ultimate_src = NULL;
  source = NULL;
  target = NULL;
  meta_target = NULL;
  containers = NULL;
  tree = NULL;
  last_added_node = NULL;
  last_sequence_index = -1;
  flatten_free_asocs = true;
  group_with_free_asocs = false;
  num_filter_box_types = max_filter_box_types = 7;
  filter_box_types = new kdu_uint32[max_filter_box_types];
  filter_box_types[0] = jp2_label_4cc;
  filter_box_types[1] = jp2_xml_4cc;
  filter_box_types[2] = jp2_iprights_4cc;
  filter_box_types[3] = jp2_number_list_4cc;
  filter_box_types[4] = jp2_roi_description_4cc;
  filter_box_types[5] = jp2_uuid_4cc;
  filter_box_types[6] = jp2_cross_reference_4cc;
  deleted_nodes = NULL;
  touched_head = touched_tail = NULL;
  first_unwritten = NULL;
  last_to_write = NULL;
  last_unwritten_with_link_targets = NULL;
  write_in_progress = false;
  simulation_phase = 0;
  last_file_pos = -1;
}

/*****************************************************************************/
/*                         jx_meta_manager::tear_down                        */
/*****************************************************************************/

void jx_meta_manager::tear_down()
{
  if (tree != NULL)
    { 
      tree->safe_delete();
      assert(tree == NULL);
      tree = NULL; // Just in case
    }
  if (filter_box_types != NULL)
    { 
      delete[] filter_box_types;
      filter_box_types = NULL;
    }
  num_filter_box_types = 0;
  max_filter_box_types = 0;
  jx_metanode *node;
  while ((node=deleted_nodes) != NULL)
    {
      deleted_nodes = node->next_sibling;
      delete node;
    }
  if (meta_target != NULL)
    { 
      delete meta_target;
      meta_target = NULL;
    }
  source = NULL;
  target = NULL;
  containers = NULL;
  last_added_node = NULL;
  touched_head = touched_tail = NULL;
  first_unwritten = last_to_write = NULL;
  last_unwritten_with_link_targets = NULL;
  write_in_progress = false;
  simulation_phase = 0;
  last_file_pos = 0;
}

/*****************************************************************************/
/*                     jx_meta_manager::test_box_filter                      */
/*****************************************************************************/

bool
  jx_meta_manager::test_box_filter(kdu_uint32 type)
{
  if ((type == jp2_association_4cc) || (type == jp2_group_4cc))
    return true;
  if (num_filter_box_types == 0)
    return true; // Accept everything
  for (int n=0; n < num_filter_box_types; n++)
    if (type == filter_box_types[n])
      return true;
  return false;
}

/*****************************************************************************/
/*                    jx_meta_manager::link_to_libraries                     */
/*****************************************************************************/

void jx_meta_manager::link_to_libraries(jx_metanode *node)
{
  if (node->rep_id == JX_NUMLIST_NODE)
    { 
      assert((node->numlist != NULL) &&
             (node->numlist->first_identical != NULL));
      numlists.add(node->numlist);
    }
  else if (node->rep_id == JX_ROI_NODE)
    { 
      assert(node->regions != NULL);
      jx_metanode *nl_node;
      for (nl_node=node->parent; nl_node != NULL; nl_node=nl_node->parent)
        if (nl_node->rep_id == JX_NUMLIST_NODE)
          break;
      jx_numlist *first_identical = NULL;
      if (nl_node != NULL)
        { // Find the head of the list of identical numlists
          jx_numlist *numlist = nl_node->numlist;
          assert(numlist != NULL);
          first_identical = numlist->first_identical;
        }
      if (first_identical == NULL)
        unassociated_regions.add(node->regions,false);
      else
        { 
          jx_region_library *lib = first_identical->region_library;
          if (lib == NULL)
            { 
              first_identical->region_library = lib = new jx_region_library;
              lib->representative_numlist = first_identical;
            }
          assert(lib->representative_numlist == first_identical);
          lib->add(node->regions,false);
        }
    }
}

/*****************************************************************************/
/*                     jx_meta_manager::find_container                       */
/*****************************************************************************/

jx_container_base *
  jx_meta_manager::find_container(int container_id)
{
  jx_container_base *scan = containers;
  for (; (scan != NULL) && (scan->get_num_base_layers() > 0); scan=scan->next)
    { 
      int idx = scan->get_id();
      if (idx == container_id)
        return scan;
      if (idx > container_id)
        break;
    }
  return NULL;
}

/*****************************************************************************/
/*                     jx_meta_manager::write_metadata                       */
/*****************************************************************************/

jp2_output_box *
  jx_meta_manager::write_metadata(jx_metanode *last_node,
                                  int *i_param, void **addr_param)
{
  if (first_unwritten == NULL)
    return NULL;
  assert(target != NULL);
  jp2_output_box *interrupted = NULL;
  if (!write_in_progress)
    { // Set things up
      assert(last_to_write == NULL);
      assert(simulation_phase == 0);
      last_to_write = last_node;
      if (last_unwritten_with_link_targets != NULL)
        { // Make sure we include this one in simulation
          simulation_phase = 1;
          jx_metanode *scan = last_to_write;
          for (; scan != NULL; scan=scan->next_sibling)
            if (scan == last_unwritten_with_link_targets)
              { 
                last_to_write = scan;
                break;
              }
        }
      if ((last_to_write != NULL) &&
          last_to_write->is_top_container_numlist())
        last_to_write = last_to_write->numlist->container->last_metanode;
      assert(last_file_pos < 0);
      last_file_pos = 0;
      tree->clear_write_state(true);
    }
  write_in_progress = true; // In case we get interrupted
  interrupted = tree->write(NULL,i_param,addr_param,last_file_pos);
  if (interrupted != NULL)
    return interrupted;
  bool success = meta_target->close_collector(simulation_phase != 0);
  if (!success)
    { 
      assert(simulation_phase == 1);
      simulation_phase = 2;
      tree->clear_write_state(true);
      last_file_pos = 0;
      interrupted = tree->write(NULL,i_param,addr_param,last_file_pos);
      if (interrupted != NULL)
        return interrupted;
      success = meta_target->close_collector(true);
      assert(success); // Cannot need more than one simulation phase
    }
  if (simulation_phase != 0)
    { // Finished simulation phase
      simulation_phase = 0;
      tree->clear_write_state(false);
      last_file_pos = 0;
      interrupted = tree->write(NULL,i_param,addr_param,last_file_pos);
      if (interrupted != NULL)
        return interrupted;
      success = meta_target->close_collector(false);
      assert(success);
    }
  write_in_progress = false;
  last_file_pos = -1;
  if (last_to_write == NULL)
    first_unwritten = NULL;
  else if ((first_unwritten = last_to_write->next_sibling) != NULL)
    tree->flags &= ~JX_METANODE_WRITTEN;
  last_to_write = NULL;
  last_unwritten_with_link_targets = NULL;
  return NULL;
}


/* ========================================================================= */
/*                            jpx_meta_manager                               */
/* ========================================================================= */

/*****************************************************************************/
/*                    jpx_meta_manager::set_behaviour                        */
/*****************************************************************************/

void jpx_meta_manager::set_behaviour(int parsing_behaviour,
                                     int writing_behaviour)
{
  state->flatten_free_asocs =
    ((parsing_behaviour & JPX_METAREAD_FLATTEN_FREE_ASOCS) != 0);
  state->group_with_free_asocs =
    ((writing_behaviour & JPX_METAWRITE_USE_FREE_ASOCS) != 0);
}

/*****************************************************************************/
/*                    jpx_meta_manager::set_box_filter                       */
/*****************************************************************************/

void jpx_meta_manager::set_box_filter(int num_types, kdu_uint32 *types)
{ 
  assert((state != NULL) && (state->filter_box_types != NULL));
  if (num_types > state->max_filter_box_types)
    { 
      int new_max_types = state->max_filter_box_types + num_types;
      kdu_uint32 *new_types = new kdu_uint32[new_max_types];
      delete[] state->filter_box_types;
      state->filter_box_types = new_types;
      state->max_filter_box_types = new_max_types;
      for (int n=0; n < num_types; n++)
        state->filter_box_types[n] = types[n];
      state->num_filter_box_types = num_types;
    }
}

/*****************************************************************************/
/*                      jpx_meta_manager::access_root                        */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::access_root()
{
  jpx_metanode result;
  if (state != NULL)
    result = jpx_metanode(state->tree);
  return result;
}

/*****************************************************************************/
/*                      jpx_meta_manager::locate_node                        */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::locate_node(kdu_long file_pos)
{
  jx_metaloc *metaloc = NULL;
  if (state != NULL)
    {
      metaloc = state->metaloc_manager.get_locator(file_pos,false);
      if ((metaloc != NULL) && (metaloc->target != NULL))
        { 
          if (metaloc->target->rep_id == 0)
            metaloc = NULL; // Make sure we don't ever give application access
                  // a node without an internal representation -- this is
                  // typically a grouping node used for structuring.
          else if ((metaloc->target->rep_id == JX_CROSSREF_NODE) &&
                   (metaloc->target->crossref != NULL) &&
                   (metaloc->target->crossref->metaloc == metaloc))
            metaloc = NULL; // Locator holding info for an unresolved link
        }
    }
  return jpx_metanode((metaloc == NULL)?NULL:(metaloc->target));
}

/*****************************************************************************/
/*                   jpx_meta_manager::get_touched_nodes                     */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::get_touched_nodes()
{
  jx_metanode *node=NULL;
  while ((state != NULL) && ((node=state->touched_head) != NULL))
    { 
      assert(node->prev_touched == NULL);
      if ((state->touched_head = node->next_touched) == NULL)
        {
          assert(node == state->touched_tail);
          state->touched_tail = NULL;
        }
      else
        state->touched_head->prev_touched = NULL;
      node->next_touched = NULL;
      if ((node->parent == NULL) || (node->rep_id != 0))
        break; // Don't give the application access to nodes without an
               // internal representation -- typically structuring nodes.
    }
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                  jpx_meta_manager::peek_touched_nodes                     */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::peek_touched_nodes(kdu_uint32 box_type,
                                       jpx_metanode last_peeked)
{
  jx_metanode *node=NULL;
  if (state != NULL)
    {
      node = last_peeked.state;
      if (node == NULL)
        node = state->touched_head;
      else if ((node->prev_touched == NULL) && (node != state->touched_head))
        node = NULL;
      else
        node = node->next_touched;
      for (; node != NULL; node=node->next_touched)
        if (((node->parent == NULL) || (node->rep_id != 0)) &&
            ((box_type == 0) || (node->box_type == box_type)))
          break;
    }
  return jpx_metanode(node);
}

/*****************************************************************************/
/*              jpx_meta_manager::peek_and_clear_touched_nodes               */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::peek_and_clear_touched_nodes(int num_box_types,
                                                 kdu_uint32 box_types[],
                                                 jpx_metanode last_peeked)
{
  if (state != NULL)
    { 
      jx_metanode *next_node, *node = last_peeked.state;
      if (node == NULL)
        node = state->touched_head;
      else if ((node->prev_touched == NULL) && (node != state->touched_head))
        node = NULL;
      else
        node = node->next_touched;
      for (; node != NULL; node=next_node)
        { 
          next_node = node->next_touched;
          if ((node->parent != NULL) && (node->rep_id == 0))
            continue; // Don't have an internal representation yet
          for (int n=0; n < num_box_types; n++)
            if (node->box_type == box_types[n])
              return jpx_metanode(node);
          assert(node->prev_touched == NULL);
          if (node->prev_touched == NULL)
            state->touched_head = next_node;
          else
            node->prev_touched->next_touched = next_node;
          if (next_node != NULL)
            next_node->prev_touched = node->prev_touched;
          node->next_touched = node->prev_touched = NULL;
        }
    }
  return jpx_metanode(); // Failed to find anything
}

/*****************************************************************************/
/*                         jpx_meta_manager::copy                            */
/*****************************************************************************/

void
  jpx_meta_manager::copy(jpx_meta_manager src)
{
  assert((state != NULL) && (src.state != NULL));
  int dummy_count;
  src.access_root().count_descendants(dummy_count); // Finish top level parsing
  jpx_metanode tree = access_root();
  jx_metanode *scan;
  for (scan=src.state->tree->head; scan != NULL; scan=scan->next_sibling)
    tree.add_copy(jpx_metanode(scan),true);
}

/*****************************************************************************/
/*                  jpx_meta_manager::reset_copy_locators                    */
/*****************************************************************************/

void
  jpx_meta_manager::reset_copy_locators(jpx_metanode src, bool recursive,
                                        bool fixup_unresolved_links)
{
  if (src.state->manager != state)
    fixup_unresolved_links = false;
  if (src.state->linked_from != NULL)
    { // Source object has links so we may have a copy locator for it
      jx_metaloc *metaloc =
        state->metaloc_manager.get_copy_locator(src.state,false);
      if (metaloc != NULL)
        {
          jx_crossref *scan, *next;
          if ((metaloc->target != NULL) &&
              (metaloc->target->rep_id == JX_CROSSREF_NODE) &&
              (metaloc->target->crossref->metaloc == metaloc))
            for (scan=metaloc->target->crossref; scan != NULL; scan=next)
              { // These cross-references all failed to resolve during a
                // previous copy.
                next = scan->next_link;
                scan->next_link = NULL;
                scan->metaloc = NULL;
                if (fixup_unresolved_links)
                  {
                    scan->link = src.state;
                    if (src.state->linked_from == NULL)
                      src.state->linked_from = scan;
                    else
                      scan->append_to_list(src.state->linked_from);         
                  }
              }
          metaloc->target = NULL;
        }
    }
  
  if (recursive)
    {
      jx_metanode *scan;
      for (scan=src.state->head; scan != NULL; scan=scan->next_sibling)
        reset_copy_locators(jpx_metanode(scan),true,fixup_unresolved_links);
    }
}

/*****************************************************************************/
/*                     jpx_meta_manager::load_matches                        */
/*****************************************************************************/

bool
  jpx_meta_manager::load_matches(int num_codestreams,
                                 const int codestream_indices[],
                                 int num_compositing_layers,
                                 const int layer_indices[])
{
  if ((state->tree == NULL) || !(state->tree->flags & JX_METANODE_EXISTING))
    return false;
  
  jx_metanode *last_one = state->last_added_node;

  // Start by reading as many top-level boxes as possible
  while (!state->source->is_top_level_complete())
    if (!state->source->parse_next_top_level_box())
      break;

  // Now walk through each of the incomplete descendants of the root of
  // the metadata hierarchy, skipping over those that represent number lists
  // which are excluded on the basis of the supplied arguments, and invoking
  // `load_recursive' on all the rest.
  jx_metanode *tree = state->tree;
  jx_metanode *scan=NULL;
  if (tree->parse_state != NULL)
    scan = tree->parse_state->incomplete_descendants;
  if (scan == NULL)
    return (last_one != state->last_added_node);
  jx_incomplete_metanode_walker walker;
  walker.attach(scan);
  try { // Make absolutely sure the `walker' detaches, no matter what
    while ((scan = walker.advance()) != NULL)
      { 
        if (scan->flags & JX_METANODE_IS_COMPLETE)
          continue;
        if ((scan->rep_id == JX_NUMLIST_NODE) && (scan->numlist != NULL) &&
            !scan->numlist->rendered_result)
          { // See if this node is excluded by the above conditions
            jx_numlist *nlst = scan->numlist;
            bool have_match =
              (((nlst->num_codestreams==0) && // Empty (container) numlist is
                (nlst->num_compositing_layers==0)) || // always taken to match
               ((num_codestreams < 0) && (nlst->num_codestreams > 0)) ||
               ((num_compositing_layers < 0) &&
                (nlst->num_compositing_layers > 0)));
            if ((!have_match) && (num_codestreams > 0))
              { // Have to check individual codestreams
                int n, m;
                for (n=0; n < nlst->num_codestreams; n++)
                  { 
                    int nlst_idx = nlst->codestream_indices[n];
                    for (m=0; m < num_codestreams; m++)
                      if (codestream_indices[m] == nlst_idx)
                        { have_match = true; break; }
                    if (have_match)
                      break;
                  }
              }
            if ((!have_match) && (num_compositing_layers > 0))
              { // Have to check individual compositing layers
                int n, m;
                for (n=0; n < nlst->num_compositing_layers; n++)
                  { 
                    int nlst_idx = nlst->layer_indices[n];
                    for (m=0; m < num_compositing_layers; m++)
                      if (layer_indices[m] == nlst_idx)
                        { have_match = true; break; }
                    if (have_match)
                      break;
                  }
              }
            if (!have_match)
              continue; // Not interested in this node or its descendants
          }
        scan->load_recursive();
      }
  } catch (...) {
    walker.detach();
    throw;
  }

  assert(!walker);
  return (last_one != state->last_added_node);
}

/*****************************************************************************/
/*                   jpx_meta_manager::enumerate_matches                     */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::enumerate_matches(jpx_metanode last_node,
                                      int codestream_idx,
                                      int compositing_layer_idx,
                                      bool applies_to_rendered_result,
                                      kdu_dims region, int min_size,
                                      bool only_non_roi_numlists,
                                      bool ignore_missing_numlist_info,
                                      bool exclude_duplicate_numlists)
{
  if (state == NULL)
    return jpx_metanode(NULL);
  jx_numlist_library *numlists = &state->numlists;
  jx_metanode *node = last_node.state;
  int base_codestream_idx=-1;
  int base_compositing_layer_idx=-1;
  if ((codestream_idx > 0) || (compositing_layer_idx > 0))
    { 
      jx_container_base *cont = NULL;
      if (node == NULL)
        { // Need to see if `codestream_idx'  `compositing_layer_idx'
          // correspond to base codestreams/layers for some JPX container
          if (compositing_layer_idx > 0)
            { // Look for first container for which base layer might match
              jx_container_base *scan=state->containers;
              for (; scan != NULL; cont=scan, scan=scan->next)
                if (scan->get_first_base_layer() > compositing_layer_idx)
                  break;
            }
          else
            { // Look for first container for which base stream might match
              jx_container_base *scan=state->containers;
              for (; scan != NULL; cont=scan, scan=scan->next)
                if (scan->get_first_base_codestream() > codestream_idx)
                  break;
            }
        }
      else
        { // We can exploit the fact that the last matching node must be
          // embedded within any relevant JPX container, or else we have
          // finished searching all potential container-embedded elements.
          // This works only because the `JX_LIBRARY_C_NLL' and
          // `JX_LIBRARY_C_NLC' library indices are smaller than those for
          // all other libraries.
          cont = node->find_container();
        }
      int first_idx, num_base_indices;
      if ((cont != NULL) && (codestream_idx > 0) &&
          ((num_base_indices = cont->get_num_base_codestreams()) > 0) &&
          (codestream_idx <= cont->get_last_codestream()))
        { 
          first_idx = cont->get_first_base_codestream();
          if (codestream_idx >= first_idx)
            base_codestream_idx = first_idx +
              ((codestream_idx - first_idx) % num_base_indices);
        }
      if ((cont != NULL) && (compositing_layer_idx > 0) &&
          (compositing_layer_idx <= cont->get_last_layer()))
        { 
          num_base_indices = cont->get_num_base_layers();
          assert(num_base_indices > 0);
          first_idx = cont->get_first_base_layer();
          if (compositing_layer_idx >= first_idx)
            base_compositing_layer_idx = first_idx +
              ((compositing_layer_idx - first_idx) % num_base_indices);
        }
    }
  
  if (!region.is_empty())
    { // Search in the ROI hierarchy
      jx_numlist *cur_numlist = NULL;
      jx_regions *new_roi = NULL;
      if (node != NULL)
        { 
          jx_regions *cur_roi = node->regions;
          if ((node->rep_id == JX_ROI_NODE) && (cur_roi != NULL) &&
              (cur_roi->region_cluster != NULL))
            { 
              jx_region_library *library = cur_roi->region_cluster->library;
              new_roi = library->enumerate_matches(cur_roi,region,min_size);
              if (new_roi != NULL)
                return jpx_metanode(new_roi->metanode);
              cur_numlist = library->representative_numlist;
              if (cur_numlist == NULL)
                { // Finished inspecting unassociated region library
                  assert(library == &state->unassociated_regions);
                  return jpx_metanode(NULL);
                }
              assert(cur_numlist->first_identical == cur_numlist);
            }
        }
      
      // Find new region library of relevance
      while (new_roi == NULL)
        { 
          jx_region_library *library=NULL;
          if ((codestream_idx < 0) && (compositing_layer_idx < 0) &&
              !applies_to_rendered_result)
            library = &state->unassociated_regions;
          else if ((cur_numlist =
                    numlists->enumerate_matches(cur_numlist,codestream_idx,
                                                compositing_layer_idx,
                                                base_codestream_idx,
                                                base_compositing_layer_idx,
                                                applies_to_rendered_result,
                                                ignore_missing_numlist_info,
                                                false)) != NULL)
            { 
              if ((library = cur_numlist->region_library) == NULL)
                continue;
            }
          else
            return jpx_metanode(NULL); // No more matching numlists
          new_roi = library->enumerate_matches(NULL,region,min_size);
        }
      return jpx_metanode(new_roi->metanode);
    }
  else if ((codestream_idx >= 0) || (compositing_layer_idx >= 0) ||
           applies_to_rendered_result)
    { // Search numlists
      jx_numlist *cur_numlist=NULL, *new_numlist=NULL;
      if ((node != NULL) && (node->rep_id == JX_NUMLIST_NODE) &&
          ((cur_numlist = node->numlist) != NULL))
        { 
          cur_numlist = node->numlist;
          if (exclude_duplicate_numlists ||
              (cur_numlist->next_identical == NULL))
            cur_numlist = cur_numlist->first_identical;
                // library enumeration function works only on head of list
          else
            return jpx_metanode(cur_numlist->next_identical->metanode);
        }
      node = NULL;
      if ((new_numlist =
           numlists->enumerate_matches(cur_numlist,codestream_idx,
                                       compositing_layer_idx,
                                       base_codestream_idx,
                                       base_compositing_layer_idx,
                                       applies_to_rendered_result,
                                       ignore_missing_numlist_info,
                                       only_non_roi_numlists)) != NULL)
        node = new_numlist->metanode;
      return jpx_metanode(node);
    }
  
  // If we get here, we are looking for top-level unassociated metadata
  if (node != NULL)
    node = node->next_sibling;
  else
    node = state->tree->head;
  while ((node != NULL) &&
         ((node->rep_id == 0) || (node->rep_id == JX_NUMLIST_NODE) ||
          (node->rep_id == JX_ROI_NODE)))
    node = node->next_sibling;
  return jpx_metanode(node);
}

/*****************************************************************************/
/*                      jpx_meta_manager::insert_node                        */
/*****************************************************************************/

jpx_metanode
  jpx_meta_manager::insert_node(int num_codestreams,
                                const int codestream_indices[],
                                int num_compositing_layers,
                                const int layer_indices[],
                                bool applies_to_rendered_result,
                                int num_regions, const jpx_roi *regions,
                                jpx_metanode root, int container_id)
{
  assert(state != NULL);
  assert((num_codestreams >= 0) && (num_compositing_layers >= 0));
  if ((num_regions > 0) && (num_codestreams == 0))
    { KDU_ERROR_DEV(e,30); e <<
        KDU_TXT("You may not use `jpx_meta_manager::insert_node' to "
        "insert a region-specific metadata node which is not also associated "
        "with at least one codestream.  The reason is that ROI description "
        "boxes have meaning only when associated with codestreams, as "
        "described in the JPX standard.");
    }
  
  jx_metanode *root_node=root.state;
  if (root_node == NULL)
    { 
      root_node = state->tree;
      root = jpx_metanode(root_node);
    }

  jx_container_base *container=NULL;
  if (root_node != state->tree)
    { 
      container = root_node->find_container();
      if ((container_id >= 0) &&
          ((container == NULL) || (container->get_id() != container_id)))
        { KDU_ERROR_DEV(e,0x12081201); e <<
          KDU_TXT("The `jpx_meta_manager::insert_node' function may be "
                  "passed a non-negative `container_id' argument only if "
                  "the node is to be inserted as an immediate descendant "
                  "of the root of the metadata hierarchy, or if "
                  "the node is to be inserted beneath an existing "
                  "`root' node that is already embedded within the "
                  "same JPX container.");
        }
      container_id = -1; // So `container_id' >= 0 only for top-level inserts
    }
  else if (container_id >= 0)
    { 
      container = state->find_container(container_id);
      if (container == NULL)
        { KDU_ERROR_DEV(e,0x12081202); e <<
          KDU_TXT("`container_id' passed to `jpx_meta_manager::insert_node' "
                  "does not correspond to an existing JPX container.");
        }
    }
  if ((container != NULL) &&
      !container->check_compatibility(num_codestreams,codestream_indices,
                                      num_compositing_layers,layer_indices,
                                      true))
    { KDU_ERROR_DEV(e,0x12081203); e <<
      KDU_TXT("Attempting to use `jpx_meta_manager::insert_node' to "
              "create a number list that is embedded within an incompatible "
              "JPX container, either explicitly (via `container_id' >= 0) "
              "or implicitly (via a non-empty `root' metanode that is "
              "itself embedded within a JPX container).  Compatibility means "
              "that the supplied compositing layer and codestream indices "
              "must must correspond to compositing layers or codestreams "
              "that are defined either at the top level of the file or by "
              "the JPX container.");
    }
  if ((num_codestreams == 0) && (num_compositing_layers == 0) &&
      (!applies_to_rendered_result) && (container_id < 0))
    return jpx_metanode(root_node);

  // Create a temporary `jx_numlist' object to facilitate testing
  // existing numlists
  int n;
  jx_numlist match_numlist(NULL,container);
  match_numlist.rendered_result = applies_to_rendered_result;
  for (n=0; n < num_codestreams; n++)
    match_numlist.add_codestream(codestream_indices[n],false);
  for (n=0; n < num_compositing_layers; n++)
    match_numlist.add_compositing_layer(layer_indices[n],false);
  
  // Look for existing numlists that match
  jx_metanode *scan;
  for (scan=root_node->head; scan != NULL; scan=scan->next_sibling)
    { 
      if ((scan->flags & JX_METANODE_WRITTEN) ||
          !((scan->flags & JX_METANODE_BOX_COMPLETE) &&
            (scan->rep_id == JX_NUMLIST_NODE)))
        continue;
      if (match_numlist.equals(scan->numlist))
        break;
    }

  if (scan == NULL)
    { 
      jx_check_metanode_before_add_child(root_node);
      scan = root_node->add_numlist(num_codestreams,codestream_indices,
                                    num_compositing_layers,layer_indices,
                                    applies_to_rendered_result,container);
    }
  jpx_metanode result(scan);
  if (num_regions == 0)
    return result;

  return result.add_regions(num_regions,regions);
}


/* ========================================================================= */
/*                              jx_registration                              */
/* ========================================================================= */

/*****************************************************************************/
/*                           jx_registration::init                           */
/*****************************************************************************/

void
  jx_registration::init(jp2_input_box *creg)
{
  if (codestreams != NULL)
    { KDU_ERROR(e,31); e <<
        KDU_TXT("JPX data source appears to contain multiple "
        "JPX Codestream Registration (creg) boxes within the same "
        "compositing layer header (jplh) box.");
    }
  final_layer_size.x = final_layer_size.y = 0; // To be calculated later
  kdu_uint16 xs=0, ys=0;
  if (!(creg->read(xs) && creg->read(ys) && (xs > 0) && (ys > 0)))
    { KDU_ERROR(e,32); e <<
        KDU_TXT("Malformed Codestream Registration (creg) box found "
        "in JPX data source.  Insufficient or illegal fields encountered.");
    }
  denominator.x = (int) xs;
  denominator.y = (int) ys;
  int box_bytes = (int) creg->get_remaining_bytes();
  if ((box_bytes % 6) != 0)
    { KDU_ERROR(e,33); e <<
        KDU_TXT("Malformed Codestream Registration (creg) box found "
        "in JPX data source.  Box size does not seem to be equal to 4+6k "
        "where k must be the number of referenced codestreams.");
    }
  num_codestreams = max_codestreams = (box_bytes / 6);
  codestreams = new jx_layer_stream[max_codestreams];
  int c;
  for (c=0; c < num_codestreams; c++)
    { 
      jx_layer_stream *str = codestreams + c;
      kdu_uint16 cdn;
      kdu_byte xr=0, yr=0, xo=0, yo=0;
      if (!(creg->read(cdn) && creg->read(xr) && creg->read(yr) &&
            creg->read(xo) && creg->read(yo)))
        abort(); // Should not be possible
      if ((xr == 0) || (yr == 0))
        { KDU_ERROR(e,34); e <<
            KDU_TXT("Malformed Codestream Registration (creg) box "
            "found in JPX data source.  Illegal (zero-valued) resolution "
            "parameters found for codestream " << cdn << ".");
        }
      if ((denominator.x <= (int) xo) || (denominator.y <= (int) yo))
        { KDU_ERROR(e,35); e <<
            KDU_TXT("Malformed Codestream Registration (creg) box "
            "found in JPX data source.  Alignment offsets must be strictly "
            "less than the denominator (point density) parameters, as "
            "explained in the JPX standard (accounting for corrigenda).");
        }
      if (container == NULL)
        str->codestream_id = (int) cdn;
      else
        str->codestream_id =
          container->convert_relative_to_base_codestream_id((int) cdn);
      str->sampling.x = xr;
      str->sampling.y = yr;
      str->alignment.x = xo;
      str->alignment.y = yo;
    }
  creg->close();
}

/*****************************************************************************/
/*                     jx_registration::finalize (reading)                   */
/*****************************************************************************/

void
  jx_registration::finalize(int layer_id)
{
  if (codestreams == NULL)
    { // Create default registration info
      if (container != NULL)
        container->validate_auto_codestream_binding(layer_id);
      num_codestreams = max_codestreams = 1;
      codestreams = new jx_layer_stream[1];
      codestreams->codestream_id = layer_id;
      codestreams->sampling = kdu_coords(1,1);
      codestreams->alignment = kdu_coords(0,0);
      denominator = kdu_coords(1,1);
    }
}

/*****************************************************************************/
/*                     jx_registration::finalize (writing)                   */
/*****************************************************************************/

void
  jx_registration::finalize(j2_channels *channels, int layer_idx)
{
  if ((denominator.x == 0) || (denominator.y == 0))
    denominator = kdu_coords(1,1);
  int n, k, c, cid;
  for (k=0; k < channels->num_colours; k++)
    {
      j2_channels::j2_channel *cp = channels->channels + k;
      for (c=0; c < 3; c++)
        {
          cid = cp->codestream_idx[c];
          if (cid < 0)
            continue;
          for (n=0; n < num_codestreams; n++)
            if (codestreams[n].codestream_id == cid)
              break;
          if (n == num_codestreams)
            { // Add a new one
              if (n >= max_codestreams)
                { // Allocate new array
                  int new_max_cs = max_codestreams*2 + 2;
                  jx_layer_stream *buf = new jx_layer_stream[new_max_cs];
                  for (n=0; n < num_codestreams; n++)
                    buf[n] = codestreams[n];
                  if (codestreams != NULL)
                    delete[] codestreams;
                  codestreams = buf;
                  max_codestreams = new_max_cs;
                }
              num_codestreams++;
              codestreams[n].codestream_id = cid;
              codestreams[n].sampling = denominator;
              codestreams[n].alignment = kdu_coords(0,0);
            }
        }
    }
  
  // Before finishing up, we should check to see whether codestream
  // registration has been specified for any unused codestreams.
  for (n=0; n < num_codestreams; n++)
    {
      int cid = codestreams[n].codestream_id;
      bool found = false;
      for (k=0; (k < channels->num_colours) && !found; k++)
        {
          j2_channels::j2_channel *cp = channels->channels + k;
          for (c=0; c < 3; c++)
            if (cid == cp->codestream_idx[c])
              found = true;
        }
      if (!found)
        { KDU_ERROR_DEV(e,0x05011001); e <<
          KDU_TXT("Registration information has been supplied via the "
                  "`jpx_layer_target::set_codestream_registration' for a "
                  "codestream which is not used by any channel defined for "
                  "the compositing layer in question!  The codestream "
                  "in question has (zero-based) index ") << cid <<
          KDU_TXT(" and the compositing layer has (zero-based) index ") <<
          layer_idx << ".";
        }
    }
}

/*****************************************************************************/
/*                        jx_registration::save_box                          */
/*****************************************************************************/

void
  jx_registration::save_box(jp2_output_box *super_box)
{
  assert(num_codestreams > 0);
  int n;
  jx_layer_stream *str;

  jp2_output_box creg;
  creg.open(super_box,jp2_registration_4cc);
  creg.write((kdu_uint16) denominator.x);
  creg.write((kdu_uint16) denominator.y);
  int min_scope_codestream=0, lim_scope_codestream=0;
  int num_top_codestreams=INT_MAX;
  if (container != NULL)
    { 
      num_top_codestreams = container->get_num_top_codestreams();
      min_scope_codestream = container->get_first_base_codestream();
      lim_scope_codestream = min_scope_codestream +
        container->get_num_base_codestreams();
    }
  for (n=0; n < num_codestreams; n++)
    {
      str = codestreams + n;
      int cdn = str->codestream_id;
      if ((cdn < 0) || (cdn >= num_top_codestreams))
        { 
          if ((cdn < 0) || (cdn < min_scope_codestream) ||
              (cdn >= lim_scope_codestream) ||
              (min_scope_codestream < num_top_codestreams))
            { KDU_ERROR_DEV(e,0x02071206); e <<
              KDU_TXT("Use of inaccessible codestream from within "
                      "a compositing layer!  You may be receiving this "
                      "error because a top-level compositing layer has "
                      "referenced a non-existent codestream or one that "
                      "is not found at the top-level of the file.  You "
                      "may also be receiving this error because a "
                      "compositing layer found within a JPX container "
                      "has referenced a codestream that lies neither at "
                      "the top-level of the file nor within the same "
                      "container.");
            }
          cdn -= (min_scope_codestream - num_top_codestreams);
        }
      int xr = str->sampling.x;
      int yr = str->sampling.y;
      int xo = str->alignment.x;
      int yo = str->alignment.y;
      if ((cdn < 0) || (cdn >= (1<<16)) || (xr <= 0) || (yr <= 0) ||
          (xr > 255) || (yr > 255) || (xo < 0) || (yo < 0) ||
          (xo >= denominator.x) || (yo >= denominator.y))
        { KDU_ERROR_DEV(e,36); e <<
            KDU_TXT("One or more codestreams registration parameters "
            "supplied using `jpx_layer_target::set_codestream_registration' "
            "cannot be recorded in a legal JPX Codestream Registration (creg) "
            "box.");
        }
      if (xo > 255)
        xo = 255;
      if (yo > 255)
        yo = 255;
      creg.write((kdu_uint16) cdn);
      creg.write((kdu_byte) xr);
      creg.write((kdu_byte) yr);
      creg.write((kdu_byte) xo);
      creg.write((kdu_byte) yo);
    }
  creg.close();
}


/* ========================================================================= */
/*                             jx_container_base                             */
/* ========================================================================= */

/*****************************************************************************/
/*              jx_container_base::invalid_relative_index_error              */
/*****************************************************************************/

void
  jx_container_base::invalid_relative_index_error(int rel_idx, bool is_cs)
{
  const char *name = (is_cs)?"codestream":"compositing layer";
  KDU_ERROR(e,0x250712010); e <<
  KDU_TXT("Invalid relative ") << name <<
  KDU_TXT(" found inside Codesteam Registration or Number List box, within "
          "a JPX container (Compositing Layer Extensions box).  All such "
          "indices must either identify top-level image entities or else "
          "one of the base entities defined by the container, expressed "
          "relative to a hypothetical file in which top-level entities "
          "are immediately followed by the container in question.  The "
          "offending relative index is ") << rel_idx << ".";
}

/*****************************************************************************/
/*                    jx_container_base::invalid_rep_error                   */
/*****************************************************************************/

void
  jx_container_base::invalid_rep_error(int rep_idx)
{
  KDU_ERROR(e,0x25071211); e <<
  KDU_TXT("Invalid repetition index used to map codestream or compositing "
          "layer indices for a JPX container (Compositing Layer Extensions "
          "box).  Problem most likely caused by corruption of a "
          "`jpx_codestream_source' or `jpx_layer_source' interface object.");
}

/*****************************************************************************/
/*                   jx_container_base::invalid_index_error                  */
/*****************************************************************************/

void
  jx_container_base::invalid_index_error()
{
  KDU_ERROR_DEV(e,0x11081203); e <<
  KDU_TXT("Attempting to create a container-embedded number list with one "
          "or more codestream or compositing layer indices that do not "
          "belong to the relevant JPX container.");
}

/*****************************************************************************/
/*            jx_container_base::validate_auto_codestream_binding            */
/*****************************************************************************/

void
  jx_container_base::validate_auto_codestream_binding(int base_id)
{
  if (base_id < num_top_codestreams)
    { 
      if (!indefinite_reps)
        { 
          if ((base_id+(known_reps-1)*num_base_layers) >=
              num_top_codestreams)
            { KDU_ERROR(e,0x25071212); e <<
              KDU_TXT("JPX container (Compositing Layer Extensions box) "
                      "has embedded compositing layer without any "
                      "CREG box to bind it to a codestream; "
                      "moreover the implicit binding rule associates "
                      "some, but not all of the compositing layers with "
                      "top-level codestreams.  To avoid such illegal "
                      "situations, it is best to use Codestream "
                      "Registration boxes within JPX containers.");
            }
        }
      else
        { KDU_ERROR(e,0x25071213); e <<
          KDU_TXT("Indefinitely repeated JPX container (Compositing "
                  "Layer Extensions box) has embedded compositing layer "
                  "without any CREG box to bind it to a codestream; "
                  "moreover the implicit binding rule associates at "
                  "least some of the indefinitely repeated layers "
                  "with top-level codestreams; there is no way to "
                  "guarantee that the others will also be top-level.  "
                  "At the very least, this is a violation of the "
                  "intent behind indefinitely repeated JCLX boxes.");
        }
    }
  else if ((base_id < first_codestream_idx) ||
           (base_id >= (first_codestream_idx+num_base_codestreams)))
    { KDU_ERROR(e,0x25071214); e <<
      KDU_TXT("JPX container (Compositing Layer Extensions box) "
              "has embedded compositing layer without any CREG box to "
              "bind it to a codestream; moreover, the implicit binding "
              "rule associates it with a codestream that appears to "
              "belong to a different JPX container.  Container-defined "
              "codestreams can only be used by the container's "
              "own compositing layers.");
    }
  else if ((num_base_codestreams != num_base_layers) &&
           (known_reps != 1))
    { KDU_ERROR(e,0x25071215); e <<
      KDU_TXT("JPX container (Compositing Layer Extensions box) "
              "has embedded compositing layer without any CREG box to "
              "bind it to a codestream; moreover the implicit binding "
              "rule associates it with a codestream that is defined "
              "within the same container.  In this situation, the "
              "container must either not be repeated, or else the "
              "implicit binding rule must yield consistent associations "
              "on each repetition.  This is not the case; it is best "
              "to use Codestream Registration boxes within JPX "
              "containers.");
    }
}

/*****************************************************************************/
/*                   jx_container_base::check_compatibility                  */
/*****************************************************************************/

bool
  jx_container_base::check_compatibility(int num_streams, const int streams[],
                                         int num_layers, const int layers[],
                                         bool any_rep)
{
  int n, idx, delta, lim_delta_stream, lim_delta_layer;
  lim_delta_stream = num_base_codestreams;
  lim_delta_layer = num_base_layers;
  if (any_rep && indefinite_reps)
    lim_delta_stream = lim_delta_layer = INT_MAX;
  else if (any_rep)
    { 
      lim_delta_stream *= known_reps;
      lim_delta_layer *= known_reps;
    }
  for (n=0; n < num_streams; n++)
    { 
      idx = streams[n];
      if ((idx >= num_top_codestreams) &&
          (((delta = idx-first_codestream_idx) < 0) ||
           (delta >= lim_delta_stream)))
        return false;
    }
  for (n=0; n < num_layers; n++)
    { 
      idx = layers[n];
      if ((idx >= num_top_layers) &&
          (((delta = idx-first_layer_idx) < 0) ||
           (delta >= lim_delta_layer)))
        return false;
    }
  return true;
}


/* ========================================================================= */
/*                            jx_container_source                            */
/* ========================================================================= */

/*****************************************************************************/
/*                 jx_container_source::jx_container_source                  */
/*****************************************************************************/

jx_container_source::jx_container_source(jx_source *owner, jp2_input_box *box,
                                         int container_id)
{
  // Set base members
  this->owner = owner;
  this->id = container_id;
  base_layers = NULL;
  base_codestreams = NULL;
  lim_layer_idx = lim_codestream_idx = 0;
  next_base_layer = next_base_codestream = 0;
  parsed = true;
  
  // Set specific `jx_container_source' members
  max_tracks = 0;
  num_tracks = 0;
  num_frames_per_track = 0;
  start_time = 0;
  tracks = NULL;
  tracks_found = 0;
  tracks_completed = 0;
  iset_boxes_found = 0;
  
  jclx_nesting_level = 0;
  metanode = NULL;
  next = prev = NULL;
  if (box != NULL)
    { 
      assert(!this->is_special());
      jclx.transplant(*box);
      jp2_locator header_loc = jclx.get_locator();
      kdu_long header_span = jclx.get_remaining_bytes();
      if (header_span >= 0)
        header_span += jclx.get_box_header_length();
      jp2_locator contents_loc = jclx.get_contents_locator();
      if (contents_loc.get_databin_pos() > 0)
        { 
          assert(contents_loc.get_databin_id() <= 0);
          jclx_nesting_level = 1;
        }
      else
        { 
          assert(contents_loc.get_databin_id() > 0);
          jclx_nesting_level = 0;
        }
      jx_metanode *tree = owner->get_metatree();
      metanode=tree->add_numlist(0,NULL,0,NULL,false,this,header_loc,true);
      assert((metanode->parse_state != NULL) &&
             (metanode->parse_state->read == NULL));
      metanode->parse_state->metanode_span = header_span;
      metanode->parse_state->read = new jx_metaread;
      metanode->parse_state->read->container_source = this;
      
      metanode->parse_state->box_databin_id = contents_loc.get_databin_id();
      metanode->parse_state->box_nesting_level = jclx_nesting_level;
    }
  else
    assert(this->is_special());
}

/*****************************************************************************/
/*                jx_container_source::~jx_container_source                  */
/*****************************************************************************/

jx_container_source::~jx_container_source()
{
  assert((first_metanode == NULL) && (last_metanode == NULL));
  int n;
  if (base_layers != NULL)
    { 
      for (n=0; n < num_base_layers; n++)
        delete base_layers[n];
      delete[] base_layers;
    }
  if (base_codestreams != NULL)
    { 
      for (n=0; n < num_base_codestreams; n++)
        delete base_codestreams[n];
      delete[] base_codestreams;
    }
  if (tracks != NULL)
    delete[] tracks;
}

/*****************************************************************************/
/* STATIC              jx_container_source::parse_info                       */
/*****************************************************************************/

jx_container_source *
  jx_container_source::parse_info(jx_container_source *scan)
{
  for (; scan != NULL; scan=scan->get_next())
    { 
      if (scan->num_base_layers > 0)
        continue; // Info box has already been parsed

      kdu_uint32 Mjclx=0, Cjclx=1, Ljclx=1, Tjclx=0, Fjclx=0, Sjclx=0;
      if (!scan->is_special())
        { // Parameters above have been initialized for "special container"
          if ((scan->prev != NULL) &&
              (scan->get_prev()->num_base_layers == 0))
            break; // Cannot interpret INFO box for this container until
                   // previous container has had its INFO box parsed.
          if (!scan->info_box)
            { 
              assert(scan->jclx.exists());
              if (!scan->info_box.open(&scan->jclx))
                break;
            }
          if (!scan->info_box.is_complete())
            break;
          if (!(scan->info_box.read(Mjclx) && scan->info_box.read(Cjclx) &&
                scan->info_box.read(Ljclx) && scan->info_box.read(Tjclx) &&
                scan->info_box.read(Fjclx) &&
                ((Tjclx==0) || scan->info_box.read(Sjclx))))
            { KDU_ERROR(e,0x25071201); e <<
              KDU_TXT("Error in Compositing Layer Extensions Info box: "
                      "box appears to be prematurely trunctated.");
            }
          if ((Mjclx | Cjclx | Ljclx | Fjclx) & 0x80000000)
            { 
              if (Mjclx & 0x80000000) Mjclx = INT_MAX;
              if (Cjclx & 0x80000000) Cjclx = INT_MAX;
              if (Ljclx & 0x80000000) Ljclx = INT_MAX;
              if (Fjclx & 0x80000000) Fjclx = INT_MAX;
              KDU_WARNING(w,0x25071202); w <<
              KDU_TXT("Compositing Layer Extensions Info box contains "
                      "one or more ridiculously large parameters!!  Values "
                      "are being truncated; memory allocation faults may "
                      "also arise.");
            }
          scan->info_box.close();
        }
      
      scan->num_top_layers = scan->owner->get_num_top_layers();
      scan->num_top_codestreams = scan->owner->get_num_top_codestreams();
      if (scan->prev == NULL)
        { 
          scan->first_layer_idx = scan->num_top_layers;
          scan->first_codestream_idx = scan->num_top_codestreams;
        }
      else
        { 
          if (scan->get_prev()->indefinite_reps)
            { KDU_ERROR(e,0x25071203); e <<
              KDU_TXT("A Compositing Layer Extensions box that specifies "
                      "indefinite repetition (Mjclx=0) should not be "
                      "followed by further Compositing Layer Extensions "
                      "boxes.");
            }
          scan->first_layer_idx = scan->get_prev()->lim_layer_idx;
          scan->first_codestream_idx = scan->get_prev()->lim_codestream_idx;
          scan->max_tracks = scan->get_prev()->max_tracks;
        }
      scan->lim_layer_idx = scan->first_layer_idx;
      scan->lim_codestream_idx = scan->first_codestream_idx;
      scan->indefinite_reps = (Mjclx==0);
      scan->known_reps = (int) Mjclx;
      scan->num_base_codestreams = (int) Cjclx;
      scan->num_base_layers = (int) Ljclx;
      scan->num_tracks = Tjclx;
      scan->num_frames_per_track = (int) Fjclx;
      scan->start_time = Sjclx;
      if (scan->num_tracks > scan->max_tracks)
        scan->max_tracks = scan->num_tracks;
      if (scan->num_base_layers == 0)
        { KDU_ERROR(e,0x25071204); e <<
          KDU_TXT("Error in Compositing Layer Extensions Info box: "
                  "the number of base compositing layers must be non-zero.");
        }
      if ((scan->num_tracks==0) && (scan->num_frames_per_track != 0))
        { KDU_ERROR(e,0x25071205); e <<
          KDU_TXT("Error in Compositing Layer Extensions Info box: "
                  "the number of presentation threads (or tracks) is "
                  "declared as zero, yet the number of presentation "
                  "frames is declared non-zero.");
        }
      int first_layer = scan->first_layer_idx;
      int first_stream = scan->first_codestream_idx;
      int b_layers = scan->num_base_layers;
      int b_streams = scan->num_base_codestreams;
      kdu_uint32 n_tracks = scan->num_tracks;
      try {
        int n;
        kdu_uint32 t;
        scan->base_layers = new jx_layer_source *[b_layers];
        memset(scan->base_layers,0,sizeof(void *)*(size_t)b_layers);
        if (b_streams > 0)
          { 
            scan->base_codestreams = new jx_codestream_source *[b_streams];
            memset(scan->base_codestreams,0,sizeof(void *)*(size_t)b_streams);
          }
        for (n=0; n < b_layers; n++)
          scan->base_layers[n] =
            new jx_layer_source(scan->owner,first_layer+n,scan);
        for (n=0; n < b_streams; n++)
          scan->base_codestreams[n] =
            new jx_codestream_source(scan->owner,first_stream+n,false,scan);
        if (n_tracks > 0)
          { 
            jx_composition *track_pred=scan->owner->get_composition();
            jx_container_source *p;
            for (p=scan->get_prev(); p != NULL; p=p->get_prev())
              if (p->tracks != NULL)
                { track_pred = &(p->tracks->composition); break; }
            scan->tracks = new jx_track_source[n_tracks];
            for (t=0; t <  n_tracks; t++)
              { 
                scan->tracks[t].composition.init_for_reading(scan->owner,scan,
                                                  scan->num_frames_per_track,
                                                  scan->start_time);
                scan->tracks[t].composition.track_idx = t+1;
                if (t > 0)
                  scan->tracks[t-1].composition.track_next =
                    &(scan->tracks[t].composition);
                if (track_pred != NULL)
                  { 
                    scan->tracks[t].composition.prev_in_track = track_pred;
                    track_pred->next_in_track = &(scan->tracks[t].composition);
                    track_pred = track_pred->track_next;
                  }
                else
                  scan->tracks[t].composition.prev_in_track =
                    scan->tracks[t-1].composition.prev_in_track;
              }
            while (track_pred != NULL)
              { 
                track_pred->next_in_track =
                  &(scan->tracks[n_tracks-1].composition);
                track_pred = track_pred->track_next;
              }
          }
      }
      catch (std::bad_alloc) { 
        KDU_ERROR_DEV(e,0x25071206); e <<
        KDU_TXT("Unable to allocate sufficient memory to record even the "
                "base compositing layers or codestreams associated with "
                "Compositing Layer Extensions box.  The box must be "
                "ridiculously large and unwieldly.");
      }
      int reps = scan->known_reps;
      if (scan->indefinite_reps)
        { // Figure out min repetitions based on codestream count
          if (b_streams < 1)
            { KDU_ERROR(e,0x25071207); e <<
              KDU_TXT("Error in Compositing Layer Extensions Info box: "
                      "indefinite repetition (Mjclx=0) is allowed only "
                      "for boxes that provide at least one new "
                      "Codestream Header (Cjclx > 0).");
            }
          int total_streams = scan->owner->get_total_codestreams();
          if (total_streams > first_stream)
            reps = (total_streams-first_stream) / b_streams;
          if (reps < 1)
            reps = 1;
          scan->known_reps = reps;
        }
      scan->lim_layer_idx = first_layer + reps*b_layers;
      scan->lim_codestream_idx = first_stream + reps*b_streams;
      scan->owner->update_container_info(scan,scan->lim_layer_idx,
                                         scan->lim_codestream_idx,
                                         scan->indefinite_reps);
      
      if (scan->is_special())
        { 
          scan->base_codestreams[0]->finish();
          scan->base_layers[0]->finish();
        }
      else
        scan->finish();
    }
  return scan;
}

/*****************************************************************************/
/*               jx_container_source::note_total_codestreams                 */
/*****************************************************************************/

void jx_container_source::note_total_codestreams(int num)
{
  if (!indefinite_reps)
    return;
  assert(num_base_codestreams > 0);
  int min_reps = (num - first_codestream_idx) / num_base_codestreams;
  if (min_reps > known_reps)
    { 
      known_reps = min_reps;
      lim_layer_idx = first_layer_idx+min_reps*num_base_layers;
      lim_codestream_idx = first_codestream_idx+min_reps*num_base_codestreams;
      owner->update_total_layers(lim_layer_idx);
    }
}

/*****************************************************************************/
/*                      jx_container_source::finish                          */
/*****************************************************************************/

bool jx_container_source::finish(bool invoked_by_metanode)
{
  if (!jclx.exists())
    return true;
  assert(!this->is_special()); // Cannot be a special container
  
  if (num_base_layers == 0)
    { // Need to try to get `parse_info' to make it to us
      jx_container_source *start = this;
      while ((start->prev != NULL) &&
             (start->get_prev()->num_base_layers == 0))
        start = start->get_prev();
      parse_info(start);
      if (num_base_layers == 0)
        return false;
    }
  
  jp2_input_box sub_box;
  bool jclx_complete = jclx.is_complete();
  while (sub_box.open(&jclx))
    { 
      kdu_uint32 box_type = sub_box.get_box_type();
      if (box_type == jp2_comp_instruction_set_4cc)
        { 
          jx_track_source *trk = tracks+tracks_completed;
          if (tracks_completed == tracks_found)
            { // We have just found a new track
              if (tracks_completed >= num_tracks)
                { KDU_ERROR(e,0x30071202); e <<
                  KDU_TXT("Number of groups of Instruction Set boxes "
                          "encountered within a Compositing Layer Extensions "
                          "box exceeds the number of presentation tracks "
                          "(or threads) advertised by its INFO sub-box.");
                }
              trk->layer_lim = next_base_layer;
              if (tracks_found > 0)
                trk->layer_start = trk[-1].layer_lim;
              if (trk->layer_lim <= trk->layer_start)
                { KDU_ERROR(e,0x30071201); e <<
                  KDU_TXT("The Instruction Set boxes that define a "
                          "Presentation Track (or thread) within a "
                          "Compositing Layer Extensions box must appear "
                          "one after the other, without any other boxes "
                          "in between.");
                }
              tracks_found++; // Increment first because track indices 1-based
            }
          trk->composition.donate_instruction_box(sub_box,iset_boxes_found++);
          assert(!sub_box);
          continue;
        }
      else
        update_completed_tracks();

      if (box_type == jp2_codestream_header_4cc)
        { 
          if (next_base_codestream >= num_base_codestreams)
            { KDU_ERROR(e,0x26071207); e <<
              KDU_TXT("Too many Codestream headers found inside Compositing "
                      "Layer Extensions box, given count advertised in its"
                      "JCLI (INFO) sub-box.");
            }
          jx_codestream_source *cs = base_codestreams[next_base_codestream++];
          cs->donate_chdr_box(sub_box,jclx_nesting_level);
          assert(!sub_box);
        }
      else if (box_type == jp2_compositing_layer_hdr_4cc)
        { 
          if (next_base_layer >= num_base_layers)
            { KDU_ERROR(e,0x26071208); e <<
              KDU_TXT("Too many Compositing Layer headers found inside "
                      "Compositing Layer Extensions box, given count "
                      "advertised by its JCLI (INFO) sub-box.");
            }
          jx_layer_source *lyr = base_layers[next_base_layer++];
          lyr->donate_jplh_box(sub_box,jclx_nesting_level);
          assert(!sub_box);
        }
      else if (owner->test_box_filter(box_type))
        { // Put into the metadata management system
          metanode->read_and_insert_child(sub_box,jclx_nesting_level);
        }
      else
        sub_box.close(); // Later we should parse other box types too
    }
  assert(!sub_box.exists());
  
  // See if we have enough of the JCLX sub-boxes to finish parsing it
  if (jclx_complete)
    { 
      jclx.close();
      update_completed_tracks();
      assert(tracks_found == tracks_completed);
      jx_metanode *meta = metanode;
      this->metanode = NULL; // So we never refer to it again
      meta->flags |= JX_METANODE_CONTAINER_KNOWN;
      if ((meta->head == NULL) && (meta->linked_from == NULL))
        { // Ensure that `meta->remove_empty_shell' is called; this is safe,
          // because we have been careful not to allow anybody access to
          // the synthesized `meta' object until it has descendants.          
          assert(meta->rep_id == JX_NUMLIST_NODE);
          meta->numlist->unlink();
          delete meta->numlist;
          meta->numlist = NULL;
          meta->rep_id = 0;
        }
      if (!invoked_by_metanode)
        { // Otherwise, the following actions are performed within the caller,
          // i.e., at the end of `jx_metanode::finish_reading'.
          delete meta->parse_state->read;
          meta->parse_state->read = NULL;
          meta->check_parsing_complete();          
          if ((meta->rep_id == 0) && (meta->parse_state == NULL))
            meta->remove_empty_shell();
        }
    }
  else if ((next_base_layer == num_base_layers) &&
           (next_base_codestream == num_base_codestreams) &&
           (tracks_completed == num_tracks))
    { // Donate `jclx' to the metadata management machinery to extract
      // any remaining boxes it can.
      metanode->parse_state->read->container_source = NULL;
      metanode->parse_state->read->asoc.transplant(jclx);
      metanode = NULL; // We have no need to refer to it again
      assert(!jclx);
    }
  
  if (jclx.exists())
    return false;
  
  if ((next_base_layer != num_base_layers) ||
      (next_base_codestream != num_base_codestreams))
    { KDU_ERROR(e,0x26071209); e <<
      KDU_TXT("Insufficient Codestream or Compositing Layer header boxes "
              "found inside Compositing Layer Extensions box, given the "
              "counts advertised by its JCLI (INFO) sub-box.");
    }
  if (tracks_completed != num_tracks)
    { KDU_ERROR(e,0x29071201); e <<
      KDU_TXT("Number of distinct collections of Instruction Set boxes "
              "found in Compositing Layer Extensions box does not agree "
              "with the number of presentation threads (or tracks) "
              "advertised by the JCLI (INFO) sub-box.");
    }
  return true;
}

/*****************************************************************************/
/*              jx_container_source::update_completed_tracks                 */
/*****************************************************************************/

void jx_container_source::update_completed_tracks()
{
  if (tracks_found == tracks_completed)
    return;
  jx_track_source *trk = &(tracks[tracks_completed++]);
  assert(tracks_found == tracks_completed);
  trk->composition.set_layer_mapping(first_layer_idx + trk->layer_start,
                                     (indefinite_reps)?0:known_reps,
                                     trk->layer_lim-trk->layer_start,
                                     num_base_layers);
  trk->finished = trk->composition.finish();
}

/*****************************************************************************/
/* STATIC              jx_container_source::find_layer                       */
/*****************************************************************************/

jx_layer_source *
  jx_container_source::find_layer(jx_container_source *scan,
                                  int layer_id, int &rep_idx)
{
  for (; scan != NULL; scan=scan->get_next())
    { 
      if ((scan->num_base_layers == 0) && (parse_info(scan) == scan))
        break; // Need `parse_info' for anything else to be meaningful
      assert(scan->num_base_layers > 0);
      if ((layer_id >= scan->lim_layer_idx) && !scan->indefinite_reps) 
        { // Need to look at the next container, but perhaps there is none
          while ((scan->next == NULL) &&
                 scan->owner->parse_next_top_level_box());
          continue;
        }
      return scan->match_layer(layer_id,rep_idx);
    }
  return NULL;
}

/*****************************************************************************/
/* STATIC           jx_container_source::find_codestream                     */
/*****************************************************************************/

jx_codestream_source *
  jx_container_source::find_codestream(jx_container_source *scan,
                                       int stream_id, int &rep_idx)
{
  for (; scan != NULL; scan=scan->get_next())
    { 
      if ((scan->num_base_codestreams == 0) && (parse_info(scan) == scan))
        break; // Need `parse_info' for anything else to be meaningful
      assert(scan->num_base_codestreams > 0);
      if ((stream_id >= scan->lim_codestream_idx) && !scan->indefinite_reps) 
        { // Need to look at the next container, but perhaps there is none
          while ((scan->next == NULL) &&
                 scan->owner->parse_next_top_level_box());
          continue;
        }
      return scan->match_codestream(stream_id,rep_idx);
    }
  return NULL;
}


/* ========================================================================= */
/*                           jpx_container_source                            */
/* ========================================================================= */

/*****************************************************************************/
/*                 jpx_container_source::get_container_id                    */
/*****************************************************************************/

int jpx_container_source::get_container_id()
{
  return (state==NULL)?-1:(state->id);
}

/*****************************************************************************/
/*               jpx_container_source::get_num_top_codestreams               */
/*****************************************************************************/

int jpx_container_source::get_num_top_codestreams()
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  return state->num_top_codestreams;
}

/*****************************************************************************/
/*                 jpx_container_source::get_num_top_layers                  */
/*****************************************************************************/

int jpx_container_source::get_num_top_layers()
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  return state->num_top_layers;
}

/*****************************************************************************/
/*                jpx_container_source::get_base_codestreams                 */
/*****************************************************************************/

int jpx_container_source::get_base_codestreams(int &num_base_codestreams)
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  num_base_codestreams = state->num_base_codestreams;
  return state->first_codestream_idx;
}

/*****************************************************************************/
/*                   jpx_container_source::get_base_layers                   */
/*****************************************************************************/

int jpx_container_source::get_base_layers(int &num_base_layers)
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  num_base_layers = state->num_base_layers;
  return state->first_layer_idx;
}

/*****************************************************************************/
/*                jpx_container_source::count_repetitions                    */
/*****************************************************************************/

bool jpx_container_source::count_repetitions(int &count)
{
  if (state == NULL)
    return 0;
  bool count_known = true;
  if (state->indefinite_reps)
    count_known = state->owner->find_all_streams();
  count = state->known_reps;
  return count_known;
}

/*****************************************************************************/
/*                jpx_container_source::access_codestream                    */
/*****************************************************************************/

jpx_codestream_source
  jpx_container_source::access_codestream(int base_idx, int rep_idx,
                                          bool need_main_header,
                                          bool find_first_rep)
{
  jpx_codestream_source result;
  if ((state == NULL) || (base_idx < 0) ||
      (base_idx >= state->num_base_codestreams))
    return result; // Empty interface
  if (rep_idx < 0)
    rep_idx = 0;
  else if ((rep_idx >= state->known_reps) && !state->indefinite_reps)
    return result; // Empty interface
  jx_codestream_source *cs = state->base_codestreams[base_idx];
  if (!cs->finish())
    return result; // Empty interface
  if (cs->stream_available(rep_idx,need_main_header))
    result = jpx_codestream_source(cs,rep_idx);
  while (find_first_rep && !result.exists())
    { 
      if (rep_idx >= state->known_reps)
        { // We already pushed things to see if we could go
          // beyond the number of previously known repetitions.
          assert(state->indefinite_reps);
          break;
        }
      rep_idx++;
      if ((rep_idx == state->known_reps) || !state->indefinite_reps)
        break;
      if (cs->stream_available(rep_idx,need_main_header))
        result = jpx_codestream_source(cs,rep_idx);
    }
  return result;
}

/*****************************************************************************/
/*                   jpx_container_source::access_layer                      */
/*****************************************************************************/

jpx_layer_source
  jpx_container_source::access_layer(int base_idx, int rep_idx,
                                     bool need_stream_headers,
                                     bool find_first_rep)
{
  jpx_layer_source result;
  if ((state == NULL) || (base_idx < 0) ||
      (base_idx >= state->num_base_layers))
    return result; // Empty interface
  if (rep_idx < 0)
    rep_idx = 0;
  else if ((rep_idx >= state->known_reps) && !state->indefinite_reps)
    return result; // Empty interface
  jx_layer_source *layer = state->base_layers[base_idx];
  if (!layer->finish())
    return result; // Empty interface
  if (layer->all_streams_available(rep_idx,need_stream_headers))
    result = jpx_layer_source(layer,rep_idx);
  while (find_first_rep && !result.exists())
    { 
      if (rep_idx >= state->known_reps)
        { // We already pushed things to see if we could go
          // beyond the number of previously known repetitions.
          assert(state->indefinite_reps);
          break;
        }
      rep_idx++;
      if ((rep_idx == state->known_reps) || !state->indefinite_reps)
        break;
      if (layer->all_streams_available(rep_idx,need_stream_headers))
        result = jpx_layer_source(layer,rep_idx);
    }
  return result;
}

/*****************************************************************************/
/*                jpx_container_source::check_compatibility                  */
/*****************************************************************************/

bool jpx_container_source::check_compatibility(int num_streams,
                                               const int streams[],
                                               int num_layers,
                                               const int layers[],
                                               bool any_rep)
{
  if (state == NULL)
    return false;
  return state->check_compatibility(num_streams,streams,
                                    num_layers,layers,any_rep);
}

/*****************************************************************************/
/*                  jpx_container_source::get_num_tracks                     */
/*****************************************************************************/

kdu_uint32 jpx_container_source::get_num_tracks()
{
  assert(state != NULL);
  assert(state->num_base_layers > 0);
  return state->num_tracks;
}

/*****************************************************************************/
/*               jpx_container_source::get_track_base_layers                 */
/*****************************************************************************/

int
  jpx_container_source::get_track_base_layers(kdu_uint32 track_idx,
                                              int &num_track_base_layers)
{
  num_track_base_layers = 0;
  if ((state == NULL) || (state->tracks == NULL) || (track_idx < 1))
    return 0;
  if (track_idx > state->num_tracks)
    track_idx = state->num_tracks;
  if (track_idx > state->tracks_completed)
    { 
      state->finish();
      if (track_idx > state->tracks_completed)
        return -1;
    }
  track_idx--; // Convert from 1-based to 0-based index
  jx_track_source *trk = &(state->tracks[track_idx]);
  num_track_base_layers = trk->layer_lim - trk->layer_start;
  return trk->layer_start;
}

/*****************************************************************************/
/*             jpx_container_source::access_presentation_track               */
/*****************************************************************************/

jpx_composition
  jpx_container_source::access_presentation_track(kdu_uint32 track_idx)
{
  jpx_composition result;
  if ((state == NULL) || (state->tracks == NULL) || (track_idx < 1))
    return result; // Empty interface
  if (track_idx > state->num_tracks)
    track_idx = state->num_tracks;
  if (track_idx > state->tracks_completed)
    { 
      state->finish();
      if (track_idx > state->tracks_completed)
        return result; // Empty interface
    }
  track_idx--; // Convert from 1-based to 0-based index
  jx_track_source *trk = &(state->tracks[track_idx]);
  if ((!trk->finished) &&
      !(trk->finished = trk->composition.finish()))
    return result; // Empty interface
  result = jpx_composition(&(trk->composition));
  return result;
}


/* ========================================================================= */
/*                           jx_codestream_source                            */
/* ========================================================================= */

/*****************************************************************************/
/*                    jx_codestream_source::donate_chdr_box                  */
/*****************************************************************************/

void
  jx_codestream_source::donate_chdr_box(jp2_input_box &src,
                                        int databin_nesting_level)
{
  if (restrict_to_jp2)
    { src.close(); return; }
  assert((!header_loc) && (!chdr) && (pending_subs == NULL));
  chdr.transplant(src);
  
  header_loc = chdr.get_locator();
  kdu_long header_span = chdr.get_remaining_bytes();
  if (header_span >= 0)
    header_span += chdr.get_box_header_length();
  jp2_locator contents_loc = chdr.get_contents_locator();
  if (contents_loc.get_databin_pos() > 0)
    chdr_nesting_level = databin_nesting_level + 1;
  else
    chdr_nesting_level = 0;
  jx_metanode *tree = owner->get_metatree();
  metanode = tree->add_numlist(1,&id,0,NULL,false,container,header_loc,true);
  assert((metanode->parse_state != NULL) &&
         (metanode->parse_state->read == NULL));
  metanode->parse_state->metanode_span = header_span;
  metanode->parse_state->read = new jx_metaread;
  metanode->parse_state->read->codestream_source = this;

  metanode->parse_state->box_databin_id = contents_loc.get_databin_id();
  metanode->parse_state->box_nesting_level = chdr_nesting_level;

  finish();
}

/*****************************************************************************/
/*                        jx_codestream_source::finish                       */
/*****************************************************************************/

bool
  jx_codestream_source::finish(bool invoked_by_metanode)
{
  if (metadata_finished)
    return true;
  
  if (!header_loc)
    { // Do sufficient parsing to obtain the Compositing Layer Header box.
      if (container != NULL)
        { 
          container->finish();
          if ((!header_loc) && !container->is_special())
            return false; // Note: special container is synthesized internally
                          // so we can continue without a CHDR box.
        }
      else
        while ((!header_loc) && !owner->are_top_codestreams_complete())
          if (!owner->parse_next_top_level_box())
            break;
    }

  if (chdr.exists())
    { // Parse as much as we can of the codestream header box
      bool chdr_complete = chdr.is_complete();
      jp2_input_box sub;
      while (sub.open(&chdr))
        { 
          bool sub_complete = sub.is_complete();
          if (sub.get_box_type() == jp2_image_header_4cc)
            { 
              have_dimensions = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                dimensions.init(&sub);
            }
          else if (sub.get_box_type() == jp2_bits_per_component_4cc)
            { 
              have_bpcc = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                dimensions.process_bpcc_box(&sub);
            }
          else if (sub.get_box_type() == jp2_palette_4cc)
            { 
              have_palette = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                palette.init(&sub);
            }
          else if (sub.get_box_type() == jp2_component_mapping_4cc)
            { 
              have_cmap = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                component_map.init(&sub);
            }
          else if (owner->test_box_filter(sub.get_box_type()))
            { // Put into the metadata management system
              metanode->read_and_insert_child(sub,chdr_nesting_level);
            }
          else
            sub.close(); // Skip it
          assert(!sub.exists());
        }
      
      // See if we have enough of codestream header to finish parsing it
      if (chdr_complete)
        { 
          chdr.close();
          jx_metanode *meta = metanode;
          this->metanode = NULL; // So we never refer to it again
          meta->flags |= JX_METANODE_CONTAINER_KNOWN;
          if ((meta->head == NULL) && (meta->linked_from == NULL))
            { // Ensure that `meta->remove_empty_shell' is called; this is safe,
              // because we have been careful not to allow anybody access to
              // the synthesized `meta' object until it has descendants.          
              assert(meta->rep_id == JX_NUMLIST_NODE);
              meta->numlist->unlink();
              delete meta->numlist;
              meta->numlist = NULL;
              meta->rep_id = 0;
            }
          if (!invoked_by_metanode)
            { // Otherwise, the following actions are performed within the caller,
              // i.e., at the end of `jx_metanode::finish_reading'.
              delete meta->parse_state->read;
              meta->parse_state->read = NULL;
              meta->check_parsing_complete();
              if ((meta->rep_id == 0) && (meta->parse_state == NULL))
                meta->remove_empty_shell();
            }
        }
      else if (have_dimensions && have_bpcc && have_palette && have_cmap)
        { // Donate `chdr' to the metadata management machinery to extract
          // any remaining boxes if it can.
          metanode->parse_state->read->codestream_source = NULL;
          metanode->parse_state->read->asoc.transplant(chdr);
          metanode = NULL; // We have no need to refer to it again
          assert(!chdr);
        }
    }
  
  if (chdr.exists())
    return false;
  
  if ((!header_loc) &&
      (((container != NULL) && !container->is_special()) ||
       ((container == NULL) && !owner->are_top_codestreams_complete())))
    return false; // Still waiting for the codestream header box
  
  while (pending_subs != NULL)
    { 
      if (!pending_subs->box.is_complete())
        return false;
      jx_pending_box *tmp = pending_subs;
      if (tmp->box.get_box_type() == jp2_image_header_4cc)
        dimensions.init(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_bits_per_component_4cc)
        dimensions.process_bpcc_box(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_palette_4cc)
        palette.init(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_component_mapping_4cc)
        component_map.init(&tmp->box);
      assert(!tmp->box.exists());
      pending_subs = tmp->next;
      delete tmp;
    }

  bool waiting_for_dflt=false;
  if (!metadata_finished)
    { 
      if (!dimensions.is_initialized())
        { // Dimensions information must be in default header box
          j2_dimensions *dflt =
            owner->get_default_dimensions();
          if (dflt == NULL)
            waiting_for_dflt = true;
          else if (dflt->is_initialized())
            dimensions.copy(dflt);
          else
            { KDU_ERROR(e,37); e <<
                KDU_TXT("JPX source contains no image header box for "
                "a codestream.  The image header (ihdr) box cannot be found in "
                "a codestream header (chdr) box, and does not exist within a "
                "default JP2 header (jp2h) box.");
            }
        }

      if (!palette.is_initialized())
        { // Palette info might be in default header box
          j2_palette *dflt = owner->get_default_palette();
          if (dflt == NULL)
            waiting_for_dflt = true;
          else if (dflt->is_initialized())
            palette.copy(dflt);
        }

      if (palette.is_initialized() && !component_map.is_initialized())
        { // Component mapping info must be in default header box
          j2_component_map *dflt =
            owner->get_default_component_map();
          if (dflt == NULL)
            waiting_for_dflt = true;
          else if (dflt->is_initialized())
            component_map.copy(dflt);
          else
            { KDU_ERROR(e,38); e <<
                KDU_TXT("JPX source contains a codestream with a palette "
                "(pclr) box, but no component mapping (cmap) box.  This "
                "illegal situation has been detected after examining both the "
                "codestream header (chdr) box, if any, for that codestream, "
                "and the default JP2 header (jp2h) box, if any.");
            }
        }

      if (!waiting_for_dflt)
        { 
          dimensions.finalize();
          palette.finalize();
          component_map.finalize(&dimensions,&palette);
          metadata_finished = true;
        }
    }
  
  if (waiting_for_dflt)
    { 
      if (owner->is_top_level_complete())
        { KDU_ERROR(e,0x25071208); e <<
          KDU_TXT("Expected a JP2 Header box to complete the descriptive "
                  "metadata (e.g., dimensions, component map, etc.) for "
                  "a codestream that does not have all this "
                  "information.  The file appears to be invalid.");
        }
      return false;
    }
  return true;
}

/*****************************************************************************/
/*                   jx_codestream_source::confirm_stream                    */
/*****************************************************************************/

bool
  jx_codestream_source::confirm_stream(int rep_idx)
{
  int cs_id = this->id;
  if (container != NULL)
    cs_id = container->map_codestream_id(cs_id,rep_idx);
  return owner->confirm_stream(cs_id);
}

/*****************************************************************************/
/*                     jx_codestream_source::get_stream                      */
/*****************************************************************************/

jx_fragment_lst *
  jx_codestream_source::get_stream(int rep_idx, bool want_ready)
{
  int cs_id = this->id;
  if (container != NULL)
    cs_id = container->map_codestream_id(cs_id,rep_idx);
  bool is_ready=false;
  jx_fragment_lst *flst = owner->find_stream_flst(cs_id,&is_ready);
  if (!is_ready)
    return (want_ready)?NULL:flst;
  if (flst != NULL)
    last_ready_rep_idx = rep_idx;
  return flst;
}


/* ========================================================================= */
/*                           jpx_codestream_source                           */
/* ========================================================================= */

/*****************************************************************************/
/*                  jpx_codestream_source::get_codestream_id                 */
/*****************************************************************************/

int
  jpx_codestream_source::get_codestream_id() const
{
  if ((state == NULL) || !state->metadata_finished)
    return -1;
  int result = state->id;
  if ((state_params.rep_idx > 0) && (state->container != NULL))
    result = state->container->map_codestream_id(result,state_params.rep_idx);
  return result;
}

/*****************************************************************************/
/*                   jpx_codestream_source::get_header_loc                   */
/*****************************************************************************/

jp2_locator
  jpx_codestream_source::get_header_loc() const
{
  if ((state == NULL) || !state->metadata_finished)
    return jp2_locator();
  return state->header_loc;
}

/*****************************************************************************/
/*                jpx_codestream_source::access_fragment_list                */
/*****************************************************************************/

jpx_fragment_list
  jpx_codestream_source::access_fragment_list()
{
  if (state != NULL)
    { 
      jx_fragment_lst *flst = state->get_stream(state_params.rep_idx,true);
      if ((flst != NULL) && (flst->count_box_frags() > 0))
        return jpx_fragment_list((jx_fragment_list *) flst);
    }
  return jpx_fragment_list();
}

/*****************************************************************************/
/*                 jpx_codestream_source::access_dimensions                  */
/*****************************************************************************/

jp2_dimensions
  jpx_codestream_source::access_dimensions(bool finalize_compatibility)
{
  if ((state == NULL) || !state->metadata_finished)
    return jp2_dimensions();
  jp2_dimensions result(&state->dimensions);
  if (finalize_compatibility && (!state->compatibility_finalized))
    {
      jpx_input_box *src = open_stream();
      if (src != NULL)
        {
          kdu_codestream cs;
          try {
              cs.create(src);
              result.finalize_compatibility(cs.access_siz());
            }
          catch (kdu_exception)
            { }
          if (cs.exists())
            cs.destroy();
          src->close();
          state->compatibility_finalized = true;
        }
    }
  return result;
}

/*****************************************************************************/
/*                   jpx_codestream_source::access_palette                   */
/*****************************************************************************/

jp2_palette
  jpx_codestream_source::access_palette()
{
  if ((state == NULL) || !state->metadata_finished)
    return jp2_palette();
  return jp2_palette(&state->palette);
}

/*****************************************************************************/
/*                   jpx_codestream_source::enum_layer_ids                   */
/*****************************************************************************/

int
  jpx_codestream_source::enum_layer_ids(int last_layer_id)
{
  if (state == NULL)
    return -1;
  jx_layer_source *lyr = NULL;
  if (last_layer_id < 0)
    { // See if we can parse any extra relevant compositing layer headers
      if (state->container == NULL)
        { // Top level codestream; consider only top-level layers
          while (!state->owner->are_top_codestreams_complete())
            if (!state->owner->parse_next_top_level_box())
              break;
          int n=0;
          while ((lyr = state->owner->get_top_layer(n++)) != NULL)
            lyr->finish();
        }
      else
        { 
          state->container->finish();
          int n = state->container->get_first_base_layer();
          while ((lyr = state->container->get_base_layer(n++)) != NULL)
            lyr->finish();
        }
    }
  
  lyr = NULL;
  int last_rep_idx = 0;
  if (last_layer_id >= 0)
    { // Start by locating `last_layer_id'
      if (state->container != NULL)
        lyr = state->container->match_layer(last_layer_id,last_rep_idx);
      else
        lyr = state->owner->get_compositing_layer(last_layer_id,last_rep_idx);
           // Note: we could just use the `owner->get_compositing_layer' to
           // recover `lyr'.  The reason for taking the container option in
           // the first branch of the "if" statement is that it could be a
           // lot faster.  If a codestream is defined within a JPX container
           // it can be referenced only by layers defined in the container.
    }
  if (lyr == NULL)
    lyr = state->referring_layers;
  else
    lyr = lyr->get_next_referring_layer(state->id);
  
  int result = -1;
  if (lyr != NULL)
    { 
      result = lyr->get_idx();
      if ((state->container != NULL) && (state_params.rep_idx > 0))
        result = state->container->map_layer_id(result,state_params.rep_idx);
    }
  return result;
}

/*****************************************************************************/
/*                    jpx_codestream_source::stream_ready                    */
/*****************************************************************************/

bool
  jpx_codestream_source::stream_ready()
{
  return (state != NULL) && state->stream_available(state_params.rep_idx,true);
}

/*****************************************************************************/
/*                    jpx_codestream_source::open_stream                     */
/*****************************************************************************/

jpx_input_box *
  jpx_codestream_source::open_stream(jpx_input_box *app_box)
{
  if (state == NULL)
    return NULL;
  jx_fragment_lst *flst = state->get_stream(state_params.rep_idx,true);
  if (flst == NULL)
    return NULL;
  if (app_box == NULL)
    { 
      if (state->stream_box.exists())
        { KDU_ERROR_DEV(e,39); e <<
          KDU_TXT("Attempting to use `jpx_codestream_source::open_stream' "
                  "a second time, to gain access to the same codestream, "
                  "without first closing the box.  To maintain multiple "
                  "open instances of the same codestream, you should "
                  "supply your own `jpx_input_box' object, rather than "
                  "attempting to use the internal resource multiple "
                  "times concurrently.");
        }
      app_box = &(state->stream_box);
    }
  kdu_long jp2c_pos = flst->get_jp2c_loc();
  if (jp2c_pos >= 0)
    { 
      jp2_locator loc;
      loc.set_file_pos(jp2c_pos);
      app_box->open(state->owner->get_ultimate_src(),loc);
    }
  else
    app_box->open_as(jpx_fragment_list((jx_fragment_list *) flst),
                     state->owner->get_data_references(),
                     state->owner->get_ultimate_src(),jp2_codestream_4cc);
  return app_box;
}


/* ========================================================================= */
/*                              jx_layer_source                              */
/* ========================================================================= */

/*****************************************************************************/
/*                      jx_layer_source::donate_jplh_box                     */
/*****************************************************************************/

void
  jx_layer_source::donate_jplh_box(jp2_input_box &src,
                                   int databin_nesting_level)
{
  assert((!header_loc) && (!jplh) && (!cgrp) && (pending_subs == NULL));
  jplh.transplant(src);
  header_loc = jplh.get_locator();
  kdu_long header_span = jplh.get_remaining_bytes();
  if (header_span >= 0)
    header_span += jplh.get_box_header_length();
  jp2_locator contents_loc = jplh.get_contents_locator();
  if (contents_loc.get_databin_pos() > 0)
    jplh_nesting_level = databin_nesting_level + 1;
  else
    jplh_nesting_level = 0;
  jx_metanode *tree = owner->get_metatree();
  metanode = tree->add_numlist(0,NULL,1,&id,false,container,header_loc,true);
  assert((metanode->parse_state != NULL) &&
         (metanode->parse_state->read == NULL));
  metanode->parse_state->metanode_span = header_span;
  metanode->parse_state->read = new jx_metaread;
  metanode->parse_state->read->layer_source = this;
  
  metanode->parse_state->box_databin_id = contents_loc.get_databin_id();
  metanode->parse_state->box_nesting_level = jplh_nesting_level;
  
  finish();
}

/*****************************************************************************/
/*                          jx_layer_source::finish                          */
/*****************************************************************************/

bool
  jx_layer_source::finish(bool invoked_by_metanode)
{
  if (finished)
    return true;

  if (!header_loc)
    { // Do sufficient parsing to obtain the Compositing Layer Header box.
      if (container != NULL)
        { 
          container->finish();
          if ((!header_loc) && !container->is_special())
            return false; // Note: special container is synthesized internally
                          // so we can continue with a JPLH box.
        }
      else
        while ((!header_loc) && !owner->are_top_layers_complete())
          if (!owner->parse_next_top_level_box())
            break;
    }

  j2_colour *cscan;

  if (jplh.exists())
    { // Parse as much as we can of the compositing layer header box
      bool jplh_complete = jplh.is_complete();
      jp2_input_box sub;
      while (sub.open(&jplh))
        { 
          bool sub_complete = sub.is_complete();
          if ((sub.get_box_type() == jp2_colour_group_4cc) && !cgrp.exists())
            { 
              have_cgrp = true;
              cgrp.transplant(sub);
              if (sub_complete)
                finish_cgrp();
            }
          else if (sub.get_box_type()==jp2_channel_definition_4cc)
            { 
              have_cdef_opct = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                channels.parse_cdef(&sub);
            }
          else  if (sub.get_box_type() == jp2_opacity_4cc)
            { 
              have_cdef_opct = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                channels.parse_opct(&sub);
            }
          else  if (sub.get_box_type() == jp2_pixel_format_4cc)
            { 
              if (!owner->get_pixel_format_allowed())
                owner->warn_pxfm_box();
              have_pxfm = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                channels.parse_pxfm(&sub);
            }
          else if (sub.get_box_type() == jp2_resolution_4cc)
            { 
              have_resolution = true;
              if (!(sub_complete && resolution.init(&sub)))
                add_pending_sub(sub);
            }
          else if (sub.get_box_type() == jp2_registration_4cc)
            { 
              have_registration = true;
              if (!sub_complete)
                add_pending_sub(sub);
              else
                registration.init(&sub);
            }
          else if (owner->test_box_filter(sub.get_box_type()))
            { // Put into the metadata management system
              metanode->read_and_insert_child(sub,jplh_nesting_level);
            }
          else
            sub.close(); // Discard it
        }
      assert(!sub.exists());
      
      // See if we have enough of the layer header to finish parsing it
      if (jplh_complete)
        { 
          jplh.close();
          jx_metanode *meta = metanode;
          this->metanode = NULL; // So we never refer to it again
          meta->flags |= JX_METANODE_CONTAINER_KNOWN;
          if ((meta->head == NULL) && (meta->linked_from == NULL))
            { // Ensure that `meta->remove_empty_shell' is called; safe,
              // because we have been careful not to allow anybody access to
              // the synthesized `meta' object until it has descendants.          
              assert(meta->rep_id == JX_NUMLIST_NODE);
              meta->numlist->unlink();
              delete meta->numlist;
              meta->numlist = NULL;
              meta->rep_id = 0;
            }
          if (!invoked_by_metanode)
            { // Otherwise, the following actions are performed in the caller,
              // i.e., at the end of `jx_metanode::finish_reading'.
              delete meta->parse_state->read;
              meta->parse_state->read = NULL;
              meta->check_parsing_complete();
              if ((meta->rep_id == 0) && (meta->parse_state == NULL))
                meta->remove_empty_shell();
            }
        }
      else if (have_cgrp && have_cdef_opct &&
               (have_pxfm || !owner->get_pixel_format_allowed()) &&
               have_resolution && have_registration)
        { // Donate `jplh' to the metadata management machinery to extract
          // any remaining boxes if it can.
          metanode->parse_state->read->layer_source = NULL;
          metanode->parse_state->read->asoc.transplant(jplh);
          metanode = NULL; // We have no need to refer to it again
          assert(!jplh);
        }
    }
  
  if (jplh.exists())
    return false;
  if ((!header_loc) &&
      (((container != NULL) && !container->is_special()) ||
       ((container == NULL) && !owner->are_top_layers_complete())))
    return false; // Still waiting for the compositing layer header box
  
  while (pending_subs != NULL)
    { 
      if (!pending_subs->box.is_complete())
        return false;
      jx_pending_box *tmp = pending_subs;
      if (tmp->box.get_box_type() == jp2_channel_definition_4cc)
        channels.parse_cdef(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_opacity_4cc)
        channels.parse_opct(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_pixel_format_4cc)
        channels.parse_pxfm(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_resolution_4cc)
        resolution.init(&tmp->box);
      else if (tmp->box.get_box_type() == jp2_registration_4cc)
        registration.init(&tmp->box);
      assert(!tmp->box.exists());
      pending_subs = tmp->next;
      delete tmp;
    }

  if (cgrp.exists() && !finish_cgrp())
    return false;

  // Check that all required codestreams are available
  registration.finalize(this->id); // Safe to do this multiple times
  int cs_id, cs_which, cs_rep_idx;
  bool have_unfinished_codestream = false;
  for (cs_which=0; cs_which < registration.num_codestreams; cs_which++)
    { 
      jx_registration::jx_layer_stream *stream =
        registration.codestreams + cs_which;
      if (stream->codestream_finished)
        continue;
      cs_id = stream->codestream_id;
      jx_codestream_source *cs=NULL;
      if (container != NULL)
        cs = container->match_codestream(cs_id,cs_rep_idx);
      if (cs == NULL)
        cs = owner->get_codestream(cs_id,cs_rep_idx,(container==NULL));
      if ((cs == NULL) && owner->are_top_codestreams_complete())
        { KDU_ERROR(e,40); e <<
            KDU_TXT("Encountered a JPX compositing layer box which "
            "utilizes a non-existent codestream!");
        }
      if ((cs == NULL) || !cs->finish())
        have_unfinished_codestream = true;
      else
        { // Mark as finished and link this object into the chain of
          // referring layers for the codestream.
          stream->codestream_finished = true;
          stream->next_referring_layer = cs->referring_layers;
          cs->referring_layers = this;
        }
    }
  if (have_unfinished_codestream)
    return false;

  // See if we need to evaluate the layer dimensions
  if ((registration.final_layer_size.x == 0) ||
      (registration.final_layer_size.y == 0))
    {
      registration.final_layer_size = kdu_coords(0,0);
      for (cs_which=0; cs_which < registration.num_codestreams; cs_which++)
        { 
          cs_id = registration.codestreams[cs_which].codestream_id;
          jx_codestream_source *cs=NULL;
          if (container != NULL)
            cs = container->match_codestream(cs_id,cs_rep_idx);
          if (cs == NULL)
            cs = owner->get_codestream(cs_id,cs_rep_idx);
          assert(cs != NULL);
          jpx_codestream_source cs_ifc(cs,0);
          kdu_coords size = cs_ifc.access_dimensions().get_size();
          size.x *= registration.codestreams[cs_which].sampling.x;
          size.y *= registration.codestreams[cs_which].sampling.y;
          size += registration.codestreams[cs_which].alignment;
          if ((cs_which == 0) || (size.x < registration.final_layer_size.x))
            registration.final_layer_size.x = size.x;
          if ((cs_which == 0) || (size.y < registration.final_layer_size.y))
            registration.final_layer_size.y = size.y;
        }
      registration.final_layer_size.x =
        ceil_ratio(registration.final_layer_size.x,registration.denominator.x);
      registration.final_layer_size.y =
        ceil_ratio(registration.final_layer_size.y,registration.denominator.y);
    }

  // Collect defaults, as required
  bool waiting_for_dflt=false;
  if (!colour.is_initialized())
    { // Colour description must be in the default header box
      j2_colour *dflt = owner->get_default_colour();
      if (dflt == NULL)
        waiting_for_dflt = true;
      else
        for (cscan=&colour; (dflt != NULL) && dflt->is_initialized();
             dflt=dflt->next)
          { 
            if (cscan->is_initialized())
              cscan = cscan->next = new j2_colour;
            cscan->copy(dflt);
          }
    }

  if (!channels.is_initialized())
    { // Channel defs (and even opacity) information might be in the default
      // header box
      j2_channels *dflt = owner->get_default_channels();
      if (dflt == NULL)
        waiting_for_dflt = true;
      else if (dflt->is_initialized())
        channels.copy(dflt);
    }
  
  if (!resolution.is_initialized())
    { // Resolution information might be in default header box
      j2_resolution *dflt = owner->get_default_resolution();
      if (dflt == NULL)
        waiting_for_dflt = true;
      else if (dflt->is_initialized())
        resolution.copy(dflt);
    }
  
  if (waiting_for_dflt)
    { 
      if (owner->is_top_level_complete())
        { KDU_ERROR(e,0x25071209); e <<
          KDU_TXT("Expected a JP2 Header box to complete the descriptive "
                  "metadata (e.g., colour, channel bindings, etc.) for "
                  "a compositing layer that does not have all this "
                  "information.  The file appears to be invalid.");
        }
      return false;
    }

  // Finalize boxes
  int num_colours = 0;
  for (cscan=&colour; (cscan != NULL) && (num_colours == 0); cscan=cscan->next)
    num_colours = cscan->get_num_colours();
  channels.finalize(num_colours,false);
  for (cs_which=0; cs_which < registration.num_codestreams; cs_which++)
    { 
      cs_id = registration.codestreams[cs_which].codestream_id;
      jx_codestream_source *cs=NULL;
      if (container != NULL)
        cs = container->match_codestream(cs_id,cs_rep_idx);
      if (cs == NULL)
        cs = owner->get_codestream(cs_id,cs_rep_idx);
      assert(cs != NULL);
      bool last_cs = (cs_which == (registration.num_codestreams-1));
      channels.find_cmap_channels(cs->get_component_map(),cs_id,last_cs);
    }
  if (!channels.all_cmap_channels_found())
    { KDU_ERROR(e,41); e <<
        KDU_TXT("JP2/JPX source is internally inconsistent.  "
        "Either an explicit channel mapping box, or the set of channels "
        "implicitly identified by a colour space box, cannot all be "
        "associated with available code-stream image components.");
    }
  
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    cscan->finalize(&channels);
  finished = true;
  return true;
}

/*****************************************************************************/
/*                       jx_layer_source::finish_cgrp                        */
/*****************************************************************************/

bool jx_layer_source::finish_cgrp()
{ 
  if (cgrp.exists() && !cgrp.is_complete())
    return false;
  while (cgrp.exists())
    { 
      kdu_long start_pos = cgrp.get_pos();
      jp2_input_box sub;
      if (!sub.open(&cgrp))
        { // All done
          cgrp.close();
          return true;
        }
      if (sub.get_box_type() == jp2_colour_4cc)
        { 
          if (!sub.is_complete())
            { 
              sub.close();
              cgrp.seek(start_pos);
              return false;
            }
          j2_colour *cscan = &colour;
          while (cscan->next != NULL)
            cscan = cscan->next;
          if (cscan->is_initialized())
            cscan = cscan->next = new j2_colour;
          cscan->init(&sub);
        }
      else
        sub.close();
      assert(!sub.exists());
    }
  return true;
}

/*****************************************************************************/
/*                  jx_layer_source::all_streams_available                   */
/*****************************************************************************/

bool
  jx_layer_source::all_streams_available(int rep_idx, bool want_ready)
{
  if (!finished)
    return false;
  if (rep_idx == last_ready_rep)
    return true;
  bool is_ready = true; // Until proven otherwise
  int cs_id, cs_which;
  for (cs_which=0; cs_which < registration.num_codestreams; cs_which++)
    { 
      cs_id = registration.codestreams[cs_which].codestream_id;
      jx_codestream_source *cs = owner->get_top_codestream(cs_id);
      if (cs == NULL)
        { 
          assert(container != NULL);
          cs = container->get_base_codestream(cs_id);
        }
      assert(cs != NULL);
      if (!cs->stream_available(rep_idx,want_ready))
        return false;
      if (is_ready && !(want_ready || cs->stream_available(rep_idx,true)))
        is_ready = false;
    }
  if (is_ready)
    last_ready_rep = rep_idx;
  return true;
}


/* ========================================================================= */
/*                             jpx_layer_source                              */
/* ========================================================================= */

/*****************************************************************************/
/*                       jpx_layer_source::get_layer_id                      */
/*****************************************************************************/

int
  jpx_layer_source::get_layer_id() const
{
  assert((state != NULL) && state->finished);
  int result = state->id;
  if ((state_params.rep_idx > 0) && (state->container != NULL))
    result = state->container->map_layer_id(result,state_params.rep_idx);
  return result;
}

/*****************************************************************************/
/*                      jpx_layer_source::get_header_loc                     */
/*****************************************************************************/

jp2_locator
  jpx_layer_source::get_header_loc() const
{
  assert((state != NULL) && state->finished);
  return state->header_loc;
}

/*****************************************************************************/
/*                     jpx_layer_source::access_channels                     */
/*****************************************************************************/

jp2_channels
  jpx_layer_source::access_channels()
{
  assert((state != NULL) && state->finished);
  int cs_threshold=0, cs_offset=0;
  if (state->container != NULL)
    state->container->get_codestream_map_params(cs_threshold,cs_offset,
                                                state_params.rep_idx);
  return jp2_channels(&state->channels,cs_threshold,cs_offset);
}

/*****************************************************************************/
/*                    jpx_layer_source::access_resolution                    */
/*****************************************************************************/

jp2_resolution
  jpx_layer_source::access_resolution()
{
  assert((state != NULL) && state->finished);
  return jp2_resolution(&state->resolution);
}

/*****************************************************************************/
/*                      jpx_layer_source::access_colour                      */
/*****************************************************************************/

jp2_colour
  jpx_layer_source::access_colour(int which)
{
  assert((state != NULL) && state->finished);
  j2_colour *scan=&state->colour;
  for (; (which > 0) && (scan != NULL); which--)
    scan = scan->next;
  return jp2_colour(scan);
}

/*****************************************************************************/
/*                   jpx_layer_source::get_num_codestreams                   */
/*****************************************************************************/

int
  jpx_layer_source::get_num_codestreams() const
{
  assert((state != NULL) && state->finished);
  return state->registration.num_codestreams;
}

/*****************************************************************************/
/*                    jpx_layer_source::get_codestream_id                    */
/*****************************************************************************/

int
  jpx_layer_source::get_codestream_id(int which) const
{
  assert((state != NULL) && state->finished);
  return state->get_codestream_id(which,state_params.rep_idx);
}

/*****************************************************************************/
/*                   jpx_layer_source::have_stream_headers                   */
/*****************************************************************************/

bool
  jpx_layer_source::have_stream_headers()
{
  assert(state != NULL);
  return state->all_streams_available(state_params.rep_idx,true);
}

/*****************************************************************************/
/*                    jpx_layer_source::get_layer_size                       */
/*****************************************************************************/

kdu_coords
  jpx_layer_source::get_layer_size() const
{
  assert((state != NULL) && state->finished);
  return state->registration.final_layer_size;
}

/*****************************************************************************/
/*               jpx_layer_source::get_codestream_registration               */
/*****************************************************************************/

int
  jpx_layer_source::get_codestream_registration(int which,
                                                kdu_coords &alignment,
                                                kdu_coords &sampling,
                                                kdu_coords &denominator) const
{
  assert((state != NULL) && state->finished);
  denominator = state->registration.denominator;
  if ((which < 0) || (which >= state->registration.num_codestreams))
    return -1;
  alignment = state->registration.codestreams[which].alignment;
  sampling = state->registration.codestreams[which].sampling;
  int cs_id = state->registration.codestreams[which].codestream_id;
  if (state->container != NULL)
    cs_id = state->container->map_codestream_id(cs_id,state_params.rep_idx);
  return cs_id;
}


/* ========================================================================= */
/*                           jx_multistream_source                           */
/* ========================================================================= */

/*****************************************************************************/
/*                     jx_multistream_source::parse_info                     */
/*****************************************************************************/

bool
  jx_multistream_source::parse_info()
{
  if (min_id <= 0)
    return false;
  if (lim_id != 0)
    return true;

  bool j2ci_invalid=false;
  kdu_uint32 Ncs=0, Ltbl=0;
  if (first_subbox_offset == 0)
    {  
      jp2_input_box sub;
      if (!sub.open(&main_box))
        return false;
      if (sub.has_caching_source())
        first_subbox_offset = -1;
      else
        first_subbox_offset = (int) sub.get_box_bytes();
      if (!sub.is_complete())
        { 
          j2ci_box.transplant(sub);
          return false;
        }
      if (!(sub.read(Ncs) && sub.read(Ltbl)))
        j2ci_invalid = true;
    }
  else
    { // Must be coming back to box fueled by dynamic cache
      assert(j2ci_box.exists() && (first_subbox_offset < 0));
      if (!j2ci_box.is_complete())
        return false;
      if (!(j2ci_box.read(Ncs) && j2ci_box.read(Ltbl)))
        j2ci_invalid = true;
    }
  if (j2ci_invalid)
    { KDU_ERROR(e,0x18071201); e <<
      KDU_TXT("Invalid J2CI (Multiple Codetream Info) box found while "
              "parsing JPX source -- each such box should hold two "
              "unsigned 32-bit integers: Ncs and Ltbl.");
    }
  kdu_uint32 max_val = (kdu_uint32)(INT_MAX-min_id);
  if (Ncs > max_val)
    Ncs = max_val; // Just in case overflow occurs -- crazy of course!
  lim_id = min_id + (int) Ncs;
  if ((Ltbl != 0) && (Ncs != 0))
    { 
      int Rval = (int)(Ltbl >> 26);
      Ltbl <<= 6;
      if (Rval < 31)
        { 
          streams_per_subbox = (1<<Rval);
          bytes_per_subbox = (int)(Ltbl >> 6);
        }
      if ((Ncs > (kdu_uint32)(1<<Rval)) && // Multiple sub-boxes
          (first_subbox_offset > 0)) // Subboxes randomly accessible
        { 
          num_descendants = 1 + ((((int) Ncs)-1) >> Rval);
          if (streams_per_subbox > 1)
            { // Helps to keep refs to multi-codestream descendants
              descendants = new jx_multistream_source *[num_descendants];
              memset(descendants,0,sizeof(void *)*(size_t)num_descendants);
            }
        }
    }
  owner->update_multistream_info(this);
  return true;
}

/*****************************************************************************/
/*                 jx_multistream_source::recover_codestream                 */
/*****************************************************************************/

bool
  jx_multistream_source::recover_codestream(int stream_id)
{
  assert((stream_id >= min_id) &&
         ((stream_id < lim_id) || (lim_id <= min_id)));
    // Otherwise, this function should not have been called in the first
    // place.
  
  if (first_subbox_offset == 0)
    { 
      parse_info();
      if (first_subbox_offset == 0)
        return false; // Still can't open the J2CI box
      if ((stream_id >= lim_id) && (lim_id > min_id))
        return false;
    }
  
  bool advertised_stream_not_found = false;
  bool success = false;
  jx_multistream_source *elt = NULL;
  if (num_descendants > 0)
    { // Use random access to parse sub-boxes
      assert((first_subbox_offset > 0) && (streams_per_subbox > 0));
      int elt_idx = (stream_id - this->min_id) / streams_per_subbox;
      assert(elt_idx < num_descendants);
      if ((descendants == NULL) || ((elt=descendants[elt_idx]) == NULL))
        { // Open the sub-box
          kdu_long pos = bytes_per_subbox;
          pos *= elt_idx; pos += first_subbox_offset;
          jp2_input_box sub;
          if (!(main_box.seek(pos) && sub.open(&main_box)))
            { KDU_ERROR(e,0x19071201); e <<
              KDU_TXT("Multiple Codestream (J2CX) box appears malformed; "
                      "embedded J2CI (info) sub-box advertises existence of "
                      "sub-boxes that cannot be read.  This error could also "
                      "arise if your data source does not support seeking.");
            }
          kdu_uint32 box_type = sub.get_box_type();
          if ((box_type == jp2_codestream_4cc) ||
              (box_type == jp2_fragment_table_4cc))
            { 
              fully_recovered_streams++;
              owner->found_codestream(stream_id,sub);
              success = true;
            }
          else if (box_type == jp2_multi_codestream_4cc)
            elt = add_descendant(min_id+elt_idx*streams_per_subbox,
                                 sub,elt_idx);
        }
      if (elt != NULL)
        { 
          success = elt->recover_codestream(stream_id);
          if (elt->check_fully_recovered())
            remove_descendant(elt,elt_idx);
          else if (streams_per_subbox == 1)
            { KDU_ERROR(e,0x19071202); e <<
              KDU_TXT("Multiple Codestream (J2CX) box appears to have invalid "
                      "J2CI info sub-box, advertising the fact that all "
                      "sub-boxes refer to at most one codestream whereas "
                      "at least one sub-box appears to refer to multiple "
                      "codestreams.");
            }
        }
      if (!success)
        advertised_stream_not_found = true;
    }
  else
    { // Sequential access
      elt = (stream_id < next_subbox_min_id)?head:tail;
      while (!success)
        { 
          if (elt == NULL)
            { // Need to open a new sub-box 
              if ((tail != NULL) && (next_subbox_min_id == tail->min_id))
                break; // We cannot read past `tail' yet
              jp2_input_box sub;
              kdu_uint32 box_type = 0;
              if (sub.open(&main_box))
                box_type = sub.get_box_type();
              else if (!main_box.is_complete())
                break; // We will have to come back another time
              if ((box_type == jp2_codestream_4cc) ||
                  (box_type == jp2_fragment_table_4cc))
                { 
                  fully_recovered_streams++;
                  owner->found_codestream(next_subbox_min_id,sub);
                  assert(!sub.exists());
                  success = (stream_id == next_subbox_min_id);
                  next_subbox_min_id++;
                  continue;
                }
              else if (box_type == jp2_multi_codestream_4cc)
                { 
                  elt = add_descendant(next_subbox_min_id,sub,-1);
                  if (elt->lim_id > next_subbox_min_id)
                    next_subbox_min_id = elt->lim_id;
                  assert(!sub.exists());
                }
              else
                { 
                  if (lim_id <= min_id)
                    lim_id = next_subbox_min_id;
                  else
                    advertised_stream_not_found = true;
                  break;
                }
            }
          if (stream_id < elt->min_id)
            break; // Should not happen
          if (elt->lim_id == 0)
            elt->parse_info();
          if (next_subbox_min_id < elt->lim_id)
            next_subbox_min_id = elt->lim_id;
          jx_multistream_source *enxt = elt->next; // In case `elt' is removed
          if ((elt->lim_id <= elt->min_id) || (stream_id < elt->lim_id))
            { 
              success = elt->recover_codestream(stream_id);
              if (next_subbox_min_id < elt->lim_id)
                next_subbox_min_id = elt->lim_id;
              if (stream_id < next_subbox_min_id)
                break; // No need to look any further
              if (elt->check_fully_recovered())
                remove_descendant(elt,-1);
            }
          elt = enxt;
        }
    }
  if (advertised_stream_not_found)
    { KDU_ERROR(e,0x19071203); e <<
      KDU_TXT("Multiple Codestream (J2CX) box advertises more "
              "codestreams than can be found within its sub-boxes.");
    }
  return success;
}

/*****************************************************************************/
/*                jx_multistream_source::discover_codestreams                */
/*****************************************************************************/

void jx_multistream_source::discover_codestreams()
{
  parse_info();
  int stream_id = -1;
  while ((lim_id <= min_id) && (next_subbox_min_id > stream_id))
    { 
      assert(num_descendants == 0); // We must be in sequential parsing mode
      stream_id = next_subbox_min_id;
      if ((tail != NULL) && (tail->min_id == stream_id))
        { 
          tail->discover_codestreams();
          if (tail->lim_id > next_subbox_min_id)
            next_subbox_min_id = tail->lim_id;
          if (tail->check_fully_recovered())
            remove_descendant(tail,-1);
        }
      else
        recover_codestream(stream_id);
    }
}

/*****************************************************************************/
/*                   jx_multistream_source::add_descendant                   */
/*****************************************************************************/

jx_multistream_source *
  jx_multistream_source::add_descendant(int min_stream_id, jp2_input_box &box,
                                        int idx)
{
  jx_multistream_source *elt = new jx_multistream_source(owner,this,box);
  if ((elt->prev = tail) == NULL)
    head = tail = elt;
  else
    tail = tail->next = elt;
  if (descendants != NULL)
    { 
      assert((idx >= 0) && (idx < num_descendants));
      assert(descendants[idx] == NULL);
      descendants[idx] = elt;
    }
  elt->init(min_stream_id);
  elt->parse_info();
  return elt;
}

/*****************************************************************************/
/*                  jx_multistream_source::remove_descendant                 */
/*****************************************************************************/

void
  jx_multistream_source::remove_descendant(jx_multistream_source *elt, int idx)
{
  assert(elt->lim_id > elt->min_id);
  fully_recovered_streams += elt->lim_id - elt->min_id;
  assert((this->lim_id <= this->min_id) ||
         (fully_recovered_streams <= (this->lim_id-this->min_id)));
  if (descendants != NULL)
    { 
      assert((idx >= 0) && (idx < num_descendants));
      descendants[idx] = NULL;
    }
  if (elt->prev == NULL)
    { 
      assert(elt == head);
      head = elt->next;
    }
  else
    elt->prev->next = elt->next;
  if (elt->next == NULL)
    { 
      assert(elt == tail);
      tail = elt->prev;
    }
  else
    elt->next->prev = elt->prev;
  delete elt;
}


/* ========================================================================= */
/*                              jx_stream_locator                            */
/* ========================================================================= */

/*****************************************************************************/
/*                   jx_stream_locator::~jx_stream_locator                   */
/*****************************************************************************/

jx_stream_locator::~jx_stream_locator()
{
  int n;
  if (shift > 0)
    for (n=0; n < JX_STREAM_LOCATOR_SLOTS; n++)
      { 
        jx_stream_locator *elt = descendants[n];
        descendants[n] = NULL;
        if (elt != NULL)
          delete elt;
      }
  else
    for (n=0; n < JX_STREAM_LOCATOR_SLOTS; n++)
      streams[n].reset();
}

/*****************************************************************************/
/*                     jx_stream_locator::add_codestream                     */
/*****************************************************************************/

void jx_stream_locator::add_codestream(int stream_idx, jp2_input_box &box)
{
  assert(stream_idx >= this->min_stream_idx);
  int slot_idx = (stream_idx - min_stream_idx) >> shift;
  if (slot_idx >= JX_STREAM_LOCATOR_SLOTS)
    { // Need to insert a new element above
      assert(parent == NULL);
      parent = new jx_stream_locator(owner,NULL);
      parent->min_stream_idx = 0;
      parent->shift = this->shift + JX_STREAM_LOCATOR_SHIFT;
      parent->descendants[0] = this;
      owner->change_stream_locator_root(parent);
      parent->add_codestream(stream_idx,box);
    }
  else if (shift > 0)
    { 
      jx_stream_locator *elt = descendants[slot_idx];
      if (elt == NULL)
        { // Create new descendant
          elt = new jx_stream_locator(owner,this);
          elt->min_stream_idx = this->min_stream_idx + (slot_idx<<shift);
          elt->shift = this->shift - JX_STREAM_LOCATOR_SHIFT;
          assert(elt->shift >= 0);
          descendants[slot_idx] = elt;
        }
      elt->add_codestream(stream_idx,box);
    }
  else
    { 
      jx_fragment_lst *lst = streams + slot_idx;
      if (box.get_box_type() == jp2_codestream_4cc)
        { 
          jp2_locator loc = box.get_locator();
          kdu_long file_pos = loc.get_file_pos();
          kdu_long cs_id = box.get_codestream_scope();
          if (cs_id >= 0)
            lst->set_incremental_codestream(cs_id,false);
          else
            { 
              assert(box.is_complete());
              lst->set_jp2c_loc(file_pos);
            }
        }
      else if (!lst->parse_ftbl(box))
        { // Still waiting for more data
          jp2_locator loc = box.get_locator();
          kdu_long file_pos = loc.get_file_pos();
          assert(file_pos >= 0);
          lst->set_ftbl_loc(file_pos);
        }
    }
  box.close();
}

/*****************************************************************************/
/*                     jx_stream_locator::get_codestream                     */
/*****************************************************************************/

jx_fragment_lst *jx_stream_locator::get_codestream(int stream_idx)
{
  int slot_idx = stream_idx - min_stream_idx;
  if (shift > 0)
    { 
      slot_idx >>= shift;
      jx_stream_locator *elt;
      if ((slot_idx < 0) || (slot_idx >= JX_STREAM_LOCATOR_SLOTS) ||
          ((elt = descendants[slot_idx]) == NULL))
        return NULL;
      else
        return elt->get_codestream(stream_idx);
    }
  jx_fragment_lst *lst = streams + slot_idx;
  if ((slot_idx < 0) || (slot_idx >= JX_STREAM_LOCATOR_SLOTS) ||
      lst->is_empty())
    lst = NULL;
  return lst;
}


/* ========================================================================= */
/*                                 jx_source                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                            jx_source::jx_source                           */
/*****************************************************************************/

jx_source::jx_source(jp2_family_src *src)
{
  ultimate_src = src;
  ultimate_src_id = src->get_id();
  have_signature = false;
  have_file_type = false;
  have_composition_box = false;
  have_reader_requirements = false;
  is_completely_open = false;
  restrict_to_jp2 = false;
  pixel_format_allowed = false;
  unexpected_pxfm_box = false;
  in_parse_next_top_level_box = false;
  num_top_codestreams = 0;
  num_top_layers = 0;
  total_codestreams = 0;
  total_layers = 0;
  top_level_complete = false;
  container_info_complete = false;
  jp2h_box_found = jp2h_box_complete = false;
  have_dtbl_box = false;
  max_top_codestreams = max_top_layers = 0;
  top_codestreams = NULL;
  top_layers = NULL;
  num_top_jplh_encountered = 0;
  num_top_chdr_encountered = 0;
  num_top_jp2c_or_ftbl_encountered = 0;
  total_jp2c_or_ftbl_confirmed = 0;
  next_multistream_min_id = 0;
  num_containers = 0;
  composition.init_for_reading(this,NULL,0,0);
  containers = NULL;
  first_unparsed_container = NULL;
  last_container = NULL;
  multistreams = NULL;
  first_unparsed_multistream = NULL;
  last_multistream = NULL;
  stream_locator = NULL;
  meta_manager.source = this;
  meta_manager.ultimate_src = src;
  meta_manager.tree = new jx_metanode(&meta_manager);
  meta_manager.tree->flags |= JX_METANODE_EXISTING | JX_METANODE_BOX_COMPLETE;
  meta_manager.tree->parse_state = new jx_metaparse();
}

/*****************************************************************************/
/*                           jx_source::~jx_source                           */
/*****************************************************************************/

jx_source::~jx_source()
{
  j2_colour *cscan;

  int n;
  for (n=0; n < num_top_codestreams; n++)
    if (top_codestreams[n] != NULL)
      { delete top_codestreams[n]; top_codestreams[n] = NULL; }
  if (top_codestreams != NULL)
    delete[] top_codestreams;
  for (n=0; n < num_top_layers; n++)
    if (top_layers[n] != NULL)
      { delete top_layers[n]; top_layers[n] = NULL; }
  if (top_layers != NULL)
    delete[] top_layers;
  while ((cscan=default_colour.next) != NULL)
    { default_colour.next = cscan->next; delete cscan; }
  while ((last_multistream=multistreams) != NULL)
    { multistreams = last_multistream->next; delete last_multistream; }
  meta_manager.tear_down();
  while ((last_container=containers) != NULL)
    { containers = last_container->get_next(); delete last_container; }
  if (stream_locator != NULL)
    delete stream_locator;
}

/*****************************************************************************/
/*                     jx_source::parse_next_top_level_box                   */
/*****************************************************************************/

bool
  jx_source::parse_next_top_level_box(bool already_open)
{
  if (!is_completely_open)
    return false;
  if (top_level_complete || in_parse_next_top_level_box)
    {
      assert(!already_open);
      return false;
    }
  if (!already_open)
    {
      assert(!top_box);
      if (!top_box.open_next())
        {
          if (!ultimate_src->is_top_level_complete())
            return false;

          if (!top_box.open_next()) // Try again, to avoid race conditions
            { 
              if (num_top_chdr_encountered == 0)
                { /* We have encountered no codestream header boxes, so there
                     must be one top-level codestream for each top-level
                     JP2C/FTBL box encountered. */
                  while (num_top_codestreams <
                         num_top_jp2c_or_ftbl_encountered)
                    add_top_codestream();
                  if (num_top_codestreams < 1)
                    { KDU_ERROR(e,51); e <<
                      KDU_TXT("JPX data source appears to contain no "
                              "codestreams at all.");
                    }
                }
              else if (num_top_codestreams > num_top_chdr_encountered)
                { KDU_ERROR(e,0x24081201); e <<
                  KDU_TXT("Looks like a top-level JPX compositing layer "
                          "header box refers to a non-existent top-level "
                          "codestream.  The file contains at least one "
                          "codestream header box, but not enough of them "
                          "to accommodate the referenced codestreams.");
                }
              top_level_complete = true;
              if (first_unparsed_container == NULL)
                container_info_complete = true;
              meta_manager.tree->flags |= JX_METANODE_CONTAINER_KNOWN;
              meta_manager.tree->check_parsing_complete();
              if (num_top_layers == 0)
                { /* We have encountered no compositing layer header boxes,
                     so there must be one top-level layer per top-level
                     codestream and there cannot be any JPX containers. */
                  assert((total_layers == 0) && (containers == NULL));
                  while (num_top_layers < num_top_codestreams)
                    add_top_layer();
                  assert(total_layers == num_top_layers);
                  if ((num_top_chdr_encountered == 0) &&
                      (next_multistream_min_id > 0))
                    { /* Special case with no explicit Compositing Layer or
                         Codestream header boxes.  We have found all top-level
                         JP2C/FTBL boxes already and identified them as
                         top-level codestreams above.  However, we also have
                         at least one Multiple Codestream box whose JP2C/FTBL
                         boxes will be interpreted as non top-level
                         codestreams as they are discovered.  There may be any
                         number of these that we have not discovered already.
                         To efficiently handle this situation, we create a
                         special indefinitely repeated JPX container. */
                      assert(multistreams != NULL);
                      containers = last_container = first_unparsed_container =
                        new jx_container_source(this,NULL,-1);
                      jx_container_source::parse_info(containers);
                    }
                }
              composition.set_layer_mapping(0,1,num_top_layers,num_top_layers);
                  // No harm done if above function has been called before
              return false;
            }
        }
    }

  in_parse_next_top_level_box = true;
  try {
    if (top_box.get_box_type() == jp2_dtbl_4cc)
      { 
        if (have_dtbl_box)
          { KDU_ERROR(e,42); e <<
            KDU_TXT("JP2-family data source appears to contain "
                    "more than one data reference (dtbl) box.  At most "
                    "one should be found in the file.");
          }
        have_dtbl_box = true;
        dtbl_box.transplant(top_box);
        if (dtbl_box.is_complete())
          { 
            data_references.init(&dtbl_box);
            assert(!dtbl_box);
          }
      }
    else if (top_box.get_box_type() == jp2_header_4cc)
      { 
        if (jp2h_box_found)
          { KDU_ERROR(e,43); e <<
            KDU_TXT("JP2-family data source contains more than one "
                    "top-level JP2 header (jp2h) box.");
          }
        jp2h_box_found = true;
        jp2h_box.transplant(top_box);
        finish_jp2_header_box();
      }
    else if ((top_box.get_box_type() == jp2_codestream_4cc) ||
             (top_box.get_box_type() == jp2_fragment_table_4cc))
      { 
        if (next_multistream_min_id > 0)
          { KDU_ERROR(e,0x23071202); e <<
            KDU_TXT("Top-level Contiguous Codestream boxes and Fragment "
                    "Table boxes must all precede any Multiple "
                    "Codestream boxes in a JPX file.");
          }
        this->found_codestream(num_top_jp2c_or_ftbl_encountered,top_box);
        num_top_jp2c_or_ftbl_encountered++;
        total_jp2c_or_ftbl_confirmed++;
        assert(!top_box);
        if (num_top_codestreams == 0)
          add_top_codestream(); // At least the first top-level stream must
                                // belong to a top-level codestream (i.e.,
                                // real or synthesized CHDR).
      }
    else if ((top_box.get_box_type() == jp2_codestream_header_4cc) &&
             !restrict_to_jp2)
      { 
        if (containers != NULL)
          { KDU_ERROR(e,0x23071203); e <<
            KDU_TXT("Top-level Codestream Header boxes must all precede any "
                    "Compositing Layer Extensions boxes in a JPX file.");
          }
        if (num_top_chdr_encountered == num_top_codestreams)
          add_top_codestream(); // We have found a new top-level codestream
        jx_codestream_source *cs=top_codestreams[num_top_chdr_encountered++];
        cs->donate_chdr_box(top_box,0);
        assert(!top_box);
      }
    else if ((top_box.get_box_type() == jp2_compositing_layer_hdr_4cc) &&
             !restrict_to_jp2)
      { 
        if (containers != NULL)
          { KDU_ERROR(e,0x23071204); e <<
            KDU_TXT("Top-level Compositing Layer Header boxes must all "
                    "precede any Compositing Layer Extensions boxes in a "
                    "JPX file.");
          }
        assert(num_top_jplh_encountered == num_top_layers);
        num_top_jplh_encountered++;
        jx_layer_source *lyr = add_top_layer();
        lyr->donate_jplh_box(top_box,0);
        assert(!top_box);
      }
    else if ((top_box.get_box_type() == jp2_multi_codestream_4cc) &&
             !restrict_to_jp2)
      { 
        if (num_top_codestreams == 0)
          { KDU_ERROR(e,0x24071201); e <<
            KDU_TXT("At least one top-level Contiguous Codestream box or "
                    "Fragment Table box must precede any Multiple Codestream "
                    "box in a legal JPX file.");
          }
        if (next_multistream_min_id == 0)
          next_multistream_min_id = num_top_codestreams;
        jx_multistream_source *elt =
          new jx_multistream_source(this,NULL,top_box);
        if ((elt->prev = last_multistream) != NULL)
          last_multistream = last_multistream->next = elt;
        else
          multistreams = last_multistream = elt;
        if (first_unparsed_multistream == NULL)
          { 
            first_unparsed_multistream = elt;
            elt->init(next_multistream_min_id);
            elt->parse_info();
          }
        assert(!top_box);
      }
    else if (top_box.get_box_type() == jp2_composition_4cc)
      { 
        composition.donate_composition_box(top_box);
        have_composition_box = true;
        assert(!top_box);
      }
    else if ((top_box.get_box_type() == jp2_layer_extensions_4cc) &&
             !restrict_to_jp2)
      { 
        if (num_top_layers == 0)
          { KDU_ERROR(e,0x24071202); e <<
            KDU_TXT("At least one top-level Compositing Layer box must "
                    "precede any Compositing Layer Extensions box "
                    "in a legal JPX file.");
          }
        if (num_top_chdr_encountered == 0)
          { KDU_ERROR(e,0x2507120A); e <<
            KDU_TXT("At least one top-level Codestream Header box must "
                    "precede any Compositing Layer Extensions box "
                    "in a legal JPX file.");
          }
        if (num_top_chdr_encountered != num_top_codestreams)
          { KDU_ERROR(e,0x24081202); e <<
            KDU_TXT("Looks like a top-level JPX Compositing Layer Header "
                    "box refers to a non-existent top-level codestream; "
                    "top-level Codestream Header boxes must all appear "
                    "before Compositing Layer Extensions boxes.");
          }
        if (!have_composition_box)
          { KDU_ERROR(e,0x24071203); e <<
            KDU_TXT("Top-level Composition box must precede any "
                    "Compositing Layer Extensions box in a legal JPX file.");
          }
        composition.set_layer_mapping(0,1,num_top_layers,num_top_layers);
        jx_container_source *elt =
          new jx_container_source(this,&top_box,num_containers++);
        if ((elt->prev = last_container) != NULL)
          { 
            last_container->next = elt;
            last_container = elt;
          }
        else
          { 
            containers = last_container = elt;
            meta_manager.containers = elt;
          }
        if (first_unparsed_container == NULL)
          { 
            first_unparsed_container = elt;
            jx_container_source::parse_info(elt);
          }
        assert(!top_box);
      }
    else if (meta_manager.test_box_filter(top_box.get_box_type()))
      { // Build into meta-data structure
        jx_metanode *new_node = new jx_metanode(&meta_manager);
        meta_manager.tree->insert_child(new_node,meta_manager.tree->tail,
                                        top_box.get_locator());
        new_node->donate_input_box(top_box,0);
        assert(!top_box.exists());
        if (new_node->finish_reading() && (new_node->rep_id == 0) &&
            (new_node->parse_state == NULL))
          new_node->remove_empty_shell(); // Deletes `new_node'
      }
    else
      top_box.close();
  }
  catch (...) {
    top_box.close();
    in_parse_next_top_level_box = false;
    throw;
  }
  in_parse_next_top_level_box = false;

  if (restrict_to_jp2 && (num_top_layers == 0) &&
      (total_codestreams > 0))
    { // JP2 files have one layer, corresponding to the 1st and only
      // top-level codestream
      assert(top_layers == NULL);
      add_top_layer();
    }
  
  return true;
}

/*****************************************************************************/
/*                      jx_source::finish_jp2_header_box                     */
/*****************************************************************************/

bool
  jx_source::finish_jp2_header_box()
{
  if (jp2h_box_complete)
    return true;
  while ((!jp2h_box_found) && !is_top_level_complete())
    if (!parse_next_top_level_box())
      break;
  if (!jp2h_box_found)
    return top_level_complete;
  
  bool result = false;
  assert(jp2h_box.exists());
  bool box_complete = jp2h_box.is_complete();
  while (sub_box.exists() || sub_box.open(&jp2h_box))
    { 
      bool sub_complete = sub_box.is_complete();
      if (sub_box.get_box_type() == jp2_image_header_4cc)
        {
          if (!sub_complete)
            break;
          default_dimensions.init(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_bits_per_component_4cc)
        {
          if (!sub_complete)
            break;
          default_dimensions.process_bpcc_box(&sub_box);
          have_default_bpcc = true;
        }
      else if (sub_box.get_box_type() == jp2_palette_4cc)
        {
          if (!sub_complete)
            break;
          default_palette.init(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_component_mapping_4cc)
        {
          if (!sub_complete)
            break;
          default_component_map.init(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_colour_4cc)
        {
          if (!sub_complete)
            break;
          j2_colour *cscan;
          for (cscan=&default_colour; cscan->next != NULL;
               cscan=cscan->next);
          if (cscan->is_initialized())
            cscan = cscan->next = new j2_colour;
          cscan->init(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_channel_definition_4cc)
        {
          if (!sub_complete)
            break;
          default_channels.parse_cdef(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_pixel_format_4cc)
        { 
          if (!pixel_format_allowed)
            warn_pxfm_box();
          if (!sub_complete)
            break;
          default_channels.parse_pxfm(&sub_box);
        }
      else if (sub_box.get_box_type() == jp2_resolution_4cc)
        {
          if (!sub_complete)
            break;
          default_resolution.init(&sub_box);
        }
      else
        sub_box.close(); // Skip over other boxes.
    }
  
  // See if we have enough of the JP2 header to finish parsing it
  if ((!sub_box) &&
      (box_complete ||
       (default_dimensions.is_initialized() && have_default_bpcc &&
        default_palette.is_initialized() &&
        default_component_map.is_initialized() &&
        default_channels.is_fully_parsed(pixel_format_allowed) &&
        default_colour.is_initialized() &&
        default_resolution.is_initialized())))
    { 
      jp2h_box.close();
      default_resolution.finalize();
      jp2h_box_complete = true;
      result = true;
    }

  return result;
}

/*****************************************************************************/
/*                           jx_source::warn_pxfm_box                        */
/*****************************************************************************/

void jx_source::warn_pxfm_box()
{
  if (unexpected_pxfm_box)
    return; // Warning generated before
  unexpected_pxfm_box = true;
  KDU_WARNING(w,0x25011601); w <<
  KDU_TXT("A \"pixel format\" (pxfm) box has been encountered "
          "in a file not advertising its existence.  The box will be parsed "
          "anyway, but file reading might not work correctly in all "
          "circumstances, especially if delivered incrementally via JPIP.  "
          "The \"reader requirements\" (rreq) box should advertise the "
          "existence of non-default pixel format features; amongst other "
          "things, this allows a reader to know when it has read sufficient "
          "sub-boxes from a JP2 header box or a compositing layer header "
          "box.");
}

/*****************************************************************************/
/*                        jx_source::add_top_codestream                      */
/*****************************************************************************/

jx_codestream_source *jx_source::add_top_codestream()
{
  if (max_top_codestreams <= num_top_codestreams)
    {
      int new_max_cs = max_top_codestreams*2 + 1;
      jx_codestream_source **buf = new jx_codestream_source *[new_max_cs];
      memset(buf,0,sizeof(jx_codestream_source *)*(size_t)new_max_cs);
      if (top_codestreams != NULL)
        {
          memcpy(buf,top_codestreams,
                 sizeof(jx_codestream_source *)*(size_t)num_top_codestreams);
          delete[] top_codestreams;
        }
      top_codestreams = buf;
      max_top_codestreams = new_max_cs;
    }
  jx_codestream_source *result = top_codestreams[num_top_codestreams] =
    new jx_codestream_source(this,num_top_codestreams,restrict_to_jp2);
  num_top_codestreams++;
  update_total_codestreams(num_top_codestreams);
  return result;
}

/*****************************************************************************/
/*                         jx_source::add_top_layer                          */
/*****************************************************************************/

jx_layer_source *jx_source::add_top_layer()
{
  if (max_top_layers <= num_top_layers)
    {
      int new_max_layers = max_top_layers*2 + 1;
      jx_layer_source **buf = new jx_layer_source *[new_max_layers];
      memset(buf,0,sizeof(jx_layer_source *)*(size_t)new_max_layers);
      if (top_layers != NULL)
        {
          memcpy(buf,top_layers,
                 sizeof(jx_layer_source *)*(size_t)num_top_layers);
          delete[] top_layers;
        }
      top_layers = buf;
      max_top_layers = new_max_layers;
    }
  jx_layer_source *result = top_layers[num_top_layers] =
    new jx_layer_source(this,num_top_layers);
  num_top_layers++;
  update_total_layers(num_top_layers);
  return result;
}

/*****************************************************************************/
/*                     jx_source::update_container_info                      */
/*****************************************************************************/

void jx_source::update_container_info(jx_container_source *obj,
                                      int layer_count, int codestream_count,
                                      bool indefinite_reps)
{
  assert(obj == first_unparsed_container);
  first_unparsed_container = obj->get_next();
  update_total_layers(layer_count);
  update_total_codestreams(codestream_count);
  if (indefinite_reps ||
      (top_level_complete && (first_unparsed_container == NULL)))
    container_info_complete = true;
}

/*****************************************************************************/
/*                    jx_source::update_multistream_info                     */
/*****************************************************************************/

void jx_source::update_multistream_info(jx_multistream_source *obj)
{
  assert(obj->lim_id >= obj->min_id);
  if (obj == first_unparsed_multistream)
    { 
      first_unparsed_multistream = obj->next;
      assert(next_multistream_min_id == obj->min_id);
      next_multistream_min_id = obj->lim_id;
      if (first_unparsed_multistream != NULL)
        first_unparsed_multistream->init(next_multistream_min_id);
    }
  else
    assert(obj->parent != NULL);
  update_total_codestreams(obj->lim_id);
  if (obj->lim_id > total_jp2c_or_ftbl_confirmed)
    total_jp2c_or_ftbl_confirmed = obj->lim_id;
}

/*****************************************************************************/
/*               jx_source::remove_fully_recovered_multistream               */
/*****************************************************************************/

void
  jx_source::remove_fully_recovered_multistream(jx_multistream_source *obj)
{
  assert((obj->parent == NULL) && (obj->lim_id > obj->min_id));
  assert(obj != first_unparsed_multistream);
  if (obj->prev == NULL)
    { 
      assert(multistreams == obj);
      multistreams  = obj->next;
    }
  else
    obj->prev->next = obj->next;
  if (obj->next == NULL)
    { 
      assert(last_multistream == obj);
      last_multistream = obj->prev;
    }
  else
    obj->next->prev = obj->prev;
  delete obj;
}

/*****************************************************************************/
/*                        jx_source::find_stream_flst                        */
/*****************************************************************************/

jx_fragment_lst *
  jx_source::find_stream_flst(int stream_idx, bool *is_ready)
{
  *is_ready = false; // Until proven otherwise
  
  // Start by trying to limit our attention to streams that
  // have already been located or that may belong to the top-level.
  jx_fragment_lst *flst=NULL;
  while (((stream_locator == NULL) ||
          ((flst = stream_locator->get_codestream(stream_idx)) == NULL)) &&
         (next_multistream_min_id == 0))
    if (!parse_next_top_level_box())
      break;
  if ((flst == NULL) && (next_multistream_min_id > 0))
    { // We expect to find `flst' within a Multiple Codestream
      // box somewhere -- i.e., we expect to recover it via calls to
      // `jx_multistream_source::recover_codestream'.
      assert(stream_idx >= num_top_codestreams);
      jx_multistream_source *scan = NULL;
      bool success = false;
      while (!success)
        { 
          if ((scan == NULL) && ((scan = first_unparsed_multistream) == NULL))
            scan = last_multistream;
          if ((scan == NULL) || (stream_idx < scan->min_id))
            scan = multistreams; // Start scanning from the head of the list
          if (scan == NULL)
            { // We should try to find another multistream
              if (!parse_next_top_level_box())
                break;
              continue;
            }
          if (stream_idx < scan->min_id)
            { // Weird -- failed to find stream in any remaining multistream
              // object; yet it was not found in the `stream_locator' database.
              // Something must have gone wrong!
              assert(0);
              break;
            }
          if (scan == first_unparsed_multistream)
            { 
              assert(scan->lim_id == 0);
              scan->parse_info(); // See if we can advance matters
            }
          if ((scan->lim_id > scan->min_id) && (stream_idx >= scan->lim_id))
            { 
              scan = scan->next;
              continue;
            }
          success = scan->recover_codestream(stream_idx);
          if (scan->check_fully_recovered())
            { 
              remove_fully_recovered_multistream(scan);
              scan = NULL;
            }
          else if ((!success) &&
                   ((scan->lim_id <= scan->min_id) ||
                    (stream_idx < scan->lim_id)))
            break; // No point in trying again
        }
      
      if (success)
        { // `flst' should now be available from the `stream_locator'
          if (stream_locator != NULL)
            flst = stream_locator->get_codestream(stream_idx);
          assert(flst != NULL);
        }
    }
  
  if (flst == NULL)
    { // See if this is a permanent or temporary situation
      if (top_level_complete && (first_unparsed_multistream == NULL) &&
          (stream_idx >= next_multistream_min_id))
        { KDU_ERROR(e,0x2507120B); e <<
          KDU_TXT("JPX source does not contain any Contiguous Codestream "
                  "or Fragment Table box, either at the top level or "
                  "within a Multiple Codestream box, for the logical "
                  "codestream with (zero based) index ") << stream_idx << ".";
        }
      return NULL;
    }
  
  // We are now in a position to return a non-NULL `flst'; however, we
  // still need to figure out if the main header `is_ready' or not.
  kdu_long ftbl_pos = flst->get_ftbl_loc();
  if (ftbl_pos >= 0)
    { 
      jp2_locator loc; loc.set_file_pos(ftbl_pos);
      jp2_input_box box;
      if (!(box.open(ultimate_src,loc) && flst->parse_ftbl(box)))
        return flst;
    }
  bool main_header_complete=false;
  kdu_long cs_id = flst->get_incremental_codestream(main_header_complete);
  if ((cs_id >= 0) && !main_header_complete)
    { // See if main header is now complete
      if (!ultimate_src->is_codestream_main_header_complete(cs_id))
        return flst;
      flst->set_incremental_codestream(cs_id,true);
    }
  *is_ready = true;
  return flst;
}

/*****************************************************************************/
/*                         jx_source::find_all_streams                       */
/*****************************************************************************/

bool jx_source::find_all_streams()
{
  bool count_known = false;
  while (!count_known)
    { 
      jx_multistream_source *elt = first_unparsed_multistream;
      if ((elt == NULL) && ((elt = last_multistream) != NULL))
        { // See if `last_multistream' has an unknown number of codestreams
          if (elt->lim_id > elt->min_id)
            elt = NULL; // We know about all codestreams already
        }
      if (elt != NULL)
        { 
          elt->discover_codestreams();
          if (!elt->parse_info())
            break;
          assert(first_unparsed_multistream == elt->next);
          if (elt->check_fully_recovered())
            remove_fully_recovered_multistream(elt);
          continue;
        }
      if (!parse_next_top_level_box())
        { 
          if (top_level_complete &&
              ((last_multistream == NULL) ||
               (last_multistream->lim_id > last_multistream->min_id)))
            count_known = true;
          break;
        }
    }
  return count_known;
}


/* ========================================================================= */
/*                                 jpx_source                                */
/* ========================================================================= */

/*****************************************************************************/
/*                           jpx_source::jpx_source                          */
/*****************************************************************************/

jpx_source::jpx_source()
{
  state = NULL;
}

/*****************************************************************************/
/*                              jpx_source::open                             */
/*****************************************************************************/

int
  jpx_source::open(jp2_family_src *src, bool return_if_incompatible)
{
  if (state == NULL)
    state = new jx_source(src);
  if (state->is_completely_open)
    { KDU_ERROR_DEV(e,44); e <<
        KDU_TXT("Attempting invoke `jpx_source::open' on a JPX "
        "source object which has been completely opened, but not yet closed.");
    }
  if ((state->ultimate_src != src) ||
      (src->get_id() != state->ultimate_src_id))
    { // Treat as though it was closed
      delete state;
      state = new jx_source(src);
    }

  // First, check for a signature box
  if (!state->have_signature)
    {
      if (!((state->top_box.exists() || state->top_box.open(src)) &&
            state->top_box.is_complete()))
        {
          if (src->uses_cache())
            return 0;
          else
            {
              close();
              if (return_if_incompatible)
                return -1;
              else
                { KDU_ERROR(e,45); e <<
                    KDU_TXT("Data source supplied to `jpx_source::open' "
                    "does not commence with a valid JP2-family signature "
                    "box.");
                }
            }
        }
      kdu_uint32 signature;
      if ((state->top_box.get_box_type() != jp2_signature_4cc) ||
          !(state->top_box.read(signature) && (signature == jp2_signature) &&
            (state->top_box.get_remaining_bytes() == 0)))
        {
          close();
          if (return_if_incompatible)
            return -1;
          else
            { KDU_ERROR(e,46); e <<
                KDU_TXT("Data source supplied to `jpx_source::open' "
                "does not commence with a valid JP2-family signature box.");
            }
        }
      state->top_box.close();
      state->have_signature = true;
    }

  // Next, check for a file-type box
  if (!state->have_file_type)
    {
      if (!((state->top_box.exists() || state->top_box.open_next()) &&
            state->top_box.is_complete()))
        {
          if (src->uses_cache())
            return 0;
          else
            {
              close();
              if (return_if_incompatible)
                return -1;
              else
                { KDU_ERROR(e,47);  e <<
                    KDU_TXT("Data source supplied to `jpx_source::open' "
                    "does not contain a correctly positioned file-type "
                    "(ftyp) box.");
                }
            }
        }
      if (state->top_box.get_box_type() != jp2_file_type_4cc)
        {
          close();
          if (return_if_incompatible)
            return -1;
          else
            { KDU_ERROR(e,48); e <<
                KDU_TXT("Data source supplied to `jpx_source::open' "
                "does not contain a correctly positioned file-type "
                "(ftyp) box.");
            }
        }
      if (!state->compatibility.init_ftyp(&state->top_box))
        { // Not compatible with JP2 or JPX.
          close();
          if (return_if_incompatible)
            return -1;
          else
            { KDU_ERROR(e,49); e <<
                KDU_TXT("Data source supplied to `jpx_source::open' "
                "contains a correctly positioned file-type box, but that box "
                "does not identify either JP2 or JPX as a compatible file "
                "type.");
            }
        }
      assert(!state->top_box);
      state->have_file_type = true;
      jpx_compatibility compat(&state->compatibility);
      state->restrict_to_jp2 = compat.is_jp2();
    }

  if (state->restrict_to_jp2)
    { // Ignore reader requirements box, if any
      state->is_completely_open = true;
      return 1;
    }

  // Need to check for a reader requirements box
  assert(!state->have_reader_requirements);
  if (!((state->top_box.exists() || state->top_box.open_next()) &&
        ((state->top_box.get_box_type() != jp2_reader_requirements_4cc) ||
         state->top_box.is_complete())))
    {
      if (src->uses_cache())
        return 0;
      else
        {
          close();
          if (return_if_incompatible)
            return -1;
          else
            { KDU_ERROR(e,50); e <<
                KDU_TXT("Data source supplied to `jpx_source::open' "
                "does not contain a correctly positioned reader "
                "requirements box.");
            }
        }
    }
  state->is_completely_open = true;
  if (state->top_box.get_box_type() == jp2_reader_requirements_4cc)
    { // Allow the reader requirements box to be missing, even though it
      // is not strictly legal
      state->compatibility.init_rreq(&state->top_box);
      state->have_reader_requirements = true;
      state->pixel_format_allowed =
        state->compatibility.check_pixel_format_features();
      assert(!state->top_box);
    }
  else
    state->parse_next_top_level_box(true);

  return 1;
}

/*****************************************************************************/
/*                        jpx_source::get_ultimate_src                      */
/*****************************************************************************/

jp2_family_src *
  jpx_source::get_ultimate_src()
{
  if (state == NULL)
    return NULL;
  else
    return state->ultimate_src;
}

/*****************************************************************************/
/*                              jpx_source::close                            */
/*****************************************************************************/

bool
  jpx_source::close()
{
  if (state == NULL)
    return false;
  bool result = state->is_completely_open;
  delete state;
  state = NULL;
  return result;
}

/*****************************************************************************/
/*                     jpx_source::access_data_references                    */
/*****************************************************************************/

jp2_data_references
  jpx_source::access_data_references()
{
  if ((state == NULL) || !state->is_completely_open)
    return jp2_data_references(NULL);
  while ((!state->have_dtbl_box) && (!state->top_level_complete) &&
         state->parse_next_top_level_box());
  if (state->dtbl_box.exists() && state->dtbl_box.is_complete())
    {
      state->data_references.init(&state->dtbl_box);
      assert(!(state->dtbl_box));
    }
  if (state->have_dtbl_box || state->top_level_complete)
    return jp2_data_references(&(state->data_references));
  else
    return jp2_data_references(NULL);
}

/*****************************************************************************/
/*                      jpx_source::access_compatibility                     */
/*****************************************************************************/

jpx_compatibility
  jpx_source::access_compatibility()
{
  jpx_compatibility result;
  if ((state != NULL) & state->is_completely_open)
    result = jpx_compatibility(&state->compatibility);
  return result;
}

/*****************************************************************************/
/*                        jpx_source::count_codestreams                      */
/*****************************************************************************/

bool
  jpx_source::count_codestreams(int &count)
{
  if ((state == NULL) || !state->is_completely_open)
    { count = 0; return false; }
  while (!state->container_info_complete)
    { 
      if ((state->first_unparsed_container != NULL) &&
          (jx_container_source::parse_info(state->first_unparsed_container) !=
           NULL))
        break; // Unable to parse all available containers' info sub-boxes yet
      if (!state->parse_next_top_level_box())
        break;
    }
  bool count_known = false;
  if (state->container_info_complete)
    { // The count is known so long as the last container has a fixed
      // number of repetitions or there are not containers.
      if ((state->containers == NULL) ||
          !state->last_container->indefinitely_repeated())
        count_known = true;
    }
    
  // If the count remains unknown, we should endeavour to discover as many
  // actual JP2C/FTBL boxes as possible, since these may give us a larger
  // codestream count than the one we have already.  Some of these may still
  // remain at the top level of the file, while others may remain undiscovered
  // within parsed or unparsed Multiple Codestream boxes (multistreams).
  if (!count_known)
    count_known = state->find_all_streams();
  count = state->total_codestreams;
  return count_known;
}

/*****************************************************************************/
/*                   jpx_source::count_compositing_layers                    */
/*****************************************************************************/

bool
  jpx_source::count_compositing_layers(int &count)
{
  if ((state == NULL) || !state->is_completely_open)
    { count = 0; return false; }
  while (!state->container_info_complete)
    { 
      if ((state->first_unparsed_container != NULL) &&
          (jx_container_source::parse_info(state->first_unparsed_container) !=
           NULL))
        break; // Unable to parse all available containers' info sub-boxes yet
      if (!state->parse_next_top_level_box())
        break;
    }
  bool count_known = false;
  if (state->container_info_complete)
    { // The count is known so long as the last container has a fixed
      // number of repetitions or there are not containers.
      if ((state->containers == NULL) ||
          !state->last_container->indefinitely_repeated())
        count_known = true;
      else if (state->containers != NULL)
        count_known = state->find_all_streams();
    }
  count = state->total_layers;
  if ((count < 1) && state->restrict_to_jp2)
    count = 1; // We always have at least one, even if we have to fake it
  return (count_known || state->restrict_to_jp2);
}

/*****************************************************************************/
/*                        jpx_source::count_containers                       */
/*****************************************************************************/

bool
  jpx_source::count_containers(int &count)
{
  if ((state == NULL) || (!state->is_completely_open) ||
      state->restrict_to_jp2)
    { count = 0; return false; }
  bool result = state->find_all_container_info();
  if (state->first_unparsed_container != NULL)
    count = state->first_unparsed_container->get_id();
  else
    count = state->num_containers;
  return result;
}

/*****************************************************************************/
/*                       jpx_source::access_codestream                       */
/*****************************************************************************/

jpx_codestream_source
  jpx_source::access_codestream(int which, bool need_main_header)
{
  jpx_codestream_source result;
  if ((state == NULL) || (!state->is_completely_open) || (which < 0))
    return result; // Return an empty interface
  int rep_idx=0;
  jx_codestream_source *cs = state->get_codestream(which,rep_idx);
  if (cs == NULL)
    return result;
  if (cs->finish() && cs->stream_available(rep_idx,need_main_header))
    result = jpx_codestream_source(cs,rep_idx);
  return result;
}

/*****************************************************************************/
/*                          jpx_source::access_layer                         */
/*****************************************************************************/

jpx_layer_source
  jpx_source::access_layer(int which, bool need_stream_headers)
{
  jpx_layer_source result;
  if ((state == NULL) || (!state->is_completely_open) || (which < 0))
    return result; // Return an empty interface
  if (state->restrict_to_jp2 && (which != 0))
    return result; // Return empty interface
  int rep_idx=0;
  jx_layer_source *layer = state->get_compositing_layer(which,rep_idx);
  if (layer == NULL)
    return result;
  if (layer->finish() &&
      layer->all_streams_available(rep_idx,need_stream_headers))
    result = jpx_layer_source(layer,rep_idx);
  return result;
}

/*****************************************************************************/
/*                       jpx_source::access_container                        */
/*****************************************************************************/

jpx_container_source
  jpx_source::access_container(int which)
{
  jpx_container_source result;
  if ((state == NULL) || (which < 0))
    return result; // Return an empty interface
  while (((state->first_unparsed_container != NULL) &&
          (state->first_unparsed_container->get_id() <= which)) ||
         ((state->num_containers <= which) && !state->container_info_complete))
    { 
      if ((state->first_unparsed_container != NULL) &&
          (jx_container_source::parse_info(state->first_unparsed_container) !=
           NULL))
        break;
      if (!state->parse_next_top_level_box())
        break;
    }
  if ((which >= state->num_containers) ||
      ((state->first_unparsed_container != NULL) &&
       (which >= state->first_unparsed_container->get_id())))
    return result; // Return an empty interface
  jx_container_source *scan;
  for (scan=state->containers; which > 0; scan=scan->get_next(), which--)
    assert(scan != NULL);
  result = jpx_container_source(scan);
  return result;
}

/*****************************************************************************/
/*                jpx_source::find_unique_compatible_container               */
/*****************************************************************************/

jpx_container_source
  jpx_source::find_unique_compatible_container(int num_codestreams,
                                               const int codestream_indices[],
                                               int num_layers,
                                               const int layer_indices[])
{
  jpx_container_source result;
  if ((state == NULL) || (state->containers == NULL))
    return result; // Empty interface
  int n, idx, non_base_stream=-1, non_base_layer=-1;
  for (n=0; n < num_codestreams; n++)
    if ((idx=codestream_indices[n]) >= state->num_top_codestreams)
      { non_base_stream = idx; break; }
  for (n=0; n < num_layers; n++)
    if ((idx=layer_indices[n]) >= state->num_top_layers)
      { non_base_layer = idx; break; }
  if ((non_base_stream < 1) && (non_base_layer < 1))
    return result; // Empty interface -- stream/layer 0 must be top-level
  jx_container_source *container=NULL, *scan=state->containers;
  for (; scan != NULL; container=scan, scan=scan->get_next())
    if ((scan->get_first_base_codestream() > non_base_stream) &&
        (scan->get_first_base_layer() > non_base_layer))
      break; // `scan' lies beyond any compatible container
  if ((container != NULL) &&
      container->check_compatibility(num_codestreams,codestream_indices,
                                     num_layers,layer_indices,true))
    result = jpx_container_source(container);
  return result;
}

/*****************************************************************************/
/*                    jpx_source::get_num_layer_codestreams                  */
/*****************************************************************************/

int
  jpx_source::get_num_layer_codestreams(int which_layer)
{
  if ((state == NULL) || (!state->is_completely_open) || (which_layer < 0))
    return 0;
  if (state->restrict_to_jp2 && (which_layer != 0))
    return 0;
  int rep_idx=0;
  jx_layer_source *layer = state->get_compositing_layer(which_layer,rep_idx);
  if (layer == NULL)
    return 0;
  layer->finish(); // Try to finish it
  return layer->get_num_codestreams();
}

/*****************************************************************************/
/*                     jpx_source::get_layer_codestream_id                   */
/*****************************************************************************/

int
  jpx_source::get_layer_codestream_id(int which_layer, int which_stream)
{
  if ((state == NULL) || (!state->is_completely_open) ||
      (which_layer < 0) || (which_stream < 0))
    return -1;
  if (state->restrict_to_jp2 && (which_layer != 0))
    return -1;
  int rep_idx=0;
  jx_layer_source *layer = state->get_compositing_layer(which_layer,rep_idx);
  if (layer == NULL)
    return -1;
  layer->finish(); // Try to finish it
  return layer->get_codestream_id(which_stream,rep_idx);
}

/*****************************************************************************/
/*                       jpx_source::access_composition                      */
/*****************************************************************************/

jpx_composition
  jpx_source::access_composition()
{
  jpx_composition result;
  if ((state != NULL) && state->composition.finish())
    result = jpx_composition(&state->composition);
  return result;
}

/*****************************************************************************/
/*                        jpx_source::generate_metareq                       */
/*****************************************************************************/

int
  jpx_source::generate_metareq(kdu_window *client_window,
                               int min_frame_idx, int max_frame_idx,
                               int max_layer_idx, int max_codestream_idx,
                               bool priority)
{
  if ((state == NULL) || state->restrict_to_jp2)
    return 0;
  
  int num_metareqs=0;
  while (!state->container_info_complete)
    { 
      if ((state->first_unparsed_container != NULL) &&
          (jx_container_source::parse_info(state->first_unparsed_container) !=
           NULL))
        break; // Unable to parse all available containers' info sub-boxes yet
      if (!state->parse_next_top_level_box())
        break;
    }
  
  if ((max_frame_idx != 0) &&
      state->composition.need_more_instructions(min_frame_idx,max_frame_idx))
    { // Generate request for compositing instructions
      client_window->add_metareq(jp2_comp_instruction_set_4cc,
                                 KDU_MRQ_ALL,priority,INT_MAX,false,0,1);
      num_metareqs++;
    }
  
  if ((state->containers != NULL) && !state->container_info_complete)
    { // We have found at least one container, so we know about all
      // top-level compositing layers
      if ((max_layer_idx < 0) || (max_layer_idx >= state->total_layers) ||
          (max_codestream_idx < 0) ||
          (max_codestream_idx >= state->total_codestreams) ||
          (max_frame_idx != 0))
        { // Need all Compositing Layer Extensions Info boxes
          client_window->add_metareq(jp2_layer_extensions_info_4cc,
                                     KDU_MRQ_ALL,priority,INT_MAX,false,0,1);
          num_metareqs++;
        }
    }
  else if ((state->last_container != NULL) &&
           state->last_container->indefinitely_repeated())
    { 
      if ((!state->find_all_streams()) &&
          ((max_layer_idx < 0) || (max_layer_idx >= state->total_layers) ||
           (max_codestream_idx < 0) ||
           (max_codestream_idx >= state->total_codestreams)))
        { // Need to know about any Multiple Codestream Info boxes
          client_window->add_metareq(jp2_multi_codestream_info_4cc,
                                     KDU_MRQ_ALL,priority,INT_MAX,false,0,1);
          num_metareqs++;
        }
    }
  
  return num_metareqs;
}

/*****************************************************************************/
/*                      jpx_source::access_meta_manager                      */
/*****************************************************************************/

jpx_meta_manager
  jpx_source::access_meta_manager()
{
  jpx_meta_manager result;
  if (state != NULL)
    result = jpx_meta_manager(&state->meta_manager);
  return result;
}


/* ========================================================================= */
/*                           jx_codestream_target                            */
/* ========================================================================= */

/*****************************************************************************/
/*                  jpx_codestream_target::get_codestream_id                 */
/*****************************************************************************/

int
  jpx_codestream_target::get_codestream_id() const
{
  assert(state != NULL);
  return state->id;
}

/*****************************************************************************/
/*                      jx_codestream_target::finalize                       */
/*****************************************************************************/

void
  jx_codestream_target::finalize()
{
  if (finalized)
    return;
  dimensions.finalize();
  palette.finalize();
  component_map.finalize(&dimensions,&palette);
  finalized = true;
}

/*****************************************************************************/
/*                     jx_codestream_target::write_chdr                      */
/*****************************************************************************/

jp2_output_box *
  jx_codestream_target::write_chdr(jp2_output_box *super_box,
                                   int *i_param, void **addr_param,
                                   int simulation_phase)
{
  assert(finalized);
  if ((last_simulation_phase != 0) &&
      (last_simulation_phase != simulation_phase))
    { // Need to reset state
      assert(chdr_written);
      assert(!chdr.exists()); // Should have been closed before simulation end
      chdr_written = false;
    }
  assert(!chdr_written);
  this->last_simulation_phase = simulation_phase;
  if (chdr.exists())
    { // We must have been processing a breakpoint.
      assert(have_breakpoint);
      chdr.close();
      chdr_written = true;
      return NULL;
    }
  
  if (super_box != NULL)
    chdr.open(super_box,jp2_codestream_header_4cc);
  else
    owner->open_top_box(&chdr,jp2_codestream_header_4cc,simulation_phase);
  if (!owner->get_default_dimensions()->compare(&dimensions))
    dimensions.save_boxes(&chdr);
  j2_palette *def_palette = owner->get_default_palette();
  j2_component_map *def_cmap = owner->get_default_component_map();
  if (def_palette->is_initialized())
    { // Otherwise, there will be no default component map either
      if (!def_palette->compare(&palette))
        palette.save_box(&chdr);
      if (!def_cmap->compare(&component_map))
        component_map.save_box(&chdr,true); // Force generation of a cmap box
    }
  else
    {
      palette.save_box(&chdr);
      component_map.save_box(&chdr);
    }
  if (have_breakpoint)
    {
      if (i_param != NULL)
        *i_param = this->i_param;
      if (addr_param != NULL)
        *addr_param = this->addr_param;
      return &chdr;
    }
  chdr.close();
  chdr_written = true;
  return NULL;
}

/*****************************************************************************/
/*                    jx_codestream_target::copy_defaults                    */
/*****************************************************************************/

void
  jx_codestream_target::copy_defaults(j2_dimensions &default_dims,
                                      j2_palette &default_plt,
                                      j2_component_map &default_map)
{
  default_dims.copy(&dimensions);
  default_plt.copy(&palette);
  default_map.copy(&component_map);
}

/*****************************************************************************/
/*               jx_codestream_target::adjust_compatibility                  */
/*****************************************************************************/

void
  jx_codestream_target::adjust_compatibility(jx_compatibility *compatibility)
{
  assert(finalized);
  int profile, compression_type, part2_caps;
  compression_type = dimensions.get_compression_type(profile);
  part2_caps = dimensions.get_part2_caps();
  if (compression_type == JP2_COMPRESSION_TYPE_JPEG2000)
    {
      if (profile < 0)
        profile = Sprofile_PROFILE2; // Reasonable guess, if never initialized
      if (profile == Sprofile_PROFILE0)
        compatibility->add_standard_feature(JPX_SF_JPEG2000_PART1_PROFILE0);
      else if (profile == Sprofile_PROFILE1)
        compatibility->add_standard_feature(JPX_SF_JPEG2000_PART1_PROFILE1);
      else if ((profile != Sprofile_PART2) && (part2_caps == 0))
        compatibility->add_standard_feature(JPX_SF_JPEG2000_PART1);
      else
        {
          compatibility->add_standard_feature(JPX_SF_JPEG2000_PART2);
          compatibility->have_non_part1_codestream();
          if (id == 0)
            {
              compatibility->set_not_jp2_compatible();
              if (!dimensions.get_jpxb_compatible())
                compatibility->set_not_jpxb_compatible();
            }
        }
      if (part2_caps & SCpart2_caps_EXTENDED_COD)
        compatibility->add_standard_feature(JPX_SF_BLOCK_CODER_EXTENSIONS);
    }
  else
    {
      if (id == 0)
        {
          compatibility->set_not_jp2_compatible();
          compatibility->set_not_jpxb_compatible();
        }
      compatibility->have_non_part1_codestream();
      if (compression_type == JP2_COMPRESSION_TYPE_JPEG)
        compatibility->add_standard_feature(JPX_SF_CODESTREAM_USING_DCT);
    }
}


/* ========================================================================= */
/*                          jpx_codestream_target                            */
/* ========================================================================= */

/*****************************************************************************/
/*                  jpx_codestream_target::access_dimensions                 */
/*****************************************************************************/

jp2_dimensions
  jpx_codestream_target::access_dimensions()
{
  assert(state != NULL);
  return jp2_dimensions(&state->dimensions);
}

/*****************************************************************************/
/*                   jpx_codestream_target::access_palette                   */
/*****************************************************************************/

jp2_palette
  jpx_codestream_target::access_palette()
{
  assert(state != NULL);
  return jp2_palette(&state->palette);
}

/*****************************************************************************/
/*                   jpx_codestream_target::copy_attributes                  */
/*****************************************************************************/

void
  jpx_codestream_target::copy_attributes(jpx_codestream_source src)
{
  access_dimensions().copy(src.access_dimensions(true));
  access_palette().copy(src.access_palette());
}

/*****************************************************************************/
/*                    jpx_codestream_target::set_breakpoint                  */
/*****************************************************************************/

void
  jpx_codestream_target::set_breakpoint(int i_param, void *addr_param)
{
  assert(state != NULL);
  state->have_breakpoint = true;
  state->i_param = i_param;
  state->addr_param = addr_param;
}

/*****************************************************************************/
/*                 jpx_codestream_target::access_fragment_list               */
/*****************************************************************************/

jpx_fragment_list
  jpx_codestream_target::access_fragment_list()
{
  return jpx_fragment_list(&(state->fragment_list));
}

/*****************************************************************************/
/*                     jpx_codestream_target::add_fragment                   */
/*****************************************************************************/

void
  jpx_codestream_target::add_fragment(const char *url_or_path, kdu_long offset,
                                      kdu_long length, bool is_path)
{
  int url_idx;
  if (is_path && (url_or_path != NULL))
    url_idx = state->owner->get_data_references().add_file_url(url_or_path);
  else
    url_idx = state->owner->get_data_references().add_url(url_or_path);
  access_fragment_list().add_fragment(url_idx,offset,length);
}

/*****************************************************************************/
/*                 jpx_codestream_target::write_fragment_table               */
/*****************************************************************************/

void
  jpx_codestream_target::write_fragment_table()
{
  if (!state->owner->can_write_codestreams())
    { KDU_ERROR_DEV(e,53); e <<
        KDU_TXT("You may not call "
        "`jpx_codestream_target::open_stream' or "
        "`jpx_codestream_target::write_fragment_table' until after the JPX "
        "file header has been written using `jpx_target::write_headers'.");
    }
  if (state->jp2c.exists())
    { KDU_ERROR_DEV(e,0x03071207); e <<
      KDU_TXT("You may not call `write_fragment_table' on a "
              "`jpx_codestream_target' object before closing any existing "
              "open box obtained via a previous call to `open_stream'.");
    }
  if (((state->container == NULL) && (state->codestream_count > 0)) ||
      ((state->container != NULL) &&
       !state->container->start_codestream(state->codestream_count)))
    { KDU_ERROR_DEV(e,0x03071204); e <<
        KDU_TXT("Too many calls to "
                "`jpx_codestream_target::write_fragment_table' for a given "
                "codestream header instance.");
    }
  if (state->fragment_list.is_empty())
    { KDU_ERROR_DEV(e,55); e <<
        KDU_TXT("You must not call "
                "`jpx_codestream_target::write_fragment_table' without first "
                "adding one or more references to codestream fragments.");
    }
  state->fragment_list.finalize(state->owner->get_data_references());
  state->owner->write_stream_ftbl(state->fragment_list);
  state->fragment_list.reset();
  state->codestream_count++;
}

/*****************************************************************************/
/*                     jpx_codestream_target::open_stream                    */
/*****************************************************************************/

jp2_output_box *
  jpx_codestream_target::open_stream()
{
  assert(state != NULL);
  if (!state->owner->can_write_codestreams())
    { KDU_ERROR_DEV(e,56); e <<
        KDU_TXT("You may not call "
        "`jpx_codestream_target::open_stream' or "
        "`jpx_codestream_target::write_fragment_table' until after the JPX "
        "file header has been written using `jpx_target::write_headers'.");
    }
  if (!state->fragment_list.is_empty())
    { KDU_ERROR_DEV(e,57); e <<
        KDU_TXT("You may not call `open_stream' on a `jpx_codestream_target' "
                "object to which one or more codestream fragment references "
                "have already been added.");
    }
  if (state->jp2c.exists())
    { KDU_ERROR_DEV(e,0x03071206); e <<
      KDU_TXT("You may not call `open_stream' on a `jpx_codestream_target' "
              "object before closing any existing open box obtained via "
              "a previous call to `open_stream'.");
    }
  if (((state->container == NULL) && (state->codestream_count > 0)) ||
      ((state->container != NULL) &&
       !state->container->start_codestream(state->codestream_count)))
    { KDU_ERROR_DEV(e,0x03071205); e <<
      KDU_TXT("Too many calls to "
              "`jpx_codestream_target::open_stream' for a given "
              "codestream header instance.");
    }
  state->owner->open_stream(&state->jp2c);
  state->codestream_count++;
  return &state->jp2c;
}

/*****************************************************************************/
/*                    jpx_codestream_target::access_stream                   */
/*****************************************************************************/

kdu_compressed_target *
  jpx_codestream_target::access_stream()
{
  assert(state != NULL);
  return &state->jp2c;
}


/* ========================================================================= */
/*                             jx_layer_target                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        jx_layer_target::finalize                          */
/*****************************************************************************/

bool
  jx_layer_target::finalize()
{
  if (finalized)
    return need_creg;
  resolution.finalize();
  j2_colour *cscan;
  int num_colours = 0;
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    if (num_colours == 0)
      num_colours = cscan->get_num_colours();
    else if ((num_colours != cscan->get_num_colours()) &&
             (cscan->get_num_colours() != 0))
      { KDU_ERROR_DEV(e,59); e <<
          KDU_TXT("The `jpx_layer_target::add_colour' function has "
          "been used to add multiple colour descriptions for a JPX "
          "compositing layer, but not all colour descriptions have the "
          "same number of colour channels.");
      }
  channels.finalize(num_colours,true);

  registration.finalize(&channels,this->id);
  need_creg = false; // Until proven otherwise
  int cs_id, cs_which;
  for (cs_which=0; cs_which < registration.num_codestreams; cs_which++)
    {
      cs_id = registration.codestreams[cs_which].codestream_id;
      if (cs_id != this->id)
        need_creg = true;
      jx_codestream_target *cs = owner->get_codestream(cs_id);
      if ((cs == NULL) && (container != NULL))
        cs = container->get_base_codestream(cs_id);
      if (cs == NULL)
        { KDU_ERROR_DEV(e,60); e <<
            KDU_TXT("Application has configured a JPX compositing "
            "layer box which utilizes a non-existent codestream!");
        }
      channels.add_cmap_channels(cs->get_component_map(),cs_id);
      jpx_codestream_target cs_ifc(cs);
      kdu_coords size = cs_ifc.access_dimensions().get_size();
      if ((registration.codestreams[cs_which].sampling !=
           registration.denominator) ||
          (registration.codestreams[cs_which].alignment.x != 0) ||
          (registration.codestreams[cs_which].alignment.y != 0))
        need_creg = true;
      size.x *= registration.codestreams[cs_which].sampling.x;
      size.y *= registration.codestreams[cs_which].sampling.y;
      size += registration.codestreams[cs_which].alignment;
      if ((cs_which == 0) || (size.x < registration.final_layer_size.x))
        registration.final_layer_size.x = size.x;
      if ((cs_which == 0) || (size.y < registration.final_layer_size.y))
        registration.final_layer_size.y = size.y;
    }
  registration.final_layer_size.x =
    ceil_ratio(registration.final_layer_size.x,registration.denominator.x);
  registration.final_layer_size.y =
    ceil_ratio(registration.final_layer_size.y,registration.denominator.y);
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    cscan->finalize(&channels);
  finalized = true;
  return need_creg;
}

/*****************************************************************************/
/*                        jx_layer_target::write_jplh                        */
/*****************************************************************************/

jp2_output_box *
  jx_layer_target::write_jplh(jp2_output_box *super_box,
                              bool write_creg_box, int *i_param,
                              void **addr_param, int simulation_phase)
{
  assert(finalized);
  if ((last_simulation_phase != 0) &&
      (last_simulation_phase != simulation_phase))
    { // Transitioning from simulation phase; need to reset state
      assert(jplh_written);
      assert(!jplh.exists()); // Should have been closed before simulation end
      jplh_written = false;
    }
  assert(!jplh_written);
  last_simulation_phase = simulation_phase;
  if (jplh.exists())
    { // We must have been processing a breakpoint.
      assert(have_breakpoint);
      jplh.close();
      jplh_written = true;
      return NULL;
    }
  
  if (super_box != NULL)
    { 
      assert(write_creg_box);
      jplh.open(super_box,jp2_compositing_layer_hdr_4cc);
    }
  else
    owner->open_top_box(&jplh,jp2_compositing_layer_hdr_4cc,simulation_phase);
  j2_colour *cscan, *dscan, *default_colour=owner->get_default_colour();
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    {
      for (dscan=default_colour; dscan != NULL; dscan=dscan->next)
        if (cscan->compare(dscan))
          break;
      if (dscan == NULL)
        break; // Could not find a match
    }
  if (cscan != NULL)
    { // Default colour spaces not identical to current ones
      jp2_output_box cgrp;
      cgrp.open(&jplh,jp2_colour_group_4cc);
      for (cscan=&colour; cscan != NULL; cscan=cscan->next)
        cscan->save_box(&cgrp);
      cgrp.close();
    }
  if (!owner->get_default_channels()->compare(&channels))
    channels.save_boxes(&jplh,(id==0));
  if (write_creg_box)
    { 
      registration.save_box(&jplh);
      creg_written = true;
    }
  if (!owner->get_default_resolution()->compare(&resolution))
    resolution.save_box(&jplh);
  if (have_breakpoint)
    {
      if (i_param != NULL)
        *i_param = this->i_param;
      if (addr_param != NULL)
        *addr_param = this->addr_param;
      return &jplh;
    }
  jplh.close();
  jplh_written = true;
  return NULL;
}

/*****************************************************************************/
/*                       jx_layer_target::copy_defaults                      */
/*****************************************************************************/

void
  jx_layer_target::copy_defaults(j2_resolution &default_res,
                                 j2_channels &default_channels,
                                 j2_colour &default_colour)
{
  default_res.copy(&resolution);
  if (!channels.needs_opacity_box())
    default_channels.copy(&channels); // Can't write opacity box in jp2 header
  j2_colour *src, *dst;
  for (src=&colour, dst=&default_colour; src != NULL; src=src->next)
    {
      dst->copy(src);
      if (src->next != NULL)
        {
          assert(dst->next == NULL);
          dst = dst->next = new j2_colour;
        }
    }
}

/*****************************************************************************/
/*                  jx_layer_target::adjust_compatibility                    */
/*****************************************************************************/

void
  jx_layer_target::adjust_compatibility(jx_compatibility *compatibility)
{
  assert(finalized);
  if (id > 0)
    compatibility->add_standard_feature(JPX_SF_MULTIPLE_LAYERS);

  if (channels.uses_palette_colour())
    compatibility->add_standard_feature(JPX_SF_PALETTIZED_COLOUR);
  if (channels.has_opacity())
    compatibility->add_standard_feature(JPX_SF_OPACITY_NOT_PREMULTIPLIED);
  if (channels.has_premultiplied_opacity())
    compatibility->add_standard_feature(JPX_SF_OPACITY_PREMULTIPLIED);
  if (channels.needs_opacity_box())
    {
      compatibility->add_standard_feature(JPX_SF_OPACITY_BY_CHROMA_KEY);
      if (id == 0)
        compatibility->set_not_jp2_compatible();
    }
  if (channels.needs_pixel_format_fixpoint())
    { 
      compatibility->add_standard_feature(JPX_SF_PIXEL_FORMAT_FIXPOINT);
      if (id == 0)
        compatibility->set_not_jp2_compatible();
    }
  if (channels.needs_pixel_format_float())
    { 
      compatibility->add_standard_feature(JPX_SF_PIXEL_FORMAT_FLOAT);
      if (id == 0)
        compatibility->set_not_jp2_compatible();
    }
  if (channels.needs_pixel_format_split_exp())
    { 
      compatibility->add_standard_feature(JPX_SF_PIXEL_FORMAT_SPLIT_EXP);
      if (id == 0)
        compatibility->set_not_jp2_compatible();
    }

  if (registration.num_codestreams > 1)
    {
      compatibility->add_standard_feature(
                                  JPX_SF_MULTIPLE_CODESTREAMS_PER_LAYER);
      if (id == 0)
        {
          compatibility->set_not_jp2_compatible();
          compatibility->set_not_jpxb_compatible();
        }
      for (int n=1; n < registration.num_codestreams; n++)
        if (registration.codestreams[n].sampling !=
            registration.codestreams[0].sampling)
          {
            compatibility->add_standard_feature(JPX_SF_SCALING_WITHIN_LAYER);
            break;
          }
    }

  jp2_resolution res(&resolution);
  float aspect = res.get_aspect_ratio();
  if ((aspect < 0.99F) || (aspect > 1.01F))
    compatibility->add_standard_feature(JPX_SF_SAMPLES_NOT_SQUARE);

  bool have_jp2_compatible_space = false;
  bool have_non_vendor_space = false;
  int best_precedence = -128;
  j2_colour *cbest=NULL, *cscan;
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    {
      jp2_colour clr(cscan);
      if (clr.get_precedence() > best_precedence)
        {
          best_precedence = clr.get_precedence();
          cbest = cscan;
        }
    }
  for (cscan=&colour; cscan != NULL; cscan=cscan->next)
    { // Add feature flags to represent each colour description.
      if (cscan->is_jp2_compatible())
        have_jp2_compatible_space = true;
      jp2_colour clr(cscan);
      jp2_colour_space space = clr.get_space();
      bool add_to_both = (cscan==cbest);
      if (space == JP2_bilevel1_SPACE)
        compatibility->add_standard_feature(JPX_SF_BILEVEL1,add_to_both);
      else if (space == JP2_YCbCr1_SPACE)
        compatibility->add_standard_feature(JPX_SF_YCbCr1,add_to_both);
      else if (space == JP2_YCbCr2_SPACE)
        compatibility->add_standard_feature(JPX_SF_YCbCr2,add_to_both);
      else if (space == JP2_YCbCr3_SPACE)
        compatibility->add_standard_feature(JPX_SF_YCbCr3,add_to_both);
      else if (space == JP2_PhotoYCC_SPACE)
        compatibility->add_standard_feature(JPX_SF_PhotoYCC,add_to_both);
      else if (space == JP2_CMY_SPACE)
        compatibility->add_standard_feature(JPX_SF_CMY,add_to_both);
      else if (space == JP2_CMYK_SPACE)
        compatibility->add_standard_feature(JPX_SF_CMYK,add_to_both);
      else if (space == JP2_YCCK_SPACE)
        compatibility->add_standard_feature(JPX_SF_YCCK,add_to_both);
      else if (space == JP2_CIELab_SPACE)
        {
          if (clr.check_cie_default())
            compatibility->add_standard_feature(JPX_SF_LAB_DEFAULT,
                                                add_to_both);
          else
            compatibility->add_standard_feature(JPX_SF_LAB,add_to_both);
        }
      else if (space == JP2_bilevel2_SPACE)
        compatibility->add_standard_feature(JPX_SF_BILEVEL2,add_to_both);
      else if (space == JP2_sRGB_SPACE)
        compatibility->add_standard_feature(JPX_SF_sRGB,add_to_both);
      else if (space == JP2_sLUM_SPACE)
        compatibility->add_standard_feature(JPX_SF_sLUM,add_to_both);
      else if (space == JP2_sYCC_SPACE)
        compatibility->add_standard_feature(JPX_SF_sYCC,add_to_both);
      else if (space == JP2_CIEJab_SPACE)
        {
          if (clr.check_cie_default())
            compatibility->add_standard_feature(JPX_SF_JAB_DEFAULT,
                                                add_to_both);
          else
            compatibility->add_standard_feature(JPX_SF_JAB,add_to_both);
        }
      else if (space == JP2_esRGB_SPACE)
        compatibility->add_standard_feature(JPX_SF_esRGB,add_to_both);
      else if (space == JP2_ROMMRGB_SPACE)
        compatibility->add_standard_feature(JPX_SF_ROMMRGB,add_to_both);
      else if (space == JP2_YPbPr60_SPACE)
        {} // Need a compatibility code for this -- none defined originally
      else if (space == JP2_YPbPr50_SPACE)
        {} // Need a compatibility code for this -- none defined originally
      else if (space == JP2_esYCC_SPACE)
        {} // Need a compatibility code for this -- none defined originally
      else if (space == JP2_iccLUM_SPACE)
        compatibility->add_standard_feature(JPX_SF_RESTRICTED_ICC,add_to_both);
      else if (space == JP2_iccRGB_SPACE)
        compatibility->add_standard_feature(JPX_SF_RESTRICTED_ICC,add_to_both);
      else if (space == JP2_iccANY_SPACE)
        compatibility->add_standard_feature(JPX_SF_ANY_ICC,add_to_both);
      else if (space == JP2_vendor_SPACE)
        {} // Need a compatibility code for this -- none defined originally
      if (space != JP2_vendor_SPACE)
        have_non_vendor_space = true;
    }
  if ((id == 0) && !have_jp2_compatible_space)
    compatibility->set_not_jp2_compatible();
  if ((id == 0) && !have_non_vendor_space)
    compatibility->set_not_jpxb_compatible();
}

/*****************************************************************************/
/*                     jx_layer_target::uses_codestream                      */
/*****************************************************************************/

bool
  jx_layer_target::uses_codestream(int idx)
{
  for (int n=0; n < registration.num_codestreams; n++)
    if (registration.codestreams[n].codestream_id == idx)
      return true;
  return false;
}


/* ========================================================================= */
/*                            jpx_layer_target                               */
/* ========================================================================= */

/*****************************************************************************/
/*                     jpx_layer_target::get_layer_id                        */
/*****************************************************************************/

int
  jpx_layer_target::get_layer_id() const
{
  assert(state != NULL);
  return state->id;
}

/*****************************************************************************/
/*                    jpx_layer_target::access_channels                      */
/*****************************************************************************/

jp2_channels
  jpx_layer_target::access_channels()
{
  assert(state != NULL);
  return jp2_channels(&state->channels);
}

/*****************************************************************************/
/*                   jpx_layer_target::access_resolution                     */
/*****************************************************************************/

jp2_resolution
  jpx_layer_target::access_resolution()
{
  assert(state != NULL);
  return jp2_resolution(&state->resolution);
}

/*****************************************************************************/
/*                       jpx_layer_target::add_colour                        */
/*****************************************************************************/

jp2_colour
  jpx_layer_target::add_colour(int precedence, kdu_byte approx)
{
  assert(state != NULL);
  if ((precedence < -128) || (precedence > 127) || (approx > 4))
    { KDU_ERROR_DEV(e,61); e <<
        KDU_TXT("Invalid `precedence' or `approx' parameter supplied "
        "to `jpx_layer_target::add_colour'.  Legal values for the precedence "
        "parameter must lie in the range -128 to +127, while legal values "
        "for the approximation level parameter are 0, 1, 2, 3 and 4.");
    }
  if (state->last_colour == NULL)
    state->last_colour = &state->colour;
  else
    state->last_colour = state->last_colour->next = new j2_colour;
  state->last_colour->precedence = precedence;
  state->last_colour->approx = approx;
  return jp2_colour(state->last_colour);
}

/*****************************************************************************/
/*                     jpx_layer_target::access_colour                       */
/*****************************************************************************/

jp2_colour
  jpx_layer_target::access_colour(int which)
{
  assert(state != NULL);
  j2_colour *scan = NULL;
  if (which >= 0)
    for (scan=&state->colour; (scan != NULL) && (which > 0); which--)
      scan = scan->next;
  return jp2_colour(scan);
}

/*****************************************************************************/
/*               jpx_layer_target::set_codestream_registration               */
/*****************************************************************************/

void
  jpx_layer_target::set_codestream_registration(int codestream_id,
                                                kdu_coords alignment,
                                                kdu_coords sampling,
                                                kdu_coords denominator)
{
  int n;
  assert(state != NULL);
  jx_registration *reg = &state->registration;
  jx_registration::jx_layer_stream *str=NULL;
  if (reg->num_codestreams == 0)
    reg->denominator = denominator;
  else if (reg->denominator != denominator)
    { KDU_ERROR_DEV(e,62); e <<
        KDU_TXT("The denominator values supplied via all calls to "
        "`jpx_layer_target::set_codestream_registration' within the same "
        "compositing layer must be identical.  This is because the "
        "codestream registration (creg) box can record only one denominator "
        "(point density) to be shared by all the codestream sampling and "
        "alignment parameters.");
    }
  if ((denominator.x < 1) || (denominator.x >= (1<<16)) ||
      (denominator.y < 1) || (denominator.y >= (1<<16)) ||
      (alignment.x < 0) || (alignment.y < 0) ||
      (alignment.x > 255) || (alignment.y > 255) ||
      (alignment.x >= denominator.x) || (alignment.y >= denominator.y) ||
      (sampling.x < 1) || (sampling.y < 1) ||
      (sampling.x > 255) || (sampling.y > 255))
    { KDU_ERROR_DEV(e,63); e <<
        KDU_TXT("Illegal alignment or sampling parameters passed to "
        "`jpx_layer_target::set_codestream_registration'.  The alignment "
        "offset and sampling numerator values must be non-negative (non-zero "
        "for sampling factors) and no larger than 255; moreover, the "
        "alignment offsets must be strictly less than the denominator "
        "(point density) values and the common sampling denominator must "
        "lie be in the range 1 to 65535.");
    }

  for (n=0; n < reg->num_codestreams; n++)
    {
      str = reg->codestreams + n;
      if (str->codestream_id == codestream_id)
        break;
    }
  if (n == reg->num_codestreams)
    { // Add new element
      if (reg->max_codestreams == reg->num_codestreams)
        { // Augment array
          int new_max_cs = reg->max_codestreams + n + 2;
          str = new jx_registration::jx_layer_stream[new_max_cs];
          for (n=0; n < reg->num_codestreams; n++)
            str[n] = reg->codestreams[n];
          if (reg->codestreams != NULL)
            delete[] reg->codestreams;
          reg->codestreams = str;
          reg->max_codestreams = new_max_cs;
        }
      reg->num_codestreams++;
      str = reg->codestreams + n;
    }
  str->codestream_id = codestream_id;
  str->sampling = sampling;
  str->alignment = alignment;
}

/*****************************************************************************/
/*                      jpx_layer_target::copy_attributes                    */
/*****************************************************************************/

void
  jpx_layer_target::copy_attributes(jpx_layer_source src)
{
  jp2_colour colour_in, colour_out;
  int k=0;
  while ((colour_in = src.access_colour(k++)).exists())
    { 
      colour_out = add_colour(colour_in.get_precedence(),
                              colour_in.get_approximation_level());
      colour_out.copy(colour_in);
    }
  access_resolution().copy(src.access_resolution());
  access_channels().copy(src.access_channels());
  int idx, num_streams=src.get_num_codestreams();
  for (k=0; k < num_streams; k++)
    { 
      kdu_coords alignment, sampling, denominator;
      idx = src.get_codestream_registration(k,alignment,sampling,denominator);
      set_codestream_registration(idx,alignment,sampling,denominator);
    }
}

/*****************************************************************************/
/*                      jpx_layer_target::set_breakpoint                     */
/*****************************************************************************/

void
  jpx_layer_target::set_breakpoint(int i_param, void *addr_param)
{
  assert(state != NULL);
  state->have_breakpoint = true;
  state->i_param = i_param;
  state->addr_param = addr_param;
}


/* ========================================================================= */
/*                           jx_container_target                             */
/* ========================================================================= */

/*****************************************************************************/
/*                 jx_container_target::jx_container_target                  */
/*****************************************************************************/

jx_container_target::jx_container_target(jx_target *owner, int container_id,
                                         int top_layers, int top_codestreams,
                                         int repetitions, int n_base_layers,
                                         int n_base_codestreams,
                                         int first_layer, int first_codestream)
{
  this->owner = owner;
  this->id = container_id;
  this->num_top_layers = top_layers;
  this->num_top_codestreams = top_codestreams;
  this->known_reps = repetitions;
  this->indefinite_reps = (repetitions == 0);
  this->num_base_layers = n_base_layers;
  this->num_base_codestreams = n_base_codestreams;
  
  this->base_layers = NULL;  this->base_codestreams = NULL;
  if (n_base_layers > 0)
    { 
      this->base_layers = new jx_layer_target *[n_base_layers];
      memset(this->base_layers,0,
             sizeof(jx_layer_target *)*(size_t)n_base_layers);
    }
  if (n_base_codestreams > 0)
    { 
      this->base_codestreams = new jx_codestream_target *[n_base_codestreams];
      memset(this->base_codestreams,0,
             sizeof(jx_codestream_target *)*(size_t)n_base_codestreams);
    }
  this->first_layer_idx = first_layer;
  this->first_codestream_idx = first_codestream;
  num_tracks = 0; tracks = last_track = NULL;
  first_frame_idx = 0; num_frames_per_track = 0;
  track_duration = 0; first_frame_time = 0;
  finalized = false;
  write_in_progress = false;
  jclx_written = false;
  last_simulation_phase = 0;
  num_chdr_written = num_jplh_written = 0;
  first_unwritten_track = NULL;
  next_generated_rep = 0;
  rep_generated_streams = 0;
  next = prev = NULL;
  
  int n;
  for (n=0; n < n_base_layers; n++)
    base_layers[n] = new jx_layer_target(owner,first_layer+n,this);
  for (n=0; n < n_base_codestreams; n++)
    base_codestreams[n] =
      new jx_codestream_target(owner,first_codestream+n,this);
}

/*****************************************************************************/
/*                jx_container_target::~jx_container_target                  */
/*****************************************************************************/

jx_container_target::~jx_container_target()
{
  assert((first_metanode == NULL) && (last_metanode == NULL));
  while ((last_track = tracks) != NULL)
    { 
      tracks = last_track->next;
      delete last_track;
    }
  int n;
  if (base_layers != NULL)
    { 
      for (n=0; n < num_base_layers; n++)
        { 
          if (base_layers[n] != NULL)
            delete base_layers[n];
        }
      delete[] base_layers;
    }
  if (base_codestreams != NULL)
    { 
      for (n=0; n < num_base_codestreams; n++)
        { 
          if (base_codestreams[n] != NULL)
            delete base_codestreams[n];
        }
      delete[] base_codestreams;
    }  
}

/*****************************************************************************/
/*                      jx_container_target::finalize                        */
/*****************************************************************************/

void
  jx_container_target::finalize(int &cumulative_frame_count,
                                kdu_long &cumulative_duration)
{
  if (!finalized)
    { 
      int n;
      this->first_frame_idx = cumulative_frame_count;
      this->first_frame_time = cumulative_duration;
      for (n=0; n < num_base_codestreams; n++)
        base_codestreams[n]->finalize();
      for (n=0; n < num_base_layers; n++)
        base_layers[n]->finalize();
      jx_composition *track_predecessor = NULL;
      if (this->tracks != NULL)
        { 
          jx_container_target *scan;
          track_predecessor = owner->get_composition();
          for (scan=this->get_prev(); scan != NULL; scan=scan->get_prev())
            if (scan->tracks != NULL)
              { 
                track_predecessor = &(scan->tracks->composition);
                break;
              }
        }
      jx_track_target *trk, *prev_trk=NULL;
      for (trk=tracks; trk != NULL; prev_trk=trk, trk=trk->next)
        { 
          if (trk->composition.is_empty())
            { KDU_ERROR_DEV(e,0x08071201); e <<
              KDU_TXT("Presentation track added to JPX container has no "
                      "compositing instructions defined!  Tracks are "
                      "delineated within their containers by one or more "
                      "compositing instructions.");
            }
          if (track_predecessor != NULL)
            { 
              trk->composition.prev_in_track = track_predecessor;
              track_predecessor->next_in_track = &(trk->composition);
              track_predecessor = track_predecessor->track_next;
            }
          else
            { 
              assert(prev_trk != NULL);
              trk->composition.prev_in_track =
              prev_trk->composition.prev_in_track;
            }
          trk->composition.finalize();
          int trk_frames = trk->composition.get_total_frames();
          kdu_long trk_duration = trk->composition.get_duration();
          if (trk == tracks)
            { 
              num_frames_per_track = trk_frames;
              track_duration = trk_duration;
            }
          else if (num_frames_per_track != trk_frames)
            { KDU_ERROR_DEV(e,0x02071205); e <<
              KDU_TXT("All presentation tracks added to a JPX container "
                      "must have exactly the same total number of frames.");
            }
          else if (trk_duration > track_duration)
            track_duration = trk_duration;
        }
      while (track_predecessor != NULL)
        { 
          track_predecessor->next_in_track = &(last_track->composition);
          track_predecessor = track_predecessor->track_next;
        }
      finalized = true;
    }
  if (tracks != NULL)
    { 
      cumulative_frame_count = first_frame_idx + num_frames_per_track;
      cumulative_duration = first_frame_time + track_duration;
    }
}

/*****************************************************************************/
/*                     jx_container_target::write_jclx                       */
/*****************************************************************************/

jp2_output_box *
  jx_container_target::write_jclx(int *i_param, void **addr_param,
                                  int simulation_phase, kdu_long *file_pos,
                                  jp2_output_box **access_jclx)
{
  assert(finalized);
  this->written = true; // Marks the base object as written so that no more
    // number list metanodes may be embedded within it from now on.
  jp2_output_box *interrupted = NULL;
  if (access_jclx != NULL)
    { 
      assert(file_pos != NULL);
      *access_jclx = &jclx;
    }
  if ((last_simulation_phase != 0) &&
      (last_simulation_phase != simulation_phase))
    { 
      assert(!write_in_progress);
      assert(!jclx.exists()); // JCLX box must have been closed after simulate
      this->jclx_written = false; // Go back to unwritten state
    }
  if (jclx_written)
    { // Being called from `jx_metanode::write' to get JCLX box
      assert(!write_in_progress);
      return NULL;
    }
  last_simulation_phase = simulation_phase;
  
  if (!write_in_progress)
    { // Preliminaries
      if (access_jclx != NULL)
        { 
          interrupted =
            owner->write_or_simulate_earlier_containers(this,i_param,
                                                        addr_param,
                                                        simulation_phase);
          if (interrupted != NULL)
            return interrupted;
        }
      write_in_progress = true;

      kdu_uint32 life_start = (kdu_uint32) first_frame_time;
      if (first_frame_time != (kdu_long) life_start)
        { KDU_ERROR(e,0x01081202); e <<
          KDU_TXT("Cumulative frame times for all compositing instructions "
                  "that appear prior to a JPX container may not equal or "
                  "exceed 2^32 milliseconds, since the LIFE-START field of "
                  "the Compositing Layer Extensions Info box must be a 32-bit "
                  "unsigned integer.");
        }
      kdu_long pos =
        owner->open_top_box(&jclx,jp2_layer_extensions_4cc,simulation_phase);
      if (file_pos != NULL)
        { 
          *file_pos = pos;
          jclx.use_long_header(); // So we can be sure of box lengths
        }
      jp2_output_box sub;
      sub.open(&jclx,jp2_layer_extensions_info_4cc);
      sub.write((kdu_uint32) known_reps);
      sub.write((kdu_uint32) num_base_codestreams);
      sub.write((kdu_uint32) num_base_layers);
      sub.write((kdu_uint32) num_tracks);
      sub.write((kdu_uint32) num_frames_per_track);
      if (num_tracks > 0)
        sub.write((kdu_uint32) life_start);
      sub.close();
      this->num_chdr_written = 0;
      this->num_jplh_written = 0;
      this->first_unwritten_track = tracks;
    }

  while (num_chdr_written < num_base_codestreams)
    { 
      jx_codestream_target *cp = base_codestreams[num_chdr_written];
      interrupted = cp->write_chdr(&jclx,i_param,addr_param,
                                   simulation_phase);
      if (interrupted != NULL)
        return interrupted;
      num_chdr_written++;
    }
  while (num_jplh_written < num_base_layers)
    { 
      jx_layer_target *lp = base_layers[num_jplh_written];
      interrupted = lp->write_jplh(&jclx,true,i_param,addr_param,
                                   simulation_phase);
      if (interrupted != NULL)
        return interrupted;
      num_jplh_written++;
      if ((first_unwritten_track != NULL) &&
          (num_jplh_written == (first_unwritten_track->layer_offset + 
                                first_unwritten_track->num_track_layers)))
        { 
          first_unwritten_track->composition.save_instructions(&jclx);
          first_unwritten_track = first_unwritten_track->next;
        }
    }
  
  jclx_written = true;
  owner->note_jclx_written_or_simulated(this,simulation_phase);
  write_in_progress = false;
  if (file_pos != NULL)
    *file_pos += jclx.get_box_length();
  if (access_jclx == NULL)
    jclx.close();
  return NULL;
}

/*****************************************************************************/
/*                  jx_container_target::start_codestream                    */
/*****************************************************************************/

bool
  jx_container_target::start_codestream(int rep_idx)
{
  if ((next_generated_rep == known_reps) && !indefinite_reps)
    return false;
  if (rep_idx != next_generated_rep)
    { KDU_ERROR_DEV(e,0x03071209); e <<
      KDU_TXT("You appear to be generating contiguous codestreams or "
              "fragment tables out of order for codestreams that are "
              "described within a JPX container.");
    }
  if (indefinite_reps && (rep_generated_streams == 0))
    { // First codestream in new repetition detected; update global
      // number of compositing layers and codestreams accordingly.
      owner->add_new_container_layers(num_base_layers);
      owner->add_new_container_codestreams(num_base_codestreams);
    }
  rep_generated_streams++;
  if (rep_generated_streams == num_base_codestreams)
    { 
      rep_generated_streams = 0;
      next_generated_rep++;
    }
  return true;
}

/*****************************************************************************/
/*                jx_container_target::adjust_compatibility                  */
/*****************************************************************************/

void
  jx_container_target::adjust_compatibility(jx_compatibility *compatibility)
{
  int n;
  for (n=0; n < num_base_codestreams; n++)
    base_codestreams[n]->adjust_compatibility(compatibility);
  for (n=0; n < num_base_layers; n++)
    base_layers[n]->adjust_compatibility(compatibility);
  // Note: compatibility information for the presentation tracks is
  // evaluated directly from the top-level `jx_compatibility' object, by
  // following the links established during container finalization.
}


/* ========================================================================= */
/*                          jpx_container_target                            */
/* ========================================================================= */

/*****************************************************************************/
/*                 jpx_container_target::get_container_id                    */
/*****************************************************************************/

int jpx_container_target::get_container_id()
{
  return (state==NULL)?-1:(state->id);
}

/*****************************************************************************/
/*               jpx_container_target::get_num_top_codestreams               */
/*****************************************************************************/

int jpx_container_target::get_num_top_codestreams()
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  return state->num_top_codestreams;
}

/*****************************************************************************/
/*                 jpx_container_target::get_num_top_layers                  */
/*****************************************************************************/

int jpx_container_target::get_num_top_layers()
{
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  return state->num_top_layers;
}

/*****************************************************************************/
/*                jpx_container_target::get_base_codestreams                 */
/*****************************************************************************/

int jpx_container_target::get_base_codestreams(int &num_base_codestreams)
{ 
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  num_base_codestreams = state->num_base_codestreams;
  return state->first_codestream_idx;
}

/*****************************************************************************/
/*                   jpx_container_target::get_base_layers                   */
/*****************************************************************************/

int jpx_container_target::get_base_layers(int &num_base_layers)
{ 
  if ((state == NULL) || (state->num_base_layers == 0))
    return 0;
  num_base_layers = state->num_base_layers;
  return state->first_layer_idx;
}

/*****************************************************************************/
/*                    jpx_container_target::access_layer                     */
/*****************************************************************************/

jpx_layer_target
  jpx_container_target::access_layer(int which)
{
  jpx_layer_target result;
  if ((state != NULL) && (which >= 0) && (which < state->num_base_layers))
    result = jpx_layer_target(state->base_layers[which]);
  return result;
}

/*****************************************************************************/
/*                  jpx_container_target::access_codestream                  */
/*****************************************************************************/

jpx_codestream_target
  jpx_container_target::access_codestream(int which)
{
  jpx_codestream_target result;
  if ((state != NULL) && (which >= 0) && (which < state->num_base_codestreams))
    result = jpx_codestream_target(state->base_codestreams[which]);
  return result;
}

/*****************************************************************************/
/*               jpx_container_target::add_presentation_track                */
/*****************************************************************************/

jpx_composition
  jpx_container_target::add_presentation_track(int track_layers)
{
  jpx_composition result;
  if (state != NULL)
    { 
      if (state->finalized)
        { KDU_ERROR_DEV(e,0x02071201); e <<
          KDU_TXT("Attempting to add presentation tracks to a JPX container "
                  "that has already been finalized -- has probably already "
                  "been written to the file!");
        }
      int layer_offset = 0;
      if (state->last_track != NULL)
        layer_offset = (state->last_track->layer_offset +
                        state->last_track->num_track_layers);
      if ((track_layers < 1) ||
          (track_layers > (state->num_base_layers-layer_offset)))
        { KDU_ERROR_DEV(e,0x02071202); e <<
          KDU_TXT("Invalid number of track layers passed to "
                  "`jpx_container_target::add_presentation_track'.  The "
                  "total number of layers for all presentation tracks in a "
                  "JPX container must not exceed the number of base "
                  "compositing layers defined for the container.");
        }
      jx_track_target *track = new jx_track_target;
      track->layer_offset = layer_offset;
      track->num_track_layers = track_layers;
      track->composition.set_layer_mapping(layer_offset+state->first_layer_idx,
                                           state->known_reps,track_layers,
                                           state->num_base_layers);
      state->num_tracks++;
      if (state->last_track == NULL)
        state->tracks = state->last_track = track;
      else
        { 
          state->last_track->composition.track_next = &(track->composition);
          state->last_track = state->last_track->next = track;
        }
      track->composition.track_idx = state->num_tracks;
      result = jpx_composition(&track->composition);
    }
  return result;
}


/* ========================================================================= */
/*                          jx_multistream_target                            */
/* ========================================================================= */

#define JX_multistream_MAX_SUBS 16 /* Max stream-containing sub-boxes / node */

/*****************************************************************************/
/*                       jx_multistream_target::init                         */
/*****************************************************************************/

void
  jx_multistream_target::init(int max_cs)
{
  assert(streams_per_subbox==0); // Otherwise prior call to `init' not matched
                                 // by call to `finish'.
  assert(multi_sub == NULL);
  streams_per_subbox = 0;
  bytes_per_subbox = 0;
  out_box_len = next_subbox_offset = 0;
  assert(expected_ftbl_len >= JX_MIN_FTBL_LEN);
  if (max_cs < 2)
    max_cs = 2;
  while (size_container(max_cs,streams_per_subbox,expected_ftbl_len) < 0)
    max_cs = (max_cs+1) >> 1;
  assert((max_cs > 1) && (streams_per_subbox >= 1));
  this->max_codestreams = max_cs;
  this->num_codestreams = 0;
}

/*****************************************************************************/
/*                      jx_multistream_target::finish                        */
/*****************************************************************************/

void
  jx_multistream_target::finish()
{
  if (streams_per_subbox == 0)
    return; // Already finished, or never initialized
  if (multi_sub != NULL)
    { 
      multi_sub->finish();
      delete multi_sub;
      multi_sub = NULL;
    }
  if (out_box.exists())
    { // Boxes all still open
      assert(num_codestreams > 0); // Must have written something at least
      assert(out_box_len <= 0); // `close_boxes' has not yet been called
      out_box.close();
      out_box_len = next_subbox_offset =
        out_box.get_box_length() - out_box.get_header_length();
    }
  else
    { // Any free box at the end required for padding should have been
      // written already, right after the last sub-box.
      next_subbox_offset = out_box_len;
    }
  write_info_box(true); // Overwrites the J2CI box that was initially written
  streams_per_subbox = 0; // Mark as finished
}

/*****************************************************************************/
/*                    jx_multistream_target::close_boxes                     */
/*****************************************************************************/

void
  jx_multistream_target::close_boxes()
{
  if (!out_box.exists())
    return;
  assert(num_codestreams > 0); // `write_stream_ftbl' called at least once
  assert(streams_per_subbox > 0); // Always true between `init' and `finish'
  int streams_left = max_codestreams - num_codestreams;
  if (multi_sub != NULL)
    { 
      multi_sub->close_boxes();
      streams_left -= (multi_sub->max_codestreams-multi_sub->num_codestreams);
    }
  assert(next_subbox_offset <= 0);
  next_subbox_offset = out_box.get_box_length() - out_box.get_header_length();
  assert(next_subbox_offset > 16); // Must have written J2CI + >= 1 FTBL box
  out_box_len = next_subbox_offset;
  kdu_long free_bytes = 0;
  if (streams_left > 0)
    { 
      int dummy_val;
      int extra_full_containers = streams_left / streams_per_subbox;
      if (extra_full_containers > 0)
        { 
          kdu_long extra_container_size =
            size_container(streams_per_subbox,dummy_val,expected_ftbl_len);
          if (extra_container_size > 0)
            free_bytes = extra_container_size * extra_full_containers;
          else
            extra_full_containers = 0; // Containers getting too big!
        }
      streams_left -= streams_per_subbox * extra_full_containers;
      if (streams_left > 0)
        { 
          kdu_long extra_container_size =
            size_container(streams_left,dummy_val,expected_ftbl_len);
          if (extra_container_size > 0)
            free_bytes += extra_container_size;
        }
      out_box_len += free_bytes;
    }
  if (free_bytes > 0)
    out_box.write_free_and_close(free_bytes);
  else
    out_box.close();
  assert(out_box.get_header_length() == 8);
}

/*****************************************************************************/
/*                     jx_multistream_target::fix_boxes                      */
/*****************************************************************************/

void
  jx_multistream_target::fix_boxes(kdu_long total_bytes,
                                   jp2_output_box *super_box)
{
  assert((num_codestreams == 0) && (max_codestreams > 0) &&
         (multi_sub == NULL) && (streams_per_subbox > 0) &&
         (out_box_len <= 0));
  out_box.open(super_box,jp2_multi_codestream_4cc);
  write_info_box(false);
  out_box_len = total_bytes - 8;
  next_subbox_offset = out_box.get_box_length() - out_box.get_header_length();
  assert(next_subbox_offset == 16); // Size of the J2CI info box
  kdu_long free_bytes = out_box_len - next_subbox_offset;
  assert(free_bytes >= expected_ftbl_len); // Must be able to write at least
                                            // one codestream's FTBL in here.
  out_box.write_free_and_close(free_bytes);
  out_box.close();
  assert(out_box.get_header_length() == 8);
}

/*****************************************************************************/
/*                 jx_multistream_target::write_stream_ftbl                  */
/*****************************************************************************/

bool
  jx_multistream_target::write_stream_ftbl(jx_fragment_list &frag_list,
                                           jx_target *tgt,
                                           jp2_output_box *super_box)
{
  // Update existing information about the length of fragment tables
  // that should be written at the leaves of the aggregation tree.  We must
  // do this first, even if `init' has not yet been called, since the
  // expected fragment table length affects the behaviour of calls to `init'.
  int ftbl_len = frag_list.calculate_box_length() + 8; // 8 for ftbl header
  if (ftbl_len > expected_ftbl_len)
    expected_ftbl_len = ftbl_len;
  ftbl_len = expected_ftbl_len; // Always use this length for writing FTBL's
  
  if (streams_per_subbox <= 0)
    return false; // This object needs a call to `init'
  if (num_codestreams >= max_codestreams)
    return false; // Need to finish this object and open another one

  bool no_more_subboxes = false;
  if (out_box_len > 0)
    { // Otherwise box lengths can grow as needed
      kdu_long free_gap = out_box_len - (next_subbox_offset + ftbl_len);
      if ((free_gap < 0) || ((free_gap != 0) && (free_gap < 8)))
        no_more_subboxes = true; // If any gap at the end, need to be able to
                                 // fill it up with a free box within `finish'.
    }
  if (multi_sub != NULL)
    { 
      if (multi_sub->write_stream_ftbl(frag_list,NULL,NULL))
        { 
          num_codestreams++;
          return true;
        }
      multi_sub->finish();
      kdu_long sub_len = multi_sub->out_box_len;
      assert(sub_len > 0);
      assert(multi_sub->out_box.get_header_length() == 8);
      sub_len += 8; // Accounts for the J2CX box header
      if (bytes_per_subbox == 0)
        bytes_per_subbox = sub_len;

      if (no_more_subboxes)
        return false;
      
      if ((sub_len != bytes_per_subbox) ||
          (multi_sub->num_codestreams != streams_per_subbox))
        bytes_per_subbox = -1; // J2CI cannot provide random access info
      
      delete multi_sub;
      multi_sub = NULL;
    }

  if (no_more_subboxes)
    return false;
  
  // We know we will be adding the codestream now
  int streams_left = max_codestreams - num_codestreams;
  num_codestreams++;
  if ((num_codestreams == 1) && (out_box_len <= 0))
    { // We have not written anything yet
      assert(!out_box.exists());
      assert((tgt != NULL) || (super_box != NULL));
      if (super_box != NULL)
        { 
          assert(super_box->exists());
          out_box.open(super_box,jp2_multi_codestream_4cc);
        }
      else
        tgt->open_top_box(&out_box,jp2_multi_codestream_4cc);
      write_info_box(false);
    }
  else if (out_box_len > 0)
    out_box.reopen(jp2_multi_codestream_4cc,next_subbox_offset);
  
  // See if we need to create a new `multi_sub' object
  int container_streams = streams_left;
  if (container_streams > streams_per_subbox)
    container_streams = streams_per_subbox;
  if ((container_streams > 1) && (out_box_len > 0))
    { // See if enough bytes remain to accommodate another J2CX container
      kdu_long bytes_left = out_box_len - next_subbox_offset;
      if (bytes_left < (8 + 16 + ftbl_len + 8))
        { // Cannot accommodate another J2CX box with free box for padding
          container_streams = 1;
        }
    }
  
  if ((container_streams < streams_per_subbox) ||
      (streams_left == container_streams))
    no_more_subboxes = true; // There will be no more sub-boxes after this one
  
  kdu_long sub_size = 0; // Total size of new sub-box, if known
  if (container_streams > 1)
    { // Create container
      int dummy_val;
      kdu_long multi_sub_size;
      while ((multi_sub_size =
              size_container(container_streams,dummy_val,ftbl_len)) < 0)
        { // multi_sub container has become much bigger than expected, due to
          // changes in `ftbl_len'.  Scale back the number of container
          // streams and also avoid adding further sub-boxes here.
          container_streams = (container_streams+1)>>1;
          no_more_subboxes = true;
        }
      assert(container_streams > 1);      
      multi_sub = new jx_multistream_target(ftbl_len);
      multi_sub->init(container_streams);
      if (out_box_len > 0)
        { 
          kdu_long bytes_left = out_box_len - next_subbox_offset;
          if (multi_sub_size > (bytes_left-ftbl_len))
            { // No space to accommodate even a single FTBL beyond this
              // sub-box, so make the sub-box cover the entire box.
              multi_sub_size = bytes_left;
              multi_sub->fix_boxes(bytes_left,&out_box);
              no_more_subboxes = true;
              out_box.close(); // Finished writing the entire span of `out_box'
                               // when we executed `multi_sub->fix_boxes'. From
                               // now on, writes to `multi_sub' will involve
                               // re-opening, so we cannot have the super-box
                               // remain in the re-opened state.
            }
        }
      if (!multi_sub->write_stream_ftbl(frag_list,NULL,&out_box))
        assert(0);
      if (out_box_len > 0)
        { // Box sizes can be fixed and known.
          multi_sub->close_boxes();
          sub_size = multi_sub->out_box.get_box_length();
          assert(sub_size==multi_sub_size); // Make sure size calculation works
        }
    }
  else
    { // Directly write fragment table sub-box
      jp2_output_box ftbl_box;
      ftbl_box.open(&out_box,jp2_fragment_table_4cc);
      frag_list.save_box(&ftbl_box,ftbl_len-8);
      ftbl_box.close();
      sub_size = ftbl_box.get_box_length();
      assert(sub_size == ftbl_len);
    }
  
  if (out_box_len > 0)
    { 
      assert(sub_size > 0); // Sub-box must have known size
      next_subbox_offset += sub_size;
      kdu_long free_bytes = out_box_len - next_subbox_offset;
      if (free_bytes < ftbl_len)
        no_more_subboxes = true;
      if (free_bytes != 0)
        { 
          assert(free_bytes >= 8);
          out_box.write_free_and_close(free_bytes);
        }
      else
        out_box.close();
      if (bytes_per_subbox == 0)
        bytes_per_subbox = sub_size;
      else if ((bytes_per_subbox != sub_size) && !no_more_subboxes)
        bytes_per_subbox = -1; // J2CI cannot provide random access      
      if (no_more_subboxes)
        next_subbox_offset = out_box_len; // Prevents addition of more streams
    }
  return true;
}

/*****************************************************************************/
/*                   jx_multistream_target::write_info_box                   */
/*****************************************************************************/

void jx_multistream_target::write_info_box(bool rewrite)
{
  kdu_uint32 Ncs, Ltbl;
  if (rewrite)
    { 
      j2ci_box.reopen(jp2_multi_codestream_info_4cc,0);
      Ncs = (kdu_uint32) num_codestreams;
      Ltbl = 0;
      if ((bytes_per_subbox > 0) && (bytes_per_subbox < (1<<26)))
        { 
          int Rtbl = 0;
          while ((streams_per_subbox >> Rtbl) > 1)
            Rtbl++;
          if (streams_per_subbox == (1<<Rtbl))
            Ltbl = (kdu_uint32)((Rtbl<<26) + bytes_per_subbox);
        }
    }
  else
    { // First time writing the J2CI box -- set all fields to 0, meaning
      // that the J2CX box is not finalized, but a smart reader can still
      // scan through the sub-boxes at any time to see what is available
      // so far.
      Ncs = Ltbl = 0;
      j2ci_box.open(&out_box,jp2_multi_codestream_info_4cc);
    }
  assert(j2ci_box.exists());
  j2ci_box.write(Ncs);
  j2ci_box.write(Ltbl);
  j2ci_box.close();
}

/*****************************************************************************/
/* STATIC            jx_multistream_target::size_container                   */
/*****************************************************************************/

int
  jx_multistream_target::size_container(int num_streams,
                                       int &num_sbb_streams, int ftbl_len)
{
  num_sbb_streams = 0;
  if (num_streams <= 0)
    return 0;
  if (num_streams == 1)
    return ftbl_len;
  num_sbb_streams = 1;
  while ((num_sbb_streams*JX_multistream_MAX_SUBS) < num_streams)
    num_sbb_streams *= JX_multistream_MAX_SUBS;
  int num_full_containers = num_streams / num_sbb_streams;
  int num_streams_left = num_streams - num_sbb_streams*num_full_containers;
  int dummy_val, container_len;
  container_len = size_container(num_sbb_streams,dummy_val,ftbl_len);
  if ((container_len < 0) ||
      (container_len >= ((1<<26) / num_full_containers)))
    return -1; // Box will exceed the maximum size of 2^26 bytes
  kdu_long total_len = 8; // Header for the J2CX container
  total_len += 16; // Total size of the J2CI sub-box
  total_len += container_len * num_full_containers;
  if (num_streams_left > 0)
    { 
      container_len = size_container(num_streams_left,dummy_val,ftbl_len);
      assert(container_len > 0);
      total_len += container_len;
      if (total_len >= (1<<26))
        total_len = -1;
    }
  return (int) total_len;
}


/* ========================================================================= */
/*                                jx_target                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                           jx_target::jx_target                            */
/*****************************************************************************/

jx_target::jx_target(jp2_family_tgt *tgt)
{
  this->ultimate_tgt = tgt;
  last_opened_top_box = NULL;
  last_simulation_phase = 0;
  need_creg_boxes = false;
  num_top_codestreams = 0;
  num_top_layers = 0;
  total_codestreams = 0;
  total_layers = 0;
  num_containers = 0;
  codestreams = NULL;
  last_codestream = NULL;
  layers = NULL;
  last_layer = NULL;
  containers = NULL;
  last_container = NULL;
  meta_manager.target = this;
  meta_manager.tree = new jx_metanode(&meta_manager);
  meta_manager.tree->flags |= (JX_METANODE_BOX_COMPLETE |
                               JX_METANODE_IS_COMPLETE |
                               JX_METANODE_DESCENDANTS_KNOWN);
  meta_manager.meta_target = new jx_meta_target(this);
  
  last_codestream_threshold = 0;
  file_has_containers = false;
  headers_in_progress = false;
  metadata_in_progress = false;
  main_header_complete = false;
  top_headers_complete = false;
  cumulative_frame_count = 0;
  cumulative_duration = 0;
  first_unfinalized_container = NULL;
  first_unwritten_container = NULL;
  first_unsimulated_container = NULL;
  
  min_j2cx_streams = 2;
  max_j2cx_streams = (1<<16);
  next_stream_idx = 0;
  last_opened_jp2c = NULL;
}

/*****************************************************************************/
/*                          jx_target::~jx_target                            */
/*****************************************************************************/

jx_target::~jx_target()
{
  // Start by unlinking all metadata nodes, so that they do not subsequently
  // reference objects that we delete here
  if (meta_manager.tree != NULL)
    meta_manager.tree->safe_delete();
  
  // Now we can delete the image entities
  jx_codestream_target *cp;
  jx_layer_target *lp;
  jx_container_target *tp;
  j2_colour *cscan;

  while ((cp=codestreams) != NULL)
    {
      codestreams = cp->next;
      delete cp;
    }
  while ((lp=layers) != NULL)
    {
      layers = lp->next;
      delete lp;
    }
  meta_manager.tear_down();
  while ((tp=containers) != NULL)
    { 
      containers = tp->get_next();
      delete tp;
    }
  while ((cscan=default_colour.next) != NULL)
    { default_colour.next = cscan->next; delete cscan; }
}

/*****************************************************************************/
/*                      jx_target::write_stream_ftbl                         */
/*****************************************************************************/

void
  jx_target::write_stream_ftbl(jx_fragment_list &frag_list)
{
  close_any_open_stream();
  assert(next_stream_idx < total_codestreams);
  if ((next_stream_idx < num_top_codestreams) || (max_j2cx_streams == 0))
    { // Write top-level fragment table -- easy
      open_top_box(&box,jp2_fragment_table_4cc);
      frag_list.save_box(&box);
      box.close();
    }
  else
    { // Write fragment table to Multiple Codestream box
      if (!multistream_root.write_stream_ftbl(frag_list,this,NULL))
        { 
          assert((min_j2cx_streams >= 2) &&
                 (max_j2cx_streams >= min_j2cx_streams));
          multistream_root.finish();
          int max_cs = multistream_root.max_codestreams;
          if (max_cs < min_j2cx_streams)
            max_cs = min_j2cx_streams;
          else if ((multistream_root.num_codestreams >= max_cs) &&
                   (max_cs <= (max_j2cx_streams-max_cs)))
            max_cs += max_cs;
          multistream_root.init(max_cs);
          if (!multistream_root.write_stream_ftbl(frag_list,this,NULL))
            assert(0); // First fragment table is always writable!
        }
    }
  next_stream_idx++;
}

/*****************************************************************************/
/*                         jx_target::open_stream                            */
/*****************************************************************************/

void
  jx_target::open_stream(jp2_output_box *stream_box)
{
  close_any_open_stream();
  assert(next_stream_idx < total_codestreams);
  if ((next_stream_idx < num_top_codestreams) || (max_j2cx_streams == 0))
    { 
      open_top_box(stream_box,jp2_codestream_4cc);
      last_opened_jp2c = stream_box;
    }
  else
    { 
      assert((min_j2cx_streams >= 2) &&
             (max_j2cx_streams >= min_j2cx_streams));
      if (!mdat_box)
        { 
          open_top_box(&mdat_box,jp2_mdat_4cc);
          mdat_box.use_long_header();
          mdat_box.write_header_last();
        }
      stream_box->open(&mdat_box,jp2_codestream_4cc,false,true);
      last_opened_jp2c = stream_box;
    }
  next_stream_idx++;
}

/*****************************************************************************/
/*                    jx_target::close_any_open_stream                       */
/*****************************************************************************/

void
  jx_target::close_any_open_stream()
{
  if (last_opened_jp2c == NULL)
    return;
  if (last_opened_jp2c->exists())
    { KDU_ERROR_DEV(e,0x16071201); e <<
      KDU_TXT("Attempting to open a new top-level box or a new code-stream, "
              "while another contiguous code-stream box is still open.  After "
              "a call to `jpx_codestream_target::open_stream', you must be "
              "sure to finish writing the code-stream and close the box, "
              "before attempting to open another one, close the `jpx_target' "
              "object or invoke its `write_headers' or `write_metadata' "
              "function.");
    }
  if ((next_stream_idx > num_top_codestreams) && (max_j2cx_streams != 0))
    { 
      assert(mdat_box.exists());
      assert(last_opened_jp2c->get_header_length() == 0);
      kdu_long pos = last_opened_jp2c->get_start_pos();
      kdu_long len = last_opened_jp2c->get_box_length();
      tmp_frag_list.set_link_region(pos,len); // No need to finalize local frag
      last_opened_jp2c = NULL; // Avoid infinite recursion if multistream_root
                               // needs to open its own top-level box.
      if (!multistream_root.write_stream_ftbl(tmp_frag_list,this,NULL))
        { 
          assert((min_j2cx_streams >= 2) &&
                 (max_j2cx_streams >= min_j2cx_streams));
          multistream_root.finish();
          int max_cs = multistream_root.max_codestreams;
          if (max_cs < min_j2cx_streams)
            max_cs = min_j2cx_streams;
          else if ((multistream_root.num_codestreams >= max_cs) &&
                   (max_cs <= (max_j2cx_streams-max_cs)))
            max_cs += max_cs;
          multistream_root.init(max_cs);
          if (!multistream_root.write_stream_ftbl(tmp_frag_list,this,NULL))
            assert(0); // First fragment table is always writable!
        }
    }
  else
    { 
      assert(!mdat_box.exists());
      last_opened_jp2c = NULL;
    }
}

/*****************************************************************************/
/*                         jx_target::open_top_box                           */
/*****************************************************************************/

kdu_long
  jx_target::open_top_box(jp2_output_box *box, kdu_uint32 box_type,
                          int simulation_phase)
{
  close_any_open_stream();
  if (multistream_root.out_box.exists())
    multistream_root.close_boxes(); // Leaves `out_box' member closed
  if (mdat_box.exists())
    mdat_box.close(); // May be in use for codestream aggregation
  
  if (last_simulation_phase != simulation_phase)
    { 
      first_unsimulated_container = NULL; // Never does us any harm
      simulated_tgt.close();
      if (simulation_phase != 0)
        simulated_tgt.open(ultimate_tgt->get_bytes_written());
    }

  if (box == NULL)
    last_opened_top_box = NULL;

  if ((last_opened_top_box != NULL) && (last_opened_top_box->exists()))
    { KDU_ERROR_DEV(e,64); e <<
      KDU_TXT("Attempting to open a new top-level box within "
              "a JPX file, while another top-level box is already open!");
    }
  last_opened_top_box = NULL;
  
  kdu_long file_pos = 0;
  if (simulation_phase != 0)
    { 
      file_pos = simulated_tgt.get_bytes_written();
      if (box != NULL)
        box->open(&simulated_tgt,box_type);
    }
  else
    { 
      file_pos = ultimate_tgt->get_bytes_written();
      if (box != NULL)
        box->open(ultimate_tgt,box_type);
    }

  last_opened_top_box = box;
  return file_pos;
}

/*****************************************************************************/
/*                     jx_target::write_collected_boxes                      */
/*****************************************************************************/

void jx_target::write_collected_boxes(const jp2_output_box *src)
{
  assert((last_simulation_phase == 0) && (last_opened_top_box == NULL));
  src->write_box(ultimate_tgt,true);
}

/*****************************************************************************/
/*                    jx_target::write_top_level_headers                     */
/*****************************************************************************/

jp2_output_box *
  jx_target::write_top_level_headers(int *i_param, void **addr_param,
                                     int codestream_threshold)
{
  if (top_headers_complete)
    return NULL;
  if ((num_top_codestreams == 0) || (num_top_layers == 0))
    { KDU_ERROR_DEV(e,66); e <<
      KDU_TXT("Attempting to write a JPX file which has no "
              "top-level code-stream, or no top-level compositing layer.");
    }
  if (file_has_containers && composition.is_empty())
    { KDU_ERROR_DEV(e,0x07071203); e <<
      KDU_TXT("JPX containers may not be written to a file that "
              "contains no top-level composition box.  You need to "
              "add at least one top-level composition frame.");
    }

  jx_codestream_target *cp;
  jx_layer_target *lp;
  jx_container_target *cont;
  
  if (!(main_header_complete || headers_in_progress))
    { // Start by finalizing all top-level items.  This part of the
      // process cannot be interrupted.
      need_creg_boxes = file_has_containers;
      for (cp=codestreams; cp != NULL; cp=cp->next)
        cp->finalize();
      for (lp=layers; lp != NULL; lp=lp->next)
        if (lp->finalize())
          need_creg_boxes = true;      
      composition.set_layer_mapping(0,1,num_top_layers,num_top_layers);
      composition.finalize();
      cumulative_frame_count = composition.get_total_frames();
      cumulative_duration = composition.get_duration();
      
      // Finalize all containers that we currently know about
      for (cont=containers; cont != NULL; cont=cont->get_next())
        cont->finalize(cumulative_frame_count,cumulative_duration);
      first_unfinalized_container = NULL;
      
      // Next, initialize the defaults
      codestreams->copy_defaults(default_dimensions,default_palette,
                                 default_component_map);
      layers->copy_defaults(default_resolution,default_channels,
                            default_colour);
      
      // Now add compatibility information
      for (cp=codestreams; cp != NULL; cp=cp->next)
        cp->adjust_compatibility(&compatibility);
      for (lp=layers; lp != NULL; lp=lp->next)
        lp->adjust_compatibility(&compatibility);
      composition.adjust_compatibility(&compatibility);
      
      // Use all known containers to augment the compatibility picture
      for (cont=containers; cont != NULL; cont=cont->get_next())
        cont->adjust_compatibility(&compatibility);
      
      // Now write the initial header boxes
      open_top_box(&box,jp2_signature_4cc);
      box.write(jp2_signature);
      box.close();
      
      compatibility.save_boxes(this);
      composition.save_box(this);
      open_top_box(&box,jp2_header_4cc);
      default_dimensions.save_boxes(&box);
      j2_colour *compat, *cscan;
      for (compat=&default_colour; compat != NULL; compat=compat->next)
        if (compat->is_jp2_compatible())
          break;
      if (compat != NULL)
        compat->save_box(&box); // Try save JP2 compatible colour first
      for (cscan=&default_colour; cscan != NULL; cscan=cscan->next)
        if (cscan != compat)
          cscan->save_box(&box);
      default_palette.save_box(&box);
      default_component_map.save_box(&box);
      default_channels.save_boxes(&box,true);
      default_resolution.save_box(&box);
      box.close();
      main_header_complete = true;
    }

  // In case the application accidentally changes the threshold value between
  // incomplete calls to this function, we remember it here.  This ensures
  // that we take up at the correct place again after an application-installed
  // breakpoint
  if (headers_in_progress)
    codestream_threshold = this->last_codestream_threshold;
  this->last_codestream_threshold = codestream_threshold;
  headers_in_progress = true; // In case we get interrupted
  
  int idx;
  jp2_output_box *interrupted=NULL;
  for (idx=0, cp=codestreams; cp != NULL; cp=cp->next, idx++)
    { 
      if ((codestream_threshold >= 0) && (idx > codestream_threshold))
        break; // We have written enough for now
      if ((!cp->is_chdr_complete()) &&
          ((interrupted = cp->write_chdr(NULL,i_param,addr_param)) != NULL))
        return interrupted;
    }
  for (lp=layers; lp != NULL; lp=lp->next)
    { 
      if ((!lp->check_jplh_complete(need_creg_boxes)) &&
          ((interrupted = lp->write_jplh(NULL,need_creg_boxes,
                                         i_param,addr_param)) != NULL))
        return interrupted;
      if ((codestream_threshold >= 0) &&
          lp->uses_codestream(codestream_threshold))
        break;
    }
  if ((cp == NULL) && (lp == NULL))
    top_headers_complete = true;
  
  headers_in_progress = false;
  return NULL;
}

/*****************************************************************************/
/*                    jx_target::finalize_all_containers                     */
/*****************************************************************************/

void jx_target::finalize_all_containers()
{
  jx_container_target *cont=first_unfinalized_container;
  for (; cont != NULL; cont=cont->get_next())
    { 
      assert(need_creg_boxes);
      cont->finalize(cumulative_frame_count,cumulative_duration);
    }
  first_unfinalized_container = NULL;
}

/*****************************************************************************/
/*             jx_target::write_or_simulate_earlier_containers               */
/*****************************************************************************/

jp2_output_box *
  jx_target::write_or_simulate_earlier_containers(jx_container_target *caller,
                                                  int *i_param,
                                                  void **addr_param,
                                                  int simulation_phase)
{
  if (last_simulation_phase != simulation_phase)
    first_unsimulated_container = NULL; // We find the correct value below

  jx_container_target *cont;  
  if (simulation_phase != 0)
    { 
      if (first_unsimulated_container == NULL)
        first_unsimulated_container = first_unwritten_container;
      while ((cont = first_unsimulated_container) != caller)
        { 
          assert((cont->first_metanode == NULL) && !cont->written);
          jp2_output_box *interrupted =
            cont->write_jclx(i_param,addr_param,simulation_phase,NULL,NULL);
          if (interrupted != NULL)
            return interrupted;
          assert(cont->get_next() == first_unsimulated_container);
        }
    }
  else
    { 
      while ((cont = first_unwritten_container) != caller)
        { 
          assert((cont->first_metanode == NULL) && !cont->written);
          jp2_output_box *interrupted =
            cont->write_jclx(i_param,addr_param,simulation_phase,NULL,NULL);
          if (interrupted != NULL)
            return interrupted;
          assert(cont->get_next() == first_unwritten_container);
        }
    }

  return NULL;
}

/*****************************************************************************/
/*                 jx_target::note_jclx_written_or_simultated                */
/*****************************************************************************/

void
  jx_target::note_jclx_written_or_simulated(jx_container_target *caller,
                                            int simulation_phase)
{
  if (simulation_phase != 0)
    { 
      assert((first_unsimulated_container == NULL) ||
             (caller == first_unsimulated_container));
      first_unsimulated_container = caller->get_next();
    }
  else
    { 
      assert(first_unsimulated_container == NULL);
      assert(caller == first_unwritten_container);
      first_unwritten_container = first_unwritten_container->get_next();
    }
}

/* ========================================================================= */
/*                                 jpx_target                                */
/* ========================================================================= */

/*****************************************************************************/
/*                           jpx_target::jpx_target                          */
/*****************************************************************************/

jpx_target::jpx_target()
{
  state = NULL;
}

/*****************************************************************************/
/*                              jpx_target::open                             */
/*****************************************************************************/

void
  jpx_target::open(jp2_family_tgt *tgt)
{
  if (state != NULL)
    { KDU_ERROR_DEV(e,65); e <<
        KDU_TXT("Attempting to open a `jpx_target' object which "
        "is already opened for writing a JPX file.");
    }
  state = new jx_target(tgt);
}

/*****************************************************************************/
/*                             jpx_target::close                             */
/*****************************************************************************/
bool
  jpx_target::close()
{
  if (state == NULL)
    return false;
  jx_codestream_target *cp;
  for (cp=state->codestreams; cp != NULL; cp=cp->next)
    if (!cp->is_complete())
      break;
  jx_container_target *cont;
  for (cont=state->containers; cont != NULL; cont=cont->get_next())
    if (!cont->is_complete())
      break;
  
  state->close_any_open_stream();
  if (state->mdat_box.exists())
    state->mdat_box.close();
  
  if (state->main_header_complete && (cp != NULL))
    { KDU_WARNING_DEV(w,2); w <<
        KDU_TXT("Started writing a JPX file, but failed to "
                "write all top-level codestreams before calling "
                "`jpx_target::close'.");
    }
  else if (state->main_header_complete && (cont != NULL))
    { KDU_WARNING_DEV(w,0x03071208); w <<
      KDU_TXT("Started writing a JPX file, but failed to complete the "
              "writing of all JPX containers and/or all of their "
              "associated codestreams.");
    }
  else if (state->headers_in_progress)
    { KDU_WARNING_DEV(w,3); w <<
        KDU_TXT("Started writing JPX file headers, but failed "
                "to finish initiated sequence of calls to "
                "`jpx_target::write_headers'.  Problem is most likely due to "
                "the use of `jpx_codestream_target::set_breakpoint' or "
                "`jpx_layer_target::set_breakpoint' and failure to handle the "
                "breakpoints via multiple calls to "
                "`jpx_target::write_headers'.");
    }
  else if (state->main_header_complete)
    { // Write any outstanding headers
      bool missed_breakpoints = false;
      while ((!state->metadata_in_progress) && (write_headers() != NULL))
        missed_breakpoints = true;
      if (missed_breakpoints)
        { KDU_WARNING_DEV(w,4); w <<
            KDU_TXT("Failed to catch all breakpoints installed "
            "via `jpx_codestream_target::set_breakpoint' or "
            "`jpx_layer_target::set_breakpoint'.  All required compositing "
            "layer header boxes and codestream header boxes have been "
            "automatically written while closing the file, but some of these "
            "included application-installed breakpoints where the application "
            "would ordinarily have written its own extra boxes.  This "
            "suggests that the application has failed to make sufficient "
            "explicit calls to `jpx_target::write_headers'.");
        }
    }

  if (access_data_references().get_num_urls() > 0)
    {
      jp2_output_box dtbl; state->open_top_box(&dtbl,jp2_dtbl_4cc);
      state->data_references.save_box(&dtbl);
    }

  delete state;
  state = NULL;
  return true;
}

/*****************************************************************************/
/*                     jpx_target::access_data_references                    */
/*****************************************************************************/

jp2_data_references
  jpx_target::access_data_references()
{
  return jp2_data_references(&state->data_references);
}

/*****************************************************************************/
/*                      jpx_target::access_compatibility                     */
/*****************************************************************************/

jpx_compatibility
  jpx_target::access_compatibility()
{
  jpx_compatibility result;
  if (state != NULL)
    result = jpx_compatibility(&state->compatibility);
  return result;
}

/*****************************************************************************/
/*                          jpx_target::add_codestream                       */
/*****************************************************************************/

jpx_codestream_target
  jpx_target::add_codestream()
{
  jpx_codestream_target result;
  if (state != NULL)
    { 
      if (state->main_header_complete || state->headers_in_progress ||
          (state->containers != NULL))
        { KDU_ERROR_DEV(e,0x01071203); e <<
          KDU_TXT("Addition of top-level codestreams to a JPX file via "
                  "`jpx_target::add_codestream' must cease after the first "
                  "call to `jpx_target::write_headers' or "
                  "`jpx_target::add_container'.");
        }
      jx_codestream_target *cs =
        new jx_codestream_target(state,state->num_top_codestreams,NULL);
      if (state->last_codestream == NULL)
        state->codestreams = state->last_codestream = cs;
      else
        state->last_codestream = state->last_codestream->next = cs;
      state->num_top_codestreams++;
      state->total_codestreams++;
      result = jpx_codestream_target(cs);
    }
  return result;
}

/*****************************************************************************/
/*                            jpx_target::add_layer                          */
/*****************************************************************************/

jpx_layer_target
  jpx_target::add_layer()
{
  jpx_layer_target result;
  if (state != NULL)
    { 
      if (state->main_header_complete || state->headers_in_progress ||
          (state->containers != NULL))
        { KDU_ERROR_DEV(e,0x01071202); e <<
          KDU_TXT("Addition of top-level compositing layers to a JPX file "
                  "via `jpx_target::add_layer' must cease after the first "
                  "call to `jpx_target::write_headers' or "
                  "`jpx_target::add_container'.");
        }
      assert(!(state->main_header_complete || state->headers_in_progress));
      jx_layer_target *layer =
        new jx_layer_target(state,state->num_top_layers,NULL);
      if (state->last_layer == NULL)
        state->layers = state->last_layer = layer;
      else
        state->last_layer = state->last_layer->next = layer;
      state->num_top_layers++;
      state->total_layers++;
      result = jpx_layer_target(layer);
    }
  return result;
}

/*****************************************************************************/
/*                       jpx_target::access_composition                      */
/*****************************************************************************/

jpx_composition
  jpx_target::access_composition()
{
  jpx_composition result;
  if (state != NULL)
    result = jpx_composition(&state->composition);
  return result;
}

/*****************************************************************************/
/*                       jpx_target::expect_containers                       */
/*****************************************************************************/

void
  jpx_target::expect_containers()
{
  if (state != NULL)
    { 
      if ((state->main_header_complete || state->headers_in_progress) &&
          !state->file_has_containers)
        { KDU_ERROR_DEV(e,0x01091201); e <<
          KDU_TXT("The first call to `jpx_target::expect_containers' or "
                  "`jpx_target::add_container' must appear before the first "
                  "call to `jpx_target::write_headers' or "
                  "`jpx_target::write_metadata'.");
        }
      state->file_has_containers = true;
    }
}

/*****************************************************************************/
/*                         jpx_target::add_container                         */
/*****************************************************************************/

jpx_container_target
  jpx_target::add_container(int num_base_codestreams, int num_base_layers,
                            int repetition_factor)
{
  jpx_container_target result;
  if (num_base_codestreams < 0) num_base_codestreams = 0;
  if (repetition_factor < 0) repetition_factor = 0;
  if (state != NULL)
    { 
      if ((state->main_header_complete || state->headers_in_progress) &&
          !state->file_has_containers)
        { KDU_ERROR_DEV(e,0x01071207); e <<
          KDU_TXT("In order to retain the right to add JPX containers after "
                  "the first call to `jpx_target::write_headers' or "
                  "`jpx_target::write_metadata', you must add at least one "
                  "container or invoke `jpx_target::expect_containers' "
                  "before that first call.");
        }
      if ((state->last_container != NULL) &&
          state->last_container->indefinitely_repeated())
        { KDU_ERROR_DEV(e,0x03071201); e <<
          KDU_TXT("A JPX container must either have a fixed number of "
                  "repetitions or be the last one in the file; you are "
                  "attempting to add another container after one for "
                  "which the number of repetitions was supplied as 0 "
                  "(indefinite).");
        }
      if ((num_base_codestreams == 0) && (repetition_factor == 0))
        { KDU_ERROR_DEV(e,0x03071202); e <<
          KDU_TXT("A JPX container for which the number of repetitions is "
                  "not fixed (zero repetition factor supplied) must "
                  "have at least one codestream header box (non-zero "
                  "`num_base_codestreams' value).");
        }
      if (num_base_layers < 1)
        { KDU_ERROR_DEV(e,0x03071203); e <<
          KDU_TXT("JPX containers must have at least one compositing layer "
                  "header box.");
        }
      if ((state->num_top_codestreams < 1) || (state->num_top_layers < 1))
        { KDU_ERROR_DEV(e,0x07071201); e <<
          KDU_TXT("You must add at least one top-level compositing layer "
                  "and one top-level codestream to a `jpx_target' object "
                  "before invoking its `add_container' function.");
        }
      state->file_has_containers = true;
      jx_container_target *container =
        new jx_container_target(state,state->num_containers++,
                                state->num_top_layers,
                                state->num_top_codestreams,
                                repetition_factor,num_base_layers,
                                num_base_codestreams,state->total_layers,
                                state->total_codestreams);
      container->prev = state->last_container;
      if (state->last_container == NULL)
        { 
          state->containers = state->last_container = container;
          state->meta_manager.containers = container;
        }
      else
        { 
          state->last_container->next = container;
          state->last_container = container;
        }
      state->total_layers += repetition_factor * num_base_layers;
      state->total_codestreams += repetition_factor * num_base_codestreams;
      state->need_creg_boxes = true;
      if (state->first_unfinalized_container == NULL)
        state->first_unfinalized_container = container;
      if (state->first_unwritten_container == NULL)
        state->first_unwritten_container = container;
      result = jpx_container_target(container);
    }
  return result;
}

/*****************************************************************************/
/*                       jpx_target::access_container                        */
/*****************************************************************************/

jpx_container_target
  jpx_target::access_container(int which)
{
  jpx_container_target result;
  if ((state != NULL) && (which >= 0) && (which < state->num_containers))
    { 
      jx_container_target *scan = state->containers;
      for (; which > 0; which--, scan=scan->get_next())
        assert(scan != NULL);
      result = jpx_container_target(scan);
    }
  return result;
}

/*****************************************************************************/
/*               jpx_target::configure_codestream_aggregation                */
/*****************************************************************************/

void
  jpx_target::configure_codestream_aggregation(int min_j2cx_streams,
                                               int max_j2cx_streams)
{
  if (state == NULL)
    return;
  if (max_j2cx_streams <= 1)
    { 
      if ((state->next_stream_idx > state->num_top_codestreams) &&
          (state->min_j2cx_streams > 0))
        return; // No going back to top-level codestreams now
      min_j2cx_streams = max_j2cx_streams = 0;
    }
  else if (min_j2cx_streams < 2)
    min_j2cx_streams = 2;
  if (max_j2cx_streams < min_j2cx_streams)
    max_j2cx_streams = min_j2cx_streams;
  state->min_j2cx_streams = min_j2cx_streams;
  state->max_j2cx_streams = max_j2cx_streams;
}

/*****************************************************************************/
/*                      jpx_target::access_meta_manager                      */
/*****************************************************************************/

jpx_meta_manager
  jpx_target::access_meta_manager()
{
  jpx_meta_manager result;
  if (state != NULL)
    result = jpx_meta_manager(&state->meta_manager);
  return result;
}

/*****************************************************************************/
/*                        jpx_target::write_headers                          */
/*****************************************************************************/

jp2_output_box *
  jpx_target::write_headers(int *i_param, void **addr_param,
                            int codestream_threshold)
{
  assert(state != NULL);
  if (state->metadata_in_progress)
    { KDU_ERROR_DEV(e,0x16081201); e <<
      KDU_TXT("Calling `jpx_target::write_headers' before completing "
              "an outstanding sequence of calls to "
              "`jpx_target::write_metadata'.  You must call each of these "
              "functions until they return NULL before calling the "
              "other.");
    }
  jp2_output_box *interrupted =
    state->write_top_level_headers(i_param,addr_param,codestream_threshold);
  if (interrupted != NULL)
    return interrupted;
  if ((codestream_threshold >= 0) &&
      (codestream_threshold < state->num_top_codestreams))
    return NULL; // Not being asked to write any JPX containers yet
  
  state->finalize_all_containers();
  
  state->headers_in_progress = true; // In case we get interrupted
  while (state->first_unwritten_container != NULL)
    { 
      jx_container_target *cont = state->first_unwritten_container;
      if ((codestream_threshold >= 0) &&
          (codestream_threshold > cont->get_last_codestream()))
        break;
      jp2_output_box *interrupted = NULL;
      if (cont->first_metanode == NULL)
        interrupted =
          cont->write_jclx(i_param,addr_param,false,NULL,NULL);
      else
        interrupted =
          state->meta_manager.write_metadata(cont->last_metanode,
                                             i_param,addr_param);
      if (interrupted != NULL)
        return interrupted;
      assert(state->first_unwritten_container != cont);
         // Should have been bumped along by one of the above
    }

  state->headers_in_progress = false;
  return NULL;
}

/*****************************************************************************/
/*                        jpx_target::write_metadata                         */
/*****************************************************************************/

jp2_output_box *
  jpx_target::write_metadata(int *i_param, void **addr_param)
{
  assert(state != NULL);
  if (!state->metadata_in_progress)
    { // See if we need to write out top level headers and/or finalize
      // containers first.  This depends upon whether or not any of the
      // metadata to be written is embedded within JPX containers.  We'll
      // do a sweep to find this out first.
      if (state->headers_in_progress)
        { KDU_ERROR_DEV(e,0x16081202); e <<
          KDU_TXT("Calling `jpx_target::write_metadata' before completing "
                  "an outstanding sequence of calls to "
                  "`jpx_target::write_headers'.  You must call each of these "
                  "functions until they return NULL before calling the "
                  "other.");
        }
      jx_metanode *scan = state->meta_manager.first_unwritten;
      if (scan == NULL)
        return NULL; // Nothing to do
      for (; scan != NULL; scan=scan->next_sibling)
        if (scan->is_top_container_numlist())
          break;
      jp2_output_box *interrupted =
        state->write_top_level_headers(i_param,addr_param,(scan==NULL)?0:-1);
      if (interrupted != NULL)
        return interrupted;
      if (scan != NULL)
        state->finalize_all_containers();
      state->metadata_in_progress = true;
    }
  jp2_output_box *interrupted =
    state->meta_manager.write_metadata(NULL,i_param,addr_param);
  if (interrupted == NULL)
    state->metadata_in_progress = false;
  return interrupted;
}
