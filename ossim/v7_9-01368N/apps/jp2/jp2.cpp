/*****************************************************************************/
// File: jp2.cpp [scope = APPS/JP2]
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
in the compressed-io header file, "jp2.h".
******************************************************************************/

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include "kdu_elementary.h"
#include "kdu_compressed.h"
#include "kdu_messaging.h"
#include "kdu_utils.h"
#include "kdu_sample_processing.h"
#include "jp2.h"
#include "jp2_shared.h"
#include "jp2_local.h"
#include "kdu_file_io.h"
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
     kdu_error _name("E(jp2.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(jp2.cpp)",_id);
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

// #define KDU_ALLOW_SILENT_RGB_MONO_CORRECTION
  /* Uncomment the above macro if you want to let certain types of badly
     constructed JP2 files with invalid CDEF boxes through without generating
     warnings. */


/* ========================================================================= */
/*                          ICC Profile Signatures                           */
/* ========================================================================= */

static const kdu_uint32 icc_file_signature = jp2_4cc_to_int("acsp");

static const kdu_uint32 icc_input_device   = jp2_4cc_to_int("scnr");
static const kdu_uint32 icc_display_device = jp2_4cc_to_int("mntr");
static const kdu_uint32 icc_output_device  = jp2_4cc_to_int("prtr");

static const kdu_uint32 icc_xyz_data      = jp2_4cc_to_int("XYZ ");
static const kdu_uint32 icc_lab_data      = jp2_4cc_to_int("Lab ");
static const kdu_uint32 icc_luv_data      = jp2_4cc_to_int("Luv ");
static const kdu_uint32 icc_ycbcr_data    = jp2_4cc_to_int("YCbr");
static const kdu_uint32 icc_yxy_data      = jp2_4cc_to_int("Yxy ");
static const kdu_uint32 icc_rgb_data      = jp2_4cc_to_int("RGB ");
static const kdu_uint32 icc_gray_data     = jp2_4cc_to_int("GRAY");
static const kdu_uint32 icc_hsv_data      = jp2_4cc_to_int("HSV ");
static const kdu_uint32 icc_hls_data      = jp2_4cc_to_int("HLS ");
static const kdu_uint32 icc_cmyk_data     = jp2_4cc_to_int("CMYK");
static const kdu_uint32 icc_cmy_data      = jp2_4cc_to_int("CMY ");
static const kdu_uint32 icc_2clr_data     = jp2_4cc_to_int("2CLR");
static const kdu_uint32 icc_3clr_data     = jp2_4cc_to_int("3CLR");
static const kdu_uint32 icc_4clr_data     = jp2_4cc_to_int("4CLR");
static const kdu_uint32 icc_5clr_data     = jp2_4cc_to_int("5CLR");
static const kdu_uint32 icc_6clr_data     = jp2_4cc_to_int("6CLR");
static const kdu_uint32 icc_7clr_data     = jp2_4cc_to_int("7CLR");
static const kdu_uint32 icc_8clr_data     = jp2_4cc_to_int("8CLR");
static const kdu_uint32 icc_9clr_data     = jp2_4cc_to_int("9CLR");
static const kdu_uint32 icc_10clr_data    = jp2_4cc_to_int("ACLR");
static const kdu_uint32 icc_11clr_data    = jp2_4cc_to_int("BCLR");
static const kdu_uint32 icc_12clr_data    = jp2_4cc_to_int("CCLR");
static const kdu_uint32 icc_13clr_data    = jp2_4cc_to_int("DCLR");
static const kdu_uint32 icc_14clr_data    = jp2_4cc_to_int("ECLR");
static const kdu_uint32 icc_15clr_data    = jp2_4cc_to_int("FCLR");

static const kdu_uint32 icc_pcs_xyz        = jp2_4cc_to_int("XYZ ");
static const kdu_uint32 icc_pcs_lab        = jp2_4cc_to_int("Lab ");

static const kdu_uint32 icc_gray_trc       = jp2_4cc_to_int("kTRC");
static const kdu_uint32 icc_red_trc        = jp2_4cc_to_int("rTRC");
static const kdu_uint32 icc_green_trc      = jp2_4cc_to_int("gTRC");
static const kdu_uint32 icc_blue_trc       = jp2_4cc_to_int("bTRC");
static const kdu_uint32 icc_red_colorant   = jp2_4cc_to_int("rXYZ");
static const kdu_uint32 icc_green_colorant = jp2_4cc_to_int("gXYZ");
static const kdu_uint32 icc_blue_colorant  = jp2_4cc_to_int("bXYZ");
static const kdu_uint32 icc_media_white    = jp2_4cc_to_int("wtpt");
static const kdu_uint32 icc_copyright      = jp2_4cc_to_int("cprt");
static const kdu_uint32 icc_profile_desc   = jp2_4cc_to_int("desc");

static const kdu_uint32 icc_curve_type     = jp2_4cc_to_int("curv");
static const kdu_uint32 icc_xyz_type       = jp2_4cc_to_int("XYZ ");
static const kdu_uint32 icc_text_type      = jp2_4cc_to_int("text");

/* ========================================================================= */
/*                             Colour Space Data                             */
/* ========================================================================= */

static const double icc_xyzD50_to_xyzD65[9] =
  { 0.9845, -0.0547,  0.0678,
   -0.0060,  1.0048,  0.0012,
    0.0000,  0.0000,  1.3200 };
  /* Describes a 3x3 matrix for converting (by left multiplication of a
     column vector) XYZ coordinates under the D50 illuminant to XYZ
     coordinates with the same (or similar) appearance under the D65
     illuminant.  The appearance transform is normalized by insisting that
     both sets of XYZ coordinates should be scaled so that the Y coordinate
     is equal to 1.  This is the XYZ normalization convention adopted by the
     ICC specification, and used throughout the present document. */

static const double xy_D65_white[2] = {0.3127, 0.3290};
static const double xy_D50_white[2] = {0.3457, 0.3585};

static const double xy_709_red[2] =   {0.640 , 0.330 };
static const double xy_709_green[2] = {0.300 , 0.600 };
static const double xy_709_blue[2] =  {0.150 , 0.060 };

static const double xy_601_red[2] =   {0.630 , 0.340 };
static const double xy_601_green[2] = {0.310 , 0.595 };
static const double xy_601_blue[2] =  {0.155 , 0.070 };

static const double xy_240m_red[2] =   {0.670 , 0.330 };
static const double xy_240m_green[2] = {0.210 , 0.710 };
static const double xy_240m_blue[2] =  {0.150 , 0.060 };


/* ========================================================================= */
/*                             External Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                     jp2_4cc_to_string                              */
/*****************************************************************************/
namespace kdu_supp {

const char *
  jp2_4cc_to_string(kdu_uint32 box_type, char buf[])
{
  for (int c=3; c >= 0; c--, box_type>>=8)
    { 
      char ch = (char)(box_type & 0xFF);
      if (ch == ' ')
        ch = '_';
      else if ((ch < 0x20) || (ch & 0x80))
        ch = '.';
      buf[c] = ch;
    }
  buf[4] = '\0';
  return buf;
}
  
kdu_uint32 known_superbox_types[] =
  { jp2_header_4cc, jp2_codestream_header_4cc, jp2_fragment_table_4cc,
    jp2_desired_reproductions_4cc, jp2_compositing_layer_hdr_4cc,
    jp2_association_4cc, jp2_group_4cc, jp2_uuid_info_4cc, jp2_resolution_4cc,
    jp2_colour_group_4cc, jp2_composition_4cc, jp2_layer_extensions_4cc,
    jp2_multi_codestream_4cc,
    mj2_movie_4cc,mj2_track_4cc,mj2_media_4cc,mj2_media_information_4cc,
    mj2_data_information_4cc,mj2_sample_table_4cc,jpb_elementary_stream_4cc,
    0};

} // namespace kdu_supp

/*****************************************************************************/
/* EXTERN                      jp2_is_superbox                               */
/*****************************************************************************/
namespace kdu_supp {
  
bool
  jp2_is_superbox(kdu_uint32 box_type)
{
  kdu_uint32 *scan;
  for (scan=known_superbox_types; *scan != 0; scan++)
    if (*scan == box_type)
      return true;
  return false;
}
  
} // namespace kdu_supp

/*****************************************************************************/
/* EXTERN                   jp2_add_box_descriptions                         */
/*****************************************************************************/

static bool jp_textualizer_literal(jp2_input_box *, kdu_message &, bool, int);
static bool jp_textualizer_ftyp(jp2_input_box *, kdu_message &, bool, int);
static bool jp_textualizer_ihdr(jp2_input_box *, kdu_message &, bool, int);
static bool jp_textualizer_bpcc(jp2_input_box *, kdu_message &, bool, int);
static bool jp_textualizer_colr(jp2_input_box *, kdu_message &, bool, int);
static bool jp_textualizer_resn(jp2_input_box *, kdu_message &, bool, int);
static bool jx_textualizer_pxfm(jp2_input_box *, kdu_message &, bool, int);

namespace kdu_supp {
  
void
  jp2_add_box_descriptions(jp2_box_textualizer &textualizer)
{
  textualizer.add_box_type(jp2_file_type_4cc,NULL,jp_textualizer_ftyp);
  textualizer.add_box_type(jp2_image_header_4cc,NULL,jp_textualizer_ihdr);
  textualizer.add_box_type(jp2_bits_per_component_4cc,NULL,
                           jp_textualizer_bpcc);
  textualizer.add_box_type(jp2_colour_4cc,NULL,jp_textualizer_colr);
  textualizer.add_box_type(jp2_capture_resolution_4cc,NULL,
                           jp_textualizer_resn);
  textualizer.add_box_type(jp2_display_resolution_4cc,NULL,
                           jp_textualizer_resn);
  textualizer.add_box_type(jp2_xml_4cc,NULL,jp_textualizer_literal);
  textualizer.add_box_type(jp2_iprights_4cc,NULL,jp_textualizer_literal);
  textualizer.add_box_type(jp2_label_4cc,NULL,jp_textualizer_literal);
  textualizer.add_box_type(jp2_pixel_format_4cc,NULL,jx_textualizer_pxfm);
}

} // namespace kdu_supp


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* INLINE                           store_big                                */
/*****************************************************************************/

static inline void
  store_big(kdu_uint32 val, kdu_byte * &bp)
{
  bp += 4;
  bp[-1] = (kdu_byte) val; val >>= 8;
  bp[-2] = (kdu_byte) val; val >>= 8;
  bp[-3] = (kdu_byte) val; val >>= 8;
  bp[-4] = (kdu_byte) val;
}

static inline void
  store_big(kdu_uint16 val, kdu_byte * &bp)
{
  bp += 2;
  bp[-1] = (kdu_byte) val; val >>= 8;
  bp[-2] = (kdu_byte) val;
}


/*****************************************************************************/
/* INLINE                           read_big                                 */
/*****************************************************************************/

static inline kdu_long
  read_big(kdu_byte *bp, int num_bytes)
{
  kdu_long val = 0;
  for (; num_bytes > 0; num_bytes--, bp++)
    val = (val<<8) + *bp;
  return val;
}

/*****************************************************************************/
/* STATIC                 get_rational_pels_per_metre                        */
/*****************************************************************************/

static int
  get_rational_pels_per_metre(double ppm, int &num, int &den, int &exponent,
                              int preferred_scheme=-1)
  /* This function is used when saving JP2 resolution boxes.  It attempts to
     find a good way of expressing a pixels-per-metre value (ppm) as a
     rational with 16-bit unsigned numerator and denominator, with a
     signed 8-bit `exponent'.  Since the problem is ill-posed, the
     function implements two schemes to find a good initial denominator.
     Scheme 0 assumes that original values are based on the cm; scheme 1
     assumes that original values are based on the inch.  Scheme -1
     means try both schemes and find the best one.  The function returns
     0 or 1, indicating the scheme that it used. */
{
  int n, num_val[2], den_val[2], exp_val[2];
  double error_val[2];

  // Start with the cm based scheme
  double res = ppm * 0.01; // pixels per cm
  exp_val[0] = 2;
  den_val[0] = 1;
  while ((res > 65535.0) && (exp_val[0] < 127))
    { res *= 0.1F; exp_val[0]++; }
  while ((res < 6000.0) && (exp_val[0] > -128))
    { res *= 10.0; exp_val[0]--; }
  num_val[0] = (int) floor(0.5+res);
  res = ((double) num_val[0]) / ((double) den_val[0]);
  for (n=exp_val[0]; n > 0; n--) res *= 10.0;
  for (; n < 0; n++) res *= 0.1;
  error_val[0] = fabs(res-ppm);

  // Now for the inch based scheme
  res = ppm * 25.4 / 1000.0; // pixels per inch
  exp_val[1] = 4;
  den_val[1] = 254;
  while ((res > 65535.0) && (exp_val[1] < 127))
    { res *= 0.1F; exp_val[1]++; }
  while ((res < 6000.0) && (exp_val[1] > -128))
    { res *= 10.0F; exp_val[1]--; }
  num_val[1] = (int) floor(0.5+res);
  res = ((double) num_val[1]) / ((double) den_val[1]);
  for (n=exp_val[1]; n > 0; n--) res *= 10.0;
  for (; n < 0; n++) res *= 0.1;
  error_val[1] = fabs(res-ppm);
  
  int scheme = preferred_scheme;
  if (scheme < 0)
    scheme = (error_val[0] < error_val[1])?0:1;
  if (scheme > 1)
    scheme = 1;
  num = num_val[scheme];
  den = den_val[scheme];
  exponent = exp_val[scheme];
  return scheme;
}


/*****************************************************************************/
/* STATIC                     find_monitor_matrix                            */
/*****************************************************************************/

static void
  find_monitor_matrix(const double xy_red[], const double xy_green[],
                      const double xy_blue[], const double xy_white[],
                      double matrix[])
  /* This function forms a key element in the construction of colour
     transforms.  It finds the 3x3 matrix (stored in raster scan order)
     such that (X Y Z)' = matrix * (R G B)' where the red, green and blue
     chromaticities are given by (xy_red[0], xy_red[1]),
     (xy_green[0], xy_green[1]) and (xy_blue[0], xy_blue[1]), and the
     whitepoint chromaticity is given by (xy_white[0], xy_white[1]).  The
     matrix coefficients are normalized such that Y = 1 when the red, green
     and blue channels are all driven at their maximum level (i.e. at the
     value 1). */
{
  int n;
  double xy_RB[2], xy_GB[2], xy_WB[2];
  for (n=0; n < 2; n++)
    {
      xy_RB[n] = xy_red[n] - xy_blue[n];
      xy_GB[n] = xy_green[n] - xy_blue[n];
      xy_WB[n] = xy_white[n] - xy_blue[n];
    }
  double det = xy_RB[0] * xy_GB[1] - xy_GB[0] * xy_RB[1];
  double Rt = (xy_GB[1] * xy_WB[0] - xy_GB[0] * xy_WB[1]) / det;
  double Gt = (xy_RB[0] * xy_WB[1] - xy_RB[1] * xy_WB[0]) / det;
  double xy_R[3] = { xy_red[0],   xy_red[1],   1-xy_red[0]-xy_red[1] };
  double xy_G[3] = { xy_green[0], xy_green[1], 1-xy_green[0]-xy_green[1] };
  double xy_B[3] = { xy_blue[0],  xy_blue[1],  1-xy_blue[0]-xy_blue[1] };
  for (n=0; n < 3; n++)
    {
      matrix[3*n+0] = xy_R[n]*Rt;
      matrix[3*n+1] = xy_G[n]*Gt;
      matrix[3*n+2] = xy_B[n]*(1-Rt-Gt);
    }

  // The above code produces a matrix, which is normalized for X+Y+Z=1 at
  // R=G=B=1.  We now scale this so that Y=1 at the maximum whitepoint instead.
  double scale = 1.0/xy_white[1];
  for (n=0; n < 9; n++)
    matrix[n] *= scale;
}

/*****************************************************************************/
/* STATIC                     find_matrix_inverse                            */
/*****************************************************************************/

static void
  find_matrix_inverse(double out[], const double in_orig[], const int dim,
                      double scratch[])
  /* The `scratch' array should have `dim'x`dim' elements. */
{
  int m, n, k;
  double scale, tmp;
  double *in = scratch;
  for (n=0; n < (dim*dim); n++)
    in[n] = in_orig[n];

  // Set `out' equal to the identity matrix to start.
  for (k=m=0; m < dim; m++)
    for (n=0; n < dim; n++, k++)
      out[k] = (m==n)?1.0:0.0;

  // The following inversion strategy uses only partial pivoting

  // Start with row ops to reduce `in' to upper triangular form, with
  // 1's on the diagonal.
  for (n=0; n < dim; n++)
    { // Walk through the columns one by one
      // Pivoting: find the best row
      tmp = in[n*dim+n];  tmp = (tmp<0.0)?(-tmp):tmp;
      for (m=k=n; k < dim; k++)
        if (in[k*dim+n] > tmp)
          { m = k; tmp = in[k*dim+n]; }
        else if (in[k*dim+n] < -tmp)
          { m = k; tmp = -in[k*dim+n]; }

      // Swap the best row into row number n and scale so location (n,n)=1
      scale = 1.0 / in[m*dim+n];
      for (k=0; k < dim; k++)
        {
          tmp = in[n*dim+k];
          in[n*dim+k] = in[m*dim+k]*scale;
          in[m*dim+k] = tmp*scale;
        }
      for (k=0; k < dim; k++)
        {
          tmp = out[n*dim+k];
          out[n*dim+k] = out[m*dim+k]*scale;
          out[m*dim+k] = tmp*scale;
        }

      // Zero out all subsequent rows in column n
      for (m=n+1; m < dim; m++)
        {
          scale = in[m*dim+n];
          for (k=0; k < dim; k++)
            in[m*dim+k] -= scale*in[n*dim+k];
          for (k=0; k < dim; k++)
            out[m*dim+k] -= scale*out[n*dim+k];
        }
    }

  // Now walk through the columns in reverse order to reduce `in' to I
  for (n=dim-1; n > 0; n--)
    {
      // Zero out all previous rows in column n
      for (m=n-1; m >= 0; m--)
        {
          scale = in[m*dim+n];
          for (k=0; k < dim; k++)
            in[m*dim+k] -= scale*in[n*dim+k];
          for (k=0; k < dim; k++)
            out[m*dim+k] -= scale*out[n*dim+k];
        }
    }
}

/*****************************************************************************/
/* STATIC                     find_matrix_product                            */
/*****************************************************************************/

static void
  find_matrix_product(double out[], const double left[], const double right[],
                      const int dim)
  /* Finds `out' = `left' * `right'. */
{
  int m, n, k;

  for (m=0; m < dim; m++)
    for (n=0; n < dim; n++)
      {
        double sum = 0.0;
        for (k=0; k < dim; k++)
          sum += left[dim*m+k] * right[dim*k+n];
        out[m*dim+n] = sum;
      }
}


/* ========================================================================= */
/*                       Internal Textualizer Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    jp_textualizer_literal                          */
/*****************************************************************************/

static bool
  jp_textualizer_literal(jp2_input_box *box, kdu_message &msg,
                         bool xml_embedded, int max_len)
{
  char buf[256];
  int n, bytes_read, xfer_bytes;
  
  if (xml_embedded)
    msg << "<![CDATA[\n";
  if (max_len < 0)
    max_len = INT_MAX;
  for (bytes_read=0; max_len > 0; max_len-=xfer_bytes, bytes_read+=xfer_bytes)
    { 
      xfer_bytes = (max_len > 255)?255:max_len;
      if ((xfer_bytes = box->read((kdu_byte *) buf,xfer_bytes)) == 0)
        break;
      buf[xfer_bytes] = '\0';
      for (n=0; n < xfer_bytes; n++)
        if (buf[n] == '\r')
          buf[n] = '\n';
        else if (buf[n] == '\0')
          buf[n] = ' ';
      msg << buf;
    }
  if (xml_embedded)
    msg << "\n]]>\n";
  return ((bytes_read > 0) || xml_embedded);
}

/*****************************************************************************/
/* STATIC                     jp_textualizer_ftyp                            */
/*****************************************************************************/

static bool
  jp_textualizer_ftyp(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_uint32 brand=0, minor_version=0;
  if (!(box->read(brand) && box->read(minor_version)))
    return false;
  max_len -= 8;
  char fourcc_buf[5];
  char hex_brand[20];
  sprintf(hex_brand,"0x%08X",brand);
  msg << "<brand> \"" << jp2_4cc_to_string(brand,fourcc_buf)
      << "\" " << hex_brand << " </brand>\n";
  
  msg << "<minor_version> " << minor_version << " </minor_version>\n";
  for (; (max_len >= 4) && box->read(brand); max_len -= 4)
    { 
      sprintf(hex_brand,"0x%08X",brand);
      msg << "<compatible_brand> \""
          << jp2_4cc_to_string(brand,fourcc_buf)
          << "\" " << hex_brand << " </compatible_brand>\n";
    }
  return true;
}

/*****************************************************************************/
/* STATIC                     jp_textualizer_ihdr                            */
/*****************************************************************************/

static bool
  jp_textualizer_ihdr(jp2_input_box *ihdr, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_uint32 height=0, width=0;
  kdu_uint16 nc=0;
  kdu_byte bpc=0, c_type=0, unk=0, ipr=0;
  if (!(ihdr->read(height) && ihdr->read(width) && ihdr->read(nc) &&
        ihdr->read(bpc) && ihdr->read(c_type) &&
        ihdr->read(unk) && ihdr->read(ipr)))
    return false;
  msg << "<height> " << height << " </height>\n";
  msg << "<width> " << width << " </width>\n";
  msg << "<components> " << nc << " </components>\n";
  msg << "<bit_depth> ";
  if (bpc == 255)
    msg << "variable";
  else if (bpc & 0x80)
    msg << "signed " << (((int)(bpc & 0x7F)+1));
  else
    msg << "unsigned " << (((int) bpc)+1);
  msg << " </bit_depth>\n";
  const char *string;
  switch ((int) c_type) {
    case JP2_COMPRESSION_TYPE_NONE: string="UNCOMPRESSED"; break;
    case JP2_COMPRESSION_TYPE_MH: string="T.4-MODIFIED-HUFFMAN"; break;
    case JP2_COMPRESSION_TYPE_MR: string="T.4-MODIFIED-READ"; break;
    case JP2_COMPRESSION_TYPE_MMR: string="T.6-MODIFIED-MODIFIED-READ"; break;
    case JP2_COMPRESSION_TYPE_JBIG: string="JBIG"; break;
    case JP2_COMPRESSION_TYPE_JPEG: string="JPEG"; break;
    case JP2_COMPRESSION_TYPE_JLS: string="JPEG-LS"; break;
    case JP2_COMPRESSION_TYPE_JPEG2000: string="JPEG2000"; break;
    case JP2_COMPRESSION_TYPE_JBIG2: string="JBIG2"; break;
    default: string = "unrecognized";
  }
  msg << "<compression_type> \"" << string << "\" </compression_type>\n";
  if (unk)
    msg << "<colour_space_unknown />\n";
  else
    msg << "<colour_space_known />\n";
  if (ipr)
    msg << "<ipr_box_available />\n";
  
  return true;
  
}

/*****************************************************************************/
/* STATIC                     jp_textualizer_bpcc                            */
/*****************************************************************************/

static bool
  jp_textualizer_bpcc(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_byte bpc;
  int c;
  for (c=0; (max_len > 0) && box->read(bpc); max_len--, c++)
    { 
      msg << "<bit_depth component=\"" << c << "\"> ";
      if (bpc == 255)
        msg << "\"variable\"";
      else if (bpc & 0x80)
        msg << "\"signed\" " << (((int)(bpc & 0x7F)+1));
      else
        msg << "\"unsigned\" " << (((int) bpc)+1);
      msg << "</bit_depth>\n";
    }
  return (c > 0);
}

/*****************************************************************************/
/* STATIC                     jp_textualizer_resn                            */
/*****************************************************************************/

static bool
  jp_textualizer_resn(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_uint16 v_num=0, v_den=0, h_num=0, h_den=0;
  kdu_byte v_exp=0, h_exp=0;
  
  if (!(box->read(v_num) && box->read(v_den) &&
        box->read(h_num) && box->read(h_den) &&
        box->read(v_exp) && box->read(h_exp)))
    return false;
  msg << "<vertical_grid_points_per_metre> ("
      << ((int) v_num) << " / " << ((int) v_den)
      << ") x 10E" << ((int) v_exp)
      << " </vertical_grid_points_per_metre>\n";
  msg << "<horizontal_grid_points_per_metre> ("
      << ((int) h_num) << " / " << ((int) h_den)
      << ") x 10E" << ((int) h_exp)
      << " </horizontal_grid_points_per_metre>\n";  
  return true;
}

/*****************************************************************************/
/* STATIC                     jp_textualizer_colr                            */
/*****************************************************************************/

static bool
  jp_textualizer_colr(jp2_input_box *colr, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  kdu_byte meth=0, prec_val=0, approx=0;
  if (!(colr->read(meth) && colr->read(prec_val) && colr->read(approx)))
    return false;
  int precedence = (int) prec_val;
  if (precedence & 0x80)
    precedence -= 256; // Value was communicated as 2's complement signed byte
  if (meth == 1)
    { 
      kdu_uint32 enum_cs;
      if (!colr->read(enum_cs))
        return false;
      const char *space;
      int ep_count = 0;
      switch (enum_cs) {
        case 0:  space="Bilevel1"; break;
        case 1:  space="YCbCr1"; break;
        case 3:  space="YCbCr2"; break;
        case 4:  space="YCbCr3"; break;
        case 9:  space="PhotoYCC"; break;
        case 11: space="CMY"; break;
        case 12: space="CMYK"; break;
        case 13: space="YCCK"; break;
        case 14: space="CIELab"; ep_count=7; break;
        case 15: space="Bilevel2"; break;
        case 16: space="sRGB"; break;
        case 17: space="sLUM"; break;
        case 18: space="sYCC"; break;
        case 19: space="CIEJab"; ep_count=6; break;
        case 20: space="esRGB"; break;
        case 21: space="ROMMRGB"; break;
        case 22: space="YPbPr60"; break;
        case 23: space="YPbPr50"; break;
        case 24: space="esYCC"; break;
        default: space="unrecognized"; break;
      }
      msg << "<enum_space> \"" << space << "\" " << enum_cs
          << " </enum_space>\n";
      int e;
      kdu_uint32 ep_data[7];
      for (e=0; e < ep_count; e++)
        if (!colr->read(ep_data[e]))
          break;
      if (e >= 6)
        { 
          msg << "<LAB_JAB_ranges> "
              << ep_data[0] << "," << ep_data[2] << "," << ep_data[4]
              << " </LAB_JAB_ranges>\n";
          msg << "<LAB_JAB_offsets> "
              << ep_data[1] << "," << ep_data[3] << "," << ep_data[5]
              << " </LAB_JAB_offsets>\n";
        }
      if (e == 7)
        { 
          msg << "<LAB_illuminant> ";
          kdu_uint32 illuminant = ep_data[6];
          if ((illuminant & JP2_CIE_DAY) == JP2_CIE_DAY)
            msg << "\"CIE_daylight\" " << ((kdu_uint16) illuminant) << "K";
          else
            { 
              const char *ill;
              switch (illuminant) {
                case JP2_CIE_D50: ill="CIE_D50"; break;
                case JP2_CIE_D65: ill="CIE_D65"; break;
                case JP2_CIE_D75: ill="CIE_D75"; break;
                case JP2_CIE_SA: ill="CIE_SA"; break;
                case JP2_CIE_SC: ill="CIE_SC"; break;
                case JP2_CIE_F2: ill="CIE_F2"; break;
                case JP2_CIE_F7: ill="CIE_F7"; break;
                case JP2_CIE_F11: ill="CIE_F11"; break;
                default: ill="unrecognized";
              }
              msg << "\"" << ill << "\"";
            }
          msg << " " << illuminant;
          msg << " </LAB_illuminant>\n";
        }
    }
  else if (meth == 2)
    msg << "<restricted_ICC_space />\n";
  else if (meth == 3)
    msg << "<any_ICC_space />\n";
  else if (meth == 4)
    { 
      kdu_byte vendor_uuid[16];
      if (colr->read(vendor_uuid,16) != 16)
        msg << "<vendor_space />\n";
      else
        { 
          msg << "<vendor_space uuid=\"";
          for (int c=0; c < 16; c++)
            { 
              char buf[8];
              sprintf(buf,"%02X",(int)vendor_uuid[c]);
              msg << buf;
            }
          msg << "\" />\n";
        }
    }
  else
    msg << "<unrecognized_method />\n";
  
  msg << "<approx> " << ((int) approx) << " </approx>\n";
  msg << "<precedence> " << precedence << " </precedence>\n";
  
  return false;
}

/*****************************************************************************/
/* STATIC                     jx_textualizer_pxfm                            */
/*****************************************************************************/

static bool
jx_textualizer_pxfm(jp2_input_box *box, kdu_message &msg,
                    bool xml_embedded, int max_len)
{
  kdu_uint16 n, num_channels=0;
  if (!box->read(num_channels))
    return false;
  for (n=0; n < num_channels; n++)
    { 
      kdu_uint16 idx=0, fmt=0;
      if (!(box->read(idx) && box->read(fmt)))
        return false;
      msg << "<format channel=\"" << n << "\" input=\"" << idx << "> ";
      if (fmt == 0)
        msg << "\"default\"";
      else if (fmt == 0x1000)
        msg << "\"mantissa-part\"";
      else if (fmt == 0x2000)
        msg << "\"exponent-part\"";
      else if ((fmt & 0xF000) == 0x3000)
        msg << "\"fixpoint-frac-bits\" " << (int)(fmt & 0x0FFF);
      else if ((fmt & 0xF000) == 0x4000)
        msg << "\"float-mantisa-bits\" " << (int)(fmt & 0x0FFF);
      msg << "</format>\n";
    }
  return true;
}


/* ========================================================================= */
/*                            jp2_box_textualizer                            */
/* ========================================================================= */

/*****************************************************************************/
/*                jp2_box_textualizer::jp2_box_textualizer                   */
/*****************************************************************************/

jp2_box_textualizer::jp2_box_textualizer()
{
  type_list = NULL;
  add_box_type(jp2_signature_4cc,"JP2-signature");
  add_box_type(jp2_file_type_4cc,"file-type");
  add_box_type(jp2_header_4cc,"JP2-header");
  add_box_type(jp2_image_header_4cc,"image-header");
  add_box_type(jp2_bits_per_component_4cc,"bits-per-component");
  add_box_type(jp2_colour_4cc,"colour");
  add_box_type(jp2_palette_4cc,"palette");
  add_box_type(jp2_component_mapping_4cc,"component-mapping");
  add_box_type(jp2_channel_definition_4cc,"channel-definition");
  add_box_type(jp2_resolution_4cc,"resolution");
  add_box_type(jp2_capture_resolution_4cc,"capture-resolution");
  add_box_type(jp2_display_resolution_4cc,"display-resolution");
  add_box_type(jp2_codestream_4cc,"contiguous-codestream");
  
  add_box_type(jp2_dtbl_4cc,"data-reference");
  add_box_type(jp2_data_entry_url_4cc,"data-reference-URL");
  add_box_type(jp2_fragment_table_4cc,"fragment-table");
  add_box_type(jp2_fragment_list_4cc,"fragment-list");
  add_box_type(jp2_cross_reference_4cc,"cross-reference");
  add_box_type(jp2_reader_requirements_4cc,"reader-requirements");
  add_box_type(jp2_codestream_header_4cc,"codestream-header");
  add_box_type(jp2_desired_reproductions_4cc,"desired-reproductions");
  add_box_type(jp2_compositing_layer_hdr_4cc,"compositing-layer-header");
  add_box_type(jp2_registration_4cc,"codestream-registration");
  add_box_type(jp2_opacity_4cc,"opacity");
  add_box_type(jp2_colour_group_4cc,"colour-group");
  add_box_type(jp2_composition_4cc,"composition");
  add_box_type(jp2_comp_options_4cc,"composition-options");
  add_box_type(jp2_comp_instruction_set_4cc,"composition-instruction-set");
  add_box_type(jp2_pixel_format_4cc,"pixel-format");
  
  add_box_type(jp2_iprights_4cc,"IP-rights");
  add_box_type(jp2_uuid_4cc,"UUID");
  add_box_type(jp2_uuid_info_4cc,"UUID-info");
  add_box_type(jp2_label_4cc,"label");
  add_box_type(jp2_xml_4cc,"xml");
  add_box_type(jp2_number_list_4cc,"number-list");
  add_box_type(jp2_association_4cc,"association");
  add_box_type(jp2_group_4cc,"group");
  
  add_box_type(mj2_movie_4cc,"movie");
  add_box_type(mj2_movie_header_4cc,"movie-header");
  add_box_type(mj2_track_4cc,"track");
  add_box_type(mj2_track_header_4cc,"track-header");
  add_box_type(mj2_media_4cc,"media");
  add_box_type(mj2_media_header_4cc,"media-header");
  add_box_type(mj2_media_header_typo_4cc,"media-header");
  add_box_type(mj2_media_handler_4cc,"media-handler");
  add_box_type(mj2_media_information_4cc,"media-information");
  add_box_type(mj2_video_media_header_4cc,"video-media-header");
  add_box_type(mj2_video_handler_4cc,"video-handler");
  add_box_type(mj2_data_information_4cc,"data-information");
  add_box_type(mj2_data_reference_4cc,"data-reference");
  add_box_type(mj2_url_4cc,"URL");
  add_box_type(mj2_sample_table_4cc,"sample-table");
  add_box_type(mj2_sample_description_4cc,"sample-description");
  add_box_type(mj2_visual_sample_entry_4cc,"visual-sample-entry");
  add_box_type(mj2_field_coding_4cc,"field-coding");
  
  add_box_type(mj2_sample_size_4cc,"sample-size");
  add_box_type(mj2_sample_to_chunk_4cc,"sample-to-chunk");
  add_box_type(mj2_chunk_offset_4cc,"chunk-offsets");
  add_box_type(mj2_chunk_offset64_4cc,"chunk-offsets-64");
  add_box_type(mj2_time_to_sample_4cc,"time-to-sample");
  
  add_box_type(jpb_elementary_stream_4cc,"elementary-stream-header");
  add_box_type(jpb_frame_rate_4cc,"frame-rate");
  add_box_type(jpb_max_bitrate_4cc,"max-bit-rate");
  add_box_type(jpb_field_coding_4cc,"field-coding"); // = `mj2_field_coding'
  add_box_type(jpb_time_code_4cc,"time-code");
  add_box_type(jpb_colour_4cc,"broadcast-colour");
  
  add_box_type(jp2_mdat_4cc,"media-data");
  add_box_type(jp2_free_4cc,"free-space");
  add_box_type(mj2_skip_4cc,"skip");
}

/*****************************************************************************/
/*                jp2_box_textualizer::~jp2_box_textualizer                  */
/*****************************************************************************/

jp2_box_textualizer::~jp2_box_textualizer()
{
  jp_box_type *tmp;
  while ((tmp = type_list) != NULL)
    { type_list = tmp->next; delete tmp; }
}

/*****************************************************************************/
/*                    jp2_box_textualizer::add_box_type                      */
/*****************************************************************************/

bool
  jp2_box_textualizer::add_box_type(kdu_uint32 box_type, const char *box_name,
                                    jp2_box_textualizer_func textualizer_func)
{ 
  if (box_type == 0)
    return false;
  jp_box_type *scan=type_list;
  for (; scan != NULL; scan=scan->next)
    if (scan->box_type == box_type)
      break;
  if (scan != NULL)
    { 
      if ((box_name != NULL) && (box_name[0] != '\0'))
        { 
          strncpy(scan->box_name,box_name,80);
          scan->box_name[80] = '\0';
        }
      if (textualizer_func != NULL)
        scan->textualizer = textualizer_func;
    }
  else if ((box_name != NULL) && (box_name[0] != '\0'))
    { 
      scan = new jp_box_type;
      scan->box_type = box_type;
      strncpy(scan->box_name,box_name,80);
      scan->box_name[80] = '\0';      
      scan->textualizer = textualizer_func;
      scan->next = type_list;
      type_list = scan;
    }
  return (scan != NULL);
}

/*****************************************************************************/
/*                    jp2_box_textualizer::get_box_name                      */
/*****************************************************************************/

const char *
  jp2_box_textualizer::get_box_name(kdu_uint32 box_type) const
{ 
  jp_box_type *scan=type_list;
  for (; scan != NULL; scan=scan->next)
    if (box_type == scan->box_type)
      return scan->box_name;
  return NULL;
}

/*****************************************************************************/
/*              jp2_box_textualizer::check_textualizer_function              */
/*****************************************************************************/

bool
  jp2_box_textualizer::check_textualizer_function(kdu_uint32 box_type) const
{ 
  jp_box_type *scan=type_list;
  for (; scan != NULL; scan=scan->next)
    if (box_type == scan->box_type)
      return (scan->textualizer != NULL);
  return false;
}

/*****************************************************************************/
/*                   jp2_box_textualizer::textualize_box                     */
/*****************************************************************************/

bool
  jp2_box_textualizer::textualize_box(jp2_input_box *box,
                                      kdu_message &tgt,
                                      bool xml_embedded,int max_len)
{ 
  kdu_uint32 box_type = box->get_box_type();
  jp_box_type *scan=type_list;
  for (; scan != NULL; scan=scan->next)
    if (box_type == scan->box_type)
      { 
        if (scan->textualizer == NULL)
          return false;
        else
          return scan->textualizer(box,tgt,xml_embedded,max_len);
      }
  return false;
}


/* ========================================================================= */
/*                               jp2_family_src                              */
/* ========================================================================= */

/*****************************************************************************/
/*                         jp2_family_src::open (file)                       */
/*****************************************************************************/

void
  jp2_family_src::open(const char *filename, bool allow_seeks)
{
  if ((fp != NULL) || (indirect != NULL) || (cache != NULL))
    { KDU_ERROR_DEV(e,0); e <<
        KDU_TXT("Attempting to open a `jp2_family_src' object which "
        "is already open.");
    }
  assert(fp_name == NULL);
  last_id++;
  fp = fopen(filename,"rb");
  if (fp == NULL)
    { KDU_ERROR(e,1); e <<
        KDU_TXT("Unable to open input file")
        << ", \"" << filename << "\".";
    }

  fp_name = new char[strlen(filename)+1];
  strcpy(fp_name,filename);
  last_read_pos = 0;
  last_bin_id = -1;
  last_bin_class = -1;
  last_bin_codestream = -1;
  last_bin_length = 0;
  last_bin_complete = false;
  seekable = allow_seeks;
}

/*****************************************************************************/
/*                       jp2_family_src::open (indirect)                     */
/*****************************************************************************/

void
  jp2_family_src::open(kdu_compressed_source *indirect)
{
  if ((fp != NULL) || (this->indirect != NULL) || (cache != NULL))
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("Attempting to open a `jp2_family_src' object which "
        "is already open.");
    }
  assert(fp_name == NULL);
  last_id++;
  int capabilities = indirect->get_capabilities();
  if (!(capabilities & KDU_SOURCE_CAP_SEQUENTIAL))
    { KDU_ERROR_DEV(e,3); e <<
        KDU_TXT("The `kdu_compressed_source' object supplied to "
        "`jp2_family_src::open' must support sequential reading.");
    }
  this->indirect = indirect;
  last_read_pos = 0;
  last_bin_id = -1;
  last_bin_class = -1;
  last_bin_codestream = -1;
  last_bin_length = 0;
  last_bin_complete = false;
  seekable = (capabilities & KDU_SOURCE_CAP_SEEKABLE)?true:false;
}

/*****************************************************************************/
/*                         jp2_family_src::open (cache)                      */
/*****************************************************************************/

void
  jp2_family_src::open(kdu_cache *cache)
{
  if ((fp != NULL) || (indirect != NULL) || (this->cache != NULL))
    { KDU_ERROR_DEV(e,4); e <<
        KDU_TXT("Attempting to open a `jp2_family_src' object which "
        "is already open.");
    }
  assert(fp_name == NULL);
  last_id++;
  this->cache = cache;
  last_read_pos = -1;
  last_bin_id = -1;
  last_bin_class = -1;
  last_bin_codestream = -1;
  last_bin_length = 0;
  last_bin_complete = false;
  seekable = true;
}

/*****************************************************************************/
/*                            jp2_family_src::close                          */
/*****************************************************************************/

void
  jp2_family_src::close()
{
  if (fp != NULL)
    fclose(fp);
  fp = NULL;
  if (fp_name != NULL)
    delete[] fp_name;
  fp_name = NULL;
  indirect = NULL;
  cache = NULL;
  last_read_pos = last_bin_id = last_bin_codestream = -1;
  last_bin_class = -1; last_bin_length = 0; last_bin_complete = false;
}

/*****************************************************************************/
/*                  jp2_family_src::is_top_level_complete                    */
/*****************************************************************************/

bool
  jp2_family_src::is_top_level_complete()
{
  if (cache == NULL)
    return true;
  acquire_lock();
  if ((last_bin_id != 0) ||
      (last_bin_class != KDU_META_DATABIN) ||
      (last_bin_codestream != 0))
    { 
      last_bin_id = 0;
      last_bin_class = KDU_META_DATABIN;
      last_bin_codestream = 0;
      last_bin_length = 0; last_bin_complete = false;
      last_read_pos = 0;
      last_bin_length =
        cache->set_read_scope(last_bin_class,last_bin_codestream,
                              last_bin_id,&last_bin_complete);
    }
  bool result = last_bin_complete;
  release_lock();
  return result;
}

/*****************************************************************************/
/*             jp2_family_src::is_codestream_main_header_complete            */
/*****************************************************************************/

bool
  jp2_family_src::is_codestream_main_header_complete(kdu_long cs_id)
{
  if (cache == NULL)
    return true;
  bool is_complete=false;
  int hdr_len = cache->get_databin_length(KDU_MAIN_HEADER_DATABIN,
                                          cs_id,0,&is_complete);
  if ((hdr_len == 0) || !is_complete)
    return false;
  if ((last_bin_codestream == cs_id) &&
      (last_bin_class == KDU_MAIN_HEADER_DATABIN))
    { // We are at risk of returning a condition which is inconsistent with
      // the information returned via `jp2_input_box' -- it may not reload the
      // state of an attached cache by invoking `cache->set_read_scope',
      // because this appears to be unnecessary.
      synch_with_cache();
    }
  return true;
}


/* ========================================================================= */
/*                               jp2_input_box                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        jp2_input_box::jp2_input_box                       */
/*****************************************************************************/

jp2_input_box::jp2_input_box()
{
  super_box = NULL;
  src = NULL;
  src_unsafe = false;
  contents_block = contents_handle = NULL;
  box_type = 0;
  original_box_length = 0;
  next_box_offset = 0;
  contents_start = 0;
  contents_lim = 0;
  bin_id = -1;
  codestream_min = codestream_lim = -1;
  bin_class = -1;
  rubber_length = false;
  passed_rubber_subbox = false;
  is_open = is_locked = false;
  capabilities = 0;
  pos = 0;
  codestream_id = -1;
  partial_word_bytes = 0;
}

/*****************************************************************************/
/*                       jp2_input_box::read_box_header                      */
/*****************************************************************************/

bool
  jp2_input_box::read_box_header(bool prefer_originals)
{
  if (src_unsafe)
    return false;
  reset_header_reading_state();
  can_dereference_contents = (locator.file_pos >= 0);
  if (src->cache == NULL)
    { pos=locator.file_pos; bin_id=-1; bin_class=-1; }
  else
    { pos=locator.bin_pos; bin_id=locator.bin_id; bin_class=KDU_META_DATABIN; }

  kdu_long contents_length = 0;
  bool have_placeholder = false;

  is_open = true; // Temporarily mark as open so that `read' will work.
  contents_start = pos; // Temporarily ensure that `read' works.
  contents_lim = KDU_LONG_MAX; // Temporarily ensure that `read' works.
  if (super_box != NULL)
    {
      if ((super_box->contents_block != NULL) && (src->cache == NULL))
        {
          assert(this->contents_handle == NULL);
          this->contents_block = super_box->contents_block +
            (this->contents_start - super_box->contents_start);
        }
      if (!super_box->rubber_length)
        contents_lim = super_box->contents_lim; // Temporary limit
    }

  partial_word_bytes = 0;

  if (read(buffer,8) < 8)
    {
      reset_header_reading_state();
      return false;
    }

  original_box_length = read_big(buffer,4);
  box_type = (kdu_uint32) read_big(buffer+4,4);
  if (box_type == jp2_placeholder_4cc)
    {
      have_placeholder = true;
      contents_lim = KDU_LONG_MAX;
      if (super_box != NULL)
        super_box->contents_lim=KDU_LONG_MAX; // Placeholders change everything
      if (original_box_length < 28)
        {
          reset_header_reading_state();
          KDU_ERROR(e,5); e <<
            KDU_TXT("Illegal placeholder box encountered.  "
            "Placeholders must not use the extended length field and must "
            "have a length of at least 28 bytes!");
        }
    }
  original_header_length = 8;
  if (original_box_length == 1)
    { // Box has extended length
      assert(!have_placeholder);
      original_header_length = 16;
      if (read(buffer,8) < 8)
        {
          reset_header_reading_state();
          return false;
        }
      original_box_length = read_big(buffer,8);
    }
  rubber_length = (original_box_length == 0);  
  if (rubber_length && (contents_block != NULL))
    { // We have already read and buffered the entire contents in super_box
      assert(super_box != NULL);
      original_box_length =
        (super_box->contents_lim - pos) + original_header_length;
      rubber_length = false;
    }
  contents_length = original_box_length - original_header_length;
  if ((!rubber_length) && (contents_length < 0))
    {
      reset_header_reading_state();
      KDU_ERROR(e,6); e <<
        KDU_TXT("Illegal box length field encountered in JP2 file.");
    }
  next_box_offset = original_box_length; // 0 if there are no more boxes

  if (!have_placeholder)
    {
      contents_start = pos;
      contents_lim = contents_start + contents_length;
      if (contents_length < 0)
        contents_lim = KDU_LONG_MAX;
      if (contents_block != NULL)
        contents_block += original_header_length;
      return true; // All done
    }

  // If we get here we are reading a placeholder box
  assert(contents_block == NULL);
  if (read(buffer,20) < 20)
    {
      reset_header_reading_state();
      return false;
    }
  kdu_uint32 flags = (kdu_uint32) read_big(buffer,4);
  kdu_long contents_bin_id = read_big(buffer+4,8);
  original_box_length = read_big(buffer+12,4);
  box_type = (kdu_uint32) read_big(buffer+16,4);
  original_header_length = 8;
  if (original_box_length == 1)
    { // Get extended length
      original_header_length = 16;
      if (read(buffer,8) < 8)
        {
          reset_header_reading_state();
          return false;
        }
      original_box_length = read_big(buffer,8);
    }
  rubber_length = (original_box_length == 0);
  contents_length = original_box_length - original_header_length;
  if ((!rubber_length) && (contents_length < 0))
    {
      reset_header_reading_state();
      KDU_ERROR(e,7); e <<
        KDU_TXT("Illegal box length field encountered in JP2 file.");
    }
  
  if ((prefer_originals && ((flags & 1) != 0)) || ((flags & 6) == 0))
    { // Treat as simple placeholder; no stream equivalents or codestreams
      bin_id = contents_bin_id;
      contents_start = pos = 0;
      if ((flags & 1) == 0)
        { // No access provided to the box contents
          box_type = 0; contents_lim = 0;
        }
      else
        {
          contents_lim = contents_start + contents_length;
          if (contents_length < 0)
            contents_lim = KDU_LONG_MAX;
        }
      return true;
    }

  // If we get here, we are looking to follow a stream equivalent
  can_dereference_contents = false;
  if (read(buffer,16) < 16)
    {
      reset_header_reading_state();
      return false;
    }
  contents_bin_id = read_big(buffer,8);
  kdu_long equiv_box_length = read_big(buffer+8,4);
  box_type = (kdu_uint32) read_big(buffer+12,4);
  contents_length = equiv_box_length - 8;
  if (equiv_box_length == 1)
    {
      if (read(buffer,8) < 8)
        {
          reset_header_reading_state();
          return false;
        }
      equiv_box_length = read_big(buffer,8);
      contents_length = equiv_box_length - 16;
    }
  if ((flags & 2) == 0)
    box_type = 0;
  else if ((equiv_box_length != 0) && (contents_length < 0))
    {
      reset_header_reading_state();
      KDU_ERROR(e,8); e <<
        KDU_TXT("Illegal box length field encountered in stream equivalent "
        "box header embedded within a JP2 placeholder box.");
    }

  if ((flags & 4) == 0)
    { // Return with stream equivalent box active
      bin_id = contents_bin_id;
      contents_start = pos = 0;
      contents_lim = contents_start + contents_length;
      if (contents_length < 0)
        contents_lim = KDU_LONG_MAX;
      return true;
    }

  // If we get here, we have a code-stream equivalent.
  if (read(buffer,8) < 8)
    {
      reset_header_reading_state();
      return false;
    }
  codestream_min = read_big(buffer,8);
  codestream_lim = codestream_min + 1;
  if (flags & 8)
    { // Placeholder represents a range of code-streams
      if (read(buffer,4) < 4)
        {
          reset_header_reading_state();
          return false;
        }
      codestream_lim = codestream_min + (kdu_uint32) read_big(buffer,4);
    }
  box_type = jp2_codestream_4cc;
  bin_class = KDU_MAIN_HEADER_DATABIN;
  bin_id = 0;
  codestream_id = codestream_min;
  contents_start = pos = 0;
  contents_lim = KDU_LONG_MAX;
  return true;
}

/*****************************************************************************/
/*                          jp2_input_box::open (src)                        */
/*****************************************************************************/

bool
  jp2_input_box::open(jp2_family_src *src, jp2_locator loc)
{
  if (is_open)
    { KDU_ERROR_DEV(e,9); e <<
        KDU_TXT("Attempting to call `jp2_input_box::open' without "
        "first closing the box."); }
  is_locked = false;
  locator = loc;
  super_box = NULL;
  this->src = src;
  src_unsafe = false;
  if ((src->cache != NULL) && (locator.bin_id < 0))
    { // Walk the boxes to convert file_pos to a data-bin identifier
      kdu_long target_pos = locator.file_pos;  assert(target_pos >= 0);
      locator.file_pos = locator.bin_id = 0; locator.bin_pos = 0;
      while (locator.file_pos != target_pos)
        {
          if (!read_box_header(true))
            return false;
          is_open = false;
          if (target_pos >= (locator.file_pos+original_box_length))
            { // Advance to next box
              if (original_box_length <= 0)
                { KDU_ERROR_DEV(e,10); e <<
                    KDU_TXT("Invoking `jp2_input_box::open' "
                    "with a `jp2_locator' object which references an invalid "
                    "original file location.");
                }
              locator.file_pos += original_box_length;
              locator.bin_pos += next_box_offset;
              continue;
            }
          if (target_pos < (locator.file_pos + original_header_length))
            { KDU_ERROR_DEV(e,11); e <<
                KDU_TXT("Invoking `jp2_input_box::open' "
                "with a `jp2_locator' object which references an invalid "
                "original file location.");
            }
          locator.file_pos += original_header_length;
          locator.bin_id = bin_id;
          locator.bin_pos = contents_start;
          if ((box_type == 0) || !can_dereference_contents)
            { KDU_ERROR(e,12); e <<
                KDU_TXT("Unable to dereference file offset in the "
                "`jp2_locator' object supplied to `jp2_input_box::open'.  "
                "The server is deliberately preventing access to the "
                "original box in which the file offset resides.");
            }
        }
    }

  if (!read_box_header())
    return false;
  if (box_type == 0)
    { is_open = false;
      KDU_ERROR(e,13); e <<
        KDU_TXT("Unable to open the box identified by the "
        "`jp2_locator' object supplied to `jp2_input_box::open'.  "
        "The server is deliberately preventing access to "
        "the box or any stream equivalent.");
    }

  if ((src->cache != NULL) && (box_type == jp2_codestream_4cc))
    capabilities = KDU_SOURCE_CAP_CACHED;
  else
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL;
  if (src->seekable)
    capabilities |= KDU_SOURCE_CAP_SEEKABLE;
  if (contents_block != NULL)
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL |
                   KDU_SOURCE_CAP_SEEKABLE |
                   KDU_SOURCE_CAP_IN_MEMORY;
  return true;
}

/*****************************************************************************/
/*                        jp2_input_box::open (sub-box)                      */
/*****************************************************************************/

bool
  jp2_input_box::open(jp2_input_box *super_box)
{
  if (is_open)
    { KDU_ERROR_DEV(e,14); e <<
        KDU_TXT("Attempting to call `jp2_input_box::open' without "
        "first closing the box.");
    }
  if (super_box->is_locked || !super_box->is_open)
    { KDU_ERROR_DEV(e,15); e <<
        KDU_TXT("Attempting to open a sub-box of a box which is "
        "not itself open, or which has already been locked by another open "
        "sub-box which has not yet been closed.");
    }
  this->super_box = super_box;
  this->src = super_box->src;
  if (!super_box->can_dereference_contents)
    locator.file_pos = -1;
  else
    locator.file_pos = super_box->locator.file_pos +
      super_box->original_header_length + super_box->original_pos_offset +
      (super_box->pos - super_box->contents_start);

  do { // Loop to skip over original boxes to which access is denied
      if (src->cache == NULL)
        { locator.bin_id = -1; locator.bin_pos = -1; }
      else
        {
          if (super_box->bin_class != KDU_META_DATABIN)
            { KDU_ERROR_DEV(e,16); e <<
                KDU_TXT("Attempting to open a sub-box of a contiguous "
                "codestream box (may be a stream equivalent contiguous "
                "codestream for a real original box, which might have had "
                "sub-boxes), but you should have checked.");
            }
          locator.bin_id = super_box->bin_id;
          locator.bin_pos = super_box->pos;
        }

      if (!read_box_header())
        return false;
      if (box_type == 0)
        {
          close(); // The box is not accessible to us
          locator.file_pos += original_box_length;
        }
    } while (box_type == 0);

  if ((src->cache != NULL) && (box_type == jp2_codestream_4cc))
    capabilities = KDU_SOURCE_CAP_CACHED;
  else
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL;
  if (src->seekable)
    capabilities |= KDU_SOURCE_CAP_SEEKABLE;
  if (contents_block != NULL)
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL |
                   KDU_SOURCE_CAP_SEEKABLE |
                   KDU_SOURCE_CAP_IN_MEMORY;
  super_box->is_locked = true;
  return true;
}

/*****************************************************************************/
/*                          jp2_input_box::open_next                         */
/*****************************************************************************/

bool
  jp2_input_box::open_next()
{
  if ((src == NULL) || is_open)
    { KDU_ERROR_DEV(e,17); e <<
        KDU_TXT("You may not use `jp2_input_box::open_next' "
        "unless the object has been previously used to open and then close "
        "a box within the source.");
    }
  if (super_box != NULL)
    return open(super_box);
  
  if (src_unsafe)
    return false;

  do { // Loop to skip over boxes to which we will not be allowed access
      if (rubber_length)
        return false;
      if (locator.file_pos >= 0)
        locator.file_pos += original_box_length;
      if (src->cache != NULL)
        locator.bin_pos += next_box_offset;
      if (!read_box_header())
        return false;
      if (box_type == 0)
        close();
    } while (box_type == 0);

  if ((src->cache != NULL) && (box_type == jp2_codestream_4cc))
    capabilities = KDU_SOURCE_CAP_CACHED;
  else
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL;
  if (src->seekable)
    capabilities |= KDU_SOURCE_CAP_SEEKABLE;
  if (contents_block != NULL)
    capabilities = KDU_SOURCE_CAP_SEQUENTIAL |
                   KDU_SOURCE_CAP_SEEKABLE |
                   KDU_SOURCE_CAP_IN_MEMORY;
  return true;
}

/*****************************************************************************/
/*                           jp2_input_box::open_as                          */
/*****************************************************************************/

bool
  jp2_input_box::open_as(kdu_uint32 b_type, jp2_family_src *ultimate_src,
                         jp2_locator box_locator, jp2_locator cont_locator,
                         kdu_long cont_length)
{
  if (is_open)
    { KDU_ERROR_DEV(e,0x18041201); e <<
      KDU_TXT("Attempting to call `jp2_input_box::open_as' without "
              "first closing the box."); }    
  if (ultimate_src == NULL)
    { KDU_ERROR_DEV(e,0x18041202); e <<
      KDU_TXT("Attempting to call `jp2_input_box::open_as' with "
              "a NULL `jp2_family_src' reference."); }
  if (b_type == 0)
    return false;
  if (cont_length < 0)
    cont_length = 0; // Just in case
  this->src_unsafe = false;
  if (ultimate_src->cache != NULL)
    { 
      if (!open(ultimate_src,box_locator))
        return false;
      this->box_type = b_type;
      if ((!rubber_length) &&
          ((contents_start+cont_length) < contents_lim))
        contents_lim = contents_start + cont_length;
    }
  else
    { 
      kdu_long box_pos = box_locator.get_file_pos();
      kdu_long cont_pos = cont_locator.get_file_pos();
      reset_header_reading_state();
      this->locator = box_locator;
      this->super_box = NULL;
      this->src = ultimate_src;
      this->box_type = b_type;
      this->original_header_length = (cont_pos - box_pos);
      this->original_box_length = cont_length + original_header_length;
      if (original_header_length < 0)
        original_header_length = 0;
      this->next_box_offset = original_box_length;
      this->contents_start = cont_pos;
      this->contents_lim = cont_pos + cont_length;
      this->bin_id = -1;
      this->bin_class = -1;
      this->can_dereference_contents = true;
      this->is_open = true;
      this->is_locked = false;
      this->capabilities = KDU_SOURCE_CAP_SEQUENTIAL;
      if (src->seekable)
        capabilities |= KDU_SOURCE_CAP_SEEKABLE;
      this->pos = contents_start;
      this->partial_word_bytes = 0;
    }
  return true;
}

/*****************************************************************************/
/*                            jp2_input_box::close                           */
/*****************************************************************************/

bool
  jp2_input_box::close()
{
  if (!is_open)
    return true;
  if ((src != NULL) && (!src_unsafe) && (src->cache != NULL))
    is_complete(); // Calling this function makes sure we have the most
                   // up-to-date info on the length of the box contents
  is_open = false;
  box_type = 0;
  capabilities = 0;

  bool result = true;
  if ((!rubber_length) && (pos < contents_lim))
    { result = false; pos = contents_lim; }
  if (super_box != NULL)
    { 
      super_box->is_locked = false;
      super_box->pos += next_box_offset;
      super_box->original_pos_offset += original_box_length-next_box_offset;
      if ((super_box->contents_lim == KDU_LONG_MAX) &&
          ((super_box->original_header_length +
            super_box->original_pos_offset +
            super_box->pos - super_box->contents_start) ==
           super_box->original_box_length))
        { // Must be a cached source and we set `contents_lim' to KDU_LONG_MAX
          // because it contained placeholders, so we were unable to determine
          // the reading extent immediately.
          super_box->contents_lim = super_box->pos;
        }
      if (rubber_length && (next_box_offset == 0))
        { 
          super_box->pos = pos;
          super_box->passed_rubber_subbox = true;
        }
    }
  contents_block = NULL;
  if (contents_handle != NULL)
    {
      free(contents_handle);
      contents_handle = NULL;
    }
  return result;
}

/*****************************************************************************/
/*                         jp2_input_box::transplant                         */
/*****************************************************************************/

void
  jp2_input_box::transplant(jp2_input_box &src)
{
  if (is_open || !src.is_open)
    { KDU_ERROR_DEV(e,18); e <<
        KDU_TXT("Attempting to invoke `jp2_input_box::transplant' "
        "on a box which is currently open, or using a donor which is not "
        "currently open."); }
  this->locator = src.locator;
  this->super_box = NULL;
  this->src = src.src;
  this->contents_block = src.contents_block;
  this->contents_handle = src.contents_handle;
  src.contents_block = src.contents_handle = NULL;
  this->box_type = src.box_type;
  this->original_box_length = src.original_box_length;
  this->original_header_length = src.original_header_length;
  this->next_box_offset = src.next_box_offset;
  this->original_pos_offset = src.original_pos_offset;
  this->contents_start = src.contents_start;
  this->contents_lim = src.contents_lim;
  this->bin_id = src.bin_id;
  this->codestream_min = src.codestream_min;
  this->codestream_lim = src.codestream_lim;
  this->bin_class = src.bin_class;
  this->can_dereference_contents = src.can_dereference_contents;
  this->rubber_length = src.rubber_length;
  this->passed_rubber_subbox = src.passed_rubber_subbox;
  this->is_open = true;
  this->is_locked = false;
  this->capabilities = src.capabilities;
  this->pos = src.pos;
  this->codestream_id = src.codestream_id;
  this->partial_word_bytes = src.partial_word_bytes;
  for (int i=0; i < partial_word_bytes; i++)
    this->buffer[i] = src.buffer[i];
  src.close();
}

/*****************************************************************************/
/*                            jp2_input_box::fork                            */
/*****************************************************************************/

void
  jp2_input_box::fork(const jp2_input_box &src)
{
  if (is_open || !src.is_open)
    { KDU_ERROR_DEV(e,0x08051201); e <<
        KDU_TXT("Attempting to invoke `jp2_input_box::fork' "
                "on a box which is currently open, or using a forking source "
                "which is not currently open."); }
  this->locator = src.locator;
  this->super_box = NULL;
  this->src = src.src;
  this->box_type = src.box_type;
  this->original_box_length = src.original_box_length;
  this->original_header_length = src.original_header_length;
  this->next_box_offset = src.next_box_offset;
  this->original_pos_offset = src.original_pos_offset;
  this->contents_start = src.contents_start;
  this->contents_lim = src.contents_lim;
  this->bin_id = src.bin_id;
  this->codestream_min = src.codestream_min;
  this->codestream_lim = src.codestream_lim;
  this->bin_class = src.bin_class;
  this->can_dereference_contents = src.can_dereference_contents;
  this->rubber_length = src.rubber_length;
  this->passed_rubber_subbox = src.passed_rubber_subbox;
  this->is_open = true;
  this->is_locked = false;
  this->capabilities = src.capabilities & ~(KDU_SOURCE_CAP_IN_MEMORY);
  this->pos = src.pos;
  this->codestream_id = src.codestream_id;
  this->partial_word_bytes = src.partial_word_bytes;
  for (int i=0; i < partial_word_bytes; i++)
    this->buffer[i] = src.buffer[i];
  if ((src.contents_block != NULL) &&
      (src.contents_block == src.contents_handle))
    { 
      int alloc_bytes = (int)(contents_lim - contents_start);
      if ((alloc_bytes < 0) ||
          ((contents_start + alloc_bytes) != contents_lim))
        alloc_bytes = 0;
      kdu_byte *mem = (kdu_byte *) malloc((size_t) alloc_bytes);
      if (mem != NULL)
        { 
          memcpy(mem,src.contents_handle,(size_t) alloc_bytes);
          this->contents_block = this->contents_handle = mem;
          this->capabilities |= KDU_SOURCE_CAP_IN_MEMORY;
        }
    }
}

/*****************************************************************************/
/*                        jp2_input_box::get_box_bytes                       */
/*****************************************************************************/

kdu_long
  jp2_input_box::get_box_bytes() const
{
  if (!is_open)
    return 0;
  if (rubber_length)
    return (pos-contents_start) + original_header_length;
  return (contents_lim-contents_start) + original_header_length;
}

/*****************************************************************************/
/*                        jp2_input_box::is_complete                         */
/*****************************************************************************/

bool
  jp2_input_box::is_complete()
{
  if (!is_open)
    return false;
  if ((src == NULL) || src_unsafe)
    return false;
  if (contents_block != NULL)
    return true;
  if (src->cache != NULL)
    { 
      assert((bin_id >= 0) && (bin_class >= 0));
      kdu_long cs_id = (bin_class==KDU_META_DATABIN)?0:codestream_id;
      src->acquire_lock();
      if ((src->last_bin_id != bin_id) ||
          (src->last_bin_class != bin_class) ||
          (src->last_bin_codestream != cs_id))
        { 
          src->last_bin_id = bin_id;
          src->last_bin_class = bin_class;
          src->last_bin_codestream = cs_id;
          src->last_bin_length = 0; src->last_bin_complete = false;
          src->last_read_pos = 0;
          src->last_bin_length =
            src->cache->set_read_scope(bin_class,cs_id,bin_id,
                                       &src->last_bin_complete);
        }
      int bin_length = src->last_bin_length;
      bool bin_complete = src->last_bin_complete;
      src->release_lock();
      if ((bin_class != KDU_META_DATABIN) || rubber_length)
        return bin_complete;
      else
        { 
          if (bin_complete)
            { 
              if (contents_lim > (kdu_long) bin_length)
                contents_lim = bin_length;
            }
          else if ((contents_lim <= (kdu_long) bin_length) &&
                   ((pos == contents_lim) || !jp2_is_superbox(box_type)))
            { // This clause exists to allow us to determine bin completeness
              // as early as possible, without necessarily waiting for the
              // server to send the "bin-complete" message.  The additional
              // tests on the second line of the "if" statement address the
              // possibility that the contents of a super-box may have been
              // replaced with placeholders that are longer than the original
              // boxes (not common, but may reasonably occur if an FTBL
              // box is replaced by a codestream equivalent placeholder).
              // If we have already encountered any placeholders amongst our
              // descendants, `contents_lim' will have been set to
              // `KDU_LONG_MAX', so we will not get here; otherwise, if
              // `pos'=`contents_lim', we can be sure that there will be no
              // more placeholder descendants.
              bin_complete = true;
            }
          return bin_complete;
        }
    }
  return true;
}

/*****************************************************************************/
/*                           jp2_input_box::seek                             */
/*****************************************************************************/

bool
 jp2_input_box::seek(kdu_long offset)
{
  if ((!is_open) || is_locked)
    { KDU_ERROR_DEV(e,19); e <<
        KDU_TXT("Attempting to seek inside a JP2 box which is not "
        "open, or is sharing its read pointer with an open sub-box.");
    }
  if ((contents_block == NULL) && (src_unsafe || !src->seekable))
    return false;
  kdu_long new_pos = contents_start + offset;
  if (new_pos > contents_lim)
    new_pos = contents_lim;
  if (new_pos < contents_start)
    new_pos = contents_start;
  if (new_pos < pos)
    passed_rubber_subbox = false;
  pos = new_pos;
  partial_word_bytes = 0;
  return true;
}

/*****************************************************************************/
/*                   jp2_input_box::set_tileheader_scope                     */
/*****************************************************************************/

bool
 jp2_input_box::set_tileheader_scope(int tnum, int num_tiles)
{
  assert(contents_block == NULL);
  if ((!is_open) || (src == NULL) || src_unsafe || (src->cache == NULL) ||
      (codestream_id < 0))
    return false;
  bin_class = KDU_TILE_HEADER_DATABIN;
  bin_id = tnum;
  pos = contents_start = 0;
  src->acquire_lock();
  if ((src->last_bin_id != bin_id) ||
      (src->last_bin_class != bin_class) ||
      (src->last_bin_codestream != codestream_id))
    { 
      src->last_bin_id = bin_id;
      src->last_bin_class = bin_class;
      src->last_bin_codestream = codestream_id;
      src->last_bin_length = 0; src->last_bin_complete = false;
      src->last_read_pos = 0;
      src->last_bin_length =
      src->cache->set_read_scope(bin_class,codestream_id,bin_id,
                                 &src->last_bin_complete);
    }
  int bin_length = src->last_bin_length;
  bool bin_complete = src->last_bin_complete;
  src->release_lock();
  contents_lim = (bin_complete)?bin_length:KDU_LONG_MAX;
  return bin_complete;
}

/*****************************************************************************/
/*                    jp2_input_box::set_precinct_scope                      */
/*****************************************************************************/

bool
 jp2_input_box::set_precinct_scope(kdu_long unique_id)
{
  assert(contents_block == NULL);
  if ((!is_open) || (src == NULL) || src_unsafe || (src->cache == NULL) ||
      (codestream_id < 0))
    return false;
  bin_class = KDU_PRECINCT_DATABIN;
  bin_id = unique_id;
  pos = contents_start = 0;
  contents_lim = KDU_LONG_MAX;
  return true;
}

/*****************************************************************************/
/*                   jp2_input_box::get_codestream_scope                     */
/*****************************************************************************/

kdu_long
  jp2_input_box::get_codestream_scope()
{
  if ((!is_open) || (src == NULL) ||
      (codestream_min < 0) || (codestream_lim <= codestream_min))
    return -1;
  return codestream_id;
}

/*****************************************************************************/
/*                   jp2_input_box::set_codestream_scope                     */
/*****************************************************************************/

bool
 jp2_input_box::set_codestream_scope(kdu_long cs_id, bool need_main_header)
{
  assert(contents_block == NULL);
  if ((!is_open) || (src == NULL) || src_unsafe || 
      (codestream_min > cs_id) || (codestream_lim <= cs_id))
    return false;
  bin_class = KDU_MAIN_HEADER_DATABIN;
  bin_id = 0;
  codestream_id = cs_id;
  pos = contents_start = 0;
  contents_lim = KDU_LONG_MAX;
  if (!need_main_header)
    return true;
  src->acquire_lock();
  if ((src->last_bin_id != bin_id) ||
      (src->last_bin_class != bin_class) ||
      (src->last_bin_codestream != codestream_id))
    { 
      src->last_bin_id = bin_id;
      src->last_bin_class = bin_class;
      src->last_bin_codestream = codestream_id;
      src->last_bin_length = 0; src->last_bin_complete = false;
      src->last_read_pos = 0;
      src->last_bin_length =
      src->cache->set_read_scope(bin_class,codestream_id,bin_id,
                                 &src->last_bin_complete);
    }
  bool bin_complete = src->last_bin_complete;
  src->release_lock();
  return bin_complete;
}

/*****************************************************************************/
/*                       jp2_input_box::access_memory                        */
/*****************************************************************************/

kdu_byte *
  jp2_input_box::access_memory(kdu_long &pos, kdu_byte * &lim)
{
  if (contents_block == NULL)
    return NULL;
  pos = this->pos-this->contents_start;
  lim = contents_block + (contents_lim - contents_start);
  return contents_block;
}

/*****************************************************************************/
/*                       jp2_input_box::load_in_memory                       */
/*****************************************************************************/

bool
  jp2_input_box::load_in_memory(int max_bytes, void *external_buffer,
                                int external_buffer_size)
{
  if (contents_block != NULL)
    return true; // Already in memory
  if (!is_open)
    return false;
  if ((src != NULL) && (src_unsafe || (src->cache != NULL)))
    return false; // Later, we may consider loading cache boxes into memory
  kdu_long restore_pos = get_pos();
  if ((pos != contents_start) && !seek(0))
    return false; // Seeking is obviously not supported
  if (rubber_length)
    {
      if ((src == NULL) || (src->fp == NULL))
        return false; // Cannot resolve true length
      src->acquire_lock();
      kdu_fseek(src->fp,0,SEEK_END);
      contents_lim = ftell(src->fp);
      src->last_read_pos = contents_lim;
      rubber_length = false;
      src->release_lock();
    }
  if (contents_lim > (contents_start + max_bytes))
    return false;
  int alloc_bytes = (int)(contents_lim - contents_start);
  if ((alloc_bytes < 0) ||
      ((contents_start + alloc_bytes) != contents_lim))
    return false;
  kdu_byte *mem = NULL;
  if ((external_buffer != NULL) &&
      (external_buffer_size >= alloc_bytes))
    mem = (kdu_byte *) external_buffer;
  else
    { 
      external_buffer = NULL; // So we know that `mem' needs to be freed later
      mem = (kdu_byte *) malloc((size_t) alloc_bytes);
      if (mem == NULL)
        return false; // Cannot allocate this much memory
    }
  try {
      int read_bytes = read(mem, alloc_bytes);
      contents_lim = contents_start + read_bytes;
      contents_block = mem;
      if (external_buffer == NULL)
        contents_handle = mem;
    }
  catch (kdu_exception exc) {
    if (external_buffer == NULL)
      free(mem);
    throw exc;
  }
  capabilities = KDU_SOURCE_CAP_SEQUENTIAL | KDU_SOURCE_CAP_SEEKABLE |
                 KDU_SOURCE_CAP_IN_MEMORY;
  seek(restore_pos);
  return true;
}

/*****************************************************************************/
/*                         jp2_input_box::read (array)                       */
/*****************************************************************************/

int
  jp2_input_box::read(kdu_byte *buf, int num_bytes)
{
  if ((src == NULL) || (!is_open) || is_locked)
    { KDU_ERROR_DEV(e,20); e <<
        KDU_TXT("Illegal attempt to read from a JP2 box which "
        "is either not open or else has an open sub-box.");
    }
  if (src_unsafe || passed_rubber_subbox)
    return 0;

  kdu_long max_bytes = contents_lim - pos;
  kdu_long req_bytes = num_bytes;
  if ((!rubber_length) && (max_bytes < req_bytes))
    req_bytes = max_bytes;
  if (req_bytes <= 0)
    return 0;
  int requested_bytes = (int) req_bytes;
  num_bytes = requested_bytes;
  if (contents_block != NULL)
    {
      memcpy(buf,contents_block+(pos-contents_start),(size_t) num_bytes);
      pos += num_bytes;
      return num_bytes;
    }

  src->acquire_lock();
  if (src->cache != NULL)
    { // See if we must open a new data-bin, or seek within the existing one.
      assert(src->seekable);
      assert(this->bin_class >= 0);
      kdu_long cs_id = (this->bin_class==KDU_META_DATABIN)?0:codestream_id;
      if ((src->last_bin_id != bin_id) ||
          (src->last_bin_class != this->bin_class) ||
          (src->last_bin_codestream != cs_id))
        { 
          src->last_bin_id = bin_id;
          src->last_bin_class = bin_class;
          src->last_bin_codestream = cs_id;
          src->last_bin_length = 0; src->last_bin_complete = false;
          src->last_read_pos = 0;
          src->last_bin_length =
            src->cache->set_read_scope(src->last_bin_class,cs_id,bin_id,
                                       &src->last_bin_complete);
        }
      if ((src->last_read_pos != pos) && !src->cache->seek(pos))
        { 
          src->release_lock();
          KDU_ERROR_DEV(e,21); e <<
            KDU_TXT("Caching source does not appear to support seeking!");
        }
      num_bytes = src->cache->read(buf,requested_bytes);
      pos += num_bytes;
      src->last_read_pos = pos;
      if (num_bytes < requested_bytes)
        src->last_bin_id = -1; // Forces a reload next time around
      int bin_length = src->last_bin_length;
      bool bin_complete = src->last_bin_complete;
      src->release_lock();
      if ((num_bytes < requested_bytes) && bin_complete &&
          (pos == bin_length))
        { // Bin contents will not grow any further
          if (rubber_length || (bin_class != KDU_META_DATABIN))
            { contents_lim = pos; rubber_length = false; }
          else if ((contents_lim != pos) && (contents_lim != KDU_LONG_MAX))
            { KDU_ERROR(e,22); e <<
              KDU_TXT("Cached data-bin appears to be complete "
                      "yet terminates prior to the end of the current "
                      "JP2 box.");
            }
          else
            contents_lim = pos;
        }
      return num_bytes;
    }

  // If we get here, we have regular file or stream access
  if (!src->seekable)
    {
      while (src->last_read_pos < pos)
        { // Skip over bytes we don't want
          int discard = J2_INPUT_MAX_BUFFER_BYTES;
          if ((src->last_read_pos+discard) > pos)
            discard = (int)(pos-src->last_read_pos);
          int read_bytes = 0;
          if (src->fp != NULL)
            read_bytes = (int)fread(buffer, 1, discard, src->fp);
          else
            read_bytes = src->indirect->read(buffer,discard);
          if (read_bytes != discard)
            break;
          src->last_read_pos += discard;
        }
      if (src->last_read_pos != pos)
        {
          src->release_lock();
          KDU_ERROR_DEV(e,23); e <<
            KDU_TXT("Non-seekable JP2 sources must be read "
            "sequentially.  You are probably trying to read from multiple "
            "boxes simultaneously.");
        }
    }
  else if (src->last_read_pos != pos)
    {
      if (src->fp != NULL)
        kdu_fseek(src->fp,this->pos);
      else if (src->indirect != NULL)
        src->indirect->seek(this->pos);
    }
  if (src->fp != NULL)
    num_bytes = (int)fread(buf, 1, (size_t)requested_bytes, src->fp);
  else if (src->indirect != NULL)
    num_bytes = src->indirect->read(buf,requested_bytes);
  pos += num_bytes;
  src->last_read_pos = pos;

  src->release_lock();
  if ((num_bytes < requested_bytes) && rubber_length)
    { // Now we know the box length
      contents_lim = pos;
      rubber_length = false;
    }

  return num_bytes;
}

/*****************************************************************************/
/*                         jp2_input_box::read (dword)                       */
/*****************************************************************************/

bool
  jp2_input_box::read(kdu_uint32 &dword)
{
  assert(partial_word_bytes < 4);
  partial_word_bytes += read(buffer+partial_word_bytes,4-partial_word_bytes);
  if (partial_word_bytes < 4)
    return false;
  assert(partial_word_bytes == 4);
  dword = buffer[0];
  dword = (dword<<8) + buffer[1];
  dword = (dword<<8) + buffer[2];
  dword = (dword<<8) + buffer[3];
  partial_word_bytes = 0;
  return true;
}

/*****************************************************************************/
/*                         jp2_input_box::read (word)                        */
/*****************************************************************************/

bool
  jp2_input_box::read(kdu_uint16 &word)
{
  if (partial_word_bytes >= 2)
    { KDU_ERROR_DEV(e,24); e <<
        KDU_TXT("Attempting to read a 2-byte word from a JP2 box, "
        "after first reading a partial 4-byte word!");
    }
  partial_word_bytes += read(buffer+partial_word_bytes,2-partial_word_bytes);
  if (partial_word_bytes < 2)
    return false;
  assert(partial_word_bytes == 2);
  word = buffer[0];
  word = (word<<8) + buffer[1];
  partial_word_bytes = 0;
  return true;
}



/* ========================================================================= */
/*                               jp2_family_tgt                              */
/* ========================================================================= */

/*****************************************************************************/
/*                         jp2_family_tgt::open (file)                       */
/*****************************************************************************/

void
  jp2_family_tgt::open(const char *filename)
{
  if ((fp != NULL) || (indirect != NULL) || opened_for_simulation)
    { KDU_ERROR_DEV(e,25); e <<
        KDU_TXT("Attempting to open a `jp2_family_tgt' object which "
        "is already open.");
    }
  fp = fopen(filename,"wb");
  if (fp == NULL)
    { KDU_ERROR(e,26); e <<
        KDU_TXT("Unable to open output file")
        << ", \"" << filename << "\".";
    }
  last_write_pos = 0;
  has_rubber_box = false;
}

/*****************************************************************************/
/*                       jp2_family_tgt::open (indirect)                     */
/*****************************************************************************/

void
  jp2_family_tgt::open(kdu_compressed_target *indirect)
{
  if ((fp != NULL) || (this->indirect != NULL) || opened_for_simulation)
    { KDU_ERROR_DEV(e,27); e <<
        KDU_TXT("Attempting to open a `jp2_family_tgt' object which "
        "is already open.");
    }
  this->indirect = indirect;
  last_write_pos = 0;
  has_rubber_box = false;
}

/*****************************************************************************/
/*                         jp2_family_tgt::open (sim)                        */
/*****************************************************************************/

void
  jp2_family_tgt::open(kdu_long simulated_start_pos)
{
  if ((fp != NULL) || (indirect != NULL) || opened_for_simulation)
    { KDU_ERROR_DEV(e,0x26050901); e <<
      KDU_TXT("Attempting to open a `jp2_family_tgt' object which "
              "is already open.");
    }
  opened_for_simulation = true;
  last_write_pos = simulated_start_pos;
  has_rubber_box = false;
}

/*****************************************************************************/
/*                             jp2_family_tgt::close                         */
/*****************************************************************************/

void
  jp2_family_tgt::close()
{
  if (fp != NULL)
    fclose(fp);
  fp = NULL;
  opened_for_simulation = false;
  indirect = NULL;
  has_rubber_box = false;
}


/* ========================================================================= */
/*                              jp2_output_box                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      jp2_output_box::jp2_output_box                       */
/*****************************************************************************/

jp2_output_box::jp2_output_box()
{
  box_type = last_box_type = 0;
  rubber_length = headerless = reopened = output_failed = false;
  write_immediately = write_header_on_close = force_long_header = false;
  super_box = NULL; tgt = NULL; rel_start_pos = cur_size = box_size = 0;
  restore_size = -1; buffer_size = 0; buffer = NULL;
}

/*****************************************************************************/
/*                      jp2_output_box::~jp2_output_box                      */
/*****************************************************************************/

jp2_output_box::~jp2_output_box()
{
  if (buffer != NULL)
    delete[] buffer;
}

/*****************************************************************************/
/*                           jp2_output_box::open                            */
/*****************************************************************************/

void
  jp2_output_box::open(jp2_family_tgt *tgt, kdu_uint32 box_type,
                       bool rubber_length, bool want_headerless)
{
  if (this->box_type != 0)
    { KDU_ERROR_DEV(e,28); e <<
        KDU_TXT("Attempting to open a `jp2_output_box' object which "
        "is already open.");
    }
  this->tgt = NULL;  this->super_box = NULL;
  if (tgt == NULL)
    assert(!rubber_length);
  else if (tgt->has_rubber_box)
    { KDU_ERROR_DEV(e,29); e <<
        KDU_TXT("Attempting to open a `jp2_output_box' to write to "
        "a `jp2_family_tgt' object which already contains a rubber length "
        "box.  Any rubber length box must be the last box in the data "
        "stream.");
    }
  assert((buffer == NULL) && (buffer_size == 0));
  this->box_type = this->last_box_type = box_type;
  this->rubber_length = rubber_length;
  this->headerless = want_headerless;
  this->tgt = tgt;
  rel_start_pos = (tgt==NULL)?0:tgt->last_write_pos;
  cur_size = 0; // Content bytes written to box so far
  box_size = -1; // Do not know the box size yet
  restore_size = -1; // Not in a rewrite section
  output_failed = false;
  write_immediately = rubber_length || want_headerless;
  write_header_on_close = false;
  force_long_header = false;
  if (write_immediately && !want_headerless)
    write_header();
}

/*****************************************************************************/
/*                           jp2_output_box::open                            */
/*****************************************************************************/

void
  jp2_output_box::open(jp2_output_box *super_box, kdu_uint32 box_type,
                       bool rubber_length, bool want_headerless)
{
  if (this->box_type != 0)
    { KDU_ERROR_DEV(e,30); e <<
        KDU_TXT("Attempting to open a `jp2_output_box' object which "
        "is already open.");
    }
  assert((buffer == NULL) && (buffer_size == 0));
  this->box_type = this->last_box_type = box_type;
  this->rubber_length = rubber_length;
  this->headerless = want_headerless;
  this->super_box = super_box;
  this->tgt = NULL;
  rel_start_pos = super_box->cur_size;
  cur_size = 0; // Content bytes written to box so far
  box_size = -1; // Do not know the length yet
  restore_size = -1; // Not in a rewrite section
  output_failed = false;
  write_immediately = rubber_length || want_headerless;
  write_header_on_close = false;
  force_long_header = false;
  if (write_immediately && !want_headerless)
    {
      super_box->set_rubber_length();
      assert(super_box->rubber_length && super_box->write_immediately);
      write_header();
    }
}

/*****************************************************************************/
/*                         jp2_output_box::open_next                         */
/*****************************************************************************/

void
  jp2_output_box::open_next(kdu_uint32 box_type, bool rubber_length,
                            bool want_headerless)
{
  if (super_box != NULL)
    open(super_box,box_type,rubber_length,want_headerless);
  else if (tgt != NULL)
    open(tgt,box_type,rubber_length,want_headerless);
  else
    { KDU_ERROR_DEV(e,31); e <<
        KDU_TXT("You cannot call `jp2_output_box::open_next' on a "
        "box which has never been opened either as a sub-box of another "
        "box or as a top-level box within a valid `jp2_family_tgt' object.");
    }
}

/*****************************************************************************/
/*                     jp2_output_box::get_box_length                        */
/*****************************************************************************/

kdu_long
  jp2_output_box::get_box_length() const
{
  kdu_long box_len = box_size;
  if (box_len < 0)
    box_len = cur_size;
  if (!headerless)
    { 
      box_len += 8; // Include header
      if (force_long_header)
        box_len += 8; // Include longer header
#ifdef KDU_LONG64
      else if ((!rubber_length) && ((box_len >> 32) > 0))
        box_len += 8; // Also need longer header in this case
#endif
    }
  return box_len;
}

/*****************************************************************************/
/*                      jp2_output_box::get_start_pos                        */
/*****************************************************************************/

kdu_long
  jp2_output_box::get_start_pos() const
{
  if (last_box_type == 0)
    return 0; // Box never opened
  kdu_long pos = this->rel_start_pos;
  jp2_output_box *scan = this->super_box;
  while (scan != NULL)
    { 
      pos += scan->rel_start_pos + scan->get_header_length();
      scan = scan->super_box;
    }
  return pos;
}

/*****************************************************************************/
/*                    jp2_output_box::get_header_length                      */
/*****************************************************************************/

int
  jp2_output_box::get_header_length() const
{
  if (headerless)
    return 0;
  int header_length = 8;
  if (force_long_header)
    header_length = 16;
#ifdef KDU_LONG64
  else if (!rubber_length)
    {
      kdu_long box_len = box_size;
      if (box_len < 0)
        box_len = cur_size;
      if (((box_len+8)>>32) > 0)
        header_length = 16;
    }
#endif
  return header_length;  
}

/*****************************************************************************/
/*                      jp2_output_box::use_long_header                      */
/*****************************************************************************/

int
  jp2_output_box::use_long_header()
{
  if (headerless || reopened)
    return 0;
  if (box_type == 0)
    { KDU_ERROR_DEV(e,0x02111002); e <<
      KDU_TXT("You cannot call `jp2_output_box::use_long_header' "
              "unless the box is open.");
    }
  if (rubber_length)
    { KDU_ERROR_DEV(e,0x03111003); e <<
      KDU_TXT("You cannot call `jp2_output_box::use_long_header' if the "
              "JP2 box in question has already been assigned a rubber "
              "length -- rubber lengths must be written using the short "
              "header format with 8 bytes instead of 16 bytes.");
    }
  force_long_header = true;
  return 16;
}

/*****************************************************************************/
/*                       jp2_output_box::write_header                        */
/*****************************************************************************/

void
  jp2_output_box::write_header()
{
  if (headerless || reopened)
    return;
  assert((box_type != 0) && ((tgt != NULL) || (super_box != NULL)));
  assert(write_immediately);
  assert(restore_size < 0); // We should not be in a rewrite section
  kdu_long save_cur_size = cur_size; cur_size = -16; // Header does not count
  if (rubber_length)
    {
      assert(!(force_long_header || write_header_on_close));
      write((kdu_uint32) 0);
      write(box_type);
    }
  else
    {
      assert(force_long_header || !write_header_on_close);
      assert(box_size >= 0);
      kdu_long header_len = 8;
      kdu_long box_len = box_size + header_len;
#ifdef KDU_LONG64
      if ((box_len>>32) > 0) // Bug corrected in v6.2 -- used to say "> 1"
        { header_len = 16; box_len += 8; }
      else
#endif
      if (force_long_header)
        { header_len = 16; box_len += 8; }
      
      if (header_len == 8)
        {
          write((kdu_uint32) box_len);
          write(box_type);
        }
      else
        {
          write((kdu_uint32) 1);
          write(box_type);
#ifdef KDU_LONG64
          write((kdu_uint32)(box_len>>32));
#else
          write((kdu_uint32) 0);
#endif
          write((kdu_uint32) box_len);
        }
    }
  cur_size = save_cur_size;
}

/*****************************************************************************/
/*                     jp2_output_box::set_rubber_length                     */
/*****************************************************************************/

void
  jp2_output_box::set_rubber_length()
{
  if (headerless || reopened)
    return;
  assert(box_type != 0);
  if ((tgt == NULL) && (super_box == NULL))
    return; // Nothing to do, since no header or contents will ever be written
  if (rubber_length)
    return;
  if (write_immediately)
    { KDU_ERROR_DEV(e,32); e <<
        KDU_TXT("Attempting to set a rubber length for a JP2 "
        "box whose total length has already been declared, or is to be "
        "written at the end.");
    }
  if (force_long_header)
    { KDU_ERROR_DEV(e,0x02111001); e <<
        KDU_TXT("Attempting to set a rubber length for a JP2 box "
                "for which `jp2_output_box::use_long_headers' has been "
                "called -- rubber length boxes must use the short (8 byte) "
                "header style.");
    }
  if (restore_size >= 0)
    { KDU_ERROR_DEV(e,33); e <<
        KDU_TXT("Attempting to set a rubber length for a JP2 "
        "box which is currently inside a rewrite section.");
    }
  if (super_box != NULL)
    super_box->set_rubber_length();
  rubber_length = true;
  write_immediately = true;
  write_header();
  if (buffer != NULL)
    {
      if (super_box != NULL)
        output_failed = !super_box->write(buffer,(int) cur_size);
      else if (tgt->fp != NULL)
        {
          output_failed = (fwrite(buffer,1,(size_t) cur_size,
                                  tgt->fp) != (size_t) cur_size);
          tgt->last_write_pos += cur_size;
        }
      else if (tgt->indirect != NULL)
        {
          output_failed = !tgt->indirect->write(buffer,(int) cur_size);
          tgt->last_write_pos += cur_size;
        }
      else if (tgt->opened_for_simulation)
        tgt->last_write_pos += cur_size;
      else
        assert(0);
      delete[] buffer;
      buffer_size = 0; buffer = NULL;
    }
}

/*****************************************************************************/
/*                       jp2_output_box::set_target_size                     */
/*****************************************************************************/

void
  jp2_output_box::set_target_size(kdu_long num_bytes)
{
  if (headerless || reopened)
    return;
  if (rubber_length)
    { KDU_ERROR_DEV(e,34); e <<
        KDU_TXT("Attempting to set the target size of a JP2 box which "
        "has already been assigned a rubber length.");
    }
  if (write_immediately)
    { KDU_ERROR_DEV(e,35); e <<
        KDU_TXT("Attempting to set the target size of a JP2 box whose "
        "content length is already known, or is to be written at the end.");
    }
  if (restore_size >= 0)
    { KDU_ERROR_DEV(e,36); e <<
        KDU_TXT("Attempting to set the target size of a JP2 "
        "box which is currently inside a rewrite section.");
    }
  assert(box_type != 0);
  if ((tgt == NULL) && (super_box == NULL))
    return; // Nothing to do, since no header or contents will ever be written
  box_size = num_bytes;
  if (cur_size > box_size)
    { KDU_ERROR_DEV(e,37); e <<
        KDU_TXT("Attempting to set the target size of a JP2 box to "
        "which a larger number of bytes has already been written.");
    }
  write_immediately = true;
  write_header();
  if (buffer != NULL)
    {
      if (super_box != NULL)
        output_failed = !super_box->write(buffer,(int) cur_size);
      else if (tgt->fp != NULL)
        {
          output_failed = (fwrite(buffer,1,(size_t) cur_size,
                           tgt->fp) != (size_t) cur_size);
          tgt->last_write_pos += cur_size;
        }
      else if (tgt->indirect != NULL)
        {
          output_failed = !tgt->indirect->write(buffer,(int) cur_size);
          tgt->last_write_pos += cur_size;
        }
      else if (tgt->opened_for_simulation)
        tgt->last_write_pos += cur_size;
      else
        assert(0);
      delete[] buffer;
      buffer_size = 0; buffer = NULL;
    }
}

/*****************************************************************************/
/*                      jp2_output_box::write_header_last                    */
/*****************************************************************************/

void
  jp2_output_box::write_header_last()
{
  if (box_type == 0)
    { KDU_ERROR_DEV(e,38); e <<
        KDU_TXT("You cannot use `jp2_output_box::write_header_last' "
        "unless the box is open.");
    }
  if (headerless)
    return;
  if (write_immediately || write_header_on_close)
    return;
  assert(!reopened); // Otherwise `write_immediately' would be true
  if ((tgt == NULL) && (super_box == NULL))
    return; // Nothing to do, since no header or contents will ever be written
  bool can_seek = false;
  if (tgt != NULL)
    {
      if (tgt->fp != NULL)
        can_seek = true;
      else if (tgt->indirect != NULL)
        {
          can_seek = tgt->indirect->start_rewrite(0);
          tgt->indirect->end_rewrite();
        }
      else if (tgt->opened_for_simulation)
        can_seek = true;
    }
  if (!can_seek)
    { KDU_ERROR_DEV(e,39); e <<
        KDU_TXT("You cannot use `jp2_output_box::write_header_last' "
        "unless this is a top level box and the underlying `jp2_family_tgt' "
        "object represents a file.");
    }
  write_header_on_close = true;
  force_long_header = true; // Forces `write_header' to write 16 bytes.
  set_target_size(KDU_LONG_MAX); // Generates a temporary header.
  assert(write_immediately);
}

/*****************************************************************************/
/*                           jp2_output_box::close                           */
/*****************************************************************************/

bool
  jp2_output_box::close()
{
  if (box_type == 0)
    return true; // Do nothing if box was not open

  if (reopened)
    { 
      if (super_box != NULL)
        super_box->end_rewrite();
      else if (tgt != NULL)
        { 
          assert(saved_tgt_write_pos > 0);
          if (tgt->fp != NULL)
            { 
              fflush(tgt->fp);
              tgt->last_write_pos = saved_tgt_write_pos;
              kdu_fseek(tgt->fp,saved_tgt_write_pos);
            }
          else if ((tgt->indirect != NULL) && tgt->indirect->end_rewrite())
            tgt->last_write_pos = saved_tgt_write_pos;
          else if (tgt->opened_for_simulation)
            tgt->last_write_pos = saved_tgt_write_pos;
          else
            assert(0);
          saved_tgt_write_pos = -1;
        }
      else
        assert(0);
      cur_size = box_size;
      reopened = false;
      box_type = 0;
      return !output_failed;
    }
  
  end_rewrite(); // Just in case, the caller forgot to end a rewrite section
  if ((box_size < 0) || write_header_on_close)
    box_size = cur_size;
  else if (box_size != cur_size)
    { KDU_ERROR_DEV(e,40); e <<
        KDU_TXT("Attempting to close an output JP2 box whose length "
        "was defined ahead of time, having written less bytes than indicated "
        "by that length value.");
    }
  if ((tgt == NULL) && (super_box == NULL))
    { // Don't actually write anything, but delete the buffer
      if (buffer != NULL)
        delete[] buffer;
      buffer_size = 0; buffer = NULL;
      write_header_on_close = write_immediately = false; // Stay on safe side
    }
  else if (!write_immediately)
    {
      write_immediately = true;
      write_header();
      if (buffer != NULL)
        {
          if (super_box != NULL)
            output_failed = !super_box->write(buffer,(int) cur_size);
          else if (tgt->fp != NULL)
            {
              output_failed = (fwrite(buffer,1,(size_t) cur_size,
                                      tgt->fp) != (size_t) cur_size);
              tgt->last_write_pos += cur_size;
            }
          else if (tgt->indirect != NULL)
            {
              output_failed = !tgt->indirect->write(buffer,(int) cur_size);
              tgt->last_write_pos += cur_size;
            }
          else if (tgt->opened_for_simulation)
            tgt->last_write_pos += cur_size;
          else
            assert(0);
          delete[] buffer;
          buffer_size = 0; buffer = NULL;
        }
    }
  else
    assert(buffer == NULL);
  
  if (write_header_on_close)
    { // Go back and rewrite the correct header
      assert(force_long_header);
      if ((tgt != NULL) && (tgt->fp != NULL))
        {
          fflush(tgt->fp);
          kdu_long pos = tgt->last_write_pos;
          tgt->last_write_pos -= box_size+16;
          kdu_fseek(tgt->fp,tgt->last_write_pos);
          write_header();
          kdu_fseek(tgt->fp,pos);
          tgt->last_write_pos = pos;
        }
      else if ((tgt != NULL) && (tgt->indirect != NULL))
        {
          kdu_long pos = tgt->last_write_pos;
          kdu_long backtrack = box_size + 16;
          tgt->last_write_pos -= backtrack;
          if (!tgt->indirect->start_rewrite(backtrack))
            assert(0);
          write_header();
          tgt->indirect->end_rewrite();
          tgt->last_write_pos = pos;
        }
      else if ((tgt == NULL) || !tgt->opened_for_simulation)
        assert(0);
    }

  box_type = 0;
  if (rubber_length && (!headerless) && (tgt != NULL))
    tgt->has_rubber_box = true;
  return !output_failed;
}

/*****************************************************************************/
/*                         jp2_output_box::write_box                         */
/*****************************************************************************/

kdu_long
  jp2_output_box::write_box(jp2_family_tgt *write_tgt,
                            bool force_headerless) const
{
  if ((box_type==0) || write_immediately || (buffer==NULL))
    return 0;
  kdu_long content_bytes = (restore_size > 0)?restore_size:cur_size;
  bool write_failed = false;
  kdu_long header_len = 0;
  
  if (!(headerless || force_headerless))
    { // Write a box header
      header_len = 8;
      kdu_long box_len = content_bytes + header_len;
#ifdef KDU_LONG64
      if ((box_len>>32) > 0)
        { header_len = 16; box_len += 8; }
      else
#endif
      if (force_long_header)
        { header_len = 16; box_len += 8; }
      kdu_byte header_buf[16];
      if (header_len == 8)
        { 
          header_buf[0] = (kdu_byte)(box_len>>24);
          header_buf[1] = (kdu_byte)(box_len>>16);
          header_buf[2] = (kdu_byte)(box_len>>8);
          header_buf[3] = (kdu_byte)(box_len);
        }
      else
        { 
          header_buf[0] = header_buf[1] = header_buf[2] = 0;
          header_buf[3] = 1;
        }
      header_buf[4] = (kdu_byte)(box_type>>24);
      header_buf[5] = (kdu_byte)(box_type>>16);
      header_buf[6] = (kdu_byte)(box_type>>8);        
      header_buf[7] = (kdu_byte)(box_type);
      if (header_len > 8)
        { 
#ifdef KDU_LONG64
          header_buf[8] = (kdu_byte)(box_len>>56);
          header_buf[9] = (kdu_byte)(box_len>>48);
          header_buf[10] = (kdu_byte)(box_len>>40);
          header_buf[11] = (kdu_byte)(box_len>>32);
#else
          header_buf[8] = header_buf[9] = header_buf[10] = header_buf[11] = 0;
#endif
          header_buf[12] = (kdu_byte)(box_len>>24);
          header_buf[13] = (kdu_byte)(box_len>>16);
          header_buf[14] = (kdu_byte)(box_len>>8);
          header_buf[15] = (kdu_byte)(box_len);          
        }
      if (write_tgt->fp != NULL)
        { 
          write_failed = (fwrite(header_buf,1,(size_t) header_len,
                                 write_tgt->fp) != (size_t) header_len);
          write_tgt->last_write_pos += header_len;
        }
      else if (write_tgt->indirect != NULL)
        { 
          write_failed = !write_tgt->indirect->write(header_buf,
                                                     (int) header_len);
          write_tgt->last_write_pos += header_len;
        }
      else if (write_tgt->opened_for_simulation)
        write_tgt->last_write_pos += header_len;
      else
        assert(0);      
    }

  if (!write_failed)
    { 
      if (write_tgt->fp != NULL)
        { 
          write_failed = (fwrite(buffer,1,(size_t) content_bytes,
                                 write_tgt->fp) != (size_t) content_bytes);
          write_tgt->last_write_pos += content_bytes;
        }
      else if (write_tgt->indirect != NULL)
        { 
          write_failed = !write_tgt->indirect->write(buffer,
                                                     (int) content_bytes);
          write_tgt->last_write_pos += content_bytes;
        }
      else if (write_tgt->opened_for_simulation)
        write_tgt->last_write_pos += content_bytes;
      else
        assert(0);
    }
  
  if (write_failed)
    return -1;
  return content_bytes + header_len;
}

/*****************************************************************************/
/*                       jp2_output_box::start_rewrite                       */
/*****************************************************************************/

bool
  jp2_output_box::start_rewrite(kdu_long backtrack)
{
  if ((box_type == 0) || (restore_size >= 0) ||
      (backtrack < 0) || (backtrack > cur_size) || reopened)
    return false;
  restore_size = cur_size;
  cur_size -= backtrack;
  if (write_immediately)
    { // Need to be able to reposition the target
      if (tgt != NULL)
        {
          if (tgt->fp != NULL)
            {
              if (backtrack > 0)
                {
                  fflush(tgt->fp);
                  tgt->last_write_pos -= backtrack;
                  kdu_fseek(tgt->fp,tgt->last_write_pos);
                }
              return true;
            }
          else if ((tgt->indirect != NULL) &&
                   tgt->indirect->start_rewrite(backtrack))
            {
              tgt->last_write_pos -= backtrack;
              return true;
            }
          else if (tgt->opened_for_simulation)
            {
              tgt->last_write_pos -= backtrack;
              return true;
            }
        }
      else if ((super_box != NULL) && super_box->start_rewrite(backtrack))
        return true;
    }
  else
    return true; // Buffered source can always be rewritten

  // If we get here, we have failed to enter rewrite mode
  restore_size = -1;
  cur_size += backtrack;
  return false;
}

/*****************************************************************************/
/*                        jp2_output_box::end_rewrite                        */
/*****************************************************************************/

bool
  jp2_output_box::end_rewrite()
{
  if ((restore_size < 0) || reopened)
    return false;
  kdu_long advance = restore_size - cur_size;   assert(advance >= 0);
  cur_size = restore_size;
  restore_size = -1;
  if (write_immediately)
    { // Need to reposition the target
      if (tgt != NULL)
        {
          if (tgt->fp != NULL)
            {
              if (advance > 0)
                {
                  fflush(tgt->fp);
                  tgt->last_write_pos += advance;
                  kdu_fseek(tgt->fp,tgt->last_write_pos);
                }
            }
          else if ((tgt->indirect != NULL) && tgt->indirect->end_rewrite())
            tgt->last_write_pos += advance;
          else if (tgt->opened_for_simulation)
            tgt->last_write_pos += advance;
          else
            assert(0);
        }
      else if (super_box != NULL)
        {
          if (!super_box->end_rewrite())
            assert(0);
        }
      else
        assert(0);
    }

  return true;
}

/*****************************************************************************/
/*                          jp2_output_box::reopen                           */
/*****************************************************************************/

kdu_long
  jp2_output_box::reopen(kdu_uint32 new_box_type, kdu_long offset)
{
  if ((box_type != 0) || (last_box_type == 0) || (box_size < 0) ||
      (offset > box_size))
    return -1;
  assert(!reopened);
  while ((super_box != NULL) && !super_box->exists())
    { // Our immediate parent is closed; for the purpose of this and all
      // subsequent re-opening operations, we will behave as though we
      // are a direct descendant of the container in which our parent
      // super-box was located.
      this->rel_start_pos +=
        super_box->rel_start_pos + super_box->get_header_length();
      tgt = super_box->tgt;
      super_box = super_box->super_box;
    }
  kdu_long backtrack, pos = this->rel_start_pos;
  int hdr_len = this->get_header_length();
  if ((new_box_type != last_box_type) && !headerless)
    { // Try rewriting the box type
      if (super_box != NULL)
        { 
          if (super_box->reopened)
            return -1;
          backtrack = super_box->cur_size - (pos+4);
          assert(backtrack >= 4);
          if (!super_box->start_rewrite(backtrack))
            return -1;
          super_box->write(new_box_type);
          super_box->end_rewrite();
        }
      else if (tgt != NULL)
        { 
          kdu_byte buf[4] =
            { (kdu_byte)(new_box_type>>24), (kdu_byte)(new_box_type>>16),
              (kdu_byte)(new_box_type>>8), (kdu_byte) new_box_type };
          backtrack = tgt->last_write_pos - (pos+4);
          if (tgt->fp != NULL)
            { 
              fflush(tgt->fp);
              kdu_fseek(tgt->fp,pos+4);
              fwrite(buf,1,4,tgt->fp);
              fflush(tgt->fp);
              kdu_fseek(tgt->fp,tgt->last_write_pos);
            }
          else if ((tgt->indirect != NULL) &&
                   tgt->indirect->start_rewrite(backtrack))
            {
              tgt->indirect->write(buf,4);
              tgt->indirect->end_rewrite();
            }
          else if (!tgt->opened_for_simulation)
            return false;
        }
      else
        return -1;
      this->last_box_type = new_box_type;            
    }            

  // Now try to open the box for rewriting from the offset location
  pos += hdr_len;
  pos += offset;
  if (super_box != NULL)
    { 
      if (super_box->reopened)
        return -1;
      backtrack = super_box->cur_size - pos;
      if (!super_box->start_rewrite(backtrack))
        return -1;
    }
  else if (tgt != NULL)
    { // Re-opened box writes directly to `tgt'
      kdu_long save_pos = tgt->last_write_pos;
      backtrack = tgt->last_write_pos - pos;
      if (tgt->fp != NULL)
        { 
          fflush(tgt->fp);
          tgt->last_write_pos = pos;
          kdu_fseek(tgt->fp,pos);
        }
      else if ((tgt->indirect != NULL) &&
               tgt->indirect->start_rewrite(backtrack))
        tgt->last_write_pos = pos;
      else if (tgt->opened_for_simulation)
        tgt->last_write_pos = pos;
      else
        return -1;
      this->saved_tgt_write_pos = save_pos;
    }
  else
    return -1;

  // Box is now ready to be marked as open
  this->box_type = new_box_type;
  this->reopened = true;
  this->cur_size = offset;
  this->output_failed = false;
  this->write_immediately = true;
  this->write_header_on_close = false;
  
  return box_size - offset;
}

/*****************************************************************************/
/*                   jp2_output_box::write_free_and_close                    */
/*****************************************************************************/

bool
  jp2_output_box::write_free_and_close(kdu_long free_bytes)
{
  if (free_bytes <= 0)
    return close();
  
  if (free_bytes < 8)
    { KDU_ERROR_DEV(e,0x17071202); e <<
      KDU_TXT("When invoking `jp2_output_box::write_free_and_close', the "
              "supplied length for the free box to be written at the end "
              "must be >= 8 bytes, in order to accommodate the header "
              "length.");
    }
  if ((box_size >= 0) && (box_size != (cur_size+free_bytes)))
    { KDU_ERROR_DEV(e,0x17071203); e <<
      KDU_TXT("When invoking `jp2_output_box::write_free_and_close' on a "
              "JP2 output box whose total size is fixed/known, the "
              "`free' sub-box to be written before closure must precisely "
              "span all remaining bytes in the box.");
    }
  if (box_size < 0)
    set_target_size(cur_size+free_bytes);
  assert(box_size == (cur_size+free_bytes));
  int free_hdr_len = 8;
#ifdef KDU_LONG64
  if ((free_bytes >> 32) > 0)
    free_hdr_len = 16;
#endif
  if (free_hdr_len == 8)
    { 
      write((kdu_uint32) free_bytes);
      write(jp2_free_4cc);
    }
  else
    { 
      write((kdu_uint32) 1);
      write(jp2_free_4cc);
#ifdef KDU_LONG64
      write((kdu_uint32)(free_bytes>>32));
#else
      write((kdu_uint32) 0);
#endif
      write((kdu_uint32) free_bytes);
    }
  if ((restore_size < 0) && !reopened)
    { // Write zeroes for the body of the box
      free_bytes -= free_hdr_len;
      kdu_byte zbuf[1024];
      memset(zbuf,0,1024);
      while (free_bytes > 0)
        { 
          int xfer_bytes = 1024;
          if (free_bytes < 1024)
            xfer_bytes = (int) free_bytes;
          free_bytes -= xfer_bytes;
          if (!write(zbuf,xfer_bytes))
            break;
        }
    }
  return close();
}

/*****************************************************************************/
/*                       jp2_output_box::write (buffer)                      */
/*****************************************************************************/

bool
  jp2_output_box::write(const kdu_byte buf[], int num_bytes)
{
  if ((box_type == 0) || output_failed)
    return false;
  int write_bytes = num_bytes;
  if ((restore_size >= 0) && ((cur_size+write_bytes) > restore_size))
    write_bytes = (int)(restore_size-cur_size);
  if (write_bytes <= 0)
    return (write_bytes == num_bytes);
  cur_size += write_bytes;
  if ((box_size >= 0) && (cur_size > box_size))
    { 
      if (!reopened)
        { KDU_ERROR_DEV(e,41); e <<
          KDU_TXT("Attempting to write more bytes to a JP2 output box "
                  "than the number which was specified via a previous call to "
                  "`jp2_output_box::set_target_size'.");
        }
      else
        { KDU_ERROR_DEV(e,0x17071201); e <<
          KDU_TXT("Attempting to write beyond the end of a re-opened JP2 "
                  "output box.");
        }
    }

  if (write_immediately)
    { // Flush data directly to the output.
      if (super_box != NULL)
        output_failed = !super_box->write(buf,write_bytes);
      else if (tgt->fp != NULL)
        {
          output_failed =
            (fwrite(buf,1,(size_t) write_bytes,tgt->fp) !=
             (size_t) write_bytes);
          tgt->last_write_pos += write_bytes;
        }
      else if (tgt->indirect != NULL)
        {
          output_failed = !tgt->indirect->write(buf,write_bytes);
          tgt->last_write_pos += write_bytes;
        }
      else if (tgt->opened_for_simulation)
        tgt->last_write_pos += write_bytes;
      else
        assert(0);
      return (write_bytes == num_bytes) && !output_failed;
    }

  // Need to buffer the data
  if (cur_size > (kdu_long) buffer_size)
    {
      assert(restore_size < 0);
      size_t new_size = buffer_size + ((size_t) cur_size) + 1024;
      if (cur_size > (kdu_long) new_size)
        throw std::bad_alloc(); // Exceeds dimensionable buffer
      kdu_byte *tmp_buf = new kdu_byte[new_size];
      if (buffer != NULL)
        {
          memcpy(tmp_buf,buffer,(size_t)(cur_size-write_bytes));
          delete[] buffer;
        }
      buffer = tmp_buf;
      buffer_size = new_size;
    }
  memcpy(buffer+(size_t)(cur_size-write_bytes),buf,(size_t) write_bytes);
  return (write_bytes == num_bytes);
}

/*****************************************************************************/
/*                        jp2_output_box::write (dword)                      */
/*****************************************************************************/

bool
  jp2_output_box::write(kdu_uint32 dword)
{
  kdu_byte buf[4];
  buf[3] = (kdu_byte) dword; dword >>= 8;
  buf[2] = (kdu_byte) dword; dword >>= 8;
  buf[1] = (kdu_byte) dword; dword >>= 8;
  buf[0] = (kdu_byte) dword;
  return write(buf,4);
}

/*****************************************************************************/
/*                        jp2_output_box::write (word)                       */
/*****************************************************************************/

bool
  jp2_output_box::write(kdu_uint16 word)
{
  kdu_byte buf[2];
  buf[1] = (kdu_byte) word; word >>= 8;
  buf[0] = (kdu_byte) word;
  return write(buf,2);
}


/* ========================================================================= */
/*                                j2_dimensions                              */
/* ========================================================================= */

/*****************************************************************************/
/*                           j2_dimensions::compare                          */
/*****************************************************************************/

bool
  j2_dimensions::compare(j2_dimensions *src)
{
  if ((size != src->size) || (compression_type != src->compression_type) ||
      (num_components != src->num_components) ||
      (colour_space_unknown != src->colour_space_unknown) ||
      (ipr_box_available != src->ipr_box_available))
    return false;
  for (int c=0; c < num_components; c++)
    if (bit_depths[c] != src->bit_depths[c])
      return false;
  return true;
}

/*****************************************************************************/
/*                             j2_dimensions::copy                           */
/*****************************************************************************/

void
  j2_dimensions::copy(j2_dimensions *src)
{
  jp2_dimensions ifc(this);
  ifc.init(src->size,src->num_components,src->colour_space_unknown,
           src->compression_type);
  ipr_box_available = src->ipr_box_available;
  profile = src->profile;
  is_jpxb_compatible = src->is_jpxb_compatible;
  part2_caps = src->part2_caps;
  for (int c=0; c < src->num_components; c++)
    bit_depths[c] = src->bit_depths[c];
}

/*****************************************************************************/
/*                             j2_dimensions::init                           */
/*****************************************************************************/

void
  j2_dimensions::init(jp2_input_box *ihdr)
{
  if (this->num_components != 0)
    { KDU_ERROR_DEV(e,42); e <<
        KDU_TXT("Attempting to read a JP2 image header box (ihdr) into a "
        "`jp2_dimensions' object which has previously been initialized!");
    }
  assert(ihdr->get_box_type() == jp2_image_header_4cc);
  kdu_uint32 height=0, width=0;
  kdu_uint16 nc=0;
  kdu_byte bpc=0, c_type=0, unk=0, ipr=0;
  if (!(ihdr->read(height) && ihdr->read(width) && ihdr->read(nc) &&
        ihdr->read(bpc) && ihdr->read(c_type) &&
        ihdr->read(unk) && ihdr->read(ipr)))
    { KDU_ERROR(e,43);  e <<
        KDU_TXT("Malformed image header box (ihdr) found in JP2-family "
        "data source.  Not all fields were present.");
    }
  if (!ihdr->close() != 0)
    { KDU_ERROR(e,44); e <<
        KDU_TXT("Malformed image header box (ihdr) found in JP2-family "
        "data source.  The box appears to be too long.");
    }

  if ((nc < 1) || (nc > 16384) ||
      (c_type > (kdu_byte) JP2_COMPRESSION_TYPE_JBIG) ||
      (unk != (unk & 1)) || (ipr != (ipr & 1)) ||
      ((bpc != 0xFF) && ((bpc & 0x7F) > 37)))
    { KDU_ERROR(e,45); e <<
        KDU_TXT("Malformed image header box (ihdr) found in JP2-family "
        "data source.  The box contains fields which do not conform to "
        "their legal range.");
    }
  if ((height & 0x80000000) || (width & 0x80000000))
    { KDU_ERROR(e,46); e <<
        KDU_TXT("Sorry: Cannot process JP2-family data sources whose image "
        "header box contains height or width values larger than "
        "2^{31}-1.");
    }
  size.y = (int) height;
  size.x = (int) width;
  num_components = (int) nc;
  colour_space_unknown = (unk != 0);
  ipr_box_available = (ipr != 0);
  compression_type = c_type;
  bit_depths = new int[num_components];
  for (int c=0; c < num_components; c++)
    if (bpc == 0xFF)
      bit_depths[c] = 0;
    else
      bit_depths[c] = (bpc & 0x80)?(-((bpc & 0x7F)+1)):(bpc+1);

  // Set default profile/compatibility information.  If more reliable info is
  // required in the future, it must be supplied via a call to
  // `jp2_dimensions::finalize_compatibility'.  This will only be important
  // if `jp2_dimensions::copy' is to be used to prepare for another
  // `jp2_dimensions' interface for file writing, as discussed in the
  // comments appearing with `jp2_dimensions::finalize_compatibility'.
  profile = 2;
  part2_caps = 0;
  is_jpxb_compatible = true;
}

/*****************************************************************************/
/*                       j2_dimensions::process_bpcc_box                     */
/*****************************************************************************/

void
  j2_dimensions::process_bpcc_box(jp2_input_box *bpcc)
{
  kdu_byte bpc;

  for (int c=0; c < num_components; c++)
    if (!bpcc->read(bpc))
      { KDU_ERROR(e,47);  e <<
          KDU_TXT("Malformed bits per component (bpcc) box found in "
          "JP2-family data source.  The box contains insufficient "
          "bit-depth specifiers.");
      }
    else if ((bpc & 0x7F) > 37)
      { KDU_ERROR(e,48);  e <<
          KDU_TXT("Malformed bits per component (bpcc) box found in "
          "JP2-family data source.  The box contains an illegal "
          "bit-depth specifier.  Bit depths may not exceed 38 bits per "
          "sample.");
      }
    else
      bit_depths[c] = (bpc & 0x80)?(-((bpc & 0x7F)+1)):(bpc+1);
  if (!bpcc->close() != 0)
    { KDU_ERROR(e,49);  e <<
        KDU_TXT("Malformed bits per component (bpcc) box found in JP2-family "
        "data source.  The box appears to be too long.");
    }
}

/*****************************************************************************/
/*                           j2_dimensions::finalize                         */
/*****************************************************************************/

void
  j2_dimensions::finalize()
{
  int c;
  for (c=0; c < num_components; c++)
    if ((bit_depths[c] == 0) || (bit_depths[c] > 38) || (bit_depths[c] < -38))
      break;
  if ((num_components < 1) || (c < num_components) ||
      (num_components > 16384))
    { KDU_ERROR_DEV(e,50); e <<
        KDU_TXT("Incomplete or invalid dimensional information "
        "provided when initializing a `jp2_dimensions' object.");
    }
  if ((compression_type < 0) ||
      (compression_type > JP2_COMPRESSION_TYPE_JBIG))
    { KDU_ERROR_DEV(e,51); e <<
        KDU_TXT("Invalid compression type value provided when "
        "initializing a `jp2_dimensions' object.");
    }
}

/*****************************************************************************/
/*                          j2_dimensions::save_boxes                        */
/*****************************************************************************/

void
  j2_dimensions::save_boxes(jp2_output_box *super_box)
{
  finalize();
  int c;
  kdu_byte bpc = 0;
  for (c=1; c < num_components; c++)
    if (bit_depths[c] != bit_depths[0])
      bpc = 0xFF;
  if (bpc == 0)
    bpc = (kdu_byte)
      ((bit_depths[0]>0)?(bit_depths[0]-1):(0x80 | (-bit_depths[0]-1)));

  jp2_output_box ihdr;
  ihdr.open(super_box,jp2_image_header_4cc);
  ihdr.write((kdu_uint32) size.y);
  ihdr.write((kdu_uint32) size.x);
  ihdr.write((kdu_uint16) num_components);
  ihdr.write(bpc);
  ihdr.write((kdu_byte) compression_type);
  ihdr.write((kdu_byte)((colour_space_unknown)?1:0));
  ihdr.write((kdu_byte)((ipr_box_available)?1:0));
  ihdr.close();
  if (bpc != 0xFF)
    return;

  jp2_output_box bpcc;
  bpcc.open(super_box,jp2_bits_per_component_4cc);
  for (c=0; c < num_components; c++)
    {
      bpc = (kdu_byte)
        ((bit_depths[c]>0)?(bit_depths[c]-1):(0x80 | (-bit_depths[c]-1)));
      bpcc.write(bpc);
    }
  bpcc.close();
}

/* ========================================================================= */
/*                               jp2_dimensions                              */
/* ========================================================================= */

/*****************************************************************************/
/*                            jp2_dimensions::copy                           */
/*****************************************************************************/

void
  jp2_dimensions::copy(jp2_dimensions src)
{
  if ((state == NULL) || (src.state == NULL))
    { 
      assert((state != NULL) && (src.state != NULL)); // Catch in debug mode
      return;
    }
  state->copy(src.state);
}

/*****************************************************************************/
/*                            jp2_dimensions::init                           */
/*****************************************************************************/

void
  jp2_dimensions::init(kdu_coords size, int num_components, bool unknown_space,
                       int compression_type)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  if (state->num_components != 0)
    { KDU_ERROR_DEV(e,52); e <<
        KDU_TXT("JP2 dimensions may be initialized only once!"); }
  assert(num_components > 0);
  state->size = size;
  state->num_components = num_components;
  state->colour_space_unknown = unknown_space;
  state->ipr_box_available = false;
  state->compression_type = compression_type;
  state->profile = 2;
  state->part2_caps = 0;
  state->is_jpxb_compatible = true;
  state->bit_depths = new int[num_components];
  for (int c=0; c < num_components; c++)
    state->bit_depths[c] = 0; // Uninitialized state
}

/*****************************************************************************/
/*                          jp2_dimensions::init (siz)                       */
/*****************************************************************************/

void
  jp2_dimensions::init(siz_params *siz, bool unknown_space)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  kdu_coords size, origin;
  int num_components=0;

  if (!(siz->get(Ssize,0,0,size.y) && siz->get(Ssize,0,1,size.x) &&
        siz->get(Sorigin,0,0,origin.y) && siz->get(Sorigin,0,1,origin.x) &&
        siz->get(Ncomponents,0,0,num_components)))
    { KDU_ERROR_DEV(e,53); e <<
        KDU_TXT("Attempting to initialize a `jp2_dimensions' object "
        "using an incomplete `siz_params' object.");
    }
  size -= origin;
  init(size,num_components,unknown_space,JP2_COMPRESSION_TYPE_JPEG2000);
  for (int c=0; c < num_components; c++)
    {
      bool is_signed=false;
      int bit_depth=0;

      if (!(siz->get(Nsigned,c,0,is_signed) &&
            siz->get(Nprecision,c,0,bit_depth)))
        { KDU_ERROR_DEV(e,54); e <<
            KDU_TXT("Attempting to initialize a `jp2_dimensions' "
            "object using an incomplete `siz_params' object.");
        }
      set_precision(c,bit_depth,is_signed);
    }
  state->profile = 2;
  state->part2_caps = 0;
  state->is_jpxb_compatible = true;
  finalize_compatibility(siz); // Give it a go, in case finalized parameter
                               // information is already available via `siz'
}

/*****************************************************************************/
/*                   jp2_dimensions::finalize_compatibility                  */
/*****************************************************************************/

void
  jp2_dimensions::finalize_compatibility(kdu_params *root)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  if ((state->compression_type != JP2_COMPRESSION_TYPE_JPEG2000) ||
      (root == NULL))
    return; // Nothing to do
  kdu_params *siz = root->access_cluster(SIZ_params);
  if (siz == NULL)
    return;
  siz->get(Sprofile,0,0,state->profile);
  if (state->profile != Sprofile_PART2)
    return;
  int extensions = 0;
  siz->get(Sextensions,0,0,extensions);
  bool have_caps = false;
  siz->get(Scap,0,0,have_caps);
  if (extensions & ~(Sextensions_MCT | Sextensions_NLT))
    state->is_jpxb_compatible = false;
  if (!siz->get(SCpart2_caps,0,0,state->part2_caps))
    state->part2_caps = 0;
  if (have_caps || (state->part2_caps & SCpart2_caps_EXTENDED_COD))
    state->is_jpxb_compatible = false;
  if ((extensions & Sextensions_MCT) && state->is_jpxb_compatible)
    { // We need to check the particularly restrictive MCT conditions
      // associated with JPX Baseline compatibility, as documented in
      // Annex M.9.2.3 of IS15444-2.
      int tiles_wide=1, tiles_high=1;
      siz->get(Stiles,0,0,tiles_high);  siz->get(Stiles,0,1,tiles_wide);
      int t, num_tiles = tiles_high*tiles_wide;
      kdu_params *mco = root->access_cluster(MCO_params);
      kdu_params *mcc = root->access_cluster(MCC_params);
      for (t=-1; t < num_tiles; t++)
        {
          int num_stages;
          kdu_params *tile_mco = NULL;
          if ((mco != NULL) &&
              ((tile_mco = mco->access_relation(t,-1,0)) != NULL) &&
              tile_mco->get(Mnum_stages,0,0,num_stages) &&
              (num_stages != 1))
            { // Must have exactly one transform stage
              state->is_jpxb_compatible = false;
              break;
            }
          kdu_params *tile_mcc = NULL;
          if (mcc != NULL)
            tile_mcc = mcc->access_relation(t,-1,0);
          for (; tile_mcc != NULL; tile_mcc=tile_mcc->access_next_inst())
            {
              int xform_type;
              if (!tile_mcc->get(Mstage_xforms,0,0,xform_type))
                continue; // This object contains no information
              if (xform_type != Mxform_MAT)
                break; // Dependency or DWT transform not acceptable for JPXB
              if (tile_mcc->get(Mstage_xforms,1,0,xform_type))
                break; // More than one transform block (component collection)
              int is_reversible = 1;
              tile_mcc->get(Mstage_xforms,0,3,is_reversible);
              if (is_reversible)
                break; // Need irreversible decorrelation transform for JPXB
            }
          if (tile_mcc != NULL)
            {
              state->is_jpxb_compatible = false;
              break;
            }
        }
    }
}

/*****************************************************************************/
/*                   jp2_dimensions::finalize_compatibility                  */
/*****************************************************************************/

void
  jp2_dimensions::finalize_compatibility(int profile, bool is_jpx_baseline)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  state->profile = profile;
  state->is_jpxb_compatible = (profile != Sprofile_PART2) || is_jpx_baseline;  
}

/*****************************************************************************/
/*                   jp2_dimensions::finalize_compatibility                  */
/*****************************************************************************/

void
  jp2_dimensions::finalize_compatibility(jp2_dimensions src)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  state->is_jpxb_compatible = src.state->is_jpxb_compatible;
  state->profile = src.state->profile;
}

/*****************************************************************************/
/*                        jp2_dimensions::set_precision                      */
/*****************************************************************************/

bool
  jp2_dimensions::set_precision(int component_idx, int bit_depth,
                                bool is_signed)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((component_idx < 0) || (component_idx >= state->num_components))
    return false;
  state->bit_depths[component_idx] = (is_signed)?-bit_depth:bit_depth;
  return true;
}

/*****************************************************************************/
/*                    jp2_dimensions::set_ipr_box_available                  */
/*****************************************************************************/

void
  jp2_dimensions::set_ipr_box_available()
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  state->ipr_box_available = true;
}

/*****************************************************************************/
/*                          jp2_dimensions::get_size                         */
/*****************************************************************************/

kdu_coords
  jp2_dimensions::get_size() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return kdu_coords();
    }
  return state->size;
}

/*****************************************************************************/
/*                     jp2_dimensions::get_num_components                    */
/*****************************************************************************/

int
  jp2_dimensions::get_num_components() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  return state->num_components;
}

/*****************************************************************************/
/*                     jp2_dimensions::colour_space_known                    */
/*****************************************************************************/

bool
  jp2_dimensions::colour_space_known() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  return state->colour_space_unknown;
}

/*****************************************************************************/
/*                        jp2_dimensions::get_bit_depth                      */
/*****************************************************************************/

int
  jp2_dimensions::get_bit_depth(int component_idx) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  if ((component_idx < 0) || (component_idx >= state->num_components))
    return 0;
  int depth = state->bit_depths[component_idx];
  return (depth < 0)?-depth:depth;
}

/*****************************************************************************/
/*                         jp2_dimensions::get_signed                        */
/*****************************************************************************/

bool
  jp2_dimensions::get_signed(int component_idx) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((component_idx < 0) || (component_idx >= state->num_components))
    return false;
  return (state->bit_depths[component_idx] < 0);
}

/*****************************************************************************/
/*                    jp2_dimensions::get_compression_type                   */
/*****************************************************************************/

int
  jp2_dimensions::get_compression_type() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  return state->compression_type;
}

/*****************************************************************************/
/*                    jp2_dimensions::is_ipr_box_available                   */
/*****************************************************************************/

bool
  jp2_dimensions::is_ipr_box_available() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  return state->ipr_box_available;
}


/* ========================================================================= */
/*                                j2_palette                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                            j2_palette::compare                            */
/*****************************************************************************/

bool
  j2_palette::compare(j2_palette *src)
{
  if ((num_components != src->num_components) ||
      (num_entries != src->num_entries))
    return false;
  for (int c=0; c < num_components; c++)
    {
      if (bit_depths[c] != src->bit_depths[c])
        return false;
      if (memcmp(luts[c],src->luts[c],(size_t) num_entries) != 0)
        return false;
    }
  return true;
}

/*****************************************************************************/
/*                             j2_palette::copy                              */
/*****************************************************************************/

void
  j2_palette::copy(j2_palette *src)
{
  if ((bit_depths != NULL) || (luts != NULL))
    { KDU_ERROR_DEV(e,55); e <<
        KDU_TXT("Trying to copy a `jp2_palette' object to another "
        "object which has already been initialized.  Reinitialization is not "
        "permitted.");
    }
  initialized = src->initialized;
  num_components = src->num_components;
  num_entries = src->num_entries;
  bit_depths = new int[num_components];
  luts = new kdu_int32 *[num_components];
  memset(luts,0,sizeof(kdu_int32 *)*(size_t)num_components);
  for (int c=0; c < num_components; c++)
    {
      bit_depths[c] = src->bit_depths[c];
      luts[c] = new kdu_int32[num_entries];
      memcpy(luts[c],src->luts[c],sizeof(kdu_int32)*((size_t) num_entries));
    }
}

/*****************************************************************************/
/*                             j2_palette::init                              */
/*****************************************************************************/

void
  j2_palette::init(jp2_input_box *pclr)
{
  if (this->num_components != 0)
    { KDU_ERROR_DEV(e,56); e <<
        KDU_TXT("Attempting to read a JP2 palette box (pclr) into a "
        "`jp2_palette' object which has already been initialized.");
    }
  initialized = true;
  assert(pclr->get_box_type() == jp2_palette_4cc);
  kdu_uint16 ne=0;
  kdu_byte npc=0;
  if (!(pclr->read(ne) && pclr->read(npc) &&
        (ne >= 1) && (ne <= 1024) && (npc >= 1)))
    {
    
      KDU_ERROR(e,57); e <<
        KDU_TXT("Malformed palette (pclr) box found in JP2-family data "
        "source.  Insufficient or illegal fields encountered.");
    }
  num_components = npc;
  num_entries = ne;

  int c;
  bit_depths = new int[num_components];
  for (c=0; c < num_components; c++)
    {
      kdu_byte bpc;

      if (!pclr->read(bpc))
        { KDU_ERROR(e,58);  e <<
            KDU_TXT("Malformed palette (pclr) box found in JP2-family data "
            "source.  The box contains insufficient bit-depth specifiers.");
        }
      else if ((bpc & 0x7F) > 37)
        { KDU_ERROR(e,59); e <<
            KDU_TXT("Malformed palette (pclr) box found in JP2-family data. "
            "source.  The box contains an illegal bit-depth specifier.  "
            "Bit depths may not exceed 38 bits per sample.");
        }
      else
        bit_depths[c] = (bpc & 0x80)?(-((bpc & 0x7F)+1)):(bpc+1);
    }

  luts = new kdu_int32 *[num_components];
  memset (luts,0,sizeof(kdu_int32 *)*(size_t)num_components);
  for (c=0; c < num_components; c++)
    luts[c] = new kdu_int32[num_entries];
  for (int n=0; n < num_entries; n++)
    {
      kdu_byte val_buf[5];
      for (c=0; c < num_components; c++)
        {
          int bits = (bit_depths[c] < 0)?(-bit_depths[c]):bit_depths[c];
          int entry_bytes = (bits+7)>>3;
          assert((entry_bytes <= 5) && (entry_bytes > 0));
          int downshift = bits-32;
          downshift = (downshift < 0)?0:downshift;
          int upshift = 32+downshift-bits;
          kdu_int32 offset = (bit_depths[c]<0)?0:KDU_INT32_MIN;

          if (pclr->read(val_buf,entry_bytes) != entry_bytes)
            { KDU_ERROR(e,60);  e <<
                KDU_TXT("Malformed palette (pclr) box found in JP2-family "
                "data source.  The box contains insufficient palette "
                "entries.");
            }
          kdu_int32 val = val_buf[0];
          if (entry_bytes > 1)
            {
              val = (val<<8) + val_buf[1];
              if (entry_bytes > 2)
                {
                  val = (val<<8) + val_buf[2];
                  if (entry_bytes > 3)
                    {
                      val = (val<<8) + val_buf[3];
                      if (entry_bytes > 4)
                        { 
                          val <<= (8-downshift);
                          val += (val_buf[4] >> downshift);
                        }
                    }
                }
            }
          val <<= upshift;
          val += offset;
          (luts[c])[n] = val;
        }
    }

  // Adjust the bit-depths fields of components which indicated more than 32
  // bits per palette entry, to reflect the fact that we have been forced to
  // discard some LSB's.
  for (c=0; c < num_components; c++)
    if (bit_depths[c] > 32)
      bit_depths[c] = 32;
    else if (bit_depths[c] < -32)
      bit_depths[c] = -32;

  if (!pclr->close())
    { KDU_ERROR(e,61); e <<
        KDU_TXT("Malformed palette (pclr) box encountered in JP2-family "
        "data source.  Box appears to be too long.");
    }
}

/*****************************************************************************/
/*                             j2_palette::finalize                          */
/*****************************************************************************/

void
  j2_palette::finalize()
{
  if (num_components == 0)
    return;
  int c;
  for (c=0; c < num_components; c++)
    if ((bit_depths[c] == 0) || (bit_depths[c] > 32) || (bit_depths[c] < -32))
      break;
  if ((num_components < 1) || (c < num_components) ||
      (num_components > 255) || (num_entries < 1) || (num_entries > 1024))
    { KDU_ERROR_DEV(e,62); e <<
        KDU_TXT("Incomplete or invalid information "
        "provided when initializing a `jp2_palette' object.");
    }
}

/*****************************************************************************/
/*                            j2_palette::save_box                           */
/*****************************************************************************/

void
  j2_palette::save_box(jp2_output_box *super_box)
{
  if (num_components == 0)
    return;
  finalize();
  jp2_output_box pclr;
  pclr.open(super_box,jp2_palette_4cc);
  pclr.write((kdu_uint16) num_entries);
  pclr.write((kdu_byte) num_components);

  int c;
  kdu_byte bpc;
  for (c=0; c < num_components; c++)
    {
      bpc = (kdu_byte)
        ((bit_depths[c]>0)?(bit_depths[c]-1):(0x80 | (-bit_depths[c]-1)));
      pclr.write(bpc);
    }

  for (int n=0; n < num_entries; n++)
    {
      kdu_byte val_buf[4];
      for (c=0; c < num_components; c++)
        {
          int bits = (bit_depths[c] < 0)?(-bit_depths[c]):bit_depths[c];
          int entry_bytes = (bits+7)>>3;
          assert((entry_bytes > 0) && (entry_bytes <= 4));
          int downshift = 32 - bits; assert(downshift >= 0);
          kdu_int32 offset = (bit_depths[c]<0)?0:KDU_INT32_MIN;

          kdu_uint32 val = (kdu_uint32)((luts[c])[n] - offset);
          val >>= downshift; // Most significant bits hold 0's
          val_buf[entry_bytes-1] = (kdu_byte) val;
          if (entry_bytes > 1)
            {
              val >>= 8; val_buf[entry_bytes-2] = (kdu_byte) val;
              if (entry_bytes > 2)
                {
                  val >>= 8; val_buf[entry_bytes-3] = (kdu_byte) val;
                  if (entry_bytes > 3)
                    {
                      val >>= 8; val_buf[entry_bytes-4] = (kdu_byte) val;
                    }
                }
            }
          pclr.write(val_buf,entry_bytes);
        }
    }
  pclr.close();
}

/* ========================================================================= */
/*                                  jp2_palette                              */
/* ========================================================================= */

/*****************************************************************************/
/*                              jp2_palette::copy                            */
/*****************************************************************************/

void
  jp2_palette::copy(jp2_palette src)
{
  if ((state == NULL) || (src.state == NULL))
    { 
      assert((state != NULL) && (src.state != NULL));
      return;
    }
  state->copy(src.state);
}

/*****************************************************************************/
/*                               jp2_palette::init                           */
/*****************************************************************************/

void
  jp2_palette::init(int num_components, int num_entries)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return;
    }
  if (state->num_components != 0)
    { KDU_ERROR_DEV(e,63);  e <<
        KDU_TXT("A `jp2_palette' object may be initialized only once!");
    }
  assert((num_components > 0) && (num_components < 256));
  state->initialized = true;
  state->num_components = num_components;
  state->num_entries = num_entries;
  state->bit_depths = new int[num_components];
  state->luts = new kdu_int32 *[num_components];
  memset(state->luts,0,sizeof(kdu_int32 *)*(size_t)num_components);
  for (int c=0; c < num_components; c++)
    {
      state->bit_depths[c] = 0;
      state->luts[c] = new kdu_int32[num_entries];
    }
}

/*****************************************************************************/
/*                              jp2_palette::set_lut                         */
/*****************************************************************************/

bool
  jp2_palette::set_lut(int comp_idx, kdu_int32 *lut, int bit_depth,
                       bool is_signed)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components) ||
      (bit_depth > 32) || (bit_depth < 1))
    return false;
  state->bit_depths[comp_idx] = (is_signed)?(-bit_depth):bit_depth;
  int upshift = 32-bit_depth;
  kdu_int32 offset = (is_signed)?0:KDU_INT32_MIN;
  kdu_int32 *dst = state->luts[comp_idx];
  for (int n=0; n < state->num_entries; n++)
    dst[n] = (lut[n] << upshift) + offset;
  return true;
}

/*****************************************************************************/
/*                         jp2_palette::get_num_entries                      */
/*****************************************************************************/

int
  jp2_palette::get_num_entries() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  return state->num_entries;
}

/*****************************************************************************/
/*                          jp2_palette::get_num_luts                        */
/*****************************************************************************/

int
  jp2_palette::get_num_luts() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  return state->num_components;
}

/*****************************************************************************/
/*                          jp2_palette::get_bit_depth                       */
/*****************************************************************************/

int
  jp2_palette::get_bit_depth(int comp_idx) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return 0;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components))
    return 0;
  int depth = state->bit_depths[comp_idx];
  return (depth<0)?-depth:depth;
}

/*****************************************************************************/
/*                           jp2_palette::get_signed                         */
/*****************************************************************************/

bool
  jp2_palette::get_signed(int comp_idx) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components))
    return false;
  return (state->bit_depths[comp_idx] < 0);
}

/*****************************************************************************/
/*                        jp2_palette::get_lut (float)                       */
/*****************************************************************************/

bool
  jp2_palette::get_lut(int comp_idx, float lut[],
                       int data_format, int format_param) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components) || (lut == NULL))
    return false;
  kdu_int32 *src = state->luts[comp_idx];
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    { 
      float scale = 1.0F / (((float)(1<<16)) * ((float)(1<<16)));
      for (int n=0; n < state->num_entries; n++)
        lut[n] = ((float) src[n]) * scale;
    }
  else if (data_format == JP2_CHANNEL_FORMAT_FIXPOINT)
    { 
      int int_bits = format_param;
      float scale = kdu_pwrof2f(int_bits - 32);
      float offset = kdu_pwrof2f(int_bits-1) - 0.5f;
      for (int n=0; n < state->num_entries; n++)
        lut[n] = ((float) src[n]) * scale + offset;
    }
  else if (data_format == JP2_CHANNEL_FORMAT_FLOAT)
    { 
      union {
        kdu_int32 int_val;
        float float_val;
      } cast;
      int precision = state->bit_depths[comp_idx];
      bool is_signed = false;
      if (precision < 0)
        { 
          precision = -precision;
          is_signed = true;
        }
      if (precision > 32)
        precision = 32;
      else if (precision < 2)
        precision = 2;
      int exponent_bits = format_param;
      if (exponent_bits > (precision-1))
        exponent_bits = precision-1;
      int mantissa_bits = precision - 1 - exponent_bits;
      int exp_off = (1<<(exponent_bits-1)) - 1;
      int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
      int mantissa_downshift = -mantissa_upshift; // May have to downshift
      kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
      kdu_int32 exp_max = 254 + exp_adjust;
      float denorm_scale = 1.0f;
      if (exp_adjust < 0)
        { // Implement as multiplication of the converted floats to correctly
          // bring denormals out of their denormalized state.
          denorm_scale = kdu_pwrof2f(-exp_adjust);
          exp_adjust = 0;
          exp_max = 2*exp_off; // This is the smaller exponent bound here
        }
      int pre_downshift = 32 - precision; // Shift back to original integers
      kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
      kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
      if (!is_signed)
        { // Converting unsigned integer bit-patterns to unsigned floats
          kdu_int32 in_off = 1<<(precision-1);
          kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
          kdu_int32 in_max = mag_max - in_off; // integer level offset in place
          float out_scale = denorm_scale; // Compensate for `exp_off' < 127
          for (int n=0; n < state->num_entries; n++)
            { 
              kdu_int32 val = src[n] >> pre_downshift;
              if (val < in_min)
                val = in_min; // Avoid exponent underflow and negative values
              else if (val > in_max)
                val = in_max; // Avoid exponent overflow
              val += in_off; // Remove the integer level offset
              val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
              if (mantissa_upshift >= 0)
                val <<= mantissa_upshift;
              else
                val >>= mantissa_downshift;
              cast.int_val = val;
              float fval = cast.float_val;
              fval *= out_scale;
              lut[n] = fval - 0.5f; // Final level adjustment (see API docs)
            }
        }
      else
        { // Converting signed integer bit-patterns to signed floats
          kdu_int32 mag_mask = ~(((kdu_int32)-1) << (precision-1));
          float out_scale = denorm_scale; // Compensates for `exp_off' < 127
          out_scale *= 0.5f; // KDU scales signed values by 0.5 (see API docs)
          for (int n=0; n < state->num_entries; n++)
            { 
              kdu_int32 val = src[n] >> pre_downshift;
              kdu_int32 sign_bit = val & KDU_INT32_MIN;
              val &= mag_mask; // Keep just the magnitudes
              if (val < pre_adjust)
                val = pre_adjust; // Avoid exponent underflow
              else if (val > mag_max)
                val = mag_max; // Avoid exponent overflow
              val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
              if (mantissa_upshift >= 0)
                val <<= mantissa_upshift;
              else
                val >>= mantissa_downshift;
              val |= sign_bit; // Now we have true IEEE single precision floats
              cast.int_val = val;
              float fval = cast.float_val;
              fval *= out_scale;
              lut[n] = fval;
            }
        }
    }
  else
    { KDU_ERROR_DEV(e,0x02021601); e <<
      KDU_TXT("Invalid or unsupported `data_format' passed to "
              "`jp2_palette::get_lut'.");
    }
  
  return true;
}

/*****************************************************************************/
/*                      jp2_palette::get_lut (fixed point)                   */
/*****************************************************************************/

bool
  jp2_palette::get_lut(int comp_idx, kdu_sample16 lut[],
                       int data_format, int format_param) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components) || (lut == NULL))
    return false;
  kdu_int32 *src = state->luts[comp_idx];
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    { 
      kdu_int32 downshift = 32 - KDU_FIX_POINT;
      kdu_int32 offset = (1<<downshift)>>1;
      for (int n=0; n < state->num_entries; n++)
        lut[n].ival = (kdu_int16)((src[n]+offset)>>downshift);
    }
  else if (data_format == JP2_CHANNEL_FORMAT_FIXPOINT)
    { 
      int int_bits = format_param;
      float scale = kdu_pwrof2f(int_bits - 32);
      float offset = kdu_pwrof2f(int_bits-1) - 0.5f;
      scale *= (float)(1<<KDU_FIX_POINT);
      offset *= (float)(1<KDU_FIX_POINT);
      offset += 0.5f; // Rounding adjustment
      float max_fval=(float) KDU_INT16_MAX;
      float min_fval=(float) KDU_INT16_MIN;
      for (int n=0; n < state->num_entries; n++)
        { 
          float fval = ((float) src[n]) * scale + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          lut[n].ival = (kdu_int16) floorf(fval);
        }
    }
  else if (data_format == JP2_CHANNEL_FORMAT_FLOAT)
    { 
      union {
        kdu_int32 int_val;
        float float_val;
      } cast;
      int precision = state->bit_depths[comp_idx];
      bool is_signed = false;
      if (precision < 0)
        { 
          precision = -precision;
          is_signed = true;
        }
      if (precision > 32)
        precision = 32;
      else if (precision < 2)
        precision = 2;
      int exponent_bits = format_param;
      if (exponent_bits > (precision-1))
        exponent_bits = precision-1;
      int mantissa_bits = precision - 1 - exponent_bits;
      int exp_off = (1<<(exponent_bits-1)) - 1;
      int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
      int mantissa_downshift = -mantissa_upshift; // May have to downshift
      kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
      kdu_int32 exp_max = 254 + exp_adjust;
      float denorm_scale = 1.0f;
      if (exp_adjust < 0)
        { // Implement as multiplication of the converted floats to correctly
          // bring denormals out of their denormalized state.
          denorm_scale = kdu_pwrof2f(-exp_adjust);
          exp_adjust = 0;
          exp_max = 2*exp_off; // This is the smaller exponent bound here
        }
      int pre_downshift = 32 - precision; // Shift back to original integers
      kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
      kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
      float max_fval=(float) KDU_INT16_MAX;
      float min_fval=(float) KDU_INT16_MIN;
      if (!is_signed)
        { // Converting unsigned integer bit-patterns to unsigned floats
          kdu_int32 in_off = 1<<(precision-1);
          kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
          kdu_int32 in_max = mag_max - in_off; // integer level offset in place
          float out_scale = denorm_scale; // Compensates for `exp_off' < 127
          out_scale *= (float)(1<<KDU_FIX_POINT); // Add conversion to fix16
          float out_off = 0.5f-(float)(1<<(KDU_FIX_POINT-1)); // Rnd + lev shft
          for (int n=0; n < state->num_entries; n++)
            { 
              kdu_int32 val = src[n] >> pre_downshift;
              if (val < in_min)
                val = in_min; // Avoid exponent underflow and negative values
              else if (val > in_max)
                val = in_max; // Avoid exponent overflow
              val += in_off; // Remove the integer level offset
              val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
              if (mantissa_upshift >= 0)
                val <<= mantissa_upshift;
              else
                val >>= mantissa_downshift;
              cast.int_val = val;
              float fval = cast.float_val;
              fval = (fval * out_scale) + out_off;
              fval = kdu_fminf(fval,max_fval);
              fval = kdu_fmaxf(fval,min_fval);
              lut[n].ival = (kdu_int16) floorf(fval);
            }
        }
      else
        { // Converting signed integer bit-patterns to signed floats
          kdu_int32 mag_mask = ~(((kdu_int32)-1) << (precision-1));
          float out_scale = denorm_scale; // Compensates for `exp_off' < 127
          out_scale *= 0.5f; // KDU scales signed values by 0.5 (see API docs)
          for (int n=0; n < state->num_entries; n++)
            { 
              kdu_int32 val = src[n] >> pre_downshift;
              kdu_int32 sign_bit = val & KDU_INT32_MIN;
              val &= mag_mask; // Keep just the magnitudes
              if (val < pre_adjust)
                val = pre_adjust; // Avoid exponent underflow
              else if (val > mag_max)
                val = mag_max; // Avoid exponent overflow
              val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
              if (mantissa_upshift >= 0)
                val <<= mantissa_upshift;
              else
                val >>= mantissa_downshift;
              val |= sign_bit; // Now we have true IEEE single precision floats
              cast.int_val = val;
              float fval = cast.float_val;
              fval = fval * out_scale + 0.5f;
              fval = kdu_fminf(fval,max_fval);
              fval = kdu_fmaxf(fval,min_fval);
              lut[n].ival = (kdu_int16) floorf(fval);
            }
        }  
    }
  else
    { KDU_ERROR_DEV(e,0x02021602); e <<
      KDU_TXT("Invalid or unsupported `data_format' passed to "
              "`jp2_palette::get_lut'.");
    }
  return true;
}

/*****************************************************************************/
/*                     jp2_palette::get_abs_lut (32-bit)                     */
/*****************************************************************************/

bool
  jp2_palette::get_abs_lut(int comp_idx, kdu_sample32 lut[]) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components) || (lut == NULL))
    return false;
  kdu_int32 *src = state->luts[comp_idx];
  int downshift = 32 - state->bit_depths[comp_idx];
  if (downshift < 0)
    downshift = 0;
  for (int n=0; n < state->num_entries; n++)
    lut[n].ival = src[n] >> downshift;
  return true;
}

/*****************************************************************************/
/*                     jp2_palette::get_abs_lut (16-bit)                     */
/*****************************************************************************/

bool
  jp2_palette::get_abs_lut(int comp_idx, kdu_sample16 lut[]) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Catch such problems at least in debug mode
      return false;
    }
  if ((comp_idx < 0) || (comp_idx >= state->num_components) || (lut == NULL))
    return false;
  kdu_int32 *src = state->luts[comp_idx];
  int downshift = 32 - state->bit_depths[comp_idx];
  if (downshift < 16)
    downshift = 16;
  for (int n=0; n < state->num_entries; n++)
    lut[n].ival = (kdu_int16)(src[n] >> downshift);
  return true;
}


/* ========================================================================= */
/*                              j2_component_map                             */
/* ========================================================================= */

/*****************************************************************************/
/*                         j2_component_map::compare                         */
/*****************************************************************************/

bool
  j2_component_map::compare(j2_component_map *src)
{
  if (num_cmap_channels != src->num_cmap_channels)
    return false;
  for (int c=0; c < num_cmap_channels; c++)
    {
      j2_cmap_channel *cp = cmap_channels+c;
      j2_cmap_channel *scp = src->cmap_channels+c;
      if ((cp->component_idx != scp->component_idx) ||
          (cp->lut_idx != scp->lut_idx))
        return false;
    }
  return true;
}

/*****************************************************************************/
/*                          j2_component_map::copy                           */
/*****************************************************************************/

void
  j2_component_map::copy(j2_component_map *src)
{
  if (dimensions.exists() || palette.exists() || (cmap_channels != NULL))
    { KDU_ERROR_DEV(e,64); e <<
        KDU_TXT("Trying to copy an internal `j2_component_map' object "
        "to another object which has already been initialized.  This is an "
        "internal fault within the file format reading/writing logic.");
    }
  use_cmap_box = src->use_cmap_box;
  max_cmap_channels = num_cmap_channels = src->num_cmap_channels;
  cmap_channels = new j2_cmap_channel[max_cmap_channels];
  for (int n=0; n < num_cmap_channels; n++)
    cmap_channels[n] = src->cmap_channels[n];
}

/*****************************************************************************/
/*                          j2_component_map::init                           */
/*****************************************************************************/

void
  j2_component_map::init(jp2_input_box *cmap)
{
  assert(cmap->get_box_type() == jp2_component_mapping_4cc);
  use_cmap_box = true;
  if ((cmap_channels != NULL) || num_cmap_channels)
    { KDU_ERROR_DEV(e,65); e <<
        KDU_TXT("Attempting to initialize a `j2_component_map' object "
        "multiple times.  Problem encountered while parsing a JP2 Component "
        "Mapping (cmap) box!");
    }
  int box_bytes = (int) cmap->get_remaining_bytes();
  if ((box_bytes & 3) || (box_bytes == 0))
    { KDU_ERROR(e,66); e <<
        KDU_TXT("Malformed component mapping (cmap) box encountered "
        "in JP2-family data source.  The body of any such box "
        "must contain exactly four bytes for each cmap-channel and there "
        "must be at least one cmap-channel.");
    }
  num_cmap_channels = box_bytes >> 2;
  if (num_cmap_channels < 1)
    { KDU_ERROR(e,67);  e <<
        KDU_TXT("Malformed component mapping (cmap) box encountered "
        "in JP2-family data source.  The body of the box does "
        "not appear to contain any channel mappings.");
    }
  max_cmap_channels = num_cmap_channels;
  cmap_channels = new j2_cmap_channel[max_cmap_channels];
  for (int n=0; n < num_cmap_channels; n++)
    {
      kdu_uint16 cmp=0;
      kdu_byte mtyp=0, pcol=0;
      if (!(cmap->read(cmp) && cmap->read(mtyp) && cmap->read(pcol) &&
            (mtyp < 2)))
        { KDU_ERROR(e,68); e <<
            KDU_TXT("Malformed component mapping (cmap) box "
            "encountered in JP2-family data source.  Invalid or "
            "truncated mapping specs.");
        }
      cmap_channels[n].component_idx = cmp;
      cmap_channels[n].lut_idx = (mtyp)?((int) pcol):-1;
      cmap_channels[n].bit_depth = -1; // To be set by `finalize'
      cmap_channels[n].is_signed = false;
    }

  cmap->close();
}

/*****************************************************************************/
/*                         j2_component_map::save_box                        */
/*****************************************************************************/

void
  j2_component_map::save_box(jp2_output_box *super_box, bool force_generation)
{
  if (!(use_cmap_box || force_generation))
    return;

  int n;
  jp2_output_box cmap;
  cmap.open(super_box,jp2_component_mapping_4cc);
  for (n=0; n < num_cmap_channels; n++)
    {
      cmap.write((kdu_uint16)(cmap_channels[n].component_idx));
      if (cmap_channels[n].lut_idx < 0)
        cmap.write((kdu_uint16) 0);
      else
        {
          cmap.write((kdu_byte) 1);
          cmap.write((kdu_byte)(cmap_channels[n].lut_idx));
        }
    }
  cmap.close();
}

/*****************************************************************************/
/*                        j2_component_map::finalize                         */
/*****************************************************************************/

void
  j2_component_map::finalize(j2_dimensions *dims, j2_palette *plt)
{
  dimensions = jp2_dimensions(dims);
  palette = jp2_palette(plt);
  int num_components = dimensions.get_num_components();
  int num_luts = palette.get_num_luts();
  if (num_luts > 0)
    use_cmap_box = true;
  else if (use_cmap_box)
    { KDU_ERROR(e,69); e <<
        KDU_TXT("JP2-family data source appears to contain a "
        "Component Mapping (cmap) box without any matching Palette (pclr) "
        "box.  Palette and Component Mapping boxes must be in one-to-one "
        "correspondence.");
    }
  if (!use_cmap_box)
    { // Fill in default cmap-channels
      assert(num_cmap_channels == 0);
      max_cmap_channels = num_cmap_channels = num_components;
      if (cmap_channels != NULL)
        delete[] cmap_channels;
      cmap_channels = new j2_cmap_channel[max_cmap_channels];
      for (int n=0; n < num_cmap_channels; n++)
        {
          j2_cmap_channel *cp = cmap_channels + n;
          cp->component_idx = n;
          cp->lut_idx = -1;
          cp->bit_depth = dimensions.get_bit_depth(n);
          cp->is_signed = dimensions.get_signed(n);
        }
    }
  else
    { // Check existing cmap-channels for compliance, and install bit-depths
      for (int n=0; n < num_cmap_channels; n++)
        {
          j2_cmap_channel *cp = cmap_channels + n;
          if ((cp->component_idx < 0) ||
              (cp->component_idx >= num_components) ||
              (cp->lut_idx >= num_luts))
            { KDU_ERROR(e,70); e <<
                KDU_TXT("JP2-family data source appears to contain "
                "an illegal Component Mapping (cmap) box, one of whose "
                "channels refers to a non-existent image component or "
                "palette lookup table.");
            }
          if (cp->lut_idx < 0)
            {
              cp->bit_depth = dimensions.get_bit_depth(cp->component_idx);
              cp->is_signed = dimensions.get_signed(cp->component_idx);
            }
          else
            {
              cp->bit_depth = palette.get_bit_depth(cp->lut_idx);
              cp->is_signed = palette.get_signed(cp->lut_idx);
            }
        }
    }
}

/*****************************************************************************/
/*                    j2_component_map::add_cmap_channel                     */
/*****************************************************************************/

int
  j2_component_map::add_cmap_channel(int component_idx, int lut_idx)
{
  assert(dimensions.exists() && palette.exists());
                // If not, you forgot to call `finalize' first

  if (lut_idx < 0)
    lut_idx = -1; // Facilitates comparisons

  int n;
  j2_cmap_channel *cp;
  for (n=0; n < num_cmap_channels; n++)
    {
      cp = cmap_channels + n;
      if ((cp->component_idx == component_idx) && (cp->lut_idx == lut_idx))
        return n;
    }

  // If we get here, a new entry must be added
  if ((component_idx < 0) ||
      (component_idx >= dimensions.get_num_components()) ||
      (lut_idx >= palette.get_num_luts()))
    { KDU_ERROR_DEV(e,71); e <<
        KDU_TXT("Attempting to create a Component Mapping (cmap) box, "
        "one of whose channels refers to a non-existent image component or "
        "palette lookup table.");
    }
  assert(use_cmap_box);  // Otherwise, the above should have generated an error
  if (num_cmap_channels >= max_cmap_channels)
    { // Re-allocate the array
      int new_max_chans = max_cmap_channels + num_cmap_channels + 3;
      j2_cmap_channel *buf = new j2_cmap_channel[new_max_chans];
      for (n=0; n < num_cmap_channels; n++)
        buf[n] = cmap_channels[n];
      if (cmap_channels != NULL)
        delete[] cmap_channels;
      cmap_channels = buf;
      max_cmap_channels = new_max_chans;
    }
  cp = cmap_channels + num_cmap_channels;
  num_cmap_channels++;
  cp->component_idx = component_idx;
  cp->lut_idx = lut_idx;
  if (cp->lut_idx < 0)
    {
      cp->bit_depth = dimensions.get_bit_depth(cp->component_idx);
      cp->is_signed = dimensions.get_signed(cp->component_idx);
    }
  else
    {
      cp->bit_depth = palette.get_bit_depth(cp->lut_idx);
      cp->is_signed = palette.get_signed(cp->lut_idx);
    }
  return num_cmap_channels-1;
}


/* ========================================================================= */
/*                                j2_channels                                */
/* ========================================================================= */

/*****************************************************************************/
/*                           j2_channels::compare                            */
/*****************************************************************************/

bool
  j2_channels::compare(j2_channels *src)
{
  if ((num_colours != src->num_colours) ||
      (have_chroma_key != src->have_chroma_key) ||
      (need_pxfm_fixpoint || need_pxfm_float || need_pxfm_split_exp) ||
      (src->need_pxfm_fixpoint || src->need_pxfm_float ||
       src->need_pxfm_split_exp))
    return false;
  int c, i;
  for (c=0; c < num_colours; c++)
    {
      for (i=0; i < 4; i++)
        if (channels[c].cmap_channel[i] != src->channels[c].cmap_channel[i])
          return false;
      if (have_chroma_key &&
          ((channels[c].chroma_key != src->channels[c].chroma_key) ||
           (channels[c].precision[0] != src->channels[c].precision[0]) ||
           (channels[c].signed_val[0] != src->channels[c].signed_val[0])))
        return false;
    }
  return true;
}

/*****************************************************************************/
/*                             j2_channels::copy                             */
/*****************************************************************************/

void
  j2_channels::copy(j2_channels *src)
{
  int n;
  if ((channels != NULL) || (chroma_key_buf != NULL) ||
      (cdef_descriptors != NULL) || (pxfm_descriptors != NULL))
    { KDU_ERROR_DEV(e,72); e <<
        KDU_TXT("Trying to copy a `jp2_channels' object to "
        "another object which has already been initialized.  Reinitialization "
        "is not permitted.");
    }

  need_pxfm_fixpoint = src->need_pxfm_fixpoint;
  need_pxfm_float = src->need_pxfm_float;
  need_pxfm_split_exp = src->need_pxfm_split_exp;
  num_pxfm_descriptors = src->num_pxfm_descriptors;
  if (num_pxfm_descriptors > 0)
    { 
      pxfm_descriptors = new kdu_uint32[num_pxfm_descriptors];
      for (int n=0; n < num_pxfm_descriptors; n++)
        pxfm_descriptors[n] = src->pxfm_descriptors[n];
    }

  num_cdef_descriptors = src->num_cdef_descriptors;
  if (num_cdef_descriptors > 0)
    { 
      cdef_descriptors = new kdu_uint32[num_cdef_descriptors];
      for (int n=0; n < num_cdef_descriptors; n++)
        cdef_descriptors[n] = src->cdef_descriptors[n];
    }

  max_colours = num_colours = src->num_colours;
  if (max_colours > 0)
    { 
      channels = new j2_channel[max_colours];
      for (n=0; n < num_colours; n++)
        channels[n] = src->channels[n];
    }

  have_chroma_key = src->have_chroma_key;
  opct_opacity = src->opct_opacity;
  opct_premult = src->opct_premult;
  resolved_cmap_channels = 0;
  chroma_key_len = src->chroma_key_len;
  if (chroma_key_len > 0)
    {
      chroma_key_buf = new kdu_byte[chroma_key_len];
      for (n=0; n < chroma_key_len; n++)
        chroma_key_buf[n] = src->chroma_key_buf[n];
    }
}

/*****************************************************************************/
/*                          j2_channels::parse_opct                          */
/*****************************************************************************/

void
  j2_channels::parse_opct(jp2_input_box *in)
{
  assert(in->get_box_type() == jp2_opacity_4cc);
  if (cdef_descriptors != NULL)
    { KDU_ERROR_DEV(e,0x24011601); e <<
      KDU_TXT("Encountered both a \"JP2 Channel Definitions\" (cdef) and a "
              "JPX \"Opacity\" (opct) box in the same context (usually a "
              "compositing layer header box).  These boxes provide alternate "
              "ways to describe channel mappings and are mutually exclusive.");
    }
  
  kdu_byte otyp;
  if (!(in->read(otyp) && (otyp <= 2)))
    { KDU_ERROR(e,78); e <<
      KDU_TXT("Malformed opacity (opct) box found "
              "in JPX data source.  Failed to read valid Otyp field.");
    }
  if (otyp == 0)
    opct_opacity = true;
  else if (otyp == 1)
    opct_premult = true;
  else
    { 
      kdu_byte nch;
      if (!in->read(nch))
        { KDU_ERROR(e,79); e <<
          KDU_TXT("Malformed opacity (opct) box found "
                  "in JPX data source.  Failed to read valid Nch field.");
        }
      have_chroma_key = true;
      num_colours = max_colours = (int) nch;
      channels = new j2_channel[max_colours];
      chroma_key_len = (int) in->get_remaining_bytes();
      chroma_key_buf = new kdu_byte[chroma_key_len];
      in->read(chroma_key_buf,chroma_key_len);
    }
  if (!in->close())
    { KDU_ERROR(e,80); e <<
      KDU_TXT("Malformed opacity (opct) box found in JPX data source.  "
              "The box appears to be too long.");
    }
}

/*****************************************************************************/
/*                          j2_channels::parse_cdef                          */
/*****************************************************************************/

void
  j2_channels::parse_cdef(jp2_input_box *in)
{
  assert(in->get_box_type() == jp2_channel_definition_4cc);
  if ((chroma_key_buf != NULL) ||
      opct_opacity || opct_premult || have_chroma_key)
    { KDU_ERROR_DEV(e,24011602); e <<
      KDU_TXT("Encountered both a \"JP2 Channel Definition\" (cdef) and a "
              "JPX \"Opacity\" (opct) box in the same context (usually a "
              "compositing layer header box).  These boxes provide alternate "
              "ways to describe channel mappings and are mutually exclusive.");
    }

  kdu_uint16 num_descs=0;
  if (!(in->read(num_descs) && (num_descs > 0)))
    { KDU_ERROR(e,74);  e <<
      KDU_TXT("Malformed \"channel definition\" (cdef) box found in "
              "JP2-family data source.  Missing or invalid count field.");
    }
  this->num_cdef_descriptors = num_descs;
  if (cdef_descriptors != NULL)
    { delete[] cdef_descriptors; cdef_descriptors = NULL; }
  cdef_descriptors = new kdu_uint32[num_cdef_descriptors];
  kdu_uint32 *cd = cdef_descriptors;
  for (; num_descs > 0; num_descs--)
    { 
      kdu_uint16 channel_idx=0, typ=0, assoc=0;
      if (!(in->read(channel_idx) && in->read(typ) && in->read(assoc) &&
            ((typ < 3) || (typ == (kdu_uint16) 0xFFFF))))
        { KDU_ERROR(e,75);  e <<
          KDU_TXT("Malformed \"channel definition\" (cdef) box found "
                  "in JP2-family data source.  Missing or invalid "
                  "channel association information.");
        }
      if (assoc >= (1<<14))
        { // Must be a code that is not defined within the standards
          num_cdef_descriptors--;
          continue;
        }
      if (typ > 2)
        typ = 3; // So the TYP code can be represented in 2 bits
      kdu_uint32 val = channel_idx;
      val = (val << 16) | (assoc << 2) | typ;
      *(cd++) = val;
    }
  assert(cd == (cdef_descriptors+num_cdef_descriptors));
  if (!in->close())
    { KDU_ERROR(e,77); e <<
      KDU_TXT("Malformed \"channel definition\" (cdef) box found in "
              "JP2-family data source.  The box appears to be too long.");
    }
}

/*****************************************************************************/
/*                          j2_channels::parse_pxfm                          */
/*****************************************************************************/

void
  j2_channels::parse_pxfm(jp2_input_box *in)
{
  assert(in->get_box_type() == jp2_pixel_format_4cc);
  kdu_uint16 num_descs=0;
  if (!(in->read(num_descs) && (num_descs > 0)))
    { KDU_ERROR(e,0x24011603);  e <<
      KDU_TXT("Malformed \"pixel format\" (pxfm) box found in "
              "JP2-family data source.  Missing or invalid count field.");
    }
  
  this->num_pxfm_descriptors = num_descs;
  if (pxfm_descriptors != NULL)
    { delete[] pxfm_descriptors; pxfm_descriptors = NULL; }
  need_pxfm_fixpoint = false; // Until we verify that a non-default
  need_pxfm_float = false;    // format is actually used.
  need_pxfm_split_exp = false;
  pxfm_descriptors = new kdu_uint32[num_pxfm_descriptors];
  kdu_uint32 *pd = pxfm_descriptors;
  for (; num_descs > 0; num_descs--)
    { 
      kdu_uint16 channel_idx=0, format=0;
      if (!(in->read(channel_idx) && in->read(format)))
        { KDU_ERROR(e,0x24011605); e <<
          KDU_TXT("Malformed \"pixel format\" (pxfm) box found in JP2-family "
                  "data source.  Missing channel format description.");
        }
      if ((format != 0x0000) && (format != 0x1000) && (format != 0x2000) &&
          ((format >> 12) != 3) && ((format >> 12) != 4))
        { KDU_ERROR(e,0x24011606); e <<
          KDU_TXT("Malformed \"pixel format\" (pxfm) box found in JP2-family "
                  "data source.  Invalid format code.");
        }
      kdu_uint32 val = channel_idx;
      val = (val << 16) | format;
      *(pd++) = val;
    }
  assert(pd == (pxfm_descriptors+num_pxfm_descriptors));
  if (!in->close())
    { KDU_ERROR(e,0x24011604); e <<
      KDU_TXT("Malformed \"pixel format\" (pxfm) box found in "
              "JP2-family data source.  The box appears to be too long.");
    }
}

/*****************************************************************************/
/*                           j2_channels::finalize                           */
/*****************************************************************************/

void
  j2_channels::finalize(int actual_colours, bool for_writing)
{
  j2_channel *cp=NULL;
  int n, c;

  if (!for_writing)
    { // Start by building the `channels' array from the information that
      // has been parsed, if any.
      if (cdef_descriptors != NULL)
        { // Build channels based on cdef box that was encountered
          for (n=0; n < num_cdef_descriptors; n++)
            { 
              kdu_uint32 desc = cdef_descriptors[n];
              int typ = (int)(desc & 3);
              int assoc = (int)((desc >> 2) & ((1<<14)-1));
              int idx = (int)(desc >> 16);
              if (assoc == ((1<<14)-1))
                continue; // Channel has no defined function
              
              if ((idx < num_pxfm_descriptors) &&
                  ((pxfm_descriptors[idx] & 0xFFFF) == 0x2000))
                typ = 3; // Exponent part of split-exponential format
              else if (typ == 3)
                continue; // Channel has no defined function
              int colour_idx = (assoc == 0)?0:(assoc-1);
              if (colour_idx >= max_colours)
                { 
                  int new_max_colours = max_colours + colour_idx + 3;
                  j2_channel *buf = new j2_channel[new_max_colours];
                  for (c=0; c < num_colours; c++)
                    buf[c] = channels[c];
                  if (channels != NULL)
                    delete[] channels;
                  channels = buf;
                  max_colours = new_max_colours;
                }
              if (colour_idx >= num_colours)
                num_colours = colour_idx+1;
              j2_channel *cp = channels + colour_idx;
              if (cp->cmap_channel[typ] >= 0)
                { KDU_ERROR(e,76); e <<
                  KDU_TXT("Malformed channel definition (cdef) box "
                          "found in JP2-family data source.  The box appears "
                          "to provide multiple channels with the same "
                          "assocation and data format.");
                }
              cp->cmap_channel[typ] = (int) idx;
              if (assoc == 0)
                cp->all_channels[typ] = true; // Fill in the rest later
            }
          
          // Now check completeness
          for (n=0; n < num_colours; n++)
            if (channels[n].cmap_channel[0] < 0)
              { KDU_ERROR(e,85); e <<
                KDU_TXT("Incomplete set of colour channel definitions "
                        "found in a `jp2_channels' object.  This is likely "
                        "due to a malformed channel definitions (cdef) box "
                        "in the JP2-family data source.");
              }
          
          // We can now remove any CDEF box information that was parsed
          delete[] cdef_descriptors;
          cdef_descriptors = NULL;
          num_cdef_descriptors = 0;
        }
      else
        { // Build `actual_colours' channels
          if ((chroma_key_buf != NULL) && (num_colours != actual_colours))
            { KDU_ERROR(e,82); e <<
              KDU_TXT("Malformed opacity (opct) box encountered in a JPX "
                      "file indicates a different number of colour channels "
                      "to that associated with the specified colour space.");
            }
          num_colours = actual_colours;
          if (num_colours > max_colours)
            { 
              max_colours = num_colours;
              if (channels != NULL)
                { delete[] channels; channels = NULL; }
              channels = new j2_channel[max_colours];
            }
          for (n=0; n < num_colours; n++)
            { 
              j2_channel *cp = channels + n;
              cp->cmap_channel[0] = n;
              if (opct_opacity)
                cp->cmap_channel[1] = num_colours;
              else if (opct_premult)
                cp->cmap_channel[2] = num_colours;
            }
        }
      
      // Now fill in data format values, possibly modifying the
      // `cmap_channel' entries of our colour channels if there was a
      // pixel format box.
      for (n=0; n < num_colours; n++)
        for (int typ=0; typ < 4; typ++)
          { 
            int idx = channels[n].cmap_channel[typ];
            if (idx < 0)
              continue; // Unused typ
            int format=JP2_CHANNEL_FORMAT_DEFAULT;
            if (num_pxfm_descriptors > 0)
              { 
                if (idx >= num_pxfm_descriptors)
                  { KDU_ERROR(e,0x24011607); e <<
                    KDU_TXT("Malformed \"pixel format\" (pxfm) box found "
                            "in JP2-family data source.  Number of channels "
                            "described is too small to accommodate those "
                            "referenced by the \"channel description\" "
                            "(cdef) box.");
                  }
                kdu_uint32 desc = pxfm_descriptors[idx];
                format = (int)(desc & 0xFFFF);
                desc >>= 16;
                channels[n].cmap_channel[typ] = (int)desc; // Rewired cmap idx
                if (format == 0x0000)
                  format = JP2_CHANNEL_FORMAT_DEFAULT;
                else if (format == 0x1000)
                  format = JP2_CHANNEL_FORMAT_SPLIT_EXP;
                else if (format == 0x2000)
                  format = JP2_CHANNEL_FORMAT_SPLIT_EXP;
                else if ((format >> 12) == 3)
                  { // `find_cmap_channels' changes frac-bits to int-bits
                    int frac_bits = (format & 0x0FFF);
                    format = JP2_CHANNEL_FORMAT_FIXPOINT | (frac_bits<<16);
                  }
                else if ((format >> 12) == 4)
                  { // `find_cmap_channels' changes mant-bits to exp-bits
                    int mant_bits = (format & 0x0FFF);
                    format = JP2_CHANNEL_FORMAT_FLOAT | (mant_bits<<16);
                  }
                else
                  assert(0); // Formats checked in `parse_pxfm'.
              }
            channels[n].data_format[typ] = format;
          }
      
      // We can now remove any PXFM box information that was parsed
      if (pxfm_descriptors != NULL)
        delete[] pxfm_descriptors;
      pxfm_descriptors = NULL;
      num_pxfm_descriptors = 0;
    }
  
  // Check `num_colours' against `actual_colours'
  if (actual_colours == 0)
    actual_colours = num_colours; // In case of a vendor colour space
  if (num_colours > actual_colours)
    { // This is an error condition, but it is one that seems to be somewhat
      // common in the construction of JP2 files that are out there.
      if ((actual_colours == 1) && (num_colours == 3) && !for_writing)
        { // Make reasonable attempt to fix the error rather than just failing
          actual_colours = num_colours;
#ifndef KDU_ALLOW_SILENT_RGB_MONO_CORRECTION
          KDU_WARNING(w,0x13051402); w <<
          KDU_TXT("Looks like the file contains an illegal channel "
                  "definitions (cdef) box; trying to fix the problem so "
                  "that content can still be rendered.");
#endif
        }
      else
        { 
          KDU_ERROR(e,81); e <<
          KDU_TXT("A `jp2_channels' object indicates the presence of "
                  "more colour channels than the number which is associated "
                  "with the specified colour space.  This may happen while "
                  "reading a JP2-family data source which contains an illegal "
                  "channel definitions (cdef) box, or it may happen while "
                  "writing a JP2-family file if the `jp2_channels' object has "
                  "been incorrectly initialized.");
        }
    }
  if (num_colours < actual_colours)
    { // Grow channel buffer to the right size; new entries initialized to -1
      if (actual_colours > max_colours)
        {
          j2_channel *buf = new j2_channel[actual_colours];
          for (n=0; n < num_colours; n++)
            buf[n] = channels[n];
          if (channels != NULL)
            delete[] channels;
          max_colours = actual_colours;
          channels = buf;
        }
      if (num_colours == 0)
        { // Install defaults -- the object has not been initialized otherwise
          for (n=0; n < actual_colours; n++)
            { 
              cp = channels + n;
              if (for_writing)
                { 
                  cp->codestream_idx[0] = 0;
                  cp->component_idx[0] = n;
                }
              else
                cp->cmap_channel[0] = n;
              cp->data_format[0] = JP2_CHANNEL_FORMAT_DEFAULT;
            }
        }
      num_colours = actual_colours;
    }

  // Compute the `need_pxfm_...' flags now, for both the reading and writing
  // cases, now that we know which data formats are actually in use -- we
  // are finalizing things here.
  need_pxfm_fixpoint = false;
  need_pxfm_float = false;
  need_pxfm_split_exp = false;
  for (n=0; n < num_colours; n++)
    { 
      cp = channels + n;
      for (c=0; c < 4; c++)
        { 
          int format = cp->data_format[c];
          if (format <= 0)
            continue;
          format = format & 0xFFFF; // Keep just the format code
          if (format == JP2_CHANNEL_FORMAT_FIXPOINT)
            need_pxfm_fixpoint = true;
          else if (format == JP2_CHANNEL_FORMAT_FLOAT)
            need_pxfm_float = true;
          else if (format == JP2_CHANNEL_FORMAT_SPLIT_EXP)
            need_pxfm_split_exp = true;
        }
    }
  
  // Finish up any finalization required for file writers.
  if (for_writing)
    { // Quick consistency check and then we are done
      for (n=0; n < num_colours; n++)
        if (have_chroma_key &&
            ((channels[n].cmap_channel[1] >= 0) ||
             (channels[n].cmap_channel[2] >= 0)))
          { KDU_ERROR(e,83); e <<
              KDU_TXT("The chroma-key feature offered by the "
              "`jp2_channels' interface may not be used in conjunction with "
              "opacity or pre-multiplied opacity channels.");
          }
      return;
    }
  
  // Now fill in incomplete entries in the `channels' array using information
  // which was supposed to be valid for `all_channels'.
  for (n=1; n < num_colours; n++)
    {
      cp = channels + n;
      for (c=0; c < 4; c++)
        if (channels->all_channels[c])
          { 
            if (cp->cmap_channel[c] >= 0)
              { KDU_ERROR(e,84); e <<
                KDU_TXT("Malformed channel definition (cdef) box found in "
                        "JP2-family data source.  The box appears to "
                        "provide multiple channels with the same "
                        "association and data format.");
              }
            assert(channels->cmap_channel[c] >= 0);
            cp->cmap_channel[c] = channels->cmap_channel[c];
            cp->data_format[c] = channels->data_format[c];
          }
    }
}

/*****************************************************************************/
/*                    j2_channels::all_cmap_channels_found                   */
/*****************************************************************************/

bool
  j2_channels::all_cmap_channels_found()
{
  int n, c;
  for (n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      for (c=0; c < 4; c++)
        if ((cp->cmap_channel[c] >= 0) && (cp->codestream_idx[c] < 0))
          return false;
    }
  return true;
}

/*****************************************************************************/
/*                      j2_channels::find_cmap_channels                      */
/*****************************************************************************/

void
  j2_channels::find_cmap_channels(j2_component_map *map, int codestream_idx,
                                  bool last_call)
{
  if (num_colours == 0)
    finalize(map->get_num_cmap_channels(),false); // In case vendor space
                        // caused `finalize' to be called with `num_colours'=0.
  int n, c, min_cmap_idx = resolved_cmap_channels;
  resolved_cmap_channels += map->get_num_cmap_channels();
  bool all_resolved = true; // Until proven otherwise
  for (n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      for (c=0; c < 4; c++)
        {
          int cmap_idx = cp->cmap_channel[c];
          if (cmap_idx < 0)
            { // No mapping for this reproduction function
              assert(c > 0); continue;
            }
          if ((cmap_idx >= min_cmap_idx) &&
              (cmap_idx < resolved_cmap_channels))
            { 
              cmap_idx -= min_cmap_idx;
              cp->codestream_idx[c] = codestream_idx;
              cp->component_idx[c] = map->get_cmap_component(cmap_idx);
              cp->lut_idx[c] = map->get_cmap_lut(cmap_idx);
              cp->precision[c] = map->get_cmap_bit_depth(cmap_idx);
              cp->signed_val[c] = map->get_cmap_signed(cmap_idx);
              int format = cp->data_format[c];
              if ((format & 0xFFFF) == JP2_CHANNEL_FORMAT_FIXPOINT)
                { // Convert frac-bits in 16 MSB's to int-bits
                  int precision = cp->precision[c];
                  int int_bits = precision - (format >> 16); // -ve allowed
                  cp->data_format[c] = (JP2_CHANNEL_FORMAT_FIXPOINT |
                                        (int_bits<<16));
                }
              else if ((format & 0xFFFF) == JP2_CHANNEL_FORMAT_FLOAT)
                { // Convert mantissa-bits in 16 MSB's to exp-bits
                  int precision = cp->precision[c];
                  int exp_bits = precision - 1 - (format >> 16);
                  if (exp_bits < 1)
                    { // All values denormalized, so treat as fixpoint with 0
                      // integer bits
                      cp->data_format[c] = JP2_CHANNEL_FORMAT_FIXPOINT;
                    }
                  else
                    cp->data_format[c] = (JP2_CHANNEL_FORMAT_FLOAT |
                                          (exp_bits<<16));
                }
            }
          else if (cp->codestream_idx[c] < 0)
            all_resolved = false;
        }
    }
  
  if (last_call && !all_resolved)
    { KDU_WARNING(w,0x16021601); w <<
      KDU_TXT("The Component Mapping (cmap) and Channel Definition (cdef) "
              "boxes, or lack thereof, are insufficient to discover "
              "codestream components to associate with all colour channels.");
    }
  
  if (chroma_key_buf != NULL)
    { // Parse `chroma_key_buf' contents
      int field_bytes, bytes_remaining = chroma_key_len;
      kdu_byte *bp = chroma_key_buf;
      for (n=0; n < num_colours; n++, bytes_remaining-=field_bytes)
        {
          j2_channel *cp = channels + n;
          assert(cp->precision[0] > 0);
          field_bytes = 1 + ((cp->precision[0] - 1) >> 3);
          if ((field_bytes > bytes_remaining) ||
              ((n==(num_colours-1)) && (field_bytes != bytes_remaining)))
            { KDU_ERROR(e,86); e <<
                KDU_TXT("Malformed opacity (opct) box in JPX data "
                "source.  The length of a chroma key specification is "
                "incompatible with the bit-depths of the colour channels.");
            }
          for (cp->chroma_key=0; field_bytes > 0; field_bytes--)
            {
              cp->chroma_key <<= 8;
              cp->chroma_key += (int) *(bp++);
              if (cp->signed_val[0] && (cp->precision[0] < 32))
                { // Sign extend
                  int shift = 32 - cp->precision[0];
                  cp->chroma_key <<= shift;
                  cp->chroma_key >>= shift;
                }
            }
        }
    }
}

/*****************************************************************************/
/*                      j2_channels::add_cmap_channels                       */
/*****************************************************************************/

void
  j2_channels::add_cmap_channels(j2_component_map *map, int codestream_idx)
{
  if (num_colours == 0)
    finalize(map->get_num_cmap_channels(),true);
  int n, c, min_cmap_idx = resolved_cmap_channels;
  resolved_cmap_channels += map->get_num_cmap_channels();
  for (n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      for (c=0; c < 4; c++)
        { 
          if (cp->codestream_idx[c] != codestream_idx)
            continue;
          assert(cp->cmap_channel[c] < 0); // Not assigned yet
          int cmap_idx =
            map->add_cmap_channel(cp->component_idx[c],cp->lut_idx[c]);
          cp->cmap_channel[c] = cmap_idx + min_cmap_idx;
          cp->precision[c] = map->get_cmap_bit_depth(cmap_idx);
          cp->signed_val[c] = map->get_cmap_signed(cmap_idx);
          if (have_chroma_key)
            {
              assert(c == 0);
              if (cmap_idx != n)
                { KDU_ERROR_DEV(e,87); e <<
                    KDU_TXT("Attempting to create a JPX file which "
                    "uses chroma-keys in an incompatible manner across "
                    "compositing layers which share a common codestream.  The "
                    "JPX file format has insufficient flexibility in its "
                    "channel mapping rules to allow arbitrary binding between "
                    "image components and colour channels at the same time as "
                    "chroma keying.");
                }
            }
        }
    }
}

/*****************************************************************************/
/*                         j2_channels::save_boxes                           */
/*****************************************************************************/

void
  j2_channels::save_boxes(jp2_output_box *super_box,
                          bool avoid_opct_if_possible)
{
  if (num_colours == 0)
    return; // Can be used to avoid writing a default channel definitions
            // box when writing a JPX header.
  assert(chroma_key_buf == NULL);
  int n, c;

  // Fill in the `all_channels' members
  for (c = 0; c < 4; c++)
    {
      for (n=1; n < num_colours; n++)
        if ((channels[n].cmap_channel[c] != channels[0].cmap_channel[c]) ||
            (channels[n].data_format[c] != channels[0].data_format[c]))
          break;
      bool common = (n == num_colours);
      for (n=0; n < num_colours; n++)
        channels[n].all_channels[c] = common;
    }
  
  // See if we need a pixel format box; if so, we will temporarily
  // instantiate the `pxfm_descriptors' array and we will also fill in
  // the `j2_channel::pxfm_desc' array of each colour channel.
  if (pxfm_descriptors != NULL)
    { delete[] pxfm_descriptors; pxfm_descriptors = NULL; }
  num_pxfm_descriptors = 0; // Get ready to accumulate PXFM information
  if (need_pxfm_fixpoint || need_pxfm_float || need_pxfm_split_exp)
    { // Scan the channels to determine the intermediate mappings.
      pxfm_descriptors = new kdu_uint32[num_colours*4]; // Max we can need
      for (c=0; c < 4; c++) // Cover intensities first, then opacity, etc.
        for (n=0; n < num_colours; n++)
          { 
            j2_channel *cp = channels + n;
            int idx = cp->cmap_channel[c];
            if (idx < 0)
              continue; // No mapping exists
            int format = cp->data_format[c] & 0xFFFF;
            kdu_uint16 fmt_code = 0;
            if (c == 3)
              { 
                assert(format == JP2_CHANNEL_FORMAT_SPLIT_EXP);
                fmt_code = 0x2000; // Separate exponent format
              }
            else if (format == JP2_CHANNEL_FORMAT_SPLIT_EXP)
              { 
                fmt_code = 0x1000; // Separate mantissa format
              }
            else if (format == JP2_CHANNEL_FORMAT_FIXPOINT)
              { 
                int int_bits = cp->data_format[c] >> 16;
                int frac_bits = cp->precision[c] - int_bits;
                if ((frac_bits < 0) || (frac_bits > 0x0FFF))
                  { KDU_ERROR(e,0x24011610); e <<
                    KDU_TXT("Fixed-point data format identified in call "
                            "to `jp2_channels::set_colour_mapping', or one "
                            "of the associated mapping functions, has "
                            "an invalid number of integer bits -- you "
                            "may need to increase the precision of the "
                            "associated codestream image component.");
                  }
                fmt_code = 0x3000 | (kdu_uint16)frac_bits;
              }
            else if (format == JP2_CHANNEL_FORMAT_FLOAT)
              { 
                int exp_bits = cp->data_format[c] >> 16;
                int mant_bits = cp->precision[c] - 1 - exp_bits;
                if ((exp_bits < 1) || (mant_bits < 0) ||
                    (mant_bits > 0x0FFF))
                  { KDU_ERROR(e,0x24011611); e <<
                    KDU_TXT("Floating-point data format identified in call "
                            "to `jp2_channels::set_colour_mapping', or one "
                            "of the associated mapping functions, has "
                            "an invalid number of exponent bits -- you "
                            "may need to increase the precision of the "
                            "associated codestream image component.");
                  }
                fmt_code = 0x4000 | (kdu_uint16)mant_bits;
              }
            kdu_uint32 pxfm_desc = (kdu_uint32) idx;
            pxfm_desc = (pxfm_desc << 16) + fmt_code;
            cp->pxfm_desc[c] = pxfm_desc;
            
            int k;
            for (k=0; k < num_pxfm_descriptors; k++)
              if (pxfm_descriptors[k] == pxfm_desc)
                break;
            if (k == num_pxfm_descriptors)
              { // Add a new pxfm descriptor
                num_pxfm_descriptors = k+1;
                pxfm_descriptors[k] = pxfm_desc;
              }
          }
    
      // Now write the PXFM box
      if (num_pxfm_descriptors > 0)
        { // Should always be the csae
          jp2_output_box pxfm;
          pxfm.open(super_box,jp2_pixel_format_4cc);
          pxfm.write((kdu_uint16) num_pxfm_descriptors);
          for (n=0; n < num_pxfm_descriptors; n++)
            { 
              kdu_uint32 desc = pxfm_descriptors[n];
              kdu_uint16 fmt_code = (kdu_uint16) desc;
              kdu_uint16 cmap_idx = (kdu_uint16)(desc>>16);
              pxfm.write(cmap_idx);
              pxfm.write(fmt_code);
            }
          pxfm.close();
        }
    }

  // See if we should write an opacity box, or no box at all
  bool need_box = have_chroma_key;
  bool use_opct = have_chroma_key || !avoid_opct_if_possible;
  if (need_pxfm_split_exp)
    { 
      need_box = true;
      use_opct = false; // Split-exponents require CDEF to be used
    }
  for (n=0; n < num_colours; n++)
    if (channels[n].cmap_channel[0] != n)
      break;
  if (n < num_colours)
    {
      need_box = true;
      use_opct = false;
      assert(!have_chroma_key);
    }
  if (!(channels->all_channels[1] && channels->all_channels[2]))
    { // Opacity and pre-multiplied opacity functions use different channels
      need_box = true;
      use_opct = false;
      assert(!have_chroma_key);
    }
  if ((channels->cmap_channel[1] < 0) && (channels->cmap_channel[2] < 0))
    { // No opacity or pre-multiplied opacity
      if (!have_chroma_key)
        use_opct = false;
    }
  else if ((channels->cmap_channel[1]>=0) && (channels->cmap_channel[2]>=0))
    { // Both opacity and pre-multiplied opacity channels used
      need_box = true;
      use_opct = false;
    }
  else if ((channels->cmap_channel[1] != num_colours) &&
           (channels->cmap_channel[2] != num_colours))
    {
      need_box = true;
      use_opct = false; // Can't write an opacity box, channels not in sequence
    }
  else
    need_box = true;
  
  if (have_chroma_key && !use_opct)
    { KDU_WARNING_DEV(w,0x24011612); w <<
      KDU_TXT("Unable to record chroma key supplied to `jp2_channels' object "
              "in the generated file, since this reqires an \"opacity\" "
              "(opct) box to be written in place of a \"channel definition\" "
              "(cdef) box, yet other aspects of the information supplied to "
              "`jp2_channels::set_colour_mapping' and related functions "
              "demand that a cdef box be written.  The use of chroma keys "
              "is discouraged.");
    }

  // Now write the appropriate box, if any
  if (use_opct)
    { // Write an opacity (opct) box
      jp2_output_box opct;
      opct.open(super_box,jp2_opacity_4cc);
      if (channels->cmap_channel[1] >= 0)
        { // Opacity box with one opacity channel
          assert((channels->cmap_channel[2] < 0) && !have_chroma_key);
          opct.write((kdu_byte) 0);
        }
      else if (channels->cmap_channel[2] >= 0)
        { // Opacity box with one pre-multiplied opacity channel
          assert(!have_chroma_key);
          opct.write((kdu_byte) 1);
        }
      else
        { // Opacity box with a chroma key
          assert(have_chroma_key);
          opct.write((kdu_byte) 2);
          if (num_colours > 255)
            { KDU_ERROR(e,88); e <<
                KDU_TXT("Attempting to write a JPX opacity box with "
                "chroma key values for more than 255 channels.  This is not "
                "possible within the syntactic constraints of the opct box.");
            }
          opct.write((kdu_byte) num_colours);
          for (n=0; n < num_colours; n++)
            {
              assert(channels[n].precision[0] > 0);
              int num_bytes = 1 + ((channels[n].precision[0] - 1) >> 3);
              for (c=(num_bytes-1)<<3; c >= 0; c-=8)
                opct.write((kdu_byte)(channels[n].chroma_key >> c));
            }
        }
      opct.close();
    }
  else if (need_box)
    { // Write a channel definition (cdef) box
      jp2_output_box cdef;
      cdef.open(super_box,jp2_channel_definition_4cc);
      kdu_uint16 typ, assoc;
      int num_descriptions = 0;
      for (n=0; n < num_colours; n++)
        for (c=0; c < 4; c++)
          if ((channels[n].component_idx[c] >= 0) &&
              ((n==0) || !channels[n].all_channels[c]))
            num_descriptions++;
      cdef.write((kdu_uint16) num_descriptions);
      for (n=0; n < num_colours; n++)
        for (c=0; c < 4; c++)
          { 
            if (channels[n].component_idx[c] < 0)
              continue;
            typ = (kdu_uint16) c;
            assoc = ((kdu_uint16) n)+1;
            if (channels[n].all_channels[c])
              { // Write whole-image description
                if (n != 0)
                  continue; // Only write whole-image description once
                assoc = 0;
              }
            int idx = channels[n].cmap_channel[c];
            if (pxfm_descriptors != NULL)
              { // Figure out which PXFM channel to use
                kdu_uint32 pxfm_desc = channels[n].pxfm_desc[c];
                for (idx=0; idx < num_pxfm_descriptors; idx++)
                  if (pxfm_descriptors[idx] == pxfm_desc)
                    break;
                assert(idx < num_pxfm_descriptors);
              }
            cdef.write((kdu_uint16)idx);
            cdef.write(typ);
            cdef.write(assoc);
          }
      cdef.close();
    }
  
  // Clean up any `pxfm_descriptors' array created above.
  if (pxfm_descriptors != NULL)
    { delete[] pxfm_descriptors; pxfm_descriptors = NULL; }
  num_pxfm_descriptors = 0;
}

/*****************************************************************************/
/*                     j2_channels::uses_palette_colour                      */
/*****************************************************************************/

bool
  j2_channels::uses_palette_colour()
{
  for (int n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      if (cp->lut_idx[0] >= 0)
        return true;
    }
  return false;
}

/*****************************************************************************/
/*                         j2_channels::has_opacity                          */
/*****************************************************************************/

bool
  j2_channels::has_opacity()
{
  for (int n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      if (cp->codestream_idx[1] >= 0)
        return true;
    }
  return false;
}

/*****************************************************************************/
/*                  j2_channels::has_premultiplied_opacity                   */
/*****************************************************************************/

bool
  j2_channels::has_premultiplied_opacity()
{
  for (int n=0; n < num_colours; n++)
    {
      j2_channel *cp = channels + n;
      if (cp->codestream_idx[2] >= 0)
        return true;
    }
  return false;
}

/*****************************************************************************/
/*            j2_channels::j2_channel::add_split_exponent_mapping            */
/*****************************************************************************/

void
  j2_channels::j2_channel::add_split_exponent_mapping(const int *format_params)
{
  int comp_idx_val = format_params[0];
  int lut_idx_val = format_params[1];
  int cs_idx_val = format_params[2];
  if (cs_idx_val < 0)
    cs_idx_val = -1; // For consistency in comparisons later on
  if (((component_idx[3] >= 0) && (component_idx[3] != comp_idx_val)) ||
      ((lut_idx[3] >= 0) && (lut_idx[3] != lut_idx_val)) ||
      ((codestream_idx[3] >= 0) && (codestream_idx[3] != cs_idx_val)))
    { KDU_ERROR_DEV(e,0x23011601); e <<
      KDU_TXT("Incompatible exponent channel mapping parameters supplied "
              "in calls to `jp2_channels::set_colour_mapping', "
              "`jp2_channels::set_opacity_mapping' or "
              "`jp2_channels:set_premult_mapping'; where two types of "
              "mapping (e.g. colour and opacity) are supplied for the same "
              "colour channel and both specify the split-exponent pixel data "
              "format, both are required to use the same mapping for the "
              "exponent part of the channel description.");
    }
  component_idx[3] = comp_idx_val;
  lut_idx[3] = lut_idx_val;
  codestream_idx[3] = cs_idx_val;
  data_format[3] = JP2_CHANNEL_FORMAT_SPLIT_EXP;
}


/* ========================================================================= */
/*                                jp2_channels                               */
/* ========================================================================= */

/*****************************************************************************/
/*                             jp2_channels::copy                            */
/*****************************************************************************/

void
  jp2_channels::copy(jp2_channels src)
{
  if ((state == NULL) || (src.state == NULL))
    { 
      assert((state != NULL) && (src.state != NULL));
      return;
    }
  state->copy(src.state);
  for (int n=0; n < state->num_colours; n++)
    for (int c=0; c < 3; c++)
      state->channels[n].cmap_channel[c] = -1; // So boxes can be written later
}

/*****************************************************************************/
/*                             jp2_channels::init                            */
/*****************************************************************************/

void
  jp2_channels::init(int num_colours)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if ((state->channels != NULL) || (state->chroma_key_buf != NULL))
    { KDU_ERROR_DEV(e,89); e <<
        KDU_TXT("Attempting to initialize a `jp2_channels' object "
        "multiple times.  `jp2_channels::init' may be applied only to an "
        "object which is not yet initialized.");
    }
  state->num_colours = state->max_colours = num_colours;
  state->channels = new j2_channels::j2_channel[num_colours];
}

/*****************************************************************************/
/*                      jp2_channels::set_colour_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::set_colour_mapping(int colour_idx, int codestream_component,
                                   int lut_idx, int codestream_idx,
                                   int data_format, const int *format_params)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  if (lut_idx < 0)
    lut_idx = -1; // For consistency in comparisons later on.
  j2_channels::j2_channel *cp = state->channels+colour_idx;
  cp->component_idx[0] = codestream_component;
  cp->lut_idx[0] = lut_idx;
  cp->codestream_idx[0] = codestream_idx;
  cp->data_format[0] = data_format;
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    return true; // Nothing more to do
  if (((data_format == JP2_CHANNEL_FORMAT_FIXPOINT) ||
       (data_format == JP2_CHANNEL_FORMAT_FLOAT)) && (format_params != NULL))
    cp->data_format[0] |= (format_params[0] << 16);
  else if ((data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP) &&
           (format_params != NULL))
    cp->add_split_exponent_mapping(format_params);
  else
    { KDU_ERROR_DEV(e,0x23011602); e <<
      KDU_TXT("Invalid data format/params combination supplied in call to "
              "`jp2_channels::set_colour_mapping'.");
    }
  return true;
}

/*****************************************************************************/
/*                     jp2_channels::set_opacity_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::set_opacity_mapping(int colour_idx, int codestream_component,
                                    int lut_idx, int codestream_idx,
                                    int data_format, const int *format_params)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  if (lut_idx < 0)
    lut_idx = -1; // For consistency in comparisons later on.
  j2_channels::j2_channel *cp = state->channels+colour_idx;
  cp->component_idx[1] = codestream_component; /* Was a bug in version 7.8 */
  cp->lut_idx[1] = lut_idx;
  cp->codestream_idx[1] = codestream_idx;
  cp->data_format[1] = data_format;
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    return true; // Nothing more to do
  if (((data_format == JP2_CHANNEL_FORMAT_FIXPOINT) ||
       (data_format == JP2_CHANNEL_FORMAT_FLOAT)) && (format_params != NULL))
    cp->data_format[1] |= (format_params[0] << 16);
  else if ((data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP) &&
           (format_params != NULL))
    cp->add_split_exponent_mapping(format_params);
  else
    { KDU_ERROR_DEV(e,0x23011603); e <<
      KDU_TXT("Invalid data format/params combination supplied in call to "
              "`jp2_channels::set_opacity_mapping'.");
    }
  return true;
}

/*****************************************************************************/
/*                     jp2_channels::set_premult_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::set_premult_mapping(int colour_idx, int codestream_component,
                                    int lut_idx, int codestream_idx,
                                    int data_format, const int *format_params)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  j2_channels::j2_channel *cp = state->channels+colour_idx;
  cp->component_idx[2] = codestream_component;
  cp->lut_idx[2] = lut_idx;
  cp->codestream_idx[2] = codestream_idx;
  cp->data_format[2] = data_format;
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    return true; // Nothing more to do
  if (((data_format == JP2_CHANNEL_FORMAT_FIXPOINT) ||
       (data_format == JP2_CHANNEL_FORMAT_FLOAT)) && (format_params != NULL))
    cp->data_format[2] |= (format_params[0] << 16);
  else if ((data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP) &&
           (format_params != NULL))
    cp->add_split_exponent_mapping(format_params);
  else
    { KDU_ERROR_DEV(e,0x23011604); e <<
      KDU_TXT("Invalid data format/params combination supplied in call to "
              "`jp2_channels::set_premult_mapping'.");
    }
  return true;
}

/*****************************************************************************/
/*                        jp2_channels::set_chroma_key                       */
/*****************************************************************************/

bool
  jp2_channels::set_chroma_key(int colour_idx, kdu_int32 key_val)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  state->channels[colour_idx].chroma_key = key_val;
  state->have_chroma_key = true;
  return true;
}

/*****************************************************************************/
/*                       jp2_channels::get_num_colours                       */
/*****************************************************************************/

int
  jp2_channels::get_num_colours() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0;
    }
  return state->num_colours;
}

/*****************************************************************************/
/*                      jp2_channels::get_colour_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::get_colour_mapping(int colour_idx, int &codestream_component,
                                   int &lut_idx, int &codestream_idx,
                                   int &data_format, int *format_params) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  j2_channels::j2_channel *cp = state->channels + colour_idx;
  if (cp->codestream_idx[0] < 0)
    return false;
  codestream_idx = cp->codestream_idx[0];
  if (codestream_idx >= state_params.cs_off_thresh)
    codestream_idx += state_params.cs_off;
  codestream_component = cp->component_idx[0];
  lut_idx = cp->lut_idx[0];
  int fmt = cp->data_format[0];
  data_format = fmt & 0x0FFFF;
  if ((data_format != JP2_CHANNEL_FORMAT_DEFAULT) && (format_params != NULL))
    { 
      if (data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP)
        { 
          format_params[0] = cp->component_idx[3];
          format_params[1] = cp->lut_idx[3];
          format_params[2] = cp->codestream_idx[3];
        }
      else
        format_params[0] = (fmt >> 16) & 0x0FFFF;
    }  
  return true;
}

/*****************************************************************************/
/*                     jp2_channels::get_opacity_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::get_opacity_mapping(int colour_idx, int &codestream_component,
                                    int &lut_idx, int &codestream_idx,
                                    int &data_format, int *format_params) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  j2_channels::j2_channel *cp = state->channels + colour_idx;
  if (cp->codestream_idx[1] < 0)
    return false;
  codestream_idx = cp->codestream_idx[1];
  if (codestream_idx >= state_params.cs_off_thresh)
    codestream_idx += state_params.cs_off;
  codestream_component = cp->component_idx[1];
  lut_idx = cp->lut_idx[1];
  int fmt = cp->data_format[1];
  data_format = fmt & 0x0FFFF;
  if ((data_format != JP2_CHANNEL_FORMAT_DEFAULT) && (format_params != NULL))
    { 
      if (data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP)
        { 
          format_params[0] = cp->component_idx[3];
          format_params[1] = cp->lut_idx[3];
          format_params[2] = cp->codestream_idx[3];
        }
      else
        format_params[0] = (fmt >> 16) & 0x0FFFF;
    }
  return true;
}

/*****************************************************************************/
/*                     jp2_channels::get_premult_mapping                     */
/*****************************************************************************/

bool
  jp2_channels::get_premult_mapping(int colour_idx, int &codestream_component,
                                    int &lut_idx, int &codestream_idx,
                                    int &data_format, int *format_params) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  j2_channels::j2_channel *cp = state->channels + colour_idx;
  if (cp->codestream_idx[2] < 0)
    return false;
  codestream_idx = cp->codestream_idx[2];
  if (codestream_idx >= state_params.cs_off_thresh)
    codestream_idx += state_params.cs_off;
  codestream_component = cp->component_idx[2];
  lut_idx = cp->lut_idx[2];
  int fmt = cp->data_format[2];
  data_format = fmt & 0x0FFFF;
  if ((data_format != JP2_CHANNEL_FORMAT_DEFAULT) && (format_params != NULL))
    { 
      if (data_format == JP2_CHANNEL_FORMAT_SPLIT_EXP)
        { 
          format_params[0] = cp->component_idx[3];
          format_params[1] = cp->lut_idx[3];
          format_params[2] = cp->codestream_idx[3];
        }
      else
        format_params[0] = (fmt >> 16) & 0x0FFFF;
    }
  return true;
}

/*****************************************************************************/
/*                        jp2_channels::get_chroma_key                       */
/*****************************************************************************/

bool
  jp2_channels::get_chroma_key(int colour_idx, kdu_int32 &key_val) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((colour_idx < 0) || (colour_idx >= state->num_colours))
    return false;
  if (!state->have_chroma_key)
    return false;
  key_val = state->channels[colour_idx].chroma_key;
  return true;
}


/* ========================================================================= */
/*                               j2_resolution                               */
/* ========================================================================= */

/*****************************************************************************/
/*                             j2_resolution::init                           */
/*****************************************************************************/

void
  j2_resolution::init(float aspect_ratio)
{
  if (display_ratio > 0.0F)
    { KDU_ERROR_DEV(e,90);  e <<
        KDU_TXT("JP2 resolution information may be initialized only once!");
    }
  display_ratio = capture_ratio = aspect_ratio;
  display_res = capture_res = 0.0F;
}

/*****************************************************************************/
/*                             j2_resolution::init                           */
/*****************************************************************************/

bool
  j2_resolution::init(jp2_input_box *res)
{
  if (display_ratio > 0.0F)
    { KDU_ERROR(e,91); e <<
        KDU_TXT("JP2-family data source contains multiple instances "
        "of the resolution (res) box within the same JP2 header box or "
        "compositing layer header box!");
    }
  assert(res->is_complete());
  jp2_input_box sub;
  while (sub.open(res))
    {
      if (!sub.is_complete())
        {
          sub.close();
          res->seek(0);
          return false;
        }
      if ((sub.get_box_type() != jp2_capture_resolution_4cc) &&
          (sub.get_box_type() != jp2_display_resolution_4cc))
        sub.close();
      else
        parse_sub_box(&sub);
    }
  if ((capture_res <= 0.0F) && (display_res <= 0.0F))
    { KDU_ERROR(e,92); e <<
        KDU_TXT("The JP2 resolution box must contain at least one "
        "of the capture or display resolution sub-boxes.");
    }
  if (!res->close())
    { KDU_ERROR(e,93); e <<
        KDU_TXT("Malformed resolution box "
        "found in JP2-family data source.  Box appears to be too long.");
    }
  return true;
}

/*****************************************************************************/
/*                        j2_resolution::parse_sub_box                       */
/*****************************************************************************/

void
  j2_resolution::parse_sub_box(jp2_input_box *box)
{
  kdu_uint16 v_num=0, v_den=0, h_num=0, h_den=0;
  kdu_byte v_exp=0, h_exp=0;

  if (!(box->read(v_num) && box->read(v_den) &&
        box->read(h_num) && box->read(h_den) &&
        box->read(v_exp) && box->read(h_exp) &&
        v_den && h_den && v_num && h_num))
    { KDU_ERROR(e,94); e <<
        KDU_TXT("Malformed capture or display resolution sub-box "
        "found in JP2-family data source.  Insufficient or illegal data "
        "fields.");
    }

  double v_res, h_res;
  v_res = ((double) v_num) / ((double) v_den);
  while (v_exp & 0x80)
    { v_res *= 0.1F; v_exp++; }
  while (v_exp != 0)
    { v_res *= 10.0F; v_exp--; }
  h_res = ((double) h_num) / ((double) h_den);
  while (h_exp & 0x80)
    { h_res *= 0.1F; h_exp++; }
  while (h_exp != 0)
    { h_res *= 10.0F; h_exp--; }

  if (box->get_box_type() == jp2_capture_resolution_4cc)
    {
      capture_ratio = (float)(h_res / v_res);
      if (display_res <= 0.0F)
        display_ratio = capture_ratio;
      capture_res = (float) v_res;
    }
  else if (box->get_box_type() == jp2_display_resolution_4cc)
    {
      display_ratio = (float)(h_res / v_res);
      if (capture_res <= 0.0F)
        capture_ratio = display_ratio;
      display_res = (float) v_res;
    }
  else
    assert(0);

  if (!box->close())
    { KDU_ERROR(e,95); e <<
        KDU_TXT("Malformed capture or display resolution sub-box "
        "found in JP2-family data source.  Box appears to be too long.");
    }
}

/*****************************************************************************/
/*                          j2_resolution::finalize                          */
/*****************************************************************************/

void
  j2_resolution::finalize()
{
  if (display_ratio <= 0.0F)
    display_ratio = 1.0F;
  if (capture_ratio <= 0.0F)
    capture_ratio = 1.0F;
}

/*****************************************************************************/
/*                          j2_resolution::save_box                          */
/*****************************************************************************/

void
  j2_resolution::save_box(jp2_output_box *super_box)
{
  bool save_display_ratio = (fabs(display_ratio-1.0) > 0.01F);
  bool save_capture_ratio = (fabs(capture_ratio-1.0) > 0.01F);
  bool save_display_res = (display_res > 0.0F);
  bool save_capture_res = (capture_res > 0.0F);
  if (!(save_display_ratio || save_display_res ||
        save_capture_ratio || save_capture_res))
    return;
  bool save_different_ratios = (fabs(capture_ratio/display_ratio-1.0) > 0.01);
  bool save_capture = save_capture_res || save_different_ratios;
  bool save_display = (save_display_res || save_different_ratios ||
                       !save_capture);

  jp2_output_box res;
  res.open(super_box,jp2_resolution_4cc);
  if (save_display)
    { 
      float v_res = display_res;
      if (v_res <= 0.0)
        v_res = 1.0; // 1 grid-point per metre
      save_sub_box(&res,jp2_display_resolution_4cc,v_res,
                   v_res*display_ratio);
    }
  if (save_capture)
    { 
      float v_res = capture_res;
      if (v_res <= 0.0)
        v_res = display_res; // Must be just a different aspect ratio
      save_sub_box(&res,jp2_capture_resolution_4cc,v_res,
                   v_res*capture_ratio);
    }
  res.close();
}

/*****************************************************************************/
/*                        j2_resolution::save_sub_box                        */
/*****************************************************************************/

void
  j2_resolution::save_sub_box(jp2_output_box *super_box, kdu_uint32 box_type,
                              double v_res, double h_res)
{
  int v_num, v_den, h_num, h_den;
  int v_exp, h_exp;

  int v_scheme = get_rational_pels_per_metre(v_res,v_num,v_den,v_exp,-1);
  get_rational_pels_per_metre(h_res,h_num,h_den,h_exp,v_scheme);
  if ((h_num <= 0) || (h_num >= (1<<16)) || (v_num <= 0) || (v_num >= (1<<16)))
    { KDU_ERROR(e,96); e <<
        KDU_TXT("Unable to save resolution information having "
        "illegal or ridiculously small or large values!");
    }

  jp2_output_box box;
  box.open(super_box,box_type);
  box.write((kdu_uint16) v_num);
  box.write((kdu_uint16) v_den);
  box.write((kdu_uint16) h_num);
  box.write((kdu_uint16) h_den);
  box.write((kdu_byte) v_exp);
  box.write((kdu_byte) h_exp);
  box.close();
}

/* ========================================================================= */
/*                               jp2_resolution                              */
/* ========================================================================= */

/*****************************************************************************/
/*                            jp2_resolution::copy                           */
/*****************************************************************************/

void
  jp2_resolution::copy(jp2_resolution src)
{
  if ((state == NULL) || (src.state == NULL))
    { 
      assert((state != NULL) && (src.state != NULL));
      return;
    }
  state->copy(src.state);
}

/*****************************************************************************/
/*                            jp2_resolution::init                           */
/*****************************************************************************/

bool
  jp2_resolution::init(float aspect_ratio)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if (aspect_ratio <= 0.0F)
    return false;
  state->init(aspect_ratio);
  return true;
}

/*****************************************************************************/
/*            jp2_resolution::set_different_capture_aspect_ratio             */
/*****************************************************************************/

bool
  jp2_resolution::set_different_capture_aspect_ratio(float aspect_ratio)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if ((aspect_ratio <= 0.0F) || (state->display_ratio <= 0.0F))
    return false;
  state->capture_ratio = aspect_ratio;
  return true;
}

/*****************************************************************************/
/*                        jp2_resolution::set_resolution                     */
/*****************************************************************************/

bool
  jp2_resolution::set_resolution(float resolution, bool for_display)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if (state->display_ratio <= 0.0F)
    return false;
  if (for_display)
    state->display_res = resolution;
  else
    state->capture_res = resolution;
  return true;
}

/*****************************************************************************/
/*                       jp2_resolution::get_aspect_ratio                    */
/*****************************************************************************/

float
  jp2_resolution::get_aspect_ratio(bool for_display) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0.0F;
    }
  return (for_display)?(state->display_ratio):(state->capture_ratio);
}

/*****************************************************************************/
/*                        jp2_resolution::get_resolution                     */
/*****************************************************************************/

float
  jp2_resolution::get_resolution(bool for_display) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0.0F;
    }
  return (for_display)?(state->display_res):(state->capture_res);
}


/* ========================================================================= */
/*                              j2_icc_profile                               */
/* ========================================================================= */

/*****************************************************************************/
/*                     j2_icc_profile::init (profile buf)                    */
/*****************************************************************************/

void
  j2_icc_profile::init(kdu_byte *profile_buf, int in_buf_size,
                       bool donate_buffer)
{
  kdu_uint32 val32=0;
  
  buffer = profile_buf;
  num_buffer_bytes = 0;
  if (in_buf_size >= 4)
    { num_buffer_bytes = 4; read(val32,0); num_buffer_bytes = (int) val32; }
  if (!donate_buffer)
    buffer = NULL; // Just in case we get destroyed thru an exception catcher
  if (num_buffer_bytes < 132)
    { KDU_ERROR(e,97); e <<
        KDU_TXT("Embedded ICC profile in JP2 colour description box does not "
        "have a complete header -- or the profile may be ridiculously long!");
    }
  if (num_buffer_bytes > in_buf_size)
    { KDU_ERROR_DEV(e,0x12051401); e <<
      KDU_TXT("Embedded ICC profile in JP2 colour description box is too "
              "large for input buffer.");
    }

  // Copy the buffer.
  if (!donate_buffer)
    {
      buffer = new kdu_byte[num_buffer_bytes];
      memcpy(buffer,profile_buf,(size_t) num_buffer_bytes);
    }

  // Check the profile and locate the relevant tags, recording their locations
  // and lengths in the relevant member arrays.
  read(val32,12); // Get profile/device class signature.
  if (val32 == icc_input_device)
    profile_is_input = true;
  else if (val32 == icc_display_device)
    profile_is_display = true;
  else if (val32 == icc_output_device)
    profile_is_output = true;

  read(val32,16); // Get colour space signature
  if (val32 == icc_xyz_data)
    num_colours = 3;
  else if (val32 == icc_lab_data)
    num_colours = 3;
  else if (val32 == icc_luv_data)
    num_colours = 3;
  else if (val32 == icc_ycbcr_data)
    num_colours = 3;
  else if (val32 == icc_yxy_data)
    num_colours = 3;
  else if (val32 == icc_rgb_data)
    num_colours = 3;
  else if (val32 == icc_gray_data)
    num_colours = 1;
  else if (val32 == icc_hsv_data)
    num_colours = 3;
  else if (val32 == icc_hls_data)
    num_colours = 3;
  else if (val32 == icc_cmyk_data)
    num_colours = 4;
  else if (val32 == icc_cmy_data)
    num_colours = 3;
  else if (val32 == icc_2clr_data)
    num_colours = 2;
  else if (val32 == icc_3clr_data)
    num_colours = 3;
  else if (val32 == icc_4clr_data)
    num_colours = 4;
  else if (val32 == icc_5clr_data)
    num_colours = 5;
  else if (val32 == icc_6clr_data)
    num_colours = 6;
  else if (val32 == icc_7clr_data)
    num_colours = 7;
  else if (val32 == icc_8clr_data)
    num_colours = 8;
  else if (val32 == icc_9clr_data)
    num_colours = 9;
  else if (val32 == icc_10clr_data)
    num_colours = 10;
  else if (val32 == icc_11clr_data)
    num_colours = 11;
  else if (val32 == icc_12clr_data)
    num_colours = 12;
  else if (val32 == icc_13clr_data)
    num_colours = 13;
  else if (val32 == icc_14clr_data)
    num_colours = 14;
  else if (val32 == icc_15clr_data)
    num_colours = 15;
  else
    { KDU_ERROR(e,98); e <<
        KDU_TXT("Unknown colour space signature found in embedded "
        "ICC profile within a JP2-family data source's colour description "
        "(colr) box.");
    }

  read(val32,20); // Get PCS
  if (val32 == icc_pcs_xyz)
    pcs_is_xyz = true;
  else if (val32 == icc_pcs_lab)
    pcs_is_xyz = false;
  else
    { KDU_ERROR(e,99); e <<
        KDU_TXT("Unknown PCS signature found in embedded ICC profile "
        "within a JP2-family data source's colour description (colr) box.");
    }

  read(val32,128); // Read tab table
  num_tags = (int) val32;
  int max_tags = (num_buffer_bytes-132) / 12;
  if ((num_tags < 0) || (num_tags > max_tags))
    { KDU_ERROR(e,0x21101305); e <<
      KDU_TXT("Embedded ICC profile in JP2 colour description box specifies "
              "more tags than can be accommodated by the length of the "
              "profile");
    }
  
  int t;
  for (t=0; t < 3; t++)
    trc_offsets[t] = colorant_offsets[t] = 0;
  for (t=0; t < num_tags; t++)
    {
      kdu_uint32 signature=0, offset=0, length=0;
      if (!(read(signature,12*t+132) && read(offset,12*t+136) &&
            read(length,12*t+140)))
        { KDU_ERROR(e,100); e <<
            KDU_TXT("Embedded ICC profile in JP2 colour description "
            "box appears to have been truncated!");
        }
      if (signature == icc_gray_trc)
        trc_offsets[0] = get_curve_data_offset(offset,length);
      else if (signature == icc_red_trc)
        trc_offsets[0] = get_curve_data_offset(offset,length);
      else if (signature == icc_green_trc)
        trc_offsets[1] = get_curve_data_offset(offset,length);
      else if (signature == icc_blue_trc)
        trc_offsets[2] = get_curve_data_offset(offset,length);
      else if (signature == icc_red_colorant)
        colorant_offsets[0] = get_xyz_data_offset(offset,length);
      else if (signature == icc_green_colorant)
        colorant_offsets[1] = get_xyz_data_offset(offset,length);
      else if (signature == icc_blue_colorant)
        colorant_offsets[2] = get_xyz_data_offset(offset,length);
    }
  uses_3d_luts = false;
  for (t=0; t < num_colours; t++)
    if (trc_offsets[t] == 0)
      {
        if (profile_is_input)
          uses_3d_luts = true;
        else if (profile_is_display)
          { KDU_ERROR(e,101); e <<
              KDU_TXT("Embedded ICC profile in JP2 colour description "
              "box specifies a display profile, but does not contain a "
              "complete set of tone reproduction curves!  This condition is "
              "not compatible with any legal ICC profile.");
          }
      }
  if (num_colours == 3)
    for (t=0; t < 3; t++)
      if (colorant_offsets[t] == 0)
        {
          if (profile_is_input)
            uses_3d_luts = true;
          else if (profile_is_display)
            { KDU_ERROR(e,102); e <<
                KDU_TXT("Embedded ICC profile in JP2 colour "
                "description box specifies a 3 colour display profile, but "
                "does not contain a complete set of primary colorant "
                "specifications.");
            }
        }
}

/*****************************************************************************/
/*                           j2_icc_profile::get_lut                         */
/*****************************************************************************/

bool
  j2_icc_profile::get_lut(int channel_idx, float lut[], int index_bits)
{
  if ((channel_idx < 0) || (channel_idx >= num_colours) ||
      ((num_colours != 1) && (num_colours != 3)) ||
      (!(profile_is_input || profile_is_display)) ||
      (trc_offsets[channel_idx] == 0) || uses_3d_luts || !pcs_is_xyz)
    return false;

  kdu_uint32 val32;
  kdu_uint16 val16;
  int offset = trc_offsets[channel_idx]; assert(offset > 128);
  int p, num_points; read(val32,offset); num_points = val32;
  offset += 4; // Get offset to first data point.
  int n, lut_entries = 1<<index_bits;

  if (num_points == 0)
    { // Curve is straight line from 0 to 1.
      float delta = 1.0F / ((float)(lut_entries-1));
      for (n=0; n < lut_entries; n++)
        lut[n] = n*delta;
    }
  else if (num_points == 1)
    { // Curve is a pure power law.
      read(val16,offset); offset += 2;
      float exponent = ((float) val16) / 256.0F;
      float delta = 1.0F / ((float)(lut_entries-1));
      for (n=0; n < lut_entries; n++)
        lut[n] = (float) pow(n*delta,exponent);
    }
  else
    {
      float lut_delta = ((float)(num_points-1)) / ((float)(lut_entries-1));
            // Holds the separation between lut entries, relative to that
            // between curve data points.
      float lut_pos = 0.0F;
      read(val16,offset); offset += 2;
      float last_val = ((float) val16) / ((float)((1<<16)-1));
      read(val16,offset); offset += 2;
      float next_val = ((float) val16) / ((float)((1<<16)-1));
      for (p=1, n=0; n < lut_entries; n++, lut_pos += lut_delta)
        {
          while (lut_pos > 1.0F)
            { // Need to advance points.
              last_val = next_val;
              lut_pos -= 1.0F;
              p++;
              if (p < num_points)
                {
                  read(val16,offset); offset += 2;
                  next_val = ((float) val16) / ((float)((1<<16)-1));
                }
            }
          lut[n] = next_val*lut_pos + last_val*(1.0F-lut_pos);
        }
    }
  assert ((offset-trc_offsets[channel_idx]) == (2*num_points+4));
  return true;
}

/*****************************************************************************/
/*                          j2_icc_profile::get_matrix                       */
/*****************************************************************************/

bool
  j2_icc_profile::get_matrix(float matrix3x3[])
{
  if ((num_colours != 3) || (!(profile_is_input || profile_is_display)) ||
      uses_3d_luts || !pcs_is_xyz)
    return false;

  for (int c=0; c < 3; c++)
    {
      int offset = colorant_offsets[c]; assert(offset > 128);
      assert(offset > 0);
      for (int t=0; t < 3; t++)
        {
          kdu_uint32 uval; read(uval,offset); offset += 4;
          kdu_int32 sval = (kdu_int32) uval;
          matrix3x3[c+3*t] = ((float) sval) / ((float)(1<<16));
        }
    }
  return true;
}

/*****************************************************************************/
/*                     j2_icc_profile::get_curve_data_offset                 */
/*****************************************************************************/

int
  j2_icc_profile::get_curve_data_offset(kdu_uint32 tag_offset,
                                        kdu_uint32 tag_length)
{
  if (((tag_length+tag_offset) > (kdu_uint32) num_buffer_bytes) ||
      ((tag_length+tag_offset) < tag_length)) // Second test checks overflow
    { KDU_ERROR(e,103); e <<
        KDU_TXT("Illegal tag offset or length value supplied in "
        "the JP2 embedded icc profile.");
    }
  kdu_uint32 val32=0; read(val32,tag_offset);
  if ((val32 != icc_curve_type) || (tag_length < 12))
    { KDU_ERROR(e,104); e <<
        KDU_TXT("Did not find a valid `curv' data type "
        "in the embedded ICC profile's tone reproduction curve tag.");
    }
  read(val32,tag_offset+4); read(val32,tag_offset+8);
  int num_points = (int) val32;
  if (tag_length != ((2*(kdu_uint32)num_points)+12))
    { KDU_ERROR(e,105); e <<
        KDU_TXT("The `curv' data type used to represent an embedded "
        "ICC profile's tone reproduction curve appears to have been "
        "truncated.");
    }
  return tag_offset+8;
}

/*****************************************************************************/
/*                     j2_icc_profile::get_xyz_data_offset                   */
/*****************************************************************************/

int
  j2_icc_profile::get_xyz_data_offset(kdu_uint32 tag_offset,
                                      kdu_uint32 tag_length)
{
  if (((tag_length+tag_offset) > (kdu_uint32) num_buffer_bytes) ||
      ((tag_length+tag_offset) < tag_length)) // Second test checks overflow
    { KDU_ERROR(e,106); e <<
        KDU_TXT("Illegal tag offset or length value supplied in "
        "JP2 embedded icc profile.");
    }
  kdu_uint32 val32=0; read(val32,tag_offset);
  if ((val32 != icc_xyz_type) || (tag_length < 20))
    { KDU_ERROR(e,107); e <<
        KDU_TXT("Did not find a valid `XYZ ' data type "
        "in the embedded ICC profile's colorant description tag.");
    }
  return tag_offset + 8;
}


/* ========================================================================= */
/*                                 j2_colour                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                           j2_colour::j2_colour                            */
/*****************************************************************************/

j2_colour::j2_colour()
{
  next = NULL;
  precedence = 0;
  approx = 0;
  initialized = false;
  num_colours = 0;
  icc_profile = NULL;
  vendor_buf_length = 0;
  vendor_buf = NULL;
  illuminant = 0;
  temperature = 0;
  for (int c=0; c < 3; c++)
    { 
      zeta[c] = 0.0f;
      precision[c] = range[c] = -1; // Means, value unknown.
      offset[c] = 0;
    }
}

/*****************************************************************************/
/*                           j2_colour::~j2_colour                           */
/*****************************************************************************/

j2_colour::~j2_colour()
{
  if (icc_profile != NULL)
    delete icc_profile;
  if (vendor_buf != NULL)
    delete[] vendor_buf;
}

/*****************************************************************************/
/*                           j2_colour::compare                              */
/*****************************************************************************/

bool
  j2_colour::compare(j2_colour *src)
{
  int c;

  if (!(initialized && src->initialized))
    return false;
  if ((space != src->space) || (num_colours != src->num_colours))
    return false;
  if ((space == JP2_CIELab_SPACE) || (space == JP2_CIEJab_SPACE))
    { // Other parameters must also agree
      for (c=0; c < num_colours; c++)
        if ((precision[c] <= 0) || (precision[c] != src->precision[c]) ||
            (offset[c] != src->offset[c]) || (range[c] != src->range[c]))
          return false;
      if ((space == JP2_CIELab_SPACE) &&
          ((illuminant!=src->illuminant) || (temperature!=src->temperature)))
        return false;
    }
  if ((space == JP2_iccLUM_SPACE) || (space == JP2_iccRGB_SPACE) ||
      (space == JP2_iccANY_SPACE))
    {
      assert((icc_profile != NULL) && (src->icc_profile != NULL));
      int pbuf_len, src_pbuf_len;
      kdu_byte *pbuf = icc_profile->get_profile_buf(&pbuf_len);
      kdu_byte *src_pbuf = src->icc_profile->get_profile_buf(&src_pbuf_len);
      if ((pbuf_len != src_pbuf_len) || (memcmp(pbuf,src_pbuf,pbuf_len) != 0))
        return false;
    }
  if (space == JP2_vendor_SPACE)
    { // Compare UUID and vendor data
      for (c=0; c < 16; c++)
        if (vendor_uuid[c] != src->vendor_uuid[c])
          return false;
      if ((vendor_buf_length != src->vendor_buf_length) ||
          (memcmp(vendor_buf,src->vendor_buf,vendor_buf_length) != 0))
        return false;
    }
  return true;
}

/*****************************************************************************/
/*                             j2_colour::copy                               */
/*****************************************************************************/

void
  j2_colour::copy(j2_colour *src)
{
  int c;
  if (icc_profile != NULL)
    { delete icc_profile; icc_profile = NULL; }
  if (vendor_buf != NULL)
    { delete[] vendor_buf; vendor_buf = NULL; vendor_buf_length = 0; }
  this->precedence = src->precedence;
  this->approx = src->approx;
  this->initialized = src->initialized;
  this->space = src->space;
  this->num_colours = src->num_colours;
  if (src->icc_profile != NULL)
    { 
      this->icc_profile = new j2_icc_profile;
      int num_bytes;
      kdu_byte *buf = src->icc_profile->get_profile_buf(&num_bytes);
      this->icc_profile->init(buf,num_bytes,false);
    }
  if (src->vendor_buf != NULL)
    {
      for (c=0; c < 16; c++)
        vendor_uuid[c] = src->vendor_uuid[c];
      vendor_buf_length = src->vendor_buf_length;
      vendor_buf = new kdu_byte[vendor_buf_length];
      memcpy(vendor_buf,src->vendor_buf,(size_t) vendor_buf_length);
    }
  for (c=0; c < 3; c++)
    { 
      this->zeta[c] = src->zeta[c];
      this->precision[c] = src->precision[c];
      this->range[c] = src->range[c];
      this->offset[c] = src->offset[c];
    }
  this->illuminant = src->illuminant;
  this->temperature = src->temperature;
}

/*****************************************************************************/
/*                             j2_colour::init                               */
/*****************************************************************************/

void
  j2_colour::init(jp2_input_box *colr)
{
  assert(colr->get_box_type() == jp2_colour_4cc);
  if (initialized || (icc_profile != NULL))
    assert(0); // Should not be possible.

  kdu_byte meth=0, prec_val=0, approx=0;
  if (!(colr->read(meth) && colr->read(prec_val) && colr->read(approx) &&
        (approx <= 4) && (meth >= 1) && (meth <= 4)))
    { KDU_ERROR(e,108); e <<
        KDU_TXT("Malformed colour description (colr) box found in "
        "JP2-family data source.  Insufficient fields, or illegal "
        "`approx' or `meth' field found in box.");
    }
  precedence = (int) prec_val;
  if (precedence & 0x80)
    precedence -= 256; // Value was communicated as 2's complement signed byte

  int c;

  for (c=0; c < 3; c++)
    { 
      range[c] = offset[c] = precision[c] = -1;
      zeta[c] = 0.0f; // Until proven otherwise
    }
  illuminant = 0; temperature = 0;

  if (meth == 1)
    { // Enumerated colour space
      kdu_uint32 enum_cs;
      if (!colr->read(enum_cs))
        { KDU_ERROR(e,109); e <<
            KDU_TXT("Malformed colour description (colr) box found in "
            "JP2-family data source.  Box appears to terminate prematurely.");
        }
      switch (enum_cs) {
        case 0:  space=JP2_bilevel1_SPACE;    num_colours=1; break;
        case 1:  space=JP2_YCbCr1_SPACE;      num_colours=3;
                 zeta[0] = 1.0f/16.0f;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 3:  space=JP2_YCbCr2_SPACE;      num_colours=3;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 4:  space=JP2_YCbCr3_SPACE;      num_colours=3;
                 zeta[0] = 1.0f/16.0f;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 9:  space=JP2_PhotoYCC_SPACE;    num_colours=3;
                 zeta[1] = 0.6094f;  zeta[2] = 0.5352f;      break;
        case 11: space=JP2_CMY_SPACE;         num_colours=3; break;
        case 12: space=JP2_CMYK_SPACE;        num_colours=4; break;
        case 13: space=JP2_YCCK_SPACE;        num_colours=4;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 14: space=JP2_CIELab_SPACE;      num_colours=3;
                 zeta[1] = 0.5f;  zeta[2] = 0.375f;          break;
        case 15: space=JP2_bilevel2_SPACE;    num_colours=1; break;
        case 16: space=JP2_sRGB_SPACE;        num_colours=3; break;
        case 17: space=JP2_sLUM_SPACE;        num_colours=1; break;
        case 18: space=JP2_sYCC_SPACE;        num_colours=3;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 19: space=JP2_CIEJab_SPACE;      num_colours=3;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 20: space=JP2_esRGB_SPACE;       num_colours=3;
                 zeta[0] = zeta[1] = zeta[2] = 0.375f;       break;
        case 21: space=JP2_ROMMRGB_SPACE;     num_colours=3; break;
        case 22: space=JP2_YPbPr60_SPACE;     num_colours=3;
                 zeta[0] = 1.0f/16.0f;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 23: space=JP2_YPbPr50_SPACE;     num_colours=3;
                 zeta[0] = 1.0f/16.0f;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        case 24: space=JP2_esYCC_SPACE;       num_colours=3;
                 zeta[1] = 0.5f;  zeta[2] = 0.5f;            break;
        default: // Unrecognized colour space.
          colr->close(); return; // Return without initializing the object
        }
    }
  else if ((meth == 2) || (meth == 3))
    { 
      int profile_bytes = (int) colr->get_remaining_bytes();
      kdu_byte *buf = new kdu_byte[profile_bytes];
      if (colr->read(buf,profile_bytes) != profile_bytes)
        { delete[] buf;
          KDU_ERROR(e,110); e <<
            KDU_TXT("JP2-family data source terminated unexpectedly "
            "inside the colour specification (colr) box.");
        }
      int check_bytes = 4;
      if (profile_bytes >= 4)
        { 
          check_bytes = buf[0];
          check_bytes = (check_bytes<<8) + buf[1];
          check_bytes = (check_bytes<<8) + buf[2];
          check_bytes = (check_bytes<<8) + buf[3];
        }
      if (profile_bytes < check_bytes)
        { delete[] buf;
          KDU_ERROR(e,0x23091302); e <<
          KDU_TXT("ICC profile embedded in JP2 colour description box "
                  "appears to have been truncated!");
        }
      icc_profile = new j2_icc_profile;
      icc_profile->init(buf,profile_bytes,true); // We are donating the buffer
      num_colours = icc_profile->get_num_colours();
      if (meth == 2)
        space = (num_colours==1)?JP2_iccLUM_SPACE:JP2_iccRGB_SPACE;
      else
        space = JP2_iccANY_SPACE;
    }
  else
    {
      assert(meth == 4);
      num_colours = 0;
      space = JP2_vendor_SPACE;
      if (colr->read(vendor_uuid,16) != 16)
        { KDU_ERROR(e,111); e <<
            KDU_TXT("JP2-family data source terminated unexpectedly "
            "inside the colour specification (colr) box.");
        }
      vendor_buf_length = (int) colr->get_remaining_bytes();
      if (vendor_buf != NULL)
        delete[] vendor_buf;
      vendor_buf = new kdu_byte[vendor_buf_length];
      colr->read(vendor_buf,vendor_buf_length);
    }

  if ((space == JP2_CIELab_SPACE) || (space == JP2_CIEJab_SPACE))
    {
      kdu_uint32 ep_data[7];
      int ep_len = (space==JP2_CIELab_SPACE)?7:6;
      for (c=0; c < ep_len; c++)
        if (!colr->read(ep_data[c]))
          break;
      if (c > 0)
        {
          if (c < ep_len)
            { KDU_ERROR(e,112); e <<
                KDU_TXT("JP2-family data source terminated "
                "unexpectedly; unable to read all EP parameter fields for "
                "CIELab or CIEJab enumerated colour space.");
            }
          for (c=0; c < 3; c++)
            {
              range[c] = (int) ep_data[2*c];
              offset[c] = (int) ep_data[2*c+1];
            }
          if (space == JP2_CIELab_SPACE)
            {
              illuminant = ep_data[6];
              if ((illuminant & JP2_CIE_DAY) == JP2_CIE_DAY)
                {
                  temperature = (kdu_uint16) illuminant;
                  illuminant = JP2_CIE_DAY;
                }
              else if (illuminant == JP2_CIE_D50)
                temperature = 5000;
              else if (illuminant == JP2_CIE_D65)
                temperature = 6500;
              else if (illuminant == JP2_CIE_D75)
                temperature = 7500;
              else
                temperature = 0;
            }
        }
    }
  initialized = true;

  if (!colr->close())
    { KDU_ERROR(e,113); e <<
        KDU_TXT("Malformed JP2 colour description (colr) box found in "
        "JP2-family data source.  The box appears to be too large.");
    }
}

/*****************************************************************************/
/*                            j2_colour::finalize                            */
/*****************************************************************************/

void
  j2_colour::finalize(j2_channels *channels)
{
  if (!initialized)
    { KDU_ERROR(e,114); e <<
        KDU_TXT("No colour description found in JP2-family data "
        "source, or provided for generating a JP2-family file!");
    }
  jp2_channels ifc(channels);
  if (num_colours == 0)
    num_colours = ifc.get_num_colours();
  if ((space == JP2_CIELab_SPACE) || (space == JP2_CIEJab_SPACE))
    {
      for (int c=0; c < num_colours; c++)
        { 
          int actual_precision = channels->get_bit_depth(c);
          if (precision[c] < 0)
            precision[c] = actual_precision;
          else if (precision[c] != actual_precision)
            {
              assert((space == JP2_CIELab_SPACE) ||
                     (space == JP2_CIEJab_SPACE));
              KDU_ERROR_DEV(e,115); e <<
                KDU_TXT("The sample precisions specified when "
                "initializing a `jp2_colour' object to represent a CIE Lab or "
                "Jab colour space do not agree with the actual precisions of "
                "the relevant codestream image components or palette "
                "lookup tables.");
            }
        }
    }

  if (space == JP2_CIELab_SPACE)
    { // See if we need to install Lab default offset/range parameters
      if (range[0] <= 0)
        {
          range[0] = 100; range[1] = 170; range[2] = 200;
          offset[0] = 0; offset[1] = (1<<precision[1])>>1;
          offset[2] = (1<<precision[2])>>2; offset[2] += offset[2]>>1;
        }
      if ((illuminant == 0) && (temperature == 0))
        illuminant = JP2_CIE_D50;
    }
  else if (space == JP2_CIEJab_SPACE)
    { // See if we need to install Jab default offset/range parameters
      if (range[0] <= 0)
        {
          range[0] = 0; range[1] = 255; range[2] = 255;
          offset[0] = 0; offset[1] = (1<<precision[1])>>1;
          offset[2] = (1<<precision[2])>>1;
        }
    }

  if ((space == JP2_CIELab_SPACE) || (space == JP2_CIEJab_SPACE))
    for (int c=0; c < 3; c++)
      { 
        zeta[c] = ((float) offset[c]) / kdu_pwrof2f(precision[c]);
        if (zeta[c] < 0.0f)
          zeta[c] = 0.0f;
        else if (zeta[c] > 0.75f)
          zeta[c] = 0.75f;
      }
}

/*****************************************************************************/
/*                            j2_colour::save_box                            */
/*****************************************************************************/

void
  j2_colour::save_box(jp2_output_box *super_box)
{
  assert(initialized);
  jp2_output_box colr;
  colr.open(super_box,jp2_colour_4cc);
  if (space == JP2_vendor_SPACE)
    {
      colr.write((kdu_byte) 4); // Method = 4
      colr.write((kdu_byte) precedence);
      colr.write((kdu_byte) approx);
      colr.write(vendor_uuid,16);
      colr.write(vendor_buf,vendor_buf_length);
    }
  else if ((space == JP2_iccLUM_SPACE) || (space == JP2_iccRGB_SPACE))
    { // Restricted ICC space
      colr.write((kdu_byte) 2); // Method = 2
      colr.write((kdu_byte) precedence);
      colr.write((kdu_byte) approx);
      int buf_bytes = 0;
      kdu_byte *buf = icc_profile->get_profile_buf(&buf_bytes);
      colr.write(buf,buf_bytes);
    }
  else if (space == JP2_iccANY_SPACE)
    { // Unrestricted ICC space
      colr.write((kdu_byte) 3); // Method = 3
      colr.write((kdu_byte) precedence);
      colr.write((kdu_byte) approx);
      int buf_bytes = 0;
      kdu_byte *buf = icc_profile->get_profile_buf(&buf_bytes);
      colr.write(buf,buf_bytes);
    }
  else
    { // Enumerated colour space.
      colr.write((kdu_byte) 1); // Method = 1
      colr.write((kdu_byte) precedence);
      colr.write((kdu_byte) approx);
      kdu_uint32 enum_cs = (kdu_uint32) space;
      colr.write(enum_cs);
      if ((space == JP2_CIELab_SPACE) || (space == JP2_CIEJab_SPACE))
        { // Write EP data
          int c, ep_len=6;
          kdu_uint32 ep_data[7];
          for (c=0; c < 3; c++)
            {
              assert((precision[c]>=0) && (offset[c]>=0) && (range[c]>=0));
                // Otherwise, we forgot to call `finalize'
              ep_data[2*c] = (kdu_uint32) range[c];
              ep_data[2*c+1] = (kdu_uint32) offset[c];
            }
          if (space == JP2_CIELab_SPACE)
            {
              ep_len = 7;
              ep_data[6] = illuminant;
              if (illuminant == JP2_CIE_DAY)
                ep_data[6] |= (kdu_uint32) temperature;
            }
          for (c=0; c < ep_len; c++)
            colr.write(ep_data[c]);
        }
    }
  colr.close();
}

/*****************************************************************************/
/*                        j2_colour::is_jp2_compatible                       */
/*****************************************************************************/

bool
  j2_colour::is_jp2_compatible()
{
  return (initialized &&
          ((space == JP2_sRGB_SPACE) || (space == JP2_sLUM_SPACE) ||
           (space == JP2_sYCC_SPACE) || (space == JP2_iccLUM_SPACE) ||
           (space == JP2_iccRGB_SPACE)));
}


/* ========================================================================= */
/*                                jp2_colour                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                             jp2_colour::copy                              */
/*****************************************************************************/

void
  jp2_colour::copy(jp2_colour src)
{
  if ((state == NULL) || (src.state == NULL))
    { 
      assert((state != NULL) && (src.state != NULL));
      return;
    }
  state->copy(src.state);
}

/*****************************************************************************/
/*                          jp2_colour::init (space)                         */
/*****************************************************************************/

void
  jp2_colour::init(jp2_colour_space space)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,116); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }

  int c;

  for (c=0; c < 3; c++)
    { 
      state->zeta[c] = 0;
      state->range[c] = state->offset[c] = -1;
    }
  state->illuminant = 0; state->temperature = 0;

  state->space = space;
  switch (space) {
    case JP2_bilevel1_SPACE:       state->num_colours=1;       break;
    case JP2_YCbCr1_SPACE:         state->num_colours=3;
         state->zeta[0] = 1.0f/16.0f;
         state->zeta[1] = 0.5f; state->zeta[2] = 0.5f;         break;
    case JP2_YCbCr2_SPACE:         state->num_colours=3;
         state->zeta[1] = 0.5f; state->zeta[2] = 0.5f;         break;
    case JP2_YCbCr3_SPACE:         state->num_colours=3;
         state->zeta[0] = 1.0f/16.0f;
         state->zeta[1] = 0.5f;    state->zeta[2] = 0.5f;      break;
    case JP2_PhotoYCC_SPACE:       state->num_colours=3;
         state->zeta[1] = 0.6094f; state->zeta[2] = 0.5352f;   break;
    case JP2_CMY_SPACE:            state->num_colours=3;       break;
    case JP2_CMYK_SPACE:           state->num_colours=4;       break;
    case JP2_YCCK_SPACE:           state->num_colours=4;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
      
    case JP2_CIELab_SPACE:         state->num_colours=3;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.375f;      break;
    case JP2_bilevel2_SPACE:       state->num_colours=1;       break;
    case JP2_sRGB_SPACE:           state->num_colours=3;       break;
    case JP2_sLUM_SPACE:           state->num_colours=1;       break;
    case JP2_sYCC_SPACE:           state->num_colours=3;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
    case JP2_CIEJab_SPACE:         state->num_colours=3;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
    case JP2_esRGB_SPACE:          state->num_colours=3;
         state->zeta[0]=state->zeta[1]=state->zeta[2]=0.375f;  break;
    case JP2_ROMMRGB_SPACE:        state->num_colours=3;       break;
    case JP2_YPbPr60_SPACE:        state->num_colours=3;
         state->zeta[0] = 1.0f/16.0f;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
    case JP2_YPbPr50_SPACE:        state->num_colours=3;
         state->zeta[0] = 1.0f/16.0f;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
    case JP2_esYCC_SPACE:          state->num_colours=3;
         state->zeta[1] = 0.5f;  state->zeta[2] = 0.5f;        break;
    default: // Unrecognized colour space.
      { KDU_ERROR_DEV(e,117); e <<
           KDU_TXT("Unrecognized colour space identifier supplied "
           "to `jp2_colour::init'.");
      }
    }
  state->initialized = true;
}

/*****************************************************************************/
/*                          jp2_colour::init (Lab or Jab)                    */
/*****************************************************************************/

void
  jp2_colour::init(jp2_colour_space space, int Lrange, int Loff, int Lbits,
                   int Arange, int Aoff, int Abits,
                   int Brange, int Boff, int Bbits,
                   kdu_uint32 illuminant, kdu_uint16 temperature)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,118); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }
  state->space = space;
  if ((space != JP2_CIELab_SPACE) && (space != JP2_CIEJab_SPACE))
    { KDU_ERROR_DEV(e,119); e <<
        KDU_TXT("The second form of the `jp2_colour::init' function "
        "may be used only to initialize an Lab or Jab colour description.  "
        "The supplied `space' argument is neither JP2_CIELab_SPACE nor "
        "JP2_CIEJab_SPACE, though.");
    }
  if ((illuminant == JP2_CIE_DAY) && (temperature == 5000))
    illuminant = JP2_CIE_D50; // Encourage use of the default
  state->num_colours = 3;
  state->precision[0] = Lbits;
  state->range[0]     = Lrange;
  state->offset[0]    = Loff;
  state->precision[1] = Abits;
  state->range[1]     = Arange;
  state->offset[1]    = Aoff;
  state->precision[2] = Bbits;
  state->range[2]     = Brange;
  state->offset[2]    = Boff;
  for (int c=0; c < 3; c++)
    { 
      state->zeta[c] = (((float) state->offset[c]) /
                        kdu_pwrof2f(state->precision[c]));
      if (state->zeta[c] < 0.0f)
        state->zeta[c] = 0.0f;
      else if (state->zeta[c] > 0.75f)
        state->zeta[c] = 0.75f;
    }  
  state->illuminant = illuminant;
  state->temperature = temperature;
  state->initialized = true;
}

/*****************************************************************************/
/*                         jp2_colour::init (profile)                        */
/*****************************************************************************/

void
  jp2_colour::init(const kdu_byte *profile_buf)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,120); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }
  j2_icc_profile tmp_profile;
  tmp_profile.init((kdu_byte *) profile_buf,INT_MAX,false);
      // Type cast is safe here, because we are not donating the buffer
  j2_icc_profile *heap_profile = new j2_icc_profile;
  heap_profile->init(tmp_profile.get_profile_buf(),INT_MAX,false);
  state->icc_profile = heap_profile;
  state->num_colours = heap_profile->get_num_colours();
  if (heap_profile->is_restricted())
    state->space = (state->num_colours==1)?JP2_iccLUM_SPACE:JP2_iccRGB_SPACE;
  else
    state->space = JP2_iccANY_SPACE;
  state->initialized = true;
}

/*****************************************************************************/
/*                         jp2_colour::init (vendor)                         */
/*****************************************************************************/

void
  jp2_colour::init(kdu_byte uuid[], int data_bytes, kdu_byte data[])
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,121); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }
  state->num_colours = 0; // We have no way of knowing.
  state->space = JP2_vendor_SPACE;
  for (int c=0; c < 16; c++)
    state->vendor_uuid[c] = uuid[c];
  state->vendor_buf_length = data_bytes;
  state->vendor_buf = new kdu_byte[data_bytes];
  memcpy(state->vendor_buf,data,(size_t) data_bytes);
  state->initialized = true;
}

/*****************************************************************************/
/*                      jp2_colour::init (monochrome ICC)                    */
/*****************************************************************************/

void
  jp2_colour::init(double gamma, double beta, int num_points)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,122); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }
  if (gamma == 1.0)
    num_points = 0; // Straight line.
  if (beta == 0.0)
    num_points = 1; // The pure exponent.
  else
    if (gamma < 1.0)
      { KDU_ERROR(e,123); e <<
          KDU_TXT("Currently can only construct profiles having "
          "gamma values greater than or equal to 1.0.");
      }

  int i;
  int body_offset = 128 + 4 + 12*4; // 4 tags in table
  int trc_offset = body_offset;
  int trc_length = 12+2*num_points;
  int trc_pad = (4-trc_length) & 3;
  int whitepoint_offset = trc_offset + trc_length + trc_pad;
  int whitepoint_length = 20;
  int copyright_offset = whitepoint_offset+whitepoint_length;
  const char *copyright_string = "Not copyrighted";
  int copyright_length = 8 + (int) strlen(copyright_string);
  int copyright_pad = (4-copyright_length) & 3;
  int description_offset = copyright_offset+copyright_length+copyright_pad;
  const char *desc_inv_string = "Kakadu Generated Profile";
  int desc_inv_length = (int) strlen(desc_inv_string) + 1;
  int description_length = 12 + desc_inv_length + 16 + 67;
  int description_pad = (4-description_length) & 3;
  int num_bytes = description_offset+description_length+description_pad;
  kdu_byte *buf = new kdu_byte[num_bytes];
  kdu_byte *bp = buf;

  // Write header
  store_big((kdu_uint32) num_bytes,bp); // Profile length
  store_big((kdu_uint32) 0,bp); // CMM signature field 0.
  store_big((kdu_uint32) 0x02200000,bp); // Profile version 2.2.0
  store_big(icc_input_device,bp); // Profile class -- could be display_device
  store_big(icc_gray_data,bp); // type of colour space
  store_big(icc_pcs_xyz,bp); // PCS signature
  store_big((kdu_uint16) 2001,bp); // Year number
  store_big((kdu_uint16) 1,bp); // Month number
  store_big((kdu_uint16) 1,bp); // Day number
  store_big((kdu_uint16) 0,bp); // Hour number
  store_big((kdu_uint16) 0,bp); // Minute number
  store_big((kdu_uint16) 0,bp); // Second number
  store_big(icc_file_signature,bp); // Standard file signature
  store_big((kdu_uint32) 0,bp); // Primary platform signature
  store_big((kdu_uint32) 0xC00000,bp); // Profile flags
  store_big((kdu_uint32) 0,bp); // Device manufacturer
  store_big((kdu_uint32) 0,bp); // Device model
  store_big((kdu_uint32) 0x80000000,bp); // Transparency (not reflective)
  store_big((kdu_uint32) 0,bp); // Reserved
  store_big((kdu_uint32) 0x00010000,bp); // Intent (relative colorimetry)
  store_big((kdu_uint32)(0.9642*(1<<16)+0.5),bp); // D50 whitepoint, X
  store_big((kdu_uint32)(1.0000*(1<<16)+0.5),bp); // D50 whitepoint, Y
  store_big((kdu_uint32)(0.8249*(1<<16)+0.5),bp); // D50 whitepoint, Z
  store_big((kdu_uint32) 0,bp); // Profile creator signature
  for (int f=0; f < 44; f++)
    *(bp++) = 0;
  assert((bp-buf) == 128);

  // Write tag table
  store_big((kdu_uint32) 4,bp); // There are four tags
  store_big(icc_gray_trc,bp);
  store_big((kdu_uint32) trc_offset,bp); // Offset to gray TRC curve
  store_big((kdu_uint32) trc_length,bp); // Size of gray TRC curve
  store_big(icc_media_white,bp);
  store_big((kdu_uint32) whitepoint_offset,bp);
  store_big((kdu_uint32) whitepoint_length,bp);
  store_big(icc_copyright,bp);
  store_big((kdu_uint32) copyright_offset,bp);
  store_big((kdu_uint32) copyright_length,bp);
  store_big(icc_profile_desc,bp);
  store_big((kdu_uint32) description_offset,bp);
  store_big((kdu_uint32) description_length,bp);

  // Write gray TRC curve
  store_big(icc_curve_type,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32) num_points,bp);
  if (num_points == 1)
    store_big((kdu_uint16)(gamma*256+0.5),bp);
  else
    {
      gamma = 1.0 / gamma; // Constructing inverse map back to linear space.
      assert(gamma < 1.0F);
      double x, y;
      double breakpoint = beta*gamma/(1.0F-gamma);
      double gradient =
        pow(breakpoint/(gamma*(1.0+beta)),1.0/gamma) / breakpoint;
      for (int n=0; n < num_points; n++)
        {
          x = ((double) n) / ((double)(num_points-1));
          if (x < breakpoint)
            y = x*gradient;
          else
            y = pow((x+beta)/(1.0+beta),1.0/gamma);
          store_big((kdu_uint16)(y*((1<<16)-1)),bp);
        }
    }
  while (trc_pad--) *(bp++) = 0;

  // Write media whitepoint tag
  store_big(icc_xyz_type,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32)(0.9642*(1<<16)+0.5),bp); // D50 whitepoint, X
  store_big((kdu_uint32)(1.0000*(1<<16)+0.5),bp); // D50 whitepoint, Y
  store_big((kdu_uint32)(0.8249*(1<<16)+0.5),bp); // D50 whitepoint, Z

  // Write copyright tag
  store_big(icc_text_type,bp);
  store_big((kdu_uint32) 0,bp);
  for (i=0; copyright_string[i] != '\0'; i++)
    *(bp++) = (kdu_byte)(copyright_string[i]);
  while (copyright_pad--) *(bp++) = 0;

  // Write description tag
  store_big(icc_profile_desc,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32) desc_inv_length,bp);
  strcpy((char *) bp,desc_inv_string); bp += desc_inv_length;
  store_big((kdu_uint32) 0,bp); // Unicode language = 0 (not localizable)
  store_big((kdu_uint32) 0,bp); // Unicode count (no unicode characters at all)
  store_big((kdu_uint32) 0,bp); // Scriptcode code = 0 (not localizable)
  store_big((kdu_uint32) 0,bp); // Scriptcode count = 0 (no script code)
  for (i=67; i > 0; i--)
    *(bp++) = 0; // Macintosh Scriptcode area filled with 0's.
  while (description_pad--) *(bp++) = 0;

  assert((bp-buf) == num_bytes);

  // Construct and install the profile.
  j2_icc_profile *heap_profile = new j2_icc_profile;
  heap_profile->init(buf,num_bytes,true);
  state->icc_profile = heap_profile;
  state->num_colours = 1;
  state->space = JP2_iccLUM_SPACE;
  state->initialized = true;
}

/*****************************************************************************/
/*                          jp2_colour::init (RGB ICC)                       */
/*****************************************************************************/

void
  jp2_colour::init(const double xy_red[], const double xy_green[],
                   const double xy_blue[], double gamma,
                   double beta, int num_points, bool reference_is_D50)
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return;
    }
  if (state->is_initialized())
    { KDU_ERROR_DEV(e,124); e <<
        KDU_TXT("Attempting to initialize a `jp2_colour' object "
        "which has already been initialized.");
    }
  if (num_points < 1)
    { gamma = 1.0; beta = 0.0; num_points = 0; }
  else if (gamma == 1.0)
    num_points = 0; // Straight line.
  if (beta == 0.0)
    num_points = 1; // The pure exponent.
  else
    if (gamma < 1.0)
      { KDU_ERROR(e,125); e <<
          KDU_TXT("Currently can only construct profiles having "
          "gamma values greater than or equal to 1.0.");
      }

  int i;
  int body_offset = 128 + 4 + 12*9; // Nine entries in tag table
  int trc_offset = body_offset;
  int trc_length = 12+2*num_points;
  int trc_pad = (4-trc_length) & 3;
  int xyz_offset = trc_offset + trc_length + trc_pad;
  int xyz_length = 20;
  int whitepoint_offset = xyz_offset + xyz_length*3;
  int whitepoint_length = 20;
  int copyright_offset = whitepoint_offset+whitepoint_length;
  const char *copyright_string = "Not copyrighted";
  int copyright_length = 8 + (int) strlen(copyright_string);
  int copyright_pad = (4-copyright_length) & 3;
  int description_offset = copyright_offset+copyright_length+copyright_pad;
  const char *desc_inv_string = "Kakadu Generated Profile";
  int desc_inv_length = (int) strlen(desc_inv_string) + 1;
  int description_length = 12 + desc_inv_length + 16 + 67;
  int description_pad = (4-description_length) & 3;
  int num_bytes = description_offset+description_length+description_pad;
  kdu_byte *buf = new kdu_byte[num_bytes];
  kdu_byte *bp = buf;
  // Write header
  store_big((kdu_uint32) num_bytes,bp); // Profile length
  store_big((kdu_uint32) 0,bp); // CMM signature field 0.
  store_big((kdu_uint32) 0x02200000,bp); // Profile version 2.2.0
  store_big(icc_input_device,bp); // Profile class -- could be display_device
  store_big(icc_rgb_data,bp); // type of colour space
  store_big(icc_pcs_xyz,bp); // PCS signature
  store_big((kdu_uint16) 2001,bp); // Year number
  store_big((kdu_uint16) 1,bp); // Month number
  store_big((kdu_uint16) 1,bp); // Day number
  store_big((kdu_uint16) 0,bp); // Hour number
  store_big((kdu_uint16) 0,bp); // Minute number
  store_big((kdu_uint16) 0,bp); // Second number
  store_big(icc_file_signature,bp); // Standard file signature
  store_big((kdu_uint32) 0,bp); // Primary platform signature
  store_big((kdu_uint32) 0xC00000,bp); // Profile flags
  store_big((kdu_uint32) 0,bp); // Device manufacturer
  store_big((kdu_uint32) 0,bp); // Device model
  store_big((kdu_uint32) 0x80000000,bp); // Transparency (not reflective)
  store_big((kdu_uint32) 0,bp); // Reserved
  store_big((kdu_uint32) 0x00010000,bp); // Intent (relative colorimetry)
  store_big((kdu_uint32)(0.9642*(1<<16)+0.5),bp); // D50 whitepoint, X
  store_big((kdu_uint32)(1.0000*(1<<16)+0.5),bp); // D50 whitepoint, Y
  store_big((kdu_uint32)(0.8249*(1<<16)+0.5),bp); // D50 whitepoint, Z
  store_big((kdu_uint32) 0,bp); // Profile creator signature
  for (int f=0; f < 44; f++)
    *(bp++) = 0;
  assert((bp-buf) == 128);

  // Write tag table
  store_big((kdu_uint32) 9,bp); // Six tags
  store_big(icc_red_trc,bp);
  store_big((kdu_uint32) trc_offset,bp); // Offset to the single TRC curve
  store_big((kdu_uint32) trc_length,bp);
  store_big(icc_green_trc,bp);
  store_big((kdu_uint32) trc_offset,bp); // Offset to the single TRC curve
  store_big((kdu_uint32) trc_length,bp);
  store_big(icc_blue_trc,bp);
  store_big((kdu_uint32) trc_offset,bp); // Offset to the single TRC curve
  store_big((kdu_uint32) trc_length,bp);
  store_big(icc_red_colorant,bp);
  store_big((kdu_uint32) xyz_offset,bp);
  store_big((kdu_uint32) xyz_length,bp);
  store_big(icc_green_colorant,bp);
  store_big((kdu_uint32) xyz_offset+xyz_length,bp);
  store_big((kdu_uint32) xyz_length,bp);
  store_big(icc_blue_colorant,bp);
  store_big((kdu_uint32) xyz_offset+xyz_length*2,bp);
  store_big((kdu_uint32) xyz_length,bp);
  store_big(icc_media_white,bp);
  store_big((kdu_uint32) whitepoint_offset,bp);
  store_big((kdu_uint32) whitepoint_length,bp);
  store_big(icc_copyright,bp);
  store_big((kdu_uint32) copyright_offset,bp);
  store_big((kdu_uint32) copyright_length,bp);
  store_big(icc_profile_desc,bp);
  store_big((kdu_uint32) description_offset,bp);
  store_big((kdu_uint32) description_length,bp);

  // Write the single TRC curve
  store_big(icc_curve_type,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32) num_points,bp);
  if (num_points == 1)
    store_big((kdu_uint16)(gamma*256+0.5),bp);
  else
    {
      gamma = 1.0 / gamma; // Constructing inverse map back to linear space.
      assert (gamma < 1.0);
      double x, y;
      double breakpoint = beta*gamma/(1.0-gamma);
      double gradient =
        pow(breakpoint/(gamma*(1.0+beta)),1.0/gamma) / breakpoint;
      for (int n=0; n < num_points; n++)
        {
          x = ((double) n) / ((double)(num_points-1));
          if (x < breakpoint)
            y = x*gradient;
          else
            y = pow((x+beta)/(1.0+beta),1.0/gamma);
          store_big((kdu_uint16)(y*((1<<16)-1)),bp);
        }
    }
  while (trc_pad--) *(bp++) = 0;

  // Generate and write the colorant XYZ values
  double primary_to_xyzD50[9];
  if (reference_is_D50)
    find_monitor_matrix(xy_red,xy_green,xy_blue,xy_D50_white,
                        primary_to_xyzD50);
  else
    {
      double xyzD65_to_xyzD50[9], primary_to_xyzD65[9], scratch[9];
      find_monitor_matrix(xy_red,xy_green,xy_blue,xy_D65_white,
                          primary_to_xyzD65);
      find_matrix_inverse(xyzD65_to_xyzD50,icc_xyzD50_to_xyzD65,3,scratch);
      find_matrix_product(primary_to_xyzD50,xyzD65_to_xyzD50,
                          primary_to_xyzD65,3);
    }
  for (int c=0; c < 3; c++)
    {
      store_big(icc_xyz_type,bp);
      store_big((kdu_uint32) 0,bp);
      for (int t=0; t < 3; t++)
        {
          kdu_int32 sval = (kdu_int32)
            (primary_to_xyzD50[c+t*3]*(1<<16)+0.5);
          store_big((kdu_uint32) sval,bp);
        }
    }
  
  // Write media whitepoint tag
  store_big(icc_xyz_type,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32)(0.9642*(1<<16)+0.5),bp); // D50 whitepoint, X
  store_big((kdu_uint32)(1.0000*(1<<16)+0.5),bp); // D50 whitepoint, Y
  store_big((kdu_uint32)(0.8249*(1<<16)+0.5),bp); // D50 whitepoint, Z

  // Write copyright tag
  store_big(icc_text_type,bp);
  store_big((kdu_uint32) 0,bp);
  for (i=0; copyright_string[i] != '\0'; i++)
    *(bp++) = (kdu_byte)(copyright_string[i]);
  while (copyright_pad--) *(bp++) = 0;

  // Write description tag
  store_big(icc_profile_desc,bp);
  store_big((kdu_uint32) 0,bp);
  store_big((kdu_uint32) desc_inv_length,bp);
  strcpy((char *) bp,desc_inv_string); bp += desc_inv_length;
  store_big((kdu_uint32) 0,bp); // Unicode language = 0 (not localizable)
  store_big((kdu_uint32) 0,bp); // Unicode count (no unicode characters at all)
  store_big((kdu_uint32) 0,bp); // Scriptcode code = 0 (not localizable)
  store_big((kdu_uint32) 0,bp); // Scriptcode count = 0 (no script code)
  for (i=67; i > 0; i--)
    *(bp++) = 0; // Macintosh Scriptcode area filled with 0's.
  while (description_pad--) *(bp++) = 0;

  assert((bp-buf) == num_bytes);

  // Construct and install the profile.
  j2_icc_profile *heap_profile = new j2_icc_profile;
  heap_profile->init(buf,num_bytes,true);
  state->icc_profile = heap_profile;
  state->num_colours = 3;
  state->space = JP2_iccRGB_SPACE;
  state->initialized = true;
}

/*****************************************************************************/
/*                         jp2_colour::get_num_colours                       */
/*****************************************************************************/

int
  jp2_colour::get_num_colours() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0;
    }
  return state->num_colours;
}

/*****************************************************************************/
/*                            jp2_colour::get_space                          */
/*****************************************************************************/

jp2_colour_space
  jp2_colour::get_space() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return JP2_bilevel1_SPACE; // equates to 0
    }
  return state->space;
}

/*****************************************************************************/
/*                        jp2_colour::is_opponent_space                      */
/*****************************************************************************/

bool
  jp2_colour::is_opponent_space() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  return ((state->space == JP2_YCbCr1_SPACE) ||
          (state->space == JP2_YCbCr2_SPACE) ||
          (state->space == JP2_YCbCr3_SPACE) ||
          (state->space == JP2_PhotoYCC_SPACE) ||
          (state->space == JP2_YCCK_SPACE) ||
          (state->space == JP2_CIELab_SPACE) ||
          (state->space == JP2_sYCC_SPACE) ||
          (state->space == JP2_CIEJab_SPACE) ||
          (state->space == JP2_YPbPr60_SPACE) ||
          (state->space == JP2_YPbPr50_SPACE) ||
          (state->space == JP2_esYCC_SPACE));
}

/*****************************************************************************/
/*                  jp2_colour::get_natural_unsigned_zero_point              */
/*****************************************************************************/

float jp2_colour::get_natural_unsigned_zero_point(int channel_idx) const
{
  if ((state == NULL) || (channel_idx < 0) || (channel_idx >= 3) ||
      (channel_idx >= state->num_colours))
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0.0f;
    }
  return state->zeta[channel_idx];
}

/*****************************************************************************/
/*                          jp2_colour::get_precedence                       */
/*****************************************************************************/

int
  jp2_colour::get_precedence() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0;
    }
  return state->precedence;
}

/*****************************************************************************/
/*                     jp2_colour::get_approximation_level                   */
/*****************************************************************************/

kdu_byte
  jp2_colour::get_approximation_level() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0;
    }
  return state->approx;
}

/*****************************************************************************/
/*                         jp2_colour::get_icc_profile                       */
/*****************************************************************************/

const kdu_byte *
  jp2_colour::get_icc_profile(int *num_bytes) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return NULL;
    }
  if (state->icc_profile == NULL)
    return NULL;
  return state->icc_profile->get_profile_buf(num_bytes);
}

/*****************************************************************************/
/*                         jp2_colour::get_icc_profile                       */
/*****************************************************************************/

int
  jp2_colour::get_icc_profile(kdu_byte buffer[], int buf_len) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return 0;
    }
  if (state->icc_profile == NULL)
    return 0;
  int profile_len = 0;
  kdu_byte *profile_buf = state->icc_profile->get_profile_buf(&profile_len);
  int copy_bytes = (buf_len < profile_len)?buf_len:profile_len;
  if ((buffer != NULL) && (copy_bytes > 0))
    memcpy(buffer,profile_buf,(size_t)copy_bytes);
  return profile_len;
}

/*****************************************************************************/
/*                         jp2_colour::get_lab_params                        */
/*****************************************************************************/

bool
  jp2_colour::get_lab_params(int &Lrange, int &Loff, int &Lbits,
                             int &Arange, int &Aoff, int &Abits,
                             int &Brange, int &Boff, int &Bbits,
                             kdu_uint32 &illuminant,
                             kdu_uint16 &temperature) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if (state->space != JP2_CIELab_SPACE)
    return false;
  Lrange = state->range[0];
  Arange = state->range[1];
  Brange = state->range[2];
  Loff = state->offset[0];
  Aoff = state->offset[1];
  Boff = state->offset[2];
  Lbits = state->precision[0];
  Abits = state->precision[1];
  Bbits = state->precision[2];
  illuminant = state->illuminant;
  temperature = state->temperature;
  return true;
}

/*****************************************************************************/
/*                         jp2_colour::get_jab_params                        */
/*****************************************************************************/

bool
  jp2_colour::get_jab_params(int &Lrange, int &Loff, int &Lbits,
                             int &Arange, int &Aoff, int &Abits,
                             int &Brange, int &Boff, int &Bbits) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if (state->space != JP2_CIEJab_SPACE)
    return false;
  Lrange = state->range[0];
  Arange = state->range[1];
  Brange = state->range[2];
  Loff = state->offset[0];
  Aoff = state->offset[1];
  Boff = state->offset[2];
  Lbits = state->precision[0];
  Abits = state->precision[1];
  Bbits = state->precision[2];
  return true;
}

/*****************************************************************************/
/*                       jp2_colour::check_cie_default                       */
/*****************************************************************************/

bool
  jp2_colour::check_cie_default() const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  int c, half[3];
  for (c=0; c < 3; c++)
    {
      if (state->precision[c] < 1)
        return false;
      half[c] = (1 << state->precision[c]) >> 1;
    }
  if (state->space == JP2_CIELab_SPACE)
    {
      if ((state->range[0] != 100) ||
          (state->range[1] != 170) ||
          (state->range[2] != 200))
        return false;
      if ((state->offset[0] != 0) ||
          (state->offset[1] != half[1]) ||
          (state->offset[2] != ((half[2]>>1)+(half[2]>>2))))
        return false;
      if (state->illuminant != JP2_CIE_D50)
        return false;
    }
  else if (state->space == JP2_CIEJab_SPACE)
    {
      if ((state->range[0] != 100) ||
          (state->range[1] != 255) ||
          (state->range[2] != 255))
        return false;
      if ((state->offset[0] != 0) ||
          (state->offset[1] != half[1]) ||
          (state->offset[2] != half[2]))
        return false;
    }
  else
    return false;
  return true;
}

/*****************************************************************************/
/*                        jp2_colour::get_vendor_uuid                        */
/*****************************************************************************/

bool
  jp2_colour::get_vendor_uuid(kdu_byte uuid[]) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return false;
    }
  if (state->space != JP2_vendor_SPACE)
    return false;
  for (int c=0; c < 16; c++)
    uuid[c] = state->vendor_uuid[c];
  return true;
}

/*****************************************************************************/
/*                        jp2_colour::get_vendor_data                        */
/*****************************************************************************/

const kdu_byte *
  jp2_colour::get_vendor_data(int *num_bytes) const
{
  if (state == NULL)
    { 
      assert(state != NULL); // Provoke failure in debug mode
      return NULL;
    }
  if (state->space != JP2_vendor_SPACE)
    return NULL;
  if (num_bytes != NULL)
    *num_bytes = state->vendor_buf_length;
  return state->vendor_buf;
}



/* ========================================================================= */
/*                            j2_colour_converter                            */
/* ========================================================================= */

# define FIX_POINT_HALF  (1<<(KDU_FIX_POINT-1))
# define FIX_POINT_UMAX  ((1<<KDU_FIX_POINT)-1)

/*****************************************************************************/
/*                 j2_colour_converter::j2_colour_converter                  */
/*****************************************************************************/

j2_colour_converter::j2_colour_converter(j2_colour *colour,
                                         bool use_wide_gamut,
                                         bool prefer_fast_approximations)
{
  int m, n, k;

  this->wide_gamut = use_wide_gamut;
  have_chroma = false; // Until proven otherwise
  lut_idx_bits = (wide_gamut)?(KDU_FIX_POINT+1):KDU_FIX_POINT;
  lut_primary_bits_f = 11;
  lut_idx_scale_f = ((float)(1<<lut_primary_bits_f)) - 1.0f;
  lut_entries_f=(wide_gamut)?(8<<lut_primary_bits_f):(1<<lut_primary_bits_f);
  num_conversion_colours = (colour->num_colours < 3)?1:3;
  for (m=0; m < 3; m++)
    { 
      tone_curves[m] = NULL;
      tone_curves_f[m] = NULL;
    }
  srgb_curve = NULL;
  srgb_curve_f = NULL;
  lum_curve = NULL;
  lum_curve_f = NULL;
  conversion_is_approximate = false;
  have_k = false;
  assert(colour->num_colours > 0);

  // Initialize the fixed parameters
  conversion_is_approximate = false;
  skip_opponent_transform = true;
  skip_primary_transform = true;
  skip_primary_matrix = true;
  use_ict = false;
  for (m=0; m < num_conversion_colours; m++)
    opponent_offset_f[m] = 0.0F;
  for (k=0, m=0; m < num_conversion_colours; m++)
    for (n=0; n < num_conversion_colours; n++, k++)
      opponent_matrix_f[k] = primary_matrix_f[k] = (m==n)?1.0F:0.0F;

  // Now go through each of the colour space options
  switch (colour->space) {
    case JP2_bilevel1_SPACE:
      {
        wide_gamut = false; lut_idx_bits = KDU_FIX_POINT;
        assert(lum_curve == NULL);
        lum_curve = new kdu_int16[1<<KDU_FIX_POINT];
        for (k=0; k < FIX_POINT_HALF; k++)
          lum_curve[k] = (kdu_int16)(FIX_POINT_HALF-1);
        for (; k < (1<<KDU_FIX_POINT); k++)
          lum_curve[k] = (kdu_int16)(-FIX_POINT_HALF);
      } break;
    case JP2_YCbCr1_SPACE:
      { 
        configure_YCbCr_transform(16.0/256.0, 219.0/256.0,
                                  0.0, 224.0/256.0, 0.0, 224.0/256.0);
        if (prefer_fast_approximations)
          use_ict = conversion_is_approximate = true;
        else
          configure_d65_primary_transform(xy_709_red,xy_709_green,xy_709_blue,
                                          1.0/0.45, 0.099);
      } break;
    case JP2_YCbCr2_SPACE:
      {
        configure_YCbCr_transform(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
        use_ict = true; // Above is simply the ICT from JPEG2000 Part 1.
        if (prefer_fast_approximations)
          conversion_is_approximate = true;
        else
          configure_d65_primary_transform(xy_601_red,xy_601_green,xy_601_blue,
                                          1.0/0.45, 0.099);
      } break;
    case JP2_YCbCr3_SPACE:
      {
        configure_YCbCr_transform(16.0/256.0, 219.0/256.0,
                                  0.0, 224.0/256.0, 0.0, 224.0/256.0);
        if (prefer_fast_approximations)
          use_ict = conversion_is_approximate = true;
        else
          configure_d65_primary_transform(xy_601_red,xy_601_green,xy_601_blue,
                                          1.0/0.45, 0.099);
      } break;
    case JP2_PhotoYCC_SPACE:
      {
        configure_YCbCr_transform(0.0,0.7133, 0.1094,0.7711, 0.0352,0.7428);
        configure_d65_primary_transform(xy_709_red,xy_709_green,xy_709_blue,
                                        1.0/0.45, 0.099);
      } break;
    case JP2_sYCC_SPACE:
      {
        configure_YCbCr_transform(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
        use_ict = true; // Above is simply the ICT from JPEG2000 Part 1.
      } break;
    case JP2_CIELab_SPACE:
      {
        if (!configure_lab_transform(colour))
          num_conversion_colours = 0;
      } break;
    case JP2_bilevel2_SPACE:
      {
        wide_gamut = false; lut_idx_bits = KDU_FIX_POINT;
        assert(lum_curve == NULL);
        lum_curve = new kdu_int16[1<<KDU_FIX_POINT];
        for (k=0; k < FIX_POINT_HALF; k++)
          lum_curve[k] = (kdu_int16)(-FIX_POINT_HALF);
        for (; k < (1<<KDU_FIX_POINT); k++)
          lum_curve[k] = (kdu_int16)(FIX_POINT_HALF-1);
      } break;
    case JP2_CMY_SPACE:
      {
        for (m=0; m < 3; m++)
          opponent_matrix_f[m*3+m] = -1.0F;
        skip_opponent_transform = false;
        conversion_is_approximate = true;
      } break;
    case JP2_CMYK_SPACE:
      {
        for (m=0; m < 3; m++)
          opponent_matrix_f[m*3+m] = -1.0F;
        skip_opponent_transform = false;
        conversion_is_approximate = true;
        have_k = true;
      } break;
    case JP2_YCCK_SPACE:
      {
        configure_YCbCr_transform(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
        use_ict = true; // Above is simply the ICT from JPEG2000 Part 1.
        conversion_is_approximate = true;
      } break;
    case JP2_sRGB_SPACE:
      break; // No conversion required at all
    case JP2_sLUM_SPACE:
      break; // No conversion required at all
    case JP2_CIEJab_SPACE:
      { // Don't know how to convert this one yet
        num_conversion_colours = 0;
      } break;
    case JP2_esRGB_SPACE:
      {
        for (m=0; m < 3; m++)
          {
            opponent_offset_f[m] = -0.125F;
            opponent_matrix_f[m*3+m] = 2.0F;
          }
        skip_opponent_transform = false;
      } break;
    case JP2_ROMMRGB_SPACE:
      {
        double xy_romm_red[2] =   {0.7347,0.2653};
        double xy_romm_green[2] = {0.1596,0.8404};
        double xy_romm_blue[2] =  {0.0366,0.0001};
        configure_d65_primary_transform(xy_romm_red,xy_romm_green,xy_romm_blue,
                                        1.8,0.0);
          // The above call is used only to set up the forward and inverse
          // gamma functions.  The primary transform which it generates is
          // incorrect (although we have provided the correct primaries) since
          // ROMM primaries are referenced to D50, not D65.  We build the
          // correct primary transform explicitly below.
        assert(!skip_primary_matrix);
        double srgb_to_xyzD65[9], romm_to_xyzD50[9], xyzD65_to_srgb[9];
        double mat1[9], mat2[9];
        find_monitor_matrix(xy_709_red,xy_709_green,xy_709_blue,xy_D65_white,
                            srgb_to_xyzD65);
        find_monitor_matrix(xy_romm_red,xy_romm_green,xy_romm_blue,
                            xy_D50_white,romm_to_xyzD50);
        find_matrix_inverse(xyzD65_to_srgb,srgb_to_xyzD65,3,mat1);
        find_matrix_product(mat1,xyzD65_to_srgb,icc_xyzD50_to_xyzD65,3);
        find_matrix_product(mat2,mat1,romm_to_xyzD50,3);
        for (n=0; n < 9; n++)
          primary_matrix_f[n] = (float) mat2[n];
      } break;
    case JP2_YPbPr60_SPACE: // Treat both YPbPr60 and YPbPr50 the same way
    case JP2_YPbPr50_SPACE:
      { // Not 100% confident about this one
        configure_YPbPr_transform(16.0/256.0, 219.0/256.0,
                                  0.0, 224.0/256.0, 0.0, 224.0/256.0);
        if (prefer_fast_approximations)
          conversion_is_approximate = true;
        else
          configure_d65_primary_transform(xy_240m_red,xy_240m_green,
                                          xy_240m_blue,1.0/0.45, 0.099);
      } break;
    case JP2_esYCC_SPACE:
      {
        configure_YCbCr_transform(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
        for (m=0; m < 3; m++)
          for (n=1; n < 3; n++)
            opponent_matrix_f[m*3+n] *= 2.0F;
      } break;
    case JP2_iccLUM_SPACE:
    case JP2_iccRGB_SPACE:
    case JP2_iccANY_SPACE:
      {
        if (!configure_icc_primary_transform(colour))
          num_conversion_colours = 0;
      } break;
    case JP2_vendor_SPACE:
      { // No way we can possibly convert this
        num_conversion_colours = 0;
      } break;
    default:
      num_conversion_colours = 0;
    }

  if (num_conversion_colours == 3)
    { // Finish off by creating the fixed-point versions of the various
      // floating point arrays.
      for (m=0; m < 3; m++)
        opponent_offset[m] = (kdu_int32)
          floor(0.5 + opponent_offset_f[m]*(1<<KDU_FIX_POINT));
      for (m=0; m < 9; m++)
        opponent_matrix[m] = (kdu_int32)
          floor(0.5 + opponent_matrix_f[m]*(1<<12));
      for (m=0; m < 9; m++)
        primary_matrix[m] = (kdu_int32)
          floor(0.5 + primary_matrix_f[m]*(1<<12));
    }
}

/*****************************************************************************/
/*                j2_colour_converter::~j2_colour_converter                  */
/*****************************************************************************/

j2_colour_converter::~j2_colour_converter()
{
  for (int c=0; c < 3; c++)
    { 
      if (tone_curves[c] != NULL)
        { 
          delete[] tone_curves[c];
          tone_curves[c] = NULL;
        }
      if (tone_curves_f[c] != NULL)
        { 
          delete[] tone_curves_f[c];
          tone_curves_f[c] = NULL;
        }
    }
  
  if (srgb_curve != NULL)
    {
      delete[] srgb_curve;
      srgb_curve = NULL;
    }
  if (srgb_curve_f != NULL)
    { 
      delete[] srgb_curve_f;
      srgb_curve_f = NULL;
    }
  
  if (lum_curve != NULL)
    {
      delete[] lum_curve;
      lum_curve = NULL;
    }
  if (lum_curve_f != NULL)
    { 
      delete[] lum_curve_f;
      lum_curve_f = NULL;
    }
}

/*****************************************************************************/
/*              j2_colour_converter::configure_YCbCr_transform               */
/*****************************************************************************/

#define ALPHA_R 0.299 // These are exact expressions from which the
#define ALPHA_G 0.587 // ICT forward and reverse transform coefficients
#define ALPHA_B 0.114 // may be expressed.

void
  j2_colour_converter::configure_YCbCr_transform(double yoff, double yscale,
                  double cboff, double cbscale, double croff, double crscale)
{
  opponent_offset_f[0] = (float)(-yoff + 0.5 - 0.5*yscale);
  opponent_offset_f[1] = (float)(-cboff);
  opponent_offset_f[2] = (float)(-croff);
  opponent_matrix_f[0] = (float)(1/yscale);
  opponent_matrix_f[1] = 0.0F;
  opponent_matrix_f[2] = (float)(2*(1-ALPHA_R)/crscale);
  opponent_matrix_f[6] = (float)(1/yscale);
  opponent_matrix_f[7] = (float)(2*(1-ALPHA_B)/cbscale);
  opponent_matrix_f[8] = 0.0F;
  opponent_matrix_f[3] = (float)(1/yscale);
  opponent_matrix_f[4] = (float)(-2*ALPHA_B*(1-ALPHA_B)/ALPHA_G/cbscale);
  opponent_matrix_f[5] = (float)(-2*ALPHA_R*(1-ALPHA_R)/ALPHA_G/crscale);
  skip_opponent_transform = false;
  have_chroma = true;
}

/*****************************************************************************/
/*              j2_colour_converter::configure_YPbPr_transform               */
/*****************************************************************************/

#define ALPHA240_R 0.2122 // These are exact expressions from which the
#define ALPHA240_G 0.7013 // SMPTE-240M YPbPr forward and reverse transform
#define ALPHA240_B 0.0865 // coefficients may be expressed.

void
  j2_colour_converter::configure_YPbPr_transform(double yoff, double yscale,
                  double cboff, double cbscale, double croff, double crscale)
{
  opponent_offset_f[0] = (float)(-yoff + 0.5 - 0.5*yscale);
  opponent_offset_f[1] = (float)(-cboff);
  opponent_offset_f[2] = (float)(-croff);
  opponent_matrix_f[0] = (float)(1/yscale);
  opponent_matrix_f[1] = 0.0F;
  opponent_matrix_f[2] = (float)(2*(1-ALPHA240_R)/crscale);
  opponent_matrix_f[6] = (float)(1/yscale);
  opponent_matrix_f[7] = (float)(2*(1-ALPHA240_B)/cbscale);
  opponent_matrix_f[8] = 0.0F;
  opponent_matrix_f[3] = (float)(1/yscale);
  opponent_matrix_f[4] = (float)
    (-2*ALPHA240_B*(1-ALPHA240_B)/ALPHA240_G/cbscale);
  opponent_matrix_f[5] = (float)
    (-2*ALPHA240_R*(1-ALPHA240_R)/ALPHA240_G/crscale);
  skip_opponent_transform = false;
  have_chroma = true;
}

/*****************************************************************************/
/*           j2_colour_converter::configure_d65_primary_transform            */
/*****************************************************************************/

void
  j2_colour_converter::configure_d65_primary_transform(const double xy_red[],
                           const double xy_green[], const double xy_blue[],
                           double gamma, double beta)
{
  int m, n, k;
  double srgb_to_xyz[9], prim_to_xyz[9], xyz_to_srgb[9], mat[9];

  find_monitor_matrix(xy_709_red,xy_709_green,xy_709_blue,xy_D65_white,
                      srgb_to_xyz);
  find_monitor_matrix(xy_red,xy_green,xy_blue,xy_D65_white,prim_to_xyz);
  find_matrix_inverse(xyz_to_srgb,srgb_to_xyz,3,mat);
       // Sets `xyz_to_srgb' = inv(`srgb_to_xyz').
  find_matrix_product(mat,xyz_to_srgb,prim_to_xyz,3);
       // Sets `mat' = `xyz_to_srgb' * `prim_to_xyz'.

  // Copy primary conversion matrix and check for the identity
  skip_primary_matrix = true;
  for (k=0, m=0; m < 3; m++)
    for (n=0; n < 3; n++, k++)
      {
        primary_matrix_f[k] = (float) mat[k];
        double diff = ((m==n)?1.0:0.0) - mat[k];
        if ((diff < -0.01) || (diff > 0.01))
          skip_primary_matrix = false;
      }

  // At this point, if `skip_primary_matrix' is true, we will do everything
  // with the tone curves.
  gamma = 1.0 / gamma; // Constructing inverse map back to linear space
  assert(gamma < 1.0);
  double x, y;
  double breakpoint = beta*gamma/(1.0-gamma);
  double gradient = (breakpoint <= 0.0)?0.0:
    (pow(breakpoint/(gamma*(1.0+beta)),1.0/gamma) / breakpoint);

  double sbeta = 0.055, sgamma = 2.4;
  double epsilon = pow(sbeta/((1.0+sbeta)*(1.0-1.0/sgamma)),sgamma);
  double g = sbeta/(epsilon*(sgamma-1.0));

  // Low precision LUT's first
  int lut_indices = (1<<lut_idx_bits);
  assert((srgb_curve == NULL) && (tone_curves[0] == NULL) &&
         (tone_curves[1] == NULL) && (tone_curves[2] == NULL));
  tone_curves[0] = new kdu_int16[lut_indices];
  if (!skip_primary_matrix)
    srgb_curve = new kdu_int16[lut_indices];
  for (n=0; n < lut_indices; n++)
    { 
      x = ((double) n) / FIX_POINT_UMAX;
      if (x < breakpoint)
        y = x*gradient;
      else
        y = pow((x+beta)/(1.0+beta),1.0/gamma);
      
      if (skip_primary_matrix)
        { // Fold the sRGB tone curve into this
          if (y <= epsilon)
            y *= g;
          else
            y = (1.0+sbeta)*pow(y,1.0/sgamma) - sbeta;
          y -= 0.5; // Make it a signed quantity.
          int val = (int) floor(y*FIX_POINT_UMAX+0.5);
          val = (val < -0x8000)?-0x8000:val;
          val = (val >  0x7FFF)? 0x7FFF:val;
          (tone_curves[0])[n] = (kdu_int16) val;
        }
      else
        { 
          int val = (int) floor(y*FIX_POINT_UMAX+0.5);
          val = (val < -0x8000)?-0x8000:val;
          val = (val >  0x7FFF)? 0x7FFF:val;
          (tone_curves[0])[n] = (kdu_int16) val;
          
          if (x <= epsilon)
            y = x * g;
          else
            y = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          y -= 0.5; // Make it a signed quantity.
          val = (int) floor(y*FIX_POINT_UMAX+0.5);
          val = (val < -0x8000)?-0x8000:val;
          val = (val >  0x7FFF)? 0x7FFF:val;
          srgb_curve[n] = (kdu_int16) val;
        }
    }
  
  // Now for the floating point LUT's
  assert((srgb_curve_f == NULL) && (tone_curves_f[0] == NULL) &&
         (tone_curves_f[1] == NULL) && (tone_curves_f[2] == NULL));
  tone_curves_f[0] = new float[lut_entries_f+1];
  if (!skip_primary_matrix)
    srgb_curve_f = new float[lut_entries_f+1];
  double inv_scale_f = 1.0 / lut_idx_scale_f;
  for (n=0; n < lut_entries_f; n++)
    { 
      x = ((double) n) * inv_scale_f;
      if (x < breakpoint)
        y = x*gradient;
      else
        y = pow((x+beta)/(1.0+beta),1.0/gamma);
      
      if (skip_primary_matrix)
        { // Fold the sRGB tone curve into this
          if (y <= epsilon)
            y *= g;
          else
            y = (1.0+sbeta)*pow(y,1.0/sgamma) - sbeta;
          y -= 0.5; // Make it a signed quantity.
          (tone_curves_f[0])[n] = (float)y;
        }
      else
        { 
          (tone_curves_f[0])[n] = (float)y;
     
          if (x <= epsilon)
            y = x * g;
          else
            y = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          y -= 0.5; // Make it a signed quantity.
          srgb_curve_f[n] = (float)y;
        }
    }
  
  // Fill the extra entry in each LUT with a replica of the last valid one
  (tone_curves_f[0])[n] = (tone_curves_f[0])[n-1];
  if (!skip_primary_matrix)
    srgb_curve_f[n] = srgb_curve_f[n-1];

  skip_primary_transform = false;
}

/*****************************************************************************/
/*           j2_colour_converter::configure_icc_primary_transform            */
/*****************************************************************************/

bool
  j2_colour_converter::configure_icc_primary_transform(j2_colour *colour)
{
  if (colour->icc_profile == NULL)
    return false;

  float tmp_buf[1<<KDU_FIX_POINT];
  double sbeta = 0.055, sgamma = 2.4;
  double epsilon = pow(sbeta/((1.0+sbeta)*(1.0-1.0/sgamma)),sgamma);
  double g = sbeta/(epsilon*(sgamma-1.0));
  int n, c, lut_indices = 1<<lut_idx_bits;
  
  if (colour->num_colours == 1)
    { // Convert linear data to sLUM data by applying sRGB gamma function
      
      // Start with the low precision curves
      assert(lum_curve == NULL);
      lum_curve = new kdu_int16[lut_indices];
      if (!colour->icc_profile->get_lut(0,tmp_buf,KDU_FIX_POINT))
        return false;
      for (n=0; n < lut_indices; n++)
        { 
          double x = (n < FIX_POINT_UMAX)?tmp_buf[n]:tmp_buf[FIX_POINT_UMAX];
          if (x <= epsilon)
            x *= g;
          else
            x = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          x -= 0.5; // Make it a signed quantity.
          int val = (int) floor(x*FIX_POINT_UMAX+0.5);
          val = (val < -0x8000)?-0x8000:val;
          val = (val >  0x7FFF)? 0x7FFF:val;
          lum_curve[n] = (kdu_int16) val;
        }

      // Now for the higher precision curves 
      assert(lum_curve_f == NULL);
      lum_curve_f = new float[lut_entries_f+1];
      if (!colour->icc_profile->get_lut(0,lum_curve_f,lut_primary_bits_f))
        return false; // Should not be possible
      int primary_entries_f = 1<<lut_primary_bits_f;
      for (n=0; n < primary_entries_f; n++)
        { 
          double x = lum_curve_f[n];
          if (x <= epsilon)
            x *= g;
          else
            x = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          x -= 0.5; // Make it a signed quantity.
          lum_curve_f[n] = (float) x;
        }
      float last_val = lum_curve_f[n-1];
      for (; n <= lut_entries_f; n++) // <= because we have an extra entry
        lum_curve_f[n] = last_val;
    }
  else if (colour->num_colours == 3)
    { 
      skip_primary_transform = skip_primary_matrix = false;

      // First fill in the 3x3 matrix entries
      if (!colour->icc_profile->get_matrix(primary_matrix_f))
        return false;
      double scratch[9];
      double srgb_to_xyzD65[9]; // Transform from linear sRGB to D65 XYZ
      find_monitor_matrix(xy_709_red,xy_709_green,xy_709_blue,
                          xy_D65_white,srgb_to_xyzD65);
      double xyzD65_to_srgb[9]; // Inverse of above
      find_matrix_inverse(xyzD65_to_srgb,srgb_to_xyzD65,3,scratch);
      double xyzD50_to_srgb[9];
      find_matrix_product(xyzD50_to_srgb,xyzD65_to_srgb,
                          icc_xyzD50_to_xyzD65,3);
      double mat_out[9], mat_in[9];
      for (n=0; n < 9; n++)
        mat_in[n] = (double) primary_matrix_f[n];
      find_matrix_product(mat_out,xyzD50_to_srgb,mat_in,3);
      for (n=0; n < 9; n++)
        primary_matrix_f[n] = (float) mat_out[n];
        
      // Now build the source lookup tables to make channel data linear
      for (c=0; c < 3; c++)
        { 
          // Start with the low precision curves
          assert(tone_curves[c] == NULL);
          tone_curves[c] = new kdu_int16[lut_indices];
          if (!colour->icc_profile->get_lut(c,tmp_buf,KDU_FIX_POINT))
            return false;
          for (n=0; n < lut_indices; n++)
            if (n < FIX_POINT_UMAX)
              (tone_curves[c])[n] = (kdu_int16)
                floor(tmp_buf[n]*FIX_POINT_UMAX+0.5);
            else
              (tone_curves[c])[n] = (kdu_int16)
                floor(tmp_buf[FIX_POINT_UMAX]*FIX_POINT_UMAX+0.5);
        
          // Now for the higher precision curves
          assert(tone_curves_f[c] == NULL);
          tone_curves_f[c] = new float[lut_entries_f+1];
          if (!colour->icc_profile->get_lut(0,tone_curves_f[c],
                                            lut_primary_bits_f))
            return false; // Should not be possible
          int primary_entries_f = 1<<lut_primary_bits_f;
          n = primary_entries_f;
          float last_val = (tone_curves_f[c])[n-1];
          for (; n <= lut_entries_f; n++) // <= because we have an extra entry
            (tone_curves_f[c])[n] = last_val;
        }

      // Fill in the `srgb_curve' lookup table
      assert(srgb_curve == NULL);
      srgb_curve = new kdu_int16[lut_indices];
      for (n=0; n < lut_indices; n++)
        { 
          double x = ((double) n) / FIX_POINT_UMAX;
          if (x <= epsilon)
            x *= g;
          else
            x = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          x -= 0.5; // Make it a signed quantity.
          int val = (int) floor(x*FIX_POINT_UMAX+0.5);
          val = (val < -0x8000)?-0x8000:val;
          val = (val >  0x7FFF)? 0x7FFF:val;
          srgb_curve[n] = (kdu_int16) val;
        }
    
      // Finally, fill in the `srgb_curve_f' high precision table
      assert(srgb_curve_f == NULL);
      srgb_curve_f = new float[lut_entries_f+1];
      double inv_scale_f = 1.0 / lut_idx_scale_f;
      for (n=0; n < lut_entries_f; n++)
        { 
          double x = ((double) n) * inv_scale_f;
          if (x <= epsilon)
            x *= g;
          else
            x = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
          x -= 0.5; // Make it a signed quantity.
          srgb_curve_f[n] = (float)x;
        }
      srgb_curve_f[n] = srgb_curve_f[n-1]; // Last entry is a replica
    }

  return true;
}

/*****************************************************************************/
/*               j2_colour_converter::configure_lab_transform                */
/*****************************************************************************/

bool
  j2_colour_converter::configure_lab_transform(j2_colour *colour)
{
  if (colour->space != JP2_CIELab_SPACE)
    return false;
  kdu_uint16 temperature = colour->temperature;
  if (colour->illuminant == JP2_CIE_D50)
    temperature = 5000;
  else if (colour->illuminant == JP2_CIE_D65)
    temperature = 6500;
  else if ((colour->illuminant != JP2_CIE_DAY) ||
           ((temperature != 5000) && (temperature != 6500)))
    return false;
  if ((colour->range[0] < 1) || (colour->range[1] < 1) ||
      (colour->range[2] < 1) || (colour->precision[0] < 1) ||
      (colour->precision[1] < 1) || (colour->precision[2] < 1))
    return false; // Insufficient range/precision information.

  int n;
  skip_opponent_transform =
    skip_primary_transform = skip_primary_matrix = false;
  for (n=0; n < 3; n++)
    opponent_offset_f[n] = 0.5F - 
      ((float) colour->offset[n]) / (float)((1<<colour->precision[n])-1);
            // The above offsets convert the channel data to scaled L, a*, b*
  double fwd[9] =
    {0.0,                     100.0/colour->range[0],    0.0,
     431.0/colour->range[1], -431.0/colour->range[1],    0.0,
     0.0,                     172.4/colour->range[2], -172.4/colour->range[2]};
      // The above matrix takes unsigned gamma-corrected XYZ (in the range 0
      // to 1) to scaled L, a*, b*
  for (n=0; n < 3; n++)
    opponent_offset_f[n] -= 0.5F * (float)(fwd[3*n+0]+fwd[3*n+1]+fwd[3*n+2]);
      // The above operation subtracts the vector, fwd * (0.5 0.5 0.5)', from
      // `opponent_offset_f', which ensures that after application of
      // inv(fwd) to the scaled channel data, we will obtain signed versions of
      // the gamma corrected X, Y and Z values, in the range -0.5 to +0.5.
  double scratch[9], inv_mat[9];
  find_matrix_inverse(inv_mat,fwd,3,scratch);
  for (n=0; n < 9; n++)
    opponent_matrix_f[n] = (float) inv_mat[n];

  // Construct the inverse map back to linear X, Y, Z
  double gamma=3.0, beta=0.16;
  gamma = 1.0 / gamma; // Inverting the forward gamma function
  assert(gamma < 1.0);
  double x, y;
  double breakpoint = beta*gamma/(1.0-gamma);
  double gradient = pow(breakpoint/(gamma*(1.0+beta)),1.0/gamma) / breakpoint;

  // Low precision curve first
  int lut_indices = 1<<lut_idx_bits;
  assert(tone_curves[0] == NULL);
  tone_curves[0] = new kdu_int16[lut_indices];
  for (n=0; n < lut_indices; n++)
    { 
      x = ((double) n) / FIX_POINT_UMAX;
      if (x < breakpoint)
        y = x*gradient;
      else
        y = pow((x+beta)/(1.0+beta),1.0/gamma);
      int val = (int) floor(y*FIX_POINT_UMAX+0.5);
      val = (val < -0x8000)?-0x8000:val;
      val = (val >  0x7FFF)? 0x7FFF:val;
      (tone_curves[0])[n] = (kdu_int16) val;
    }
  
  // Now for the high precision tone curve
  assert(tone_curves_f[0] == NULL);
  tone_curves_f[0] = new float[lut_entries_f+1];
  double inv_scale_f = 1.0 / lut_idx_scale_f;
  for (n=0; n < lut_entries_f; n++)
    { 
      x = ((double) n) * inv_scale_f;
      if (x < breakpoint)
        y = x*gradient;
      else
        y = pow((x+beta)/(1.0+beta),1.0/gamma);
      (tone_curves_f[0])[n] = (float)y;
    }
  (tone_curves_f[0])[n] = (tone_curves_f[0])[n-1]; // Extra entry is a replica

  // Construct the forward mapping from linear sRGB to sRGB
  double sbeta = 0.055, sgamma = 2.4;
  double epsilon = pow(sbeta/((1.0+sbeta)*(1.0-1.0/sgamma)),sgamma);
  double g = sbeta/(epsilon*(sgamma-1.0));

  // Low precision curve first
  assert(srgb_curve == NULL);
  srgb_curve = new kdu_int16[lut_indices];
  for (n=0; n < lut_indices; n++)
    { 
      x = ((double) n) / FIX_POINT_UMAX;
      if (x <= epsilon)
        y = x * g;
      else
        y = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
      y -= 0.5; // Make it a signed quantity.
      int val = (int) floor(y*FIX_POINT_UMAX+0.5);
      val = (val < -0x8000)?-0x8000:val;
      val = (val >  0x7FFF)? 0x7FFF:val;
      srgb_curve[n] = (kdu_int16) val;
    }
  
  // Now for the high precision sRGB tone reproduction curve
  assert(srgb_curve_f == NULL);
  srgb_curve_f = new float[lut_entries_f+1];
  for (n=0; n < lut_entries_f; n++)
    { 
      x = ((double) n) * inv_scale_f;
      if (x <= epsilon)
        y = x * g;
      else
        y = (1.0+sbeta)*pow(x,1.0/sgamma) - sbeta;
      y -= 0.5; // Make it a signed quantity.
      srgb_curve_f[n] = (float)y;
    }
  srgb_curve_f[n] = srgb_curve_f[n-1]; // Extra entry is a replica

  // Finally, construct the primary transformation from normalized linear
  // XYZ (i.e., X/X0, Y/Y0, Z/Z0) to linear sRGB with illuminant D65.  This
  // is an illuminant-sensitive transformation.  The only version of this
  // transform which is perfectly well defined corresponds to the case where
  // the Lab illuminant is also D65.
  double srgb_to_xyzD65[9]; // Transform from linear sRGB to D65 XYZ
  find_monitor_matrix(xy_709_red,xy_709_green,xy_709_blue,
                      xy_D65_white,srgb_to_xyzD65);
  double xyzD65_to_srgb[9]; // Inverse of above
  find_matrix_inverse(xyzD65_to_srgb,srgb_to_xyzD65,3,scratch);
  double matrix[9]; // Primary transform will be stored here
  if (temperature == 6500)
    {
      double X0 = xy_D65_white[0] / xy_D65_white[1];
      double Y0 = 1.0;
      double Z0 = (1-xy_D65_white[0]-xy_D65_white[1]) / xy_D65_white[1];
      for (n=0; n < 3; n++)
        {
          matrix[3*n+0] = xyzD65_to_srgb[3*n+0] * X0;
          matrix[3*n+1] = xyzD65_to_srgb[3*n+1] * Y0;
          matrix[3*n+2] = xyzD65_to_srgb[3*n+2] * Z0;
        }
    }
  else if (temperature == 5000)
    {
      double X0 = xy_D50_white[0] / xy_D50_white[1];
      double Y0 = 1.0;
      double Z0 = (1-xy_D50_white[0]-xy_D50_white[1]) / xy_D50_white[1];
      find_matrix_product(matrix,xyzD65_to_srgb,icc_xyzD50_to_xyzD65,3);
      for (n=0; n < 3; n++)
        {
          matrix[3*n+0] *= X0;
          matrix[3*n+1] *= Y0;
          matrix[3*n+2] *= Z0;
        }
    }
  else
    assert(0);

  for (n=0; n < 9; n++)
    primary_matrix_f[n] = (float) matrix[n];

  have_chroma = true;
  return true;
}


/* ========================================================================= */
/*                           jp2_colour_converter                            */
/* ========================================================================= */

/*****************************************************************************/
/*                       jp2_colour_converter::clear                         */
/*****************************************************************************/

void
  jp2_colour_converter::clear()
{
  if (state != NULL)
    delete state;
  state = NULL;
}

/*****************************************************************************/
/*                          jp2_colour_converter::init                       */
/*****************************************************************************/

bool
  jp2_colour_converter::init(jp2_colour colour, bool use_wide_gamut,
                             bool prefer_fast_approximations)
{
  if (state != NULL)
    clear();
  state = new j2_colour_converter(colour.state,use_wide_gamut,
                                  prefer_fast_approximations);
  if (state->num_conversion_colours == 0)
    { delete state; state = NULL; }
  return (state != NULL);
}

/*****************************************************************************/
/*                    jp2_colour_converter::get_wide_gamut                   */
/*****************************************************************************/

bool
  jp2_colour_converter::get_wide_gamut() const
{
  if (state == NULL)
    return false;
  return state->wide_gamut;
}

/*****************************************************************************/
/*                    jp2_colour_converter::is_approximate                   */
/*****************************************************************************/

bool
  jp2_colour_converter::is_approximate() const
{
  if (state == NULL)
    return false;
  return state->conversion_is_approximate;
}

/*****************************************************************************/
/*                    jp2_colour_converter::is_non_trivial                   */
/*****************************************************************************/

bool
  jp2_colour_converter::is_non_trivial() const
{
  if (state == NULL)
    return false;
  return !(state->skip_opponent_transform &&
           state->skip_primary_transform && (state->lum_curve == NULL));
}

/*****************************************************************************/
/*                     jp2_colour_converter::convert_lum                     */
/*****************************************************************************/

bool
  jp2_colour_converter::convert_lum(kdu_line_buf &line, int width)
{
  if ((state == NULL) || (state->num_conversion_colours != 1))
    return false;
  if (state->lum_curve == NULL)
    return true; // Nothing to do.

  if (width < 0)
    width = line.get_width();
  assert(width <= line.get_width());
  assert(!line.is_absolute());
  if (line.get_buf16() != NULL)
    { // Low precision conversion
      kdu_sample16 *sp = line.get_buf16();
      kdu_int16 *lut = state->lum_curve;
      int lut_indices = 1<<state->lut_idx_bits;
      kdu_int32 idx, mask=(kdu_int16)(~(lut_indices-1));
      for (; width > 0; width--, sp++)
        { 
          idx = sp->ival;
          idx += FIX_POINT_HALF;
          if (idx >= 0)
            sp->ival = lut[(idx & mask)?(~mask):idx];
          else
            { idx = -idx;
              sp->ival = -(1<<KDU_FIX_POINT) -lut[(idx & mask)?(~mask):idx];
            }
        }
    }
  else
    { // High precision conversion
      kdu_sample32 *sp = line.get_buf32();
      float *lut = state->lum_curve_f;
      int idx, max_lut_idx=state->lut_entries_f-1; // Actually have extra one
      float idx_scale = state->lut_idx_scale_f;
      for (; width > 0; width--, sp++)
        { 
          float y1, y2, x = (sp->fval + 0.5f) * idx_scale;
          if (x >= 0.0f)
            { // Most common case
              idx = (int) x;  x -= (float) idx;
              assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
              idx = (idx < max_lut_idx)?idx:max_lut_idx;
              y1 = lut[idx]; y2 = lut[idx+1];
              sp->fval = y1 + x*(y2-y1);
            }
          else
            { 
              x = -x;  idx = (int) x;  x -= (float) idx;
              assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
              idx = (idx < max_lut_idx)?idx:max_lut_idx;
              y1 = lut[idx]; y2 = lut[idx+1];
              sp->fval = -1.0f - y1 - x*(y2-y1);
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                     jp2_colour_converter::convert_rgb                     */
/*****************************************************************************/

bool
  jp2_colour_converter::convert_rgb(kdu_line_buf &red, kdu_line_buf &green,
                                    kdu_line_buf &blue, int width)
{
  if ((state == NULL) || (state->num_conversion_colours != 3))
    return false;

  if (width < 0)
    width = red.get_width();
  assert((width <= red.get_width()) && (width <= green.get_width()) &&
         (width <= blue.get_width()));
  assert(!(red.is_absolute() || green.is_absolute() || blue.is_absolute()));
    
  int n;
  if (state->use_ict)
    kdu_convert_ycc_to_rgb(red,green,blue,width);
  else if (!state->skip_opponent_transform)
    { 
      if (red.get_buf16() != NULL)
        { // Low precision conversion
          kdu_sample16 *sp1 = red.get_buf16();
          kdu_sample16 *sp2 = green.get_buf16();
          kdu_sample16 *sp3 = blue.get_buf16();
          assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL));
          kdu_int32 off1=state->opponent_offset[0];
          kdu_int32 off2=state->opponent_offset[1];
          kdu_int32 off3=state->opponent_offset[2];
          kdu_int32 *matrix = state->opponent_matrix;
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              kdu_int32 val1, val2, val3, out;
              val1 = off1 + sp1->ival;
              val2 = off2 + sp2->ival;
              val3 = off3 + sp3->ival;
              out = (val1*matrix[0]+val2*matrix[1]+val3*matrix[2]+(1<<11))>>12;
              sp1->ival = (kdu_int16) out;
              out = (val1*matrix[3]+val2*matrix[4]+val3*matrix[5]+(1<<11))>>12;
              sp2->ival = (kdu_int16) out;
              out = (val1*matrix[6]+val2*matrix[7]+val3*matrix[8]+(1<<11))>>12;
              sp3->ival = (kdu_int16) out;
            }
        }
      else
        { // High precision conversion
          kdu_sample32 *sp1 = red.get_buf32();
          kdu_sample32 *sp2 = green.get_buf32();
          kdu_sample32 *sp3 = blue.get_buf32();
          assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL));
          float off1=state->opponent_offset_f[0];
          float off2=state->opponent_offset_f[1];
          float off3=state->opponent_offset_f[2];
          float *matrix = state->opponent_matrix_f;
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              float val1, val2, val3;
              val1 = off1 + sp1->fval;
              val2 = off2 + sp2->fval;
              val3 = off3 + sp3->fval;
              sp1->fval = val1*matrix[0] + val2*matrix[1] + val3*matrix[2];
              sp2->fval = val1*matrix[3] + val2*matrix[4] + val3*matrix[5];
              sp3->fval = val1*matrix[6] + val2*matrix[7] + val3*matrix[8];
            }
        }
    }
  if (state->skip_primary_transform)
    return true;

  if (red.get_buf16() != NULL)
    { // Do primary transform at low precision
      int lut_indices = 1<<state->lut_idx_bits;
      kdu_int32 mask = (kdu_int32)(~(lut_indices-1));
      kdu_int16 *lut1 = state->tone_curves[0];
      kdu_int16 *lut2 = state->tone_curves[1];
      kdu_int16 *lut3 = state->tone_curves[2];
      assert(lut1 != NULL);
      if (lut2 == NULL) lut2 = lut1;
      if (lut3 == NULL) lut3 = lut1;
      kdu_sample16 *sp1 = red.get_buf16();
      kdu_sample16 *sp2 = green.get_buf16();
      kdu_sample16 *sp3 = blue.get_buf16();
      assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL));      
      if (state->skip_primary_matrix)
        { 
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              int val1 = sp1->ival + FIX_POINT_HALF;
              if (val1 >= 0)
                sp1->ival = lut1[(val1 & mask)?(~mask):val1];
              else
                { val1 = -val1;
                  sp1->ival = -(1<<KDU_FIX_POINT)
                              -lut1[(val1 & mask)?(~mask):val1]; }
              int val2 = sp2->ival + FIX_POINT_HALF;
              if (val2 >= 0)
                sp2->ival = lut2[(val2 & mask)?(~mask):val2];
              else
                { val2 = -val2;
                  sp2->ival = -(1<<KDU_FIX_POINT)
                              -lut2[(val2 & mask)?(~mask):val2]; }
              int val3 = sp3->ival + FIX_POINT_HALF;
              if (val3 >= 0)
                sp3->ival = lut3[(val3 & mask)?(~mask):val3];
              else
                { val3 = -val3;
                  sp3->ival = -(1<<KDU_FIX_POINT)
                              -lut3[(val3 & mask)?(~mask):val3]; }
            }
        }
      else
        { 
          kdu_int32 *matrix = state->primary_matrix;
          kdu_int16 *srgb_curve = state->srgb_curve;
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              int val1, val2, val3, out;
              val1 = sp1->ival + FIX_POINT_HALF;
              if (val1 >= 0)
                val1 = lut1[(val1 & mask)?(~mask):val1];
              else
                { val1 = -val1; val1 = -lut1[(val1 & mask)?(~mask):val1]; }
              val2 = sp2->ival + FIX_POINT_HALF;
              if (val2 >= 0)
                val2 = lut2[(val2 & mask)?(~mask):val2];
              else
                { val2 = -val2; val2 = -lut2[(val2 & mask)?(~mask):val2]; }
              val3 = sp3->ival + FIX_POINT_HALF;
              if (val3 >= 0)
                val3 = lut3[(val3 & mask)?(~mask):val3];
              else
                { val3 = -val3; val3 = -lut3[(val3 & mask)?(~mask):val3]; }
          
              out = (val1*matrix[0]+val2*matrix[1]+val3*matrix[2]+(1<<11))>>12;
              if (out >= 0)
                sp1->ival = srgb_curve[(out & mask)?(~mask):out];                
              else
                { out = -out;
                  sp1->ival = -(1<<KDU_FIX_POINT)
                              -srgb_curve[(out & mask)?(~mask):out]; }

              out = (val1*matrix[3]+val2*matrix[4]+val3*matrix[5]+(1<<11))>>12;
              if (out >= 0)
                sp2->ival = srgb_curve[(out & mask)?(~mask):out];
              else
                { out = -out;
                  sp2->ival = -(1<<KDU_FIX_POINT)
                              -srgb_curve[(out & mask)?(~mask):out]; }
              
              out = (val1*matrix[6]+val2*matrix[7]+val3*matrix[8]+(1<<11))>>12;
              if (out >= 0)
                sp3->ival = srgb_curve[(out & mask)?(~mask):out];
              else
                { out = -out;
                  sp3->ival = -(1<<KDU_FIX_POINT)
                              -srgb_curve[(out & mask)?(~mask):out]; }
            }
        }
    }
  else
    { // Do primary transform at high precision
      int idx, max_lut_idx=state->lut_entries_f-1; // Actually have extra one
      float *lut1 = state->tone_curves_f[0];
      float *lut2 = state->tone_curves_f[1];
      float *lut3 = state->tone_curves_f[2];
      assert(lut1 != NULL);
      if (lut2 == NULL) lut2 = lut1;
      if (lut3 == NULL) lut3 = lut1;
      kdu_sample32 *sp1 = red.get_buf32();
      kdu_sample32 *sp2 = green.get_buf32();
      kdu_sample32 *sp3 = blue.get_buf32();
      assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL));      
      float idx_scale = state->lut_idx_scale_f;
      if (state->skip_primary_matrix)
        { 
          int idx;
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              float y1, y2, x;
              
              x = (sp1->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut1[idx]; y2 = lut1[idx+1];
                  sp1->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut1[idx]; y2 = lut1[idx+1];
                  sp1->fval = -1.0f - y1 - x*(y2-y1);
                }

              x = (sp2->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut2[idx]; y2 = lut2[idx+1];
                  sp2->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut2[idx]; y2 = lut2[idx+1];
                  sp2->fval = -1.0f - y1 - x*(y2-y1);
                }
              
              x = (sp3->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut3[idx]; y2 = lut3[idx+1];
                  sp3->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut3[idx]; y2 = lut3[idx+1];
                  sp3->fval = -1.0f - y1 - x*(y2-y1);
                }
            }
        }
      else
        { 
          float *matrix = state->primary_matrix_f;
          float *srgb_curve = state->srgb_curve_f;
          for (n=width; n > 0; n--, sp1++, sp2++, sp3++)
            { 
              float y1, y2, x, val1, val2, val3;
              
              x = (sp1->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut1[idx]; y2 = lut1[idx+1];
                  val1 = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut1[idx]; y2 = lut1[idx+1];
                  val1 = -y1 - x*(y2-y1);
                }
              
              x = (sp2->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut2[idx]; y2 = lut2[idx+1];
                  val2 = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut2[idx]; y2 = lut2[idx+1];
                  val2 = -y1 - x*(y2-y1);
                }
              
              x = (sp3->fval + 0.5f) * idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut3[idx]; y2 = lut3[idx+1];
                  val3 = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = lut3[idx]; y2 = lut3[idx+1];
                  val3 = -y1 - x*(y2-y1);
                }

              x = val1*matrix[0] + val2*matrix[1] + val3*matrix[2];
              x *= idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp1->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp1->fval = -1.0f - y1 - x*(y2-y1);
                }
              
              x = val1*matrix[3] + val2*matrix[4] + val3*matrix[5];
              x *= idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp2->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp2->fval = -1.0f - y1 - x*(y2-y1);
                }

              x = val1*matrix[6] + val2*matrix[7] + val3*matrix[8];
              x *= idx_scale;
              if (x >= 0.0f)
                { // Most common case
                  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp3->fval = y1 + x*(y2-y1);
                }
              else
                { 
                  x = -x;  idx = (int) x;  x -= (float) idx;
                  assert((x >= 0.0f) && (x < 1.0f)); // x is for interpolation
                  idx = (idx < max_lut_idx)?idx:max_lut_idx;
                  y1 = srgb_curve[idx]; y2 = srgb_curve[idx+1];
                  sp3->fval = -1.0f - y1 - x*(y2-y1);
                }
            }      
        }
    }
      
  return true;
}

/*****************************************************************************/
/*                    jp2_colour_converter::convert_rgb4                     */
/*****************************************************************************/

bool
  jp2_colour_converter::convert_rgb4(kdu_line_buf &red, kdu_line_buf &green,
                                     kdu_line_buf &blue, kdu_line_buf &extra,
                                     int width)
{
  if (!convert_rgb(red,green,blue,width))
    return false;
  if (!state->have_k)
    return true;

  if (width < 0)
    width = extra.get_width();
  assert((width <= red.get_width()) && (width <= green.get_width()) &&
         (width <= blue.get_width()));

  int n;
  if (red.get_buf16() != NULL)
    { // Do conversion at low precision
      kdu_int32 val, factor, offset;
      kdu_sample16 *sp1=red.get_buf16();
      kdu_sample16 *sp2=green.get_buf16();
      kdu_sample16 *sp3=blue.get_buf16();
      kdu_sample16 *spk=extra.get_buf16();
      assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL) && (spk != NULL));
      for (n=0; n < width; n++)
        { 
          factor = (FIX_POINT_HALF-1) - spk[n].ival;
          offset = (factor+1)*FIX_POINT_HALF - (FIX_POINT_HALF<<KDU_FIX_POINT);
          val = (factor * sp1[n].ival) + offset;
          sp1[n].ival = (kdu_int16)(val >> KDU_FIX_POINT);
          val = (factor * sp2[n].ival) + offset;
          sp2[n].ival = (kdu_int16)(val >> KDU_FIX_POINT);
          val = (factor * sp3[n].ival) + offset;
          sp3[n].ival = (kdu_int16)(val >> KDU_FIX_POINT);
        }
    }
  else
    { // Do conversion at high precision
      float factor, offset;
      kdu_sample32 *sp1=red.get_buf32();
      kdu_sample32 *sp2=green.get_buf32();
      kdu_sample32 *sp3=blue.get_buf32();
      kdu_sample32 *spk=extra.get_buf32();
      assert((sp1 != NULL) && (sp2 != NULL) && (sp3 != NULL) && (spk != NULL));
      for (n=0; n < width; n++)
        { 
          factor = 0.5f - spk[n].fval;
          offset = factor * 0.5f - 0.5f;
          sp1[n].fval = (factor * sp1[n].fval) + offset;
          sp2[n].fval = (factor * sp2[n].fval) + offset;
          sp3[n].fval = (factor * sp3[n].fval) + offset;
        }
    }
  return true;
}

/*****************************************************************************/
/*                  jp2_colour_converter::get_channel_info                   */
/*****************************************************************************/

bool jp2_colour_converter::get_channel_info(int idx, float &square_weight,
                                            bool &is_chroma)
{
  square_weight = 1.0f;
  is_chroma = false;
  if ((state == NULL) || (idx < 0) || (idx >= state->num_conversion_colours))
    return false;
  is_chroma = false;
  if (state->have_chroma && ((idx == 1) || (idx == 2)))
    is_chroma = true;
  int c;
  for (square_weight=0.0f, c=0; c < state->num_conversion_colours; c++)
    { 
      float val = state->opponent_matrix_f[3*idx+c];
      square_weight += val*val;
    }
  if (square_weight < 0.0001f)
    square_weight = 0.0001f; // Don't let any channel get too under-represented
  return true;
}


/* ========================================================================= */
/*                                jp2_header                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                           jp2_header::jp2_header                          */
/*****************************************************************************/

jp2_header::jp2_header()
{
  state = new j2_header;
  state->hdr = NULL;
}

/*****************************************************************************/
/*                          jp2_header::~jp2_header                          */
/*****************************************************************************/

jp2_header::~jp2_header()
{
  delete state;
}

/*****************************************************************************/
/*                             jp2_header::read                              */
/*****************************************************************************/

bool
  jp2_header::read(jp2_input_box *box)
{
  if (state->hdr == NULL)
    { // First call to `read'
      assert(!state->sub_box);
      state->hdr = box;
    }
  else if (state->hdr != box)
    { // Must be using the same box.
      assert(state->hdr == box);
      return false;
    }
  if (!box->is_complete())
    return false;

  // At this point, we can be sure that the box is complete, but that does not
  // mean (in a caching environment) that the sub-boxes are necessarily
  // complete, since they may have been assigned to different data-bins.
  while (state->sub_box.exists() || state->sub_box.open(box))
    {
      bool sub_complete = state->sub_box.is_complete();
      if (state->sub_box.get_box_type() == jp2_image_header_4cc)
        {
          if (!sub_complete)
            return false;
          state->dimensions.init(&state->sub_box);
        }
      else if (state->sub_box.get_box_type() == jp2_bits_per_component_4cc)
        {
          if (!sub_complete)
            return false;
          state->dimensions.process_bpcc_box(&state->sub_box);
        }
      else if ((state->sub_box.get_box_type() == jp2_colour_4cc) &&
               (!state->colour.is_initialized()))
        {
          if (!sub_complete)
            return false;
          state->colour.init(&state->sub_box);
        }
      else if (state->sub_box.get_box_type() == jp2_palette_4cc)
        {
          if (!sub_complete)
            return false;
          state->palette.init(&state->sub_box);
        }
      else if (state->sub_box.get_box_type() == jp2_channel_definition_4cc)
        {
          if (!sub_complete)
            return false;
          state->channels.parse_cdef(&state->sub_box);
        }
      else if (state->sub_box.get_box_type() == jp2_component_mapping_4cc)
        {
          if (!sub_complete)
            return false;
          state->component_map.init(&state->sub_box);
        }
      else if (state->sub_box.get_box_type() == jp2_resolution_4cc)
        {
          if (!sub_complete)
            return false;
          if (!state->resolution.init(&state->sub_box))
            return false;
        }
      else
        state->sub_box.close(); // Skip over unknown boxes, even if incomplete
    }
  state->dimensions.finalize();
  state->palette.finalize();
  state->resolution.finalize();
  state->component_map.finalize(&state->dimensions,&state->palette);
  state->channels.finalize(state->colour.get_num_colours(),false);
  state->channels.find_cmap_channels(&state->component_map,0,true);
  state->colour.finalize(&state->channels);
  if (!box->close())
    { KDU_ERROR(e,126); e <<
        KDU_TXT("Encountered a JP2 Header box having "
        "data which does not belong toany defined sub-box.");
    }
  return true;
}

/*****************************************************************************/
/*                            jp2_header::write                              */
/*****************************************************************************/

void jp2_header::write(jp2_output_box *open_box)
{
  state->dimensions.finalize();
  state->palette.finalize();
  state->resolution.finalize();
  state->component_map.finalize(&state->dimensions,&state->palette);
  state->channels.finalize(state->colour.get_num_colours(),true);
  state->channels.add_cmap_channels(&state->component_map,0);
  state->colour.finalize(&state->channels);

  if (state->channels.needs_opacity_box())
    { KDU_ERROR_DEV(e,127); e <<
        KDU_TXT("Attempting to write a JP2 opacity (opct) box to "
        "the image header box of a baseline JP2 file.  This box type is "
        "defined by JPX, not JP2, and is required only if you are trying to "
        "record chroma-key information.  "
        "You might like to upgrade the application to write files using "
        "the `jpx_target' object, rather than `jp2_target'.");
    }
  if (state->channels.needs_pixel_format_fixpoint() ||
      state->channels.needs_pixel_format_float() ||
      state->channels.needs_pixel_format_split_exp())
    { KDU_ERROR_DEV(e,0x24011613); e <<
      KDU_TXT("Attempting to write a JP2 pixel format (pxfm) box to "
              "the image header box of a baseline JP2 file.  This box type "
              "is defined by JPX, not JP2, and is required only if you "
              "specified a non-default data format in calls to "
              "`jp2_channels::set_colour_mapping', "
              "`jp2_channels::set_opacity_mapping' or "
              "`jp2_channels::set_premult_mappint'.");
    }
  if (!state->colour.is_jp2_compatible())
    { KDU_ERROR_DEV(e,128); e <<
        KDU_TXT("Attempting to write a colour description (colr) "
        "box which uses JPX extended features to the image header of a "
        "baseline JP2 file.  "
        "You might like to upgrade the application to write files using "
        "the `jpx_target' object, rather than `jp2_target'.");
    }

  state->dimensions.save_boxes(open_box);
  state->colour.save_box(open_box);
  state->palette.save_box(open_box);
  state->component_map.save_box(open_box);
  state->channels.save_boxes(open_box,true);
  state->resolution.save_box(open_box);
}

/*****************************************************************************/
/*                       jp2_header::is_jp2_compatible                       */
/*****************************************************************************/

bool
  jp2_header::is_jp2_compatible()
{
  int profile, compression_type;
  compression_type = state->dimensions.get_compression_type(profile);

  return ((compression_type == JP2_COMPRESSION_TYPE_JPEG2000) &&
          ((profile == Sprofile_PROFILE0) ||
           (profile == Sprofile_PROFILE1) ||
           (profile == Sprofile_PROFILE2) ||
           (profile == Sprofile_CINEMA2K) ||
           (profile == Sprofile_CINEMA4K) ||
           (profile == Sprofile_BROADCAST)));
}

/*****************************************************************************/
/*                       jp2_header::access_dimensions                       */
/*****************************************************************************/

jp2_dimensions
  jp2_header::access_dimensions()
{
  return jp2_dimensions(&(state->dimensions));
}

/*****************************************************************************/
/*                         jp2_header::access_colour                         */
/*****************************************************************************/

jp2_colour
  jp2_header::access_colour()
{
  return jp2_colour(&(state->colour));
}

/*****************************************************************************/
/*                        jp2_header::access_palette                         */
/*****************************************************************************/

jp2_palette
  jp2_header::access_palette()
{
  return jp2_palette(&(state->palette));
}

/*****************************************************************************/
/*                        jp2_header::access_channels                        */
/*****************************************************************************/

jp2_channels
  jp2_header::access_channels()
{
  return jp2_channels(&(state->channels));
}

/*****************************************************************************/
/*                       jp2_header::access_resolution                       */
/*****************************************************************************/

jp2_resolution
  jp2_header::access_resolution()
{
  return jp2_resolution(&(state->resolution));
}


/* ========================================================================= */
/*                                 jp2_source                                */
/* ========================================================================= */

/*****************************************************************************/
/*                           jp2_source::jp2_source                          */
/*****************************************************************************/

jp2_source::jp2_source()
{
  header=NULL; check_src=NULL; check_src_id=0;
  header_complete = signature_complete = file_type_complete = false;
  codestream_found=codestream_ready=false;
  header_bytes=0;
}

/*****************************************************************************/
/*                           jp2_source::~jp2_source                         */
/*****************************************************************************/

jp2_source::~jp2_source()
{
  if (header != NULL)
    delete header;
}
  
/*****************************************************************************/
/*                              jp2_source::open                             */
/*****************************************************************************/

bool
  jp2_source::open(jp2_family_src *src, jp2_locator loc)
{
  if ((src != check_src) || (src->get_id() != check_src_id))
    {
      if (header != NULL)
        delete header;
      header = NULL;
      header_complete = signature_complete = file_type_complete = false;
      codestream_found = codestream_ready = false;
      header_bytes = 0;
      check_src = src;
      check_src_id = src->get_id();
    }
  return jp2_input_box::open(src,loc);
}

/*****************************************************************************/
/*                           jp2_source::read_header                         */
/*****************************************************************************/

bool
  jp2_source::read_header()
{
  if (codestream_ready)
    return true;
  if (!signature_complete)
    {
      if (!exists())
        { KDU_ERROR(e,129); e <<
            KDU_TXT("Unable to open JP2 file.  Perhaps the file "
            "contains no box headers, or perhaps you forgot to call or "
            "check the return value from `jp2_source::open' before "
            "invoking `jp2_source::read_header'.");
        }
      kdu_uint32 signature;
      if (get_box_type() != jp2_signature_4cc)
        { KDU_ERROR(e,130); e <<
            KDU_TXT("Source supplied to `jp2_source::open' does "
            "not contain a valid JP2 header.");
        }
      if (!is_complete())
        return false;
      if ((!read(signature)) || (signature != jp2_signature) ||
          (get_remaining_bytes() != 0))
        { KDU_ERROR(e,131); e <<
            KDU_TXT("JP2 source does not commence with a valid "
            "signature box.");
        }
      header_bytes += get_box_bytes();
      close();
      signature_complete = true;
      assert(header == NULL);
      header = new jp2_header;
    }

  while (!codestream_found)
    {
      if ((!exists()) && !open_next())
        return false;
      if ((!file_type_complete) && (get_box_type() == jp2_file_type_4cc))
        {
          if (!is_complete())
            return false;
          bool is_compatible = false;
          kdu_uint32 brand, minor_version, compat;
          read(brand); read(minor_version);
          while (read(compat))
            if (compat == jp2_brand)
              is_compatible = true;
          header_bytes += get_box_bytes();
          if (!close())
            { KDU_ERROR(e,132); e <<
                KDU_TXT("JP2 source contains a malformed file type box.");
            }
          file_type_complete = true;
          if (!is_compatible)
            { KDU_ERROR(e,133); e <<
                KDU_TXT("JP2 source contains a file type box whose "
                "compatibility list does not include JP2.");
            }
        }
      else if ((!header_complete) && (get_box_type() == jp2_header_4cc))
        {
          if (!is_complete())
            return false;
          kdu_long box_len = get_box_bytes();
          if (!header->read(this))
            return false; // Don't close the open box
          header_bytes += box_len;
          close();
          header_complete = true;
        }
      else if (get_box_type() == jp2_codestream_4cc)
        {
          if (!(header_complete && file_type_complete))
            { KDU_ERROR(e,134); e <<
                KDU_TXT("A contiguous codestream box has been "
                "encountered within the JP2 source without first finding "
                "both the file-type box and the image header box.");
            }
          codestream_found = true;
        }
      else
        close();
    }
          
  assert(codestream_found);
  if (!codestream_ready)
    {
      assert(get_box_type() == jp2_codestream_4cc);
      if (has_caching_source() && !set_codestream_scope(0))
        return false; // Main header not completely ready yet
      codestream_ready = true;
    }

  return true;
}

/*****************************************************************************/
/*                       jp2_source::access_dimensions                       */
/*****************************************************************************/

jp2_dimensions
  jp2_source::access_dimensions()
{
  if (!header_complete)
    return jp2_dimensions(NULL);
  return header->access_dimensions();
}

/*****************************************************************************/
/*                         jp2_source::access_colour                         */
/*****************************************************************************/

jp2_colour
  jp2_source::access_colour()
{
  if (!header_complete)
    return jp2_colour(NULL);
  return header->access_colour();
}

/*****************************************************************************/
/*                        jp2_source::access_palette                         */
/*****************************************************************************/

jp2_palette
  jp2_source::access_palette()
{
  if (!header_complete)
    return jp2_palette(NULL);
  return header->access_palette();
}

/*****************************************************************************/
/*                        jp2_source::access_channels                        */
/*****************************************************************************/

jp2_channels
  jp2_source::access_channels()
{
  if (!header_complete)
    return jp2_channels(NULL);
  return header->access_channels();
}

/*****************************************************************************/
/*                       jp2_source::access_resolution                       */
/*****************************************************************************/

jp2_resolution
  jp2_source::access_resolution()
{
  if (!header_complete)
    return jp2_resolution(NULL);
  return header->access_resolution();
}

/* ========================================================================= */
/*                                 jp2_target                                */
/* ========================================================================= */

/*****************************************************************************/
/*                           jp2_target::jp2_target                          */
/*****************************************************************************/

jp2_target::jp2_target()
{
  tgt=NULL; header=NULL; header_written=false;
}

/*****************************************************************************/
/*                           jp2_target::~jp2_target                         */
/*****************************************************************************/

jp2_target::~jp2_target()
{
  if (header != NULL)
    delete header;
}

/*****************************************************************************/
/*                              jp2_target::open                             */
/*****************************************************************************/

void
  jp2_target::open(jp2_family_tgt *tgt)
{
  this->tgt = tgt;
  if (tgt->get_bytes_written() != 0)
    { KDU_ERROR_DEV(e,135); e <<
        KDU_TXT("The `jp2_target::open' function must be supplied "
        "with a `jp2_family_tgt' object to which nothing has yet been "
        "written.");
    }
  if (header != NULL)
    { delete header; header = NULL; }
  header = new jp2_header;
  header_written = false;
}

/*****************************************************************************/
/*                          jp2_target::write_header                         */
/*****************************************************************************/

void
  jp2_target::write_header()
{
  if (tgt == NULL)
    { KDU_ERROR_DEV(e,136); e <<
        KDU_TXT("You may not call `jp2_target::write_header' until "
        "after you have called `jp2_target::open'.");
    }
  assert(header != NULL); // Should not be possible to have NULL `header' if
                          // `tgt' is non-NULL.
  if (tgt->get_bytes_written() != 0)
    { KDU_ERROR_DEV(e,137); e <<
        KDU_TXT("At the point when `jp2_target::write_header' is "
        "called, no other information should have been written to the "
        "`jp2_family_tgt' object with which it was opened.");
    }
  if (!header->is_jp2_compatible())
    { KDU_ERROR(e,0x29060500); e <<
        KDU_TXT("Attempting to embed a codestream which does not conform "
                "to Part-1 of the JPEG2000 standard within a plain JP2 file.  "
                "For this, you must either write a raw codestream, or embed "
                "the codestream within the more advanced JPX file format.");
    }
  if (header_written)
    { KDU_ERROR_DEV(e,0x13051401); e <<
      KDU_TXT("Attempting to invoke `jp2_target::write_header' after the "
              "JP2 header has already been written.");
    }
  header_written = true;

  jp2_output_box::open(tgt,jp2_signature_4cc);
  write(jp2_signature);
  close();

  open_next(jp2_file_type_4cc);
  write(jp2_brand);
  write((kdu_uint32) 0);
  write(jp2_brand);
  close();

  open_next(jp2_header_4cc);
  header->write(this);
  close();
}

/*****************************************************************************/
/*                        jp2_target::open_codestream                        */
/*****************************************************************************/

void
  jp2_target::open_codestream(bool rubber_length)
{
  if ((tgt == NULL) || !header_written)
    { KDU_ERROR_DEV(e,138); e <<
        KDU_TXT("You may not call `jp2_target::open_codestream' until "
        "after you have called `jp2_target::open' and "
        "`jp2_target::write_header'.");
    }
  open_next(jp2_codestream_4cc,rubber_length);
}

/*****************************************************************************/
/*                       jp2_target::access_dimensions                       */
/*****************************************************************************/

jp2_dimensions
  jp2_target::access_dimensions()
{
  if (header == NULL)
    return jp2_dimensions(); // Empty interface
  return header->access_dimensions();
}

/*****************************************************************************/
/*                         jp2_target::access_colour                         */
/*****************************************************************************/

jp2_colour
  jp2_target::access_colour()
{
  assert(header != NULL);
  return header->access_colour();
}

/*****************************************************************************/
/*                        jp2_target::access_palette                         */
/*****************************************************************************/

jp2_palette
  jp2_target::access_palette()
{
  if (header == NULL)
    return jp2_palette(); // Empty interface
  return header->access_palette();
}

/*****************************************************************************/
/*                        jp2_target::access_channels                        */
/*****************************************************************************/

jp2_channels
  jp2_target::access_channels()
{
  if (header == NULL)
    return jp2_channels(); // Empty interface
  return header->access_channels();
}

/*****************************************************************************/
/*                       jp2_target::access_resolution                       */
/*****************************************************************************/

jp2_resolution
  jp2_target::access_resolution()
{
  if (header == NULL)
    return jp2_resolution(); // Empty interface
  return header->access_resolution();
}


/* ========================================================================= */
/*                              j2_data_references                           */
/* ========================================================================= */

/*****************************************************************************/
/*                  j2_data_references::~j2_data_references                  */
/*****************************************************************************/

j2_data_references::~j2_data_references()
{
  int n;
  if (refs != NULL)
    {
      for (n=0; n < num_refs; n++)
        if (refs[n] != NULL)
          delete[] refs[n];
      delete[] refs;
      refs = NULL;
    }
  if (file_refs != NULL)
    {
      for (n=0; n < num_refs; n++)
        if (file_refs[n] != NULL)
          delete[] file_refs[n];
      delete[] file_refs;
    }
}

/*****************************************************************************/
/*                           j2_data_references::init                        */
/*****************************************************************************/

void
  j2_data_references::init(jp2_input_box *dtbl)
{
  if (file_refs != NULL)
    {
      delete[] file_refs;
      file_refs = NULL;
    }
  if (dtbl->get_box_type() != jp2_dtbl_4cc)
    { // Current implementation parses only `dtbl', but later versions
      // should also be capable of parsing `dref' boxes.
      dtbl->close();
      return;
    }
  int n;
  kdu_uint16 num_dr;
  if (!dtbl->read(num_dr))
    { KDU_ERROR(e,139); e <<
        KDU_TXT("Malformed data reference box (dtbl) found in "
        "JPX data source.  Not all fields were present.");
    }
  num_refs = (int) num_dr;
  if (max_refs < num_refs)
    {
      char **tmp_refs = new char *[num_refs];
      memset(tmp_refs,0,sizeof(char *)*(size_t)num_refs);
      if (refs != NULL)
        {
          for (n=0; n < num_refs; n++)
            refs[n] = tmp_refs[n];
          delete[] refs;
        }
      refs = tmp_refs;
      max_refs = num_refs;
    }
  memset(refs,0,(size_t)(sizeof(char *)*num_refs));

  jp2_input_box url_box;
  kdu_uint32 flags;
  for (n=0; n < num_refs; n++)
    {
      int length=0;
      if (!(url_box.open(dtbl) &&
            (url_box.get_box_type() == jp2_data_entry_url_4cc) &&
            url_box.read(flags) &&
            ((length = (int) url_box.get_remaining_bytes()) >= 0)))
        { KDU_ERROR(e,140); e <<
            KDU_TXT("Malformed data reference box (dtbl).  Unable to "
            "read sufficient correctly formatted data entry URL boxes.");
        }
      refs[n] = new char[length+1];
      url_box.read((kdu_byte *)(refs[n]),length);
      (refs[n])[length] = '\0'; // In case there was no null terminator
      url_box.close();
    }
  if (dtbl->get_remaining_bytes() > 0)
    { KDU_ERROR(e,141); e <<
        KDU_TXT("Malformed data reference box (dtbl).  Box appears "
        "to contain additional content beyond the declared number of data "
        "entry URL boxes.");
    }
  dtbl->close();
}

/*****************************************************************************/
/*                         j2_data_references::save_box                      */
/*****************************************************************************/

void
  j2_data_references::save_box(jp2_output_box *dtbl)
{
  if (dtbl->get_box_type() != jp2_dtbl_4cc)
    { KDU_ERROR_DEV(e,142); e <<
        KDU_TXT("Current implementation of "
        "`j2_data_references::save_box' can only write JPX formatted "
        "data reference boxes -- i.e., those with box type `dtbl' rather "
        "than `dref'.  However, the implementation can easily be "
        "expanded.");
    }

  int n;
  jp2_output_box url;
  kdu_uint16 num_dr = (kdu_uint16) num_refs;
  dtbl->write(num_dr);
  for (n=0; n < num_refs; n++)
    {
      url.open(dtbl,jp2_data_entry_url_4cc);
      url.write((kdu_uint32) 0);
      int len = (int) strlen(refs[n]);
      url.write((kdu_byte *)(refs[n]),len+1);
      url.close();
    }
  dtbl->close();
}


/* ========================================================================= */
/*                             jp2_data_references                           */
/* ========================================================================= */

/*****************************************************************************/
/*                         jp2_data_references::add_url                      */
/*****************************************************************************/

int
  jp2_data_references::add_url(const char *url, int url_idx)
{
  if (state == NULL)
    return 0;
  if (url == NULL)
    {
      if (url_idx == 0)
        return 0;
      else
        url = "";
    }
  if (url_idx == 0)
    { // Find a suitable index
      if ((url_idx = find_url(url)) != 0)
        return url_idx;
      url_idx = state->num_refs+1;
    }

  if (url_idx <= 0)
    return 0;
  if (url_idx >= (1<<16))
    { KDU_ERROR_DEV(e,143); e <<
        KDU_TXT("Trying to add too many URL's to the "
        "`jp2_data_references' object.  At most 2^16 - 1 URL's may be "
        "stored by the data references box.");
    }
  if (url_idx <= state->num_refs)
    { // URL already exists; replace it
      if (state->refs[url_idx-1] != NULL)
        delete[] state->refs[url_idx-1];
      state->refs[url_idx-1] = NULL;
      state->refs[url_idx-1] = new char[strlen(url)+1];
      strcpy(state->refs[url_idx-1],url);
      if ((state->file_refs != NULL)  &&
          (state->file_refs[url_idx-1] != NULL))
        { // Delete any derived pathname -- can regenerate on demand
          delete[] state->file_refs[url_idx-1];
          state->file_refs[url_idx-1] = NULL;
        }
      return url_idx;
    }
  
  while (url_idx > state->num_refs)
    { // Grow the total number of references, installing empty references if
      // necessary
      int n;
      if (state->max_refs == state->num_refs)
        {
          state->max_refs += url_idx + 8;
          char **refs = new char *[state->max_refs];
          memset(refs,0,sizeof(char *)*(size_t)(state->max_refs));
          if (state->refs != NULL)
            {
              for (n=0; n < state->num_refs; n++)
                refs[n] = state->refs[n];
              delete[] state->refs;
            }
          state->refs = refs;
          if (state->file_refs != NULL)
            {
              char **file_refs = new char *[state->max_refs];
              memset(file_refs,0,sizeof(char *)*(size_t)state->max_refs);
              for (n=0; n < state->num_refs; n++)
                file_refs[n] = state->file_refs[n];
              delete[] state->file_refs;
              state->file_refs = file_refs;
            }
        }
      const char *new_url = url;
      if ((state->num_refs+1) != url_idx)
        new_url = "";
      state->refs[state->num_refs] = new char[strlen(new_url)+1];
      strcpy(state->refs[state->num_refs],new_url);
      state->num_refs++;
    }
  return url_idx;
}

/*****************************************************************************/
/*                     jp2_data_references::add_file_url                     */
/*****************************************************************************/

int jp2_data_references::add_file_url(const char *path, int url_idx)
{
  if (path == NULL)
    return add_url(path,url_idx);
  char *url = new char[5 + strlen("file:///") + kdu_hex_hex_encode(path,NULL)];
  strcpy(url,"file:///");
  char *cp = url + strlen(url);
  bool is_absolute = ((path[0] == '/') || (path[0] == '\\') ||
                      ((path[0] != '\0') && (path[1] == ':') &&
                       ((path[2] == '/') || (path[2] == '\\'))));
  if (is_absolute)
    {
      if ((path[0] == '/') || (path[0] == '\\'))
        path++;
    }
  else if (path[0] != '.')
    {
      *(cp++) = '.';
      *(cp++) = '/';
    }
  kdu_hex_hex_encode(path,cp);
  try {
    url_idx = add_url(url,url_idx);
  } catch (kdu_exception exc) {
    delete[] url;
    throw exc;
  }
  delete[] url;
  return url_idx;
}

/*****************************************************************************/
/*                     jp2_data_references::get_num_urls                     */
/*****************************************************************************/

int
  jp2_data_references::get_num_urls() const
{
  if (state == NULL)
    return 0;
  return state->num_refs;
}

/*****************************************************************************/
/*                       jp2_data_references::find_url                       */
/*****************************************************************************/

int
  jp2_data_references::find_url(const char *url) const
{
  if (state == NULL)
    return 0;
  for (int n=0; n < state->num_refs; n++)
    if (strcmp(state->refs[n],url) == 0)
      return n+1;
  return 0;
}

/*****************************************************************************/
/*                         jp2_data_references::get_url                      */
/*****************************************************************************/

const char *
  jp2_data_references::get_url(int idx) const
{
  if ((state == NULL) || (idx < 0) || (idx > state->num_refs))
    return NULL;
  if (idx == 0)
    return "";
  return state->refs[idx-1];
}

/*****************************************************************************/
/*                     jp2_data_references::get_file_url                     */
/*****************************************************************************/

const char *
  jp2_data_references::get_file_url(int idx)
{
  if ((state == NULL) || (idx <= 0) || (idx > state->num_refs))
    return NULL;
  const char *url = state->refs[idx-1];
  if (state->file_refs == NULL)
    {
      state->file_refs = new char *[state->max_refs];
      memset(state->file_refs,0,sizeof(char *)*(size_t)state->max_refs);
    }
  char *file_url = state->file_refs[idx-1];
  if (file_url == NULL)
    {
      const char *sep = strstr(url,":///");
      if (sep != NULL)
        { // Look for "file:" scheme name with an empty authority
          if ((tolower(url[0]) != (int) 'f') ||
              (tolower(url[1]) != (int) 'i') ||
              (tolower(url[2]) != (int) 'l') ||
              (tolower(url[3]) != (int) 'e') ||
              (sep != (url+4)))
            return NULL; // Cannot open anything other than a file
          url = sep+3;
          assert(url[0] == '/');
          if ((url[1] == '.') ||
              ((url[1] != '\0') && (url[2] == ':') &&
               ((url[3] == '/') || (url[3] == '\\'))))
            url++; // The last "/" of "file:///" is not the path root
        }
      file_url = state->file_refs[idx-1] = new char[strlen(url)+1];
      strcpy(file_url,url);
      kdu_hex_hex_decode(file_url);
    }
  return file_url;
}
