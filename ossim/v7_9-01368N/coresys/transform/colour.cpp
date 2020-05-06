/*****************************************************************************/
// File: colour.cpp [scope = CORESYS/TRANSFORMS]
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
   Implements forward and reverse colour transformations: both reversible (RCT)
and irreversible (ICT = RGB to YCbCr).
******************************************************************************/

#include <assert.h>
#include "kdu_arch.h" // Include architecture-specific info for speed-ups.
#include "kdu_sample_processing.h"
using namespace kdu_core;

#define ALPHA_R 0.299              // These are exact expressions from which
#define ALPHA_B 0.114              // the ICT forward and reverse transform
#define ALPHA_RB (ALPHA_R+ALPHA_B) // coefficients may be expressed.
#define ALPHA_G (1-ALPHA_RB)
#define CB_FACT (1/(2*(1-ALPHA_B)))
#define CR_FACT (1/(2*(1-ALPHA_R)))
#define CR_FACT_R (2*(1-ALPHA_R))
#define CB_FACT_B (2*(1-ALPHA_B))
#define CR_FACT_G (2*ALPHA_R*(1-ALPHA_R)/ALPHA_G)
#define CB_FACT_G (2*ALPHA_B*(1-ALPHA_B)/ALPHA_G)

#if defined KDU_X86_INTRINSICS
#  define KDU_SIMD_OPTIMIZATIONS
#  include "x86_colour_local.h" // x86-based SIMD accelerators
#elif defined KDU_NEON_INTRINSICS
#  define KDU_SIMD_OPTIMIZATIONS
#  include "neon_colour_local.h" // ARM-NEON based SIMD accelerators
#elif defined KDU_SPARCVIS_GCC
#  define KDU_SIMD_OPTIMIZATIONS
#  include "gcc_colour_sparcvis_local.h" // Ultra-sparc SIMD accelerators
#elif defined KDU_ALTIVEC_GCC
#  define KDU_SIMD_OPTIMIZATIONS
#  include "gcc_colour_altivec_local.h" // Altivec SIMD accelerators
#endif // KDU_PENTIUM_GCC

#ifdef KDU_SIMD_OPTIMIZATIONS
using namespace kd_core_simd; // So macro selectors can access SIMD externals
#endif

// Global function pointers to initialize
namespace kdu_core {
  void (*kdu_convert_rgb_to_ycc_rev16)(kdu_int16*,kdu_int16*,kdu_int16*,int);
  void (*kdu_convert_rgb_to_ycc_irrev16)(kdu_int16*,kdu_int16*,kdu_int16*,int);
  void (*kdu_convert_rgb_to_ycc_rev32)(kdu_int32*,kdu_int32*,kdu_int32*,int);
  void (*kdu_convert_rgb_to_ycc_irrev32)(float*,float*,float*,int);
  void (*kdu_convert_ycc_to_rgb_rev16)(kdu_int16*,kdu_int16*,kdu_int16*,int);
  void (*kdu_convert_ycc_to_rgb_irrev16)(kdu_int16*,kdu_int16*,kdu_int16*,int);
  void (*kdu_convert_ycc_to_rgb_rev32)(kdu_int32*,kdu_int32*,kdu_int32*,int);
  void (*kdu_convert_ycc_to_rgb_irrev32)(float*,float*,float*,int);
}

static bool kd_initialize_colour_conversion();
static float _kd_colour_conversion_initialized =
  kd_initialize_colour_conversion();

// Forward declarations
static void kd_rgb_to_ycc_rev16(kdu_int16*,kdu_int16*,kdu_int16*,int);
static void kd_rgb_to_ycc_rev32(kdu_int32*,kdu_int32*,kdu_int32*,int);
static void kd_rgb_to_ycc_irrev16(kdu_int16*,kdu_int16*,kdu_int16*,int);
static void kd_rgb_to_ycc_irrev32(float*,float*,float*,int);

static void kd_ycc_to_rgb_rev16(kdu_int16*,kdu_int16*,kdu_int16*,int);
static void kd_ycc_to_rgb_rev32(kdu_int32*,kdu_int32*,kdu_int32*,int);
static void kd_ycc_to_rgb_irrev16(kdu_int16*,kdu_int16*,kdu_int16*,int);
static void kd_ycc_to_rgb_irrev32(float*,float*,float*,int);

/*****************************************************************************/
/* STATIC               kd_initialize_colour_conversion                      */
/*****************************************************************************/

static bool kd_initialize_colour_conversion()
{
  kdu_convert_rgb_to_ycc_rev16 = kd_rgb_to_ycc_rev16;
  kdu_convert_rgb_to_ycc_rev32 = kd_rgb_to_ycc_rev32;
  kdu_convert_rgb_to_ycc_irrev16 = kd_rgb_to_ycc_irrev16;
  kdu_convert_rgb_to_ycc_irrev32 = kd_rgb_to_ycc_irrev32;

  kdu_convert_ycc_to_rgb_rev16 = kd_ycc_to_rgb_rev16;
  kdu_convert_ycc_to_rgb_rev32 = kd_ycc_to_rgb_rev32;
  kdu_convert_ycc_to_rgb_irrev16 = kd_ycc_to_rgb_irrev16;
  kdu_convert_ycc_to_rgb_irrev32 = kd_ycc_to_rgb_irrev32;

#ifdef KDU_SIMD_OPTIMIZATIONS
  KD_SET_SIMD_FUNC_RGB_TO_YCC_REV16(kdu_convert_rgb_to_ycc_rev16);
  KD_SET_SIMD_FUNC_RGB_TO_YCC_REV32(kdu_convert_rgb_to_ycc_rev32);
  KD_SET_SIMD_FUNC_RGB_TO_YCC_IRREV16(kdu_convert_rgb_to_ycc_irrev16);
  KD_SET_SIMD_FUNC_RGB_TO_YCC_IRREV32(kdu_convert_rgb_to_ycc_irrev32);

  KD_SET_SIMD_FUNC_YCC_TO_RGB_REV16(kdu_convert_ycc_to_rgb_rev16);
  KD_SET_SIMD_FUNC_YCC_TO_RGB_REV32(kdu_convert_ycc_to_rgb_rev32);
  KD_SET_SIMD_FUNC_YCC_TO_RGB_IRREV16(kdu_convert_ycc_to_rgb_irrev16);
  KD_SET_SIMD_FUNC_YCC_TO_RGB_IRREV32(kdu_convert_ycc_to_rgb_irrev32);
#endif // KDU_SIMD_OPTIMIZATIONS
  return true;
}

/*****************************************************************************/
/* STATIC                      kd_rgb_to_ycc_rev16                           */
/*****************************************************************************/

static void
  kd_rgb_to_ycc_rev16(kdu_int16 *sp1, kdu_int16 *sp2, kdu_int16 *sp3, int n)
{
  kdu_int16 x_y, x_db, x_dr;
  kdu_int16 x_r, x_g, x_b;
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_r = sp1[0];  x_g = sp2[0];  x_b = sp3[0];
      x_y = (x_r + x_g+x_g + x_b) >> 2;
      x_db = x_b - x_g;
      x_dr = x_r - x_g;
      sp1[0] = x_y;  sp2[0] = x_db;  sp3[0] = x_dr;
    }
}

/*****************************************************************************/
/* STATIC                      kd_rgb_to_ycc_rev32                           */
/*****************************************************************************/

static void
  kd_rgb_to_ycc_rev32(kdu_int32 *sp1, kdu_int32 *sp2, kdu_int32 *sp3, int n)
{
  kdu_int32 x_y, x_db, x_dr;
  kdu_int32 x_r, x_g, x_b;
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    { 
      x_r = sp1[0];  x_g = sp2[0];  x_b = sp3[0];
      x_y = (x_r + x_g+x_g + x_b) >> 2;
      x_db = x_b - x_g;
      x_dr = x_r - x_g;
      sp1[0] = x_y;  sp2[0] = x_db;  sp3[0] = x_dr;
    }
}

/*****************************************************************************/
/* STATIC                     kd_rgb_to_ycc_irrev16                          */
/*****************************************************************************/

static void
  kd_rgb_to_ycc_irrev16(kdu_int16 *sp1, kdu_int16 *sp2, kdu_int16 *sp3, int n)
{
  kdu_int32 x_y, x_cb, x_cr;
  kdu_int32 x_r, x_g, x_b;
#define ALPHA_R14 ((kdu_int32)(0.5+ALPHA_R*(1<<14)))
#define ALPHA_G14 ((kdu_int32)(0.5+ALPHA_G*(1<<14)))
#define ALPHA_B14 ((kdu_int32)(0.5+ALPHA_B*(1<<14)))
#define CB_FACT14 ((kdu_int32)(0.5 + CB_FACT*(1<<14)))
#define CR_FACT14 ((kdu_int32)(0.5 + CR_FACT*(1<<14)))
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_r = sp1[0];  x_g = sp2[0];  x_b = sp3[0];
      x_y=(ALPHA_R14*x_r+ALPHA_G14*x_g+ALPHA_B14*x_b+(1<<13))>>14;
      x_cb = (CB_FACT14*(x_b-x_y) + (1<<13)) >> 14;
      x_cr = (CR_FACT14*(x_r-x_y) + (1<<13)) >> 14;
      sp1[0] = (kdu_int16) x_y;
      sp2[0] = (kdu_int16) x_cb;
      sp3[0] = (kdu_int16) x_cr;
    }
}

/*****************************************************************************/
/* STATIC                     kd_rgb_to_ycc_irrev32                          */
/*****************************************************************************/

static void
  kd_rgb_to_ycc_irrev32(float *sp1, float *sp2, float *sp3, int n)
{
  double x_y, x_cb, x_cr;
  double x_r, x_g, x_b;
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_r = sp1[0];  x_g = sp2[0];  x_b = sp3[0];
      x_y = ALPHA_R*x_r + ALPHA_G*x_g + ALPHA_B*x_b;
      x_cb = CB_FACT*(x_b-x_y);
      x_cr = CR_FACT*(x_r-x_y);
      sp1[0] = (float) x_y;
      sp2[0] = (float) x_cb;
      sp3[0] = (float) x_cr;
    }
}

/*****************************************************************************/
/* STATIC                      kd_ycc_to_rgb_rev16                           */
/*****************************************************************************/

static void
  kd_ycc_to_rgb_rev16(kdu_int16 *sp1, kdu_int16 *sp2, kdu_int16 *sp3, int n)
{
  kdu_int16 x_y, x_db, x_dr;
  kdu_int16 x_r, x_g, x_b;        
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_y = sp1[0];  x_db = sp2[0];  x_dr = sp3[0];
      x_g = x_y - ((x_db+x_dr) >> 2);
      x_r = x_g + x_dr;
      x_b = x_g + x_db;
      sp1[0] = x_r;  sp2[0] = x_g; sp3[0] = x_b;
    }
}

/*****************************************************************************/
/* STATIC                      kd_ycc_to_rgb_rev32                           */
/*****************************************************************************/

static void
  kd_ycc_to_rgb_rev32(kdu_int32 *sp1, kdu_int32 *sp2, kdu_int32 *sp3, int n)
{
  kdu_int32 x_y, x_db, x_dr;
  kdu_int32 x_r, x_g, x_b;
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_y = sp1[0];  x_db = sp2[0];  x_dr = sp3[0];
      x_g = x_y - ((x_db+x_dr) >> 2);
      x_r = x_g + x_dr;
      x_b = x_g + x_db;
      sp1[0] = x_r;  sp2[0] = x_g;  sp3[0] = x_b;
    }
}

/*****************************************************************************/
/* STATIC                     kd_ycc_to_rgb_irrev16                          */
/*****************************************************************************/

static void
  kd_ycc_to_rgb_irrev16(kdu_int16 *sp1, kdu_int16 *sp2, kdu_int16 *sp3, int n)
{
  kdu_int32 x_y, x_cb, x_cr;
  kdu_int32 x_r, x_g, x_b;
#define CR_FACT_R14 ((kdu_int32)(0.5 + CR_FACT_R*(1<<14)))
#define CB_FACT_B14 ((kdu_int32)(0.5 + CB_FACT_B*(1<<14)))
#define CR_FACT_G14 ((kdu_int32)(0.5 + CR_FACT_G*(1<<14)))
#define CB_FACT_G14 ((kdu_int32)(0.5 + CB_FACT_G*(1<<14)))
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_y = sp1[0];  x_cb = sp2[0]; x_cr = sp3[0];
      x_y <<= 14;
      x_r = x_y + CR_FACT_R14*x_cr;
      x_b = x_y + CB_FACT_B14*x_cb;
      x_g = x_y - CR_FACT_G14*x_cr - CB_FACT_G14*x_cb;
      sp1[0] = (kdu_int16)((x_r + (1<<13))>>14);
      sp2[0] = (kdu_int16)((x_g + (1<<13))>>14);
      sp3[0] = (kdu_int16)((x_b + (1<<13))>>14);
    }
}

/*****************************************************************************/
/* STATIC                     kd_ycc_to_rgb_irrev32                          */
/*****************************************************************************/

static void
  kd_ycc_to_rgb_irrev32(float *sp1, float *sp2, float *sp3, int n)
{
  double x_y, x_cb, x_cr;
  double x_r, x_g, x_b;
  for (; n > 0; n--, sp1++, sp2++, sp3++)
    {
      x_y = sp1[0];  x_cb = sp2[0];  x_cr = sp3[0];
      x_r = x_y + CR_FACT_R*x_cr;
      x_b = x_y + CB_FACT_B*x_cb;
      x_g = x_y - CR_FACT_G*x_cr - CB_FACT_G*x_cb;
      sp1[0] = (float) x_r;
      sp2[0] = (float) x_g;
      sp3[0] = (float) x_b;
    }
}
