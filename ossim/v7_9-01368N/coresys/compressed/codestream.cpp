/*****************************************************************************/
// File: codestream.cpp [scope = CORESYS/COMPRESSED]
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
   Implements a part of the compressed data management machinery, including
code-stream I/O, packet sequencing and the top level interfaces associated with
codestream objects.
******************************************************************************/

#include <math.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include "kdu_elementary.h"
#include "kdu_utils.h"
#include "kdu_messaging.h"
#include "kdu_kernels.h"
#include "kdu_compressed.h"
#include "compressed_local.h"

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
   kdu_error _name("E(codestream.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(codestream.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) kdu_error _name("Kakadu Core Error:\n");
#  define KDU_WARNING(_name,_id) kdu_warning _name("Kakadu Core Warning:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


#define KDU_IDENTIFIER ("Kakadu-" KDU_CORE_VERSION)

using namespace kd_core_local;

/* ========================================================================= */
/*                              External Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERNAL             kdu_core_sample_alignment_checker                    */
/*****************************************************************************/

bool
  kdu_core::kdu_core_sample_alignment_checker(int overread_bytes,
                            int prealign_bytes, int align_samples16,
                            int align_samples32, bool return_on_fail,
                            bool strict)
{
  bool fail = false;
  if (strict)
    fail = ((overread_bytes != KDU_OVERREAD_BYTES) ||
            (prealign_bytes != KDU_PREALIGN_BYTES) ||
            (align_samples16 != KDU_ALIGN_SAMPLES16) ||
            (align_samples32 != KDU_ALIGN_SAMPLES32));
  else
    fail = ((overread_bytes > KDU_OVERREAD_BYTES) ||
            (prealign_bytes > KDU_PREALIGN_BYTES) ||
            (align_samples16 > KDU_ALIGN_SAMPLES16) ||
            (align_samples32 > KDU_ALIGN_SAMPLES32));
  if (fail)
    { 
      if (return_on_fail)
        return false;
      KDU_ERROR_DEV(e,0x28051401); e <<
      KDU_TXT("The core system was compiled with sample alignment "
              "constraints that are incompatible with those expected by the "
              "application, or another linked library.  You are receiving "
              "this error because a dangerous condition has arise due to "
              "changes in sample alignment constants such as "
              "`KDU_ALIGN_SAMPLE16' between the point at which the core "
              "system was compiled and the point at which another dependent "
              "element was compiled.  For debugging purposes, the call to "
              "`kdu_core_sample_alignment_checker' has been invoked with:\n\t")
      << "  `strict' = " << ((strict)?"true":"false") << " while:\n\t";
      e << "  `overread_bytes' = " << overread_bytes
        << "; `KDU_OVERREAD_BYTES' = " << KDU_OVERREAD_BYTES << "\n\t";
      e << "  `prealign_bytes' = " << prealign_bytes
        << "; `KDU_PREALIGN_BYTES' = " << KDU_PREALIGN_BYTES << "\n\t";
      e << "  `align_sample16' = " << align_samples16
        << "; `KDU_ALIGN_SAMPLES16' = " << KDU_ALIGN_SAMPLES16 << "\n\t";
      e << "  `align_sample32' = " << align_samples32
        << "; `KDU_ALIGN_SAMPLES32' = " << KDU_ALIGN_SAMPLES32 << "\n\t";
    }
  return true;
}

/*****************************************************************************/
/* EXTERNAL                    kdu_get_core_version                          */
/*****************************************************************************/

const char *
  kdu_core::kdu_get_core_version()
{
  return KDU_CORE_VERSION;
}

/*****************************************************************************/
/* EXTERN                   kd_create_dwt_description                        */
/*****************************************************************************/

void
  kd_core_local::kd_create_dwt_description(int kernel_id, int atk_idx,
                                     kdu_params *root, int tnum,
                                     bool &reversible, bool &symmetric,
                                     bool &symmetric_extension,
                                     int &num_steps,
                                     kdu_kernel_step_info * &step_info,
                                     float * &coefficients)
{
  step_info = NULL;
  coefficients = NULL;
  num_steps = 0;

  if (kernel_id == Ckernels_ATK)
    { // Read kernel parameters from ATK data
      kdu_params *atk = root->access_cluster(ATK_params);
      if ((atk == NULL) ||
          ((atk = atk->access_relation(tnum,-1,atk_idx,true)) == NULL))
        { KDU_ERROR(e,0x22050500); e <<
            KDU_TXT("Unable to find ATK marker segment referenced from "
                    "within an COD/COC or MCC marker segment.");
        }
      int atk_ext=0;
      if (!(atk->get(Ksymmetric,0,0,symmetric) &&
            atk->get(Kextension,0,0,atk_ext) &&
            atk->get(Kreversible,0,0,reversible)))
        abort();
      symmetric_extension = (atk_ext == Kextension_SYM);

      int c, n, s, Ls, num_coeffs=0;
      for (s=0; atk->get(Ksteps,s,0,Ls); s++)
        {
          num_coeffs += Ls;
          if (num_coeffs > 16384)
            { KDU_ERROR(e,0x06110800); e <<
                KDU_TXT("Custom DWT kernel found in ATK marker segment "
                        "contains a ridiculously large number of "
                        "coefficients!");
            }
        }
      num_steps = s;
      step_info = new kdu_kernel_step_info[num_steps];
      coefficients = new float[num_coeffs];
      for (c=s=0; s < num_steps; s++)
        {
          kdu_kernel_step_info *sp = step_info + s;
          if (!(atk->get(Ksteps,s,0,sp->support_length) &&
                atk->get(Ksteps,s,1,sp->support_min) &&
                atk->get(Ksteps,s,2,sp->downshift) &&
                atk->get(Ksteps,s,3,sp->rounding_offset)))
            assert(0);
          for (n=0; n < sp->support_length; n++, c++)
            atk->get(Kcoeffs,c,0,coefficients[c]);
        }
      assert(c == num_coeffs);
    }
  else
    { // Fill in description for Part-1 standard kernels
      symmetric = true;
      symmetric_extension = true;
      if (kernel_id == Ckernels_W5X3)
        {
          reversible = true;
          num_steps = 2;
          step_info = new kdu_kernel_step_info[num_steps];
          coefficients = new float[2*num_steps];
          coefficients[0] = coefficients[1] = -0.5F;
          coefficients[2] = coefficients[3] = 0.25F;
          step_info[0].downshift = 1;
          step_info[1].downshift = 2;
          step_info[0].rounding_offset = 1;
          step_info[1].rounding_offset = 2;
        }
      else if (kernel_id == Ckernels_W9X7)
        {
          reversible = false;
          num_steps = 4;
          step_info = new kdu_kernel_step_info[num_steps];
          coefficients = new float[2*num_steps];
          coefficients[0] = coefficients[1] = (float) -1.586134342;
          coefficients[2] = coefficients[3] = (float) -0.052980118;
          coefficients[4] = coefficients[5] = (float)  0.882911075;
          coefficients[6] = coefficients[7] = (float)  0.443506852;
        }
      else
        assert(0);
      for (int n=0; n < num_steps; n++)
        {
          step_info[n].support_length = 2;
          step_info[n].support_min =
            -((step_info[n].support_length + (n & 1) - 1) >> 1);
        }
    }
}


/* ========================================================================= */
/*                              Internal Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                        is_power_2                                  */
/*****************************************************************************/

static bool
  is_power_2(int val)
{
  for (; val > 1; val >>= 1)
    if (val & 1)
      return false;
    return (val==1);
}

/* ========================================================================= */
/*                              External Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                      print_marker_code                             */
/*****************************************************************************/

void
  kd_core_local::print_marker_code(kdu_uint16 code, kdu_message &out)
{
  const char *name=NULL;

  if (code == KDU_SOC)
    name = "SOC";
  else if (code == KDU_SOT)
    name = "SOT";
  else if (code == KDU_SOD)
    name = "SOD";
  else if (code == KDU_SOP)
    name = "SOP";
  else if (code == KDU_EPH)
    name = "EPH";
  else if (code == KDU_EOC)
    name = "EOC";
  else if (code == KDU_SIZ)
    name = "SIZ";
  else if (code == KDU_CBD)
    name = "CBD";
  else if (code == KDU_MCT)
    name = "MCT";
  else if (code == KDU_MCC)
    name = "MCC";
  else if (code == KDU_MCO)
    name = "MCO";
  else if (code == KDU_NLT)
    name = "NLT";
  else if (code == KDU_COD)
    name = "COD";
  else if (code == KDU_COC)
    name = "COC";
  else if (code == KDU_ADS)
    name = "ADS";
  else if (code == KDU_DFS)
    name = "DFS";
  else if (code == KDU_ATK)
    name = "ATK";
  else if (code == KDU_QCD)
    name = "QCD";
  else if (code == KDU_QCC)
    name = "QCC";
  else if (code == KDU_RGN)
    name = "RGN";
  else if (code == KDU_POC)
    name = "POC";
  else if (code == KDU_CRG)
    name = "CRG";
  else if (code == KDU_COM)
    name = "COM";
  else if (code == KDU_TLM)
    name = "TLM";
  else if (code == KDU_PLM)
    name = "PLM";
  else if (code == KDU_PLT)
    name = "PLT";
  else if (code == KDU_PPM)
    name = "PPM";
  else if (code == KDU_PPT)
    name = "PPT";

  if (name == NULL)
    {
      bool hex_state = out.set_hex_mode(true);
      out << "0x";
      out << code;
      out.set_hex_mode(hex_state);
    }
  else
    out << "<" << name << ">";
}


/* ========================================================================= */
/*                                 kd_input                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                    kd_input::process_unexpected_marker                    */
/*****************************************************************************/

void
  kd_input::process_unexpected_marker(kdu_byte last_byte)
{
  assert(throw_markers);
  kdu_uint16 code = 0xFF00; code += last_byte;
  disable_marker_throwing();
  if (!reject_all)
    {
      bool bona_fide = false;
      if ((code == KDU_SOP) || (code == KDU_SOT))
        {
          kdu_byte byte;
          kdu_uint16 length;
          if (!get(byte))
            exhausted = false;
          else
            {
              length = byte;
              if (!get(byte))
                {
                  exhausted = false;
                  putback((kdu_byte) length);
                }
              else
                {
                  length = (length<<8) + byte;
                  if (code == KDU_SOP)
                    bona_fide = (length == 4);
                  else
                    bona_fide = (length == 10);
                  putback(length);
                }
            }
        }
      if (!bona_fide)
        {
          enable_marker_throwing(reject_all);
          have_FF = (last_byte==0xFF);
          return; // Continue processing as though nothing had happened.
        }
    }
  assert(!exhausted);
  putback(code);
  throw code;
}

/*****************************************************************************/
/*                       kd_input::read (flat array)                         */
/*****************************************************************************/

int
  kd_input::read(kdu_byte *buf, int count)
{
  int xfer_bytes;
  int nbytes = 0;

  if (exhausted)
    return 0;
  while (count > 0)
    {
      if ((xfer_bytes = (int)(first_unwritten-first_unread)) == 0)
        {
          if (!load_buf())
            break;
          xfer_bytes = (int)(first_unwritten-first_unread);
          assert(xfer_bytes > 0);
        }
      xfer_bytes = (xfer_bytes < count)?xfer_bytes:count;
      nbytes += xfer_bytes;
      count -= xfer_bytes;
      if (throw_markers)
        { // Slower loop has to look for marker codes.
          kdu_byte byte;
          while (xfer_bytes--)
            {
              *(buf++) = byte = *(first_unread++);
              if (have_FF && (byte > 0x8F))
                process_unexpected_marker(byte);
              have_FF = (byte==0xFF);
            }
        }
      else
        { // Fastest loop. Probably not beneficial to use `memcpy'.
          while (xfer_bytes--)
            *(buf++) = *(first_unread++);
        }
    }
  return nbytes;
}

/*****************************************************************************/
/*                    kd_input::read (code buffer list)                      */
/*****************************************************************************/

int
  kd_input::read(kd_code_buffer *&cbuf, kdu_byte &cbuf_pos,
                 kd_buf_server *buf_server, int count)
{
  int xfer_bytes;
  int nbytes = 0;

  if (exhausted)
    return 0;

  // Create local working variables in place of `cbuf' and `cbpos' to
  // maximize access efficiency in the loop.  Copy modified results back
  // once we are done.
  kd_code_buffer *cbp = cbuf;
  kdu_byte *cbuf_dest = cbp->buf + cbuf_pos;
  int cbuf_remains = KD_CODE_BUFFER_LEN - cbuf_pos;
  while (count > 0)
    {
      if ((xfer_bytes = (int)(first_unwritten-first_unread)) == 0)
        {
          if (!load_buf())
            break;
          xfer_bytes = (int)(first_unwritten-first_unread);
          assert(xfer_bytes > 0);
        }
      xfer_bytes = (xfer_bytes < count)?xfer_bytes:count;
      nbytes += xfer_bytes;
      count -= xfer_bytes;
      if (!throw_markers)
        while (1)
          {
            if (xfer_bytes <= cbuf_remains)
              {
                memcpy(cbuf_dest,first_unread,(size_t) xfer_bytes);
                cbuf_remains -= xfer_bytes;
                cbuf_dest += xfer_bytes;
                first_unread += xfer_bytes;
                break;
              }
            else
              {
                memcpy(cbuf_dest,first_unread,(size_t) cbuf_remains);
                xfer_bytes -= cbuf_remains;
                cbuf_dest += cbuf_remains;
                first_unread += cbuf_remains;
                cbp = cbp->next = buf_server->get();
                cbuf_remains = KD_CODE_BUFFER_LEN;
                cbuf_dest = cbp->buf;
              }
          }
      else
        { // Slower loop has to look for marker codes.
          kdu_byte byte;
          while (1)
            {
              if (xfer_bytes <= cbuf_remains)
                {
                  cbuf_remains -= xfer_bytes;
                  while (xfer_bytes--)
                    {
                      *(cbuf_dest++) = byte = *(first_unread++);
                      if (have_FF && (byte > 0x8F))
                        process_unexpected_marker(byte);
                      have_FF = (byte==0xFF);
                    }
                  break;
                }
              else
                {
                  xfer_bytes -= cbuf_remains;
                  for (; cbuf_remains > 0; cbuf_remains--)
                    {
                      *(cbuf_dest++) = byte = *(first_unread++);
                      if (have_FF && (byte > 0x8F))
                        process_unexpected_marker(byte);
                      have_FF = (byte==0xFF);
                    }
                  cbp = cbp->next = buf_server->get();
                  cbuf_remains = KD_CODE_BUFFER_LEN;
                  cbuf_dest = cbp->buf;
                }
            }
        }
    }

  cbuf = cbp;
  cbuf_pos = (kdu_byte)(KD_CODE_BUFFER_LEN-cbuf_remains);
  return nbytes;
}

/*****************************************************************************/
/*                             kd_input::ignore                              */
/*****************************************************************************/

kdu_long
  kd_input::ignore(kdu_long count)
{
  int xfer_bytes;
  kdu_long nbytes = 0;

  if (exhausted)
    return 0;

  while (count > 0)
    {
      if ((xfer_bytes = (int)(first_unwritten-first_unread)) == 0)
        {
          if (!load_buf())
            break;
          xfer_bytes = (int)(first_unwritten-first_unread);
          assert(xfer_bytes > 0);
        }
      if (count < (kdu_long) xfer_bytes)
        xfer_bytes = (int) count;
      nbytes += xfer_bytes;
      count -= xfer_bytes;
      if (throw_markers)
        { // Slower loop has to look for marker codes.
          kdu_byte byte;
          while (xfer_bytes--)
            {
              byte = *(first_unread++);
              if (have_FF && (byte > 0x8F))
                process_unexpected_marker(byte);
              have_FF = (byte==0xFF);
            }
        }
      else
        first_unread += xfer_bytes;
    }
  return nbytes;
}

/* ========================================================================= */
/*                            kd_compressed_input                            */
/* ========================================================================= */

/*****************************************************************************/
/*                  kd_compressed_input::kd_compressed_input                 */
/*****************************************************************************/

kd_compressed_input::kd_compressed_input(kdu_compressed_source *source)
{
  this->source = source;
  cur_offset = 0;
  max_bytes_allowed = (KDU_LONG_MAX>>1);
  max_address_read = 0;
  suspend_ptr = alt_first_unwritten = NULL;
  suspended_bytes = last_loaded_bytes = 0;
  special_scope = false;
  if (source->get_capabilities() & KDU_SOURCE_CAP_IN_MEMORY)
    { 
      kdu_long mem_pos;
      kdu_byte *mem, *mem_lim;
      if ((mem = source->access_memory(mem_pos,mem_lim)) == NULL)
        return;
      assert(mem_pos >= 0);
      fully_buffered = true;
      first_unread = mem;
      first_unwritten = mem_lim;
      cur_offset = 0;
      last_loaded_bytes = first_unwritten - first_unread;
    }
}

/*****************************************************************************/
/*                     kd_compressed_input::set_max_bytes                    */
/*****************************************************************************/

void
  kd_compressed_input::set_max_bytes(kdu_long limit)
{
  if (special_scope)
    return;
  if (limit >= max_bytes_allowed)
    return; // Only allow tighter restrictions
  if (limit > (KDU_LONG_MAX>>1))
    limit = KDU_LONG_MAX>>1;
  max_bytes_allowed = limit;
  if (suspend_ptr != NULL)
    return; // Don't actually apply any limits until suspension is over.
  limit = max_bytes_allowed + suspended_bytes - cur_offset;
  if (limit < last_loaded_bytes)
    { // We have already loaded the buffer with too many bytes.
      if (alt_first_unwritten == NULL)
        alt_first_unwritten = first_unwritten;
      first_unwritten -= (last_loaded_bytes - limit);
      last_loaded_bytes = limit;
      if (first_unwritten < first_unread)
        { // We have already read past the end.
          exhausted = true;
          first_unwritten = first_unread;
          alt_first_unwritten = NULL;
        }
    }
}

/*****************************************************************************/
/*                       kd_compressed_input::load_buf                       */
/*****************************************************************************/

bool
  kd_compressed_input::load_buf()
{
  assert(first_unwritten == first_unread);
  if (fully_buffered)
    { exhausted = true; return false; }

  first_unread = buffer + KD_IBUF_PUTBACK;
  cur_offset += (first_unwritten - first_unread);
  if (special_scope)
    {
      last_loaded_bytes = source->read(first_unread,KD_IBUF_SIZE);
      first_unwritten = first_unread + last_loaded_bytes;
      if (last_loaded_bytes == 0)
        exhausted = true;
    }
  else if (suspend_ptr == NULL)
    { // Not in suspended state; byte limits may apply.
      alt_first_unwritten = NULL;
      last_loaded_bytes = max_bytes_allowed + suspended_bytes - cur_offset;
      first_unwritten = first_unread;
      if (last_loaded_bytes <= 0)
        { exhausted = true; last_loaded_bytes=0; return false; }
      if (last_loaded_bytes > KD_IBUF_SIZE)
        last_loaded_bytes = KD_IBUF_SIZE;
      last_loaded_bytes = source->read(first_unread,(int) last_loaded_bytes);
      first_unwritten += last_loaded_bytes;
      if (last_loaded_bytes == 0)
        exhausted = true;
    }
  else
    { // In suspended state; no byte limits apply, but keep track of things
      assert(alt_first_unwritten == NULL);
      assert((suspend_ptr>=first_unread) && (suspend_ptr<=first_unwritten));
      suspended_bytes += first_unwritten - suspend_ptr;
      first_unwritten = first_unread;
      suspend_ptr = first_unread;
      last_loaded_bytes = source->read(first_unread,KD_IBUF_SIZE);
      first_unwritten += last_loaded_bytes;
      if (last_loaded_bytes == 0)
        exhausted = true;
    }
  return !exhausted;
}

/*****************************************************************************/
/*                    kd_compressed_input::get_bytes_read                    */
/*****************************************************************************/

kdu_long
  kd_compressed_input::get_bytes_read()
{
  if (special_scope)
    return 0;
  kdu_long last_read =
    cur_offset + last_loaded_bytes - (first_unwritten-first_unread) - 1;
  if (last_read > max_address_read)
    max_address_read = last_read;
  return max_address_read+1;
}

/*****************************************************************************/
/*                 kd_compressed_input::get_suspended_bytes                  */
/*****************************************************************************/

kdu_long
  kd_compressed_input::get_suspended_bytes()
{
  if (special_scope)
    return 0;
  if (suspend_ptr != NULL)
    {
      assert((suspend_ptr <= first_unread) &&
             (suspend_ptr >= (first_unwritten-last_loaded_bytes)));
      suspended_bytes += first_unread - suspend_ptr;
      suspend_ptr = first_unread;
    }
  return suspended_bytes;
}

/*****************************************************************************/
/*                  kd_compressed_input::set_tileheader_scope                */
/*****************************************************************************/

bool
  kd_compressed_input::set_tileheader_scope(int tnum, int num_tiles)
{
  special_scope = true;
  first_unread = buffer + KD_IBUF_PUTBACK;
  if (!source->set_tileheader_scope(tnum,num_tiles))
    {
      if ((source->get_capabilities() & KDU_SOURCE_CAP_CACHED) == 0)
        {
          KDU_ERROR_DEV(e,0); e <<
            KDU_TXT("Attempting to load cached tile header data from "
            "a compressed data source which does not appear to "
            "support caching.  It is possible that the source has "
            "been incorrectly implemented.");
        }
      first_unwritten = first_unread;
      exhausted = true;
      return false;
    }
  int xfer_bytes = source->read(first_unread,KD_IBUF_SIZE);
  first_unwritten = first_unread + xfer_bytes;
  assert(xfer_bytes >= 0);
  exhausted = (xfer_bytes == 0);
  return true;
}

/*****************************************************************************/
/*                         kd_compressed_input::seek                         */
/*****************************************************************************/

void
  kd_compressed_input::seek(kdu_long address)
{
  assert(!throw_markers); // Marker throwing fragile; no seeking permitted

  if (address < 0)
    { // Only for cacheable source.
      special_scope = true;
      first_unread = first_unwritten = buffer + KD_IBUF_PUTBACK;
      if (!source->set_precinct_scope(-(1+address)))
        { KDU_ERROR_DEV(e,1); e <<
            KDU_TXT("Attempting to load cached precinct packets from "
            "a compressed data source which does not appear to "
            "support caching.  It is possible that the source has "
            "been incorrectly implemented.");
        }
      int xfer_bytes = source->read(first_unread,KD_IBUF_SIZE);
      first_unwritten = first_unread + xfer_bytes;
      exhausted = (xfer_bytes == 0);
      return;
    }

  kdu_long cur_addr =
    cur_offset + last_loaded_bytes - (first_unwritten - first_unread);
  kdu_long cur_size = last_loaded_bytes;
  if (address == cur_addr)
    return; // Already at exactly the right location.

  // Before changing context, see if `max_address_read' needs updating.
  kdu_long last_read = cur_addr-1;
  if (last_read > max_address_read)
    max_address_read = last_read;

  // Now for the seeking code.
  alt_first_unwritten = NULL; // Just in case
  if (address >= max_bytes_allowed)
    {
      exhausted = true;
      if (!fully_buffered)
        {
          cur_offset = max_bytes_allowed;
          first_unwritten = buffer + KD_IBUF_PUTBACK;
        }
      first_unread = first_unwritten;
      return;
    }
  exhausted = false;
  if ((address >= cur_offset) && (address < (cur_offset+cur_size)))
    { // No need to read any new data into the internal buffer.
      first_unread += address - cur_addr;
      return;
    }
  assert(!fully_buffered);
  if (suspend_ptr != NULL)
    {
      kdu_long extra_sus = (first_unread - suspend_ptr) + (address-cur_addr);
      suspend_ptr = buffer + KD_IBUF_PUTBACK;
      if (extra_sus > 0)
        suspended_bytes += extra_sus;
    }
  cur_offset = address;
  first_unread = first_unwritten = buffer + KD_IBUF_PUTBACK;
  if (!source->seek(address))
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("Attempting to seek inside a compressed data source "
        "which does not appear to support seeking.  The source may "
        "have been implemented incorrectly.");
    }
  last_loaded_bytes = max_bytes_allowed - cur_offset;
  if (last_loaded_bytes > KD_IBUF_SIZE)
    last_loaded_bytes = KD_IBUF_SIZE;
  last_loaded_bytes = source->read(first_unread,(int) last_loaded_bytes);
  first_unwritten = first_unread + last_loaded_bytes;
  if (last_loaded_bytes == 0)
    exhausted = true;
}

/*****************************************************************************/
/*                        kd_compressed_input::ignore                        */
/*****************************************************************************/

kdu_long
  kd_compressed_input::ignore(kdu_long count)
{
  if (throw_markers ||
      !(source->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE))
    return kd_input::ignore(count);
  kdu_long old_addr, new_addr;
  old_addr = cur_offset + last_loaded_bytes - (first_unwritten-first_unread);
  new_addr = old_addr + count;
  seek(new_addr);
  new_addr = cur_offset + last_loaded_bytes - (first_unwritten-first_unread);
  return new_addr-old_addr;
}


/* ========================================================================= */
/*                               kd_pph_input                                */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_pph_input::~kd_pph_input                        */
/*****************************************************************************/

kd_pph_input::~kd_pph_input()
{
  read_buf = NULL; // Just in case.
  while ((write_buf=first_buf) != NULL)
    {
      first_buf = write_buf->next;
      buf_server->release(write_buf);
    }
}

/*****************************************************************************/
/*                         kd_pph_input::add_bytes                           */
/*****************************************************************************/

void
  kd_pph_input::add_bytes(kdu_byte *data, int num_bytes)
{
  while (num_bytes > 0)
    {
      if (write_buf == NULL)
        {
          write_buf = read_buf = first_buf = buf_server->get();
          write_pos = read_pos = 0;
        }
      else if (write_pos == KD_CODE_BUFFER_LEN)
        {
          write_buf = write_buf->next = buf_server->get();
          write_pos = 0;
        }
      int xfer_bytes = KD_CODE_BUFFER_LEN-write_pos;
      if (xfer_bytes > num_bytes)
        xfer_bytes = num_bytes;
      num_bytes -= xfer_bytes;
      while (xfer_bytes--)
        write_buf->buf[write_pos++] = *(data++);
    }
}

/*****************************************************************************/
/*                          kd_pph_input::load_buf                           */
/*****************************************************************************/

bool
  kd_pph_input::load_buf()
{
  if (read_buf == NULL)
    { exhausted = true; return false; }

  first_unread = first_unwritten = buffer + KD_IBUF_PUTBACK;
  int xfer_bytes = KD_IBUF_SIZE - KD_IBUF_PUTBACK;
  int buf_bytes;
  while (xfer_bytes > 0)
    {
      if (read_pos == KD_CODE_BUFFER_LEN)
        {
          if (read_buf != write_buf)
            {
              read_buf = read_buf->next;
              read_pos = 0;
              assert(read_buf != NULL);
            }
        }
      buf_bytes = (read_buf==write_buf)?write_pos:KD_CODE_BUFFER_LEN;
      buf_bytes -= read_pos;
      assert(buf_bytes >= 0);
      if (buf_bytes == 0)
        break;
      if (buf_bytes > xfer_bytes)
        buf_bytes = xfer_bytes;
      xfer_bytes -= buf_bytes;
      while (buf_bytes--)
        *(first_unwritten++) = read_buf->buf[read_pos++];
    }
  if (first_unread == first_unwritten)
    { exhausted = true; return false; }
  return true;
}

/* ========================================================================= */
/*                                 kd_marker                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_marker::kd_marker (copy)                        */
/*****************************************************************************/

kd_marker::kd_marker(const kd_marker &orig)
{
  source = NULL;
  codestream = orig.codestream;
  code = orig.code;
  max_length = length = orig.length;
  buf = NULL;
  if (max_length > 0)
    {
      buf = new kdu_byte[max_length];
      memcpy(buf,orig.buf,(size_t) length);
    }
  encountered_skip_code = false;
}

/*****************************************************************************/
/*                              kd_marker::read                              */
/*****************************************************************************/

bool
  kd_marker::read(bool exclude_stuff_bytes, bool skip_to_marker)
{
  assert(source != NULL);
  kdu_byte byte = 0;
  bool valid_code, force_skip;

  source->disable_marker_throwing();
  do {
      force_skip = false;
      if ((byte != 0xFF) && !source->get(byte))
        { code = 0; length = 0; return false; }
      if (skip_to_marker)
        while (byte != 0xFF)
          if (!source->get(byte))
            { code = 0; length = 0; return false; }
      if (byte != 0xFF)
        {
          source->putback(byte);
          code = 0; length = 0; return false;
        }
      if (!source->get(byte))
        { code = 0; length = 0; return false; }
      code = 0xFF00 + byte;
      valid_code = true;
      if (exclude_stuff_bytes)
        valid_code = (byte > 0x8F);
      if ((code == KDU_SOP) || (code == KDU_SOT))
        { // Want to be really sure that this is not a corrupt marker code.
          assert(valid_code);
          if (!source->get(byte))
            { code = 0; length = 0; return false; }
          length = byte; length <<= 8;
          if (!source->get(byte))
            { code = 0; length = 0; return false; }
          length += byte;
          if ((code == KDU_SOP) && (length != 4))
            {
              valid_code = false;
              source->putback((kdu_uint16) length);
              KDU_WARNING(w,0); w <<
                KDU_TXT("Skipping over corrupt SOP marker code!");
            }
          else if ((code == KDU_SOT) && (length != 10))
            {
              valid_code = false;
              source->putback((kdu_uint16) length);
              KDU_WARNING(w,1); w <<
                KDU_TXT("Skipping over corrupt SOT marker code!");
            }
          byte = (kdu_byte) code;
        }
      else if (code == KDU_EOC)
        { /* If the input source is about to end, we will simply skip over
             the EOC marker so that true EOC termination can be treated in
             the same way as premature termination of the code-stream.
             Otherwise, the EOC marker would appear to have been generated
             by some type of code-stream corruption.  In this case, we will
             treat it as an invalid marker code. */
          if (source->get(byte))
            {
              if (!codestream->resilient)
                {
                  source->putback(byte);
                  source->terminate_prematurely();
                  length = 0; code = 0;
                  return false;
                }

              valid_code = false;
              source->putback(byte);
              byte = (kdu_byte) code;
              KDU_WARNING(w,2); w <<
                KDU_TXT("Disregarding non-terminal EOC marker.");
            }
          else
            { length = 0; code = 0; return false; }
        }
      else if (valid_code && (code >= 0xFF30) && (code <= 0xFF3F))
        { // These codes are all to be skipped.
          valid_code = false;
          byte = 0;
          if (!encountered_skip_code)
            { KDU_WARNING_DEV(w,3); w <<
                KDU_TXT("Encountered one or more marker codes in "
                "the range 0xFF30 to 0xFF3F.  These are to be ignored.");
            }
          encountered_skip_code = true;
          force_skip = true;
        }
    } while ((skip_to_marker || force_skip) && (!valid_code));

  if (!valid_code)
    {
      source->putback(code);
      code = 0; length = 0;
      return false;
    }

  // Now we are committed to processing the marker, returning false only if
  // the source becomes exhausted.

  if ((code == KDU_SOC) || (code == KDU_SOD) ||
      (code == KDU_EOC) || (code == KDU_EPH))
    return true; // Delimiter found. There is no marker segment.
  if ((code != KDU_SOT) && (code != KDU_SOP) && (code != KDU_SIZ) &&
      (code != KDU_CBD) && (code != KDU_CAP) && (code != KDU_MCT) &&
      (code != KDU_MCC) && (code != KDU_MCO) && (code != KDU_NLT) &&
      (code != KDU_COD) && (code != KDU_COC) &&
      (code != KDU_ADS) && (code != KDU_DFS) && (code != KDU_ATK) &&
      (code != KDU_QCD) && (code != KDU_QCC) &&
      (code != KDU_RGN) && (code != KDU_POC) &&
      (code != KDU_CRG) && (code != KDU_COM) &&
      (code != KDU_TLM) && (code != KDU_PLM) && (code != KDU_PLT) &&
      (code != KDU_PPM) && (code != KDU_PPT))
    {
      KDU_WARNING_DEV(w,4); w <<
        KDU_TXT("Unrecognized/unimplemented marker code, ");
      print_current_code(w);
      w << KDU_TXT(", found in code-stream.");
    }
  if ((code != KDU_SOP) && (code != KDU_SOT))
    { // Otherwise, we already have the length.
      if (!source->get(byte))
        { code = 0; return false; }
      length = byte; length <<= 8;
      if (!source->get(byte))
        { code = 0; length = 0; return false; }
      length += byte;
    }
  length -= 2;
  if (length < 0)
    { code = 0; length = 0; return false; }
  if (length > max_length)
    {
      max_length = 2*length; // Don't want to have to re-allocate too often
      delete[] buf;
      buf = NULL;
      buf = new kdu_byte[max_length];
    }
  if (source->read(buf,length) < length)
    {
      code = 0; length = 0;
      return false;
    }
  return true;
}


/* ========================================================================= */
/*                                kd_pp_markers                              */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_pp_markers::~kd_pp_markers                      */
/*****************************************************************************/

kd_pp_markers::~kd_pp_markers()
{
  kd_pp_marker_list *tmp;
  while ((tmp=list) != NULL)
    {
      list = tmp->next;
      delete tmp;
    }
}

/*****************************************************************************/
/*                          kd_pp_markers::add_marker                        */
/*****************************************************************************/

void
  kd_pp_markers::add_marker(kd_marker &copy_source)
{
  if (copy_source.get_length() < 1)
    { KDU_ERROR(e,3); e <<
        KDU_TXT("PPM/PPT marker segments must be at least 3 bytes long!");
    }
  kd_pp_marker_list *elt =
    new kd_pp_marker_list(copy_source);
  kdu_byte *data = elt->get_bytes();
  elt->znum = data[0];
  elt->bytes_read = 1;
  if (elt->get_code() == KDU_PPM)
    {
      assert((list == NULL) || is_ppm);
      is_ppm = true;
    }
  else
    {
      assert(elt->get_code() == KDU_PPT);
      assert((list == NULL) || !is_ppm);
      is_ppm = false;
    }

  kd_pp_marker_list *scan, *prev;

  for (prev=NULL, scan=list; scan != NULL; prev=scan, scan=scan->next)
    if (scan->znum > elt->znum)
      break;
  elt->next = scan;
  if (prev == NULL)
    list = elt;
  else
    {
      prev->next = elt;
      if (prev->znum == elt->znum)
        { KDU_ERROR(e,4); e <<
            KDU_TXT("Found multiple PPM/PPT marker segments with "
            "identical Zppt/Zppm indices within the same header scope "
            "(main or tile-part header)!");
        }
    }
}

/*****************************************************************************/
/*                        kd_pp_markers::transfer_tpart                      */
/*****************************************************************************/

void
  kd_pp_markers::transfer_tpart(kd_pph_input *pph_input)
{
  int xfer_bytes = INT_MAX;

  if (is_ppm)
    {
      while ((list != NULL) && (list->bytes_read == list->get_length()))
        advance_list();
      if (list == NULL)
        { KDU_ERROR(e,5); e <<
            KDU_TXT("Insufficient packet header data in PPM marker "
            "segments!");
        }
      if ((list->get_length()-list->bytes_read) < 4)
        { KDU_ERROR(e,6); e <<
            KDU_TXT("Encountered malformed PPM marker: 4-byte Nppm "
            "values may not straddle multiple PPM marker segments.  "
            "Problem is most likely due to a previously incorrect "
            "Nppm value.");
        }
      kdu_byte *data = list->get_bytes();
      xfer_bytes = data[list->bytes_read++];
      xfer_bytes = (xfer_bytes << 8) + data[list->bytes_read++];
      xfer_bytes = (xfer_bytes << 8) + data[list->bytes_read++];
      xfer_bytes = (xfer_bytes << 8) + data[list->bytes_read++];
    }
  while ((list != NULL) && (xfer_bytes > 0))
    {
      int elt_bytes = list->get_length()-list->bytes_read;
      if (elt_bytes > xfer_bytes)
        elt_bytes = xfer_bytes;
      pph_input->add_bytes(list->get_bytes()+list->bytes_read,elt_bytes);
      xfer_bytes -= elt_bytes;
      list->bytes_read += elt_bytes;
      if (list->bytes_read == list->get_length())
        advance_list();
    }
  if (is_ppm && (xfer_bytes > 0))
    { KDU_ERROR(e,7); e <<
        KDU_TXT("Insufficient packet header data in PPM marker "
        "segments, or else Nppm values must be incorrect!");
    }
}

/*****************************************************************************/
/*                         kd_pp_markers::ignore_tpart                       */
/*****************************************************************************/

void
  kd_pp_markers::ignore_tpart()
{
  int xfer_bytes = INT_MAX;

  if (is_ppm)
    {
      kdu_byte byte;

      int len_bytes = 0;
      while (len_bytes < 4)
        { // Need to read 4 bytes of length information.
          if (list == NULL)
            { KDU_ERROR(e,8); e <<
                KDU_TXT("Insufficient packet header data in PPM "
                "marker segments!");
            }
          if (list->bytes_read == list->get_length())
            {
              advance_list();
              continue;
            }
          byte = (list->get_bytes())[list->bytes_read++];
          xfer_bytes = (xfer_bytes << 8) + byte;
          len_bytes++;
        }
    }
  while ((list != NULL) && (xfer_bytes > 0))
    {
      int elt_bytes = list->get_length()-list->bytes_read;
      if (elt_bytes > xfer_bytes)
        elt_bytes = xfer_bytes;
      xfer_bytes -= elt_bytes;
      list->bytes_read += elt_bytes;
      if (list->bytes_read == list->get_length())
        advance_list();
    }
  if (is_ppm && (xfer_bytes > 0))
    { KDU_ERROR(e,9); e <<
        KDU_TXT("Insufficient packet header data in PPM marker "
        "segments, or else Nppm values must be incorrect!");
    }
}

/*****************************************************************************/
/*                         kd_pp_markers::advance_list                       */
/*****************************************************************************/

void
  kd_pp_markers::advance_list()
{
  assert((list != NULL) && (list->bytes_read == list->get_length()));
  kd_pp_marker_list *tmp = list;
  list = tmp->next;
  delete tmp;
}


/* ========================================================================= */
/*                             kd_tlm_generator                              */
/* ========================================================================= */

/*****************************************************************************/
/*                          kd_tlm_generator::init                           */
/*****************************************************************************/

bool
  kd_tlm_generator::init(int num_tiles, int max_tparts,
                         int tnum_bytes, int tplen_bytes)
{
  clear();
  if ((tnum_bytes < 0) || (tnum_bytes > 2) ||
      ((tplen_bytes != 2) && (tplen_bytes != 4)))
    return false;
  if (tnum_bytes == 0)
    { 
      if (max_tparts != 1)
        return false;
    }
  else if (tnum_bytes == 1)
    {
      if (num_tiles > 256)
        return false;
    }
  this->num_tiles = num_tiles;
  if (max_tparts < 0)
    max_tparts = 1;
  else if (max_tparts > 255)
    max_tparts = 255;
  this->max_tparts = max_tparts;
  this->tnum_prec = tnum_bytes;
  this->tplen_prec = tplen_bytes;
  record_bytes = tnum_prec + tplen_prec;
  
  num_elts = max_tparts * num_tiles;
  elt_ctr = 0;
  tile_data_bytes = 0;

  int z_tlm = 0;
  int elts_left = num_elts;
  tlm_bytes = 0;
  do {
      int tlm_elts = (65535-4) / record_bytes;
      if (tlm_elts > elts_left)
        tlm_elts = elts_left;
      elts_left -= tlm_elts;
      tlm_bytes += 6 + record_bytes*tlm_elts;
      z_tlm++;
    } while ((elts_left > 0) && (z_tlm < 255));

  if (elts_left > 0)
    { // Cannot write a legal set of TLM marker segments -- too many
      // tile-parts.
      clear();
      return false;
    }

  elts = new kd_tlm_elt[num_elts];
  return true;
}

/*****************************************************************************/
/*                      kd_tlm_generator::add_tpart_length                   */
/*****************************************************************************/

void
  kd_tlm_generator::add_tpart_length(int tnum, kdu_long length)
{
  if (!exists())
    return;
  assert(elt_ctr < num_elts);
  assert((tnum >= 0) && (tnum < num_tiles));
  elts[elt_ctr].tnum = (kdu_uint16) tnum;
  elts[elt_ctr].length = (kdu_uint32) length;
  kdu_long max_tplen = (tplen_prec==2)?0x0FFFFL:0x0FFFFFFFFL;
  if (length > max_tplen)
    { KDU_ERROR(e,10); e <<
      KDU_TXT("Attempting to write TLM (tile-part length) data where "
              "at least one tile-part's length cannot be represented as an "
              "unsigned value with the precision identified via the "
              "`ORGtlm_style' parameter attribute -- or 32 bits if no such "
              "attribute was specified.");
    }
  if ((tnum_prec == 0) && (tnum != elt_ctr))
    { KDU_ERROR(e,0x25041001); e <<
      KDU_TXT("Attempting to write TLM (tile-part length) data using "
              "the \"implied\" tile-numbering style, as specified via "
              "the `ORGtlm_style' parameter attribute.  However, this "
              "requires tiles to be written in lexicographic order, which "
              "is not what's happening!");
    }
  elt_ctr++;
  tile_data_bytes += length;
}

/*****************************************************************************/
/*                      kd_tlm_generator::write_dummy_tlms                   */
/*****************************************************************************/

void
  kd_tlm_generator::write_dummy_tlms(kd_compressed_output *out)
{
  if (!exists())
    return;
  int z_tlm = 0;
  int elts_left = num_elts;
  int check_tlm_bytes = 0;
  kdu_byte *marker_body = new kdu_byte[65535];
  memset(marker_body,0,65535);
  try { // Protect against possible memory leaks
    do {
      assert(z_tlm <= 255);
      int tlm_elts = (65535-4) / record_bytes;
      if (tlm_elts > elts_left)
        tlm_elts = elts_left;
      elts_left -= tlm_elts;
      check_tlm_bytes += 6 + record_bytes*tlm_elts;
      out->put(KDU_TLM);
      out->put((kdu_uint16)(4+record_bytes*tlm_elts));
      out->put((kdu_byte) z_tlm);
      out->put((kdu_byte)((tnum_prec<<4)+((tplen_prec==4)?0x40:0)));
      z_tlm++;
      out->write(marker_body,record_bytes*tlm_elts);
    } while (elts_left > 0);
    assert(check_tlm_bytes == tlm_bytes);
  }
  catch (kdu_exception exc) {
    delete[] marker_body;
    throw exc;
  }
  delete[] marker_body;
}

/*****************************************************************************/
/*                         kd_tlm_generator::write_tlms                      */
/*****************************************************************************/

void
  kd_tlm_generator::write_tlms(kdu_compressed_target *tgt,
                               int prev_tiles_written,
                               kdu_long prev_tile_bytes_written)
{
  if (!exists())
    return;

  kdu_long backtrack = tile_data_bytes + prev_tile_bytes_written;
  backtrack += tlm_bytes; // This should take us to the start of the TLM data
  
  // Start by walking over all TLM entries which have already been written
  int z_tlm = 0;
  int tlm_elts_left = 0;
  int elts_left = prev_tiles_written*max_tparts;
  int xfer;
  while (elts_left > 0)
    {
      if (tlm_elts_left == 0)
        { // Open a new marker segment
          assert(z_tlm <= 255);
          tlm_elts_left = (65535-4) / record_bytes;
          backtrack -= record_bytes; // For the TLM header already written
          z_tlm++;
        }
      xfer = (elts_left < tlm_elts_left)?elts_left:tlm_elts_left;
      tlm_elts_left -= xfer;
      elts_left -= xfer;
      backtrack -= record_bytes*xfer;
    }

  // Now we are ready to write the new tile-part lengths
  if (!tgt->start_rewrite(backtrack))
    { KDU_ERROR_DEV(e,11); e <<
        KDU_TXT("Attempting to invoke `kd_tlm_generator::write_final_tlms' "
        "with a compressed data target which does not support "
        "repositioning.");
    }
  elts_left = num_elts - prev_tiles_written*max_tparts;
  if (tlm_elts_left > elts_left)
    tlm_elts_left = elts_left;
  kd_tlm_elt *scan = elts;
  kdu_byte *marker_body = new kdu_byte[65535];
  kd_compressed_output out(tgt);
  try { // Protect against possible memory leaks
    while (elt_ctr > 0)
      { 
        assert(elts_left > 0);
        if (tlm_elts_left == 0)
          { // Open a new marker segment
            assert(z_tlm <= 255);
            tlm_elts_left = (65535-4) / record_bytes;
            if (tlm_elts_left > elts_left)
              tlm_elts_left = elts_left;
            out.put(KDU_TLM);
            out.put((kdu_uint16)(4+record_bytes*tlm_elts_left));
            out.put((kdu_byte) z_tlm);
            out.put((kdu_byte)((tnum_prec<<4)+((tplen_prec==4)?0x40:0)));
            z_tlm++;
          }

        if (tnum_prec == 2)
          out.put(scan->tnum);
        else if (tnum_prec == 1)
          out.put((kdu_byte) scan->tnum);
        if (tplen_prec == 4)
          out.put(scan->length);
        else
          out.put((kdu_uint16)(scan->length));
        scan++;
        elt_ctr--;
        tlm_elts_left--;
        elts_left--;
      }
  }
  catch (kdu_exception exc) {
    delete[] marker_body;
    throw exc;
  }
  delete[] marker_body;
  out.flush();
  tgt->end_rewrite();
}


/* ========================================================================= */
/*                         kd_tpart_pointer_server                           */
/* ========================================================================= */

/*****************************************************************************/
/*            kd_tpart_pointer_server::~kd_tpart_pointer_server              */
/*****************************************************************************/

kd_tpart_pointer_server::~kd_tpart_pointer_server()
{
  kd_tlm_marker_list *mkr;
  while ((mkr=tlm_markers) != NULL)
    {
      tlm_markers = mkr->next;
      delete mkr;
    }

  kd_pointer_group *grp;
  while ((grp=groups) != NULL)
    {
      groups = grp->next;
      delete grp;
    }
}

/*****************************************************************************/
/*                  kd_tpart_pointer_server::add_tlm_marker                  */
/*****************************************************************************/

void
  kd_tpart_pointer_server::add_tlm_marker(kd_marker &copy_source)
{
  translated_tlm_markers = false;
  assert(copy_source.get_code() == KDU_TLM);
  if (copy_source.get_length() < 4)
    { KDU_ERROR(e,12); e <<
        KDU_TXT("TLM marker segments must be at least 6 bytes long!");
    }
  kd_tlm_marker_list *elt = new kd_tlm_marker_list(copy_source);
  kdu_byte *data = elt->get_bytes();
  elt->znum = data[0];

  kd_tlm_marker_list *scan, *prev;

  for (prev=NULL, scan=tlm_markers; scan != NULL; prev=scan, scan=scan->next)
    if (scan->znum > elt->znum)
      break;
  elt->next = scan;
  if (prev == NULL)
    tlm_markers = elt;
  else
    {
      prev->next = elt;
      if (prev->znum == elt->znum)
        { KDU_ERROR(e,13); e <<
            KDU_TXT("Found multiple TLM marker segments with "
            "identical Ztlm indices within the main header!");
        }
    }
}

/*****************************************************************************/
/*                     kd_tpart_pointer_server::add_tpart                    */
/*****************************************************************************/

void
  kd_tpart_pointer_server::add_tpart(kd_tile_ref *tile_ref,
                                     kdu_long sot_address)
{
  kd_tpart_pointer *elt = free_list;
  if (elt == NULL)
    { // Allocate new group of pointer structures and put on free list
      kd_pointer_group *grp = new kd_pointer_group;
      grp->next = groups;
      groups = grp;
      elt = grp->elements;
      for (int g=KD_POINTER_GROUP_SIZE-1; g > 0; g--, elt++)
        elt->next = elt+1;
      elt->next = free_list;
      free_list = elt = grp->elements;
    }
  free_list = elt->next;
  elt->next = NULL;
  elt->address = sot_address;
  if (tile_ref->tpart_head == NULL)
    tile_ref->tpart_head = tile_ref->tpart_tail = elt;
  else if (tile_ref->tpart_tail != NULL)
    tile_ref->tpart_tail = tile_ref->tpart_tail->next = elt;
  else
    assert(0);
}

/*****************************************************************************/
/*                 kd_tpart_pointer_server::translate_markers                */
/*****************************************************************************/

void
  kd_tpart_pointer_server::translate_markers(kdu_long address, int num_tiles,
                                             kd_tile_ref *tile_refs)
{
  assert(groups == NULL); // Illegal to call this function more than once.
  if (tlm_markers == NULL)
    return;

  int tnum, last_tnum = -1;
  kd_tlm_marker_list *tlm;
  bool tlm_info_incomplete = false;
  while (((tlm=tlm_markers) != NULL) && !tlm_info_incomplete)
    {
      kdu_byte *data = tlm->get_bytes() + 1;
      int num_bytes = tlm->get_length() - 1;
      kdu_byte style = *(data++); num_bytes--;
      bool short_lengths = ((style & (1<<6)) == 0);
      bool short_tnums=false, missing_tnums=false;
      int num_fields, field_bytes=0;
      switch ((style>>4) & 3) {
        case 0: missing_tnums=true; short_tnums=false; field_bytes=0; break;
        case 1: missing_tnums=false; short_tnums=true; field_bytes=1; break;
        case 2: missing_tnums=false; short_tnums=false; field_bytes=2; break;
        default: { KDU_ERROR(e,14); e <<
                     KDU_TXT("Illegal Stlm field encountered in TLM "
                     "marker segment!");
                 }
        }
      field_bytes += (short_lengths)?2:4;
      num_fields = num_bytes / field_bytes;
      if ((num_fields < 1) || (num_bytes != (num_fields*field_bytes)))
        { KDU_ERROR(e,15); e <<
            KDU_TXT("Malformed TLM marker segment encountered in "
            "main header.  Segment length is inconsistent with the "
            "number of bytes used to represent pointer info for each "
            "tile-part.");
        }
      while (num_fields--)
        {
          if (missing_tnums)
            tnum = last_tnum + 1; // Tiles must be in order with one part
          else if (short_tnums)
            tnum = *(data++);
          else
            { tnum = *(data++); tnum<<=8; tnum += *(data++); }
          if (tnum >= num_tiles)
            { KDU_ERROR(e,16); e <<
                KDU_TXT("Illegal TLM marker segment data encountered "
                "in main header.  An illegal tile number has been "
                "identified, either explicitly or implicitly (through "
                "the rule that missing tile identifiers are legal "
                "only when tiles appear in order with only one "
                "tile-part each).");
            }
          add_tpart(tile_refs+tnum,address);
          last_tnum = tnum;
          kdu_uint32 length = *(data++); length<<=8; length += *(data++);
          if (!short_lengths)
            { length <<= 8; length += *(data++);
              length <<= 8; length += *(data++); }
          if (length < 14)
            { // Illegal length.
              tlm_info_incomplete = true;
              KDU_WARNING(w,5); w <<
                KDU_TXT("TLM marker segments contain "
                "one or more illegal lengths (< 14 bytes).  Proceeding "
                "with incomplete tile-part length information.");
              break;
            }
          address += (kdu_long) length;
        }
      tlm_markers = tlm->next;
      delete tlm;
    }

  translated_tlm_markers = true;

  // Finally, visit all the tiles, setting their address lists to have a
  // NULL tail so as to assert the fact that all tile-parts have been
  // found.
  for (tnum=0; tnum < num_tiles; tnum++)
    tile_refs[tnum].tpart_tail = NULL;
}


/* ========================================================================= */
/*                        kd_precinct_pointer_server                         */
/* ========================================================================= */

/*****************************************************************************/
/*               kd_precinct_pointer_server::add_plt_marker                  */
/*****************************************************************************/

void
  kd_precinct_pointer_server::add_plt_marker(kd_marker &marker,
                                             kdu_params *cod, kdu_params *poc)
{
  if (buf_server == NULL)
    return;
  kdu_byte *data = marker.get_bytes();
  int length = marker.get_length();
  if ((length < 1) || (data[0] != next_znum))
    { KDU_ERROR(e,17); e <<
        KDU_TXT("PLT marker segments appear out of order within one or more "
                "tile-part headers.  While this is not illegal, it is highly "
                "inadvisable since it prevents immediate condensation of the "
                "pointer information by efficient parsers.  To process this "
                "code-stream, you will have to open it again, with file "
                "seeking disabled.");
    }
  next_znum++; data++; length--;
  if (tpart_bytes_left > 0)
    { KDU_ERROR(e,18); e <<
        KDU_TXT("There appears to be a problem with the PLT marker "
        "segments included in the input code-stream.  The PLT marker "
        "segments encountered so far do not have sufficient length "
        "information to describe the lengths of all packets in the "
        "tile-parts encountered so far.  To process this code-stream, "
        "you will have to open it again, with file seeking disabled.");
    }

  int ival, layer_val=0, order_val=0;
  if (!(cod->get(Clayers,0,0,layer_val) && cod->get(Corder,0,0,order_val)))
    assert(0);
  if (num_layers == 0)
    num_layers = layer_val;
  if ((num_layers != layer_val) ||
      ((num_layers > 1) &&
       ((order_val == Corder_LRCP) || (order_val == Corder_RLCP) ||
        poc->get(Porder,0,0,ival))))
    { // PLT information is unusable.
      disable();
      if (something_served)
        { KDU_ERROR(e,19); e <<
            KDU_TXT("Unexpected change in coding parameters or "
            "packet sequencing detected while parsing packet length "
            "information in PLT marker segments.  While this is not "
            "illegal, it is highly inadvisable.  To process this "
            "code-stream, open it again with file seeking disabled!");
        }
      return;
    }
  if (head == NULL)
    initialize_recording();

  while (length > 0)
    {
      if (packets_left_in_precinct == 0)
        { packets_left_in_precinct = num_layers; precinct_length = 0; }
      kdu_long packet_length = 0;
      kdu_byte byte;
      do {
          if (length == 0)
            { KDU_ERROR(e,20); e <<
                KDU_TXT("Malformed PLT marker segment encountered "
                "in tile-part header.  Segment terminates part of the "
                "way through a multi-byte packet length "
                "specification!");
            }
          byte = *(data++); length--;
          packet_length = (packet_length<<7) + (kdu_long)(byte & 0x7F);
        } while (byte & 0x80);
      precinct_length += packet_length;
      packets_left_in_precinct--;
      if (packets_left_in_precinct == 0)
        { // Record new precinct length using the same byte extension code.
          int shift = 0;
          while ((precinct_length >> shift) >= 128)
            shift += 7;
          for (; shift >= 0; shift -= 7)
            {
              byte = (kdu_byte)(precinct_length >> shift);
              byte &= 0x7F;
              if (shift > 0)
                byte |= 0x80;
              record_byte(byte);
            }
          available_addresses++;
        }
    }
}

/*****************************************************************************/
/*              kd_precinct_pointer_server::start_tpart_body                 */
/*****************************************************************************/

void
  kd_precinct_pointer_server::start_tpart_body(kdu_long start_address,
                                kdu_uint32 length, kdu_params *cod,
                                kdu_params *poc, bool packed_headers,
                                bool final_tpart_with_unknown_length)
{
  next_znum = 0;
  if ((buf_server == NULL) ||
      ((length == 0) && !final_tpart_with_unknown_length))
    return;
  if ((head == NULL) && !something_served)
    { // There is no PLT information available and the tile-part is non-empty
      disable();
      return;
    }

  int ival, layer_val, order_val;
  if (packed_headers ||
      (!cod->get(Clayers,0,0,layer_val)) ||
      (layer_val != num_layers) ||
      ((num_layers > 1) &&
       (poc->get(Porder,0,0,ival) ||
        (!cod->get(Corder,0,0,order_val)) || (order_val == Corder_LRCP) ||
        (order_val == Corder_RLCP))))
    { // Cannot use PLT information
      disable();
      if (something_served)
        { KDU_ERROR(e,21); e <<
            KDU_TXT("Unexpected change in coding parameters or "
            "packet sequencing detected after parsing packet length "
            "information in PLT marker segments.  While this is not "
            "illegal, it is highly inadvisable.  To process this "
            "code-stream, open it again with file seeking disabled!");
        }
    }
  next_address = start_address;
  tpart_bytes_left = length;
  this->final_tpart_with_unknown_length = final_tpart_with_unknown_length;
}

/*****************************************************************************/
/*                 kd_precinct_pointer_server::pop_address                   */
/*****************************************************************************/

kdu_long
  kd_precinct_pointer_server::pop_address()
{
  if (buf_server == NULL)
    return 0;
  if ((available_addresses == 0) &&
      (final_tpart_with_unknown_length || (tpart_bytes_left != 0)))
    {
      assert(something_served);  // Otherwise, must have forgotten to call
                                 // `start_tpart_body'.
      KDU_ERROR(e,22); e <<
        KDU_TXT("Unexpectedly ran out of packet length information "
        "while processing tile-parts.  Most likely cause is that PLT "
        "marker segments are malformed, incomplete, or do not appear "
        "until after the packets whose lengths they describe.  All of "
        "these conditions are violations of the standard!");
    }
  if ((tpart_bytes_left == 0) && !final_tpart_with_unknown_length)
    return -1;

  kdu_long new_length = 0;
  kdu_byte byte;
  do {
      byte = retrieve_byte();
      new_length = (new_length << 7) + (kdu_long)(byte & 0x7F);
    } while (byte & 0x80);
  available_addresses--;
  if (final_tpart_with_unknown_length)
    {
      something_served = true;
      kdu_long result = next_address;
      next_address += new_length;
      return result;
    }

  if (new_length > (kdu_long) tpart_bytes_left)
    { KDU_ERROR(e,23); e <<
        KDU_TXT("Tile-part holds some but not all the packets of "
        "a precinct for which PLT information is being used to extract "
        "precinct addresses for random access.  In particular, the current "
        "tile has its packets sequenced so that all packets of any given "
        "precinct appear consecutively and yet a tile-part boundary has "
        "been inserted between the packets of a precinct.  While this is not "
        "illegal, it indicates very poor judgement in the placement of "
        "tile-part boundaries.  To process this code-stream, you will have "
        "to open it again with file seeking disabled.");
    }
  tpart_bytes_left -= (kdu_uint32) new_length;
  something_served = true;
  kdu_long result = next_address;
  next_address += new_length;
  return result;
}


/* ========================================================================= */
/*                               kd_buf_master                               */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_buf_master::kd_buf_master                        */
/*****************************************************************************/

kd_buf_master::kd_buf_master()
{
  ccb_get_pos.set(0);
  release_list.set(NULL);
  num_release_blocks.set(0);
  num_allocated_blocks.set(0);
  peak_allocated_blocks = 0;
  num_structure_blocks.set(0);
  peak_structure_blocks = 0;
  cache_threshold_bytes = 0;
  cache_threshold_blocks = 0;
  num_buf_servers.set(0);
  num_codestreams.set(1);
  ccb_fill_pos = 0;
  alloc = NULL;
  post_release_list = NULL;
  partial_block_bufs = NULL;
  num_partial_block_bufs = 0;
  total_alloc_blocks = 0;
  num_blocks_per_ccb_entry = 0;
  for (int n=0; n < KD_BUF_MASTER_CCB_SPAN; n++)
    ccb_entries[n].set(NULL);
}

/*****************************************************************************/
/*                       kd_buf_master::~kd_buf_master                       */
/*****************************************************************************/

kd_buf_master::~kd_buf_master()
{
  assert(num_buf_servers.get() == 0);
  assert(num_codestreams.get() == 0);
  assert(num_allocated_blocks.get() == 0);
  kd_code_alloc *tmp;
  while ((tmp=alloc) != NULL)
    { 
      alloc = tmp->next;
      free(tmp);
    }
  mutex.destroy();
}

/*****************************************************************************/
/*                     kd_buf_master::set_multi_threaded                     */
/*****************************************************************************/

void
  kd_buf_master::set_multi_threaded()
{
  if (!mutex.exists())
    mutex.create(4000); // Prefer to spin rather than sleep
}

/*****************************************************************************/
/*                         kd_buf_master::get_blocks                         */
/*****************************************************************************/

kd_code_buffer *
  kd_buf_master::get_blocks(kdu_int32 &num_blocks)
{
  kd_code_buffer *list = NULL;
  if (!mutex.exists())
    { // No need for careful atomic interlocked operations, because
      // `set_multi_threaded' has not yet been called.
      kdu_int32 pos = ccb_get_pos.get();
      ccb_get_pos.set(pos+1);
      pos &= (kdu_int32)(KD_BUF_MASTER_CCB_SPAN-1);
      list = (kd_code_buffer *) ccb_entries[pos].get();
      if (list == NULL)
        { 
          service_lists();
          list = (kd_code_buffer *) ccb_entries[pos].get();
          assert(list != NULL);
        }
      ccb_entries[pos].set(NULL);
      num_blocks = list->get_buf_val32();
      int val = num_allocated_blocks.add_get(num_blocks);
      if (val > peak_allocated_blocks)
        peak_allocated_blocks = val;
    }
  else
    { // Use atomic interlocked operations for mostly lock-less queueing
      kdu_int32 pos = ccb_get_pos.exchange_add(1);
      pos &= (kdu_int32)(KD_BUF_MASTER_CCB_SPAN-1);
      do { 
        list = (kd_code_buffer *) ccb_entries[pos].get();
        if (list == NULL)
          service_lists();
      } while ((list == NULL) ||
               !ccb_entries[pos].compare_and_set(list,NULL));
      num_blocks = list->get_buf_val32();
      int val = num_allocated_blocks.exchange_add(num_blocks) + num_blocks;
      if (val > peak_allocated_blocks)
        peak_allocated_blocks = val;
    }
  return list;
}

/*****************************************************************************/
/*                       kd_buf_master::release_blocks                       */
/*****************************************************************************/

void
  kd_buf_master::release_blocks(kd_code_buffer *head, kd_code_buffer *tail,
                                kdu_int32 num_blocks)
{
  if ((num_blocks <= 0) || (head == NULL) || (tail == NULL))
    return;
  assert(((num_blocks == 1) && (head == tail)) ||
         ((num_blocks > 1) && (tail->get_buf_link() == NULL)));
  if (!mutex.exists())
    { // No need for careful atomic interlocked operations, because
      // `set_multi_threaded' has not yet been called.  In this case, we
      // dump the blocks directly onto the `post_release_list' to simplify
      // the implementation of `service_lists'.
      assert(release_list.get() == NULL);
      kd_code_buffer *old_head = (kd_code_buffer *) post_release_list;
      tail->set_buf_link(old_head);
      post_release_list = head;
      num_release_blocks.get_add(num_blocks);
      num_allocated_blocks.get_add(-num_blocks);
    }
  else
    { // Atomically move the new blocks to the head of the release list
      kd_code_buffer *old_head;
      do { 
        old_head = (kd_code_buffer *) release_list.get();
        tail->set_buf_link(old_head);
      } while (!release_list.compare_and_set(old_head,head));
      num_release_blocks.exchange_add(num_blocks);
      num_allocated_blocks.exchange_add(-num_blocks);
    }
}

/*****************************************************************************/
/*                   kd_buf_master::release_partial_blocks                   */
/*****************************************************************************/

void
  kd_buf_master::release_partial_blocks(kd_code_buffer *head,
                                        kd_code_buffer *tail,
                                        int num_bufs)
{
  if ((num_bufs == 0) || (head == NULL) || (tail == NULL))
    return;
  mutex.lock(); // Does nothing if `set_multi_threaded' has not been called
  num_partial_block_bufs += num_bufs;
  tail->next = partial_block_bufs;
  partial_block_bufs = head;
  for (; num_partial_block_bufs >= KD_CODE_BUFFERS_PER_BLOCK;
       num_partial_block_bufs -= KD_CODE_BUFFERS_PER_BLOCK)
    { // Try packaging buffers up into blocks again; this might be time
      // consuming, but we expect this function to be called only when a
      // codestream is detaching from the `kd_buf_master' object and then
      // we expect only a small number of partial block buffers to be
      // released.
      head = tail = partial_block_bufs;
      for (int n=KD_CODE_BUFFERS_PER_BLOCK-1; n > 0; n--)
        tail = tail->next;
      partial_block_bufs = tail->next;
      tail->next = NULL;
      release_blocks(head,head,1); // This function never locks the mutex
    }
  mutex.unlock(); // Does nothing if `set_multi_threaded' has not been called
}

/*****************************************************************************/
/*                       kd_buf_master::service_lists                        */
/*****************************************************************************/

void
  kd_buf_master::service_lists()
{
  mutex.lock(); // Does nothing if `set_multi_threaded' has not been called
  
  // Start by figuring out how many blocks should be allocated to each empty
  // element encountered within `ccb_entries'.
  int num_free_blocks = num_release_blocks.get(); // Take a single snapshot;
     // note that the actual number of free blocks might increase while this
     // function is executing, but nobody can decrease the number of
     // released blocks from outside the critical section in which this
     // function is running.
  if (num_blocks_per_ccb_entry < 1)
    { 
      int num_clients = num_buf_servers.get();
      if (num_clients > KD_BUF_MASTER_CCB_SPAN)
        num_blocks_per_ccb_entry = num_free_blocks / num_clients;
      else
        num_blocks_per_ccb_entry=num_free_blocks>>KD_BUF_MASTER_LOG2_CCB_SPAN;
      if (num_blocks_per_ccb_entry < 1)
        num_blocks_per_ccb_entry = 1;
    }

  // Now move around the `ccb_entries' array filling in entries
  int delta_num_release_blocks = 0;
  while (ccb_entries[ccb_fill_pos].get() == NULL)
    { 
      kd_code_buffer *list=NULL;
      int list_blocks = 0;
      while (list_blocks < num_blocks_per_ccb_entry)
        { 
          kd_code_buffer *blk;
          if (num_free_blocks > 0)
            { 
              blk = post_release_list;
              if (blk == NULL)
                { // Import the `release_list'
                  blk = post_release_list = (kd_code_buffer *)
                    release_list.exchange(NULL);
                  if (blk == NULL)
                    { // Because `num_free_blocks' was evaluated already and
                      // the `release_list' can only have grown in the
                      // meanwhile, it makes no sense that we would run out of
                      // released blocks now!
                      mutex.unlock();
                      KDU_ERROR_DEV(e,0x04081101); e <<
                        KDU_TXT("A serious problem has occurred during "
                        "memory allocation within the core codestream "
                        "machinery; it seems that you must have accessed "
                        "shared memory from multiple threads without "
                        "passing `kdu_thread_env' references into the "
                        "appropriate functions offered by `kdu_codestream' "
                        "and its descendants.");
                    }
                }
              post_release_list = blk->get_buf_link();
              delta_num_release_blocks--;
              num_free_blocks--;
            }
          else
            { // Allocate memory from the heap, putting all but one of the
              // allocated blocks onto the `post_release_list'.  Note that
              // the `post_release_list' might not actually be empty, because
              // blocks might have been released since we last evaluated
              // `num_free_blocks' and we might have moved some of these
              // extra blocks onto the `post_release_list' in a previous
              // iteration of the loop we are in here.  Nevertheless, we
              // allocate memory based on the snap shot that we took at the
              // start of this function, because this is the typical
              // memory consumption of the application would not be
              // affected by any blocks that may have been released while
              // this function is running.
              kd_code_alloc *new_alloc = (kd_code_alloc *)
                malloc(sizeof(kd_code_alloc *) + KDU_CODE_BUFFER_ALIGN +
                       KD_CODEBUF_BLOCK_BYTES*KD_BUF_MASTER_CCB_SPAN);
              if (new_alloc == NULL)
                throw std::bad_alloc();
              new_alloc->next = alloc;
              alloc = new_alloc;
              int align_off = ((-_addr_to_kdu_int32(new_alloc->block)) &
                               (KDU_CODE_BUFFER_ALIGN-1));
              blk = (kd_code_buffer *)(new_alloc->block+align_off);
              int n, new_blocks = KD_BUF_MASTER_CCB_SPAN;
              delta_num_release_blocks += new_blocks-1;
              num_free_blocks += new_blocks-1;
              for (; new_blocks > 0; new_blocks--)
                { 
                  kd_code_buffer *scan=blk;
                  for (n=KD_CODE_BUFFERS_PER_BLOCK-1; n > 0; n--, scan++)
                    scan->next = scan+1;
                  scan->next=NULL; // Last buffer in block has NULL terminator
                  scan++;
                  if (new_blocks > 1)
                    {
                      blk->set_buf_link(post_release_list);
                      post_release_list = blk;
                      blk = scan;
                    }
                }
            }
          blk->set_buf_link(list);
          list = blk;
          list_blocks++;
        }
      list->set_buf_val32(list_blocks);
      ccb_entries[ccb_fill_pos].set(list); // No need for memory barrier here
      if ((++ccb_fill_pos) == KD_BUF_MASTER_CCB_SPAN)
        ccb_fill_pos = 0;
    }
  if (delta_num_release_blocks != 0)
    num_release_blocks.exchange_add(delta_num_release_blocks);

  mutex.unlock(); // Does nothing if `set_multi_threaded' has not been called
}


/* ========================================================================= */
/*                               kd_buf_server                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_buf_server::attach_and_init                       */
/*****************************************************************************/

void
  kd_buf_server::attach_and_init(kd_buf_master *tgt)
{
  free_blocks = strip_bufs = first_free_buf = last_free_buf = NULL;
  num_free_blocks = num_strip_bufs = num_free_bufs = 0;
  surplus_structure_bytes = 0;
  this->master = tgt;
  tgt->attach_buf_server(this);
}

/*****************************************************************************/
/*                    kd_buf_server::cleanup_and_detach                      */
/*****************************************************************************/

void
  kd_buf_server::cleanup_and_detach()
{
  assert(master != NULL);
  kd_code_buffer *scan;
  while ((scan=strip_bufs) != NULL)
    { // Move all the strip buffers to the free list
      strip_bufs = scan->next;
      num_strip_bufs--;
      release(scan);
    }
  assert(num_strip_bufs == 0);
  if (num_free_bufs > 0)
    {
      master->release_partial_blocks(first_free_buf,last_free_buf,
                                     num_free_bufs);
      first_free_buf = last_free_buf = NULL; num_free_bufs = 0;
    }
  if ((scan = free_blocks) != NULL)
    {
      for (int n=num_free_blocks-1; n > 0; n--)
        scan=scan->get_buf_link();
      assert(scan->get_buf_link() == NULL);
      master->release_blocks(free_blocks,scan,num_free_blocks);
      free_blocks = NULL; num_free_blocks = 0;
    }
  master->detach_buf_server(this);
  master = NULL;
  surplus_structure_bytes = 0;
}

/*****************************************************************************/
/*                      kd_buf_server::get_from_block                        */
/*****************************************************************************/

kd_code_buffer *
  kd_buf_server::get_from_block()
{
  assert((num_strip_bufs == 0) && (num_free_bufs == 0));
  if (num_free_blocks == 0)
    { 
      assert(free_blocks == NULL);
      free_blocks = master->get_blocks(num_free_blocks);
    }
  kd_code_buffer *buf = free_blocks;
  free_blocks = buf->get_buf_link();
  num_free_blocks--;
  strip_bufs = buf->next;
  num_strip_bufs = KD_CODE_BUFFERS_PER_BLOCK-1;
  return buf;
}


/* ========================================================================= */
/*                               kdu_thread_env                              */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdu_thread_env::kdu_thread_env                      */
/*****************************************************************************/

kdu_thread_env::kdu_thread_env()
{
}

/*****************************************************************************/
/*                      kdu_thread_env::~kdu_thread_env                      */
/*****************************************************************************/

kdu_thread_env::~kdu_thread_env()
{
}

/*****************************************************************************/
/*                       kdu_thread_env::new_instance                        */
/*****************************************************************************/

kdu_thread_entity *kdu_thread_env::new_instance()
{ 
  return new kdu_thread_env;
}

/*****************************************************************************/
/*                       kdu_thread_env::cs_terminate                        */
/*****************************************************************************/

bool
  kdu_thread_env::cs_terminate(kdu_codestream codestream,
                               kdu_exception *exc_code)
{
  if (codestream.state == NULL)
    return true; // Nothing to do -- should not happen
  bool result = true;
  kd_cs_thread_context *ctxt = codestream.state->thread_context;
  if (ctxt != NULL)
    { 
      result = terminate(ctxt,false,exc_code);
      if (result)
        { 
          codestream.state->stop_multi_threading(this);
          codestream.state->process_pending_precincts();
        }
    }
  return result;
}


/* ========================================================================= */
/*                            kd_packet_sequencer                            */
/* ========================================================================= */

/*****************************************************************************/
/*                         kd_packet_sequencer::init                         */
/*****************************************************************************/

void
  kd_packet_sequencer::init()
{
  int c, r;
  kd_tile_comp *tc;
  kd_resolution *res;

  assert(tile->initialized);
  max_dwt_levels = 0;
  common_grids = true;
  for (c=0; c < tile->num_components; c++)
    {
      tc = tile->comps + c;
      if (tc->dwt_levels > max_dwt_levels)
        max_dwt_levels = tc->dwt_levels;
      if (!(is_power_2(tc->sub_sampling.x) && is_power_2(tc->sub_sampling.y)))
        common_grids = false;
      for (r=0; r <= tc->dwt_levels; r++)
        {
          kdu_long inc;

          res = tc->resolutions + r;
          inc = res->precinct_partition.size.x;

          inc <<= res->hor_depth;
          inc *= tc->sub_sampling.x;
          if (inc > INT_MAX)
            inc = INT_MAX;
          if ((r == 0) || (inc < tc->grid_inc.x))
            tc->grid_inc.x = (int) inc;

          inc = res->precinct_partition.size.y;
          inc <<= res->vert_depth;
          inc *= tc->sub_sampling.y;
          if (inc > INT_MAX)
            inc = INT_MAX;
          if ((r == 0) || (inc < tc->grid_inc.y))
            tc->grid_inc.y = (int) inc;
        }
      tc->grid_min = tile->dims.pos - tile->coding_origin;
      tc->grid_min.x = tc->grid_inc.x *
        floor_ratio(tc->grid_min.x,tc->grid_inc.x);
      tc->grid_min.y = tc->grid_inc.y *
        floor_ratio(tc->grid_min.y,tc->grid_inc.y);
      tc->grid_min += tile->coding_origin;
    }
  grid_lim = tile->dims.pos + tile->dims.size;
  state_saved = false;
  state.poc = NULL;
  state.next_poc_record = 0;
  next_progression();
}

/*****************************************************************************/
/*                     kd_packet_sequencer::save_state                       */
/*****************************************************************************/

void
  kd_packet_sequencer::save_state()
{
  saved_state = state;
  tile->saved_sequenced_packets = tile->sequenced_relevant_packets;
  for (int c=0; c < tile->num_components; c++)
    {
      kd_tile_comp *tc = tile->comps + c;
      tc->saved_grid_min = tc->grid_min;
      tc->saved_grid_inc = tc->grid_inc;
      for (int r=0; r <= tc->dwt_levels; r++)
        {
          kd_resolution *res = tc->resolutions + r;
          res->saved_current_sequencer_pos = res->current_sequencer_pos;
          int num_precincts = (int) res->precinct_indices.area();
          for (int n=0; n < num_precincts; n++)
            {
              kd_precinct *precinct = res->precinct_refs[n].deref();
              if (precinct != NULL)
                precinct->saved_next_layer_idx = precinct->next_layer_idx;
            }
        }
    }
  state_saved = true;
}

/*****************************************************************************/
/*                   kd_packet_sequencer::restore_state                      */
/*****************************************************************************/

void
  kd_packet_sequencer::restore_state()
{
  assert(state_saved);
  state = saved_state;
  tile->sequenced_relevant_packets = tile->saved_sequenced_packets;
  for (int c=0; c < tile->num_components; c++)
    {
      kd_tile_comp *tc = tile->comps + c;
      tc->grid_min = tc->saved_grid_min;
      tc->grid_inc = tc->saved_grid_inc;
      for (int r=0; r <= tc->dwt_levels; r++)
        {
          kd_resolution *res = tc->resolutions + r;
          res->current_sequencer_pos = res->saved_current_sequencer_pos;
          int num_precincts = (int) res->precinct_indices.area();
          for (int n=0; n < num_precincts; n++)
            {
              kd_precinct *precinct = res->precinct_refs[n].deref();
              if (precinct != NULL)
                precinct->next_layer_idx = precinct->saved_next_layer_idx;
            }
        }
    }
}

/*****************************************************************************/
/*                  kd_packet_sequencer::next_progression                    */
/*****************************************************************************/

bool
  kd_packet_sequencer::next_progression()
{
  if (state.poc == NULL) // Must be initial call.
    {
      state.poc = tile->codestream->siz->access_cluster(POC_params);
      assert(state.poc != NULL);
      state.poc = state.poc->access_relation(tile->t_num,-1,0,true);
      assert(state.poc != NULL);
      if (!state.poc->get(Porder,0,0,state.res_min))
        state.poc = NULL;
    }
  if (state.poc == NULL)
    { // Get information from COD marker segment
      kdu_params *cod =
        tile->codestream->siz->access_cluster(COD_params);
      cod = cod->access_relation(tile->t_num,-1,0,true);
      if (!cod->get(Corder,0,0,state.order))
        assert(0);
      state.comp_min = state.res_min = 0;
      state.layer_lim = tile->num_layers;
      state.comp_lim = tile->num_components;
      state.res_lim = max_dwt_levels+1;
    }
  else
    { // Get information from POC marker segment
      if (!state.poc->get(Porder,state.next_poc_record,0,state.res_min))
        { // Need to access a new POC instance.
          int inst_idx = state.poc->get_instance();
          inst_idx++;
          kdu_params *tmp_poc =
            state.poc->access_relation(tile->t_num,-1,inst_idx,true);
          if ((tmp_poc == NULL) ||
              !tmp_poc->get(Porder,0,0,state.res_min))
            {
              if (tile->codestream->in == NULL)
                { KDU_ERROR(e,24); e <<
                    KDU_TXT("Supplied progression order attributes "
                    "for tile " << tile->t_num << " are insuffient to cover "
                    "all packets for the tile!");
                }
              return false;
            }
          if (inst_idx >= tile->next_tpart)
            return false; // Need to generate a new tile-part first.
          state.poc = tmp_poc;
          state.next_poc_record = 0;
        }
      state.poc->get(Porder,state.next_poc_record,1,state.comp_min);
      state.poc->get(Porder,state.next_poc_record,2,state.layer_lim);
      state.poc->get(Porder,state.next_poc_record,3,state.res_lim);
      state.poc->get(Porder,state.next_poc_record,4,state.comp_lim);
      state.poc->get(Porder,state.next_poc_record,5,state.order);
      if (((state.comp_min != 0) || (state.res_min != 0)) &&
          (state.next_poc_record == 0) && (state.poc->get_instance() == 0) &&
          (tile->codestream->profile == 0))
        { KDU_WARNING(w,8); w <<
            KDU_TXT("Profile violation detected (code-stream is "
            "technically illegal).  In a Profile-0 code-stream, the first "
            "progression specification found in the first POC marker segment "
            "of the main or any tile header may not describe a progression "
            "which starts from resolution or component indices other than 0.");
          tile->codestream->profile = 2;
        }
      state.next_poc_record++;
    }
  if (state.layer_lim > tile->num_layers)
    state.layer_lim = tile->num_layers;
  if (state.comp_lim > tile->num_components)
    state.comp_lim = tile->num_components;
  if (state.res_lim > max_dwt_levels)
    state.res_lim = max_dwt_levels+1;
  state.layer_idx=0;
  state.comp_idx = state.comp_min;
  state.res_idx = state.res_min;
  state.pos.x = state.pos.y=0;

  bool spatial = false;
  if ((state.order == Corder_PCRL) || (state.order == Corder_RPCL))
    {
      spatial = true;
      if (!common_grids)
        { KDU_ERROR(e,25); e <<
            KDU_TXT("Attempting to use a spatially progressive "
            "packet sequence where position order dominates component order. "
            "This is illegal when the component sub-sampling factors are not "
            "exact powers of 2!");
        }
      for (int c=0; c < tile->num_components; c++)
        {
          kd_tile_comp *tc = tile->comps + c;
          if ((c == 0) || (tc->grid_inc.x < state.grid_inc.x))
            {
              state.grid_inc.x = tc->grid_inc.x;
              state.grid_min.x = tc->grid_min.x;
            }
          if ((c == 0) || (tc->grid_inc.y < state.grid_inc.y))
            {
              state.grid_inc.y = tc->grid_inc.y;
              state.grid_min.y = tc->grid_min.y;
            }
        }
      state.grid_loc = state.grid_min;
    }
  else if ((state.order == Corder_CPRL) && (state.comp_idx < state.comp_lim))
    {
      spatial = true;
      state.grid_min = tile->comps[state.comp_idx].grid_min;
      state.grid_inc = tile->comps[state.comp_idx].grid_inc;
      state.grid_loc = state.grid_min;
    }

  if (spatial)
    { // Need to reset the precinct position indices in each resolution.
      for (int c=0; c < tile->num_components; c++)
        {
          kd_tile_comp *tc = tile->comps + c;
          for (int r=0; r <= tc->dwt_levels; r++)
            {
              kd_resolution *res = tc->resolutions + r;
              res->current_sequencer_pos.x = res->current_sequencer_pos.y = 0;
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                   kd_packet_sequencer::next_in_sequence                   */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_sequence(kd_resolution* &res,
                                        kdu_coords &idx)
{
  kd_precinct_ref *result = NULL;

  if (tile->sequenced_relevant_packets == tile->max_relevant_packets)
    return NULL;
  do {
      if (state.order == Corder_LRCP)
        result = next_in_lrcp(res,idx);
      else if (state.order == Corder_RLCP)
        result = next_in_rlcp(res,idx);
      else if (state.order == Corder_RPCL)
        result = next_in_rpcl(res,idx);
      else if (state.order == Corder_PCRL)
        result = next_in_pcrl(res,idx);
      else if (state.order == Corder_CPRL)
        result = next_in_cprl(res,idx);
      else
        assert(0);
    } while ((result == NULL) && next_progression());
  if (result == NULL)
    return NULL;
  kd_precinct *precinct = result->deref();
  if ((tile->codestream->in != NULL) &&
      ((precinct == NULL) || (precinct->next_layer_idx == 0)))
    { // See if we can recover a seek address.
      kdu_long seek_address =
        tile->precinct_pointer_server.get_precinct_address();
      if (seek_address < 0)
        return NULL; // Need a new tile-part.
      if (seek_address > 0)
        {
          if (!result->set_address(res,idx,seek_address))
            result = NULL; // Tile has been destroyed
        }
    }
  return result;
}

/*****************************************************************************/
/*                    kd_packet_sequencer::next_in_lrcp                      */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_lrcp(kd_resolution* &p_res, kdu_coords &p_idx)
{
  for (; state.layer_idx < state.layer_lim;
       state.layer_idx++, state.res_idx=state.res_min)
    for (; state.res_idx < state.res_lim;
         state.res_idx++, state.comp_idx=state.comp_min)
      for (; state.comp_idx < state.comp_lim;
           state.comp_idx++, state.pos.y=0)
        {
          kd_tile_comp *tc = tile->comps + state.comp_idx;
          if (state.res_idx > tc->dwt_levels)
            continue; // Advance to next component.
          kd_resolution *res = tc->resolutions + state.res_idx;
          for (; state.pos.y < res->precinct_indices.size.y;
               state.pos.y++, state.pos.x=0)
            for (; state.pos.x < res->precinct_indices.size.x;
                 state.pos.x++)
              {
                kd_precinct_ref *ref = res->precinct_refs +
                  state.pos.y*res->precinct_indices.size.x + state.pos.x;
                if (!ref->is_desequenced())
                  {
                    kd_precinct *precinct = ref->deref();
                    assert(((precinct != NULL) &&
                            (precinct->next_layer_idx >= state.layer_idx)) ||
                           (state.layer_idx == 0));
                    if ((precinct == NULL) ||
                        (precinct->next_layer_idx == state.layer_idx))
                      { p_res = res; p_idx = state.pos; return ref; }
                  }
              }
        }
  return NULL;
}

/*****************************************************************************/
/*                    kd_packet_sequencer::next_in_rlcp                      */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_rlcp(kd_resolution* &p_res, kdu_coords &p_idx)
{
  for (; state.res_idx < state.res_lim;
       state.res_idx++, state.layer_idx=0)
    for (; state.layer_idx < state.layer_lim;
         state.layer_idx++, state.comp_idx=state.comp_min)
      for (; state.comp_idx < state.comp_lim;
           state.comp_idx++, state.pos.y=0)
        {
          kd_tile_comp *tc = tile->comps + state.comp_idx;
          if (state.res_idx > tc->dwt_levels)
            continue; // Advance to next component.
          kd_resolution *res = tc->resolutions + state.res_idx;
          for (; state.pos.y < res->precinct_indices.size.y;
               state.pos.y++, state.pos.x=0)
            for (; state.pos.x < res->precinct_indices.size.x;
                 state.pos.x++)
              {
                kd_precinct_ref *ref = res->precinct_refs +
                  state.pos.y*res->precinct_indices.size.x + state.pos.x;
                if (!ref->is_desequenced())
                  {
                    kd_precinct *precinct = ref->deref();
                    assert(((precinct != NULL) &&
                            (precinct->next_layer_idx >= state.layer_idx)) ||
                           (state.layer_idx == 0));
                    if ((precinct == NULL) ||
                        (precinct->next_layer_idx == state.layer_idx))
                      { p_res = res; p_idx = state.pos; return ref; }
                  }
              }
        }
  return NULL;
}

/*****************************************************************************/
/*                    kd_packet_sequencer::next_in_rpcl                      */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_rpcl(kd_resolution* &p_res, kdu_coords &p_idx)
{
  if (state.layer_lim <= 0)
    return NULL;
  for (; state.res_idx < state.res_lim;
       state.res_idx++, state.grid_loc.y=state.grid_min.y)
    for (; state.grid_loc.y < grid_lim.y;
         state.grid_loc.y+=state.grid_inc.y, state.grid_loc.x=state.grid_min.x)
      for (; state.grid_loc.x < grid_lim.x;
           state.grid_loc.x+=state.grid_inc.x, state.comp_idx=state.comp_min)
        for (; state.comp_idx < state.comp_lim; state.comp_idx++)
          {
            kd_tile_comp *tc = tile->comps + state.comp_idx;
            if (state.res_idx > tc->dwt_levels)
              continue; // Advance to next component.
            kd_resolution *res = tc->resolutions + state.res_idx;
            state.pos = res->current_sequencer_pos;
            if ((state.pos.x >= res->precinct_indices.size.x) ||
                (state.pos.y >= res->precinct_indices.size.y))
              continue; // No precincts left in this resolution.
            kd_precinct_ref *ref = res->precinct_refs +
              state.pos.x + state.pos.y*res->precinct_indices.size.x;
            kd_precinct *precinct;
            if (ref->is_desequenced() ||
                (((precinct=ref->deref()) != NULL) &&
                 (precinct->next_layer_idx >= state.layer_lim)))
              { // Cannnot sequence this one any further.
                state.pos.x++;
                if (state.pos.x >= res->precinct_indices.size.x)
                  { state.pos.x = 0; state.pos.y++; }
                res->current_sequencer_pos = state.pos;
                continue; // Move sequencing loops ahead.
              }

            int gpos;
            gpos = res->precinct_indices.pos.y + state.pos.y;
            gpos *= res->precinct_partition.size.y;
            gpos <<= res->vert_depth;
            gpos *= tc->sub_sampling.y;
            gpos += tile->coding_origin.y;
            if ((gpos >= state.grid_min.y) && (gpos != state.grid_loc.y))
              continue;
            gpos = res->precinct_indices.pos.x + state.pos.x;
            gpos *= res->precinct_partition.size.x;
            gpos <<= res->hor_depth;
            gpos *= tc->sub_sampling.x;
            gpos += tile->coding_origin.x;
            if ((gpos >= state.grid_min.x) && (gpos != state.grid_loc.x))
              continue;

            p_res = res; p_idx = state.pos;
            return ref;
          }
  return NULL;
}

/*****************************************************************************/
/*                    kd_packet_sequencer::next_in_pcrl                      */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_pcrl(kd_resolution* &p_res, kdu_coords &p_idx)
{
  if (state.layer_lim <= 0)
    return NULL;

  for (; state.grid_loc.y < grid_lim.y;
       state.grid_loc.y+=state.grid_inc.y, state.grid_loc.x=state.grid_min.x)
    for (; state.grid_loc.x < grid_lim.x;
         state.grid_loc.x+=state.grid_inc.x, state.comp_idx=state.comp_min)
      for (; state.comp_idx < state.comp_lim;
           state.comp_idx++, state.res_idx=state.res_min)
        for (; state.res_idx < state.res_lim; state.res_idx++)
          {
            kd_tile_comp *tc = tile->comps + state.comp_idx;
            if (state.res_idx > tc->dwt_levels)
              break; // Advance to next component.
            kd_resolution *res = tc->resolutions + state.res_idx;
            state.pos = res->current_sequencer_pos;
            if ((state.pos.x >= res->precinct_indices.size.x) ||
                (state.pos.y >= res->precinct_indices.size.y))
              continue; // No precincts left in this resolution.
            kd_precinct_ref *ref = res->precinct_refs +
              state.pos.x + state.pos.y*res->precinct_indices.size.x;
            kd_precinct *precinct;
            if (ref->is_desequenced() ||
                (((precinct=ref->deref()) != NULL) &&
                 (precinct->next_layer_idx >= state.layer_lim)))
              { // Cannot sequence this one any further.
                state.pos.x++;
                if (state.pos.x >= res->precinct_indices.size.x)
                  { state.pos.x = 0; state.pos.y++; }
                res->current_sequencer_pos = state.pos;
                continue; // Move sequencing loops ahead.
              }

            int gpos;
            gpos = res->precinct_indices.pos.y + state.pos.y;
            gpos *= res->precinct_partition.size.y;
            gpos <<= res->vert_depth;
            gpos *= tc->sub_sampling.y;
            gpos += tile->coding_origin.y;
            if ((gpos >= state.grid_min.y) && (gpos != state.grid_loc.y))
              continue;
            gpos = res->precinct_indices.pos.x + state.pos.x;
            gpos *= res->precinct_partition.size.x;
            gpos <<= res->hor_depth;
            gpos *= tc->sub_sampling.x;
            gpos += tile->coding_origin.x;
            if ((gpos >= state.grid_min.x) && (gpos != state.grid_loc.x))
              continue;

            p_res = res; p_idx = state.pos;
            return ref;
          }
  return NULL;
}

/*****************************************************************************/
/*                    kd_packet_sequencer::next_in_cprl                      */
/*****************************************************************************/

kd_precinct_ref *
  kd_packet_sequencer::next_in_cprl(kd_resolution* &p_res, kdu_coords &p_idx)
{
  if (state.layer_lim <= 0)
    return NULL;
  while (state.comp_idx < state.comp_lim)
    {
      kd_tile_comp *tc = tile->comps + state.comp_idx;

      for (; state.grid_loc.y < grid_lim.y;
           state.grid_loc.y += state.grid_inc.y,
           state.grid_loc.x = state.grid_min.x)
        for (; state.grid_loc.x < grid_lim.x;
             state.grid_loc.x += state.grid_inc.x, state.res_idx=state.res_min)
          for (; state.res_idx < state.res_lim; state.res_idx++)
            {
              if (state.res_idx > tc->dwt_levels)
                break; // Advance to next position.
              kd_resolution *res = tc->resolutions + state.res_idx;
              state.pos = res->current_sequencer_pos;
              if ((state.pos.x >= res->precinct_indices.size.x) ||
                  (state.pos.y >= res->precinct_indices.size.y))
                continue; // No precincts left in this resolution.
              kd_precinct_ref *ref = res->precinct_refs +
                state.pos.x + state.pos.y*res->precinct_indices.size.x;
              kd_precinct *precinct;
              if (ref->is_desequenced() ||
                  (((precinct=ref->deref()) != NULL) &&
                   (precinct->next_layer_idx >= state.layer_lim)))
                { // Cannot sequence this one any further.
                  state.pos.x++;
                  if (state.pos.x >= res->precinct_indices.size.x)
                    { state.pos.x = 0; state.pos.y++; }
                  res->current_sequencer_pos = state.pos;
                  continue; // Move sequencing loops ahead.
                }

              int gpos;
              gpos = res->precinct_indices.pos.y + state.pos.y;
              gpos *= res->precinct_partition.size.y;
              gpos <<= res->vert_depth;
              gpos *= tc->sub_sampling.y;
              gpos += tile->coding_origin.y;
              if ((gpos >= state.grid_min.y) && (gpos != state.grid_loc.y))
                continue;
              gpos = res->precinct_indices.pos.x + state.pos.x;
              gpos *= res->precinct_partition.size.x;
              gpos <<= res->hor_depth;
              gpos *= tc->sub_sampling.x;
              gpos += tile->coding_origin.x;
              if ((gpos >= state.grid_min.x) && (gpos != state.grid_loc.x))
                continue;

              p_res = res; p_idx = state.pos;
              return ref;
            }

      // Advance component index.

      state.comp_idx++;
      if (state.comp_idx < state.comp_lim)
        { // Install spatial progression parameters for new tile-component.
          tc = tile->comps + state.comp_idx;
          state.grid_min = tc->grid_min;
          state.grid_inc = tc->grid_inc;
          state.grid_loc = state.grid_min;
        }
    }
  return NULL;
}


/* ========================================================================= */
/*                           kd_reslength_checker                            */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_reslength_checker::init                         */
/*****************************************************************************/

bool kd_reslength_checker::init(cod_params *cod, int component_idx,
                                int num_components,
                                kd_reslength_checker component_checkers[])
{
  if (specs != NULL)
    { delete[] specs; specs = NULL; }
  is_active = false;
  num_specs = 0;
  current_layer_idx = prev_layer_idx = -1;
  memset(redirect,0,sizeof(void *)*33);
  memset(max_bytes,0,sizeof(kdu_long)*33);
  if (cod == NULL)
    return false;
    
  int n, val, max_specs=0;
  for (n=0; cod->get(Creslengths,n,0,val,false,false); n++)
    { 
      if (max_specs <= n)
        { 
          max_specs += max_specs + 8;
          kdu_long *new_specs = new kdu_long[max_specs];
          if (specs != NULL)
            { 
              memcpy(new_specs,specs,(size_t)(num_specs*sizeof(kdu_long)));
              delete[] specs;
            }
          specs = new_specs;
        }
      specs[num_specs++] = val;
      is_active = true;
    }
  
  kd_reslength_checker *tgt=this;
  int d=0;
  if ((component_idx >= 0) && (component_checkers != NULL))
    { 
      for (d=0; (d < 33) && cod->get(Cagglengths,d,0,val,false,false); d++)
        { 
          if ((val >= 0) && (val < num_components))
            tgt = component_checkers+val;
          else
            tgt = NULL;
          redirect[d] = tgt;
          is_active = true;
        }
    }
  if (is_active)
    for (; d < 33; d++)
      redirect[d] = tgt;
      
  return is_active;
}

/*****************************************************************************/
/*                      kd_reslength_checker::set_layer                      */
/*****************************************************************************/

void kd_reslength_checker::set_layer(int layer_idx)
{
  if (specs != NULL)
    { 
      if (layer_idx == 0)
        { 
          prev_layer_idx = -1;
          memset(num_bytes,0,33*sizeof(kdu_long));
          memset(prev_layer_bytes,0,33*sizeof(kdu_long));
        }
      else if (layer_idx > current_layer_idx)
        { 
          prev_layer_idx = current_layer_idx;
          memcpy(prev_layer_bytes,num_bytes,33*sizeof(kdu_long));
        }
      else
        { 
          assert(layer_idx > prev_layer_idx);
          memcpy(num_bytes,prev_layer_bytes,33*sizeof(kdu_long));
        }

      if (layer_idx != current_layer_idx)
        { // Parse contents of the `specs' array for this layer
          memset(max_bytes,0,33*sizeof(kdu_long));
          int d, l_idx, n;
          for (l_idx=0, d=0, n=0; n < num_specs; n++, d++)
            {
            if (specs[n] <= 0)
              { l_idx++; d=-1; continue; }
            if (l_idx == layer_idx)
              max_bytes[d] = specs[n];
            else if ((l_idx > layer_idx) &&
                     ((max_bytes[d] == 0) || (max_bytes[d] > specs[n])))
              max_bytes[d] = specs[n];
            }
        }
    }
  current_layer_idx = layer_idx;
}


/* ========================================================================= */
/*                             kd_global_rescomp                             */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_global_rescomp::close_all                       */
/*****************************************************************************/

void
  kd_global_rescomp::close_all()
{
  while ((last_ready=first_ready) != NULL)
    { 
      first_ready = last_ready->next;
      last_ready->next = last_ready->prev = NULL;
      last_ready->ref->close(NULL); // Caller is in the "glock" context
    }
}

/*****************************************************************************/
/*                          kd_comp_info::scale_region                       */
/*****************************************************************************/

kdu_dims
  kd_comp_info::scale_region(const kdu_dims &region, int depth)
{
  kdu_coords min = region.pos;
  kdu_coords lim = min + region.size;
  int shift_x=0, shift_y=0;
  if (depth >= 0)
    { 
      if (depth < 33)
        { shift_x = hor_depth[depth];  shift_y = vert_depth[depth]; }
      else
        shift_x = shift_y = 32;
    }
  if (shift_x < 32)
    { 
      min.x = ceil_ratio(min.x,sub_sampling.x);
      lim.x = ceil_ratio(lim.x,sub_sampling.x);
      min.x = 1 + ((min.x-1)>>shift_x);
      lim.x = 1 + ((lim.x-1)>>shift_x);
    }
  else
    { // Treat as infinite depth
      min.x = 1;
      lim.x = 1;
    }
  if (shift_y < 32)
    { 
      min.y = ceil_ratio(min.y,sub_sampling.y);
      lim.y = ceil_ratio(lim.y,sub_sampling.y);
      min.y = 1 + ((min.y-1)>>shift_y);
      lim.y = 1 + ((lim.y-1)>>shift_y);
    }
  else
    { // Treat as infinite depth
      min.y = 1;
      lim.y = 1;
    }
  kdu_dims result;
  result.pos = min;
  result.size = lim - min;
  return result;
}

/*****************************************************************************/
/*                        kd_global_rescomp::initialize                      */
/*****************************************************************************/

void
  kd_global_rescomp::initialize(kd_codestream *codestream,
                                int depth, int comp_idx)
{
  close_all();
  this->codestream = codestream;
  this->depth = depth; this->comp_idx = comp_idx;
  kd_comp_info *ci = codestream->comp_info + comp_idx;
  this->size = ci->scale_region(codestream->region,depth).size;
  total_area = ((kdu_long) size.x) * ((kdu_long) size.y);
  area_used_by_tiles = area_covered_by_tiles = 0;
  remaining_area = total_area;
  first_ready = last_ready = NULL;
  ready_area = expected_area = attributed_area = 0;
  ready_fraction = reciprocal_fraction = -1.0;
}

/*****************************************************************************/
/*                   kd_global_rescomp::notify_tile_status                   */
/*****************************************************************************/

void
  kd_global_rescomp::notify_tile_status(kdu_dims tile_dims,
                                        bool uses_this_resolution)
{
  kd_comp_info *ci = codestream->comp_info + comp_idx;
  kdu_long tile_area = ci->scale_region(tile_dims,depth).area();
  area_covered_by_tiles += tile_area;
  if (uses_this_resolution)
    area_used_by_tiles += tile_area;
  else
    remaining_area -= tile_area;
  assert((area_covered_by_tiles <= total_area) && (remaining_area >= 0));
  expected_area = -1; attributed_area = 0;
  ready_fraction = reciprocal_fraction = -1.0;
}

/*****************************************************************************/
/*                   kd_global_rescomp::add_ready_precinct                   */
/*****************************************************************************/

void
  kd_global_rescomp::add_ready_precinct(kd_precinct *precinct)
{
  // Add to ready list
  assert((precinct->prev == NULL) && (precinct->next == NULL) &&
         !(precinct->flags & KD_PFLAG_READY));
  precinct->flags |= KD_PFLAG_READY;
  if ((precinct->prev = last_ready) == NULL)
    first_ready = last_ready = precinct;
  else
    last_ready = last_ready->next = precinct;
  
  // Calculate area
  kd_resolution *res = precinct->resolution;
  int p_idx = (int)(precinct->ref - res->precinct_refs);
  kdu_coords idx;
  idx.y = p_idx / res->precinct_indices.size.x;
  idx.x = p_idx - idx.y*res->precinct_indices.size.x;
  idx += res->precinct_indices.pos;
  kdu_dims dims = res->precinct_partition;
  dims.pos.x += dims.size.x*idx.x;
  dims.pos.y += dims.size.y*idx.y;
  dims &= res->node.dims;
  ready_area += dims.area();
  ready_fraction = reciprocal_fraction = -1.0;
}

/*****************************************************************************/
/*                  kd_global_rescomp::close_ready_precinct                  */
/*****************************************************************************/

void
  kd_global_rescomp::close_ready_precinct(kd_precinct *precinct)
{
  assert(precinct->flags & KD_PFLAG_READY);
  precinct->flags &= ~KD_PFLAG_READY;
  // Remove from ready list
  if (precinct->prev == NULL)
    {
      assert(precinct == first_ready);
      first_ready = precinct->next;
    }
  else
    precinct->prev->next = precinct->next;
  if (precinct->next == NULL)
    {
      assert(precinct == last_ready);
      last_ready = precinct->prev;
    }
  else
    precinct->next->prev = precinct->prev;
  precinct->prev = precinct->next = NULL;
  
  // Calculate area
  kd_resolution *res = precinct->resolution;
  int p_idx = (int)(precinct->ref - res->precinct_refs);
  kdu_coords idx;
  idx.y = p_idx / res->precinct_indices.size.x;
  idx.x = p_idx - idx.y*res->precinct_indices.size.x;
  idx += res->precinct_indices.pos;
  kdu_dims dims = res->precinct_partition;
  dims.pos.x += dims.size.x*idx.x;
  dims.pos.y += dims.size.y*idx.y;
  dims &= res->node.dims;
  kdu_long area = dims.area();
  ready_area -= area;
  remaining_area -= area;
  expected_area = -1; // Need to recompute this
  ready_fraction = reciprocal_fraction = -1.0;

  // Close the precinct itself
  precinct->ref->close(NULL); // Caller is in the "glock" context
}


/* ========================================================================= */
/*                           kd_codestream_comment                           */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_codestream_comment::init                        */
/*****************************************************************************/

void
  kd_codestream_comment::init(int length, kdu_byte *data, bool is_text)
{
  assert(!readonly);
  readonly = true;
  this->is_text = is_text;
  this->is_binary = !is_text;
  if (length <= 0)
    {
      this->num_bytes = length = 0;
      if (!is_text)
        return;
    }
  if (max_bytes <= length)
    {
      int new_max_bytes = length+1;
      kdu_byte *new_buf = new kdu_byte[new_max_bytes];
      if (buf != NULL)
        { delete[] buf; buf = NULL; }
      buf = new_buf;
      max_bytes = new_max_bytes;
    }
  memcpy(buf,data,(size_t) length);
  if (is_text && ((length == 0) || (buf[length-1] != 0)))
    buf[length++] = 0;
  num_bytes = length;
}

/*****************************************************************************/
/*                    kd_codestream_comment::write_marker                    */
/*****************************************************************************/

int
  kd_codestream_comment::write_marker(kdu_output *out, int force_length)
{
  readonly = true;
  int pad_bytes = 0;
  int write_bytes = num_bytes;
  if ((write_bytes > 0) && is_text)
    write_bytes--; // Don't write the terminating null character
  if ((force_length <= 0) && (write_bytes > 0xFFFF))
    force_length = 0xFFFF;
  if (force_length > 0)
    {
      force_length -= 6;
      if (force_length < 0)
        force_length = 0;
      if (force_length < write_bytes)
        { // Truncate the output buffer
          write_bytes = num_bytes = force_length;
          if (is_text)
            buf[num_bytes++] = 0;
        }
      else
        pad_bytes = force_length - write_bytes;
    }
  if (out != NULL)
    {
      out->put(KDU_COM);
      out->put((kdu_uint16)(4+write_bytes+pad_bytes));
      out->put((kdu_uint16)((is_text)?1:0)); // Latin values
      out->write(buf,write_bytes);
      for (int i=0; i < pad_bytes; i++)
        out->put((kdu_byte) 0);
    }
  return 6+write_bytes+pad_bytes;
}


/* ========================================================================= */
/*                           kdu_codestream_comment                          */
/* ========================================================================= */

/*****************************************************************************/
/*                     kdu_codestream_comment::get_text                      */
/*****************************************************************************/

const char *
  kdu_codestream_comment::get_text()
{
  if (state == NULL)
    return NULL;
  if ((state->buf == NULL) || !state->is_text)
    return "";
  return (const char *)(state->buf);
}

/*****************************************************************************/
/*                     kdu_codestream_comment::get_data                      */
/*****************************************************************************/

int
  kdu_codestream_comment::get_data(kdu_byte buf[], int offset, int length)
{
  int xfer_bytes = state->num_bytes - offset;
  if (xfer_bytes <= 0)
    return 0;
  if (xfer_bytes > length)
    xfer_bytes = length;
  if (buf != NULL)
    memcpy(buf,state->buf,(size_t) xfer_bytes);
  return xfer_bytes;
}

/*****************************************************************************/
/*                  kdu_codestream_comment::check_readonly                   */
/*****************************************************************************/

bool
  kdu_codestream_comment::check_readonly()
{
  if (state == NULL)
    return true;
  else
    return state->readonly;
}

/*****************************************************************************/
/*                     kdu_codestream_comment::put_data                      */
/*****************************************************************************/

bool
  kdu_codestream_comment::put_data(const kdu_byte data[], int num_bytes)
{
  if ((state == NULL) || state->readonly || state->is_text)
    return false;
  state->is_binary = true;
  int len = state->num_bytes + num_bytes;
  if (len > 65531)
    { KDU_WARNING(w,0x30050901); w <<
      KDU_TXT("Call to `kdu_codestream_comment::put_data' leaves the total "
              "length of the codestream comment greater than 65531, which "
              "is the longest comment that can be represented in a COM "
              "marker segment in the codestream.  Comment is being "
              "truncated.");
      len = 65531;
    }
  if (len > state->max_bytes)
    {
      int new_max_bytes = len + state->max_bytes; 
      if (new_max_bytes > 65531)
        new_max_bytes = 65531;
      assert(new_max_bytes >= len);
      kdu_byte *new_buf = new kdu_byte[new_max_bytes];
      if (state->buf == NULL)
        new_buf[0] = 0;
      else
        {
          memcpy(new_buf,state->buf,(size_t)(state->num_bytes));
          delete[] state->buf;
        }
      state->max_bytes = new_max_bytes;
      state->buf = new_buf;
    }
  if (len > state->num_bytes)
    memcpy(state->buf,data,(size_t)(len-state->num_bytes));
  state->num_bytes = len;  
  return true;
}

/*****************************************************************************/
/*                     kdu_codestream_comment::put_text                      */
/*****************************************************************************/

bool
  kdu_codestream_comment::put_text(const char *string)
{
  if ((state == NULL) || state->readonly || state->is_binary)
    return false;
  state->is_text = true;
  int len = state->num_bytes + (int) strlen(string);
  if (state->num_bytes == 0)
    len++; // Leave space for the terminating null character
  if ((len-1) > 65531)
    { KDU_WARNING(w,0x06110801); w <<
      KDU_TXT("Call to `kdu_codestream_comment::put_text' leaves the total "
              "length of the codestream comment greater than 65531, which "
              "is the longest comment that can be represented in a COM "
              "marker segment in the codestream.  Comment is being "
              "truncated.");
      len = 65531+1;
    }
  if (len > state->max_bytes)
    {
      int new_max_bytes = len + state->max_bytes; 
      if (new_max_bytes > 65532)
        new_max_bytes = 65532;
      assert(new_max_bytes >= len);
      kdu_byte *new_buf = new kdu_byte[new_max_bytes];
      if (state->buf == NULL)
        new_buf[0] = 0;
      else
        {
          memcpy(new_buf,state->buf,(size_t)(state->num_bytes));
          delete[] state->buf;
        }
      state->max_bytes = new_max_bytes;
      state->buf = new_buf;
    }
  if (len > state->num_bytes)
    strncat((char *) state->buf,string,(size_t)(len-state->num_bytes));
  state->num_bytes = len;
  return true;
}


/* ========================================================================= */
/*                                 kd_nlt_info                               */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                      kd_nlt_info::create                           */
/*****************************************************************************/

kd_nlt_info *
  kd_nlt_info::create(kdu_core::kdu_params *params, int t_num, int c_num)
{
  int nl_type=0;
  if ((params == NULL) ||
      ((params = params->access_unique(t_num,c_num)) == NULL) ||
      !params->get(NLType,0,0,nl_type))
    return NULL;
  float gamma[5]={0,0,0,0,0};
  float dmin=0, dmax=0, fval=0;
  int lut_N=0;
  kd_nlt_info *result=NULL;
  if ((nl_type == NLType_GAMMA) &&
           params->get(NLTgamma,0,0,gamma[0]) &&
           params->get(NLTgamma,0,1,gamma[1]) &&
           params->get(NLTgamma,0,2,gamma[2]) &&
           params->get(NLTgamma,0,3,gamma[3]) &&
           params->get(NLTgamma,0,4,gamma[4]))
    { 
      result = new kd_nlt_info;
      result->nlt_type = nl_type;
      result->dmin=0.0f;  result->dmax=1.0f;
      for (int p=0; p < 5; p++)
        result->gamma[p] = gamma[p];
    }
  else if ((nl_type == NLType_LUT) &&
           params->get(NLTlut,0,0,dmin) &&
           params->get(NLTlut,0,1,dmax) &&
           params->get(NLTlut,0,2,lut_N) && (lut_N >= 2) &&
           params->get(NLTdata,lut_N-1,0,fval))
    { 
      result = new kd_nlt_info;
      result->nlt_type = nl_type;
      result->dmin = dmin;  result->dmax = dmax;
      result->num_lut_entries = lut_N;
      result->num_lut_entries = lut_N;
      result->lut = new float[lut_N];
      for (int n=0; n < lut_N; n++)
        params->get(NLTdata,n,0,result->lut[n]);
    }
  else
    { 
      result = new kd_nlt_info;
      if ((nl_type == NLType_SMAG) || (nl_type == NLType_UMAG))
        result->nlt_type = nl_type;
      else
        result->nlt_type = NLType_NONE; // Treat unrecognized types as NONE
      result->dmin=0.0f;  result->dmax=1.0f;
    }
  result->c_num = c_num;
  result->t_num = t_num;
  return result;
}


/* ========================================================================= */
/*                                kd_mct_stage                               */
/* ========================================================================= */

/*****************************************************************************/
/*                         kd_mct_stage::create_stages                       */
/*****************************************************************************/

void
  kd_mct_stage::create_stages(kd_mct_stage * &head,
                              kd_mct_stage * &tail,
                              kdu_params *root, int tnum,
                              int num_input_components,
                              kd_comp_info *input_comp_info,
                              int num_output_components,
                              kd_output_comp_info *output_comp_info,
                              bool is_output_codestream)
{
  head = tail = NULL;

  // Start by accessing the MCO object which describes the transform stages
  kdu_params *cod, *mco, *mco_root = root->access_cluster(MCO_params);
  int num_stages = 0;
  if ((mco_root == NULL) ||
      ((mco = mco_root->access_relation(tnum,-1,0,true)) == NULL) ||
      (!mco->get(Mnum_stages,0,0,num_stages)) || (num_stages == 0) ||
      ((cod = root->access_cluster(COD_params)) == NULL) ||
      ((cod = cod->access_relation(tnum,-1,0,true)) == NULL))
    return;
  int mct_flags = 0;  cod->get(Cmct,0,0,mct_flags);

  // Now walk through the stages
  int stage_num, b, n, m;
  kdu_params *mcc=NULL, *mcc_root = root->access_cluster(MCC_params);
  kdu_params *mct_root = root->access_cluster(MCT_params);
  for (stage_num=0; stage_num < num_stages; stage_num++)
    {
      int mcc_idx, i_val;
      if ((!mco->get(Mstages,stage_num,0,mcc_idx)) || (mcc_root == NULL) ||
          ((mcc = mcc_root->access_relation(tnum,-1,mcc_idx,true)) == NULL) ||
          !(mcc->get(Mstage_collections,0,0,i_val) &&
            mcc->get(Mstage_collections,0,1,i_val)))
        { KDU_ERROR(e,0x08080502); e <<
            KDU_TXT("Unable to access the description of stage ")
            << stage_num <<
            KDU_TXT(" (starting from 0) in the multi-component transform for "
            "some tile (or in the main header).  Either the `Mstages' "
            "attribute contains insufficient entries, or no "
            "`Mstage_collections' attribute exists with the instance "
            "identifier provided by `Mstages'.");
        }
      kd_mct_stage *stage = new kd_mct_stage;
      stage->prev_stage = tail;
      if (tail == NULL)
        head = tail = stage;
      else
        tail = tail->next_stage = stage;

      for (b=1; mcc->get(Mstage_collections,b,0,i_val) &&
                mcc->get(Mstage_collections,b,1,i_val); b++);
      stage->num_blocks = b;
      stage->blocks = new kd_mct_block[stage->num_blocks];
      
      int in_from=0, in_to=-1, in_range_idx=0;
      int out_from=0, out_to=-1, out_range_idx=0;
      for (b=0; b < stage->num_blocks; b++)
        { 
          kd_mct_block *block = stage->blocks + b;
          block->stage = stage;
          mcc->get(Mstage_collections,b,0,block->num_inputs);
          mcc->get(Mstage_collections,b,1,block->num_outputs);
          if ((block->num_inputs < 1) || (block->num_outputs < 1))
            { KDU_ERROR(e,0x08080503); e <<
                KDU_TXT("Multi-component transform blocks must each have "
                "a strictly positive number of inputs and outputs, as "
                "identified by the `Mstage_collections' parameter attribute.");
            }
          if ((block->num_inputs > 16384) || (block->num_outputs > 16384))
            { KDU_ERROR(e,0x06110802); e <<
              KDU_TXT("Multi-component transform block identifies more "
                      "input components or more output components than "
                      "the maximum number of image components in a "
                      "codestream.  Seems like an error!");
            }
          block->input_indices = new int[block->num_inputs];
          block->output_indices = new int[block->num_outputs];
          for (n=0; n < block->num_inputs; n++)
            {
              if (in_to < in_from)
                {
                  if (!(mcc->get(Mstage_inputs,in_range_idx,0,in_from) &&
                        mcc->get(Mstage_inputs,in_range_idx,1,in_to)))
                    { KDU_ERROR(e,0x16110901); e <<
                      KDU_TXT("Insufficient input ranges supplied with"
                              "`Mstage_inputs' codestream attribute to "
                              "accommodate the dimensions supplied by an "
                              "`Mstage_collections' attribute.");
                    }
                  if (!((in_to >= in_from) && (in_from >= 0)))
                    assert(0); // Should be caught by `mcc_params::finalize'
                  in_range_idx++;
                }
              block->input_indices[n] = in_from;
              if (in_from >= stage->num_inputs)
                stage->num_inputs = in_from+1;
              in_from++;
            }
          for (m=0; m < block->num_outputs; m++)
            {
              if (out_to < out_from)
                {
                  if (!(mcc->get(Mstage_outputs,out_range_idx,0,out_from) &&
                        mcc->get(Mstage_outputs,out_range_idx,1,out_to)))
                    { KDU_ERROR(e,0x16110902); e <<
                      KDU_TXT("Insufficient output ranges supplied with"
                              "`Mstage_inputs' codestream attribute to "
                              "accommodate the dimensions supplied by an "
                              "`Mstage_collections' attribute.");
                    }                  
                  if (!((out_to >= out_from) && (out_from >= 0)))
                    assert(0); // Should be caught by `mcc_params::finalize'
                  out_range_idx++;
                }
              block->output_indices[m] = out_from;
              if (out_from >= stage->num_outputs)
                stage->num_outputs = out_from+1;
              out_from++;
            }

          // Set initial null and diag properties, until proven otherwise
          block->is_null_transform = true;
          block->is_diag_transform = (block->num_inputs == block->num_outputs);

          /*
          if (tnum < 0)
            continue; // No need to access the transform coefficients or even
                      // the transform type unless we are in a tile.
          */

          int block_type=0, xform_idx=0, off_idx=0, aux1=0, aux2=0;
          if (!(mcc->get(Mstage_xforms,b,0,block_type) &&
                mcc->get(Mstage_xforms,b,1,xform_idx) &&
                mcc->get(Mstage_xforms,b,2,off_idx) &&
                mcc->get(Mstage_xforms,b,3,aux1) &&
                mcc->get(Mstage_xforms,b,4,aux2)))
            abort(); // Should have been caught by `mcc_params::finalize'

          if ((block_type == Mxform_MATRIX) && (mct_flags == 0) &&
              (aux1 != 0) && !is_output_codestream)
            { // Replace MATRIX with MAT to support reading (and possibly
              // interchange) of legacy codestreams that were constructed
              // with a transposed notion of the matrix employed by
              // reversible decorrelating transforms.  It is dangerous to do
              // this with an output codestream, because this function might
              // be called before the app calls `kdu_params::finalize_all',
              // which is where the `mct_flags' are usually set to the
              // required non-zero value; it is also completely unnecessary
              // to convert MATRIX to MAT for new codestreams that are being
              // generated since they will never use the wrongly transposed
              // matrix interpretation.
              mcc->set(Mstage_xforms,b,0,block_type=Mxform_MAT);
            }

          if ((off_idx != 0) &&
              ((mct_root == NULL) ||
               ((block->offset_params =
                 mct_root->access_relation(tnum,-1,off_idx,true))==NULL) ||
               (!block->offset_params->get(Mvector_size,0,0,i_val)) ||
               (i_val != block->num_outputs)))
            { KDU_ERROR(e,0x08080504); e <<
                KDU_TXT("Unable to access the offset parameters "
                "identified by an `Mstage_xforms' parameter attribute.  "
                "Specifically, there should be an `Mvector_size' "
                "attribute, with instance index ")
                << off_idx <<
                KDU_TXT(" and value ")
                << block->num_outputs <<
                KDU_TXT(", corresponding to the number of output "
                "components produced by the multi-component transform "
                "block in question.");
            }

          if ((block->num_inputs != block->num_outputs) &&
              (((block_type != Mxform_MATRIX) && (block_type != Mxform_MAT)) ||
               (aux1 != 0)))
            { KDU_ERROR(e,0x08080505); e <<
                KDU_TXT("Except for irreversible matrix-based decorrelation "
                "transforms, all multi-component transform blocks described "
                "by the `Mstage_collections' and `Mstage_xforms' parameter "
                "attributes must produce exactly one output component "
                "for each of their input components.");
            }

          if ((block_type == Mxform_MATRIX) || (block_type == Mxform_MAT))
            {
              block->is_reversible = (aux1 != 0);
              int mat_coeffs = block->num_inputs * block->num_outputs;
              if (block->is_reversible)
                mat_coeffs += block->num_inputs;
              if ((xform_idx != 0) &&
                  ((mct_root == NULL) ||
                   ((block->matrix_params =
                     mct_root->access_relation(tnum,-1,xform_idx,true))
                    == NULL) ||
                   (!block->matrix_params->get(Mmatrix_size,0,0,i_val)) ||
                   (i_val != mat_coeffs)))
                { KDU_ERROR(e,0x08080506); e <<
                    KDU_TXT("Unable to access the matrix parameters "
                    "identified by an `Mstage_xforms' parameter attribute.  "
                    "Specifically, there should be an `Mmatrix_size' "
                    "attribute, with instance index ")
                    << xform_idx <<
                    KDU_TXT(" and value ")
                    << mat_coeffs <<
                    KDU_TXT(", corresponding to the total number of "
                    "coefficients required to describe the irreversible "
                    "decorrelation transform or reversible SERM transform "
                    "in question -- note that reversible transforms are "
                    "described by M*(M+1) coefficients, where M is the "
                    "number of input (or output) components.");
                }
              if (block->matrix_params != NULL)
                { 
                  block->is_null_transform = false;
                  if (block->is_reversible)
                    block->is_diag_transform = false; // Not worth looking for
                        // reversible transforms with a diagonal SS equivalent.
                  else
                    { // Try to infer simple properties from the coefficients
                      kdu_params *params = block->matrix_params;
                      if (block->is_diag_transform)
                        { // Try to disprove diagonality
                          int m, n, M=block->num_outputs, N=block->num_inputs;
                          for (m=0; m < M; m++)
                            for (n=0; n < N; n++)
                              { 
                                if (m == n) continue;
                                float val=0.0f;
                                params->get(Mmatrix_coeffs,N*m+n,0,val);
                                if (val != 0.0f)
                                  { 
                                    block->is_diag_transform = false;
                                    M=0; break; // Force early exit
                                  }
                              }
                        }
                  
                      if ((block->num_inputs==3) && (block->num_outputs==3))
                        { // See if we can identify `luma_input_idx' condition
                          assert(block->luma_input_idx < 0);
                          for (int s=0; s < 3; s++)
                            { // Look for a constant non-zero synthesis vector;
                              // if exists, the other analysis vectors must be
                              // 0 mean.
                              float a=0.0f, b=0.0f, c=0.0f;
                              params->get(Mmatrix_coeffs,s,0,a);
                              params->get(Mmatrix_coeffs,3+s,0,b);
                              params->get(Mmatrix_coeffs,6+s,0,c);
                              float low = a*0.99f, high = a*1.01f;
                              if ((low > 0.0f) && (b > low) && (c > low) &&
                                  (b < high) && (c < high))
                                { block->luma_input_idx = s; break; }
                            }
                        }
                    }
                }
              if ((block_type == Mxform_MAT) && block->is_reversible)
                {
                  block->old_mat_params = block->matrix_params;
                  block->matrix_params = NULL;
                }
            }
          else if (block_type == Mxform_DEP)
            {
              block->is_reversible = (aux1 != 0);
              int tri_coeffs = (block->num_outputs*(block->num_outputs-1)) / 2;
              if (block->is_reversible)
                tri_coeffs += block->num_outputs-1;
              if ((xform_idx != 0) &&
                  ((mct_root == NULL) ||
                   ((block->triang_params =
                     mct_root->access_relation(tnum,-1,xform_idx,true))
                    == NULL) ||
                   (!block->triang_params->get(Mtriang_size,0,0,i_val)) ||
                   (i_val != tri_coeffs)))
                { KDU_ERROR(e,0x08080507); e <<
                    KDU_TXT("Unable to access the matrix parameters "
                    "identified by an `Mstage_xforms' parameter attribute.  "
                    "Specifically, there should be an `Mtriang_size' "
                    "attribute, with instance index ")
                    << xform_idx <<
                    KDU_TXT(" and value ")
                    << tri_coeffs <<
                    KDU_TXT(", corresponding to the total number of "
                    "coefficients in the lower triangular matrix which "
                    "describes the dependency transform in question.  For "
                    "irreversible dependency transforms, the matrix should "
                    "not contain any diagonal entries.  For reversible "
                    "dependency transforms, however, all but the first "
                    "diagonal entries should also be included, as "
                    "normalization factors for the integer predictors.");
                }
              if (block->triang_params != NULL)
                block->is_null_transform = block->is_diag_transform = false;
            }
          else if ((block_type == Mxform_DWT) && (aux1 > 0))
            {
              block->is_null_transform = block->is_diag_transform = false;
              block->dwt_num_levels = aux1;
              block->dwt_canvas_origin = aux2;
              int kernel_id = (xform_idx < 2)?xform_idx:Ckernels_ATK;
              kd_create_dwt_description(kernel_id,xform_idx,root,tnum,
                        block->is_reversible,block->dwt_symmetric,
                        block->dwt_symmetric_extension,block->dwt_num_steps,
                        block->dwt_step_info,block->dwt_coefficients);
              kdu_kernels kernels;
              kernels.init(block->dwt_num_steps,block->dwt_step_info,
                           block->dwt_coefficients,block->dwt_symmetric,
                           block->dwt_symmetric_extension,
                           block->is_reversible);
              int low_hlen, high_hlen, nstps;
              kernels.get_impulse_response(KDU_SYNTHESIS_LOW,low_hlen,
                                           &(block->dwt_low_synth_min),
                                           &(block->dwt_low_synth_max));
              kernels.get_impulse_response(KDU_SYNTHESIS_HIGH,high_hlen,
                                           &(block->dwt_high_synth_min),
                                           &(block->dwt_high_synth_max));
              if (block->is_reversible)
                block->dwt_synth_gains[0] = block->dwt_synth_gains[1] = 1.0F;
              else
                {
                  kernels.get_lifting_factors(nstps,block->dwt_synth_gains[0],
                                              block->dwt_synth_gains[1]);
                  assert(nstps == block->dwt_num_steps);
                  block->dwt_synth_gains[0] = 1.0F / block->dwt_synth_gains[0];
                  block->dwt_synth_gains[1] = 0.5F / block->dwt_synth_gains[1];
                }
            }
        }

      // Once we get here, all the transform blocks have been parsed and
      // we know the value of `stage->num_inputs' and
      // `stage->num_outputs'.  Now we can allocate the input and
      // output information arrays and fill in their contents.
      if ((stage->num_inputs > 65535) || (stage->num_outputs > 65535))
        { KDU_ERROR(e,0x06110803); e <<
          KDU_TXT("Multi-component transform stage appears to have a "
                  "ridiculous number of component inputs or outputs -- "
                  "greater than 65535!  Treating this as an error.");
        }
      if ((stage->prev_stage != NULL) &&
          (stage->num_inputs > stage->prev_stage->num_outputs))
        { // It is convenient to augment the previous stage's number of
          // outputs to match the current stage's number of inputs.  Simplifies
          // processing.
          kd_output_comp_info *new_outputs =
            new kd_output_comp_info[stage->num_inputs];
          for (n=0; n < stage->prev_stage->num_outputs; n++)
            new_outputs[n] = stage->prev_stage->output_comp_info[n];
          stage->prev_stage->num_outputs = stage->num_inputs;
          delete[] stage->prev_stage->output_comp_info;
          stage->prev_stage->output_comp_info = new_outputs;
        }

      stage->input_required_indices = new int[stage->num_inputs];
      for (n=0; n < stage->num_inputs; n++)
        stage->input_required_indices[n] = -1; // Change this later
      if ((stage_num == (num_stages-1)) &&
          (stage->num_outputs < num_output_components))
        stage->num_outputs = num_output_components;
      stage->output_comp_info = new kd_output_comp_info[stage->num_outputs];
      for (b=0; b < stage->num_blocks; b++)
        {
          kd_mct_block *block = stage->blocks + b;
          block->inputs_required = new bool[block->num_inputs];
          for (n=0; n < block->num_inputs; n++)
            { 
              block->inputs_required[n] = false; // Change this later
              m = block->input_indices[n];
              stage->input_required_indices[block->input_indices[n]] = m;
                // Above statement lets us check that all inputs are "touched",
                // as required by the standard.
              kd_comp_info *ci;
              kd_mct_stage *ps = stage->prev_stage;
              if (ps == NULL)
                ci = input_comp_info + m;
              else
                { ci = ps->output_comp_info[m].subsampling_ref;
                  if (ps->output_comp_info[m].crg_incompatible)
                    block->crg_incompatible = true;
                }
              if (n==0)
                { block->crg_x = ci->crg_x;  block->crg_y = ci->crg_y; }
              else if (block->crg_incompatible ||
                       (block->crg_x != ci->crg_x) ||
                       (block->crg_y != ci->crg_y))
                { block->crg_x = block->crg_y = 0.0f;
                  block->crg_incompatible = true; }
            }
          kd_comp_info *last_ref = NULL;
             // Use this to provide a `subsampling_ref' value for output
             // components whose input component is not available.
          for (n=0; n < block->num_outputs; n++)
            { 
              kd_output_comp_info *oci =
                stage->output_comp_info + block->output_indices[n];
              if (oci->block != NULL)
                { KDU_ERROR(e,0x09080500); e <<
                    KDU_TXT("Multi-component transform stage contains "
                    "multiple transform blocks which provide different "
                    "definitions for the same stage output component.");
                }
              oci->block = block;
              oci->block_comp_idx = n;
              if (n < block->num_inputs)
                { 
                  kd_mct_stage *ps = stage->prev_stage;
                  m = block->input_indices[n];
                  if (ps == NULL)
                    last_ref = input_comp_info + m;
                  else if (ps->output_comp_info[m].subsampling_ref != NULL)
                    last_ref = ps->output_comp_info[m].subsampling_ref;
                }
              oci->subsampling_ref = last_ref;
              if (!oci->block->is_null_transform)
                oci->crg_incompatible = block->crg_incompatible;
              if (last_ref != NULL)
                for (m=n-1; m >= 0; m--)
                  { 
                    oci = stage->output_comp_info + block->output_indices[m];
                    if (oci->subsampling_ref != NULL)
                      break;
                    oci->subsampling_ref = last_ref;
                  }
            }
        }
      for (n=0; n < stage->num_inputs; n++)
        if (stage->input_required_indices[n] < 0)
          break;
      if ((n < stage->num_inputs) ||
          ((stage->prev_stage == NULL) &&
           (stage->num_inputs != num_input_components)) ||
          ((stage->prev_stage != NULL) &&
           (stage->num_inputs < stage->prev_stage->num_outputs)))
        { KDU_ERROR(e,0x09080501); e <<
            KDU_TXT("Multi-component transform does not satisfy the "
            "constraints imposed by Part 2 of the JPEG2000 standard.  The "
            "first transform stage must touch every codestream image "
            "component (no more and no less), while subsequent stages must "
            "touch every component produced by the previous stage.");
        }
    }
  if (tail->num_outputs > num_output_components)
    { KDU_ERROR(e,0x09080502); e <<
        KDU_TXT("The last stage of the multi-component transform may not "
        "produce more output components than the number specified in the "
        "CBD marker segment (i.e. the `Mcomponents' value).");
    }
  for (n=0; n < tail->num_outputs; n++)
    {
      tail->output_comp_info[n].in_signed =
        tail->output_comp_info[n].out_signed = output_comp_info[n].in_signed;
      tail->output_comp_info[n].in_precision =
        tail->output_comp_info[n].out_precision =
          output_comp_info[n].in_precision;
      if ((tnum < 0) &&
          (tail->output_comp_info[n].subsampling_ref == NULL))
        { KDU_ERROR(e,0x09080503); e <<
            KDU_TXT("Although not strictly illegal, the present "
            "Part-2 codestream contains insufficient information to "
            "determine the dimensions of all image components output by "
            "the multi-component transform, based on main header marker "
            "segments.  The fact that this is not illegal is almost "
            "certainly an oversight in the development of the Part-2 "
            "syntax, but Kakadu cannot work with such codestreams.");
        }
    }

  tail->apply_output_restrictions(output_comp_info);
}

/*****************************************************************************/
/*                 kd_mct_stage::apply_output_restrictions                   */
/*****************************************************************************/

void
  kd_mct_stage::apply_output_restrictions(
                                  kd_output_comp_info *global_comp_info,
                                  int num_comps_of_interest,
                                  const int *comps_of_interest,
                                  bool find_type_flags)
{
  int n, b;

  // Start by identifying the number and order of apparent output components
  num_apparent_outputs = 0;
  if (next_stage == NULL)
    {
      assert(global_comp_info != NULL);
      for (n=0; n < num_outputs; n++)
        { 
          kd_output_comp_info *oci = output_comp_info + n;
          oci->apparent_idx = global_comp_info[n].apparent_idx;
          oci->from_apparent = global_comp_info[n].from_apparent;
          oci->qlim_sq_weight = global_comp_info[n].qlim_sq_weight;
          oci->qlim_type_flags = global_comp_info[n].qlim_type_flags;
          oci->is_of_interest = false;
          if (oci->apparent_idx >= 0)
            {
              num_apparent_outputs++;
              if (num_comps_of_interest == 0)
                oci->is_of_interest = true;
              else if (comps_of_interest == NULL)
                oci->is_of_interest =
                  (oci->apparent_idx < num_comps_of_interest);
            }
        }
      if (comps_of_interest != NULL)
        for (n=0; n < num_comps_of_interest; n++)
          {
            int idx = comps_of_interest[n];
            if ((idx < 0) || (idx >= num_apparent_outputs))
              continue;
            idx = output_comp_info[idx].from_apparent;
            kd_output_comp_info *oci = output_comp_info + idx;
            assert(oci->apparent_idx == comps_of_interest[n]);
            oci->is_of_interest = true;
          }
    }
  else
    { 
      assert((global_comp_info == NULL) &&
             (num_outputs == next_stage->num_inputs));
      for (n=0; n < num_outputs; n++)
        { 
          kd_output_comp_info *oci = output_comp_info + n;
          oci->from_apparent = 0; // So everything at least gets initialized
          if (next_stage->input_required_indices[n] >= 0)
            {
              output_comp_info[num_apparent_outputs].from_apparent = n;
              oci->apparent_idx = num_apparent_outputs;
              num_apparent_outputs++;
              oci->is_of_interest = true;
              assert(next_stage->input_required_indices[n]==oci->apparent_idx);
            }
          else
            {
              oci->apparent_idx = -1; // This one is not apparent
              oci->is_of_interest = false;
            }
          oci->qlim_sq_weight = 1.0f;
          oci->qlim_type_flags = 0; // If `find_type_flags' we will OR flags
                                    // into here from the individual blocks.
        }
      assert(num_apparent_outputs == next_stage->num_required_inputs);
    
      if (find_type_flags)
        { // Scan previous stage blocks to generate this stage's type flags
          for (b=0; b < next_stage->num_blocks; b++)
            { 
              kd_mct_block *block = next_stage->blocks + b;
              if (block->is_null_transform)
                continue;
              if (block->is_diag_transform)
                { 
                  assert(block->num_inputs == block->num_outputs);
                  for (n=0; n < block->num_inputs; n++)
                    { 
                      kd_output_comp_info *oci_src =
                        next_stage->output_comp_info+block->output_indices[n];
                      kd_output_comp_info *oci_tgt =
                        this->output_comp_info+block->input_indices[n];
                      oci_tgt->qlim_type_flags |= oci_src->qlim_type_flags;
                    }
                }
              else if ((block->luma_input_idx >= 0) &&
                       (block->apparent_input_types == -1))
                for (n=0; n < block->num_inputs; n++)
                  output_comp_info[block->input_indices[n]].qlim_type_flags |=
                    (n==block->luma_input_idx)?(1<<30):KDU_INT32_MIN;
              else
                for (n=0; n < block->num_inputs; n++)
                  output_comp_info[block->input_indices[n]].qlim_type_flags |=
                    block->apparent_input_types;
            }
        }
    }

  // Next, scan through the blocks, identifying the required input components
  // and setting the appearance parameters for each block's outputs
  num_required_inputs = 0;
  for (n=0; n < num_inputs; n++)
    input_required_indices[n] = -1; // Initially mark everything not required
  for (b=0; b < num_blocks; b++)
    { 
      kd_mct_block *block = blocks + b;

      block->num_required_inputs = 0;
      for (n=0; n < block->num_inputs; n++)
        block->inputs_required[n] = false;
      block->num_apparent_outputs = 0;
      kdu_int32 type_flags=0;
      for (n=0; n < block->num_outputs; n++)
        { 
          kd_output_comp_info *oci = output_comp_info+block->output_indices[n];
          if (!oci->is_of_interest)
            continue; // This output is not required
          type_flags |= oci->qlim_type_flags;
          assert((oci->apparent_idx >= 0) && (oci->block == block));
          oci->apparent_block_comp_idx = block->num_apparent_outputs;
          block->num_apparent_outputs++;
        }
      if (find_type_flags)
        { // Convert `type_flags' to a value for `block->apparent_input_types'
          block->apparent_input_types = type_flags;
          if (block->luma_input_idx >= 0)
            { 
              type_flags &= 0xC0000007; // Leave 3 LSBs + chroma + chroma-block
              if (type_flags == 7)
                block->apparent_input_types = -1; // Confirms an opponent xform
              else if ((type_flags != 1) && (type_flags != 2) &&
                       (type_flags != 4) && (type_flags != KDU_INT32_MIN))
                block->apparent_input_types = (1<<30); // Chroma-blocking flag
            }
        }
      
      if (block->num_apparent_outputs == 0)
        continue; // This entire transform block is not required.
      if ((block->num_apparent_outputs == block->num_inputs) ||
          ((block->matrix_params != NULL) || (block->old_mat_params != NULL)))
        { // Require all inputs
          for (n=0; n < block->num_inputs; n++)
            {
              block->inputs_required[n] = true;
              block->num_required_inputs++;
            }
        }
      else if (block->triang_params != NULL)
        { // Require a leading set of the inputs
          for (n=0; n < block->num_outputs; n++)
            if (output_comp_info[block->output_indices[n]].is_of_interest)
              block->num_required_inputs = n+1;
          for (n=0; n < block->num_required_inputs; n++)
            block->inputs_required[n] = true;
        }
      else if (block->is_null_transform)
        { // Require only those inputs which correspond to required outputs
          for (n=0; n < block->num_outputs; n++)
            if ((n < block->num_inputs) &&
                output_comp_info[block->output_indices[n]].is_of_interest)
              {
                block->inputs_required[n] = true;                
                block->num_required_inputs++;
              }
        }
      else
        { // We have a DWT transform block, at least some of whose outputs
          // are not required.  Deducing the required set of inputs
          // in this case is a bit more difficult.
          int length = block->num_inputs;
          if (block->scratch == NULL)
            block->scratch = new float[length];
          bool *used = (bool *)(block->scratch);
          bool *expanded_scratch = used + length;
          for (n=0; n < length; n++)
            used[n]=output_comp_info[block->output_indices[n]].is_of_interest;
          int c_min=block->dwt_canvas_origin;
          int lev_min=c_min, lev_lim=c_min+length;
          int unset_inputs=length; // Decreases as we process levels
          for (int lev=0; lev < block->dwt_num_levels; lev++)
            {
              used -= lev_min; // So we can use absolute indices
              bool *expanded = expanded_scratch - lev_min;

              int band_min, band_max, k, c;
              // Map the components used at this level from `used' into the
              // set of components which are required in each of the subbands,
              // storing the result in `expanded'.
              for (n=lev_min; n < lev_lim; n++)
                expanded[n] = false;
              for (n=lev_min; n < lev_lim; n++)
                if (used[n])
                  { // Expand this one into its required subbands
                    // Start with low-pass subband
                    band_min = n - block->dwt_low_synth_max;
                    band_max = n - block->dwt_low_synth_min;
                    band_min += (band_min & 1);  band_max -= (band_max & 1);
                    for (k=band_min; k <= band_max; k+=2)
                      {
                        for (c=k; (c < lev_min) || (c >= lev_lim); )
                          if (c < lev_min)
                            c = 2*lev_min - c;
                          else
                            c = 2*(lev_lim-1) - c;
                        expanded[c] = true;
                      }

                    // Now for the high-pass subband
                    band_min = n - block->dwt_high_synth_max;
                    band_max = n - block->dwt_high_synth_min;
                    band_min += 1-(band_min & 1);  band_max -= 1-(band_max&1);
                    for (k=band_min; k <= band_max; k+=2)
                      {
                        for (c=k; (c < lev_min) || (c >= lev_lim); )
                          if (c < lev_min)
                            c = 2*lev_min - c;
                          else
                            c = 2*(lev_lim-1) - c;
                        expanded[c] = true;
                      }
                  }

              // Transfer high-pass subband entries to the end of the
              // `inputs_required' array.
              band_min = lev_min + 1-(lev_min & 1);
              band_max = (lev_lim-1) - (lev_lim & 1);
              for (n=band_max; n >= band_min; n-=2)
                {
                  assert(unset_inputs > 0);
                  block->inputs_required[--unset_inputs] = expanded[n];
                  block->num_required_inputs += (expanded[n])?1:0;
                }

              // Transfer low-pass subband entries from `expanded' to `used'
              used += lev_min;     // Restore to true array base
              lev_min = (lev_min+1)>>1;
              lev_lim = (lev_lim+1)>>1;
              for (n=lev_min; n < lev_lim; n++)
                used[n-lev_min] = expanded[2*n];
            }

          // At this point we have only to write the low-pass subband flags out
          // to the remaining inputs
          used -= lev_min;
          for (n=lev_lim-1; n >= lev_min; n--)
            {
              assert(unset_inputs > 0);
              block->inputs_required[--unset_inputs] = used[n];
              block->num_required_inputs += (used[n])?1:0;
            }
          assert(unset_inputs == 0);
        }

      // Now transfer the required input flags from `block' to the stage
      for (n=0; n < block->num_inputs; n++)
        if (block->inputs_required[n])
          input_required_indices[block->input_indices[n]] = 0;
                     // We use 0 to temporarily mark the component as required
    }

  // Now enter the correct ordinal values in all non-negative entries of the
  // `input_required_indices' array.
  assert(num_required_inputs == 0);
  for (n=0; n < num_inputs; n++)
    if (input_required_indices[n] >= 0)
      input_required_indices[n] = num_required_inputs++;

  // Recursively invoke the function on the previous stage, if any
  if (prev_stage != NULL)
    prev_stage->apply_output_restrictions(NULL,0,NULL,find_type_flags);
}


/* ========================================================================= */
/*                                kd_mct_block                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_mct_block::analyze_sensitivity                    */
/*****************************************************************************/

void
  kd_mct_block::analyze_sensitivity(int which_input, float input_weight,
                                    int &min_output_idx, int &max_output_idx,
                                    bool restrict_to_interest)
{
  if (is_null_transform)
    {
      int out_idx = output_indices[which_input];
      kd_output_comp_info *oci = stage->output_comp_info+out_idx;
      if (oci->is_of_interest || !restrict_to_interest)
        {
          if (min_output_idx > max_output_idx)
            {
              min_output_idx = max_output_idx = out_idx;
              oci->ss_tmp = 0.0F;
            }
          else
            {
              while (min_output_idx > out_idx)
                stage->output_comp_info[--min_output_idx].ss_tmp = 0.0F;
              while (max_output_idx < out_idx)
                stage->output_comp_info[++max_output_idx].ss_tmp = 0.0F;
            }
          oci->ss_tmp += input_weight;
        }
      return;
    }

  if (ss_models == NULL)
    {
      ss_models = new kd_mct_ss_model[num_inputs];
      if ((matrix_params != NULL) && !is_reversible)
        create_matrix_ss_model();
      else if (matrix_params != NULL)
        create_rxform_ss_model();
      else if (old_mat_params != NULL)
        create_old_rxform_ss_model();
      else if (triang_params != NULL)
        create_dependency_ss_model();
      else if (dwt_num_levels > 0)
        create_dwt_ss_model();
      else
        assert(0);
    }

  kd_mct_ss_model *model = ss_models+which_input;
  for (kdu_int16 n=0; n < model->range_len; n++)
    {
      int out_idx = output_indices[model->range_min + n];
      kd_output_comp_info *oci = stage->output_comp_info+out_idx;
      if (oci->is_of_interest || !restrict_to_interest)
        {
          if (min_output_idx > max_output_idx)
            {
              min_output_idx = max_output_idx = out_idx;
              oci->ss_tmp = 0.0F;
            }
          else
            {
              while (min_output_idx > out_idx)
                stage->output_comp_info[--min_output_idx].ss_tmp = 0.0F;
              while (max_output_idx < out_idx)
                stage->output_comp_info[++max_output_idx].ss_tmp = 0.0F;
            }
          oci->ss_tmp += input_weight * model->ss_vals[n];
        }
    }
}

/*****************************************************************************/
/*                    kd_mct_block::create_matrix_ss_model                   */
/*****************************************************************************/

void
  kd_mct_block::create_matrix_ss_model()
{
  float *coeffs;
  int n, m, num_coeffs = num_inputs * num_outputs;
  assert(!this->is_reversible);
  kd_mct_ss_model *model = ss_models;
  model->ss_handle = coeffs = new float[num_coeffs];
  for (n=0; n < num_inputs; n++, model++, coeffs+=num_outputs)
    {
      model->ss_vals = coeffs;
      model->range_min = 0;
      model->range_len = (kdu_int16) num_outputs;
      for (m=0; m < num_outputs; m++)
        {
          model->ss_vals[m] = 0.0F; // Avoids any possible failure condition
          matrix_params->get(Mmatrix_coeffs,m*num_inputs+n,0,
                             model->ss_vals[m]);
        }
    }
}

/*****************************************************************************/
/*                    kd_mct_block::create_rxform_ss_model                   */
/*****************************************************************************/

void
  kd_mct_block::create_rxform_ss_model()
{
  float *coeffs;
  int n, m, k, N=num_inputs;  assert(N==num_outputs);
  assert(this->is_reversible);

  // Allocate the storage
  int num_coeffs = (N+1)*N;
  kd_mct_ss_model *model = ss_models;
  model->ss_handle = coeffs = new float[num_coeffs];
  for (n=0; n < num_inputs; n++, model++, coeffs+=N)
    {
      model->ss_vals = coeffs;
      model->range_min = 0;
      model->range_len = (kdu_int16) N;
      for (m=0; m < N; m++)
        model->ss_vals[m] =
          (m==n)?1.0F:0.0F; // Start with inputs mapped to outputs
    }

  // Now generate the model coefficients
  int s; // Stands for the "step"
  for (s=0; s <= N; s++)
    {
      m = (N-1) - ((s==N)?0:s); // Index of the channel affected by this step
      float scale = 1.0F;
      matrix_params->get(Mmatrix_coeffs,s*N+m,0,scale);
      scale = 1.0F / scale;
      float sign = 1.0F;
      if (scale < 0.0F)
        {
          assert(s == N);
          sign = -1.0F;
          scale = -scale;
        }

      for (k=0; k < N; k++)
        { // k is the index of the source channel for this step
          if (k == m)
            continue;
          float val = 0.0F;
          matrix_params->get(Mmatrix_coeffs,s*N+k,0,val);
          val *= scale; // This is the update multiplier
          for (model=ss_models, n=0; n < N; n++, model++)
            { // Find the effect of this step update on each input's model
              model->ss_vals[m] -= val*model->ss_vals[k];
              model->ss_vals[m] *= sign;
            }
        }
    }
}

/*****************************************************************************/
/*                   kd_mct_block::create_old_rxform_ss_model                */
/*****************************************************************************/

void
  kd_mct_block::create_old_rxform_ss_model()
{
  float *coeffs;
  int n, m, k, N=num_inputs;  assert(N==num_outputs);
  assert(this->is_reversible);

  // Allocate the storage
  int num_coeffs = (N+1)*N;
  kd_mct_ss_model *model = ss_models;
  model->ss_handle = coeffs = new float[num_coeffs];
  for (n=0; n < num_inputs; n++, model++, coeffs+=N)
    {
      model->ss_vals = coeffs;
      model->range_min = 0;
      model->range_len = (kdu_int16) N;
      for (m=0; m < N; m++)
        model->ss_vals[m] =
          (m==n)?1.0F:0.0F; // Start with inputs mapped to outputs
    }

  // Now generate the model coefficients
  int s; // Stands for the "step"
  for (s=0; s <= N; s++)
    {
      m = (N-1) - ((s==N)?0:s); // Index of the channel affected by this step
      float scale = 1.0F;
      old_mat_params->get(Mmatrix_coeffs,m*(N+1)+s,0,scale);
      scale = 1.0F / scale;
      float sign = 1.0F;
      if (scale < 0.0F)
        {
          assert(s == N);
          sign = -1.0F;
          scale = -scale;
        }

      for (k=0; k < N; k++)
        { // k is the index of the source channel for this step
          if (k == m)
            continue;
          float val = 0.0F;
          old_mat_params->get(Mmatrix_coeffs,k*(N+1)+s,0,val);
          val *= scale; // This is the update multiplier
          for (model=ss_models, n=0; n < N; n++, model++)
            { // Find the effect of this step update on each input's model
              model->ss_vals[m] -= val*model->ss_vals[k];
              model->ss_vals[m] *= sign;
            }
        }
    }
}

/*****************************************************************************/
/*                 kd_mct_block::create_dependency_ss_model                  */
/*****************************************************************************/

void
  kd_mct_block::create_dependency_ss_model()
{
  float *coeffs;
  int n, m, k, N=num_inputs;  assert(N==num_outputs);

  // Allocate the storage
  int num_coeffs = ((N+1)*N)/2;
  kd_mct_ss_model *model = ss_models;
  model->ss_handle = coeffs = new float[num_coeffs];
  for (n=0; n < num_inputs; n++, model++)
    {
      model->range_min = (kdu_int16) n;
      model->range_len = (kdu_int16)(num_outputs-n);
      model->ss_vals = coeffs;
      coeffs += model->range_len;
    }

  // Now generate the model coefficients
  int row_param_start = 0; // Idx of first entry for the row in `triang_params'
  for (m=0; m < num_outputs; m++)
    {
      for (model=ss_models, n=0; n < m; n++, model++)
        model->ss_vals[m-model->range_min] = 0.0F;
      assert(m == (int) model->range_min);
      model->ss_vals[0] = 1.0F;
      if (m == 0)
        continue;

      float scale = 1.0F;
      if (is_reversible)
        { // Find normalization factor
          triang_params->get(Mtriang_coeffs,row_param_start+m,0,scale);
          scale = 1.0F / scale;
        }
      for (k=0; k < m; k++)
        { // Include contribution from output k to output m
          float val = 0.0F;
          triang_params->get(Mtriang_coeffs,row_param_start+k,0,val);
          val *= scale;
          for (model=ss_models, n=0; n <= k; n++, model++)
            model->ss_vals[m-model->range_min] +=
              val * model->ss_vals[k-model->range_min];
        }
      row_param_start += (is_reversible)?(m+1):m;
    }
}

/*****************************************************************************/
/*                      kd_mct_block::create_dwt_ss_model                    */
/*****************************************************************************/

void
  kd_mct_block::create_dwt_ss_model()
{
  int N=num_inputs;  assert(N==num_outputs);
  if (scratch == NULL)
    scratch = new float[N];
  float *buf = scratch - dwt_canvas_origin; // So we can use absolute addresses

  int band_start_idx = 0;
  for (int lev_idx=dwt_num_levels; lev_idx > 0; lev_idx--)
    {
      int b = (lev_idx==dwt_num_levels)?0:1;
      for (; b < 2; b++)
        {
          int band_min = 1+((dwt_canvas_origin-1-(b<<(lev_idx-1)))>>lev_idx);
          int band_lim = 1+((dwt_canvas_origin+N-1-(b<<(lev_idx-1)))>>lev_idx);
          for (int band_pos=band_min; band_pos < band_lim; band_pos++)
            {
              int n, synth_rdx, range_min, range_max;
              range_min = range_max = (band_pos<<lev_idx) + (b<<(lev_idx-1));
              buf[range_min] = 1.0F;
              for (synth_rdx=lev_idx-1; synth_rdx >= 0; synth_rdx--)
                { // Walk up through the synthesis levels
                  int synth_gap=1<<synth_rdx;// Between synthesized samples
                  int synth_min = 1+((dwt_canvas_origin-1) >> synth_rdx);
                  int synth_max = ((dwt_canvas_origin+N-1) >> synth_rdx);
                  synth_min <<= synth_rdx;  synth_max <<= synth_rdx;
                  assert((range_min >= synth_min) && (range_max <= synth_max));

                  if (synth_min == synth_max)
                    { // Unit length sequence synthesized differently
                      assert(range_min == range_max);
                      if ((b == 1) && (synth_rdx == (lev_idx-1)))
                        buf[range_min] *= 0.5F; // Hi-pass subband divided by 2
                      continue;
                    }

                  // Find even and odd coset bounds for current synth level
                  int bound_min[2], bound_max[2];
                  bound_min[0] = synth_min + (synth_min&synth_gap);
                  bound_min[1] = synth_min + synth_gap - (synth_min&synth_gap);
                  bound_max[0] = synth_max - (synth_max&synth_gap);
                  bound_max[1] = synth_max - synth_gap + (synth_max&synth_gap);

                  // Apply pre-lifting scaling of subband samples, initialize
                  // new subband samples in the range and find initial ranges
                  // for the even and odd cosets
                  int rg_min[2], rg_max[2];
                  if (synth_rdx == (lev_idx-1))
                    {
                      assert(range_min == range_max);
                      buf[range_min] *= dwt_synth_gains[b];
                      rg_min[b] = rg_max[b] = range_min;
                      rg_min[1-b] = 0;  rg_max[1-b] = -1; // Empty range
                    }
                  else
                    {
                      assert(!((range_min|range_max) & synth_gap));
                      for (n=range_min; n <= range_max; n+=2*synth_gap)
                        buf[n] *= dwt_synth_gains[0];
                      for (n=range_min+synth_gap; n<range_max; n+=2*synth_gap)
                        buf[n] = 0.0F;
                      rg_min[0] = range_min;  rg_max[0] = range_max;
                      rg_min[1] = 0;  rg_max[1] = -1; // Range starts empty
                    }

                  // Now work through the lifting steps
                  int step_idx;
                  float *step_coeffs = dwt_coefficients;
                  for (step_idx=0; step_idx < dwt_num_steps; step_idx++)
                    step_coeffs += dwt_step_info[step_idx].support_length;
                  for (step_idx=dwt_num_steps-1; step_idx >= 0; step_idx--)
                    {
                      kdu_kernel_step_info *step = dwt_step_info+step_idx;
                      step_coeffs -= step->support_length;
                      int Ns=step->support_min;
                      int Ps=(Ns+step->support_length)-1;
                      Ns <<= (synth_rdx+1);  Ps <<= (synth_rdx+1);
                      if (step_idx & 1)
                        { Ns += synth_gap;  Ps += synth_gap; }
                      else
                        { Ns -= synth_gap;  Ps -= synth_gap; }
                      bool sym_ext = dwt_symmetric_extension;
                      int update_parity = 1-(step_idx&1);
                      int src_min = rg_min[step_idx&1];
                      int src_max = rg_max[step_idx&1];
                      if (src_min > src_max)
                        continue; // Nothing to do here; source coset is empty

                      int update_min = src_min - Ps;
                      int update_max = src_max - Ns;
                      if ((update_min < bound_min[update_parity]) ||
                          ((2*synth_min -
                            (bound_min[update_parity] + Ns)) >= src_min))
                        update_min = bound_min[update_parity];
                      if ((update_max > bound_max[update_parity]) ||
                          ((2*synth_max -
                            (bound_max[update_parity]+Ps)) <= src_max))
                        update_max = bound_max[update_parity];

                      if (rg_min[update_parity] > rg_max[update_parity])
                        { // Range of update samples is currently empty
                          rg_min[update_parity] = update_min;
                          rg_max[update_parity] = update_max;
                        }
                      else if (update_min < rg_min[update_parity])
                        rg_min[update_parity] = update_min;
                      else if (update_max > rg_max[update_parity])
                        rg_max[update_parity] = update_max;
                      while (range_min > update_min)
                        buf[range_min-=synth_gap] = 0.0F;
                      while (range_max < update_max)
                        buf[range_max+=synth_gap] = 0.0F;

                      int edge_min = (sym_ext)?synth_min:bound_min[step_idx&1];
                      int edge_max = (sym_ext)?synth_max:bound_max[step_idx&1];
                      for (n=update_min; n <= update_max; n+=2*synth_gap)
                        {
                          float *cf = step_coeffs;
                          for (int k=Ns; k <= Ps; k+=2*synth_gap, cf++)
                            {
                              int src_idx = n + k;
                              while ((src_idx<edge_min) || (src_idx>edge_max))
                                if (!sym_ext)
                                  src_idx=(src_idx<edge_min)?edge_min:edge_max;
                                else if (src_idx < edge_min)
                                  src_idx = 2*edge_min - src_idx;
                                else
                                  src_idx = 2*edge_max - src_idx;
                              if ((src_idx >= src_min) && (src_idx <= src_max))
                                buf[n] -= buf[src_idx] * *cf;
                            }
                        }
                    }
                  assert(step_coeffs == dwt_coefficients);
                }

              // Finally transfer the data
              kd_mct_ss_model *model =
                ss_models + band_start_idx + (band_pos-band_min);
              model->range_min = (kdu_int16)(range_min-dwt_canvas_origin);
              model->range_len = (kdu_int16)(range_max+1-range_min);
              model->ss_handle = model->ss_vals = new float[model->range_len];
              for (n=range_min; n <= range_max; n++)
                model->ss_vals[n-range_min] = buf[n];

              // See if we can just replicate this model into subsequent
              // locations within the same subband -- this should be possible
              // so long as the model and its replica do not intersect the
              // boundaries.
              int left_gap = model->range_min;
              int right_gap = N - model->range_min - model->range_len;
              while ((left_gap > 0) && (right_gap > (1<<lev_idx)) &&
                     (band_pos < band_lim))
                {
                  left_gap += 1<<lev_idx;
                  right_gap -= 1<<lev_idx;
                  model[1].ss_vals = model[0].ss_vals;
                  model[1].range_min = (kdu_int16) left_gap;
                  model[1].range_len = model[0].range_len;
                  band_pos++;
                  model++;
                }
            }
          band_start_idx += band_lim-band_min;
        }
    }
  assert(band_start_idx == num_inputs);
}


/* ========================================================================= */
/*                            kd_cs_background_job                           */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_cs_background_job::process                       */
/*****************************************************************************/

void
  kd_cs_background_job::process(kd_cs_background_job *job, kdu_thread_env *env)
{
  kd_cs_thread_context *context = job->context;
  if (job->job_type == 0)
    context->do_glock_processing(env);
  else if (job->job_type == 1)
    context->do_plock_processing(env);
  else
    assert(0);
}


/* ========================================================================= */
/*                            kd_cs_thread_context                           */
/* ========================================================================= */

/*****************************************************************************/
/*                 kd_cs_thread_context::manage_buf_servers                  */
/*****************************************************************************/

int
  kd_cs_thread_context::manage_buf_servers(kd_buf_server buf_servers[])
{
  mutex.lock();
  this->thread_buf_servers = buf_servers;
  int num_threads_snapshot = this->cur_threads;
  mutex.unlock();
  if (buf_servers != NULL)
    for (int t=1; t <= num_threads_snapshot; t++)
      buf_servers[t].attach_and_init(buf_servers);
  return num_threads_snapshot;
}

/*****************************************************************************/
/*              kd_cs_thread_context::manage_compressed_stats                */
/*****************************************************************************/

int
  kd_cs_thread_context::manage_compressed_stats(kd_compressed_stats *stats[])
{
  mutex.lock();
  this->thread_stats = stats;
  int num_threads_snapshot = this->cur_threads;
  mutex.unlock();
  if (stats != NULL)
    { 
      assert(stats[0] != NULL);
      for (int t=1; t <= num_threads_snapshot; t++)
        { 
          assert(stats[t] == NULL);
          stats[t] = new kd_compressed_stats(stats[0]);
          stats[t-1]->set_next(stats[t]);
        }
    }
  return num_threads_snapshot;
}

/*****************************************************************************/
/*                 kd_cs_thread_context::num_threads_changed                 */
/*****************************************************************************/

void
  kd_cs_thread_context::num_threads_changed(int num_threads)
{
  mutex.lock();
  int old_num_threads = cur_threads;
  if (num_threads > cur_threads)
    cur_threads = num_threads;
  mutex.unlock();
  for (int t=old_num_threads+1; t <= num_threads; t++)
    { 
      if (thread_buf_servers != NULL)
        thread_buf_servers[t].attach_and_init(thread_buf_servers);
      if (thread_stats != NULL)
        { 
          assert((thread_stats[t] == NULL) && (thread_stats[0] != NULL));
          thread_stats[t] = new kd_compressed_stats(thread_stats[0]);
          thread_stats[t-1]->set_next(thread_stats[t]);
        }
    }
}

/*****************************************************************************/
/*            kd_cs_thread_context::schedule_bkgnd_processing                */
/*****************************************************************************/

void
  kd_cs_thread_context::schedule_bkgnd_processing(kdu_int32 flags,
                                                  kdu_thread_entity *caller)
{
  if (flags & KD_BKGND_GLOCK_SERVICES)
    flags |= KD_BKGND_FLAG_GLOCK_ACTIVE;
  if (flags & KD_BKGND_PLOCK_SERVICES)
    flags |= KD_BKGND_FLAG_PLOCK_ACTIVE;
  kdu_int32 new_val, old_val;
  do { // Enter compare-and-set loop
    old_val = new_val = job_state.get();
    if (old_val & KD_BKGND_FLAG_ALL_DONE)
      { KDU_ERROR_DEV(e,0x16091101); e <<
        KDU_TXT("Something is seriously wrong; a background codestream "
                "processing job is being scheduled, when the background "
                "processing queue for this codestream already identifies "
                "itself as `all_done'.  It looks like you must have issued a "
                "global (not queue specific) call to `kdu_thread_entity::join'"
                " or `kdu_thread_entity::terminate' and then gone back to "
                "do multi-threaded processing work with the codestream "
                "(perhaps following a `kdu_codestream::restart' call).  "
                "This is OK so long as you first call "
                "`kdu_thread_env::cs_terminate'.");
      }
    if (!(old_val & (KD_BKGND_FLAG_TERMINATE | KD_BKGND_FLAG_CLEAR_WAITING)))
      new_val = old_val | flags;
  } while (!job_state.compare_and_set(old_val,new_val));
  if ((new_val ^ old_val) & KD_BKGND_FLAG_GLOCK_ACTIVE)
    schedule_job(&glock_job,caller); // `glock_job' was not previously active
  if ((new_val ^ old_val) & KD_BKGND_FLAG_PLOCK_ACTIVE)
    schedule_job(&plock_job,caller); // `plock_job' was not previously active
}

/*****************************************************************************/
/*                kd_cs_thread_context::append_to_res_queue                  */
/*****************************************************************************/

void
  kd_cs_thread_context::append_to_res_queue(kd_resolution *res)
{
  res->bkgnd_next.set(NULL);
  kd_resolution *t_old, *n_old;
  t_old = (kd_resolution *) res_tail.exchange(res);
    // Above statement reserves an exclusive right to append `res' to the
    // end of `t_old' (or to make `res' the head of the queue if `t_old' was
    // NULL) without interruption.
  if (t_old == NULL)
    { // The queue must previously have been empty; no other thread can see
      // this condition, at least until after we have set a non-NULL value
      // into `res_head'.
      assert(res_head.get() == NULL);
      res_head.set(res);
    }
  else
    { 
      n_old = (kd_resolution *) t_old->bkgnd_next.exchange(res);
      if (n_old == KD_BKGND_RES_TAIL_INTERRUPTED)
        { // Special "INTERRUPTED" marker was left behind by
          // `do_glock_processing' or `do_plock_processing' if it thought that
          // `t_old' was at the end of the queue, but then found that
          // `res_tail' had been changed (by us).  In that case, `t_old' would
          // have been abandoned by the background processing job, so we
          // should put it back here as the head of the queue.
          assert(res_head.get() == NULL);
          res_head.set(t_old);
        }
    }
}

/*****************************************************************************/
/*               kd_cs_thread_context::append_to_tile_queue                  */
/*****************************************************************************/

void
  kd_cs_thread_context::append_to_tile_queue(kd_tile_ref *tref)
{
  tref->bkgnd_next.set(NULL);
  kd_tile_ref *t_old, *n_old;
  t_old = (kd_tile_ref *) tref_tail.exchange(tref);
    // Above statement reserves an exclusive right to append `tref' to the
    // end of `t_old' (or to make `tref' the head of the queue if `t_old' was
    // NULL) without interruption.
  if (t_old == NULL)
    { // The queue must previously have been empty; no other thread can see
      // this condition, at least until after we have set a non-NULL value
      // into `res_head'.
      assert(tref_head.get() == NULL);
      tref_head.set(tref);
    }
  else
    { 
      n_old = (kd_tile_ref *) t_old->bkgnd_next.exchange(tref);
      if (n_old == KD_BKGND_TREF_TAIL_INTERRUPTED)
        { // Special "INTERRUPTED" marker was left behind by
          // `do_glock_processing' if it thought that `t_old' was at the end
          // of the queue, but then found that `tref_tail' had been changed
          // (by us).  In that case, `t_old' would have been abandoned by the
          // background processing job, so we should put it back as the head
          // of the queue.
          assert(tref_head.get() == NULL);
          tref_head.set(t_old);
        }
    }
}

/*****************************************************************************/
/*                   kd_cs_thread_context::clear_queues                      */
/*****************************************************************************/

void
  kd_cs_thread_context::clear_queues(kdu_thread_env *env)
{
  if ((cs != NULL) && (env != NULL))
    { 
      assert(this->clear_wakeup == NULL);
      clear_wakeup = env->get_condition();
      kdu_int32 new_val, old_val;
      do { // Enter compare-and-set loop
        old_val = job_state.get();
        new_val = old_val & ~(KD_BKGND_GLOCK_SERVICES |
                              KD_BKGND_PLOCK_SERVICES);
        if (new_val & KD_BKGND_PROCESSING_MASK)
          new_val |= KD_BKGND_FLAG_CLEAR_WAITING;
      } while (!job_state.compare_and_set(old_val,new_val));
      assert(!(old_val & KD_BKGND_FLAG_CLEAR_WAITING));
      if ((new_val ^ old_val) & KD_BKGND_FLAG_CLEAR_WAITING)
        env->wait_for_condition();
      clear_wakeup = NULL;
      
      cs->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  kd_resolution *res_nxt, *res = (kd_resolution *) res_head.get();
  res_head.set(NULL);
  res_tail.set(NULL);
  kd_tile_ref *tref_nxt, *tref = (kd_tile_ref *) tref_head.get();
  tref_head.set(NULL);
  tref_tail.set(NULL);
  for (; res != NULL; res=res_nxt)
    { 
      res_nxt = (kd_resolution *) res->bkgnd_next.get();
      res->reset_background_processing();
    }
  for (; tref != NULL; tref=tref_nxt)
    { 
      tref_nxt = (kd_tile_ref *) tref->bkgnd_next.get();
      kdu_int32 flags = tref->flags.get();
      if (flags & KD_TREF_FLAGS_CLOSE_PENDING)
        { 
          kd_tile *tp = tref->tile;
          if ((tp != NULL) && (tp != KD_EXPIRED_TILE))
            tp->close(NULL); // No need for atomic flags manipulation
          flags = tref->flags.get(); // Catch any updates
        }
      flags &= ~(KD_TREF_FLAGS_ENQUEUED |
                 KD_TREF_FLAGS_OPEN_PENDING |
                 KD_TREF_FLAGS_CLOSE_PENDING);
      tref->flags.set(flags);
    }
  if ((cs != NULL) && (env != NULL))
    cs->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                kd_cs_thread_context::request_termination                  */
/*****************************************************************************/

void
  kd_cs_thread_context::request_termination(kdu_thread_entity *caller)
{
  kdu_int32 new_val, old_val;
  old_val = new_val = job_state.get();
  do { // Enter compare-and-set loop
    old_val = new_val = job_state.get();
    if (old_val & (KD_BKGND_FLAG_TERMINATE | KD_BKGND_FLAG_ALL_DONE))
      break;
    new_val |= KD_BKGND_FLAG_TERMINATE;
    if (!(old_val & KD_BKGND_ACTIVE_MASK))
      new_val |= KD_BKGND_FLAG_ALL_DONE; // We will be calling `all_done'
  } while (!job_state.compare_and_set(old_val,new_val));
  if ((new_val ^ old_val) & KD_BKGND_FLAG_ALL_DONE)
    all_done(caller); // We have just set the "all_done" flag
}

/*****************************************************************************/
/*               kd_cs_thread_context::do_glock_processing                   */
/*****************************************************************************/

void
  kd_cs_thread_context::do_glock_processing(kdu_thread_env *env)
{
  kdu_int32 old_services=0; // Allows lower priority services to be remembered
                            // while picking up new requests for higher
                            // priority services.
  while (true)
    { // Outer job loop
      kdu_int32 old_val, new_val, services;
      do { // Enter compare-and-set loop
        new_val = old_val = job_state.get();
        services = old_val & KD_BKGND_GLOCK_SERVICES;
        new_val &= ~(KD_BKGND_FLAG_GLOCK_ACTIVE |
                     KD_BKGND_FLAG_GLOCK_PROCESSING |
                     KD_BKGND_GLOCK_SERVICES); // Clear all glock related flags
        services |= old_services; // It is very important to do this here
        if (new_val & (KD_BKGND_FLAG_TERMINATE | KD_BKGND_FLAG_CLEAR_WAITING))
          { // Cancel all services for this job and become non-active
            services = 0;
            if ((new_val & KD_BKGND_FLAG_TERMINATE) &&
                !(new_val & KD_BKGND_ACTIVE_MASK))
              new_val |= KD_BKGND_FLAG_ALL_DONE;
            if (!(new_val & KD_BKGND_PROCESSING_MASK))
              new_val &= ~KD_BKGND_FLAG_CLEAR_WAITING; // The wait is over
          }
        else if (services != 0)
          new_val |= (KD_BKGND_FLAG_GLOCK_ACTIVE |
                      KD_BKGND_FLAG_GLOCK_PROCESSING);
      } while (!job_state.compare_and_set(old_val,new_val));
      if (!(new_val & KD_BKGND_FLAG_GLOCK_ACTIVE))
        { // End of current job instance
          assert(!(new_val & KD_BKGND_FLAG_GLOCK_PROCESSING));
          if ((new_val ^ old_val) & KD_BKGND_FLAG_ALL_DONE)
            all_done(env); // We have just set the `all_done' flag
          if ((new_val ^ old_val) & KD_BKGND_FLAG_CLEAR_WAITING)
            { // We can now wake the thread that is waiting in `clear_queues'
              env->signal_condition(clear_wakeup); // Safe even if this is NULL
            }
          return;
        }
      assert((new_val & KD_BKGND_FLAG_GLOCK_PROCESSING) &&
             !(new_val & KD_BKGND_FLAG_ALL_DONE));

      old_services = 0; // We have already transferred these to `services'
      if (services & KD_BKGND_FLAG_RES_PARSE)
        { 
          services -= KD_BKGND_FLAG_RES_PARSE;
          kd_resolution *res, *nxt;
          while ((res = (kd_resolution *) res_head.get()) != NULL)
            { 
              nxt = (kd_resolution *) res->bkgnd_next.get();
              res_head.set(nxt); // Only one thread can pull resolutions from
                                 // the queue, so we don't need to worry about
                                 // contention here.
              if (nxt == NULL)
                { // `res' appears to have been at the tail of the queue, but
                  // it is possible that another thread is in the process
                  // (or about to be in the process) of appending to the queue,
                  // in which case it will try to modify the value of
                  // `res->bkgnd_next'.
                  if (!res->bkgnd_next.compare_and_set(NULL,
                                                KD_BKGND_RES_TAIL_INTERRUPTED))
                    { // There was indeed an appending operation in progress
                      // and it added another `kd_resolution' object onto the
                      // end of the queue before the above atomic operation was
                      // committed.  We are the only thread that can set a
                      // non-NULL `res_head' value at this point.  The
                      // `res_tail' variable will remain non-NULL at least
                      // until then, so nobody else can touch `res_head'.
                      assert(res_tail.get() != NULL);
                      res_head.set(res->bkgnd_next.get());
                    }
                  else if (!res_tail.compare_and_set(res,NULL))
                    { // There is or was another thread in the process of
                      // appending to the tail of the queue.  This appending
                      // thread has not yet written to `res->bkgnd_next', at
                      // least prior to the point at which the
                      // `res->bkgnd_next.compare_and_set' atomic operation was
                      // committed, so when it does write to `res->bkgnd_next',
                      // it will see the special "INTERRUPTED" marker.  We
                      // should not service `res' at this point, because
                      // `res->bkgnd_next' is still vulnerable to
                      // manipulation.  The safest thing is to finish our
                      // background processing here, leaving `res_head' NULL;
                      // the appending thread will set `res_head' back to `res'
                      // again once it sees the "INTERRUPTED" marker, and we
                      // can be sure that no other thread will do this.  After
                      // that, the appending thread will schedule the job
                      // again or cause the current instance of it to loop
                      // back again.
                      break;
                    }
                }
              assert(cs->in != NULL); // res-parse is for input codestreams
              res->do_background_processing(env);
              env->advance_work_domains(); // Parsing operations may be costly,
                // so make sure we are not holding up the removal of completed
                // domain sequences.
            }
        }
      
      if (services & KD_BKGND_FLAG_TILE_OPENCLOSE)
        { 
          bool lock_acquired = false;
          kd_tile_ref *tref, *nxt;
          while ((tref = (kd_tile_ref *) tref_head.get()) != NULL)
            { 
              nxt = (kd_tile_ref *) tref->bkgnd_next.get();
              tref_head.set(nxt); // Only one thread can pull tile-refs from
                                  // the queue, so we don't need to worry about
                                  // contention here.
              if (nxt == NULL)
                { // `tref' appears to have been at the tail of the queue, but
                  // it is possible that another thread is in the process
                  // (or about to be in the process) of appending to the queue,
                  // in which case it will try to modify the value of
                  // `tref->bkgnd_next'.
                  if (!tref->bkgnd_next.compare_and_set(NULL,
                                               KD_BKGND_TREF_TAIL_INTERRUPTED))
                    { // There was indeed an appending operation in progress
                      // and it added another `kd_tile_ref' object onto the
                      // end of the queue before the above atomic operation was
                      // committed.  We are the only thread that can set a
                      // non-NULL `tref_head' value at this point.  The
                      // `tref_tail' variable will remain non-NULL at least
                      // until then, so nobody else can touch `tref_head'.
                      assert(tref_tail.get() != NULL);
                      tref_head.set(tref->bkgnd_next.get());
                    }
                  else if (!tref_tail.compare_and_set(tref,NULL))
                    { // There is or was another thread in the process of
                      // appending to the tail of the queue.  This appending
                      // thread has not yet written to `tref->bkgnd_next', at
                      // least prior to the point at which the
                      // `tref->bkgnd_next.compare_and_set' atomic operation
                      // was committed, so when it does write to
                      // `tref->bkgnd_next', it will see the special
                      // "INTERRUPTED" marker.  We should not service `tref'
                      // at this point, because `tref->bkgnd_next' is still
                      // vulnerable to manipulation.  The safest thing is to
                      // finish our background processing here, leaving
                      // `tref_head' NULL; the appending thread will set
                      // `tref_head' back to `tref' again once it sees the
                      // "INTERRUPTED" marker, and we can be sure that no
                      // other thread will do this.  After that, the appending
                      // thread will schedule the job again or cause the
                      // current instance of it to loop back again.
                      tref = NULL;
                    }
                }
              if (tref != NULL)
                { 
                  kdu_int32 old_flags, new_flags;
                  do { // Enter compare-and-set loop
                    new_flags = old_flags = tref->flags.get();
                    new_flags &= ~KD_TREF_FLAGS_ENQUEUED; // Leave pending
                                      // open/close flags alone for the moment
                  } while (!tref->flags.compare_and_set(old_flags,new_flags));
                  if (new_flags & (KD_TREF_FLAGS_CLOSE_PENDING |
                                   KD_TREF_FLAGS_OPEN_PENDING))
                    { // Otherwise, no need to acquire mutex
                      if (!lock_acquired)
                        { 
                          cs->acquire_lock(KD_THREADLOCK_GENERAL,env);
                          new_flags = tref->flags.get(); // In case conditions
                                      // were cleared by direct open/close ops
                          lock_acquired = true;
                        }
                      kd_tile *tp = tref->tile;
                      if (new_flags & KD_TREF_FLAGS_CLOSE_PENDING)
                        
                        { 
                          if ((tp != NULL) && (tp != KD_EXPIRED_TILE))
                            tp = tp->close(env); // Modifies flags
                          else
                            assert(0);
                        }
                      if (new_flags & KD_TREF_FLAGS_OPEN_PENDING)
                        { 
                          if (tp == NULL)
                            { // Need to create the tile
                              kdu_coords tile_idx;
                              size_t off = tref - cs->tile_refs;
                              size_t stride = cs->tile_indices.size.x;
                              tile_idx.y = (int)(off / stride);
                              tile_idx.x = (int)(off-tile_idx.y*stride);
                              tile_idx += cs->tile_indices.pos;
                              tp = cs->create_tile(tile_idx);
                            }
                          else if ((tp != KD_EXPIRED_TILE) && tp->needs_reinit)
                            { // We must have restarted the code-stream,
                              // since the tile was created
                              assert(cs->allow_restart &&
                                     (tref->tpart_head == NULL));
                              tp->reinitialize();
                            }
                          if ((tp != KD_EXPIRED_TILE) && !tp->closed)
                            tp->open(env); // Modifies flags & does wakeup
                          else
                            { // Should not happen, but to be safe we should
                              // remove any pending open/wakeup conditions.
                              kdu_int32 mask = ~(KD_TREF_FLAGS_OPEN_PENDING |
                                                 KD_TREF_FLAGS_NEED_WAKEUP);
                              kdu_int32 old_val=tref->flags.exchange_and(mask);
                              if (old_val & KD_TREF_FLAGS_NEED_WAKEUP)
                                env->signal_condition(tref->wakeup); // NULL OK
                            }
                          env->advance_work_domains(); // Tile opening may be
                             // costly (in terms of time), so make sure we are
                             // not holding up the removal of completed domain
                             // sequences.
                        }
                      if ((tref_head.get() != NULL) &&
                          (job_state.get() & KD_BKGND_FLAG_RES_PARSE))
                        { // Go back and do newly arrived parsing work,
                          // before continuing with the lower priority tile
                          // open/close tasks.
                          old_services = services; // Store unfinished services
                          break;
                        }
                    }
                }
            }
          if (lock_acquired)
            cs->release_lock(KD_THREADLOCK_GENERAL,env);
        }

      if ((services & KD_BKGND_FLAG_FLUSH) && (cs->out != NULL))
        cs->flush_if_ready(env);

      if ((services & KD_BKGND_FLAG_TRIM) && (cs->out != NULL))
        cs->trim_compressed_data(env);
    }
}

/*****************************************************************************/
/*               kd_cs_thread_context::do_plock_processing                   */
/*****************************************************************************/

void
kd_cs_thread_context::do_plock_processing(kdu_thread_env *env)
{
  while (true)
    { // Outer job loop
      kdu_int32 old_val, new_val, services;
      do { // Enter compare-and-set loop
        new_val = old_val = job_state.get();
        services = old_val & KD_BKGND_PLOCK_SERVICES;
        new_val &= ~(KD_BKGND_FLAG_PLOCK_ACTIVE |
                     KD_BKGND_FLAG_PLOCK_PROCESSING |
                     KD_BKGND_PLOCK_SERVICES); // clear all plock related flags
        if (new_val & (KD_BKGND_FLAG_TERMINATE | KD_BKGND_FLAG_CLEAR_WAITING))
          { // Cancel all services for this job and become non-active
            services = 0;
            if ((new_val & KD_BKGND_FLAG_TERMINATE) &&
                !(new_val & KD_BKGND_ACTIVE_MASK))
              new_val |= KD_BKGND_FLAG_ALL_DONE;
            if (!(new_val & KD_BKGND_PROCESSING_MASK))
              new_val &= ~KD_BKGND_FLAG_CLEAR_WAITING; // The wait is over
          }
        else if (services != 0)
          new_val |= (KD_BKGND_FLAG_PLOCK_ACTIVE |
                      KD_BKGND_FLAG_PLOCK_PROCESSING);
      } while (!job_state.compare_and_set(old_val,new_val));
      if (!(new_val & KD_BKGND_FLAG_PLOCK_ACTIVE))
        { // End of current job instance
          assert(!(new_val & KD_BKGND_FLAG_PLOCK_PROCESSING));
          if ((new_val ^ old_val) & KD_BKGND_FLAG_ALL_DONE)
            all_done(env); // We have just set the `all_done' flag
          if ((new_val ^ old_val) & KD_BKGND_FLAG_CLEAR_WAITING)
            { // We can now wake the thread that is waiting in `clear_queues'
              env->signal_condition(clear_wakeup); // Safe even if this is NULL
            }
          return;
        }
      assert((new_val & KD_BKGND_FLAG_PLOCK_PROCESSING) &&
             !(new_val & KD_BKGND_FLAG_ALL_DONE));
      
      if (services & KD_BKGND_FLAG_RES_PALLOC)
        { 
          kd_resolution *res, *nxt;
          while ((res = (kd_resolution *) res_head.get()) != NULL)
            { 
              nxt = (kd_resolution *) res->bkgnd_next.get();
              res_head.set(nxt); // Only one thread can pull resolutions from
                                 // the queue, so we don't need to worry about
                                 // contention here.
              if (nxt == NULL)
                { // `res' appears to have been at the tail of the queue, but
                  // it is possible that another thread is in the process
                  // (or about to be in the process) of appending to the queue,
                  // in which case it will try to modify the value of
                  // `res->bkgnd_next'.
                  if (!res->bkgnd_next.compare_and_set(NULL,
                                                KD_BKGND_RES_TAIL_INTERRUPTED))
                    { // There was indeed an appending operation in progress
                      // and it added another `kd_resolution' object onto the
                      // end of the queue before the above atomic operation was
                      // committed.  We are the only thread that can set a
                      // non-NULL `res_head' value at this point.  The
                      // `res_tail' variable will remain non-NULL at least
                      // until then, so nobody else can touch `res_head'.
                      assert(res_tail.get() != NULL);
                      res_head.set(res->bkgnd_next.get());
                    }
                  else if (!res_tail.compare_and_set(res,NULL))
                    { // There is or was another thread in the process of
                      // appending to the tail of the queue.  This appending
                      // thread has not yet written to `res->bkgnd_next', at
                      // least prior to the point at which the
                      // `res->bkgnd_next.compare_and_set' atomic operation was
                      // committed, so when it does write to `res->bkgnd_next',
                      // it will see the special "INTERRUPTED" marker.  We
                      // should not service `res' at this point, because
                      // `res->bkgnd_next' is still vulnerable to
                      // manipulation.  The safest thing is to finish our
                      // background processing here, leaving `res_head' NULL;
                      // the appending thread will set `res_head' back to `res'
                      // again once it sees the "INTERRUPTED" marker, and we
                      // can be sure that no other thread will do this.  After
                      // that, the appending thread will schedule the job
                      // again or cause the current instance of it to loop
                      // back again.
                      break;
                    }
                }
              assert(cs->in == NULL); // res-palloc is for out/xchg codestreams
              res->do_background_processing(env);
            }
        }
    }
}


/* ========================================================================= */
/*                               kd_codestream                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_codestream::construct_common                    */
/*****************************************************************************/

void
 kd_codestream::construct_common()
{
  initial_fragment = final_fragment = true;
  construction_finalized = false;
  fragment_area_fraction = 1.0;

  // Get summary parameters from SIZ object.
  if ((in != NULL) && (marker->get_code() == KDU_SIZ))
    read_siz_auxiliary_marker_segs(); // Else we may have done this already
  ((kdu_params *) siz)->finalize(out==NULL);
  if (!(siz->get(Sprofile,0,0,profile) &&
        siz->get(Scomponents,0,0,num_components) &&
        siz->get(Ssize,0,0,canvas.size.y) && // Subtract y_pos later
        siz->get(Ssize,0,1,canvas.size.x) && // Subtract x_pos later
        siz->get(Sorigin,0,0,canvas.pos.y) &&
        siz->get(Sorigin,0,1,canvas.pos.x) &&
        siz->get(Stiles,0,0,tile_partition.size.y) &&
        siz->get(Stiles,0,1,tile_partition.size.x) &&
        siz->get(Stile_origin,0,0,tile_partition.pos.y) &&
        siz->get(Stile_origin,0,1,tile_partition.pos.x)))
    assert(0);
  if  (profile == 0)
    next_tnum = 0;
  else
    next_tnum = -1;
  if ((canvas.size.x < 0) || (canvas.size.y < 0) ||
      (canvas.pos.x < 0) || (canvas.pos.y < 0) ||
      (tile_partition.size.x < 0) || (tile_partition.size.y < 0) ||
      (tile_partition.pos.x < 0) || (tile_partition.pos.y < 0))
    { KDU_ERROR(e,0x23091301); e <<
      KDU_TXT("One or more 32-bit fields within the SIZ marker segment "
              "is greater than or equal to 2^31.  This is legal, but this "
              "version of Kakadu expects all codestream dimensions "
              "(including tile dimensions) to be positive, when interpreted "
              "as 32-bit signed integers.  If this proves to be a practical "
              "limitation, the restriction may be removed in the future.");
    }
  canvas.size -= canvas.pos;
  if ((canvas.size.y <= 0) || (canvas.size.x <= 0) ||
      (tile_partition.pos.x > canvas.pos.x) ||
      (tile_partition.pos.y > canvas.pos.y) ||
      ((tile_partition.pos.x+tile_partition.size.x) <= canvas.pos.x) ||
      ((tile_partition.pos.y+tile_partition.size.y) <= canvas.pos.y))
    { KDU_ERROR(e,26); e <<
        KDU_TXT("Illegal canvas coordinates: the first tile is "
        "required to have a non-empty intersection with the image on the "
        "high resolution grid.");
    }

  // Configure the codestream component structure
  int n;

  if (num_components > 16384)
    { KDU_ERROR(e,0x06110804); e <<
      KDU_TXT("Trying to create a `kdu_codestream' object with more than "
              "16384 image components -- this is the maximum number allowed "
              "by the standard.");
    }
  
  comp_info = new kd_comp_info[num_components];
  for (n=0; n < num_components; n++)
    { 
      kd_comp_info *ci = comp_info + n;
      if (!siz->get(Sprecision,n,0,ci->precision))
        { KDU_ERROR(e,27); e <<
            KDU_TXT("No information available concerning component "
            "sample bit-depths (i.e., sample precision).");
        }
      if (!siz->get(Ssigned,n,0,ci->is_signed))
        { KDU_ERROR(e,28); e <<
            KDU_TXT("No information available regarding whether "
            "components are signed or unsigned.");
        }
      if (!(siz->get(Ssampling,n,0,ci->sub_sampling.y) &&
            siz->get(Ssampling,n,1,ci->sub_sampling.x)))
        { KDU_ERROR(e,29); e <<
            KDU_TXT("No information available concerning component "
            "sub-sampling factors.");
        }
      ci->apparent_idx = n;
      ci->from_apparent = ci;
      ci->crg_x = ci->crg_y = 0.0F;
      for (int d=0; d <= 32; d++)
        ci->hor_depth[d] = ci->vert_depth[d] = (kdu_byte) d;
           // This default initialization of the depth values may be
           // changed in `finalize_construction'.
      ci->qlim_sq_weight = 1.0f;
      ci->qlim_type_flags = 0;
    }

  // Configure tiles
  tiles_in_progress_head = tiles_in_progress_tail = NULL;
  tile_span.y = ceil_ratio(canvas.size.y+canvas.pos.y-tile_partition.pos.y,
                           tile_partition.size.y);
  tile_span.x = ceil_ratio(canvas.size.x+canvas.pos.x-tile_partition.pos.x,
                           tile_partition.size.x);
  kdu_long t_tiles = ((kdu_long) tile_span.x) * ((kdu_long) tile_span.y);
  if ((t_tiles > 65535) || (t_tiles <= 0) ||
      (tile_span.x < 0) || (tile_span.x > 65535) || (tile_span.y > 65535))
    { KDU_ERROR(e,30); e <<
        KDU_TXT("Maximum number of allowable tiles is 65535 for any "
        "JPEG2000 code-stream.  You have managed to exceed this number!!");
    }
  int total_tiles = (int) t_tiles;
  tile_indices.pos = kdu_coords(0,0);
  tile_indices.size = tile_span;
  tile_refs = new kd_tile_ref[total_tiles];
  memset(tile_refs,0,((size_t) total_tiles) * sizeof(kd_tile_ref));
  
  // See if CBR flushing is required
  cbr_flush_tile = 0;
  if ((out != NULL) &&
      siz->get(Scbr, 0, 0, cbr_flush_interval) &&
      siz->get(Scbr, 0, 1, cbr_max_bucket_lines))
    { // Do some compatibility checking
    if ((cbr_flush_interval < 1) || (cbr_max_bucket_lines < 1))
      {
 KDU_ERROR(e,0x23081601); e <<
      KDU_TXT("The V and Bmax parameters supplied with the `Scbr' "
              "attribute must both be positive integers.");
      }
    if (tile_span.x != 1)
      {
 KDU_ERROR(e,0x24081608); e <<
      KDU_TXT("Unsupported combination of `Scbr' attribute with "
              "horizontal tiling.  For simplicity, we currently do not "
              "allow this, although it could be supported if there were "
              "a good reason to do so in the future.");
      }
    }
  else
    { cbr_flush_interval = 0; cbr_max_bucket_lines = 0; }

  // Check for profile violations
  if (profile == 0)
    {
      if (((tile_partition.size.x != 128) ||
           (tile_partition.size.y != 128)) && (total_tiles > 1))
        { KDU_WARNING(w,9); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  Profile-0 code-streams must either be untiled "
            "or else the tile dimensions must be exactly 128x128.  Try "
            "setting \"Sprofile\" to 1 or 2 or avoid using tiles.");
          profile = 2;
        }
      else if (tile_partition.pos.x || tile_partition.pos.y ||
               canvas.pos.x || canvas.pos.y)
         { KDU_WARNING(w,10); w <<
             KDU_TXT("Profile violation detected (code-stream is technically "
             "illegal).  Profile-0 code-streams must have image and tiling "
             "origins (anchor points) set to zero.  Try setting "
             "\"Sprofile\" to 1 or 2.");
           profile = 2;
         }
       else
         {
           for (n=0; n < num_components; n++)
             {
               kd_comp_info *ci = comp_info + n;
               if (((ci->sub_sampling.x != 1) && (ci->sub_sampling.x != 2) &&
                    (ci->sub_sampling.x != 4)) ||
                   ((ci->sub_sampling.y != 1) && (ci->sub_sampling.y != 2) &&
                    (ci->sub_sampling.y != 4)))
                 break;
             }
           if (n < num_components)
             { KDU_WARNING(w,11); w <<
                 KDU_TXT("Profile violation detected (code-stream is "
                 "technically illegal).  Component sub-sampling factors for "
                 "Profile-0 code-streams are restricted to the values 1, 2 "
                 "and 4.  Try setting \"Sprofile\" to 1 or 2.");
               profile = 2;
             }
         }
    }
  else if (profile == 1)
    { // Check for violations
      if (total_tiles > 1)
        { // The image is tiled
          if (tile_partition.size.x != tile_partition.size.y)
            { KDU_WARNING(w,12); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-1 code-streams must "
                "either be untiled or else the horizontal and vertical "
                "tile dimensions must be identical (square tiles on the "
                "hi-res canvas).  You might like to set \"Sprofile\" "
                "to 2 or avoid using tiles.");
              profile = 2;
            }
          else
            {
              for (n=0; n < num_components; n++)
                {
                  kd_comp_info *ci = comp_info + n;
                  if ((tile_partition.size.x > (ci->sub_sampling.x<<10)) ||
                      (tile_partition.size.y > (ci->sub_sampling.y<<10)))
                    break;
                }
              if (n < num_components)
                { KDU_WARNING(w,13); w <<
                    KDU_TXT("Profile violation detected (code-stream is "
                    "technically illegal).  If a Profile-1 code-stream is "
                    "tiled (has multiple tiles), the width and height of "
                    "its tiles, projected onto any given image component, "
                    "may not exceed 1024.  You might like to set "
                    "\"Sprofile\" to 2 or avoid using tiles.");
                  profile = 2;
                }
            }
        }
    }

  // Build the parameter structure.
  int extensions = 0;
  siz->get(Sextensions,0,0,extensions);
  kdu_params *element = NULL;
  try { // Protect against possible memory leaks
    element = new mct_params;
    element->link(siz,-1,-1,total_tiles,0);

    element = new mcc_params;
    element->link(siz,-1,-1,total_tiles,0);

    element = new mco_params;
    element->link(siz,-1,-1,total_tiles,0);

    element = new atk_params; // Must link this before `cod_params', if at all
    element->link(siz,-1,-1,total_tiles,0);

    element = new cod_params;
    element->link(siz,-1,-1,total_tiles,num_components);

    element = new dfs_params; // Must link this one after `cod_params'
    element->link(siz,-1,-1,0,0);

    element = new ads_params; // Must link this one after `cod_params'
    element->link(siz,-1,-1,total_tiles,0);

    element = new qcd_params; // Must link this one after `cod_params'
    element->link(siz,-1,-1,total_tiles,num_components);

    element = new rgn_params;
    element->link(siz,-1,-1,total_tiles,num_components);

    element = new poc_params;
    element->link(siz,-1,-1,total_tiles,0);

    element = new org_params;
    element->link(siz,-1,-1,total_tiles,0);

    element = new crg_params;
    element->link(siz,-1,-1,0,0);
    
    int nlt_components=0;
    siz->get(Ncomponents,0,0,nlt_components);
    if (nlt_components != 0)
      { // Should always be the case
        element = new nlt_params;
        element->link(siz,-1,-1,total_tiles,nlt_components);
      }
  }
  catch (kdu_exception exc) {
    if (element != NULL) delete element;
    throw exc;
  }

  // Now set up some common services, which we are sure to need.
  buf_master = new kd_buf_master;
  buf_servers = new kd_buf_server[1+KDU_MAX_THREADS];
  buf_servers[0].attach_and_init(buf_master);
  precinct_server = new kd_precinct_server(this,(out != NULL));
  block = new kdu_block();

  // Now some final initialization steps.
  if (in != NULL)
    { 
      if (in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE)
        tpart_ptr_server = new kd_tpart_pointer_server;
      read_main_header();
    }
  if (output_comp_info == NULL)
    construct_output_comp_info();

  region = canvas;
  discard_levels = 0;
  min_dwt_levels = 100; // Ridiculous value
  max_apparent_layers = 0xFFFF;
  num_apparent_components = num_components;
  max_tile_layers = 1;
  tiles_accessed = false;
  memset(rate_stats,0,sizeof(kd_compressed_stats *)*(1+KDU_MAX_THREADS));
  cannot_flip = false; // Until proven otherwise
}

/*****************************************************************************/
/*                   kd_codestream::close_pending_precincts                  */
/*****************************************************************************/

void
  kd_codestream::close_pending_precincts()
{
  kd_precinct *scan, *list = (kd_precinct *) pending_precincts.exchange(NULL);
  while ((scan = list) != NULL)
    { 
      list = scan->next;
      scan->next = NULL;
      scan->ref->close(NULL); // Caller must be in the "glock" context
    }
}

/*****************************************************************************/
/*                  kd_codestream::process_pending_precincts                 */
/*****************************************************************************/

void
  kd_codestream::process_pending_precincts()
{
  kd_precinct *scan, *list = (kd_precinct *) pending_precincts.exchange(NULL);
  if (in != NULL)
    { // Invoke `kd_precinct::release' on all elements in the list
      while ((scan = list) != NULL)
        { 
          list = scan->next;
          scan->next = NULL;
          scan->release();
        }
    }
  else if (out != NULL)
    {
      while ((scan = list) != NULL)
        { 
          list = scan->next;
          scan->next = NULL;
          scan->resolution->rescomp->add_ready_precinct(scan);
        }
    }
  else if (list != NULL)
    { // We should not have pending precincts for interchange codestreams
      assert(0);
      while ((scan = list) != NULL)
        { // Better close the precincts just to be safe
          list = scan->next;
          scan->next = NULL;
          scan->ref->close(NULL); // Caller is in the "glock" context -- the
                                  // `KD_THREADLOCK_GENERAL' mutex is locked
        }
    }
}

/*****************************************************************************/
/*                 kd_codestream::gen_no_thread_context_error                */
/*****************************************************************************/

void
  kd_codestream::gen_no_thread_context_error()
{
  KDU_ERROR_DEV(e,0x04081102); e << 
    KDU_TXT("Multi-threaded implementation error detected.  Before passing "
            "a `kdu_thread_env' reference into any of the `kdu_codestream' "
            "machinery's interface functions you need to pass a "
            "`kdu_thread_env' reference into one of the top-level "
            "interface functions that configures the codestream for "
            "multi-threaded processing.  The main functions of this form "
            "are `kdu_codestream::create' and `kdu_codestream::open_tile', "
            "although there are others which can see a `kdu_thread_env' "
            "environment for the first time.");
}

/*****************************************************************************/
/*                 kd_codestream::gen_bad_thread_context_error               */
/*****************************************************************************/

void
  kd_codestream::gen_bad_thread_context_error()
{
  KDU_ERROR_DEV(e,0x10081101); e << 
    KDU_TXT("Multi-threaded implementation error detected.  All "
            "`kdu_thread_env' references used in calls to a `kdu_codestream' "
            "object's interface functions (including those of its descendant "
            "objects such as `kdu_tile', `kdu_subband' and so forth) must "
            "all belong to a single thread group.");
}

/*****************************************************************************/
/*                  kd_codestream::construct_output_comp_info                */
/*****************************************************************************/

void
  kd_codestream::construct_output_comp_info()
{
  assert(output_comp_info == NULL);

  int n, extension_flags = 0;
  siz->get(Sextensions,0,0,extension_flags);
  uses_mct = ((extension_flags & Sextensions_MCT) != 0);
  bool have_cbd =
    (siz->get(Mcomponents,0,0,num_output_components) &&
     (num_output_components > 0));
  if (have_cbd != uses_mct)
    { KDU_ERROR(e,0x04080500); e <<
        KDU_TXT("The `Mcomponents' parameter attribute must be assigned a "
                "non-zero value if and only if the `MCT' flag is present "
                "in the `Sextensions' attribute.");
    }
  if (!have_cbd)
    num_output_components = num_components;
  else if (num_output_components > 16384)
    { KDU_ERROR(e,0x07110800); e <<
      KDU_TXT("Number of multi-component transform output components "
              "defined by CBD marker segment exceeds the maximum "
              "allowed value of 16384.");
    }

  num_apparent_output_components = num_output_components;
  output_comp_info = new kd_output_comp_info[num_output_components];
  for (n=0; n < num_output_components; n++)
    { 
      kd_output_comp_info *oci = output_comp_info + n;
      
      if (!have_cbd)
        {
          oci->in_precision = comp_info[n].precision;
          oci->in_signed = comp_info[n].is_signed;
        }
      else if (!(siz->get(Mprecision,n,0,oci->in_precision) &&
                 siz->get(Msigned,n,0,oci->in_signed) &&
                 (oci->in_precision >= 1)))
        { // Should have been configured during finalization.
          KDU_ERROR_DEV(e,0x10011602); e <<
          KDU_TXT("The `siz_params' object does not seem to have been "
                  "finalized properly -- valid `Mprecision'/`Msigned' "
                  "parameters could not be recovered.  Have you "
                  "forgotten to invoke `kdu_params::finalize_all'?");
        }
      if (!(siz->get(Nprecision,n,0,oci->out_precision) &&
            siz->get(Nsigned,n,0,oci->out_signed) &&
            (oci->out_precision >= 1)))
        { 
          KDU_ERROR_DEV(e,0x10011603); e <<
          KDU_TXT("The `siz_params' object does not seem to have been "
                  "finalized properly -- valid `Nprecision'/`Nsigned' "
                  "parameters could not be recovered.  Have you forgotten to "
                  "invoke `kdu_params::finalize_all'?");
        }
      
      int n1 = (n >= num_components)?(num_components-1):n; // Temporarily bind
      oci->subsampling_ref=comp_info+n1; // sampling to a codestream component
      oci->apparent_idx = n;
      oci->from_apparent = n;
      oci->block = NULL;
      oci->block_comp_idx = 0;
      oci->apparent_block_comp_idx = 0;
    }

  component_access_mode = KDU_WANT_OUTPUT_COMPONENTS; // Default access mode
}

/*****************************************************************************/
/*                     kd_codestream::finalize_construction                  */
/*****************************************************************************/

void
  kd_codestream::finalize_construction()
{
  if (output_comp_info == NULL)
    construct_output_comp_info();
  if (construction_finalized)
    return;
  construction_finalized = true;

  int n, c, d;

  // Obtain codestream registration information, if any
  kdu_params *crg = siz->access_cluster(CRG_params);
  for (n=0; n < num_components; n++)
    {
      kd_comp_info *ci = comp_info + n;
      if ((crg == NULL) ||
          (!crg->get(CRGoffset,n,0,ci->crg_y)) ||
          (!crg->get(CRGoffset,n,1,ci->crg_x)))
        ci->crg_x = ci->crg_y = 0.0F;
    }

  // Obtain the downsampling factor structure
  kdu_params *cod = siz->access_cluster(COD_params);
  for (n=0; n < num_components; n++)
    {
      kd_comp_info *ci = comp_info + n;
      kdu_params *coc = cod->access_relation(-1,n,0,true);
      for (d=1; d < 33; d++)
        {
          int decomp=3;
          coc->get(Cdecomp,d-1,0,decomp);
          ci->hor_depth[d] = ci->hor_depth[d-1] + (decomp & 1);
          ci->vert_depth[d] = ci->vert_depth[d-1] + ((decomp >> 1) & 1);
        }
    }
  
  // Configure global non-linear point transform (NLT) information
  kdu_params *nlt = siz->access_cluster(NLT_params);
  output_comp_info[0].nlt_info = kd_nlt_info::create(nlt,-1,-1); // May be NULL  
  for (n=0; n < num_output_components; n++)
    { 
      kd_output_comp_info *oci = output_comp_info + n;
      if ((n < (num_output_components-1)) && (oci->nlt_info != NULL))
        oci[1].nlt_info = oci->nlt_info->add_ref(); // Avoid premature delete
      kd_nlt_info *local_nlt_info = kd_nlt_info::create(nlt,-1,n);
      if (local_nlt_info != NULL)
        { // Replace any global NLT descriptor with the local one here 
          if (oci->nlt_info != NULL)
            oci->nlt_info->release(); // Global nlt_info ref already held by
                // next component, if any, so no risk of unwanted self-efacure.
          oci->nlt_info = local_nlt_info;
        }
    }
  
  // Configure the resolution-component progress status management system
  if (out != NULL)
    {
      kd_global_rescomp *rc = global_rescomps;
      if (rc == NULL)
        rc = global_rescomps = new kd_global_rescomp[33*num_components];
      for (d=0; d < 33; d++)
        for (c=0; c < num_components; c++, rc++)
          rc->initialize(this,d,c);
    }
  
  // Configure the codestream-level reslength information
  if (out != NULL)
    {
      reslength_constraints_used = reslength_constraints_violated = false;
      if (reslength_checkers == NULL)
        reslength_checkers = new kd_reslength_checker[1+num_components];
      for (c=-1; c < num_components; c++)
        {
          cod_params *coc = (cod_params *) cod->access_unique(-1,c);
          if (reslength_checkers[c+1].init(coc,c,num_components,
                                           reslength_checkers+1))
            reslength_constraints_used = true;
        }
    }

  if (uses_mct)
    { // Now we have only to build an association between each MCT output
      // component and a corresponding codestream component, so that
      // applications can determine the dimensions of each output component,
      // right from the beginning.  We will base this on an analysis of the
      // main header MCC and MCO marker segments which are available -- if
      // there are none, our assumptions might be wrong, but the error can
      // be caught later on when we come to actually process each tile.
      kd_mct_stage::create_stages(global_mct_head,global_mct_tail,siz,-1,
                                  num_components,comp_info,
                                  num_output_components,output_comp_info,
                                  (out != NULL));
      if (global_mct_tail != NULL)
        { 
          kd_output_comp_info *oci_src = global_mct_tail->output_comp_info;
          kd_output_comp_info *oci_tgt = this->output_comp_info;
          for (n=0; n < num_output_components; n++)
            { 
              oci_tgt[n].subsampling_ref = oci_src[n].subsampling_ref;
              oci_tgt[n].crg_incompatible = oci_src[n].crg_incompatible;
            }
        } 
    }
}

/*****************************************************************************/
/*                     kd_codestream::restrict_to_fragment                   */
/*****************************************************************************/

void
 kd_codestream::restrict_to_fragment(kdu_dims fragment_region,
                                     int fragment_tiles_generated,
                                     kdu_long fragment_tile_bytes_generated)
{
  assert(out != NULL);
  fragment_region &= canvas;
  fragment_area_fraction =
    ((double) fragment_region.area()) / ((double) canvas.area());
  this->prev_tiles_written = fragment_tiles_generated;
  this->prev_tile_bytes_written = fragment_tile_bytes_generated;

  kdu_coords tsize = tile_partition.size;
  kdu_coords min = fragment_region.pos - tile_partition.pos;
  kdu_coords lim = min + fragment_region.size;
  kdu_coords idx_min, idx_lim;
  idx_min.x = min.x / tsize.x;
  idx_min.y = min.y / tsize.y;
  idx_lim.x = 1 + ((lim.x-1) / tsize.x);
  idx_lim.y = 1 + ((lim.y-1) / tsize.y);
  bool last_x = (lim.x == (canvas.pos.x+canvas.size.x));
  bool last_y = (lim.y == (canvas.pos.y+canvas.size.y));
  bool first_x = (min.x == canvas.pos.x);
  bool first_y = (min.y == canvas.pos.y);
  if (!((first_x || (min.x == idx_min.x*tsize.x)) &&
        (first_y || (min.y == idx_min.y*tsize.y))))
    { KDU_ERROR_DEV(e,0x06110901); e <<
      KDU_TXT("The fragment region supplied to `kdu_codestream::create' "
              "is not correctly aligned with its left and upper edges on a "
              "tile boundary (or the image boundary).");
    }
  if (!((last_x || (lim.x == idx_lim.x*tsize.x)) &&
        (last_y || (lim.y == idx_lim.y*tsize.y))))
    { KDU_ERROR_DEV(e,0x06110902); e <<
      KDU_TXT("The fragment region supplied to `kdu_codestream::create' "
              "is not correctly aligned with its right and lower edges on a "
              "tile boundary (or the image boundary).");
    }
  if ((idx_lim.x <= idx_min.x) || (idx_lim.y <= idx_min.y))
    { KDU_ERROR_DEV(e,32); e <<
        KDU_TXT("The fragment region supplied to "
        "`kdu_codestream::create' is empty.");
    }

  kdu_coords num_indices = idx_lim - idx_min;
  int current_fragment_tiles = num_indices.x*num_indices.y;
  assert(current_fragment_tiles > 0);
  int future_fragment_tiles = tile_span.x*tile_span.y -
    (current_fragment_tiles + fragment_tiles_generated);
  if (future_fragment_tiles < 0)
    { KDU_ERROR_DEV(e,33); e <<
        KDU_TXT("The fragment region supplied to "
        "`kdu_codestream::create' represents too many tiles, allowing for "
        "the number of tiles indicated for previously generated fragments.");
    }
  
  initial_fragment = (fragment_tiles_generated == 0);
  final_fragment = (future_fragment_tiles == 0);

  if (num_indices == tile_indices.size)
    assert((idx_min == tile_indices.pos) &&
           (canvas == fragment_region) && initial_fragment && final_fragment);
  else
    {
      delete[] tile_refs;
      tile_refs = NULL;
      tile_indices.pos = idx_min;
      tile_indices.size = num_indices;
      canvas = fragment_region;
      region = canvas;
      tile_refs = new kd_tile_ref[current_fragment_tiles];
      memset(tile_refs,0,(size_t) current_fragment_tiles*sizeof(kd_tile_ref));
    }
}

/*****************************************************************************/
/*                            kd_codestream::restart                         */
/*****************************************************************************/

void
  kd_codestream::restart()
{
  close_pending_precincts(); // Remove any precincts which happen to still be
                             // on the `pending_precincts' list.
  
  // Remove some uncommon services; this may cost us nothing in efficiency
  if (ppm_markers != NULL)
    delete ppm_markers;
  ppm_markers = NULL;
  if (tpart_ptr_server != NULL)
    delete tpart_ptr_server;
  tpart_ptr_server = NULL;
  if (rate_stats[0] != NULL)
    { 
      int t, num_threads = 0;
      rate_stats[0]->set_next(NULL);
      if (thread_context != NULL)
        num_threads = thread_context->manage_compressed_stats(NULL);
      for (t=0; t <= num_threads; t++)
        if (rate_stats[t] != NULL)
          { 
            delete[] rate_stats[t];
            rate_stats[t] = NULL;
          }
    }
  tlm_generator.clear();
  construction_finalized = false;

  // Reset rate-control related members
  if (layer_sizes != NULL)
    { delete[] layer_sizes; layer_sizes = NULL; }
  if (tmp_layer_sizes != NULL)
    { delete[] tmp_layer_sizes; tmp_layer_sizes = NULL; }
  if (target_sizes != NULL)
    { delete[] target_sizes; target_sizes = NULL; }
  if (expected_sizes != NULL)
    { delete[] expected_sizes; expected_sizes = NULL; }
  if (target_min_sizes != NULL)
    { delete[] target_min_sizes; target_min_sizes = NULL; }
  if (layer_thresholds != NULL)
    { delete[] layer_thresholds; layer_thresholds = NULL; }
  if (target_thresholds != NULL)
    { delete[] target_thresholds; target_thresholds = NULL; }
  rate_tolerance = 0.0F;
  record_in_comseg = false;
  trim_to_rate = false;
  using_slopes = false;
  using_min_sizes = false;
  trans_out_non_empty_layers = 0;
  trans_out_max_bytes = 0;
  trans_out_non_empty_layers = 0;
  num_sized_layers = 0;
  cbr_flush_tile = 0;
  cbr_fset.active_tile = NULL;

  // Delete the output component info array
  if (output_comp_info != NULL)
    delete[] output_comp_info;
  output_comp_info = NULL;
  num_output_components = num_apparent_output_components = 0;
  
  // Remove any quality limiter
  if (limiter != NULL)
    delete limiter;
  limiter = NULL;

  // Restart all existing tiles.
  assert(tile_refs != NULL);
  kdu_coords idx;
  kd_tile_ref *tref = tile_refs;
  for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < tile_indices.size.x; idx.x++, tref++)
      { 
        tref->tpart_head = tref->tpart_tail = NULL;
        if (tref->flags.get() & KD_TREF_FLAGS_NEED_WAKEUP)
          { KDU_ERROR_DEV(e,0x21041402); e <<
            KDU_TXT("Calling `kdu_codestream::restart' while another "
                    "thread is still waiting for a tile in the original "
                    "codestream to be opened!!  This is clearly a problem "
                    "since restarting a codestream is conceputally "
                    "equivalent to destroying and recreating it.");
          }
        assert(!(tref->flags.get() & KD_TREF_FLAGS_ENQUEUED));
        tref->flags.set(0);
        tref->wakeup = NULL;
        tref->bkgnd_next.set(NULL);
        if (tref->tile == NULL)
          continue;
        assert(tref->tile != KD_EXPIRED_TILE);
          // In restart mode, no tiles should be allowed to expire
        if (tref->tile->is_open)
          { KDU_ERROR_DEV(e,34); e <<
              KDU_TXT("You must close all open tile interfaces "
              "before calling `kdu_codestream::restart'.");
          }
        tref->tile->restart();
      }

  // Delete all previously released typical tiles
  kd_tile *typ;
  while ((typ=typical_tile_cache) != NULL)
    {
      typical_tile_cache = typ->typical_next;
      assert(typ->tile_ref == NULL);
      delete typ;
    }

  // Now some final initialization steps.
  while ((comtail=comhead) != NULL)
    {
      comhead = comtail->next;
      delete comtail;
    }
  comments_frozen = false;
  header_generated = false;
  header_length = 0;
  written_packet_bytes = 0;
  written_packet_header_bytes = 0;
  siz->clear_marks(in != NULL);
  if (in != NULL)
    { 
      assert(marker->get_code() == KDU_SIZ);
      siz->translate_marker_segment(marker->get_code(),marker->get_length(),
                                    marker->get_bytes(),-1,0);
      read_main_header();
    }
  if (output_comp_info == NULL)
    construct_output_comp_info();
  tiles_accessed = false;
  active_tile = NULL;
  next_sot_address = 0;
  next_sot_scan_address = 0;
  next_tnum = 0;
  num_completed_tparts = 0;
  num_open_tiles = 0;
  textualize_out = NULL;
  
  assert(pending_precincts.get() == NULL);
  tc_flush_counter.set(0);
  incr_flush_counter.set(0);
  tc_flush_interval = 0;
  incr_flush_interval = 0;
  tc_flush_pending.set(0);
}

/*****************************************************************************/
/*                kd_codestream::set_reserved_layer_info_bytes               */
/*****************************************************************************/

void
  kd_codestream::set_reserved_layer_info_bytes(int num_layers)
{
  // First remove any existing layer info comment
  kd_codestream_comment *prev, *scan;
  for (prev=NULL, scan=comhead; scan != NULL; prev=scan, scan=scan->next)
    {
      kdu_codestream_comment com(scan);
      if (strncmp(com.get_text(),
                  "Kdu-Layer-Info: ",strlen("Kdu-Layer-Info: ")) == 0)
        break;
    }
  if (scan != NULL)
    {
      if (prev == NULL)
        comhead = scan->next;
      else
        prev->next = scan->next;
      delete scan;
      if (scan == comtail)
        comtail = prev;
    }

  // Now set aside a sufficient number of bytes
  reserved_layer_info_bytes = 6 + num_layers*17 + (int) strlen(
    "Kdu-Layer-Info: log_2{Delta-D(squared-error)/Delta-L(bytes)}, L(bytes)\n"
    );
}

/*****************************************************************************/
/*                     kd_codestream::gen_layer_info_comment                 */
/*****************************************************************************/

void
  kd_codestream::gen_layer_info_comment(int num_layers, kdu_long *layer_bytes,
                                        kdu_uint16 *thresholds)
{
  if (reserved_layer_info_bytes == 0)
    return;

  kd_codestream_comment *elt = new kd_codestream_comment;
  if (comhead == NULL)
    comhead = comtail = elt;
  else
    comtail = comtail->next = elt;
  kdu_codestream_comment com(elt);
  com <<
    "Kdu-Layer-Info: log_2{Delta-D(squared-error)/Delta-L(bytes)}, L(bytes)\n";
  double size_factor = 1.0 / fragment_area_fraction;
  char line[18];
  for (int n=0; n < num_layers; n++)
    {
      double log_lambda = thresholds[n];
      log_lambda *= 1.0/256.0;
      log_lambda -= 256-64;
      sprintf(line,"%6.1f, %8.1e\n",log_lambda,
              size_factor * ((double) layer_bytes[n]));
      com << line;
    }
  elt->write_marker(NULL,reserved_layer_info_bytes);
      // Truncates or pads the comment marker if length is wrong and sets it
      // to read-only mode.
}

/*****************************************************************************/
/*                kd_codestream::read_siz_auxiliary_marker_segs              */
/*****************************************************************************/

void
kd_codestream::read_siz_auxiliary_marker_segs()
{
  assert(marker->get_code() == KDU_SIZ);
  
  bool is_seekable=false, uses_mct=false;
  is_seekable = ((in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE) != 0);
  int ext=0;
  siz->get(Sextensions,0,0,ext);
  uses_mct = ((ext & Sextensions_MCT) != 0);
  
  bool found_cbd=false;
  bool need_to_seek_back=false;
  kdu_long seek_pos = in->get_offset(); // In case we need to seek back again
  while ((uses_mct && !found_cbd) &&
         marker->read() && (marker->get_code() != KDU_SOT))
    {       
      kdu_uint16 code = marker->get_code();
      if (code == KDU_CBD)
        { 
          if (!siz->translate_marker_segment(code,marker->get_length(),
                                             marker->get_bytes(),-1,0))
            { KDU_ERROR_DEV(e,0x02081501); e <<
              KDU_TXT("`siz_params' failed to translate marker segment ");
              print_marker_code(code,e); e <<
              KDU_TXT(" that provides critical global codestream "
                      "information.");
            }
          found_cbd = true;
          marker->clear();
          if (!need_to_seek_back)
            seek_pos = in->get_offset(); // Update the seek position
        }
      else
        { // Found something we are not looking for
          if (!is_seekable)
            { KDU_WARNING(w,0x06031601); w <<
              KDU_TXT("Source does not support seeking, yet while parsing "
                      "the main codestream header, critical marker segments "
                      "that might provide global codestream information for "
                      "the `siz_params' marker segment were not found all "
                      "together up front.  This may or may not prove to be "
                      "a problem.");
              return;
            }
          need_to_seek_back = true;
        }
    }
  
  if (is_seekable && need_to_seek_back)
    { 
      in->seek(seek_pos);
      marker->clear();
    }
}

/*****************************************************************************/
/*                        kd_codestream::read_main_header                    */
/*****************************************************************************/

void
  kd_codestream::read_main_header()
{
  if (in == NULL)
    return;
  bool have_tlm = false;
  bool have_marker_on_entry = (marker->get_code() != 0);
  do {
      if (!(have_marker_on_entry || marker->read()))
        {
          if (in->failed())
            break;
          KDU_ERROR(e,35); e <<
            KDU_TXT("Main code-stream header appears corrupt!");
        }
      have_marker_on_entry = false;
      if (marker->get_code() == KDU_PPM)
        {
          if (cached_source)
            { KDU_ERROR_DEV(e,36); e <<
                KDU_TXT("You cannot use PPM or PPT marker "
                "segments (packed packet headers) with cached compressed "
                "data sources.");
            }
          if (profile == 0)
            { KDU_WARNING(w,14);  w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  PPM marker segments may not "
                "appear within a Profile-0 code-stream.  You should "
                "set \"Sprofile\" to 1 or 2.");
              profile = 2;
            }
          if (ppm_markers == NULL)
            ppm_markers = new kd_pp_markers;
          ppm_markers->add_marker(*marker);
        }
      else if (marker->get_code() == KDU_TLM)
        {
          if (tpart_ptr_server != NULL)
            tpart_ptr_server->add_tlm_marker(*marker);
          have_tlm = true;
        }
      else if (marker->get_code() == KDU_COM)
        {
          int len = marker->get_length();
          kdu_byte *dat = marker->get_bytes();
          if ((len > 2) && (dat[0] == 0) && (dat[1] < 2))
            {
              kd_codestream_comment *new_elt = new kd_codestream_comment;
              if (comtail == NULL)
                comhead = comtail = new_elt;
              else
                comtail = comtail->next = new_elt;
              bool is_text = (dat[1] == 1);
              comtail->init(len-2,dat+2,is_text);
            }
        }
      else
        siz->translate_marker_segment(marker->get_code(),
                                      marker->get_length(),
                                      marker->get_bytes(),-1,0);
    } while (marker->get_code() != KDU_SOT);

  siz->finalize_all(-1,true); // Finalize all main header parameter objects

  if (tpart_ptr_server != NULL)
    {
      if (ppm_markers != NULL)
        {
          delete tpart_ptr_server;
          tpart_ptr_server = NULL;
          if (have_tlm)
            { KDU_WARNING_DEV(w,15); w <<
                KDU_TXT("Dynamic indexing of tile-parts, whether "
                "by TLM (tile-part length) marker segments or otherwise, "
                "cannot be used by the current implementation when PPM "
                "(packed packet header) marker segments are also used.");
            }
        }
      else
        {
          assert(tile_span == tile_indices.size);
          tpart_ptr_server->translate_markers(in->get_bytes_read()-12,
                                              tile_span.x*tile_span.y,
                                              tile_refs);
        }
    }
  
  finalize_construction();
}

/*****************************************************************************/
/*                        kd_codestream::freeze_comments                     */
/*****************************************************************************/

void
  kd_codestream::freeze_comments()
{
  if (comments_frozen)
    return;
#ifdef KDU_IDENTIFIER
  kd_codestream_comment *comscan;
  for (comscan=comhead; comscan != NULL; comscan=comscan->next)
    {
      kdu_codestream_comment com(comscan);
      if (strcmp(com.get_text(),KDU_IDENTIFIER) == 0)
        break;
    }
  if (comscan == NULL)
    {
      comscan = new kd_codestream_comment;
      comscan->init((int) strlen(KDU_IDENTIFIER),(kdu_byte *) KDU_IDENTIFIER,
                    true);
      if (comtail == NULL)
        comhead = comtail = comscan;
      else
        comtail = comtail->next = comscan;
    }
#endif // KDU_IDENTIFIER
  comments_frozen = true;
}

/*****************************************************************************/
/*      kd_codestream::delete_and_reset_all_but_buffering_and_threading      */
/*****************************************************************************/

void kd_codestream::delete_and_reset_all_but_buffering_and_threading()
{
  close_pending_precincts(); // Cleanly remove any precincts which happen to
                             // still be on the `pending_precincts' list.
  
  // Delete arrays
  if (comp_info != NULL)
    { delete[] comp_info; comp_info = NULL; }
  if (output_comp_info != NULL)
    { delete[] output_comp_info; output_comp_info = NULL; }
  if (global_rescomps != NULL)
    { delete[] global_rescomps; global_rescomps = NULL; }
  if (reslength_checkers != NULL)
    { delete[] reslength_checkers; reslength_checkers = NULL; }
  
  active_tile = NULL;
  if (tile_refs != NULL)
    { 
      kdu_coords idx, abs_idx;
      kd_tile_ref *tref = tile_refs;
      for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
        for (idx.x=0; idx.x < tile_indices.size.x; idx.x++, tref++)
          { 
            if ((tref->tile != NULL) && (tref->tile != KD_EXPIRED_TILE))
              { 
                abs_idx = idx + tile_indices.pos;
                assert(abs_idx == tref->tile->t_idx);
                delete tref->tile; // Invokes tile destructor.
                assert((tref->tile == NULL) ||
                       (tref->tile == KD_EXPIRED_TILE));
              }
          }
      delete[] tile_refs;  tile_refs = NULL;
    }
  
  kd_tile *typ;
  while ((typ=typical_tile_cache) != NULL)
    { 
      typical_tile_cache = typ->typical_next;
      assert(typ->tile_ref == NULL);
      delete typ;
    }
  
  // Delete other owned resources
  if (in != NULL)
    { delete in; in = NULL; }
  if (out != NULL)
    { delete out; out = NULL; }
  if (siz != NULL)
    { delete siz; siz = NULL; }
  if (marker != NULL)
    { delete marker; marker = NULL; }
  if (ppm_markers != NULL)
    { delete ppm_markers; ppm_markers = NULL; }
  if (precinct_server != NULL)
    { delete precinct_server; precinct_server = NULL; }
  if (block != NULL)
    { delete block;  block = NULL; }
  if (tpart_ptr_server != NULL)
    { delete tpart_ptr_server; tpart_ptr_server = NULL; }
  while ((comtail=comhead) != NULL)
    { 
      comhead = comtail->next;
      delete comtail;
    }
  if (layer_sizes != NULL)
    { delete[] layer_sizes; layer_sizes = NULL; }
  if (tmp_layer_sizes != NULL)
    { delete[] tmp_layer_sizes; tmp_layer_sizes = NULL; }
  if (target_sizes != NULL)
    { delete[] target_sizes; target_sizes = NULL; }
  if (expected_sizes != NULL)
    { delete[] expected_sizes; expected_sizes = NULL; }
  if (target_min_sizes != NULL)
    { delete[] target_min_sizes; target_min_sizes = NULL; }
  if (layer_thresholds != NULL)
    { delete[] layer_thresholds; layer_thresholds = NULL; }
  if (target_thresholds != NULL)
    { delete[] target_thresholds; target_thresholds = NULL; }
  while ((global_mct_tail=global_mct_head) != NULL)
    { 
      global_mct_head = global_mct_tail->next_stage;
      delete global_mct_tail;
    }
  if (limiter != NULL)
    { delete limiter; limiter = NULL; }
  cbr_fset.cleanup();
}

/*****************************************************************************/
/*                      kd_codestream::~kd_codestream                        */
/*****************************************************************************/

kd_codestream::~kd_codestream()
{
  // If we have come here after handling an exception and destroying a
  // multi-threaded environment, we may still have a non-NULL `thread_context'
  // object with links into `kd_resolution' objects.  It is safest, therefore,
  // to execute `stop_multi_threading' first.  Before doing this, we should
  // also remove any pending precincts, so we will do the first line of
  // `delete_and_reset_all_but_buffering_and_threading' redundantly here, and
  // then stop any multi-threading, before actually deleting anything.
  close_pending_precincts(); // Cleanly remove any precincts which happen to
                             // still be on the `pending_precincts' list.
  if (thread_context != NULL)
    stop_multi_threading();
  
  delete_and_reset_all_but_buffering_and_threading();
  
  // Finish by cleaning up resources which are affected by multi-threading
  if (thread_context != NULL)
    stop_multi_threading();
  if (rate_stats[0] != NULL)
    { delete rate_stats[0];  rate_stats[0] = NULL; }
  if (buf_servers != NULL)
    { 
      buf_servers[0].cleanup_and_detach();
      delete[] buf_servers;
      buf_servers = NULL;
    }
  if (buf_master != NULL)
    buf_master->detach_codestream(); // Also deletes it, if appropriate
  if (aux_flush_stats != NULL)
    { 
      aux_flush_stats->remove_ref();
      aux_flush_stats = NULL;      
    }
  if (flush_stats != NULL)
    { 
      flush_stats->remove_ref();
      flush_stats = NULL;
    }
}

/*****************************************************************************/
/*                   kd_codestream::stop_multi_threading                     */
/*****************************************************************************/

void
  kd_codestream::stop_multi_threading(kdu_thread_env *caller)
{ // Remember: `caller' might be NULL
  if (thread_context == NULL)
    return;
  thread_context->leave_group(caller);
  if (rate_stats[0] != NULL)
    { 
      rate_stats[0]->set_next(NULL);
      int t, num_threads = thread_context->manage_compressed_stats(NULL);
      for (t=1; t <= num_threads; t++)
        if (rate_stats[t] != NULL)
          { 
            rate_stats[0]->transcribe(rate_stats[t]);
            delete rate_stats[t];
            rate_stats[t] = NULL;
          }
    }
  if (buf_servers != NULL)
    { 
      int t, num_threads = thread_context->manage_buf_servers(NULL);
      for (t=1; t <= num_threads; t++)
        buf_servers[t].cleanup_and_detach();
    }
  kd_cs_thread_context *ctxt = thread_context;
  thread_context = NULL;
  ctxt->clear_queues(NULL);
  delete ctxt;
}

/*****************************************************************************/
/*                         kd_codestream::create_tile                        */
/*****************************************************************************/

kd_tile *
  kd_codestream::create_tile(kdu_coords idx)
{
  kdu_coords rel_idx = idx - tile_indices.pos;
  assert((rel_idx.x >= 0) && (rel_idx.x < tile_indices.size.x) &&
         (rel_idx.y >= 0) && (rel_idx.y < tile_indices.size.y));
  kd_tile_ref *tref = tile_refs + rel_idx.x + rel_idx.y*tile_indices.size.x;
  kd_tile *tp = tref->tile;
  assert(tp == NULL);

  kdu_dims tile_dims = tile_partition; // `tile_partition' is not affected by
                                       // fragment regions
  tile_dims.pos.x += idx.x*tile_dims.size.x;
  tile_dims.pos.y += idx.y*tile_dims.size.y;

  tile_dims &= canvas;
  assert(!tile_dims.is_empty());
  if ((in != NULL) && (!persistent) && (!allow_restart) &&
      !tile_dims.intersects(region))
    { // Don't even bother creating the tile; it will never be accessed.  Just
      // mark it as expired and return.
      return (tref->tile = KD_EXPIRED_TILE);
    }

  if (typical_tile_cache != NULL)
    { // Try re-using an element from the typical tile cache
      tp = tref->tile = typical_tile_cache;
      typical_tile_cache = tp->typical_next;
      tp->recycle(tref,idx,tile_dims);
                    // Similar to `kd_tile::initialize', but tries to
                    // reuse the tile's data structures
    }
  else
    {
      tp = tref->tile = new kd_tile(this,tref,idx,tile_dims);
      tp->initialize();
    }
  return tref->tile;
}

/*****************************************************************************/
/*                   kd_codestream::trim_compressed_data                     */
/*****************************************************************************/

void
  kd_codestream::trim_compressed_data(kdu_thread_env *env)
{
  if (rate_stats[0] == NULL)
    return;
  
  kdu_uint16 threshold = rate_stats[0]->get_conservative_slope_threshold(true);
  if (threshold <= 1)
    return;
  if (env != NULL)
    { 
      acquire_lock(KD_THREADLOCK_GENERAL,env);
      process_pending_precincts();
    }

  // Work through all the ready precincts
  int c, d=max_depth;
  kd_global_rescomp *rc = global_rescomps + d*num_components;
  kd_precinct *precinct;
  for (; d >= 0; d--, rc-=(num_components<<1))
    for (c=0; c < num_components; c++, rc++)
      for (precinct=rc->first_ready; precinct!=NULL; precinct=precinct->next)
        for (int b=0; b < precinct->resolution->num_subbands; b++)
          {
            kd_precinct_band *pb = precinct->subbands + b;
            for (int n=0; n < (int) pb->block_indices.area(); n++)
              {
                kd_block *block = pb->blocks + n;
                block->trim_data(threshold,buf_servers);
              }
          }
  
  if (env != NULL)
    release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*               kd_codestream::get_main_and_tile_header_cost                */
/*****************************************************************************/

kdu_long
  kd_codestream::get_main_and_tile_header_cost()
{
  if (main_and_tile_header_cost >= 0)
    { 
      assert((main_and_tile_header_cost > 0) ||
             header_generated || !initial_fragment);
      return main_and_tile_header_cost;
    }
  main_and_tile_header_cost = 0;
  if (initial_fragment && !header_generated)
    { 
      main_and_tile_header_cost = 2 +  // 2 for the SOC marker
        siz->generate_marker_segments(NULL,-1,0);
      if (!comments_frozen)
        freeze_comments();
      kd_codestream_comment *comscan;
      for (comscan=comhead; comscan != NULL; comscan=comscan->next)
        main_and_tile_header_cost += comscan->write_marker(NULL);
      main_and_tile_header_cost += reserved_layer_info_bytes;
    }
  
  // Calculate the cost of including tile-part headers.  This calculation
  // may be off by a bit if the `ORGtparts' attribute has been used to request
  // extra tile-parts, or if `ORGgen_plt' has been used to request the
  // generation of PLT marker segments.
  kd_tile *tile;
  int num_tiles_in_progress=0;
  for (tile=tiles_in_progress_head; tile != NULL; tile=tile->in_progress_next)
    { 
      num_tiles_in_progress++;
      if (cached_target && (tile->next_tpart != 0))
        continue; // The one and only tile-part header has been written already
                  // during a previous incremental flush
      main_and_tile_header_cost += 12 + 2 + // For SOT and SOD
        siz->generate_marker_segments(NULL,tile->t_num,tile->next_tpart);
    }
  assert(num_tiles_in_progress <= num_incomplete_tiles);
  main_and_tile_header_cost +=
    (12 + 2) * (num_incomplete_tiles - num_tiles_in_progress);
  return main_and_tile_header_cost;
}

/*****************************************************************************/
/*                       kd_codestream::simulate_output                      */
/*****************************************************************************/

kdu_long
  kd_codestream::simulate_output(kdu_long &total_packet_header_bytes,
                                 int layer_idx, int reslength_layer_idx,
                                 kdu_uint16 slope_threshold,
                                 bool finalize_layer, bool last_layer,
                                 kdu_long max_bytes, kdu_long *sloppy_bytes)
{
  if (reslength_constraints_used && !reslength_warning_issued)
    { 
      int c;
      if (reslength_checkers != NULL)
        for (c=0; c <= num_components; c++)
          reslength_checkers[c].set_layer(reslength_layer_idx);
      for (kd_tile *tile=tiles_in_progress_head;
           tile != NULL; tile=tile->in_progress_next)
        if (tile->reslength_checkers != NULL)
          for (c=0; c <= num_components; c++)
            tile->reslength_checkers[c].set_layer(reslength_layer_idx);
    }

  // Figure out fixed fixed header costs
  total_packet_header_bytes = 0;
  kdu_long total_bytes = 0;
  if (layer_idx == 0)
    total_bytes = get_main_and_tile_header_cost();
  
  // See if we can make a premature exit
  if (total_bytes > max_bytes)
    {
      assert(!finalize_layer);
      return total_bytes;
    }

  // Now work through all the ready precincts, component by component,
  // starting from the lowest resolution and moving upwards so that we
  // can compute `attributed_area' values to pass to higher resolutions
  // where a lower resolution has no ready precincts.
  for (int c=0; c < num_components; c++)
    { 
      int d=max_depth;
      kdu_long attributed_area=0;
      kd_global_rescomp *rc = global_rescomps + c + d*num_components;
      for (; d >= 0; d--, rc-=num_components)
        { 
          if (rc->expected_area < 0)
            { // Need to recompute the `expected_area' 
              kdu_long T = rc->total_area - rc->area_covered_by_tiles;
              if (T > 0)
                { 
                  assert(T <= (rc->remaining_area + rc->ready_area));
                  double frac = 1.0;
                  if (rc->area_used_by_tiles > 0)
                    frac = (((double) rc->area_used_by_tiles) /
                            ((double) rc->area_covered_by_tiles));
                  rc->expected_area = rc->remaining_area - T
                                    + (kdu_long) ceil(frac * (double) T);
                }
              else
                rc->expected_area = rc->remaining_area;
              rc->ready_fraction = rc->reciprocal_fraction = -1.0;
            }
          
          kd_precinct *precinct = rc->first_ready;
          if (precinct == NULL)
            { // May need to augment attributed area
              assert(rc->ready_area == 0);
              attributed_area += rc->expected_area;
              continue;
            }
          assert(rc->ready_area > 0);
          
          // Find ready/reciprocal fraction scaling factors
          if (attributed_area != rc->attributed_area)
            { 
              rc->attributed_area = attributed_area;
              rc->ready_fraction = rc->reciprocal_fraction = -1.0;
            }
          attributed_area = 0; // All attributed area has been reconciled here
          kdu_long ref_area = rc->expected_area + rc->attributed_area;
          bool no_scaling = (rc->ready_area == ref_area);
          if (rc->ready_fraction < 0.0)
            { // Need to recompute ready/reciprocal fractions
              assert(ref_area >= rc->ready_area);
              if (no_scaling)
                rc->ready_fraction = rc->reciprocal_fraction = 1.0;
              else
                { 
                  rc->ready_fraction =
                    ((double) rc->ready_area) / ((double) ref_area);
                  if (rc->ready_fraction > 1.0)
                    rc->ready_fraction = 1.0; // Just in case
                  rc->reciprocal_fraction = 1.0 / rc->ready_fraction;
                }
            }

          // Adjust target sizes to account for scaling
          kdu_long local_max_bytes = max_bytes-total_bytes;
          kdu_long local_sloppy = (sloppy_bytes!=NULL)?(*sloppy_bytes):0;
          if (!no_scaling)
            { 
              local_max_bytes = (kdu_long)(local_max_bytes*rc->ready_fraction);
              local_sloppy = ((kdu_long)(local_sloppy*rc->ready_fraction)) - 1;
              if (local_sloppy < 0)
                local_sloppy = 0;
            }
          kdu_long initial_local_sloppy = local_sloppy;
          kdu_long local_header_bytes=0, local_total_bytes=0;

          // Now size the packets using scaled sizes
          kdu_long packet_header_bytes, packet_bytes;
          for (; precinct != NULL; precinct=precinct->next)
            { 
              if (precinct->next_layer_idx != 0)
                { KDU_ERROR_DEV(e,37); e <<
                  KDU_TXT("Attempting to run rate-control simulation "
                          "on a precinct for which one or more packets have "
                          "already been written to the code-stream.  Problem "
                          "is most likely caused by trying to use the "
                          "incremental code-stream flushing feature with one "
                          "of the progression orders, LRCP or RLCP.");
                }
              if (layer_idx >=
                  precinct->resolution->tile_comp->tile->num_layers)
                continue;
              packet_bytes = packet_header_bytes = 0;
              if (sloppy_bytes != NULL)
                { 
                  assert(finalize_layer && last_layer && (local_sloppy >= 0));
                  assert(slope_threshold < 0xFFFF);
                  packet_bytes =
                    precinct->simulate_packet(packet_header_bytes,layer_idx,
                                              slope_threshold+1,false,true);
                  kdu_long packet_max_bytes = packet_bytes + local_sloppy;
                  if (packet_max_bytes > (local_max_bytes-local_total_bytes))
                    packet_max_bytes = local_max_bytes-local_total_bytes;
                  assert(packet_max_bytes >= packet_bytes);
                  packet_bytes =
                    precinct->simulate_packet(packet_header_bytes,layer_idx,
                                              slope_threshold,true,true,
                                              packet_max_bytes,true);
                  assert(packet_bytes <= packet_max_bytes);
                  local_sloppy = packet_max_bytes - packet_bytes;
                }
              else
                packet_bytes =
                  precinct->simulate_packet(packet_header_bytes,layer_idx,
                                            slope_threshold,finalize_layer,
                                            last_layer,
                                            local_max_bytes-local_total_bytes);
              local_total_bytes += packet_bytes;
              local_header_bytes += packet_header_bytes;
              if (reslength_constraints_used && !reslength_warning_issued)
                for (int glob=0; glob < 2; glob++)
                  { 
                    kd_tile *tile = precinct->resolution->tile_comp->tile;
                    kd_reslength_checker *checkers =
                      (glob)?this->reslength_checkers:tile->reslength_checkers;
                    if (checkers != NULL)
                      { 
                        if (!checkers[0].check_packet(packet_bytes,d))
                          reslength_constraints_violated = true;
                        if (!checkers[c+1].check_packet(packet_bytes,d))
                          reslength_constraints_violated = true;
                      }
                  }
              if (local_total_bytes > local_max_bytes)
                { 
                  assert(!finalize_layer);
                    // Otherwise, you are not driving this function correctly
                  break;
                }
            }

          // Now scale the packet sizes back up again
          if (no_scaling)
            { 
              total_packet_header_bytes += local_header_bytes;
              total_bytes += local_total_bytes;
              if (sloppy_bytes != NULL)
                *sloppy_bytes = local_sloppy;
            }
          else
            { 
              total_packet_header_bytes +=
                1 + (kdu_long)(local_header_bytes*rc->reciprocal_fraction);
              total_bytes +=
                1 + (kdu_long)(local_total_bytes*rc->reciprocal_fraction);
              if (sloppy_bytes != NULL)
                { 
                  kdu_long delta_sloppy = initial_local_sloppy - local_sloppy;
                  delta_sloppy =
                    1 + (kdu_long)(delta_sloppy * rc->reciprocal_fraction);
                  *sloppy_bytes -= delta_sloppy;
                  if (*sloppy_bytes < 0)
                    *sloppy_bytes = 0;
                }
            }
          if (total_bytes > max_bytes)
            return total_bytes;
        }
    }
  
  return total_bytes;
}

/*****************************************************************************/
/*            kd_codestream::check_incremental_flush_consistency             */
/*****************************************************************************/

void
  kd_codestream::check_incremental_flush_consistency(int num_layer_specs)
{
  assert((layer_thresholds != NULL) && (layer_sizes != NULL) &&
         (target_sizes != NULL) && (expected_sizes != NULL));
    // Above arrays should have been created on first call to flush/auto-flush
  if (num_layer_specs != this->num_sized_layers)
    { KDU_ERROR_DEV(e,59); e <<
      KDU_TXT("When generating code-stream output incrementally, each call "
              "to `kdu_codestream::flush' or `kdu_codestream::auto_flush' "
              "must provide the same number of quality layer specifications.");
    }
  if (this->reslength_constraints_used && !this->reslength_warning_issued)
    { KDU_WARNING_DEV(w,0x10110800); w <<
      KDU_TXT("You cannot currently use the `Creslength' parameter "
              "attribute in conjunction with incremental flushing of "
              "the codestream.  Ignoring the `Creslength' "
              "constraints.");
      this->reslength_warning_issued = true;
    }
}

/*****************************************************************************/
/*                   kd_codestream::construct_cbr_flush_set                  */
/*****************************************************************************/

bool
  kd_codestream::construct_cbr_flush_set()
{
  if (cbr_fset.active_tile == NULL)
    { 
      if (cbr_flush_tile == 0)
        { // This is the very first call to this function
          cbr_fset.num_flushed_precincts = 0;
          cbr_fset.num_precinct_simulations = 0;
          
          if (num_sized_layers != 1)
            { KDU_ERROR(e,0x24081601); e <<
              KDU_TXT("The `Scbr' option is not currently compatible with the "
                      "generation of more than one sized quality layer.");
            }
          cbr_fset.cs_target_bytes = target_sizes[0];
          if (cbr_fset.cs_target_bytes <= header_length)
            { KDU_ERROR(e,0x24081602); e <<
              KDU_TXT("The `Scbr' option can only be used when a non-zero "
                      "target codestream length (bytes) is supplied to one "
                      "or another of the flush functions.  You might have "
                      "forgotten to supply a `-rate' argument to the "
                      "compression tool, or the supplied or derived "
                      "codestream length may be too small to accommodate even "
                      "the main header.");
            }
          kdu_dims first_comp_dims = comp_info[0].scale_region(this->region,0);
          cbr_fset.cs_total_samples = first_comp_dims.area();
          for (int c=1; c < num_components; c++)
            cbr_fset.cs_total_samples += 
              comp_info[c].scale_region(this->region,0).area();
          cbr_fset.data_rate = (((double)cbr_fset.cs_target_bytes) /
                                ((double)cbr_fset.cs_total_samples));
          cbr_fset.max_bucket_fulness = cbr_max_bucket_lines;
          cbr_fset.max_bucket_fulness *= (double)first_comp_dims.size.x;
          cbr_fset.max_bucket_fulness *= (double)cbr_fset.cs_total_samples;
          cbr_fset.max_bucket_fulness *= 1.0 / (double)first_comp_dims.area();
          cbr_fset.max_bucket_fulness *= cbr_fset.data_rate;
          cbr_fset.bucket_fulness = (double)header_length;
          cbr_fset.cs_flushed_bytes = header_length;
          cbr_fset.cs_flushed_samples = 0;
          cbr_fset.nominal_flush_set_samples = 0;
          if (cbr_fset.bucket_fulness > cbr_fset.max_bucket_fulness)
            { KDU_ERROR(e,0x24081605); e <<
              KDU_TXT("The Bmax (second) parameter supplied with `Scbr' is "
                      "way too small!  Not large enough to accommodate even "
                      "the main codestream header, which is part of the "
                      "initial content of the \"leaky bucket\" that forms "
                      "part of the CBR channel model.");
            }
          cbr_fset.num_flush_sets = 0;
          cbr_fset.min_slope_threshold = 0xFFFF;
          cbr_fset.max_slope_threshold = 0;
          cbr_fset.acc_slope_threshold = 0;
          cbr_fset.acc_sq_slope_threshold = 0;
          cbr_fset.acc_bucket_fulness = 0;
          cbr_fset.non_final_fill_bytes = 0;
        }
      int num_tiles = tile_span.x*tile_span.y;
      if (cbr_flush_tile >= num_tiles)
        return false;
      kd_tile_ref *tref = tile_refs + cbr_flush_tile;
      kd_tile *tp = tref->tile;
      if (tp == NULL)
        return false; // This might happen if tiles generated out of order
      cbr_fset.active_tile = tp;
      cbr_fset.total_tile_samples = 0;
      for (int c=0; c < num_components; c++)
        cbr_fset.total_tile_samples +=
          comp_info[c].scale_region(tp->region,0).area();
      kdu_long bytes_left =
        cbr_fset.cs_target_bytes - cbr_fset.cs_flushed_bytes;
      bytes_left -= 2; // Allow for the EOC marker at the very end
      kdu_long samples_left =
        cbr_fset.cs_total_samples - cbr_fset.cs_flushed_samples;
      cbr_fset.planned_tile_bytes = bytes_left;

      tp->next_tpart = 1; // Makes sure packet sequencer does the right thing
      int hdr_bytes = 12 + 2 + siz->generate_marker_segments(NULL,tp->t_num,0);

      int tiles_left = num_tiles - cbr_flush_tile;
      if (tiles_left == 1)
        assert(samples_left == cbr_fset.total_tile_samples);
      else
        { 
          bytes_left -= hdr_bytes*tiles_left; // Assume all tile hdrs same size
          double frac = (((double)cbr_fset.total_tile_samples) /
                         ((double)samples_left));
          cbr_fset.planned_tile_bytes = hdr_bytes +
            (kdu_long)(frac*(double)bytes_left);
        }
      cbr_fset.flushed_tile_samples = 0;
      cbr_fset.flushed_tile_bytes = 0;
      cbr_fset.res_boundary = tp->comps[0].resolutions;
      cbr_fset.next_pref =
        tp->sequencer->next_in_sequence(cbr_fset.next_res,
                                        cbr_fset.next_rel_idx);
      assert(cbr_fset.next_pref != NULL);
      assert(cbr_fset.next_res == cbr_fset.res_boundary);
      cbr_fset.y_boundary_increment = cbr_flush_interval;
      cbr_fset.y_boundary =
        cbr_fset.next_rel_idx.y + cbr_fset.y_boundary_increment;
      cbr_fset.last_set_in_codestream = cbr_fset.last_set_in_tile = false;
      cbr_fset.flush_set_samples = 0;
      cbr_fset.flush_set_bytes = hdr_bytes; // We simulated these above
      cbr_fset.num_precincts = 0;
      cbr_fset.last_non_empty_precinct = 0;
      
      // Do some checking for incompatible tile configurations
      if (tp->num_layers != 1)
        { KDU_ERROR(e,0x27081601); e <<
          KDU_TXT("The `Clayers' attribute must be exactly 1 when using the "
                  "`Scbr' low latency CBR flushing option.");
        }
      if (tp->sequencer->get_order() != Corder_PCRL)
        { KDU_ERROR(e,0x24081606); e <<
          KDU_TXT("The `Scbr' option may only be used in connection with "
                  "the `Corder_PCRL' packet progression order.");
        }
      if (tp->insert_plt_segments || tp->resolution_tparts ||
          tp->component_tparts || tp->layer_tparts)
        { KDU_ERROR(e,0x24081607); e <<
          KDU_TXT("The `Scbr' option may not be used together with "
                  "`ORGgen_plt' or `ORGtparts' options.");
        }
    }
  
  // Now we can add precincts to the flush set until either it is complete or
  // we encounter one that is not yet ready to flush.
  kd_tile *tp = cbr_fset.active_tile;
  while ((cbr_fset.next_pref != NULL) &&
         ((cbr_fset.next_res != cbr_fset.res_boundary) ||
          (cbr_fset.next_rel_idx.y < cbr_fset.y_boundary)))
    { 
      kd_precinct_ref *pref = cbr_fset.next_pref;
      kd_precinct *precinct = pref->deref();
      if ((precinct == NULL) || !(precinct->flags & KD_PFLAG_READY))
        return false;
      if (cbr_fset.num_precincts >= cbr_fset.max_precincts)
        { // Need to enlarge the array
          assert(cbr_fset.num_precincts == cbr_fset.max_precincts);
          int new_max = cbr_fset.num_precincts + 32;
          kd_precinct_flush_info *new_precs =
            new kd_precinct_flush_info [new_max];
          for (int p=0; p < cbr_fset.num_precincts; p++)
            new_precs[p] = cbr_fset.precincts[p];
          if (cbr_fset.precincts != NULL) delete[] cbr_fset.precincts;
          cbr_fset.precincts = new_precs;
          cbr_fset.max_precincts = new_max;
        }
      kd_precinct_flush_info *pinf = cbr_fset.precincts+cbr_fset.num_precincts;
      cbr_fset.num_precincts++;
      pinf->precinct = precinct;
      pinf->last_sim_threshold = 0xFFFF;
      pinf->last_mod_threshold = 0xFFFF;
      pinf->next_slope_val = 0;
      
      // Calculate number of samples contributed by the precinct
      kd_resolution *res = cbr_fset.next_res;
      kdu_coords abs_idx = res->precinct_indices.pos + cbr_fset.next_rel_idx;
      kdu_dims dims = res->precinct_partition;
      dims.pos.x += dims.size.x*abs_idx.x;
      dims.pos.y += dims.size.y*abs_idx.y;
      dims &= res->node.dims;
      kdu_long prec_samples = dims.area();
      if (res->res_level != 0)
        { // Need to subtract the number of samples passed to lower resolution
          int min_y=dims.pos.y, min_x=dims.pos.x;
          int lim_y=min_y+dims.size.y, lim_x=min_x+dims.size.x;
          if (res->node.children[LH_BAND] != NULL)
            { min_y = (min_y+1)>>1;  lim_y = (lim_y+1)>>1; }
          if (res->node.children[HL_BAND] != NULL)
            { min_x = (min_x+1)>>1;  lim_x = (lim_x+1)>>1; }
          kdu_long low_samples = lim_y-min_y;  low_samples *= (lim_x-min_x);
          prec_samples -= low_samples;
        }
      cbr_fset.flush_set_samples += prec_samples;
      if (prec_samples > 0)
        cbr_fset.last_non_empty_precinct = cbr_fset.num_precincts-1;
        
      // Advance to the next precinct
      precinct->next_layer_idx = tp->num_layers; // Restore this later
      tp->sequenced_relevant_packets++;
      cbr_fset.next_pref =
        tp->sequencer->next_in_sequence(cbr_fset.next_res,
                                        cbr_fset.next_rel_idx);
    }

  // If we get here, we have a complete flush-set.  Reset the `next_layer_idx'
  // members of each associated precinct to 0 now, but leave the
  // `tp->sequence_relevant_packets' member, since we will not repeat the
  // sequencing process.
  kd_precinct_flush_info *pinf = cbr_fset.precincts;
  for (int p=cbr_fset.num_precincts; p > 0; p--, pinf++)
    pinf->precinct->next_layer_idx = 0;

  // Flag and check the flush-set status
  if ((cbr_fset.flushed_tile_samples+cbr_fset.flush_set_samples) ==
      cbr_fset.total_tile_samples)
    cbr_fset.last_set_in_tile = true;
  if ((cbr_fset.cs_flushed_samples + cbr_fset.flush_set_samples) ==
      cbr_fset.cs_total_samples)
    cbr_fset.last_set_in_codestream = true;
  assert(cbr_fset.last_set_in_tile || !cbr_fset.last_set_in_codestream);
  if (cbr_fset.nominal_flush_set_samples == 0)
    cbr_fset.nominal_flush_set_samples = cbr_fset.flush_set_samples;
  else if ((!cbr_fset.last_set_in_codestream) &&
           (cbr_fset.flush_set_samples != cbr_fset.nominal_flush_set_samples))
    { KDU_ERROR(e,0x01091603); e <<
      KDU_TXT("`Scbr' parameters do not result in flush-sets which all "
              "have the same size -- this invalidates the CBR channel "
              "model on which the `Scbr' low latency flushing process "
              "is based.  You may be receiving this error because image "
              "components have different dimensions and the `Cprecincts' "
              "and/or `Clevels' attributes have not been configured on "
              "a per-component basis to ensure that all components have "
              "identical natural flush-set boundaries; note that the "
              "`Scbr' attribute's first parameter V defines the flushing "
              "interval based on the 1st image component.");
    }

  // Reset fill bytes indicators
  cbr_fset.primary_bulk_fill_bytes = 0;
  cbr_fset.secondary_bulk_fill_bytes = 0;
  cbr_fset.fine_fill_bytes = 0;

  // Finish by checking for tiling compatibility
  if (cbr_fset.next_pref == NULL)
    { // The tile should be complete
      assert(cbr_fset.last_set_in_tile);
      assert(tp->sequenced_relevant_packets == tp->max_relevant_packets);
    }
  else
    { // There are more flush-sets in this tile
      assert(!cbr_fset.last_set_in_tile);
      assert((cbr_fset.flush_set_samples+cbr_fset.flushed_tile_samples) <
             cbr_fset.total_tile_samples);
      if (tile_span.y != 1)
        { KDU_ERROR(e,0x29081602); e <<
          KDU_TXT("Unsupported combination of `Scbr' attribute with "
                  "vertical tiling.  Currently, we allow vertical tiling "
                  "only if each flush set spans an entire tile, but your "
                  "flush sets are smaller than this, while you have multiple "
                  "vertical tiles.  This combination might be supported in "
                  "the future, but it requires tile lengths to be predicted "
                  "exactly, so that the tile-part header can be written "
                  "before the content that is to be written to the tile-part "
                  "body is even available.  To predict flushed content length "
                  "exactly requires us to be able to use fill/spacer bytes "
                  "with absolute precision, which may occasionally require "
                  "complex packet header manipulations.  Although these are "
                  "possible, introducing them does not currently appear to "
                  "have much real practical value.");
        }
    }  
  return true;
}

/*****************************************************************************/
/*                       kd_codestream::ready_for_flush                      */
/*****************************************************************************/

bool
  kd_codestream::ready_for_flush()
{ // General mutex must be locked before calling this function
  if (cbr_flush_interval > 0)
    return true; // No harm comes from going straight into a flush in this case
  if (cached_target)
    { // We need at least to ensure that for each image component, if there
      // are any resolutions that have remaining area but no ready
      // precincts, there is a higher resolution that has at least one
      // ready precinct.
      int c, d;
      for (c=0; c < num_components; c++)
        { 
          kd_global_rescomp *rc = global_rescomps + c;
          for (d=0; d <= max_depth; d++, rc += num_components)
            { 
              if (rc->first_ready)
                break; // This one can cover all lower resolutions
              if (rc->remaining_area > 0)
                return false;
            }
        }
      
      // Additionally, if the function is being called automatically, we
      // need to make sure that we are not too close to the end.
      if (incr_flush_interval <= 0)
        return true;

      int nt=(transpose)?tile_indices.size.y:tile_indices.size.x;
      kdu_long outstanding_lines_needed = 1 + (incr_flush_interval/(2*nt));
      kdu_long outstanding_area=0, outstanding_lines=0;
      for (c=0; c < num_components; c++)
        { 
          kd_comp_info *ci = comp_info + c;
          int subs = (transpose)?ci->sub_sampling.y:ci->sub_sampling.x;
          kd_global_rescomp *rc = global_rescomps + c;
          for (d=0; d <= max_depth; d++, rc += num_components)
            { 
              kdu_long area = rc->remaining_area - rc->ready_area;
              outstanding_area += area;
              int dim = (transpose)?rc->size.y:rc->size.x;
              if (dim <= 0)
                continue;
              outstanding_lines += subs * (area / dim);
              if (outstanding_lines >= outstanding_lines_needed)
                return true;
            }
        }
      if (outstanding_area == 0)
        return true;
    }
  else
    { 
      kd_tile *tp=tiles_in_progress_head;
      for (; tp != NULL; tp=tp->in_progress_next)
        { 
          kd_precinct *precinct;
          kd_precinct_ref *p_ref;
          kd_resolution *p_res;
          kdu_coords p_idx;
          p_ref = tp->sequencer->next_in_sequence(p_res,p_idx);
          if (p_ref != NULL)
            { 
              precinct = p_ref->deref();
              if ((precinct != NULL) &&
                  (precinct->num_outstanding_blocks.get() == 0))
                return true;
            }
        }
    }
  
  return false;
}

/*****************************************************************************/
/*                       kd_codestream::flush_if_ready                       */
/*****************************************************************************/

void
  kd_codestream::flush_if_ready(kdu_thread_env *env)
{
  assert((num_sized_layers > 0) && (layer_thresholds != NULL) &&
         (layer_sizes != NULL) && (target_sizes != NULL) &&
         (expected_sizes != NULL));
  if (env != NULL)
    { 
      acquire_lock(KD_THREADLOCK_GENERAL,env);
      process_pending_precincts();
    }
  if ((cbr_flush_interval <= 0) && !ready_for_flush())
    { 
      if (env != NULL)
        release_lock(KD_THREADLOCK_GENERAL,env);
      return;
    }
  
  // Reset the header cost state variable so that new header costs will
  // be computed when `get_main_and_tile_header_cost' is next called.
  main_and_tile_header_cost = -1;
  
    
  int num_layers = num_sized_layers;
  bool last_layer_takes_all=false; // If last layer has all remaining data
  bool flushed_all = false; // Becomes true if this function completes the
                            // flushing of all content for the codestream.
  if (cbr_flush_interval > 0)
    { // Everything is quite different in this case
      if (!header_generated)
        { 
          if (cached_target)
            { KDU_ERROR(e,0x24081603); e <<
              KDU_TXT("The `Scbr' option requires you to supply a sequential "
                      "target device for the generated codestream.");
            }
          if (!initial_fragment)
            { KDU_ERROR(e,0x24081604); e <<
              KDU_TXT("The `Scbr' option is not compatible with fragmented "
                      "compression.");
            }
          generate_main_header();
        }
      while (construct_cbr_flush_set())
        { 
          pcrd_opt_cbr();
          if (write_packets_cbr())
            { 
              flushed_all = true;
              break;
            }
        }
      out->flush();
      if (cbr_fset.num_flush_sets > 0)
        layer_thresholds[0] = (kdu_uint16)
          (cbr_fset.acc_slope_threshold / cbr_fset.num_flush_sets);
    }
  else
    { // Regular codestream flush operation

      // Set up comment segment if required
      reserved_layer_info_bytes = 0;
      if (record_in_comseg && initial_fragment && !header_generated)
        set_reserved_layer_info_bytes(num_sized_layers);

      // Start with simulation phase -- depends upon whether we are doing
      // a flush or a trans_out operation.
      int n;
      if (trans_out_max_bytes == 0)
        { // Doing `flush', not `trans_out'
          if (using_slopes)
            { // Use slope thresholds to generate the layers directly
              for (n=0; n < num_layers; n++)
                layer_thresholds[n] = target_thresholds[n];
              
              bool have_constraints = (reslength_constraints_used &&
                                       !reslength_warning_issued);
              if (have_constraints || using_min_sizes)
                pcrd_trim(!cached_target);
              else if (!cached_target)
                { // Need to find the actual byte counts so that PLT and
                  // tile-part lengths can be generated.  If we have a
                  // `cached_target', the byte counts and expected layer sizes
                  // are computed by the `cache_write_ready_packets' function.
                  kdu_long packet_header_bytes, total_bytes=0;
                  for (n=0; n < num_layers; n++)
                    { 
                      bool last_layer = (n == (num_layers-1));
                      kdu_uint16 thresh = layer_thresholds[n];
                      total_bytes += simulate_output(packet_header_bytes,n,n,
                                                     thresh,true,last_layer);
                      expected_sizes[n] = total_bytes + ((last_layer)?2:0);
                      // extra 2 bytes is for EOC marker
                    }
                }
            }
          else
            { // Do full PCRD-opt based on size targets
              last_layer_takes_all = (target_sizes[num_layers-1] == 0);
              if (trim_to_rate)
                { // Check if we are allowed to trim to the exact rate yet
                  kd_global_rescomp *rc = global_rescomps;
                  int n=num_components*(1+max_depth);
                  for (; (n > 0) && trim_to_rate; n--, rc++)
                    if (rc->ready_area < rc->remaining_area)
                      trim_to_rate = false;
                }
              pcrd_opt(trim_to_rate,rate_tolerance);
            }
        }
      else
        { // Doing `trans_out', not `flush'
          kdu_long max_bytes = trans_out_max_bytes-2; // 2 bytes for EOC marker
          kdu_long total_bytes=0, packet_header_bytes=0;
          bool last_layer=true;
          if (trans_out_max_bytes < KDU_LONG_MAX)
            { // May need to truncate the existing layers
              for (n=0; n < num_layers; n++)
                max_bytes -= layer_sizes[n];
              do { // Run simulations until we find enough non-empty layers
                total_bytes = 0;
                for (n=0; n < num_layers; n++)
                  { 
                    last_layer = (n == (num_layers-1));
                    kdu_uint16 threshold = layer_thresholds[n];
                    if (last_layer)
                      threshold = 0xFFFF; // Just count the header cost
                    total_bytes += simulate_output(packet_header_bytes,n,n,
                                                   threshold,true,last_layer);
                    if (total_bytes >= max_bytes)
                      { 
                        if (last_layer)
                          { 
                            num_layers = n; // Couldn't even afford header cost
                            last_layer = false; // Make sure we iterate
                          }
                        else
                          num_layers = n+1; // Try current layer as last one,
                                            // counting cost of headers only.
                      }
                  }
                if (num_layers == 0)
                  { KDU_ERROR(e,60); e <<
                    KDU_TXT("You have set the byte limit too low.  "
                            "All compressed data would have to be discarded "
                            "in the call to `kdu_codestream::trans_out'!");
                  }
              } while (!last_layer);
            }
          
          if ((trans_out_max_bytes < KDU_LONG_MAX) || !cached_target)
            { // Go back through all the layers, finalizing the allocation and,
              // potentially, truncating the last quality layer.  We don't need
              // to do this if there is no possibility of truncation and if
              // the compressed data target is a structured cache; in that
              // case, the `expected_sizes' and all other relevant byte counts
              // will be filled out in the `cache_write_ready_precincts' call.
              total_bytes = 0;
              for (n=0; n < num_layers; n++)
                { 
                  last_layer = (n == (num_layers-1));
                  kdu_uint16 threshold = layer_thresholds[n];
                  if (!last_layer)
                    { 
                      total_bytes += simulate_output(packet_header_bytes,n,n,
                                                     threshold,true,
                                                     last_layer);
                      assert(total_bytes < max_bytes);
                    }
                  else
                    { // Last layer is a sloppy one
                      kdu_long trial_bytes = total_bytes +
                      simulate_output(packet_header_bytes,n,n,threshold+1,
                                      false,true);
                      assert(trial_bytes <= max_bytes); // Header cost only
                      kdu_long sloppy_bytes = max_bytes - trial_bytes;
                      total_bytes +=
                      simulate_output(packet_header_bytes,n,n,threshold,
                                      true,true,max_bytes-total_bytes,
                                      &sloppy_bytes);
                      assert(total_bytes <= max_bytes);
                    }
                  expected_sizes[n] = total_bytes + ((last_layer)?2:0);
                  // extra 2 bytes is for EOC marker
                }
            }
        }
  
      // Generate the code-stream
      if (cached_target)
        { // Writing output to a structured cache
          cache_write_ready_precincts(num_layers);
          if (reserved_layer_info_bytes && initial_fragment &&
              !header_generated)
            gen_layer_info_comment(num_layers,expected_sizes,layer_thresholds);
          flushed_all = cache_write_headers();
        }
      else
        { // Regular sequential codestream generation  
          if (reserved_layer_info_bytes && initial_fragment &&
              !header_generated)
            gen_layer_info_comment(num_layers,expected_sizes,layer_thresholds);
          flushed_all = generate_codestream(num_layers);
        }
    }
  
  if (flushed_all && (flush_stats != NULL))
    { // Update summary slope information in `flush_stats' and flag its
      // validity.
      int set_idx = flush_stats->active_set;
      flush_stats->summary_slope[set_idx] =
      layer_thresholds[num_sized_layers-1];
      int frame_idx = flush_stats->frame_valid[set_idx].get();
      assert((frame_idx & 0x80000000) == 0); // MSB set only once stats valid
      flush_stats->frame_valid[set_idx].barrier_set(frame_idx | 0x80000000);
    }

  // Finish up
  if (trans_out_max_bytes == 0)
    { 
      assert(num_layers == num_sized_layers);
      if (last_layer_takes_all)
        { // Make sure next incremental flush is not limited by current size
          target_sizes[num_layers-1] = 0;
        }
    }
  else
    { 
      target_sizes[num_sized_layers-1] = trans_out_max_bytes;
      if (num_layers > trans_out_non_empty_layers)
        trans_out_non_empty_layers = num_layers;
    }

  if (env != NULL)
    release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                   kd_codestream::find_slope_threshold                     */
/*****************************************************************************/

kdu_uint16
  kd_codestream::find_slope_threshold(int final_layer_idx, int unsized_layers,
                                      kdu_long min_bytes, kdu_long max_bytes,
                                      kdu_uint16 max_threshold,
                                      kdu_long initial_cumulative_bytes,
                                      kdu_uint16 suggested_threshold,
                                      kdu_long *simulated_layer_bytes)
{
  int sim_layer_idx = final_layer_idx-unsized_layers;
    // If `unsized_layers' > 0 we are trying to determine the threshold
    // for multiple layers, conservatively scaling the header cost by
    // 1+`unsized_layers'.  We need to make sure that `simulate_output'
    // believes it is working with the first of these layers since that is
    // the first one that has not yet been finalized.  However,
    // `final_layer_idx' will be passed as the `reslength_layer_idx' argument
    // to `simulate_output' so that the simulation is constrained by any
    // reslength specifications that apply to the sized layer.
  assert(sim_layer_idx >= 0);
  bool last_layer = (sim_layer_idx == (num_sized_layers-1));
  kdu_long packet_header_bytes=0, layer_bytes;
  kdu_uint16 threshold;
  kdu_uint16 min_threshold=0;
  if (rate_stats[0] != NULL)
    min_threshold = rate_stats[0]->get_pcrd_opt_min_threshold();
  kdu_uint16 best_threshold=max_threshold; // Most compatible so far
  kdu_long best_layer_bytes=-1;
  bool force_bisection_search=false; // Becomes true if we find that
             // `reslength' constraints are violated by a slope threshold that
             // does not violate the `max_bytes' constraint.
  
  // The following members are used to form good estimates of the next
  // threshold to try; they are not used if `use_bisection_search' is true.
  // The `below' and `upper' values correspond to exclusive lower and upper
  // bounds on the slope threshold that have been discovered, along with
  // their cumulative lengths (expressed as logs); with -ve values used for
  // bounds that have not yet been discovered.
  int thresh_below=-1, thresh_above=-1;
  double log_cum_bytes_below=-1.0, log_cum_bytes_above=-1.0;
  double log_cum_target_bytes =
    log(0.5*((double)(min_bytes+max_bytes+2*initial_cumulative_bytes)));
  if (initial_cumulative_bytes > 0)
    { 
      thresh_above = (int) max_threshold;
      log_cum_bytes_above = log((double) initial_cumulative_bytes);
    }
  double threshold_gradient = - 600.0 / log(2.0);
    // Estimate of the dS/dL, where S is the slope threshold and L is
    // log(layer_bytes).  The initial value here is based on the
    // observation that the compressed size changes roughly by a factor
    // of 2 if the slope threshold (already expressed in the log domain)
    // changes by 600.
  
  while (true)
    { 
      if (suggested_threshold != 0)
        { // Initial estimate available from previous flush attempt
          threshold = suggested_threshold;
          suggested_threshold = 0; // So we don't come here again
          if (threshold < min_threshold)
            threshold = min_threshold;
          else if (threshold > max_threshold)
            threshold = max_threshold;
        }
      else if (force_bisection_search ||
               ((thresh_above < 0) && (thresh_below < 0)))
        { // Select a mid-point threshold
          threshold = (kdu_uint16)
            ((((int) min_threshold) + ((int) max_threshold) + 1)>>1);
        }
      else
        { // Try to estimate a good threshold based on the `threshold_gradient'
          double delta_L;
          int thresh;
          if (thresh_below >= 0)
            { 
              delta_L = log_cum_target_bytes - log_cum_bytes_below;
              thresh = thresh_below;
            }
          else
            { 
              delta_L = log_cum_target_bytes - log_cum_bytes_above;
              thresh = thresh_above;
            }
          thresh += (int) floor(0.5 + delta_L * threshold_gradient);
          if (thresh < (int) min_threshold)
            thresh = min_threshold;
          else if (thresh > (int) max_threshold)
            thresh = (int) max_threshold;
          threshold = (kdu_uint16) thresh;
          threshold_gradient *= 1.5; // Encourage overshoot next time if we
                                     // do not get upper and lower bounds from
                                     // which to assign a computed gradient.
        }

      reslength_constraints_violated = false;
      layer_bytes =
        simulate_output(packet_header_bytes,sim_layer_idx,final_layer_idx,
                        threshold,false,last_layer);
      layer_bytes += packet_header_bytes*unsized_layers;
      
      if ((layer_bytes > max_bytes) || reslength_constraints_violated)
        { // Need a larger slope threshold
          if (threshold == max_threshold)
            break;
          min_threshold = threshold+1;
          if (layer_bytes <= max_bytes)
            force_bisection_search = true;
          else if (!force_bisection_search)
            { // Update `threshold_gradient' related state information
              thresh_below = (int) threshold;
              log_cum_bytes_below =
                log((double)(layer_bytes + initial_cumulative_bytes));
            }
        }
      else if (layer_bytes >= min_bytes)
        { 
          best_threshold = threshold;
          best_layer_bytes = layer_bytes;
          break; // Found a suitable slope threshold satisfying the bounds
        }
      else
        { // Best slope found so far, but we should try smaller ones also
          best_threshold = threshold;
          best_layer_bytes = layer_bytes;
          if (threshold == min_threshold)
            break;
          max_threshold = threshold-1;
          if (!force_bisection_search)
            { // Update `threshold_gradient' related state information
              thresh_above = (int) threshold;
              log_cum_bytes_above =
                log((double)(layer_bytes + initial_cumulative_bytes));
            }
        }
      if (force_bisection_search)
        continue;
      if ((thresh_above >= 0) && (thresh_below >= 0))
        { 
          double delta_S = (double)(thresh_above - thresh_below);
          double delta_L = log_cum_bytes_above - log_cum_bytes_below;
          if (delta_L >= 0.0)
            { // It does not matter what threshold we use between these bounds
              // any more; we cannot do better than `best_threshold'.
              break;
            }
          threshold_gradient = delta_S / delta_L;
        }
    }
  if (simulated_layer_bytes != NULL)
    *simulated_layer_bytes = best_layer_bytes;
  return best_threshold;
}

/*****************************************************************************/
/*                    kd_codestream::simulate_output_cbr                     */
/*****************************************************************************/

kdu_long
  kd_codestream::simulate_output_cbr(kdu_uint16 slope_threshold,
                                     kdu_uint16 &mod_threshold,
                                     kdu_uint16 &next_slope_val)
{
  mod_threshold = 0xFFFF;
  next_slope_val = 0;
  kdu_long total_bytes = 0;
  for (int p=0; p < cbr_fset.num_precincts; p++)
    { 
      kd_precinct_flush_info *pinf = cbr_fset.precincts + p;
      kdu_long num_bytes;
      kdu_uint16 mod_thresh, nxt_slope;
      if ((slope_threshold >= pinf->last_sim_threshold) &&
          (slope_threshold <= pinf->last_mod_threshold) &&
          (slope_threshold != 0xFFFF))
        { // Existing simulation has everything we need
          num_bytes = pinf->precinct->packet_bytes[0];
          mod_thresh = pinf->last_mod_threshold;
          nxt_slope = pinf->next_slope_val;
        }
      else
        { 
          kdu_long hdr_bytes=0, fill_nf=0;
          int fill_f=0;
          num_bytes =
            pinf->precinct->simulate_packet_cbr(hdr_bytes,slope_threshold,
                                                mod_thresh,nxt_slope,
                                                fill_nf,fill_f);
          assert((mod_thresh >= slope_threshold) &&
                 (nxt_slope <= slope_threshold));
          pinf->last_sim_threshold = slope_threshold;
          pinf->last_mod_threshold = mod_thresh;
          pinf->next_slope_val = nxt_slope;
        }
      total_bytes += num_bytes;
      if (mod_thresh < mod_threshold)
        mod_threshold = mod_thresh;
      if (nxt_slope > next_slope_val)
        next_slope_val = nxt_slope;
    }
  assert((mod_threshold >= slope_threshold) &&
         (next_slope_val <= slope_threshold));
  return total_bytes;
}

/*****************************************************************************/
/*                        kd_codestream::pcrd_opt_cbr                        */
/*****************************************************************************/

void
  kd_codestream::pcrd_opt_cbr()
{
  // Start by working out bounds on the number of bytes that can be generated
  double f_bytes = // the bucket fulness will be decremented by this upon flush
    cbr_fset.data_rate * (double)cbr_fset.flush_set_samples;
  
  kdu_long this_fset_samples = cbr_fset.flush_set_samples;
  kdu_long remaining_fset_samples = (cbr_fset.cs_total_samples -
                                     cbr_fset.cs_flushed_samples);
  kdu_long future_fset_samples = remaining_fset_samples - this_fset_samples;
  kdu_long header_bytes = cbr_fset.flush_set_bytes; // Simulated SOT seg bytes
  kdu_long abs_min_bytes, nominal_min_bytes, max_bytes;
  if (!cbr_fset.last_set_in_codestream)
    { 
      assert(future_fset_samples > 0);
      double cur_bucket_fulness = cbr_fset.bucket_fulness;
      double max_bucket_fulness = cbr_fset.max_bucket_fulness;
      double max_over = ((double)future_fset_samples)*0.25*cbr_fset.data_rate;
      if ((max_over+f_bytes) < max_bucket_fulness)
        max_bucket_fulness = max_over+f_bytes;
      abs_min_bytes = (kdu_long)ceil(f_bytes - cur_bucket_fulness);
      nominal_min_bytes = (kdu_long)(0.5*f_bytes);
      max_bytes = (kdu_long)floor(max_bucket_fulness - cur_bucket_fulness);
    }
  else
    { // Last flush set in codestream
      assert(future_fset_samples == 0);
      header_bytes += 2; // Include cost of the EOC marker
      abs_min_bytes = 0; // Can always append fill/spacer bytes to codestream
      max_bytes = cbr_fset.cs_target_bytes - cbr_fset.cs_flushed_bytes;
      nominal_min_bytes = max_bytes; // Always try to use entire byte budget
    }
  if ((abs_min_bytes -= header_bytes) < 0)
    abs_min_bytes = 0;
  if ((nominal_min_bytes -= header_bytes) < abs_min_bytes)
    nominal_min_bytes = abs_min_bytes;
  if ((max_bytes -= header_bytes) < cbr_fset.num_precincts)
    { KDU_ERROR(e,29081601); e <<
      KDU_TXT("Max codestream size is too small to allow even for the "
              "overhead of empty J2K packets!");
    }
  if (max_bytes < (abs_min_bytes+8)) // Allow imprecision of 8 for fill bytes
    { KDU_ERROR(e,0x29081603); e <<
      KDU_TXT("Leaky bucket max size supplied via `Scbr' attribute is too "
              "small to allow reliable flushing of content to a constant "
              "bit-rate channel that neither overflows nor underflows.");
    }
  
  // Now perform a first simulation and come up with a target length for the
  // flush-set, that is within the nominal min/max bounds discovered above.
  kdu_uint16 global_slope=1;
  float initial_slope_significance = 0.0f;
  bool have_summary_slope_evidence = false;
  if (flush_stats != NULL)
    { // See if we can get the most recent flush result
      int cur_set = flush_stats->active_set;
      int cur_frame_idx = flush_stats->frame_valid[cur_set].get();
      assert(!(cur_frame_idx & 0x80000000)); // Not flushed yet
      if (aux_flush_stats != NULL)
        { 
          int aux_set = cur_set ^ flush_stats->aux_set_parity;
          assert((aux_set & ~1) == 0);
          int aux_frame_idx =
            aux_flush_stats->frame_valid[aux_set].get_barrier();
          if ((aux_frame_idx & 0x80000000) &&
              (((kdu_int32)((aux_frame_idx&0x7FFFFFFF) - cur_frame_idx)) < 0))
            { 
              global_slope=(kdu_uint16)aux_flush_stats->summary_slope[aux_set];
              initial_slope_significance = 1.0f;
              have_summary_slope_evidence = true;
            }
        }
      if (!have_summary_slope_evidence)
        { 
          int prev_frame_idx = flush_stats->frame_valid[1-cur_set].get();
          if (prev_frame_idx & 0x80000000)
            { // We can use our own summary slope information from the
              // last time we flushed a codestream.
              assert(((kdu_int32)((prev_frame_idx & 0x7FFFFFFF) -
                                  cur_frame_idx)) < 0);
              global_slope = (kdu_uint16)flush_stats->summary_slope[1-cur_set];
              initial_slope_significance = 1.0f;
              have_summary_slope_evidence = true;
            }
        }
    }
  if (!have_summary_slope_evidence)
    { // Use other methods to estimate the initial slope threshold
      if (min_slope_threshold > 0)
        { 
          initial_slope_significance = 1.0f;
          global_slope = min_slope_threshold;
        }
      else if (cbr_fset.num_flush_sets > 0)
        { 
          global_slope = (kdu_uint16)(cbr_fset.acc_slope_threshold /
                                      cbr_fset.num_flush_sets);
          initial_slope_significance = (((float) cbr_fset.cs_flushed_samples) /
                                        ((float) cbr_fset.cs_total_samples));
        }
    }
  kdu_uint16 mod_global_slope; // Will be the largest equiv. to global_slope
  kdu_uint16 max_rejected_slope_val;
  kdu_long sim_bytes = simulate_output_cbr(global_slope,mod_global_slope,
                                           max_rejected_slope_val);
  kdu_long tgt_bytes = (kdu_long)
    (f_bytes + initial_slope_significance * (((double)sim_bytes) - f_bytes));
  if (tgt_bytes < nominal_min_bytes)
    tgt_bytes = nominal_min_bytes;
  else if (tgt_bytes > max_bytes)
    tgt_bytes = max_bytes;
  kdu_long tgt_tolerance = (tgt_bytes>>5) + (tgt_bytes>>6); // ~5%

  // Next, adjust a single global slope threshold in order to get as close
  // to `tgt_bytes' as possible, while not exceeding `max_bytes'.  An
  // acceptable slope threshold is one which also produces at least
  // `nominal_min_bytes' bytes.  We finish with either the best acceptable
  // slope threshold or the smallest one that produces no more than
  // `max_bytes'.
  kdu_uint16 min_global_slope=0, lim_global_slope=0xFFFF;
  kdu_uint16 best_global_slope = 0xFFFF; // Means we have not found one yet
  kdu_long best_bytes = 0;
  do { 
    if (sim_bytes > max_bytes)
      { // Future choices must be strictly greater than `mod_global_slope'
        min_global_slope = mod_global_slope+1;
      }
    else if (sim_bytes < nominal_min_bytes)
      { // Future choices should be no larger than `next_smaller_slope'
        lim_global_slope = max_rejected_slope_val;
      }
    else if (sim_bytes > tgt_bytes)
      { // We have an acceptable solution; moreover, future explored slope
        // thresholds should be strictly larger than `mod_global_slope'
        if ((best_global_slope == 0xFFFF) ||
            ((sim_bytes-tgt_bytes) < (best_bytes-tgt_bytes)) ||
            ((sim_bytes-tgt_bytes) < (tgt_bytes-best_bytes)))
          { // New best slope has been found
            best_global_slope = mod_global_slope;
            best_bytes = sim_bytes;
            if ((sim_bytes-tgt_bytes) <= tgt_tolerance)
              break; // This one is good enough
          }
        min_global_slope = mod_global_slope+1;
      }
    else if (sim_bytes < tgt_bytes)
      { // We have an acceptable solution; moreover, future explored slope
        // thresholds should be no larger than `next_smaller_slope'
        if ((best_global_slope == 0xFFFF) ||
            ((tgt_bytes-sim_bytes) < (best_bytes-tgt_bytes)) ||
            ((tgt_bytes-sim_bytes) < (tgt_bytes-best_bytes)))
          { // New best slope has been found
            best_global_slope = mod_global_slope;
            best_bytes = sim_bytes;
            if ((tgt_bytes-sim_bytes) <= tgt_tolerance)
              break; // This one is good enough
          }
        lim_global_slope = max_rejected_slope_val;
      }
    else
      { // Found the target size exactly!
        best_global_slope = mod_global_slope;
        best_bytes = sim_bytes;
        break;
      }
    if (lim_global_slope <= min_global_slope)
      break;
    global_slope = (kdu_uint16)
      ((((int)min_global_slope) + ((int)lim_global_slope)) >> 1);
    sim_bytes = simulate_output_cbr(global_slope,mod_global_slope,
                                    max_rejected_slope_val);
  } while (true); // Loop breaking conditions appear in the body, above.
  
  if (best_global_slope < 0xFFFF)
    { // Found a global best solution; make sure all precinct-info records
      // reflect this value and finish up.
      int p;
      kd_precinct_flush_info *pinf;
      for (pinf=cbr_fset.precincts, p=cbr_fset.num_precincts; p>0; p--, pinf++)
        { 
          pinf->last_sim_threshold = best_global_slope;
          pinf->last_mod_threshold = best_global_slope;
          pinf->next_slope_val = 0; // do not know or need a smaller slope
        }
      cbr_fset.note_simulated_slope_and_bytes(best_global_slope,best_bytes);
      assert((cbr_fset.primary_bulk_fill_bytes == 0) &&
             (cbr_fset.secondary_bulk_fill_bytes == 0) &&
             (cbr_fset.fine_fill_bytes == 0)); // No fill/spacer bytes
      return;
    }
  
  // If we get here, we have not found an acceptable global slope threshold,
  // but `min_global_slope' should be the smallest slope threshold that
  // produces no more than `max_bytes'.  Next, we try adjusting the threshold
  // for individual precincts in order to get at least `nominal_min_bytes',
  // if we can, but no matter what we will not violate the `max_bytes'
  // threshold.
  global_slope = min_global_slope;
  sim_bytes = simulate_output_cbr(global_slope,mod_global_slope,
                                  max_rejected_slope_val);
  if (sim_bytes > max_bytes)
    { KDU_ERROR(e,0x29081605); e <<
      KDU_TXT("Max codestream size is too small to accommodate the `Scbr' "
              "constant-bit-rate codestream flushing constraints!");
    }
  assert(sim_bytes < nominal_min_bytes);
  bool global_slope_included_everything = true;
  do {
    int p;
    kd_precinct_flush_info *pinf, *best_pinf=NULL;
    kdu_uint16 best_local_slope = 0;
    for (pinf=cbr_fset.precincts, p=cbr_fset.num_precincts; p > 0; p--, pinf++)
      if (pinf->next_slope_val > best_local_slope)
        { best_local_slope = pinf->next_slope_val; best_pinf = pinf; }
    if (best_pinf == NULL)
      break; // No more precincts to consider
    global_slope_included_everything = false;
    pinf = best_pinf;
    assert(best_local_slope > 0);
    best_local_slope--; // This is the threshold we should use
    kdu_uint16 mod_thresh, nxt_slope_val;
    kdu_long hdr_bytes=0, old_bytes = pinf->precinct->packet_bytes[0];
    kdu_long fill_nf=0;  int fill_f=0;
    kdu_long new_bytes =
      pinf->precinct->simulate_packet_cbr(hdr_bytes,best_local_slope,
                                          mod_thresh,nxt_slope_val,
                                          fill_nf,fill_f);
    kdu_long delta_bytes = new_bytes - old_bytes;
    if ((sim_bytes + delta_bytes) <= max_bytes)
      { // Accept the change
        sim_bytes += delta_bytes;
        pinf->last_sim_threshold = best_local_slope;
        pinf->last_mod_threshold = mod_thresh;
        pinf->next_slope_val = nxt_slope_val;
      }
    else
      { // Do not consider this precinct again
        pinf->next_slope_val = 0;
      }
  } while (sim_bytes < nominal_min_bytes);
  
  if (global_slope_included_everything)
    { // The slope threshold values recorded with the individual precincts are
      // not reflective of the slope threshold we would expect to pick if we
      // had generated more content originally.  This is because `global_slope'
      // was adjusted in such a way as to take on the largest value that would
      // produce the same truncation points; but we truncated nothing. If we
      // had generated more content, something would have to have been
      // truncated, and its slope would have driven the `gobal_slope' threshold
      // to a smaller value.  This can become a problem for block encoders that
      // use valid `kdu_block::flush_stats' information to optimize the set
      // of coding passes that they will generate in the future, since it will
      // appear to them as if the last generated coding pass had a slope that
      // was reasonably compatible with the slope threshold that was used,
      // discouraging them from generating more content in the next frame.  To
      // avoid the problem, we simply subtract 128 from all of the precincts'
      // `last_sim_threshold' values, which are the values preserved by any
      // `kdu_flush_stats' machinery that is available.
      if (global_slope > 128)
        global_slope = 128;
      else
        global_slope = 0;
      kd_precinct_flush_info *pinf=cbr_fset.precincts;
      for (int p=cbr_fset.num_precincts; p > 0; p--, pinf++)
        { 
          assert((pinf->next_slope_val == 0) &&
                 (pinf->last_sim_threshold >= global_slope));
          pinf->last_sim_threshold = global_slope;
        }
    }
  
  if (sim_bytes >= abs_min_bytes)
    { // Found an acceptable solution
      cbr_fset.note_simulated_slope_and_bytes(mod_global_slope,sim_bytes);
      assert((cbr_fset.primary_bulk_fill_bytes == 0) &&
             (cbr_fset.secondary_bulk_fill_bytes == 0) &&
             (cbr_fset.fine_fill_bytes == 0)); // No fill/spacer bytes
      return;
    }
  
  // If we get here, we have still not found an acceptable set of slope
  // thresholds.  We have no choice now but to add fill/spacer bytes.  This
  // is not completely trivial if there are a lot of bytes to be added.  We
  // start by partitioning the desired fill bytes into two sets: the fine
  // fill bytes that are to be added to the last code-block of a suitable
  // precinct, and the bulk fill bytes that are to be added to other
  // code-blocks.  We will adjust the fine fill bytes to fit, but not the
  // bulk fill bytes, which will get distributed 8192 at a time to
  // code-blocks, in a fixed, yet not interesting order.
  assert(!cbr_fset.last_set_in_codestream);
  
  // First add bulk fill bytes to all but the last non-empty precinct, being
  // careful to add only multiples of 8192 bytes and to leave a good amount of
  // headroom so that there can be some opportunity for fine fill in the
  // last precinct.  This means that only the last precinct is allowed to
  // receive fine fill and only the last precinct is allowed to receive
  // bulk fill that is not a multiple of 8192 bytes.  The bulk fill for the
  // last precinct may end up being coalesced with the fine fill, which
  // might be required if that precinct has only one code-block -- highly
  // unlikely.  When allocating the fill bytes during packet writing, we
  // must remember to assign bulk fill strictly in multiples of 8192 bytes
  // at a time, except in the last precinct.
  int p, p_special=cbr_fset.last_non_empty_precinct;
  assert(p_special < cbr_fset.num_precincts);
  kd_precinct_flush_info *pinf=cbr_fset.precincts;
  kdu_long primary_bulk_fill_bytes = 0;
  kdu_long max_bulk_fill = (abs_min_bytes - sim_bytes) - 1024;
  for (p=0; (p < p_special) && (max_bulk_fill >= 8192); p++, pinf++)
    { 
      max_bulk_fill -= max_bulk_fill & 8191; // Use only multiples of 8192
      kdu_long actual_bulk_fill = max_bulk_fill;
      int fine_fill = 0;
      kdu_uint16 mod_thresh, nxt_slope_val;
      kdu_long hdr_bytes=0, old_bytes = pinf->precinct->packet_bytes[0];
      kdu_long new_bytes =
        pinf->precinct->simulate_packet_cbr(hdr_bytes,pinf->last_mod_threshold,
                                          mod_thresh,nxt_slope_val,
                                          actual_bulk_fill,fine_fill);
      sim_bytes += new_bytes - old_bytes;
      primary_bulk_fill_bytes += actual_bulk_fill;
      max_bulk_fill = (abs_min_bytes - sim_bytes) - 1024;
    }
  if (primary_bulk_fill_bytes & 8191)
    { KDU_ERROR_DEV(e,0x15091601); e <<
      KDU_TXT("Something is seriously wrong with the handling of bulk-fill by "
              "`kd_precinct::simulate_packet_cbr'.");
    }
  if (sim_bytes > max_bytes)
    { KDU_ERROR_DEV(e,0x15091602); e <<
      KDU_TXT("Crazy outcome from the simulation of bulk fill/spacer bytes.  "
              "Headroom of 1024 bytes is not enough to accommodate packet "
              "header costs associated with the introduction of fill bytes.  "
              "This makes no sense, unless the flush sets implied by the "
              "`Scbr' attribute's parameters contain many thousands of "
              "precinct each!");
    }
  
  // Finish by adding any fine fill bytes, plus any remaining bulk fill bytes
  // to the last non-empty precinct (p_special), trying to keep the number
  // of fine fill bytes small so that its impact is more controllable.
  pinf = cbr_fset.precincts + p_special;
  max_bulk_fill = abs_min_bytes - sim_bytes; // Should >~ 1024 bytes.
  int fine_fill_bytes = (int)max_bulk_fill; // Put all on last block if small
  if (max_bulk_fill > 512)
    fine_fill_bytes = 256; // Prefer small fine-fill values, if possible
  max_bulk_fill -= fine_fill_bytes;
  if (max_bulk_fill & 1)
    { // Number of bulk fill bytes must be even
      max_bulk_fill++;
      fine_fill_bytes--;
    }
  bool first_attempt = true;
  kdu_long secondary_bulk_fill_bytes = max_bulk_fill;
  do {
    kdu_uint16 mod_thresh, nxt_slope_val;
    kdu_long hdr_bytes=0, old_bytes = pinf->precinct->packet_bytes[0];
    kdu_long new_bytes =
      pinf->precinct->simulate_packet_cbr(hdr_bytes,pinf->last_mod_threshold,
                                          mod_thresh,nxt_slope_val,
                                          secondary_bulk_fill_bytes,
                                          fine_fill_bytes);
    sim_bytes += new_bytes - old_bytes; // Keep track of the change
    if (secondary_bulk_fill_bytes != max_bulk_fill)
      { // This condition means that there were not enough code-blocks in the
        // precinct to absorb the bulk fill bytes, given that the last
        // code-block takes the fine fill bytes.  The condition is unlikely,
        // since our bulk fill should now be no larger than ~(8192+768) bytes.
        // Only in degenerate cases will there be fewer than 3 code-blocks in
        // the final precinct.  In any case, we need to redistribute the
        // bulk fill bytes that remain to the fine fill bytes and then
        // run the simulation again.  We will not have to do this a second
        // time.
        assert(first_attempt);
        max_bulk_fill = secondary_bulk_fill_bytes;
        kdu_long diff = abs_min_bytes - sim_bytes;
        fine_fill_bytes += (int)diff;
        if (diff > (1<<14))
          { KDU_ERROR(e,0x29081604); e <<
            KDU_TXT("A ridiculously large number of fill bytes are required "
                    "to meet the CBR (constant bit-rate) codestream flushing "
                    "objective associated with the `Scbr' parameter "
                    "attribute.  To resolve this problem, you may need to "
                    "decrease the target code-stream size.");
          }
      }
    else if ((sim_bytes > (abs_min_bytes+4)) &&
             first_attempt && (secondary_bulk_fill_bytes != 0))
      { // Last precinct involves bulk fill as well as fine fill, so the
        // impact of fill bytes on packet header size is less predictable.
        // This should only happen the first time around, after which the
        // amount of bulk fill never changes.  For this case, we might as
        // well have another go, adjusting the number of fine fill bytes
        // downwards to account for the enlarged packet header that resulted
        // from all the fill bytes we added in the first attempt.
        fine_fill_bytes += (int)(abs_min_bytes - sim_bytes); // -ve adjustment
        if (fine_fill_bytes < 0)
          fine_fill_bytes = 0; // Should never happen, but not a disaster
      }
    else if (sim_bytes < abs_min_bytes)
      { // Rare case in which bit-stuffing might have been removed, causing
        // the fill bytes to actually reduce the packet header size.  From
        // the first attempt onwards, we definitely only ever increase the
        // number of fill bytes, so the loop will terminate quickly, if not
        // immediately.
        fine_fill_bytes += (int)(abs_min_bytes - sim_bytes); // +ve adjustment
      }
    else
      break;
    first_attempt = false;
  } while (true);
  if (sim_bytes > max_bytes)
    { KDU_ERROR(e,0x08091601); e <<
      KDU_TXT("Unable to generate sufficient fill bytes to avoid buffer "
              "underflow with `Scbr' attribute present without overflowing "
              "the buffer -- you must increase the maximum buffer size, as "
              "supplied via the `Scbr' attribute.");
    }
  cbr_fset.note_simulated_slope_and_bytes(mod_global_slope,sim_bytes);
  cbr_fset.primary_bulk_fill_bytes = primary_bulk_fill_bytes;
  cbr_fset.secondary_bulk_fill_bytes = secondary_bulk_fill_bytes;
  cbr_fset.fine_fill_bytes = fine_fill_bytes;
  cbr_fset.non_final_fill_bytes += (primary_bulk_fill_bytes +
                                    secondary_bulk_fill_bytes +
                                    fine_fill_bytes);
}

/*****************************************************************************/
/*                          kd_codestream::pcrd_opt                          */
/*****************************************************************************/

void
  kd_codestream::pcrd_opt(bool trim_to_rate, double tolerance)
{
  if (tolerance > 0.5)
    tolerance = 0.5;
  
  // Start by making sure that either the last or the second last
  // quality layer has a non-zero target size.
  bool synthesized_second_last_target_size = false;
  if ((num_sized_layers > 1) && (target_sizes[num_sized_layers-1] <= 0) &&
      (target_sizes[num_sized_layers-2] <= 0))
    { 
      int n;
      
      // Start by figuring out roughly how large the last layer will be
      kdu_long hdr_bytes, rough_upper_bound;
      rough_upper_bound = simulate_output(hdr_bytes,0,num_sized_layers-1,
                                          0,false,false);
      for (n=0; n < num_sized_layers; n++)
        rough_upper_bound += layer_sizes[n]; // Include previously flushed data
      
      // Now see if there is a lower bound available
      for (n=num_sized_layers-3; n >= 0; n--)
        if (target_sizes[n] > 0)
          break;
      if (n >= 0)
        { 
          kdu_long lower_bound = target_sizes[n];
          int layer_gap = (num_sized_layers-1) - n;
          double factor = ((double) rough_upper_bound)/((double) lower_bound);
          factor = pow(factor,1.0/layer_gap); // Desired inter-layer size ratio
          target_sizes[num_sized_layers-2] =
            1 + (kdu_long)(((double) rough_upper_bound) / factor);
        }
      else
        target_sizes[num_sized_layers-2] =
          1 + (kdu_long)(((double) rough_upper_bound) / sqrt(2.0));
      
      // Remember to remove this synthetic second last layer's target size
      // before the function returns.
      synthesized_second_last_target_size = true;
    }
  
  // Now we can run the rate control process.
  kdu_long prev_packet_header_bytes=0, cum_new_bytes=0;
  kdu_long generated_bytes=0; // Generated by previous incremental flush cycles
  kdu_uint16 prev_threshold=0xFFFF;
  int layer_idx, next_layer_idx;
  for (layer_idx=0; layer_idx < num_sized_layers; layer_idx=next_layer_idx)
    { 
      next_layer_idx = layer_idx+1;
      bool last_layer = (next_layer_idx == num_sized_layers);
      kdu_long target_len = target_sizes[layer_idx];
      kdu_long layer_bytes;
      if ((target_len <= 0) && !last_layer)
        { // Must be part of an unsized prefix
          while ((target_len = target_sizes[next_layer_idx]) <= 0)
            { 
              next_layer_idx++;
              assert(next_layer_idx < num_sized_layers);
            }
          kdu_long next_layer_generated_bytes = generated_bytes;
          for (int n=layer_idx; n <= next_layer_idx; n++)
            next_layer_generated_bytes += layer_sizes[n];
          
          // Work out bounds for the `find_slope_threshold' search
          kdu_long next_layer_max_bytes =
            target_len - next_layer_generated_bytes - cum_new_bytes;
          double tmp_tolerance=1.0-sqrt(0.5);
          if (layer_idx > 0)
            tmp_tolerance = 1.0 / (next_layer_idx+1-layer_idx);
          if (tolerance > tmp_tolerance)
            tmp_tolerance = tolerance;
          kdu_long next_layer_min_bytes = next_layer_max_bytes;
          if (tmp_tolerance > 0.0)
            { 
              kdu_long min_cost = prev_packet_header_bytes;
              if (layer_idx == 0)
                min_cost += get_main_and_tile_header_cost();
              if (next_layer_max_bytes > min_cost)
                next_layer_min_bytes -=
                  (kdu_long)((next_layer_max_bytes-min_cost) * tmp_tolerance);
            }
          if (last_layer)
            { // Account for EOC marker cost
              next_layer_max_bytes -= 2;
              next_layer_min_bytes -= 2;
            }
          
          // Work out approximate slope threshold for `sized_layer_idx'
          int next_threshold =
            find_slope_threshold(next_layer_idx,next_layer_idx-layer_idx,
                                 next_layer_min_bytes,next_layer_max_bytes,
                                 prev_threshold,cum_new_bytes,
                                 layer_thresholds[next_layer_idx],
                                 &layer_bytes);
          
          // Work out the slope threshold for the last unsized layer, using
          // the typical gap between slope thresholds as a guideline to
          // correct for typically high tolerance associated with the
          // above call to `find_slope_threshold'.
          double log_size_correction =
            log(((double)(next_layer_max_bytes+cum_new_bytes)) /
                ((double)(layer_bytes+cum_new_bytes)));
          int adj_next_threshold, last_threshold;
          if (cum_new_bytes > 0)
            { 
              assert(layer_idx > 0);
              int delta_slope = next_threshold - prev_threshold;
              if (delta_slope == 0)
                last_threshold = adj_next_threshold = next_threshold;
              else
                { 
                  double delta_log_size =
                    log(((double)(layer_bytes+cum_new_bytes)) /
                        ((double) cum_new_bytes));
                  double slope_gradient = ((double)delta_slope)/delta_log_size;
                  int slope_correction = (int)
                    (log_size_correction * slope_gradient);
                  adj_next_threshold = next_threshold + slope_correction;
                  int slope_gap = adj_next_threshold - prev_threshold;
                  slope_gap = slope_gap / (next_layer_idx - (layer_idx-1));
                  last_threshold = adj_next_threshold - slope_gap;
                  if (last_threshold < next_threshold)
                    last_threshold = next_threshold;
                }
            }
          else
            { 
              double slope_gradient = -600.0 / log(2.0);
              int slope_correction = (int)
                (log_size_correction * slope_gradient);
              adj_next_threshold = next_threshold + slope_correction;
              last_threshold = adj_next_threshold + 300;
              if (last_threshold < next_threshold)
                last_threshold = next_threshold;
            }
          layer_thresholds[next_layer_idx] = adj_next_threshold;
                // So we can start with this already quite good
                // estimate of the slope threshold for `next_layer_idx'
                // when the "for" loop reaches that layer.
          
          // Assign thresholds and finalize the unsized layers
          for (; layer_idx < next_layer_idx; layer_idx++)
            { 
              generated_bytes += layer_sizes[layer_idx];
              int layers_left = next_layer_idx - layer_idx;
              int threshold = prev_threshold;
              if ((threshold >= 0xFFFF) &&
                  ((last_threshold + (layers_left-1)*300) < 0xFFFF))
                threshold = last_threshold + (layers_left-1)*300;
              else
                threshold += (last_threshold-threshold) / layers_left;
              layer_bytes = simulate_output(prev_packet_header_bytes,layer_idx,
                                            layer_idx,(kdu_uint16) threshold,
                                            true,false);
              layer_thresholds[layer_idx] = (kdu_uint16) threshold;
              prev_threshold = (kdu_uint16) threshold;
              cum_new_bytes += layer_bytes;
              expected_sizes[layer_idx] = generated_bytes + cum_new_bytes +
                ((last_layer)?2:0); // extra 2 bytes is for EOC marker
            }
        }
      else if ((target_len <= 0) && !reslength_constraints_used)
        { // Final layer takes all
          generated_bytes += layer_sizes[layer_idx];
          assert(last_layer);
          layer_thresholds[layer_idx] = prev_threshold = 0;
          layer_bytes = simulate_output(prev_packet_header_bytes,layer_idx,
                                        layer_idx,0,true,last_layer);
          cum_new_bytes += layer_bytes;
          expected_sizes[layer_idx] = generated_bytes + cum_new_bytes +
            ((last_layer)?2:0); // extra 2 bytes is for EOC marker
        }
      else
        { // Regular PCRD-opt for single layer
          if (target_len <= 0)
            target_len = KDU_LONG_HUGE;
          
          // Work out bounds for the `find_slope_threshold' search
          generated_bytes += layer_sizes[layer_idx];
          kdu_long max_bytes = target_len - generated_bytes - cum_new_bytes;
          kdu_long min_bytes = max_bytes;
          if (tolerance > 0.0)
            { 
              kdu_long min_cost = prev_packet_header_bytes;
              if (layer_idx == 0)
                min_cost += get_main_and_tile_header_cost();              
              if (max_bytes > min_cost)
                min_bytes -= (kdu_long)((max_bytes-min_cost)*tolerance);
            }
          if (last_layer)
            max_bytes -= 2; // Account for EOC marker cost
          kdu_uint16 threshold =
            find_slope_threshold(layer_idx,0,min_bytes,max_bytes,
                                 prev_threshold,cum_new_bytes,
                                 layer_thresholds[layer_idx],&layer_bytes);
          kdu_long sloppy_bytes=0;
          if (trim_to_rate && final_fragment && last_layer && (threshold > 0))
            { // Prepare to finalize with sloppy fill-in
              if (reslength_constraints_used)
                { // Need to check that sloppy fill in is safe
                  reslength_constraints_violated = false;
                  simulate_output(prev_packet_header_bytes,layer_idx,
                                  layer_idx,threshold-1,false,last_layer);
                  if (reslength_constraints_violated)
                    layer_bytes = max_bytes; // Avoids sloppy fill-in
                }
              if (layer_bytes < 0)
                { // `find_slope_threshold' did not need to actually simulate
                  // the number of bytes associated with the returned
                  // `threshold', so we have to do it here -- highly unusual.
                  layer_bytes =
                    simulate_output(prev_packet_header_bytes,layer_idx,
                                    layer_idx,threshold,false,last_layer);
                }
              sloppy_bytes = max_bytes - layer_bytes;
            }
          reslength_constraints_violated = false;
          if (sloppy_bytes > 0)
            { 
              threshold--;
              layer_bytes = simulate_output(prev_packet_header_bytes,layer_idx,
                                            layer_idx,threshold,true,true,
                                            max_bytes,&sloppy_bytes);
            }
          else
            layer_bytes = simulate_output(prev_packet_header_bytes,layer_idx,
                                          layer_idx,threshold,true,last_layer);
          assert(reslength_warning_issued || !reslength_constraints_violated);
          layer_thresholds[layer_idx] = threshold;
          prev_threshold = threshold;
          cum_new_bytes += layer_bytes;
          expected_sizes[layer_idx] = target_len;
        }
    }
  
  if (synthesized_second_last_target_size)
    target_sizes[num_sized_layers-2] = 0; // Restore original 0 entry
}

/*****************************************************************************/
/*                          kd_codestream::pcrd_trim                         */
/*****************************************************************************/

void kd_codestream::pcrd_trim(bool finalize_last_layer)
{
  int layer_idx;
  kdu_long generated_bytes=0;
  for (layer_idx=0; layer_idx < num_sized_layers; layer_idx++)
    { 
      generated_bytes += layer_sizes[layer_idx]; // Add in any bytes already
            // generated for this quality layer, because the `simulate_output'
            // function only works out the number of remaining bytes that
            // will be generated for the layer if the supplied slope threshold
            // is used -- of course, this is an estimate when incremental
            // flushing is employed.
      
      // Figure out what lower bound applies, if any.
      kdu_long target_min_bytes = 0;
      if (using_min_sizes)
        { 
          target_min_bytes = target_min_sizes[layer_idx];
          target_min_bytes -= generated_bytes;
          if (target_min_bytes < 0)
            target_min_bytes = 0;
        }

      // Find `threshold' and `lim_threshold' taking into account the need
      // for monotonicity
      int lim_threshold = (1<<16);
      if (layer_idx > 0)
        lim_threshold = ((int) layer_thresholds[layer_idx-1]) + 1;
      int original_threshold = layer_thresholds[layer_idx];
      if (original_threshold >= lim_threshold)
        { 
          original_threshold = lim_threshold-1;
          layer_thresholds[layer_idx] = (kdu_uint16) original_threshold;
        }
  
      bool last_layer = (layer_idx == (num_sized_layers-1));
      int threshold = original_threshold; // First one to try
      kdu_long layer_bytes=0, packet_header_bytes=0;
      if ((target_min_bytes <= 0) && !reslength_constraints_used)
        { // Can finalize this layer immediately
          if (finalize_last_layer || !last_layer)
            layer_bytes =
              simulate_output(packet_header_bytes,layer_idx,layer_idx,
                              (kdu_uint16) threshold,true,last_layer);
        }
      else
        { 
          int best_threshold = -1; // Non-negative if we find one that works
          int fallback_threshold = -1; // In case we can't satisfy both constraints
          int min_threshold = 0; // Until we find another bound
          kdu_long best_layer_bytes=-1, fallback_layer_bytes=-1;
          
          do { // Enter bisection search
            reslength_constraints_violated = false;
            layer_bytes =
              simulate_output(packet_header_bytes,layer_idx,layer_idx,
                              (kdu_uint16) threshold,false,last_layer);
            if (reslength_constraints_violated)
              min_threshold = threshold+1;
            else if (layer_bytes < target_min_bytes)
              { 
                lim_threshold = fallback_threshold = threshold;
                fallback_layer_bytes = layer_bytes;
              }
            else
              { // The current threshold is acceptable
                if (threshold >= original_threshold)
                  lim_threshold = threshold+1; // No need to go any larger
                if (threshold <= original_threshold)
                  min_threshold = threshold; // No need to go any smaller
                best_threshold = threshold; // Best one so far
                best_layer_bytes = layer_bytes;
              }
            threshold = (min_threshold+lim_threshold)>>1;
          } while ((threshold != best_threshold) &&
                   (lim_threshold > min_threshold));
          if ((best_threshold < 0) && (fallback_threshold >= 0))
            { // We failed to satisfy both constraints, but the smallest
              // threshold that satisfies the critical reslength constraints
              // is `lim_threshold', so we can use this.
              best_threshold = fallback_threshold;
              best_layer_bytes = fallback_layer_bytes;
            }
          if (best_threshold < 0)
            { KDU_WARNING_DEV(w,0x09051200); w <<
              KDU_TXT("Unable to find modified distortion-length "
                      "slope thresholds which can satisfy the "
                      "compressed length constraints imposed by the "
                      "supplied `Creslength' parameter attribute(s).");
              reslength_warning_issued = true;
              best_threshold = original_threshold;
            }
          layer_thresholds[layer_idx] = (kdu_uint16) best_threshold;
          if (finalize_last_layer || !last_layer)
            { // Finalize the quality layer
              layer_bytes =
                simulate_output(packet_header_bytes,layer_idx,layer_idx,
                                (kdu_uint16)(best_threshold),true,last_layer);
              assert(layer_bytes == best_layer_bytes);
            }
          else
            { // Finalization not required because the compressed data target
              // is a structured cache.
              assert(best_layer_bytes >= 0);
              layer_bytes = best_layer_bytes;
            }
        }
      
      generated_bytes += layer_bytes;
      expected_sizes[layer_idx] = generated_bytes + ((last_layer)?2:0);
                                  // extra 2 bytes is for EOC marker
    }
}

/*****************************************************************************/
/*                  kd_codestream::generate_main_header                      */
/*****************************************************************************/

void
kd_codestream::generate_main_header()
{ 
  assert(header_generated == false);
  header_generated = true;
  if (initial_fragment)
    { 
      layer_sizes[0] += out->put(KDU_SOC);
      layer_sizes[0] += siz->generate_marker_segments(out,-1,0);
      
      // Write out comment marker segments
      if (!comments_frozen)
        freeze_comments();
      kd_codestream_comment *comscan;
      for (comscan=comhead; comscan != NULL; comscan=comscan->next)
        layer_sizes[0] += comscan->write_marker(out);
    }
  
  // See if we need to write TLM marker segments
  assert(!tlm_generator);
  kdu_params *org = siz->access_cluster(ORG_params); assert(org != NULL);
  int max_tlm_tparts;
  if (org->get(ORGgen_tlm,0,0,max_tlm_tparts) && (max_tlm_tparts > 0))
    { 
      if (cbr_flush_interval != 0)
        { KDU_ERROR(e,0x26081601); e <<
          KDU_TXT("`ORGgen_tlm' behaviour is not currently supported with "
                  "the CBR flushing option identified by `Scbr'.");
        }
      if (max_tlm_tparts > 255)
        max_tlm_tparts = 255;
      int tnum_bytes, tplen_bytes;
      if (org->get(ORGtlm_style,0,0,tnum_bytes) &&
          org->get(ORGtlm_style,0,1,tplen_bytes))
        { 
          if ((tnum_bytes == 0) && (max_tlm_tparts > 1))
            { KDU_ERROR(e,0x25041002); e <<
              KDU_TXT("The \"implied\" style for signalling tile numbers "
                      "in TLM marker segments, as requested via the "
                      "\"ORGtlm_style\" parameter attribute, cannot be "
                      "used unless there is only one tile-part per tile, "
                      "so the \"ORGgen_tlm\" parameter attribute should "
                      "specify a value of 1 for the maximum number of "
                      "tile-parts per tile in this case.");
            }
          if ((tnum_bytes == 1) && ((tile_span.x*tile_span.y) > 256))
            { KDU_ERROR(e,0x25041003); e <<
              KDU_TXT("You have used the \"ORGtlm_style\" parameter "
                      "attribute to specify a TLM marker segment style "
                      "in which tile numbers are represented using "
                      "only one byte.  However, the number of tiles "
                      "in the image is greater than 256, so this is "
                      "clearly going to be a problem.");
            }
        }
      else
        { tnum_bytes = 2; tplen_bytes = 4; }
      kdu_compressed_target *tgt = out->access_tgt();
      if (tgt->start_rewrite(0))
        { 
          tgt->end_rewrite();
          if (tlm_generator.init(tile_span.x*tile_span.y,max_tlm_tparts,
                                 tnum_bytes,tplen_bytes))
            { 
              if (initial_fragment)
                tlm_generator.write_dummy_tlms(out);
            }
          else
            { KDU_WARNING(w,16); w <<
              KDU_TXT("Unable to generate the TLM marker "
                      "segments requested via the `ORGgen_tlm' parameter "
                      "attribute.  The reason for this is that the total "
                      "number of tile-parts whose lengths would need to be "
                      "represented exceeds the amount of data which can "
                      "legally be stored in the maximum allowable 256 TLM "
                      "marker segments, allowing for 6 bytes per tile-part "
                      "length value.");
            }
        }
      else
        { KDU_WARNING(w,17); w <<
          KDU_TXT("Unable to generate the TLM marker segments "
                  "requested via the `ORGgen_tlm' parameter attribute.  "
                  "The reason for this is that the logical compressed data "
                  "target supplied by the application does not appear to "
                  "support rewriting (i.e., seeking).  We need this to reserve "
                  "space for the TLM marker segments up front and later "
                  "overwrite the reserved space with valid tile-part lengths.");
        }
    }
  assert(header_length == 0);
  header_length = out->get_bytes_written();
  if (!initial_fragment)
    assert(header_length == 0);
}

/*****************************************************************************/
/*                  kd_codestream::generate_codestream                       */
/*****************************************************************************/

bool
  kd_codestream::generate_codestream(int max_layers)
{
  assert(out != NULL);
  assert((layer_sizes != NULL) && (layer_thresholds != NULL));
  if (max_layers > num_sized_layers)
    { KDU_ERROR_DEV(e,38); e <<
        KDU_TXT("Using the `kdu_codestream::generate_codestream' "
        "function in an illegal manner.  The `max_layers' argument may not "
        "exceed the maximum number of layers which are being sized.  The "
        "problem may have arisen from an incorrect use of the incremental "
        "code-stream flushing capability.");
    }

  // Write the main header, if necessary
  if (!header_generated)
    generate_main_header();
  
  // Now cycle through the tiles, interleaving their tile-parts.
  bool done;
  do {
      done = true;
      kd_tile *tnext = NULL;
      for (kd_tile *tile=tiles_in_progress_head; tile != NULL; tile=tnext)
        {
          tnext = tile->in_progress_next;
          kd_tile_ref *tref = tile->tile_ref;
          kdu_long tpart_bytes =
            tile->generate_tile_part(max_layers,layer_thresholds);
          if (((tile = tref->tile) != KD_EXPIRED_TILE) && (tpart_bytes > 0))
            done = false;
        }
    } while (!done);

  // Finish up with the EOC marker, once all data has been generated
  if (num_incomplete_tiles == 0)
    {
      if (tlm_generator.exists())
        { 
          kdu_compressed_target *tgt = out->access_tgt();
          tlm_generator.write_tlms(tgt,prev_tiles_written,
                                   prev_tile_bytes_written);
        }
      if (final_fragment)
        layer_sizes[0] += out->put(KDU_EOC);
      out->flush();
    }
  return (num_incomplete_tiles == 0);
}

/*****************************************************************************/
/*                    kd_codestream::write_packets_cbr                       */
/*****************************************************************************/

bool
  kd_codestream::write_packets_cbr()
{
  assert(out != NULL);
  assert((layer_sizes != NULL) && (layer_thresholds != NULL));
  assert(num_sized_layers == 1);
  assert(header_generated);
  
  kd_tile *tp = cbr_fset.active_tile;
  assert((tp != NULL) &&
         (tp->next_tpart == 1) && // incremented by `construct_cbr_flush_set'
         (cbr_fset.num_precincts > 0));
  kdu_long start_bytes = out->get_bytes_written();
  kdu_long flush_bytes = 0;
  if (cbr_fset.flushed_tile_bytes == 0)
    { // Need to write the SOT marker segment
      assert(!tlm_generator.exists()); // `generate_main_header' refuses to
                                       // support this feature with `Scbr'.
      kdu_long tpart_bytes = cbr_fset.flush_set_bytes;
      if (tp->sequenced_relevant_packets != tp->max_relevant_packets)
        { // More than one flush-set in the tile.  We currently require
          // the image to have only one tile, so we can write 0 in the
          // tile-part-length field, avoiding the need to commit future
          // flush-sets to an exact size.
          tpart_bytes = 0;
          assert((tile_span.x == 1) && (tile_span.y == 1));
        }
      out->put(KDU_SOT);
      out->put((kdu_uint16) 10);
      out->put((kdu_uint16) tp->t_num);
      out->put((kdu_uint32) tpart_bytes);
      out->put((kdu_byte) 0);
      out->put((kdu_byte) 1); // There is only one tile-part
      flush_bytes += 12 + // 12 is for the SOT marker segment
        siz->generate_marker_segments(out,tp->t_num,0);
      flush_bytes += out->put(KDU_SOD); // 2 bytes for the SOD marker
    }

  int p, p_special = cbr_fset.last_non_empty_precinct;
  assert(p_special < cbr_fset.num_precincts);
  kd_precinct_flush_info *pinf = cbr_fset.precincts;
  kdu_long bulk_fill_bytes, pbytes, hbytes;
  for (p=0; p < p_special; p++, pinf++)
    { 
      hbytes = 0;
      bulk_fill_bytes = cbr_fset.primary_bulk_fill_bytes;
      pbytes = pinf->precinct->write_packet_cbr(pinf->last_sim_threshold,
                                                hbytes,bulk_fill_bytes,0);
      cbr_fset.primary_bulk_fill_bytes -= bulk_fill_bytes;
      written_packet_header_bytes += hbytes;
      written_packet_bytes += pbytes;
      flush_bytes += pbytes;
    }
  if (cbr_fset.primary_bulk_fill_bytes != 0)
    { KDU_ERROR_DEV(e,0x15091603); e <<
      KDU_TXT("Inconsistency detected between simulated and actually written "
              "primary bulk fill bytes!");
    }
  
  { 
    hbytes = 0;
    bulk_fill_bytes = cbr_fset.secondary_bulk_fill_bytes;
    pbytes = pinf->precinct->write_packet_cbr(pinf->last_sim_threshold,
                                              hbytes,bulk_fill_bytes,
                                              cbr_fset.fine_fill_bytes);
    cbr_fset.secondary_bulk_fill_bytes -= bulk_fill_bytes;
    cbr_fset.fine_fill_bytes = 0;
    written_packet_header_bytes += hbytes;
    written_packet_bytes += pbytes;
    flush_bytes += pbytes;
    p++;
    pinf++;
  }
  if (cbr_fset.secondary_bulk_fill_bytes != 0)
    { KDU_ERROR_DEV(e,0x15091604); e <<
      KDU_TXT("Inconsistency detected between simulated and actually written "
              "secondary bulk fill bytes!");
    }
  
  for (; p < cbr_fset.num_precincts; p++, pinf++)
    { // Unlikely to be any non-empty precincts left, but do this anyway.
      hbytes = 0;  bulk_fill_bytes = 0;
      pbytes = pinf->precinct->write_packet_cbr(pinf->last_sim_threshold,
                                                hbytes,bulk_fill_bytes,0);
      written_packet_header_bytes += hbytes;
      written_packet_bytes += pbytes;
      flush_bytes += pbytes;
    }

  bool flushed_all = false;
  if (cbr_fset.last_set_in_tile)
    { // Finished the tile; may need to emit EOC
      cbr_flush_tile++;
      tp->remove_from_in_progress_list();
      if (tp->closed && !allow_restart)
        tp->release();
      tp = NULL; // Not safe to access this pointer any more

      this->num_completed_tparts++;
      if (this->num_incomplete_tiles == 0)
        { 
          assert(cbr_fset.last_set_in_codestream);
          flush_bytes += out->put(KDU_EOC);
          flushed_all = true;
        }
      else
        assert(!cbr_fset.last_set_in_codestream);
      
      cbr_fset.bucket_fulness -= (cbr_fset.data_rate *
                                  (double)cbr_fset.flush_set_samples);
      assert((cbr_fset.bucket_fulness >= 0.0) ||
             cbr_fset.last_set_in_codestream);

      cbr_fset.num_flushed_precincts += cbr_fset.num_precincts;
      cbr_fset.cs_flushed_bytes += cbr_fset.flush_set_bytes;
      cbr_fset.cs_flushed_samples += cbr_fset.flush_set_samples;
      cbr_fset.flush_set_bytes = 0;
      cbr_fset.flush_set_samples = 0;
      cbr_fset.num_precincts = 0;

      cbr_fset.active_tile = NULL;
      cbr_fset.total_tile_samples = 0;
      cbr_fset.planned_tile_bytes = 0;
      cbr_fset.flushed_tile_bytes = 0;
      cbr_fset.flushed_tile_samples = 0;
      cbr_fset.y_boundary = 0; // `construct_cbr_flush_set' will set this again
    }
  else
    { // Finished the flush-set but not the tile
      cbr_fset.bucket_fulness -= (cbr_fset.data_rate *
                                  (double)cbr_fset.flush_set_samples);
      assert(cbr_fset.bucket_fulness >= 0.0);
      
      cbr_fset.num_flushed_precincts += cbr_fset.num_precincts;
      cbr_fset.flushed_tile_bytes += cbr_fset.flush_set_bytes;
      cbr_fset.cs_flushed_bytes += cbr_fset.flush_set_bytes;
      cbr_fset.flushed_tile_samples += cbr_fset.flush_set_samples;
      cbr_fset.cs_flushed_samples += cbr_fset.flush_set_samples;
      cbr_fset.flush_set_bytes = 0;
      cbr_fset.flush_set_samples = 0;
      cbr_fset.num_precincts = 0;
      cbr_fset.y_boundary += cbr_fset.y_boundary_increment;
    }

  if (flush_bytes != (out->get_bytes_written() - start_bytes))
    { KDU_ERROR_DEV(e,0x15091605); e <<
      KDU_TXT("Inconsistency detected between reported and actually "
              "written packet bytes within a flush-set!");
    }
  this->layer_sizes[0] += flush_bytes;
  return flushed_all;
}

/*****************************************************************************/
/*              kd_codestream::cache_write_ready_precincts                   */
/*****************************************************************************/

void
  kd_codestream::cache_write_ready_precincts(int max_layers)
{
  // Start by calculating the cost of including codestream main and tile
  // headers -- these calculations are based on the assumption that all
  // marker segments need to be written, whereas for the cached target we
  // do not actually need to write the SOT marker segments, nor do we need
  // to write the EOC or SOD markers.
  kdu_long cum_bytes = 0;
  if (initial_fragment && !header_generated)
    { 
      cum_bytes = 2 + siz->generate_marker_segments(NULL,-1,0);
      if (!comments_frozen)
        freeze_comments();
      kd_codestream_comment *comscan;
      for (comscan=comhead; comscan != NULL; comscan=comscan->next)
        cum_bytes += comscan->write_marker(NULL);
      cum_bytes += reserved_layer_info_bytes;
    }  
  for (kd_tile *tile=tiles_in_progress_head;
       tile != NULL; tile=tile->in_progress_next)
    cum_bytes += 12 + 2 + // SOT + SOD
      siz->generate_marker_segments(NULL,tile->t_num,tile->next_tpart);
  
  // Initialize the `expected_sizes' array
  int n;
  for (n=0; n < num_sized_layers; n++)
    { 
      cum_bytes += layer_sizes[n];
      expected_sizes[n] = cum_bytes;
    }

  // Now work through all the ready precincts
  int c, d=max_depth;
  kd_global_rescomp *rc = global_rescomps + d*num_components;
  for (; d >= 0; d--, rc-=(num_components<<1))
    for (c=num_components; c > 0; c--, rc++)
      { 
        kd_precinct *p_next, *precinct=rc->first_ready;
        if (precinct == NULL)
          continue;
        
        // Scale sizes to properly handle incremental flushing.
        bool all_ready = (rc->remaining_area==rc->ready_area);
        if (rc->ready_fraction < 0.0)
          { // The `ready_fraction' value needs to be estimated
            assert((rc->remaining_area > 0) &&
                   (rc->remaining_area >= rc->ready_area));
            if (all_ready)
              rc->ready_fraction = rc->reciprocal_fraction = 1.0;
            else
              { 
                double A;
                kdu_long T = rc->total_area - rc->area_covered_by_tiles;
                if (T > 0)
                  { 
                    assert(T <= (rc->remaining_area + rc->ready_area));
                    A = ((double)(rc->remaining_area - T))
                      + ((double) T) * ((double) rc->area_used_by_tiles) /
                        ((double) rc->area_covered_by_tiles);
                  }
                else
                  A = (double)(rc->remaining_area);
                rc->ready_fraction = ((double)(rc->ready_area)) / A;
                if (rc->ready_fraction > 1.0)
                  rc->ready_fraction = 1.0; // In case of rounding problems.
                rc->reciprocal_fraction = 1.0 / rc->ready_fraction;
              }
          }
        
        for (n=0; n < num_sized_layers; n++)
          tmp_layer_sizes[n] = layer_sizes[n];
        for (; precinct != NULL; precinct=p_next)
          { 
            p_next = precinct->next;
            precinct->cache_write_packets(max_layers,layer_thresholds);
          }
        for (cum_bytes=0, n=0; n < num_sized_layers; n++)
          { 
            kdu_long new_pbytes = layer_sizes[n] - tmp_layer_sizes[n];
            tmp_layer_sizes[n] = 0;
            if (!all_ready)
              { // Scale the new packet bytes discovered up to account for
                // all outstanding precincts.
                new_pbytes = 1+(kdu_long)(new_pbytes*rc->reciprocal_fraction);
              }
            cum_bytes += new_pbytes;
            expected_sizes[n] += cum_bytes;
          }
      }
}

/*****************************************************************************/
/*                  kd_codestream::cache_write_headers                       */
/*****************************************************************************/

bool kd_codestream::cache_write_headers()
{
  assert(out != NULL);
  assert((layer_sizes != NULL) && (layer_thresholds != NULL));

  // Write the main header, if necessary
  if (!header_generated)
    { 
      header_generated = true;
      if (initial_fragment)
        { 
          out->start_mainheader();
          layer_sizes[0] += out->put(KDU_SOC);
          layer_sizes[0] += siz->generate_marker_segments(out,-1,0);
          
          // Write out comment marker segments
          if (!comments_frozen)
            freeze_comments();
          kd_codestream_comment *comscan;
          for (comscan=comhead; comscan != NULL; comscan=comscan->next)
            layer_sizes[0] += comscan->write_marker(out);
          header_length = out->get_bytes_written();
          out->end_mainheader();
        }      
    }
  
  // Now write tile headers for any tiles that are in-progress, but have
  // not yet had their header written.
  kd_tile *tile, *tnext=NULL;
  for (tile=tiles_in_progress_head; tile != NULL; tile=tnext)
    { 
      tnext = tile->in_progress_next;
      if (tile->next_tpart == 0)
        tile->cache_write_tileheader();
    }
  
  // Finish up with the EOC marker, once all data has been generated
  if (num_incomplete_tiles == 0)
    { 
      if (final_fragment)
        layer_sizes[0] += 2; // For the EOC marker that we don't actually write
      out->flush();
    }
  return (num_incomplete_tiles == 0);
}

/*****************************************************************************/
/*            kd_codestream::unload_tiles_to_cache_threshold                 */
/*****************************************************************************/

void
  kd_codestream::unload_tiles_to_cache_threshold()
{
  while ((num_unloadable_tiles > 0) &&
         ((num_unloadable_tiles > max_unloadable_tiles) ||
          buf_servers->cache_threshold_exceeded()))
    {
      while ((unloadable_tile_scan != NULL) &&
             unloadable_tile_scan->dims.intersects(region))
        unloadable_tile_scan = unloadable_tile_scan->unloadable_next;
      if (unloadable_tile_scan != NULL)
        {
          assert(unloadable_tile_scan->is_unloadable);
          unloadable_tile_scan->release(); // Automatically updates pointers
        }
      else
        unloadable_tiles_head->release(); // Automatically updates pointers
    }
}


/* ========================================================================= */
/*                              kdu_codestream                               */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdu_codestream::create (output)                  */
/*****************************************************************************/

void
  kdu_codestream::create(siz_params *siz_in,
                         kdu_compressed_target *target,
                         kdu_dims *fragment_region,
                         int fragment_tiles_generated,
                         kdu_long fragment_tile_bytes_generated,
                         kdu_thread_env *env)
{
  assert((state == NULL) && (target != NULL));
  ((kdu_params *) siz_in)->finalize(); // In case the caller did not do it
  state = new kd_codestream;
  state->out = new kd_compressed_output(target);
  state->cached_target =
    (target->get_capabilities() & KDU_TARGET_CAP_CACHED) != 0;
  state->siz = new siz_params;
  state->siz->copy_from(siz_in,-1,-1);
  state->construct_common();
  if (fragment_region != NULL)
    state->restrict_to_fragment(*fragment_region,fragment_tiles_generated,
                                fragment_tile_bytes_generated);
  state->num_incomplete_tiles = (int) state->tile_indices.area();
  state->main_and_tile_header_cost = -1;
  if (env != NULL)
    state->start_multi_threading(env);
}

/*****************************************************************************/
/*                       kdu_codestream::create (input)                      */
/*****************************************************************************/

void
  kdu_codestream::create(kdu_compressed_source *source, kdu_thread_env *env)
{
  assert(state == NULL);
  state = new kd_codestream;
  state->in = new kd_compressed_input(source);
  state->cached_source =
    (source->get_capabilities() & KDU_SOURCE_CAP_CACHED) != 0;
  state->in_memory_source = state->in->is_fully_buffered();
  state->marker = new kd_marker(state->in,state);
  if ((!state->marker->read()) ||
      (state->marker->get_code() != KDU_SOC))
    { KDU_ERROR(e,39); e <<
        KDU_TXT("Code-stream must start with an SOC marker!");
    }
  state->siz = new siz_params;
  if (!(state->marker->read() &&
        state->siz->translate_marker_segment(state->marker->get_code(),
                                             state->marker->get_length(),
                                             state->marker->get_bytes(),-1,0)))
    { KDU_ERROR(e,40); e <<
        KDU_TXT("Code-stream must contain a valid SIZ marker segment, "
        "immediately after the SOC marker!");
    }
  int ext=0;
  if (state->siz->get(Sextensions,0,0,ext) &&
      (ext & Sextensions_MCT) && (ext & Sextensions_NLT) &&
      (state->in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE))
    { // Before we can construct the parameter sub-system fully, we need to
      // know the number of output components, for which we first need to
      // parse the CBD marker segment if there is one.  Without this, we will
      // not be able to correctly instantiate the NLT_params cluster, which is
      // required to correctly parse NLT information. In practice, the CBD
      // marker segment is almost certain to immediately follow the SIZ marker
      // segment, but unfortunately the standard never mandated this, so we may
      // need to scan the entire main header and then seek back to the start
      // again afterwards.  We avoid this as far as we possibly can, coming
      // here only if the MCT and NLT features are both present.
      kdu_long pos = state->in->get_offset();
      while (state->marker->read() && (state->marker->get_code() != KDU_SOT))
        if (state->marker->get_code() == KDU_CBD)
          { 
            state->siz->translate_marker_segment(state->marker->get_code(),
                                                 state->marker->get_length(),
                                                 state->marker->get_bytes(),
                                                 -1,0);
            break;
          }
      state->in->seek(pos);
    }
  
  state->construct_common();
  state->comments_frozen = true;
  if (env != NULL)
    state->start_multi_threading(env);
}

/*****************************************************************************/
/*                     kdu_codestream::create (interchange)                  */
/*****************************************************************************/

void
  kdu_codestream::create(siz_params *siz_in, kdu_thread_env *env)
{
  assert(state == NULL);
  ((kdu_params *) siz_in)->finalize(); // In case the caller did not do it
  state = new kd_codestream;
  state->siz = new siz_params;
  state->siz->copy_from(siz_in,-1,-1);
  state->construct_common();
  state->interchange = true;
  state->persistent = true; // Structures persist after tile is closed, but
                            // compressed data does not.
  if (env != NULL)
    state->start_multi_threading(env);
}

/*****************************************************************************/
/*                        kdu_codestream::restart (output)                   */
/*****************************************************************************/

void
  kdu_codestream::restart(kdu_compressed_target *target, kdu_thread_env *env)
{
  if (!state->allow_restart)
    { KDU_ERROR_DEV(e,41); e <<
        KDU_TXT("You may not use the `kdu_codestream::restart' "
        "function unless `kdu_codestream::enable_restart' was called after "
        "the code-stream management machinery was first created.");
    }
  if (state->out == NULL)
    { KDU_ERROR_DEV(e,42); e <<
        KDU_TXT("You may not use the output form of "
        "`kdu_codestream::restart' if the code-stream management machinery "
        "was originally created using anything other than the output form of "
        "`kdu_codestream::create'.");
    }
  if (env != NULL)
    { 
      if (state->thread_context != NULL)
        state->thread_context->clear_queues(env);
      else
        state->start_multi_threading(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  delete state->out;
  state->out = NULL;
  state->out = new kd_compressed_output(target);
  state->cached_target =
    (target->get_capabilities() & KDU_TARGET_CAP_CACHED) != 0;
  state->restart();
  state->num_incomplete_tiles = (int) state->tile_indices.area();
  state->main_and_tile_header_cost = -1;
  if ((state->flush_stats != NULL) &&
      (state->flush_stats->auto_advance_frame_sep > 0))
    state->flush_stats->advance(state->flush_stats->auto_advance_frame_sep);
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                         kdu_codestream::restart (input)                   */
/*****************************************************************************/

void
  kdu_codestream::restart(kdu_compressed_source *source, kdu_thread_env *env)
{
  if (!state->allow_restart)
    { KDU_ERROR_DEV(e,43); e <<
        KDU_TXT("You may not use the `kdu_codestream::restart' "
        "function unless `kdu_codestream::enable_restart' was called after "
        "the code-stream management machinery was first created.");
    }
  if (state->in == NULL)
    { KDU_ERROR_DEV(e,44); e <<
        KDU_TXT("You may not use the input form of "
        "`kdu_codestream::restart' if the code-stream management machinery "
        "was originally created using anything other than the input form of "
        "`kdu_codestream::create'.");
    }

  if (env != NULL)
    { 
      if (state->thread_context != NULL)
        state->thread_context->clear_queues(env);
      else
        state->start_multi_threading(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  delete state->in;
  state->in = NULL;
  state->in = new kd_compressed_input(source);
  delete state->marker;
  state->marker = new kd_marker(state->in,state);
  state->cached_source =
    (source->get_capabilities() & KDU_SOURCE_CAP_CACHED) != 0;
  state->in_memory_source = state->in->is_fully_buffered();
  state->block_truncation_factor = 0;
  if ((!state->marker->read()) ||
      (state->marker->get_code() != KDU_SOC))
    { KDU_ERROR(e,45); e <<
        KDU_TXT("Code-stream must start with an SOC marker!");
    }
  
  // Read the SIZ marker segment and determine whether or not the high
  // level structure has changed; if so, we might as well create the internal
  // machinery again from scratch.
  state->siz->clear_marks(true); // So we can tell if anything changes
  if (!(state->marker->read() &&
        state->siz->translate_marker_segment(state->marker->get_code(),
                                             state->marker->get_length(),
                                             state->marker->get_bytes(),-1,0)))
    { KDU_ERROR(e,46); e <<
      KDU_TXT("Code-stream must contain a valid SIZ marker segment, "
              "immediately after the SOC marker!");
    }
  state->read_siz_auxiliary_marker_segs();
  if (state->siz->any_changes())
    { // Rebuild the code-stream management machinery from scratch     
      kd_codestream *new_state = new kd_codestream;
      new_state->in = state->in; state->in = NULL;
      new_state->marker = state->marker->move(new_state->in,new_state);
      state->marker = NULL;
      new_state->siz = new siz_params;
      new_state->siz->copy_from(state->siz,-1,-1);
      new_state->construct_common();
      
      // Transfer members that we want to preserve from `state' to `new_state'
      new_state->discard_levels = state->discard_levels;
      new_state->max_apparent_layers = state->max_apparent_layers;
      if (new_state->num_components == state->num_components)
        { 
          new_state->num_apparent_components = state->num_apparent_components;
          for (int c=0; c < new_state->num_components; c++)
            { 
              new_state->comp_info[c].apparent_idx =
                state->comp_info[c].apparent_idx;
              new_state->comp_info[c].from_apparent = new_state->comp_info +
                (state->comp_info[c].from_apparent - state->comp_info);
            }
        }
      if (new_state->num_output_components == state->num_output_components)
        { 
          new_state->num_apparent_output_components =
            state->num_apparent_output_components;
          for (int c=0; c < new_state->num_output_components; c++)
            { 
              new_state->output_comp_info[c].apparent_idx =
                state->output_comp_info[c].apparent_idx;
              new_state->output_comp_info[c].from_apparent =
                state->output_comp_info[c].from_apparent;
            }
        }
      new_state->component_access_mode = state->component_access_mode;
      
      new_state->allow_restart = state->allow_restart;
      new_state->transpose = state->transpose;
      new_state->vflip = state->vflip;
      new_state->hflip = state->hflip;
      new_state->resilient = state->resilient;
      new_state->expect_ubiquitous_sops = state->expect_ubiquitous_sops;
      new_state->fussy = state->fussy;
      new_state->persistent = state->persistent;
      new_state->cached_source = state->cached_source;
      new_state->cached_target = state->cached_target;
      new_state->in_memory_source = state->in_memory_source;
      new_state->min_slope_threshold = state->min_slope_threshold;
      
      // Now we can swap the contents of `new_state' with those of `state',
      // being careful to adjust ownership of any resources that reference
      // their the buffer management or `kd_codestream' object.
      state->delete_and_reset_all_but_buffering_and_threading();

      // Transfer multi-threading machinery and buffer management to
      // `new_state', recognizing that all member variables are shortly
      // going to be copied from `new_state' to `state' and then `state'
      // will be reset to 0 and deleted.
      assert(new_state->thread_context == NULL);
      if (new_state->buf_servers != NULL)
        { 
          new_state->buf_servers[0].cleanup_and_detach();
          delete[] new_state->buf_servers;
          new_state->buf_servers = NULL;
        }
      new_state->buf_master->detach_codestream();
      new_state->buf_master = state->buf_master;    state->buf_master = NULL;
      new_state->buf_servers = state->buf_servers;  state->buf_servers = NULL;
      new_state->thread_context = state->thread_context;
      state->thread_context=NULL;
      assert((new_state->rate_stats[0]==NULL) && (state->rate_stats[0]==NULL));
      
      // Now swap the contents of `state' with `new_state'
      memcpy(state,new_state,sizeof(kd_codestream));
      memset(new_state,0,sizeof(kd_codestream));
      state->marker = state->marker->move(state->in,state);
      delete new_state;
    }
  else
    state->restart();
  state->comments_frozen = true;
  state->unloadable_tile_scan = state->unloadable_tiles_head;
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                           kdu_codestream::destroy                         */
/*****************************************************************************/

void
  kdu_codestream::destroy()
{
  assert(state != NULL);
  delete state;
  state = NULL;
}

/*****************************************************************************/
/*                      kdu_codestream::share_buffering                      */
/*****************************************************************************/

void
  kdu_codestream::share_buffering(kdu_codestream existing)
{
  assert(!state->tiles_accessed);
  if ((state->buf_master->get_peak_structure_blocks() > 0) ||
      (state->buf_master->get_peak_buf_blocks() > 0))
    { KDU_ERROR_DEV(e,47); e <<
        KDU_TXT("You cannot use the `kdu_codestream::share_buffering' "
        "function if the codestream object which will be sharing another "
        "codestream's buffering has already allocated some internal "
        "resources.");
    }
  if (state->thread_context != NULL)
    { 
      int t, num_threads = state->thread_context->manage_buf_servers(NULL);
      for (t=0; t <= num_threads; t++)
        state->buf_servers[t].cleanup_and_detach();
      state->buf_master->detach_codestream();
      state->buf_master = existing.state->buf_master;
      state->buf_master->attach_codestream();
      state->buf_master->set_multi_threaded(); // Just in case
      state->buf_servers[0].attach_and_init(state->buf_master);
      state->thread_context->manage_buf_servers(state->buf_servers);
    }
  else
    {
      state->buf_servers[0].cleanup_and_detach();
      state->buf_master->detach_codestream();
      state->buf_master = existing.state->buf_master;
      state->buf_master->attach_codestream();
      state->buf_servers[0].attach_and_init(state->buf_master);
    }
}

/*****************************************************************************/
/*                         kdu_codestream::mem_failure                       */
/*****************************************************************************/

void
  kdu_codestream::mem_failure(const char *mem_category, const char *message)
{
  KDU_ERROR(e,0x21101306); e <<
  KDU_TXT("Memory allocation failure in sub-system") <<
  " \"" << mem_category << "\" " <<
  KDU_TXT("with message:") <<
  " \"" << message << "\".";
}

/*****************************************************************************/
/*                   kdu_codestream::augment_cache_threshold                 */
/*****************************************************************************/

kdu_long
  kdu_codestream::augment_cache_threshold(int extra_bytes)
{
  return state->buf_master->augment_cache_threshold(extra_bytes);
}

/*****************************************************************************/
/*                kdu_codestream::set_tile_unloading_threshold               */
/*****************************************************************************/

int
  kdu_codestream::set_tile_unloading_threshold(int max_tiles_on_list,
                                               kdu_thread_env *env)
{
  if (env != NULL)
    state->acquire_lock(KD_THREADLOCK_GENERAL,env);
  if (max_tiles_on_list < 0)
    max_tiles_on_list = 0;
  int prev_value = state->max_unloadable_tiles;
  state->max_unloadable_tiles = max_tiles_on_list;
  state->unload_tiles_to_cache_threshold();
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
  return prev_value;
}


/*****************************************************************************/
/*                      kdu_codestream::enable_restart                       */
/*****************************************************************************/

void
  kdu_codestream::enable_restart()
{
  if (state->allow_restart)
    return;
  if (state->tiles_accessed)
    { KDU_ERROR_DEV(e,48); e <<
        KDU_TXT("You may not call `kdu_codestream::enable_restart' "
        "after opening the first tile.");
    }
  state->allow_restart = true;
}

/*****************************************************************************/
/*                       kdu_codestream::set_persistent                      */
/*****************************************************************************/

void
  kdu_codestream::set_persistent()
{
  if (state->in == NULL)
    return;
  if (state->tiles_accessed)
    { KDU_ERROR_DEV(e,49); e <<
        KDU_TXT("You may only set the codestream object into its "
        "\"persistent\" mode prior to opening the first tile.");
    }
  state->persistent = true;
}

/*****************************************************************************/
/*                      kdu_codestream::is_last_fragment                     */
/*****************************************************************************/

bool
  kdu_codestream::is_last_fragment()
{
  return (state!=NULL) && (state->out!=NULL) && state->final_fragment;
}

/*****************************************************************************/
/*                          kdu_codestream::access_siz                       */
/*****************************************************************************/

siz_params *
  kdu_codestream::access_siz()
{
  return state->siz;
}

/*****************************************************************************/
/*                      kdu_codestream::set_textualization                   */
/*****************************************************************************/

void
  kdu_codestream::set_textualization(kdu_message *output)
{
  assert(!state->tiles_accessed);
  if (output != NULL)
    {
      state->siz->textualize_attributes(*output,-1,-1);
      output->flush();
    }
  state->textualize_out = output;
}

/*****************************************************************************/
/*                        kdu_codestream::set_max_bytes                      */
/*****************************************************************************/

void
  kdu_codestream::set_max_bytes(kdu_long max_bytes, bool simulate_parsing,
                                bool enable_periodic_trimming)
{
  assert(!state->tiles_accessed);

  if (state->in != NULL)
    {
      state->simulate_parsing_while_counting_bytes = simulate_parsing;
      state->in->set_max_bytes(max_bytes);
      if (state->in->failed())
        { KDU_ERROR(e,50); e <<
            KDU_TXT("Attempting to impose too small a limit on the "
            "number of code-stream bytes. " << (int) max_bytes << " bytes is "
            "insufficient to accommodate even the main header!");
        }
    }
  else if (state->out != NULL)
    { 
      if (state->rate_stats[0] != NULL)
        { KDU_ERROR_DEV(e,51); e <<
            KDU_TXT("\"kdu_codestream::set_max_bytes\" may not be "
            "called multiple times.");
        }
      kdu_long total_samples = 0;
      for (int c=0; c < state->num_components; c++)
        {
          kdu_dims comp_dims; get_dims(c,comp_dims);
          total_samples += comp_dims.area();
        }
      state->rate_stats[0] = new kd_compressed_stats(total_samples,max_bytes,
                                                     enable_periodic_trimming);
      if (state->thread_context != NULL)
        state->thread_context->manage_compressed_stats(state->rate_stats);
    }
}

/*****************************************************************************/
/*                   kdu_codestream::set_min_slope_threshold                 */
/*****************************************************************************/

void
  kdu_codestream::set_min_slope_threshold(kdu_uint16 threshold)
{
  state->min_slope_threshold = threshold;
}

/*****************************************************************************/
/*                        kdu_codestream::set_resilient                      */
/*****************************************************************************/

void
  kdu_codestream::set_resilient(bool expect_ubiquitous_sops)
{
  state->resilient = true;
  state->expect_ubiquitous_sops = expect_ubiquitous_sops;
  state->fussy = false;
}

/*****************************************************************************/
/*                          kdu_codestream::set_fussy                        */
/*****************************************************************************/

void
  kdu_codestream::set_fussy()
{
  state->resilient = false;
  state->fussy = true;
}

/*****************************************************************************/
/*                          kdu_codestream::set_fast                         */
/*****************************************************************************/

void
  kdu_codestream::set_fast()
{
  state->resilient = false;
  state->fussy = false;
}

/*****************************************************************************/
/*                kdu_codestream::apply_input_restrictions                   */
/*****************************************************************************/

void
  kdu_codestream::apply_input_restrictions(int first_component,
                        int max_components, int discard_levels,
                        int max_layers, const kdu_dims *region_of_interest,
                        kdu_component_access_mode access_mode,
                        kdu_thread_env *env,
                        const kdu_quality_limiter *limiter)
{
  if (env != NULL)
    { 
      state->start_multi_threading(env); // Just in case
      state->thread_context->clear_queues(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  
  if (state->out != NULL)
    { KDU_ERROR_DEV(e,52); e <<
        KDU_TXT("The `kdu_codestream::apply_input_restrictions' "
        "function may not be invoked on codestream objects opened for output "
        "(i.e. for compression).");
    }
  if (state->tiles_accessed)
    {
      if (state->num_open_tiles != 0)
        { 
          if (state->thread_context != NULL)
            { KDU_ERROR_DEV(e,0x26041402); e <<
              KDU_TXT("You must pass a non-NULL `env' argument to the "
                      "`kdu_codestream::apply_input_restrictions' function "
                      "if there are tiles that have been scheduled for "
                      "background closure processing unless `cs_terminate' "
                      "is called first.");
            }
          else
            { KDU_ERROR_DEV(e,53); e <<
              KDU_TXT("You may apply restrictions to the resolution or number "
                      "of image components only after closing all open "
                      "tiles.");
            }
        }
      if (state->tiles_accessed && !state->persistent)
        { KDU_ERROR_DEV(e,54); e <<
            KDU_TXT("You may not apply restrictions to the resolution "
            "or number of image components after the first tile access, "
            "unless the codestream object is set up to be persistent.");
        }
    }

  state->discard_levels = discard_levels;
  if (max_layers <= 0)
    max_layers = 0xFFFF;
  state->max_apparent_layers = max_layers;
  state->region = state->canvas;
  if (region_of_interest != NULL)
    state->region &= *region_of_interest;
  state->unloadable_tile_scan = state->unloadable_tiles_head;
  if (state->limiter != NULL)
    { delete state->limiter; state->limiter = NULL; }
  if (limiter != NULL)
    state->limiter = limiter->duplicate();

  // Now process component restrictions.
  int c, from_idx;
  state->component_access_mode = access_mode;
  if (access_mode == KDU_WANT_CODESTREAM_COMPONENTS)
    {
      if ((first_component < 0) || (first_component >= state->num_components))
        { KDU_ERROR_DEV(e,55); e <<
            KDU_TXT("The range of apparent image components supplied to "
            "`kdu_codestream::apply_input_restrictions' is empty or illegal!");
        }
      state->num_apparent_output_components = 0;
           // Output components are inaccessible in codestream access mode
      state->num_apparent_components =
        state->num_components - first_component;
      if ((max_components > 0) &&
          (max_components < state->num_apparent_components))
        state->num_apparent_components = max_components;
      for (c=from_idx=0; c < state->num_components; c++)
        { 
          kd_comp_info *ci = state->comp_info + c;
          ci->apparent_idx = c - first_component;
          if (ci->apparent_idx >= state->num_apparent_components)
            ci->apparent_idx = -1;
          ci->from_apparent = NULL;
          ci->qlim_sq_weight = 1.0f; ci->qlim_type_flags = 0;
          if (ci->apparent_idx >= 0)
            { 
              state->comp_info[from_idx++].from_apparent = ci;
              if (limiter != NULL)
                limiter->get_comp_info(ci->apparent_idx,ci->qlim_sq_weight,
                                       ci->qlim_type_flags);
            }
        }
    }
  else if (access_mode == KDU_WANT_OUTPUT_COMPONENTS)
    {
      // Start by removing any restrictions on the codestream components
      state->num_apparent_components = state->num_components;
      for (c=0; c < state->num_components; c++)
        { 
          kd_comp_info *ci = state->comp_info + c;
          ci->apparent_idx = c;
          ci->from_apparent = ci;
          ci->qlim_sq_weight = 1.0f;   // Not really necessary to init these
          ci->qlim_type_flags = 0;     // `limiter' related parameters here.
        }

      // Now to apply/remove restrictions on the output image components.
      if ((first_component < 0) ||
          (first_component >= state->num_output_components))
        { KDU_ERROR_DEV(e,0x05090500); e <<
            KDU_TXT("The range of apparent output image components supplied "
            "to `kdu_codestream::apply_input_restrictions' is empty or "
            "illegal!");
        }

      state->num_apparent_output_components =
        state->num_output_components - first_component;
      if ((max_components > 0) &&
          (max_components < state->num_apparent_output_components))
        state->num_apparent_output_components = max_components;
      for (c=from_idx=0; c < state->num_output_components; c++)
        { 
          kd_output_comp_info *oci = state->output_comp_info + c;
          oci->apparent_idx = c - first_component;
          if (oci->apparent_idx >= state->num_apparent_output_components)
            oci->apparent_idx = -1;
          oci->from_apparent = 0;
          oci->qlim_sq_weight = 1.0f;
          oci->qlim_type_flags = 0;
          if (oci->apparent_idx >= 0)
            { 
              state->output_comp_info[from_idx++].from_apparent = c;
              if (limiter != NULL)
                limiter->get_comp_info(oci->apparent_idx,oci->qlim_sq_weight,
                                       oci->qlim_type_flags);
            }
        }
    }
  else
    assert(0);
  
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                kdu_codestream::apply_input_restrictions                   */
/*****************************************************************************/

void
  kdu_codestream::apply_input_restrictions(int num_components,
                        int *component_indices, int discard_levels,
                        int max_layers, const kdu_dims *region_of_interest,
                        kdu_component_access_mode access_mode,
                        kdu_thread_env *env,
                        const kdu_quality_limiter *limiter)
{
  if (env != NULL)
    { 
      state->start_multi_threading(env); // Just in case
      state->thread_context->clear_queues(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }

  // Set up everything except components, by calling the first form of
  // the function
  apply_input_restrictions(0,0,discard_levels,max_layers,region_of_interest,
                           access_mode,NULL); // No need to pass `env' in this
                                              // call; mutex is already locked.
  assert(state->limiter == NULL);
  if (limiter != NULL)
    state->limiter = limiter->duplicate();

  int c, apparent_idx;
  if (access_mode == KDU_WANT_CODESTREAM_COMPONENTS)
    { // In this case, we have only to configure any special restrictions on
      // the codestream image components.  Everything else has already been
      // done for us by the first form of the function.
      for (c=0; c < state->num_components; c++)
        { 
          state->comp_info[c].from_apparent = NULL;
          state->comp_info[c].apparent_idx = -1;
          state->comp_info[c].qlim_sq_weight = 1.0f;
          state->comp_info[c].qlim_type_flags = 0;
        }
      state->num_apparent_components = 0;
      for (c=apparent_idx=0; c < num_components; c++)
        { 
          int idx = component_indices[c];
          if ((idx < 0) || (idx >= state->num_components) ||
              (c >= state->num_components) ||
              (state->comp_info[idx].apparent_idx >= 0))
            continue;
          kd_comp_info *ci = state->comp_info + idx;
          ci->apparent_idx = apparent_idx;
          state->comp_info[apparent_idx].from_apparent = ci;
          if (limiter != NULL)
            limiter->get_comp_info(apparent_idx,ci->qlim_sq_weight,
                                   ci->qlim_type_flags);
          apparent_idx++;
        }
      state->num_apparent_components = apparent_idx;
    }
  else if (access_mode == KDU_WANT_OUTPUT_COMPONENTS)
    { // Restrictions have already been removed on codestream image
      // components, so all we have to do here is apply/remove restrictions
      // on the output image components.
      for (c=0; c < state->num_output_components; c++)
        { 
          state->output_comp_info[c].from_apparent = 0;
          state->output_comp_info[c].apparent_idx = -1;
          state->output_comp_info[c].qlim_sq_weight = 1.0f;
          state->output_comp_info[c].qlim_type_flags = 0;
        }
      state->num_apparent_output_components = 0;
      for (c=apparent_idx=0; c < num_components; c++)
        { 
          int idx = component_indices[c];
          if ((idx < 0) || (idx >= state->num_output_components) ||
              (c >= state->num_output_components) ||
              (state->output_comp_info[idx].apparent_idx >= 0))
            continue;
          kd_output_comp_info *oci = state->output_comp_info + idx;
          oci->apparent_idx = apparent_idx;
          state->output_comp_info[apparent_idx].from_apparent = idx;
          if (limiter != NULL)
            limiter->get_comp_info(apparent_idx,oci->qlim_sq_weight,
                                   oci->qlim_type_flags);
          apparent_idx++;
        }
      state->num_apparent_output_components = apparent_idx;
    }
  else
    assert(0);
  
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                    kdu_codestream::change_appearance                      */
/*****************************************************************************/

void
  kdu_codestream::change_appearance(bool transpose, bool vflip, bool hflip,
                                    kdu_thread_env *env)
{
  if (env != NULL)
    { 
      state->start_multi_threading(env); // Just in case
      state->thread_context->clear_queues(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  
  if (state->tiles_accessed)
    { 
      if (state->num_open_tiles != 0)
        { 
          if (state->thread_context != NULL)
            { KDU_ERROR_DEV(e,0x26041403); e <<
              KDU_TXT("You must pass a non-NULL `env' argument to the "
                      "`kdu_codestream::change_appearance' function "
                      "if there are tiles that have been scheduled for "
                      "background closure processing unless `cs_terminate' "
                      "is called first.");
            }
          else
            { KDU_ERROR_DEV(e,56); e <<
              KDU_TXT("You may change the apparent geometry of the "
                      "code-stream only after closing all open tiles.");
            }
        }
      if (state->tiles_accessed && !state->persistent)
        { KDU_ERROR_DEV(e,57); e <<
            KDU_TXT("You may not change the apparent geometry of the "
            "code-stream after the first tile access, unless "
            "the codestream object is set up to be persistent.");
        }
    }
  state->transpose = transpose;
  state->vflip = vflip;
  state->hflip = hflip;
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                   kdu_codestream::set_block_truncation                    */
/*****************************************************************************/

void
  kdu_codestream::set_block_truncation(kdu_int32 factor)
{
  if (factor < 0)
    factor = 0;
  if (state != NULL)
    state->block_truncation_factor = factor;
}

/*****************************************************************************/
/*                       kdu_codestream::get_tile_dims                       */
/*****************************************************************************/

void
  kdu_codestream::get_tile_dims(kdu_coords tile_idx,
                                int comp_idx, kdu_dims &dims,
                                bool want_output_comps)
{
  tile_idx.from_apparent(state->transpose,state->vflip,state->hflip);
  assert((tile_idx.x >= 0) && (tile_idx.x < state->tile_span.x) &&
         (tile_idx.y >= 0) && (tile_idx.y < state->tile_span.y));
  dims = state->tile_partition;
  dims.pos.x += tile_idx.x * dims.size.x;
  dims.pos.y += tile_idx.y * dims.size.y;
  dims &= state->canvas;
  if (state->out == NULL)
    dims &= state->region;
  kdu_coords min = dims.pos;
  kdu_coords lim = min + dims.size;
  kdu_coords subs = kdu_coords(1,1);
  if (comp_idx >= 0)
    {
      if (!state->construction_finalized)
        state->finalize_construction();
      kd_comp_info *ci = NULL;
      if (want_output_comps &&
          (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
        {
          if (comp_idx < state->num_apparent_output_components)
            {
              kd_output_comp_info *oci = state->output_comp_info +
                state->output_comp_info[comp_idx].from_apparent;
              ci = oci->subsampling_ref;
            }
        }
      else
        {
          if (comp_idx < state->num_apparent_components)
            ci = state->comp_info[comp_idx].from_apparent;
        }

      subs = ci->sub_sampling;
      subs.x <<= ci->hor_depth[state->discard_levels];
      subs.y <<= ci->vert_depth[state->discard_levels];
    }
  min.x = ceil_ratio(min.x,subs.x);
  min.y = ceil_ratio(min.y,subs.y);
  lim.x = ceil_ratio(lim.x,subs.x);
  lim.y = ceil_ratio(lim.y,subs.y);
  dims.pos = min;
  dims.size = lim-min;
  dims.to_apparent(state->transpose,state->vflip,state->hflip);
}

/*****************************************************************************/
/*                    kdu_codestream::get_num_components                     */
/*****************************************************************************/

int
  kdu_codestream::get_num_components(bool want_output_comps)
{
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    return state->num_apparent_output_components;
  else
    return state->num_apparent_components;
}

/*****************************************************************************/
/*                      kdu_codestream::get_bit_depth                        */
/*****************************************************************************/

int
  kdu_codestream::get_bit_depth(int comp_idx, bool want_output_comps,
                                bool pre_nlt)
{
  if (comp_idx < 0)
    return 0; // Invalid component
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    {
      if (comp_idx >= state->num_apparent_output_components)
        return 0;
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      return (pre_nlt)?oci->in_precision:oci->out_precision;
    }
  else
    {
      if (comp_idx >= state->num_apparent_components)
        return 0;
      kd_comp_info *ci = state->comp_info[comp_idx].from_apparent;
      return ci->precision;
    }
}

/*****************************************************************************/
/*                       kdu_codestream::get_signed                          */
/*****************************************************************************/

bool
  kdu_codestream::get_signed(int comp_idx, bool want_output_comps,
                             bool pre_nlt)
{
  if (comp_idx < 0)
    return false; // Invalid component
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    {
      if (comp_idx >= state->num_apparent_output_components)
        return false;
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      return (pre_nlt)?oci->in_signed:oci->out_signed;
    }
  else
    {
      if (comp_idx >= state->num_apparent_components)
        return false;
      kd_comp_info *ci = state->comp_info[comp_idx].from_apparent;
      return ci->is_signed;
    }
}

/*****************************************************************************/
/*                      kdu_codestream::get_subsampling                      */
/*****************************************************************************/

void
  kdu_codestream::get_subsampling(int comp_idx, kdu_coords &subs,
                                  bool want_output_comps)
{
  if (!state->construction_finalized)
    state->finalize_construction();
  if (comp_idx < 0)
    { subs = kdu_coords(0,0); return; }
  kd_comp_info *ci;
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    {
      if (comp_idx >= state->num_apparent_output_components)
        { subs = kdu_coords(0,0); return; }
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      ci = oci->subsampling_ref;
    }
  else
    {
      if (comp_idx >= state->num_apparent_components)
        { subs = kdu_coords(0,0); return; }
      ci = state->comp_info[comp_idx].from_apparent;
    }
  subs = ci->sub_sampling;
  subs.x <<= ci->hor_depth[state->discard_levels];
  subs.y <<= ci->vert_depth[state->discard_levels];
  if (state->transpose)
    subs.transpose();
}

/*****************************************************************************/
/*                      kdu_codestream::get_registration                     */
/*****************************************************************************/

void
  kdu_codestream::get_registration(int comp_idx, kdu_coords scale,
                                   kdu_coords &reg, bool want_output_comps)
{
  if (!state->construction_finalized)
    state->finalize_construction();
  if (comp_idx < 0)
    { reg = kdu_coords(0,0); return; }
  kd_comp_info *ci=NULL;
  bool crg_incompatible=false;
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    { 
      if (comp_idx >= state->num_apparent_output_components)
        { reg = kdu_coords(0,0); return; }
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      ci = oci->subsampling_ref;
      crg_incompatible = oci->crg_incompatible;
    }
  else
    { 
      if (comp_idx >= state->num_apparent_components)
        { reg = kdu_coords(0,0); return; }
      ci = state->comp_info[comp_idx].from_apparent;
    }
  if (crg_incompatible)
    { 
      reg = kdu_coords(0,0);
      return;
    }
  if (state->transpose)
    scale.transpose();
  reg.x = (int) floor(ci->crg_x*scale.x+0.5);
  reg.y = (int) floor(ci->crg_y*scale.y+0.5);
  reg.to_apparent(state->transpose,state->vflip,state->hflip);
}

/*****************************************************************************/
/*                kdu_codestream::get_relative_registration                  */
/*****************************************************************************/

void
  kdu_codestream::get_relative_registration(int comp_idx, int ref_comp_idx,
                                            kdu_coords scale, kdu_coords &reg,
                                            bool want_output_comps)
{
  if (!state->construction_finalized)
    state->finalize_construction();
  if ((comp_idx < 0) || (ref_comp_idx < 0))
    { reg = kdu_coords(0,0); return; }
  kd_comp_info *ci;
  kd_comp_info *ref_ci;
  bool crg_incompatible=false;
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    { 
      if ((comp_idx >= state->num_apparent_output_components) ||
          (ref_comp_idx >= state->num_apparent_output_components))
        { reg = kdu_coords(0,0); return; }
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      crg_incompatible = oci->crg_incompatible;
      ci = oci->subsampling_ref;
      oci = state->output_comp_info +
        state->output_comp_info[ref_comp_idx].from_apparent;
      ref_ci = oci->subsampling_ref;
      if (oci->crg_incompatible)
        crg_incompatible = true;
    }
  else
    { 
      if ((comp_idx >= state->num_apparent_components) ||
          (ref_comp_idx >= state->num_apparent_components))
        { reg = kdu_coords(0,0); return; }
      ci = state->comp_info[comp_idx].from_apparent;
      ref_ci = state->comp_info[ref_comp_idx].from_apparent;
    }
  if (crg_incompatible)
    { 
      reg = kdu_coords(0,0);
      return;
    }
  if (state->transpose)
    scale.transpose();
  float ref_crg_x = ref_ci->crg_x *
    ((float) ref_ci->sub_sampling.x) / ((float) ci->sub_sampling.x);
  float ref_crg_y = ref_ci->crg_y *
    ((float) ref_ci->sub_sampling.y) / ((float) ci->sub_sampling.y);
  reg.x = (int) floor((ci->crg_x-ref_crg_x)*scale.x+0.5);
  reg.y = (int) floor((ci->crg_y-ref_crg_y)*scale.y+0.5);
  reg.to_apparent(state->transpose,state->vflip,state->hflip);
}

/*****************************************************************************/
/*                           kdu_codestream::get_dims                        */
/*****************************************************************************/
    
void
  kdu_codestream::get_dims(int comp_idx, kdu_dims &dims,
                           bool want_output_comps)
{
  if (comp_idx < 0)
    dims = state->region;
  else
    { 
      if ((!state->construction_finalized) && (comp_idx >= 0) &&
          want_output_comps && state->uses_mct)
        state->finalize_construction(); // MCT affects component connections
      kd_comp_info *ci = NULL;
      if (want_output_comps &&
          (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
        {
          if (comp_idx < state->num_apparent_output_components)
            {
              kd_output_comp_info *oci = state->output_comp_info +
                state->output_comp_info[comp_idx].from_apparent;
              ci = oci->subsampling_ref;
            }
        }
      else
        {
          if (comp_idx < state->num_apparent_components)
            ci = state->comp_info[comp_idx].from_apparent;
        }
      if (ci == NULL)
        { KDU_ERROR_DEV(e,0x24091305); e <<
          KDU_TXT("Illegal component index supplied in call to "
                  "`kdu_codesteram::get_dims'.");
        }

      kdu_coords min = state->region.pos;
      kdu_coords lim = min+state->region.size;
      int y_fact =
        ci->sub_sampling.y << ci->vert_depth[state->discard_levels];
      int x_fact =
        ci->sub_sampling.x << ci->hor_depth[state->discard_levels];

      min.x = ceil_ratio(min.x,x_fact);
      lim.x = ceil_ratio(lim.x,x_fact);
      min.y = ceil_ratio(min.y,y_fact);
      lim.y = ceil_ratio(lim.y,y_fact);
      dims.pos = min;
      dims.size = lim-min;
    }
  dims.to_apparent(state->transpose,state->vflip,state->hflip);
}

/*****************************************************************************/
/*                     kdu_codestream::get_tile_partition                    */
/*****************************************************************************/
    
void
  kdu_codestream::get_tile_partition(kdu_dims &partition)
{
  partition = state->tile_partition;

  // Convert dimensions to those of the entire partition so geometric
  // transformations produce correct `pos' member
  partition.size.x *= state->tile_span.x;
  partition.size.y *= state->tile_span.y;
  partition.to_apparent(state->transpose,state->vflip,state->hflip);
  partition.size = state->tile_partition.size; // Put back the element size
  if (state->transpose)
    partition.size.transpose();
}

/*****************************************************************************/
/*                      kdu_codestream::get_max_tile_layers                  */
/*****************************************************************************/

int
  kdu_codestream::get_max_tile_layers()
{
  return state->max_tile_layers;
}

/*****************************************************************************/
/*                      kdu_codestream::get_min_dwt_levels                   */
/*****************************************************************************/

int
  kdu_codestream::get_min_dwt_levels()
{
  if (state->min_dwt_levels > 32)
    { // Check main header COD marker segment
      int levels;
      kdu_params *cod = state->siz->access_cluster(COD_params);
      if (cod->get(Clevels,0,0,levels) && (levels < state->min_dwt_levels))
        state->min_dwt_levels = levels;
      if (state->min_dwt_levels > 32)
        state->min_dwt_levels = 32;
    }
  return state->min_dwt_levels;
}

/*****************************************************************************/
/*                           kdu_codestream::can_flip                        */
/*****************************************************************************/

bool
  kdu_codestream::can_flip(bool check_current_appearance_only)
{
  return !(state->cannot_flip && (state->hflip || state->vflip));
}

/*****************************************************************************/
/*                         kdu_codestream::cbr_flushing                      */
/*****************************************************************************/

bool
kdu_codestream::cbr_flushing() const
{
  return (state->cbr_flush_interval > 0);
}

/*****************************************************************************/
/*                          kdu_codestream::map_region                       */
/*****************************************************************************/

void
  kdu_codestream::map_region(int comp_idx, kdu_dims comp_region,
                             kdu_dims &canvas_region, bool want_output_comps)
{
  comp_region.from_apparent(state->transpose,state->vflip,state->hflip);
  kdu_coords min = comp_region.pos;
  kdu_coords lim = min + comp_region.size;
  if (comp_idx >= 0)
    { 
      if ((!state->construction_finalized) && (comp_idx >= 0) &&
          want_output_comps && state->uses_mct)
        state->finalize_construction();
      kd_comp_info *ci = NULL;
      if (want_output_comps &&
          (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
        {
          if (comp_idx < state->num_apparent_output_components)
            {
              kd_output_comp_info *oci = state->output_comp_info +
                state->output_comp_info[comp_idx].from_apparent;
              ci = oci->subsampling_ref;
            }
        }
      else
        {
          if (comp_idx < state->num_apparent_components)
            ci = state->comp_info[comp_idx].from_apparent;
        }
      if (ci == NULL)
        { KDU_ERROR_DEV(e,0x24091306); e <<
          KDU_TXT("Illegal component index supplied in call to "
                  "`kdu_codesteram::map_region'.");
        }
      min.x *= ci->sub_sampling.x << ci->hor_depth[state->discard_levels];
      min.y *= ci->sub_sampling.y << ci->vert_depth[state->discard_levels];
      lim.x *= ci->sub_sampling.x << ci->hor_depth[state->discard_levels];
      lim.y *= ci->sub_sampling.y << ci->vert_depth[state->discard_levels];
    }
  canvas_region.pos = min;
  canvas_region.size = lim-min;
  canvas_region &= state->canvas;
}

/*****************************************************************************/
/*                       kdu_codestream::get_valid_tiles                     */
/*****************************************************************************/

void
  kdu_codestream::get_valid_tiles(kdu_dims &indices)
{
  kdu_coords min = state->region.pos - state->tile_partition.pos;
  kdu_coords lim = min + state->region.size;

  indices.pos.x = floor_ratio(min.x,state->tile_partition.size.x);
  indices.size.x =
    ceil_ratio(lim.x,state->tile_partition.size.x)-indices.pos.x;
  if (lim.x <= min.x)
    indices.size.x = 0;
  indices.pos.y = floor_ratio(min.y,state->tile_partition.size.y);
  indices.size.y =
    ceil_ratio(lim.y,state->tile_partition.size.y) - indices.pos.y;
  if (lim.y <= min.y)
    indices.size.y = 0;
  indices.to_apparent(state->transpose,state->vflip,state->hflip);
}

/*****************************************************************************/
/*                          kdu_codestream::find_tile                        */
/*****************************************************************************/

bool
  kdu_codestream::find_tile(int comp_idx, kdu_coords loc, kdu_coords &tile_idx,
                            bool want_output_comps)
{
  if (comp_idx < 0)
    return false;
  if ((!state->construction_finalized) && want_output_comps && state->uses_mct)
    state->finalize_construction();
  kd_comp_info *ci = NULL;
  if (want_output_comps &&
      (state->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    {
      if (comp_idx >= state->num_apparent_output_components)
        return false;
      kd_output_comp_info *oci = state->output_comp_info +
        state->output_comp_info[comp_idx].from_apparent;
      ci = oci->subsampling_ref;
    }
  else
    {
      if (comp_idx >= state->num_apparent_components)
        return false;
      ci = state->comp_info[comp_idx].from_apparent;
    }

  loc.from_apparent(state->transpose,state->vflip,state->hflip);
  loc.x *= ci->sub_sampling.x << ci->hor_depth[state->discard_levels];
  loc.y *= ci->sub_sampling.y << ci->vert_depth[state->discard_levels];
  loc -= state->region.pos;
  if ((loc.x < 0) || (loc.y < 0) ||
      (loc.x >= state->region.size.x) || (loc.y >= state->region.size.y))
    return false;
  loc += state->region.pos;
  loc -= state->tile_partition.pos;
  tile_idx.x = floor_ratio(loc.x,state->tile_partition.size.x);
  tile_idx.y = floor_ratio(loc.y,state->tile_partition.size.y);
  tile_idx.to_apparent(state->transpose,state->vflip,state->hflip);
  return true;
}

/*****************************************************************************/
/*                         kdu_codestream::open_tile                         */
/*****************************************************************************/

kdu_tile
  kdu_codestream::open_tile(kdu_coords tile_idx, kdu_thread_env *env)
{
  if (env != NULL)
    { 
      state->start_multi_threading(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  state->tiles_accessed = true;
  if (!state->construction_finalized)
    state->finalize_construction();

  tile_idx.from_apparent(state->transpose,state->vflip,state->hflip);
  assert((tile_idx.x >= 0) && (tile_idx.x < state->tile_span.x) &&
         (tile_idx.y >= 0) && (tile_idx.y < state->tile_span.y));

  kdu_coords rel_idx = tile_idx - state->tile_indices.pos;
  assert((rel_idx.x >= 0) && (rel_idx.x < state->tile_indices.size.x) &&
         (rel_idx.y >= 0) && (rel_idx.y < state->tile_indices.size.y));
  kd_tile_ref *tref = state->tile_refs +
    rel_idx.x + rel_idx.y*state->tile_indices.size.x;

  if (tref->flags.get() & KD_TREF_FLAGS_CLOSE_PENDING)
    { 
      kd_tile *tp = tref->tile;
      if ((tp != NULL) && (tp != KD_EXPIRED_TILE))
        tp->close(env); // Call will cancel the pending close flag
      else
        assert(0); // Should not happen that close is pending but alredy closed
    }
  kd_tile *tp = NULL;
  bool permanently_closed = ((tref->flags.get() & KD_TREF_FLAGS_CLOSED) != 0);
  if (!permanently_closed)
    { 
      if ((tp = tref->tile) == NULL)
        tp = state->create_tile(tile_idx);
      else
        { 
          assert(tp != KD_EXPIRED_TILE);
          if (tp->needs_reinit)
            { // Must have restarted the code-stream since the tile was created
              assert(state->allow_restart && (tref->tpart_head == NULL));
              tp->reinitialize();
            }
        }
      assert(tp != NULL);
      if ((tp == KD_EXPIRED_TILE) || tp->closed)
        { // The `KD_TREF_FLAGS_CLOSED' flag should be set.
          tp = NULL;
          tref->flags.exchange_or(KD_TREF_FLAGS_CLOSED);
          permanently_closed = true;
        }
    }
  if (permanently_closed)
    { KDU_ERROR_DEV(e,58); e << 
      KDU_TXT("Attempting to open a tile which has already been "
              "discarded or permanently closed!");
    }
  assert((tp != NULL) && (tp == tref->tile) && (tp->tile_ref == tref));
  tp->open(env); // Cancels any pending close flag, install the open
                 // flag and also wake any thread waiting for the open
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
  
  if (tp == NULL)
    tref = NULL;
  return kdu_tile(tref);
}

/*****************************************************************************/
/*                       kdu_codestream::create_tile                         */
/*****************************************************************************/

void kdu_codestream::create_tile(kdu_coords tile_idx, kdu_thread_env *env)
{
  if (env != NULL)
    { 
      state->start_multi_threading(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  state->tiles_accessed = true;
  if (!state->construction_finalized)
    state->finalize_construction();
  
  tile_idx.from_apparent(state->transpose,state->vflip,state->hflip);
  assert((tile_idx.x >= 0) && (tile_idx.x < state->tile_span.x) &&
         (tile_idx.y >= 0) && (tile_idx.y < state->tile_span.y));
  
  kdu_coords rel_idx = tile_idx - state->tile_indices.pos;
  assert((rel_idx.x >= 0) && (rel_idx.x < state->tile_indices.size.x) &&
         (rel_idx.y >= 0) && (rel_idx.y < state->tile_indices.size.y));
  kd_tile_ref *tref = state->tile_refs +
    rel_idx.x + rel_idx.y*state->tile_indices.size.x;
  if (!(tref->flags.get() & (KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSED)))
    {   
      kd_tile *tp = tref->tile;
      if (tp == NULL)
        tp = state->create_tile(tile_idx);
      else if ((tp != KD_EXPIRED_TILE) && tp->needs_reinit)
        { // We must have restarted the code-stream, since the tile was created
          assert(state->allow_restart && (tref->tpart_head == NULL));
          tp->reinitialize();
        }
      assert(tp != NULL);
      if ((tp == KD_EXPIRED_TILE) || tp->closed)
        { // The `KD_TREF_FLAGS_CLOSED' flag should be set.
          tref->flags.exchange_or(KD_TREF_FLAGS_CLOSED);
        }
    }
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                        kdu_codestream::open_tiles                         */
/*****************************************************************************/

void
  kdu_codestream::open_tiles(kdu_dims tile_indices, bool bkgnd_open,
                             kdu_thread_env *env)
{
  bool have_lock = false;  
  if (env != NULL)
    state->start_multi_threading(env);
  if (!(state->construction_finalized && state->tiles_accessed))
    { 
      if (env != NULL)
        { 
          state->acquire_lock(KD_THREADLOCK_GENERAL,env);
          state->process_pending_precincts();
          have_lock = true;
        }
      if (!state->construction_finalized)
        state->finalize_construction();
      state->tiles_accessed = true;
    }
  
  tile_indices.from_apparent(state->transpose,state->vflip,state->hflip);
  tile_indices &= state->tile_indices;
  kdu_coords idx;
  for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < tile_indices.size.x; idx.x++)
      { 
        kdu_coords abs_idx = idx + tile_indices.pos;
        kdu_coords rel_idx = abs_idx - state->tile_indices.pos;
        kd_tile_ref *tref = state->tile_refs +
          rel_idx.x + rel_idx.y*state->tile_indices.size.x;
        if (tref->flags.get_barrier() &
            (KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSED))
          continue; // This one is already open
        if (bkgnd_open && (env != NULL) && !have_lock)
          { // Schedule opening operation for later
            kdu_int32 old_flags, new_flags;
            do { // Enter compare-and-set loop
              new_flags = old_flags = tref->flags.get();
              if (!(old_flags & (KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSED)))
                new_flags |= KD_TREF_FLAGS_OPEN_PENDING|KD_TREF_FLAGS_ENQUEUED;
            } while (!tref->flags.compare_and_set(old_flags,new_flags));
            if ((new_flags ^ old_flags) & KD_TREF_FLAGS_ENQUEUED)
              { // Otherwise it is already scheduled for background processing
                state->thread_context->append_to_tile_queue(tref);
                state->thread_context->schedule_tile_processing(env);
              }
            continue;
          }
        
        // If we get here, the opening operation should be performed right away
        if ((env != NULL) && !have_lock)
          { 
            state->acquire_lock(KD_THREADLOCK_GENERAL,env);
            state->process_pending_precincts();
            have_lock = true;
          }        
        kd_tile *tp = tref->tile;
        if (tref->flags.get() & KD_TREF_FLAGS_CLOSE_PENDING)
          { 
            if ((tp != NULL) && (tp != KD_EXPIRED_TILE))
              tp = tp->close(env); // Call will cancel the pending close flag
            else
              assert(0); // Should not happen
          }
        if (tref->flags.get() & KD_TREF_FLAGS_CLOSED)
          continue; // Ignore tiles that cannot be opened
        if (tp == NULL)
          tp = state->create_tile(abs_idx);
        else if ((tp != KD_EXPIRED_TILE) && tp->needs_reinit)
          { // Must have restarted the code-stream since the tile was created
            assert(state->allow_restart && (tref->tpart_head == NULL));
            tp->reinitialize();
          }
        if ((tp == KD_EXPIRED_TILE) || tp->closed)
          { // The `KD_TREF_FLAGS_CLOSED' flag should be set
            tref->flags.exchange_or(KD_TREF_FLAGS_CLOSED);
            continue; // Ignore tiles that cannot be opened
          }
        assert(tp->tile_ref == tref);
        tp->open(env); // Call will cancel any pending close flag, install the
                       // open flag and wake any thread waiting for the open
        if (bkgnd_open && (env != NULL))
          { // We must have acquired the lock because no tiles had been opened
            // so we have now opened the first one in-line, but should not
            // open any more in this way.  If there are others, they will be
            // scheduled for background opening.
            state->release_lock(KD_THREADLOCK_GENERAL,env);
            have_lock = false;
          }
      }
  
  if (have_lock)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                        kdu_codestream::close_tiles                        */
/*****************************************************************************/

void
  kdu_codestream::close_tiles(kdu_dims tile_indices, kdu_thread_env *env)
{
  if (!state->tiles_accessed)
    return; // Nothing to do
  if (env != NULL)
    { 
      state->start_multi_threading(env);
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
    }
  
  tile_indices.from_apparent(state->transpose,state->vflip,state->hflip);
  tile_indices &= state->tile_indices;
  kdu_coords idx;
  for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < tile_indices.size.x; idx.x++)
      { 
        kdu_coords abs_idx = idx + tile_indices.pos;
        kdu_coords rel_idx = abs_idx - state->tile_indices.pos;
        kd_tile_ref *tref = state->tile_refs +
          rel_idx.x + rel_idx.y*state->tile_indices.size.x;
        kdu_int32 flags = tref->flags.get();
        if (flags & KD_TREF_FLAGS_OPEN_PENDING)
          { // Just cancel the scheduled open operation and continue
            tref->flags.set(flags & ~KD_TREF_FLAGS_OPEN_PENDING);
          }
        else if (flags & (KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSE_PENDING))
          { // We want to close the tile right here
            if ((tref->tile != NULL) && (tref->tile != KD_EXPIRED_TILE))
              tref->tile->close(env);
          }
      }
  
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                        kdu_codestream::access_tile                        */
/*****************************************************************************/

kdu_tile
  kdu_codestream::access_tile(kdu_coords tile_idx, bool wait_for_bkgnd,
                              kdu_thread_env *env)
{
  if (!state->tiles_accessed)
    return kdu_tile();
  tile_idx.from_apparent(state->transpose,state->vflip,state->hflip);
  assert((tile_idx.x >= 0) && (tile_idx.x < state->tile_span.x) &&
         (tile_idx.y >= 0) && (tile_idx.y < state->tile_span.y));
  
  kdu_coords rel_idx = tile_idx - state->tile_indices.pos;
  assert((rel_idx.x >= 0) && (rel_idx.x < state->tile_indices.size.x) &&
         (rel_idx.y >= 0) && (rel_idx.y < state->tile_indices.size.y));
  kd_tile_ref *tref = state->tile_refs +
    rel_idx.x + rel_idx.y*state->tile_indices.size.x;
  kd_tile *tp = NULL;
  if (tref->flags.get_barrier() & KD_TREF_FLAGS_OPEN)
    { 
      tp = tref->tile;
      if (tp == KD_EXPIRED_TILE)
        { // Should not happen
          assert(0);
          tp = NULL;
        }
    }
  else if (wait_for_bkgnd && (env != NULL))
    { 
      bool other_waiters_exist = (tref->wakeup != NULL);
      if (!other_waiters_exist)
        { 
          tref->wakeup = env->get_condition();
          kdu_int32 old_flags, new_flags;
          do { // Enter compare-and-set loop
            new_flags = old_flags = tref->flags.get();
            if (old_flags & KD_TREF_FLAGS_OPEN_PENDING)
              new_flags |= KD_TREF_FLAGS_NEED_WAKEUP;
          } while (!tref->flags.compare_and_set(old_flags,new_flags));
          if (old_flags & KD_TREF_FLAGS_NEED_WAKEUP)
            other_waiters_exist = true;
          if (old_flags != new_flags)
            env->wait_for_condition();
          tref->wakeup = NULL;
        }
      if (other_waiters_exist)
        { KDU_ERROR_DEV(e,0x24041401); e <<
          KDU_TXT("Attempting to wait for a background tile opening "
                  "operation inside `kdu_codestream::access_tile', while "
                  "another thread appears to be concurrently waiting upon "
                  "the same condition -- see API docs for an explanation!");
        }
      if (tref->flags.get_barrier() & KD_TREF_FLAGS_OPEN)
        { 
          tp = tref->tile;
          if (tp == KD_EXPIRED_TILE)
            { // Should not happen
              assert(0);
              tp = NULL;
            }
        }
      else if (tref->flags.get() & KD_TREF_FLAGS_CLOSED)
        { KDU_ERROR_DEV(e,0x25041402); e <<
          KDU_TXT("Attempting to use access a tile which has already been "
                  "discarded or permanently closed!");
        }
    }
  if (tp == NULL)
    tref = NULL;
  return kdu_tile(tref);
}

/*****************************************************************************/
/*                        kdu_codestream::get_comment                        */
/*****************************************************************************/

kdu_codestream_comment
  kdu_codestream::get_comment(kdu_codestream_comment prev)
{
  kdu_codestream_comment result;
  if (state != NULL)
    {
      result.state = state->comhead;
      if (prev.state != NULL)
        result.state = prev.state->next;
    }
  return result;
}

/*****************************************************************************/
/*                        kdu_codestream::add_comment                        */
/*****************************************************************************/

kdu_codestream_comment
  kdu_codestream::add_comment()
{
  kdu_codestream_comment result;
  if (state != NULL)
    {
      result.state = new kd_codestream_comment;
      if (state->comtail == NULL)
        state->comhead = state->comtail = result.state;
      else
        state->comtail = state->comtail->next = result.state;
      result.state = state->comtail;
    }
  return result;
}

/*****************************************************************************/
/*                          kdu_codestream::flush                            */
/*****************************************************************************/

void
  kdu_codestream::flush(kdu_long *layer_bytes, int num_layer_specs,
                        kdu_uint16 *thresholds, bool trim_to_rate,
                        bool record_in_comseg, double tolerance,
                        kdu_thread_env *env, int flags)
{
  assert((state->out != NULL) && (num_layer_specs > 0));
  if (env != NULL)
    { 
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  else if (state->thread_context != NULL)
    { KDU_ERROR_DEV(e,0x27011201); e <<
      KDU_TXT("Attempting to invoke `kdu_codestream::flush' with a NULL "
              "`env' argument (i.e., without multi-threaded protection) "
              "without first using `kdu_thread_env::cs_terminate' to "
              "terminate background processing within the codestream "
              "machinery.  This error is most likely caused by a "
              "transition to Kakadu v7 without proper attention to the "
              "use of the new `cs_terminate' function.  See the demo "
              "applications for examples of its use.");
    }
  if (!state->construction_finalized)
    state->finalize_construction();

  // Start by maintaining the internal record of slope or size targets to
  // ensure consistency between multiple calls to this function.
  int n;
  if (state->target_sizes == NULL)
    { // First call to `flush'
      assert((state->layer_thresholds==NULL) && (state->layer_sizes==NULL) &&
             (state->tmp_layer_sizes == NULL) &&
             (state->expected_sizes == NULL) &&
             (state->target_min_sizes == NULL) &&
             (state->target_thresholds==NULL));
      state->rate_tolerance = (float) tolerance;
      state->record_in_comseg = record_in_comseg;
      state->trim_to_rate = trim_to_rate;
      state->using_slopes = ((thresholds != NULL) && (thresholds[0] != 0) &&
                             !(flags & KDU_FLUSH_THRESHOLDS_ARE_HINTS));
      state->trans_out_non_empty_layers = 0;
      state->trans_out_max_bytes = 0; // Not doing `trans_out'
      state->using_min_sizes = false;
      if (state->using_slopes && (layer_bytes != NULL) &&
          (flags & KDU_FLUSH_USES_THRESHOLDS_AND_SIZES))
        { // See if we need a `target_min_sizes' array
          for (n=0; n < num_layer_specs; n++)
            if (layer_bytes[n] != 0)
              { state->using_min_sizes = true; break; }
        }
      state->num_sized_layers = num_layer_specs;
      state->layer_sizes = new kdu_long[num_layer_specs];
      state->tmp_layer_sizes = new kdu_long[num_layer_specs];
      state->target_sizes = new kdu_long[num_layer_specs];
      state->expected_sizes = new kdu_long[num_layer_specs];
      state->layer_thresholds = new kdu_uint16[num_layer_specs];
      if (state->using_slopes)
        state->target_thresholds = new kdu_uint16[num_layer_specs];
      if (state->using_min_sizes)
        state->target_min_sizes = new kdu_long[num_layer_specs];

      for (n=0; n < num_layer_specs; n++)
        { 
          state->layer_sizes[n] = state->tmp_layer_sizes[n] = 0;
          state->target_sizes[n] = 0;
          state->expected_sizes[n] = 0;
          state->layer_thresholds[n] = 0;
          if (state->using_slopes)
            { 
              if ((n > 0) && (thresholds[n] > state->target_thresholds[n-1]))
                state->target_thresholds[n] = state->target_thresholds[n-1];
              else
                state->target_thresholds[n] = thresholds[n];
              if (state->using_min_sizes)
                state->target_min_sizes[n] = layer_bytes[n];
            }
          else if (layer_bytes != NULL)
            state->target_sizes[n] = layer_bytes[n];
          if ((thresholds != NULL) && (flags & KDU_FLUSH_THRESHOLDS_ARE_HINTS))
            state->layer_thresholds[n] = thresholds[n];
        }
    }
  else
    state->check_incremental_flush_consistency(num_layer_specs);
  if (state->using_slopes && (thresholds != NULL) && (thresholds[0] != 0))
    { // Update slopes with the new ones supplied by the caller
      // It is safe to update slope thresholds between incremental flushing
      // operations, but not to update layer size targets.
      for (n=0; n < state->num_sized_layers; n++)
        state->layer_thresholds[n] = thresholds[n];
    }
  state->flush_if_ready(NULL);
  
  // Write results back to user-supplied arrays, if available
  if (thresholds != NULL)
    for (n=0; n < num_layer_specs; n++)
      thresholds[n] = state->layer_thresholds[n];
  if (layer_bytes != NULL)
    {
      kdu_long generated_bytes=0;
      for (n=0; n < num_layer_specs; n++)
        layer_bytes[n] = (generated_bytes += state->layer_sizes[n]);
    }

  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                         kdu_codestream::trans_out                         */
/*****************************************************************************/

int
  kdu_codestream::trans_out(kdu_long max_bytes,
                            kdu_long *layer_bytes, int layer_bytes_entries,
                            bool record_in_comseg, kdu_thread_env *env)
{
  assert(state->out != NULL);
  
  if (env != NULL)
    { 
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  else if (state->thread_context != NULL)
    { KDU_ERROR_DEV(e,0x27011202); e <<
      KDU_TXT("Attempting to invoke `kdu_codestream::trans_out' with a NULL "
              "`env' argument (i.e., without multi-threaded protection) "
              "without first using `kdu_thread_env::cs_terminate' to "
              "terminate background processing within the codestream "
              "machinery.  This error is most likely caused by a "
              "transition to Kakadu v7 without proper attention to the "
              "use of the new `cs_terminate' function.  See the demo "
              "applications for examples of its use.");
    }  

  if (!state->construction_finalized)
    state->finalize_construction();
  if (state->reslength_constraints_used && !state->reslength_warning_issued)
    { KDU_WARNING_DEV(w,0x10110801); w <<
        KDU_TXT("You cannot currently use the `Creslength' parameter "
                "attribute in conjunction with `kdu_codestream::trans_out' "
                "(i.e., you cannot use this parameter to control the "
                "compressed lengths of individual resolutions during "
                "transcoding).  Ignoring the `Creslength' constraints.");
      state->reslength_warning_issued = true;
    }

  int n;
  if (max_bytes <= 0)
    max_bytes = KDU_LONG_HUGE;
  if (state->target_sizes == NULL)
    { // First call to `flush'; set up `num_sized_layers' and internal arrays
      // on this first call
      assert((state->layer_thresholds==NULL) && (state->layer_sizes==NULL) &&
             (state->tmp_layer_sizes==NULL) &&
             (state->expected_sizes==NULL) &&
             (state->target_min_sizes==NULL) &&
             (state->target_thresholds==NULL));
      state->record_in_comseg = record_in_comseg;
      state->trim_to_rate = false;
      state->using_slopes = false;
      state->using_min_sizes = false;
      state->trans_out_non_empty_layers = 0;
      state->trans_out_max_bytes = max_bytes;

      state->num_sized_layers = 1;
      for (kd_tile *tile=state->tiles_in_progress_head;
           tile != NULL; tile=tile->in_progress_next)
        if (tile->num_layers > state->num_sized_layers)
          state->num_sized_layers = tile->num_layers;
      state->layer_sizes = new kdu_long[state->num_sized_layers];
      state->tmp_layer_sizes = new kdu_long[state->num_sized_layers];
      state->target_sizes = new kdu_long[state->num_sized_layers];
      state->expected_sizes = new kdu_long[state->num_sized_layers];
      state->layer_thresholds = new kdu_uint16[state->num_sized_layers];
      state->target_min_sizes = NULL;   // Reinforce the fact that we don't use
      state->target_thresholds = NULL; // these arrays during transcoding
      for (n=0; n < state->num_sized_layers; n++)
        { 
          state->layer_sizes[n] = state->tmp_layer_sizes[n] = 0;
          state->target_sizes[n] = 0;
          state->expected_sizes[n] = 0;
          state->layer_thresholds[n] = 0xFFFF - n - 1;
        }
      state->target_sizes[n-1] = max_bytes;
    }
  else
    assert(state->num_sized_layers > 0);

  state->flush_if_ready(NULL);

  if (layer_bytes != NULL)
    {
      kdu_long generated_bytes = 0;
      for (n=0; n < layer_bytes_entries; n++)
        if (n < state->num_sized_layers)
          layer_bytes[n] = (generated_bytes += state->layer_sizes[n]);
        else
          layer_bytes[n] = generated_bytes;
    }

  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
  return state->trans_out_non_empty_layers;
}

/*****************************************************************************/
/*                      kdu_codestream::ready_for_flush                      */
/*****************************************************************************/

bool
  kdu_codestream::ready_for_flush(kdu_thread_env *env)
{
  if ((state == NULL) || (state->out == NULL))
    return false;

  if (env != NULL)
    { 
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  else if (state->thread_context != NULL)
    { KDU_ERROR_DEV(e,0x27011203); e <<
      KDU_TXT("Attempting to invoke `kdu_codestream::ready_for_flush' with "
              "a NULL `env' argument (i.e., without multi-threaded "
              "protection) without first using `kdu_thread_env::cs_terminate' "
              "to terminate background processing within the codestream "
              "machinery.  This error is most likely caused by a "
              "transition to Kakadu v7 without proper attention to the "
              "use of the new `cs_terminate' function.  See the demo "
              "applications for examples of its use.");
    }  
  
  bool result = state->ready_for_flush();
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
  return result;
}

/*****************************************************************************/
/*                         kdu_codestream::auto_flush                        */
/*****************************************************************************/

void
  kdu_codestream::auto_flush(int first_tile_comp_trigger_point,
                             int tile_comp_trigger_interval,
                             int first_incr_trigger_point,
                             int incr_trigger_interval,
                             const kdu_long *layer_bytes,
                             int num_layer_specs,
                             const kdu_uint16 *thresholds,
                             bool trim_to_rate, bool record_in_comseg,
                             double tolerance, kdu_thread_env *env,
                             int flags)
{
  assert((state->out != NULL) && (num_layer_specs > 0));
  if ((first_tile_comp_trigger_point <= 0) ||
      (tile_comp_trigger_interval <= 0))
    { 
      assert(0);
      return;
    }
  if ((first_incr_trigger_point < 0) || (incr_trigger_interval < 0))
    first_incr_trigger_point = incr_trigger_interval = 0;
  
  if (state->reslength_constraints_used)
    { KDU_WARNING(w,0x08011301); w <<
      KDU_TXT("With incremental codestream flushing, you cannot currently "
              "expect the supplied `Creslengths' constraints to be "
              "applied correctly -- this weakness can be corrected in "
              "the future if there is a good reason for wanting both "
              "features to work together.");
    }
  
  if (env != NULL)
    { 
      state->start_multi_threading(env); // Just in case
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  
  if (!state->construction_finalized)
    state->finalize_construction();
  
  // Start by maintaining the internal record of slope or size targets to
  // ensure consistency between multiple calls to this function.
  int n;
  if (state->target_sizes == NULL)
    { // First call to `flush'
      assert((state->layer_thresholds==NULL) && (state->layer_sizes==NULL) &&
             (state->tmp_layer_sizes == NULL) &&
             (state->expected_sizes == NULL) &&
             (state->target_min_sizes == NULL) &&
             (state->target_thresholds==NULL));
      state->rate_tolerance = (float) tolerance;
      state->record_in_comseg = record_in_comseg;
      state->trim_to_rate = trim_to_rate;
      state->using_slopes = ((thresholds != NULL) && (thresholds[0] != 0) &&
                             !(flags & KDU_FLUSH_THRESHOLDS_ARE_HINTS));
      state->trans_out_non_empty_layers = 0;
      state->trans_out_max_bytes = 0; // Not doing `trans_out'
      state->using_min_sizes = false;
      if (state->using_slopes && (layer_bytes != NULL) &&
          (flags & KDU_FLUSH_USES_THRESHOLDS_AND_SIZES))
        { // See if we need a `target_min_sizes' array
          for (n=0; n < num_layer_specs; n++)
            if (layer_bytes[n] != 0)
              { state->using_min_sizes = true; break; }
        }
      state->num_sized_layers = num_layer_specs;
      state->layer_sizes = new kdu_long[num_layer_specs];
      state->tmp_layer_sizes = new kdu_long[num_layer_specs];
      state->target_sizes = new kdu_long[num_layer_specs];
      state->expected_sizes = new kdu_long[num_layer_specs];
      state->layer_thresholds = new kdu_uint16[num_layer_specs];
      if (state->using_slopes)
        state->target_thresholds = new kdu_uint16[num_layer_specs];
      if (state->using_min_sizes)
        state->target_min_sizes = new kdu_long[num_layer_specs];
      
      for (n=0; n < num_layer_specs; n++)
        { 
          state->layer_sizes[n] = state->tmp_layer_sizes[n] = 0;
          state->target_sizes[n] = 0;
          state->expected_sizes[n] = 0;
          state->layer_thresholds[n] = 0;
          if (state->using_slopes)
            state->target_thresholds[n] = thresholds[n];
          else if (layer_bytes != NULL)
            state->target_sizes[n] = layer_bytes[n];
          if (state->using_min_sizes)
            state->target_min_sizes[n] = layer_bytes[n];
          if ((thresholds != NULL) && (flags & KDU_FLUSH_THRESHOLDS_ARE_HINTS))
            state->layer_thresholds[n] = thresholds[n];
        }
    }
  else
    state->check_incremental_flush_consistency(num_layer_specs);  
  if (state->using_slopes && (thresholds != NULL) && (thresholds[0] != 0))
    { // Update slopes with the new ones supplied by the caller
      // It is safe to update slope thresholds between incremental flushing
      // operations, but not to update layer size targets.
      for (n=0; n < state->num_sized_layers; n++)
        state->target_thresholds[n] = thresholds[n];
    }
  state->tc_flush_interval = tile_comp_trigger_interval;
  state->tc_flush_counter.set(first_tile_comp_trigger_point);
  state->incr_flush_interval = incr_trigger_interval;
  state->incr_flush_counter.set(first_incr_trigger_point);
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                      kdu_codestream::auto_trans_out                       */
/*****************************************************************************/

void
  kdu_codestream::auto_trans_out(int first_tile_comp_trigger_point,
                                 int tile_comp_trigger_interval,
                                 int first_incr_trigger_point,
                                 int incr_trigger_interval,
                                 kdu_long max_bytes, bool record_in_comseg,
                                 kdu_thread_env *env)
{
  assert(state->out != NULL);
  if ((first_tile_comp_trigger_point <= 0) ||
      (tile_comp_trigger_interval <= 0))
    { 
      assert(0);
      return;
    }
  if ((first_incr_trigger_point < 0) || (incr_trigger_interval < 0))
    first_incr_trigger_point = incr_trigger_interval = 0;
  
  if (env != NULL)
    { 
      state->start_multi_threading(env); // Just in case
      state->acquire_lock(KD_THREADLOCK_GENERAL,env);
      state->process_pending_precincts();
    }
  
  if (!state->construction_finalized)
    state->finalize_construction();
  if (state->reslength_constraints_used && !state->reslength_warning_issued)
    { KDU_WARNING_DEV(w,0x14091101); w <<
      KDU_TXT("You cannot currently use the `Creslength' parameter "
              "attribute in conjunction with `kdu_codestream::trans_out' "
              "(i.e., you cannot use this parameter to control the "
              "compressed lengths of individual resolutions during "
              "transcoding).  Ignoring the `Creslength' constraints.");
      state->reslength_warning_issued = true;
    }
  
  int n;
  if (max_bytes <= 0)
    max_bytes = KDU_LONG_HUGE;
  if (state->target_sizes == NULL)
    { // First call to `flush'; set up `num_sized_layers' and internal arrays
      // on this first call
      assert((state->layer_thresholds==NULL) && (state->layer_sizes==NULL) &&
             (state->tmp_layer_sizes==NULL) &&
             (state->expected_sizes==NULL) &&
             (state->target_min_sizes==NULL) &&
             (state->target_thresholds==NULL));
      state->record_in_comseg = record_in_comseg;
      state->trim_to_rate = false;
      state->using_slopes = false;
      state->using_min_sizes = false;
      state->trans_out_non_empty_layers = 0;
      state->trans_out_max_bytes = max_bytes;
      
      state->num_sized_layers = 1;
      for (kd_tile *tile=state->tiles_in_progress_head;
           tile != NULL; tile=tile->in_progress_next)
        if (tile->num_layers > state->num_sized_layers)
          state->num_sized_layers = tile->num_layers;
      state->layer_sizes = new kdu_long[state->num_sized_layers];
      state->tmp_layer_sizes = new kdu_long[state->num_sized_layers];
      state->target_sizes = new kdu_long[state->num_sized_layers];
      state->expected_sizes = new kdu_long[state->num_sized_layers];
      state->layer_thresholds = new kdu_uint16[state->num_sized_layers];
      state->target_min_sizes = NULL;   // Reinforce the fact that we don't use
      state->target_thresholds = NULL; // these arrays during transcoding
      for (n=0; n < state->num_sized_layers; n++)
        { 
          state->layer_sizes[n] = state->tmp_layer_sizes[n] = 0;
          state->target_sizes[n] = 0;
          state->expected_sizes[n] = 0;
          state->layer_thresholds[n] = 0xFFFF - n - 1;
        }
      state->target_sizes[n-1] = max_bytes;
    }
  else
    assert(state->num_sized_layers > 0);

  state->tc_flush_interval = tile_comp_trigger_interval;
  state->tc_flush_counter.set(first_tile_comp_trigger_point);
  state->incr_flush_interval = incr_trigger_interval;
  state->incr_flush_counter.set(first_incr_trigger_point);
  if (env != NULL)
    state->release_lock(KD_THREADLOCK_GENERAL,env);
}

/*****************************************************************************/
/*                      kdu_codestream::add_flush_stats                      */
/*****************************************************************************/

kdu_flush_stats
  kdu_codestream::add_flush_stats(int initial_frame_idx)
{ 
  if (state == NULL)
    return kdu_flush_stats();
  if (state->tiles_accessed || (state->flush_stats != NULL) ||
      (state->out == NULL))
    { KDU_ERROR_DEV(e,0x11091601); e <<
      KDU_TXT("The `kdu_codestream::add_flush_stats' function may only "
              "be used once, and then only prior to the first `open_tile' "
              "call.  Moreover, the function may only be used with "
              "codestreams created for output -- i.e., compression.");
    }
  kd_flush_stats *fstats = new kd_flush_stats;
  state->flush_stats = fstats;
  fstats->ref_count = 1;
  fstats->active_set = 0;
  fstats->aux_set_parity = 0;
  fstats->summary_slope[0] = fstats->summary_slope[1] = 0;
  fstats->frame_valid[0].set(initial_frame_idx & 0x7FFFFFFF);
  fstats->frame_valid[1].set(0);
  fstats->auto_advance_frame_sep = 0;
  fstats->num_components = state->num_components;
  fstats->num_tiles = ((kdu_long)state->tile_span.x) * state->tile_span.y;
  fstats->num_tile_comps = fstats->num_tiles * fstats->num_components;
  return kdu_flush_stats(fstats);
}

/*****************************************************************************/
/*                    kdu_codestream::attach_flush_stats                     */
/*****************************************************************************/

void
  kdu_codestream::attach_flush_stats(kdu_core::kdu_flush_stats flush_stats)
{
  if (state == NULL)
    return;
  if (state->tiles_accessed || (state->flush_stats == NULL) ||
      (state->aux_flush_stats != NULL) ||
      (flush_stats.state == state->flush_stats))
    { KDU_ERROR_DEV(e,0x12091606); e <<
      KDU_TXT("The `kdu_codestream::attach_flush_stats' function may only "
              "be used once, and then only prior to the first `open_tile' "
              "call.  Moreover, you may only attach a `kdu_flush_stats' "
              "object from another codestream to a `kdu_codestream' "
              "interface that already has its own one -- i.e., the target "
              "object must have had `kdu_codestream::add_flush_stats' "
              "invoked already.");
    }
  state->aux_flush_stats = flush_stats.state;
  state->aux_flush_stats->add_ref();
  if (!state->aux_flush_stats->check_compatible(state->flush_stats))
    { KDU_ERROR_DEV(e,0x12091607); e <<
      KDU_TXT("The `kdu_codestream::attach_flush_stats' can only be used "
              "to share flush statistics between two `kdu_codestream' "
              "interfaces that have been created with consistent coding "
              "parameters.  In particular, the number of tiles, components "
              "and so forth must all be identical.");
    }
  int cur_frame = state->flush_stats->frame_valid[0].get();
  int aux_frame = state->aux_flush_stats->frame_valid[0].get();
  if ((state->flush_stats->frame_valid[1].get() != 0) ||
      (state->aux_flush_stats->frame_valid[1].get() != 0) ||
      (cur_frame == aux_frame) ||
      ((cur_frame | aux_frame) & 0x80000000))
    { KDU_ERROR_DEV(e,0x12091608); e <<
      KDU_TXT("Calls to `kdu_codestream::attach_flush_stats' must precede "
              "the generation of content in either of the `kdu_codestream' "
              "objects that are involved in the sharing of flush "
              "statistics.");
    }
  state->flush_stats->aux_set_parity = (aux_frame < cur_frame)?0:1;
}

/*****************************************************************************/
/*                      kdu_codestream::get_total_bytes                      */
/*****************************************************************************/

kdu_long
  kdu_codestream::get_total_bytes(bool exclude_main_header)
{
  if (state == NULL)
    return 0;
  kdu_long excluded = 0;
  if (exclude_main_header)
    excluded = state->header_length;
  if (state->in != NULL)
    return (state->in->get_bytes_read() -
            state->in->get_suspended_bytes() - excluded);
  else if (state->out != NULL)
    return (state->out->get_bytes_written() - excluded);
  else
    return 0;
}

/*****************************************************************************/
/*                     kdu_codestream::get_packet_bytes                      */
/*****************************************************************************/

kdu_long
  kdu_codestream::get_packet_bytes()
{
  return (state == NULL)?0:state->written_packet_bytes;
}

/*****************************************************************************/
/*                 kdu_codestream::get_packet_header_bytes                   */
/*****************************************************************************/

kdu_long
  kdu_codestream::get_packet_header_bytes()
{
  return (state == NULL)?0:state->written_packet_header_bytes;
}

/*****************************************************************************/
/*                      kdu_codestream::get_num_tparts                       */
/*****************************************************************************/

int
  kdu_codestream::get_num_tparts()
{
  return state->num_completed_tparts;
}

/*****************************************************************************/
/*                   kdu_codestream::collect_timing_stats                    */
/*****************************************************************************/

void
  kdu_codestream::collect_timing_stats(int num_coder_iterations)
{
  if (num_coder_iterations < 0)
    num_coder_iterations = 0;
  state->block->initialize_timing(num_coder_iterations);
  state->timer.reset();
}

/*****************************************************************************/
/*                     kdu_codestream::get_timing_stats                      */
/*****************************************************************************/

double
  kdu_codestream::get_timing_stats(kdu_long *num_samples, bool coder_only)
{
  double system_time = state->timer.get_ellapsed_seconds();
  kdu_long system_samples = 0;
  for (int c=0; c < state->num_apparent_components; c++)
    {
      kdu_dims region; get_dims(c,region);
      system_samples += region.area();
    }

  kdu_long coder_samples;
  double wasted_time=0.0;
  double coder_time =
    state->block->get_timing_stats(coder_samples,wasted_time);
  system_time -= wasted_time;

  if (coder_only)
    {
      if (num_samples != NULL)
        *num_samples = coder_samples;
      return coder_time;
    }
  if (num_samples != NULL)
    *num_samples = system_samples;
  return system_time;
}

/*****************************************************************************/
/*                kdu_codestream::get_compressed_data_memory                 */
/*****************************************************************************/

kdu_long
  kdu_codestream::get_compressed_data_memory(bool get_peak_allocation)
{
  if (get_peak_allocation)
    return state->buf_servers->get_peak_buf_bytes();
  else
    return state->buf_servers->get_current_buf_bytes();
}

/*****************************************************************************/
/*                kdu_codestream::get_compressed_state_memory                */
/*****************************************************************************/

kdu_long
  kdu_codestream::get_compressed_state_memory(bool get_peak_allocation)
{
  if (get_peak_allocation)
    return state->buf_servers->get_peak_structure_bytes();
  else
    return state->buf_servers->get_current_structure_bytes();
}

/*****************************************************************************/
/*                      kdu_codestream::get_cbr_flush_stats                  */
/*****************************************************************************/

int
  kdu_codestream::get_cbr_flush_stats(float &bucket_max_bytes,
                                      float &bucket_mean_bytes,
                                      float &inter_flush_bytes,
                                      kdu_uint16 &min_slope,
                                      kdu_uint16 &max_slope,
                                      float &mean_slope,
                                      float &mean_square_slope,
                                      kdu_long &num_fill_bytes)
{
  int num_flush_sets = state->cbr_fset.num_flush_sets;
  if ((!state->cbr_flush_interval) || (num_flush_sets == 0))
    return 0;
  float inv_flushes = 1.0f / (float)num_flush_sets;
  bucket_max_bytes = (float)state->cbr_fset.max_bucket_fulness;
  bucket_mean_bytes = inv_flushes * (float)state->cbr_fset.acc_bucket_fulness;
  inter_flush_bytes = (((float)state->cbr_fset.nominal_flush_set_samples) *
                       ((float)state->cbr_fset.data_rate));
  min_slope = state->cbr_fset.min_slope_threshold;
  max_slope = state->cbr_fset.max_slope_threshold;
  mean_slope = (inv_flushes * (float)state->cbr_fset.acc_slope_threshold);
  mean_square_slope = (inv_flushes *
                       (float)state->cbr_fset.acc_sq_slope_threshold);
  num_fill_bytes = state->cbr_fset.non_final_fill_bytes;
  return num_flush_sets;
}


/* ========================================================================= */
/*                               kd_flush_stats                              */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_flush_stats::~kd_flush_stats                      */
/*****************************************************************************/

kd_flush_stats::~kd_flush_stats()
{
}

/*****************************************************************************/
/*                          kd_flush_stats::advance                          */
/*****************************************************************************/

int
  kd_flush_stats::advance(int frame_sep)
{
  int old_set = active_set;
  int frame_idx = frame_valid[old_set].get();
  if (!(frame_idx & 0x80000000))
    { KDU_ERROR_DEV(e,0x12091609); e << 
      KDU_TXT("Calling `kdu_flush_stats::advance', perhaps indirectly via "
              "`kdu_codestream::restart', with auto-advance enabled, "
              "before codestream flushing has completed for the associated "
              "codestream!");
    }
  frame_idx += frame_sep;
  frame_idx &= 0x7FFFFFFF;
  int new_set = 1-old_set;
  frame_valid[new_set].set(frame_idx);
  active_set = new_set;
  return frame_idx;
}


/* ========================================================================= */
/*                              kdu_flush_stats                              */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdu_flush_stats::advance                         */
/*****************************************************************************/

int
  kdu_flush_stats::advance(int frame_sep)
{
  if (state == NULL)
    return 0;
  if (frame_sep < 1)
    { KDU_ERROR_DEV(e,0x12091610); e << 
      KDU_TXT("`kdu_flush_stats::advance' requires a positive `frame_sep' "
              "argument.");
    }
  if (state->auto_advance_frame_sep)
    { KDU_ERROR_DEV(e,0x12091613); e <<
      KDU_TXT("The `kdu_flush_stats::advance' function should not be "
              "explicitly called if a non-zero auto-advance value has "
              "already been configured via `kdu_flush_set::auto_advance'.");
    }
  return state->advance(frame_sep);
}
  
/*****************************************************************************/
/*                      kdu_flush_stats::auto_advance                        */
/*****************************************************************************/

void
  kdu_flush_stats::auto_advance(int frame_sep)
{
  if (frame_sep < 1)
    { KDU_ERROR_DEV(e,0x12091611); e << 
      KDU_TXT("`kdu_flush_stats::auto_advance' requires a positive "
              "`frame_sep' argument.");
    }    
  if (state != NULL)
    state->auto_advance_frame_sep = frame_sep;
}


/* ========================================================================= */
/*                            kdu_quality_limiter                            */
/* ========================================================================= */

/*****************************************************************************/
/*                 kdu_quality_limiter::kdu_quality_limiter                  */
/*****************************************************************************/

kdu_quality_limiter::kdu_quality_limiter(float weighted_rmse,
                                         bool preserve_if_reversible)
{
  this->wrmse=weighted_rmse;
  inv_ppi_x = inv_ppi_y = -1.0f;
  this->preserve_reversible = preserve_if_reversible;
  num_comps = max_comps = 0;
  comp_sq_weights = NULL;
  comp_types = NULL;
}

/*****************************************************************************/
/*                 kdu_quality_limiter::~kdu_quality_limiter                 */
/*****************************************************************************/

kdu_quality_limiter::~kdu_quality_limiter()
{
  if (comp_sq_weights != NULL)
    delete[] comp_sq_weights;
  if (comp_types != NULL)
    delete[] comp_types;
}

/*****************************************************************************/
/*                      kdu_quality_limiter::duplicate                       */
/*****************************************************************************/

kdu_quality_limiter *kdu_quality_limiter::duplicate() const
{ 
  kdu_quality_limiter *obj =
  new kdu_quality_limiter(this->wrmse,this->preserve_reversible);
  obj->inv_ppi_x = this->inv_ppi_x;
  obj->inv_ppi_y = this->inv_ppi_y;
  obj->num_comps = this->num_comps;
  obj->max_comps = this->max_comps;
  if (max_comps > 0)
    { 
      obj->comp_sq_weights = new float[max_comps];
      obj->comp_types = new kdu_int32[max_comps];
      for (int c=0; c < num_comps; c++)
        { obj->comp_sq_weights[c] = this->comp_sq_weights[c];
          obj->comp_types[c] = this->comp_types[c]; }
    }
  return obj;
}

/*****************************************************************************/
/*                    kdu_quality_limiter::set_comp_info                     */
/*****************************************************************************/

void
  kdu_quality_limiter::set_comp_info(int c, float square_weight,
                                     kdu_int32 type_flags)
{
  if (square_weight <= 0.0f)
    { assert(0); square_weight=1.0f; }
  if (c >= max_comps)
    { // Need to enlarge the arrays
      max_comps += c+1;
      float *old_weights = this->comp_sq_weights;
      kdu_int32 *old_types = this->comp_types;
      comp_sq_weights = new float[max_comps];
      comp_types = new kdu_int32[max_comps];
      if (old_weights != NULL)
        { 
          memcpy(comp_sq_weights,old_weights,sizeof(float)*(size_t)num_comps);
          delete[] old_weights;
        }
      if (old_types != NULL)
        { 
          memcpy(comp_types,old_types,sizeof(kdu_int32)*(size_t)num_comps);
          delete[] old_types;
        }
    }
  for (; num_comps <= c; num_comps++)
    { comp_sq_weights[num_comps]=1.0f; comp_types[num_comps]=0; }
  comp_sq_weights[c] = square_weight;
  comp_types[c] = (type_flags < 0)?KDU_INT32_MIN:type_flags;
}

/*****************************************************************************/
/*               kdu_quality_limiter::get_square_visual_weight               */
/*****************************************************************************/

float
  kdu_quality_limiter::get_square_visual_weight(int orientation, int rel_depth,
                                                kdu_coords subsampling,
                                                bool is_chroma,
                                                bool reversible) const
{
  if (preserve_reversible && reversible)
    return -1.0f;
  if ((orientation == LL_BAND) || (inv_ppi_x <= 0.0f) || (inv_ppi_y <= 0.0f))
    return 1.0f; // No visual weights

  // The following weights are reasonable for 300dpi at 15cm:
  const int num_luma_levs=3, num_chroma_levs=6;
  const float lh_luma_weights[num_luma_levs]={0.2758f,0.8378f,1.0f};
  const float hh_luma_weights[num_luma_levs]={0.0901f,0.7018f,1.0f};
  const float lh_chroma_weights[num_chroma_levs] =
    {0.1835f,0.4130f,0.6464f,0.8254f,0.9424f,1.0f};
  const float hh_chroma_weights[num_chroma_levs] =
    {0.0773f,0.2598f,0.5040f,0.7220f,0.8769f,1.0f};
  
  int num_levs;
  const float *weights;
  if (!is_chroma)
    { 
      num_levs = num_luma_levs;
      weights = (orientation==HH_BAND)?hh_luma_weights:lh_luma_weights;
    }
  else
    { 
      num_levs = num_chroma_levs;
      weights = (orientation==HH_BAND)?hh_chroma_weights:lh_chroma_weights;
    }

  float f_x = 300.0f * inv_ppi_x * subsampling.x;
  float f_y = 300.0f * inv_ppi_y * subsampling.y;
  float f = (f_x > f_y)?f_x:f_y;
  int n=rel_depth-1; // If 1 <= f < 2 we interpolate table entries at n and n+1
  if (f < 1.0f)
    { // Adjust n downwards and f upwards, so as to make 1 <= f < 2
      while ((n > 0) && (f < 1.0f))
        { n--; f *= 2.0f; }
      if (f < 1.0f)
        { // Generate an immediate result by extrapolation
          float w0=weights[0];
          return w0*w0*f;
        }
    }
  else
    { // Adjust n upwards and f downwards, so as to make 1 <= f < 2
      while (f >= 2.0f)
        { n++; f *= 0.5f; }
    }
  if (n < 0)
    { // Should not happen
      f=1.0f; n = 0;
    }
  if ((n+1) >= num_levs)
    return 1.0f;
  else
    { 
      f -= 1.0f; // So that `f' lies in the range 0 to 1
      float w0=weights[n], w1=weights[n+1];
      return (w0*w0)*(1.0f-f) + (w1*w1)*f;
    }
}


#include "kdu_sample_processing.h"

/* ========================================================================= */
/*          Heap-dependent functions from "kdu_sample_processing.h"          */
/* ========================================================================= */

/*****************************************************************************/
/*                kdu_sample_allocator::~kdu_sample_allocator                */
/*****************************************************************************/

kdu_sample_allocator::~kdu_sample_allocator()
{
  if (buf_handle != NULL)
    delete[] buf_handle; 
}

/*****************************************************************************/
/* PRIVATE             kdu_sample_allocator::do_finalize                     */
/*****************************************************************************/

void kdu_sample_allocator::do_finalize(kdu_codestream codestream)
{
  if (pre_alloc_overflow)
    codestream.mem_failure("Core sample processing",
                           "internal error (numerical overflow)");
  assert(pre_creation_phase); pre_creation_phase = false;
  if ((bytes_reserved > buffer_size) ||
      (alignment_reserved > buffer_alignment))
    { // Otherwise, use the previously allocated buffer.
      buffer_size = bytes_reserved;
      buffer_alignment = alignment_reserved;
      if (buf_handle != NULL) delete[] buf_handle;
      buffer=buf_handle = new kdu_byte[buffer_size+3*buffer_alignment];
      buffer += (-_addr_to_kdu_int32(buffer)) & (buffer_alignment-1);
      buffer += buffer_alignment;
    }
  assert((bytes_reserved == 0) || (buffer != NULL));
}

/*****************************************************************************/
/*                          kdu_push_ifc::destroy                            */
/*****************************************************************************/

void kdu_push_ifc::destroy()
{
  if (state != NULL)
    delete state;
  state = NULL;
}

/*****************************************************************************/
/*                          kdu_pull_ifc::destroy                            */
/*****************************************************************************/

void kdu_pull_ifc::destroy()
{
  if (state != NULL)
    delete state;
  state = NULL;
}

/*****************************************************************************/
/*                       kdu_multi_analysis::destroy                         */
/*****************************************************************************/

void kdu_multi_analysis::destroy(kdu_thread_env *env)
{
  if (state != NULL)
    { 
      state->terminate_queues(env);
      delete state;
    }
  state = NULL;
}

/*****************************************************************************/
/*                       kdu_multi_synthesis::destroy                        */
/*****************************************************************************/

void kdu_multi_synthesis::destroy(kdu_thread_env *env)
{
  if (state != NULL)
    { 
      state->terminate_queues(env);
      delete state;
    }
  state = NULL;
}
