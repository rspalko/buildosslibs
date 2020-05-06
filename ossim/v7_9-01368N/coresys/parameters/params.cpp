/*****************************************************************************/
// File: params.cpp [scope = CORESYS/PARAMETERS]
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
   Implements the services defined by "kdu_params.h".
******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "kdu_messaging.h"
#include "kdu_utils.h"
#include "kdu_params.h"
#include "kdu_kernels.h"
#include "kdu_compressed.h"
#include "params_local.h"

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
   kdu_error _name("E(params.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(params.cpp)",_id);
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

using namespace kd_core_local;

/* ========================================================================= */
/*            Initialization of static const class members                   */
/* ========================================================================= */

const int kdu_params::MULTI_RECORD    = 1;
const int kdu_params::CAN_EXTRAPOLATE = 2;
const int kdu_params::ALL_COMPONENTS  = 4;


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* INLINE                        match_attribute                             */
/*****************************************************************************/

inline kd_attribute *
  match_attribute(kd_attribute *list, const char *name)
  /* Returns a pointer to the first element in the supplied list whose name
     matches that provided, or NULL if there is no match.  This function gets
     called a great deal, so we do well to make it efficient.  The
     implementation takes advantage of the fact that in most cases the
     `name' string will have exactly the same physical address as the string
     which is being matched. */
{
  kd_attribute *scan;
  for (scan=list; scan != NULL; scan=scan->next)
    if (scan->name == name)
      return scan;

  // Direct match of string addresses failed.  Try content-oriented match.
  for (scan=list; scan != NULL; scan=scan->next)
    if (strcmp(scan->name,name) == 0)
      break;
  return scan;
}

/*****************************************************************************/
/* STATIC                        step_to_eps_mu                              */
/*****************************************************************************/

static void
  step_to_eps_mu(float val, int &eps, int &mu)
  /* Finds `eps' and `mu' such that `val' ~ 2^{-`eps'}*(1+2^{-11}`mu'). */
{
  if (val <= 0.0F)
    { KDU_ERROR_DEV(e,0); e <<
        KDU_TXT("Absolute quantization step sizes must be "
        "strictly positive.");
    }
  for (eps=0; val < 1.0F; eps++)
    val *= 2.0F;
  mu = (int) floor(0.5F+((val-1.0F)*(1<<11)));
  if (mu >= (1<<11))
    { mu = 0; eps--; }
  if (eps > 31)
    { eps = 31; mu = 0; }
  if (eps < 0)
    { eps = 0; mu = (1<<11)-1; }
}

/*****************************************************************************/
/* STATIC                          find_lcm                                  */
/*****************************************************************************/

static int
  find_lcm(int m, int n)
  /* Returns the LCM (Least Common Multiple) of the two supplied integers.
     Both must be strictly positive. */
{
  assert((m > 0) && (n > 0));

  /* The idea is to remove all common factors of m and n from m and
     count them only once, in a `common_part'. */

  int common_part = 1;
  int divisor = 2;
  for (; (divisor <= m) && (divisor <= n); divisor++)
    {
      while (((m % divisor) == 0) && ((n % divisor) == 0))
        {
          common_part *= divisor;
          m /= divisor;
          n /= divisor;
        }
    }
  return m*n*common_part;
}

/*****************************************************************************/
/* STATIC                    parse_translator_entry                          */
/*****************************************************************************/

static char const *
  parse_translator_entry(char const *start, char separator,
                         char buf[], int len, int &value)
  /* Attempts to parse a translator entry from the supplied string. Translator
     entries consist of a text string, followed by an '=' sign and an integer
     value. Successive entries are delimited by the character identified by
     `separator', which is normally either a comma or the '|' symbol.
     The function generates an error if a valid entry cannot be found.
     Otherwise, it places the string identifier in the supplied buffer and
     the value through the `value' reference, returning a pointer to the
     delimiting character. This delimiting character is either the separator
     itself, or one of the characters, ')' or ']'. Note that identifiers whose
     length exceeds `len'-1 will cause an error to be generated. */
{
  int i;

  for (i=0; i < len; i++)
    {
      if ((start[i] == separator) || (start[i] == ')') || (start[i] == ']') ||
          (start[i] == '\0'))
        { KDU_ERROR_DEV(e,1); e <<
            KDU_TXT("String translators in code-stream attribute "
            "specifications must contain an '=' sign! Problem "
            "encountered at")
            << ": \"" << start << "\".";
        }
      else if (start[i] == '=')
        break;
      buf[i] = start[i];
    }
  if (i == len)
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("String translators in code-stream attribute "
        "specifications may not exceed ") << len-1 <<
        KDU_TXT(" characters in length! Problem encountered at")
        << ": \"" << start << "\".";
    }
  buf[i] = '\0';
  start += i+1;

  char *end_p;

  value = (int) strtol(start,&end_p,10);
  if ((end_p == start) ||
      ((*end_p != separator) && (*end_p != ')') && (*end_p != ']')))
    { KDU_ERROR_DEV(e,3); e <<
        KDU_TXT("String translators in code-stream attribute "
        "specifications must be identified with integers and correctly "
        "delimited! Problem encountered at")
        << ": \"" << start << "\".";
    }
  return(end_p);
}

/*****************************************************************************/
/* STATIC                       display_options                              */
/*****************************************************************************/

static void
  display_options(char const *pattern, kdu_message &output)
{
  char buf1[80], buf2[80], *bp, *obp, *last_bp;
  int val;
  bool multi_option;

  bp = buf1; obp = buf2; last_bp = NULL;
  multi_option = false;
  if (*pattern == '(')
    {
      output << "Expected one of the identifiers, ";
      do {
          pattern = parse_translator_entry(pattern+1,',',bp,80,val);
          if (multi_option)
            output << ", ";
          if (last_bp != NULL)
            { output << "\"" << last_bp << "\""; multi_option = true; }
          last_bp = bp; bp = obp; obp = last_bp;
        } while (*pattern == ',');
      assert(*pattern == ')');
      if (multi_option)
        output << " or ";
      output << "\"" << last_bp << "\".";
    }
  else if (*pattern == '[')
    {
      output << "Expected one or more of the identifiers, ";
      do {
          pattern = parse_translator_entry(pattern+1,'|',bp,80,val);
          if (multi_option)
            output << ", ";
          if (last_bp != NULL)
            { output << "\"" << last_bp << "\""; multi_option = true; }
          last_bp = bp; bp = obp; obp = last_bp;
        } while (*pattern == '|');
      assert(*pattern == ']');
      if (multi_option)
        output << " or ";
      output << "\"" << last_bp << "\", separated by `|' symbols.";
    }
  else
    assert(0);
}

/*****************************************************************************/
/* STATIC                           int2log                                  */
/*****************************************************************************/

static int
  int2log(int val)
  /* Returns the integer exponent, e, such that 2^e=val. Returns -1 if
     such an integer does not exist. */
{
  int e;

  for (e=0; ((1<<e) > 0) && ((1<<e) < val); e++);
  if (val != (1<<e))
    return 0;
  return e;
}

/*****************************************************************************/
/* STATIC                    synthesize_canvas_size                          */
/*****************************************************************************/

static bool
  synthesize_canvas_size(int components, int dims[], int origin, int &size)
  /* Determines a canvas size which is compatible with the supplied
     component component dimensions and image origin.  Specifically, we must
     have dims[n] = ceil(size/subs[n])-ceil(origin/subs[n]) for each some
     set of valid component sub-sampling factors, subs[n].  If this is not
     possible within the constraint that sub-sampling factors are no greater
     than 255, the function returns false; otherwise, the function returns
     true, with the compatible size parameter in `size'. The function
     basically conducts an intelligent (but still somewhat tedious)
     search through the parameter space. */

{
  int ref_dim, ref_sub, n;

  for (ref_dim=dims[0], n=1; n < components; n++)
    if (dims[n] > ref_dim)
      ref_dim = dims[n];
  for (ref_sub=1; ref_sub <= 255; ref_sub++)
    { // Reference component has largest dimension and smallest sub-sampling
      int max_size = (ceil_ratio(origin,ref_sub)+ref_dim)*ref_sub;
      int min_size = max_size - ref_sub + 1;
      for (n=0; n < components; n++)
        {
          int sz, last_sz, sub;
          
          // Find smallest sub-sampling factor compatible with existing
          // minimum canvas size and use this to deduce new minimum size.
          sub = (min_size - origin) / dims[n]; // Roughly smallest factor.
          if (sub <= 0)
            sub = 1;
          while ((sub > 1) &&
                 (((ceil_ratio(origin,sub)+dims[n])*sub) > min_size))
            sub--;
          while ((sz=(ceil_ratio(origin,sub)+dims[n])*sub) < min_size)
            sub++;
          if ((sz-sub+1) > min_size)
            min_size = sz-sub+1;

          if (min_size > max_size)
            break; // Current reference sub-sampling factor cannot work.

          // Find largest sub-sampling factor compatible with existing
          // maximum canvas size and use this to deduce new maximum size.

          do {
              last_sz = sz;
              if (sub == 255)
                break;
              sub++;
              sz = (ceil_ratio(origin,sub)+dims[n])*sub;
            } while ((sz-sub+1) <= max_size);
          if (last_sz < max_size)
            max_size = last_sz;
          
          if (min_size > max_size)
            break; // Current reference sub-sampling factor cannot work.
        }
      if (n == components)
        { // Succeeded.
          size = min_size;
          return true;
        }
    }
  return false; // Failed to find compatible sub-sampling factors.
}

/*****************************************************************************/
/* STATIC                   derive_absolute_ranges                           */
/*****************************************************************************/

static void
  derive_absolute_ranges(qcd_params *qcd, kdu_params *cod, kdu_params *atk,
                         int num_dwt_levels, int num_bands, int precision,
                         int guard_bits)
  /* This function determines a suitable set of ranging parameters for
     reversible coding.  To avoid overflow/underflow we need
     `epsilon'+G >= `precision'+log2(B) where `epsilon' is the ranging
     parameter, G is the number of guard bits and B is the worst case
     (BIBO) bit-depth expansion for the reversible DWT.  Values for B are
     calculated using the `kdu_kernels::get_bibo_gain' function.
     For non-conventional decomposition styles, a non-NULL `ads' argument
     will be available to identify the structure. */
{
  int n, b;
  int level_bands;
  kdu_int16 desc, band_descriptors[49];
  int hor_initial_levs=0, vert_initial_levs=0;
  int hor_extra_levs=0, vert_extra_levs=0;
  bool hor_extra_high[3], vert_extra_high[3];
  int band_idx = num_bands-1;
  int range;
  double gain;

  // Initialize a `kdu_kernels' object
  kdu_kernels kernels;
  if (atk == NULL)
    { 
      int kernel_id=0;  cod->get(Ckernels,0,0,kernel_id);
      kernels.init(kernel_id,true);
    }
  else
    { 
      kdu_kernel_step_info *step_info = NULL;
      float *step_coeffs = NULL;
      try { // Protect against possible memory leaks
        int c, s, Ls=0, num_steps=0, num_coeffs=0;
        for (s=0; atk->get(Ksteps,s,0,Ls); s++)
          num_coeffs += Ls;
        num_steps = s;
        step_info = new kdu_kernel_step_info[num_steps];
        float *step_coeffs = new float[num_coeffs];
        for (c=s=0; s < num_steps; s++)
          { 
            kdu_kernel_step_info *sp = step_info + s;
            if (!(atk->get(Ksteps,s,0,sp->support_length) &&
                  atk->get(Ksteps,s,1,sp->support_min) &&
                  atk->get(Ksteps,s,2,sp->downshift) &&
                  atk->get(Ksteps,s,3,sp->rounding_offset)))
              assert(0);
            for (n=0; n < sp->support_length; n++, c++)
              atk->get(Kcoeffs,c,0,step_coeffs[c]);
          }
        kernels.init(num_steps,step_info,step_coeffs,false,false,true);
      }
      catch (kdu_exception exc) { 
        if (step_coeffs != NULL) delete[] step_coeffs;
        if (step_info != NULL) delete[] step_info;
        throw exc;
      }
      delete[] step_info;
      delete[] step_coeffs;
    }

  // Now work through the levels
  for (n=0; n < num_dwt_levels; n++)
    { 
      int decomp=0;
      cod->get(Cdecomp,n,0,decomp);
      level_bands = cod_params::expand_decomp_bands(decomp,band_descriptors);
      vert_extra_levs = hor_extra_levs = 0;
      for (b=level_bands-1; b >= 0; b--)
        {
          desc = band_descriptors[b];
          hor_extra_levs = (desc & 3);
          hor_extra_high[0] = (desc & (1<<2))?true:false;
          hor_extra_high[1] = (desc & (1<<3))?true:false;
          hor_extra_high[2] = (desc & (1<<4))?true:false;
          desc >>= 8;
          vert_extra_levs = (desc & 3);
          vert_extra_high[0] = (desc & (1<<2))?true:false;
          vert_extra_high[1] = (desc & (1<<3))?true:false;
          vert_extra_high[2] = (desc & (1<<4))?true:false;
          if (b > 0)
            {
              gain  = kernels.get_bibo_gain(hor_initial_levs,hor_extra_levs,
                                            hor_extra_high);
              gain *= kernels.get_bibo_gain(vert_initial_levs,vert_extra_levs,
                                            vert_extra_high);
              for (range=precision-guard_bits; gain > 0.9F; gain*=0.5F)
                range++;
              assert(band_idx > 0);
              qcd->set(Qabs_ranges,band_idx--,0,range);
            }
        }
      hor_initial_levs += hor_extra_levs;
      vert_initial_levs += vert_extra_levs;
    }
  assert(band_idx == 0);
  gain  = kernels.get_bibo_gain(hor_initial_levs,0,NULL);
  gain *= kernels.get_bibo_gain(vert_initial_levs,0,NULL);
  for (range=precision-guard_bits; gain > 0.9F; gain*=0.5F)
    range++;
  qcd->set(Qabs_ranges,0,0,range);
}

/*****************************************************************************/
/* STATIC                    derive_absolute_steps                           */
/*****************************************************************************/

static void
  derive_absolute_steps(qcd_params *qcd, kdu_params *cod, kdu_params *atk,
                        int num_dwt_levels, int num_bands, float ref_step,
                        bool derived_from_LL)
  /* This function determines a suitable set of scalar quantization step
     sizes for the irreversible path, based on the number of DWT levels,
     the DWT kernel type, the decomposition style, and a base
     quantization step size (`ref_step').  If `derived_from_LL' is true, only
     the LL band's step size will be determined and set into the supplied
     `qcd' object.  Otherwise, separate step sizes will be determined for
     each subband and set into the `qcd' object. */

{
  int n, b;
  int level_bands;
  kdu_int16 desc, band_descriptors[49];
  int hor_initial_levs=0, vert_initial_levs=0;
  int hor_extra_levs=0, vert_extra_levs=0;
  bool hor_extra_high[3], vert_extra_high[3];
  double gain;
  int band_idx = (derived_from_LL)?0:(num_bands-1);

  // Initialize a `kdu_kernels' object
  kdu_kernels kernels;
  if (atk == NULL)
    {
      int kernel_id=0;  cod->get(Ckernels,0,0,kernel_id);
      kernels.init(kernel_id,false);
    }
  else
    { 
      kdu_kernel_step_info *step_info = NULL;
      float *step_coeffs = NULL;
      try { // Protect against possible memory leaks
        int c, s, Ls=0, num_steps=0, num_coeffs=0;
        for (s=0; atk->get(Ksteps,s,0,Ls); s++)
          num_coeffs += Ls;
        num_steps = s;
        step_info = new kdu_kernel_step_info[num_steps];
        step_coeffs = new float[num_coeffs];
        for (c=s=0; s < num_steps; s++)
          { 
            kdu_kernel_step_info *sp = step_info + s;
            if (!(atk->get(Ksteps,s,0,sp->support_length) &&
                  atk->get(Ksteps,s,1,sp->support_min) &&
                  atk->get(Ksteps,s,2,sp->downshift) &&
                  atk->get(Ksteps,s,3,sp->rounding_offset)))
              assert(0);
            for (n=0; n < sp->support_length; n++, c++)
              atk->get(Kcoeffs,c,0,step_coeffs[c]);
          }
        kernels.init(num_steps,step_info,step_coeffs,false,false,false);
      }
      catch (kdu_exception exc) {
        if (step_info != NULL) delete[] step_info;
        if (step_coeffs != NULL) delete[] step_coeffs;
        throw exc;
      }
      delete[] step_info;
      delete[] step_coeffs;
    }

  // Now work through the levels
  for (n=0; n < num_dwt_levels; n++)
    { 
      int decomp=0;
      cod->get(Cdecomp,n,0,decomp);
      level_bands = cod_params::expand_decomp_bands(decomp,band_descriptors);
      vert_extra_levs = hor_extra_levs = 0;
      for (b=level_bands-1; b >= 0; b--)
        {
          desc = band_descriptors[b];
          hor_extra_levs = (desc & 3);
          hor_extra_high[0] = (desc & (1<<2))?true:false;
          hor_extra_high[1] = (desc & (1<<3))?true:false;
          hor_extra_high[2] = (desc & (1<<4))?true:false;
          desc >>= 8;
          vert_extra_levs = (desc & 3);
          vert_extra_high[0] = (desc & (1<<2))?true:false;
          vert_extra_high[1] = (desc & (1<<3))?true:false;
          vert_extra_high[2] = (desc & (1<<4))?true:false;
          if ((b > 0) && !derived_from_LL)
            {
              gain =
                kernels.get_energy_gain(hor_initial_levs,hor_extra_levs,
                                        hor_extra_high);
              gain *=
                kernels.get_energy_gain(vert_initial_levs,vert_extra_levs,
                                        vert_extra_high);
              assert(band_idx > 0);
              qcd->set(Qabs_steps,band_idx--,0,ref_step/(float)sqrt(gain));
            }
        }
      hor_initial_levs += hor_extra_levs;
      vert_initial_levs += vert_extra_levs;
    }
  assert(band_idx == 0);
  gain  = kernels.get_energy_gain(hor_initial_levs,0,NULL);
  gain *= kernels.get_energy_gain(vert_initial_levs,0,NULL);
  qcd->set(Qabs_steps,0,0,ref_step/(float)sqrt(gain));
}

/*****************************************************************************/
/* STATIC                    derive_weighted_steps                           */
/*****************************************************************************/

static void
  derive_weighted_steps(qcd_params *qcd, kdu_params *cod, kdu_params *atk,
                        int num_dwt_levels, int num_bands, float ref_step)
  /* Same as `derive_absolute_steps', except that there is no option to
     derive from LL, and the step sizes are additionally weighted by the
     reciprocal of any visual weights derived from `Cweight', `Clev_weights'
     and/or `Cband_weights'. */

{
  int n, b;
  int level_bands;
  kdu_int16 desc, band_descriptors[49];
  int hor_initial_levs=0, vert_initial_levs=0;
  int hor_extra_levs=0, vert_extra_levs=0;
  bool hor_extra_high[3], vert_extra_high[3];
  double gain;
  int band_idx = num_bands-1;

  // Initialize a `kdu_kernels' object
  kdu_kernels kernels;
  if (atk == NULL)
    {
      int kernel_id=0;  cod->get(Ckernels,0,0,kernel_id);
      kernels.init(kernel_id,false);
    }
  else
    { 
      kdu_kernel_step_info *step_info = NULL;
      float *step_coeffs = NULL;
      try { // Protect against possible memory leaks
        int c, s, Ls=0, num_steps=0, num_coeffs=0;
        for (s=0; atk->get(Ksteps,s,0,Ls); s++)
          num_coeffs += Ls;
        num_steps = s;
        step_info = new kdu_kernel_step_info[num_steps];
        step_coeffs = new float[num_coeffs];
        for (c=s=0; s < num_steps; s++)
          { 
            kdu_kernel_step_info *sp = step_info + s;
            if (!(atk->get(Ksteps,s,0,sp->support_length) &&
                  atk->get(Ksteps,s,1,sp->support_min) &&
                  atk->get(Ksteps,s,2,sp->downshift) &&
                  atk->get(Ksteps,s,3,sp->rounding_offset)))
              assert(0);
            for (n=0; n < sp->support_length; n++, c++)
              atk->get(Kcoeffs,c,0,step_coeffs[c]);
          }
        kernels.init(num_steps,step_info,step_coeffs,false,false,false);
      }
      catch (kdu_exception exc) {
        if (step_info != NULL) delete[] step_info;
        if (step_coeffs != NULL) delete[] step_coeffs;
        throw exc;
      }
      delete[] step_info;
      delete[] step_coeffs;
    }
  
  // See if we have any weights to apply
  float comp_weight;
  if (!cod->get(Cweight,0,0,comp_weight))
    comp_weight = 1.0F;

  // Now work through the levels
  for (n=0; n < num_dwt_levels; n++)
    { 
      float level_weight;
      if (!cod->get(Clev_weights,n,0,level_weight))
        level_weight = 1.0;
      level_weight *= comp_weight;
      
      int decomp=0;
      cod->get(Cdecomp,n,0,decomp);
      level_bands = cod_params::expand_decomp_bands(decomp,band_descriptors);
      vert_extra_levs = hor_extra_levs = 0;
      for (b=level_bands-1; b >= 0; b--)
        {
          desc = band_descriptors[b];
          hor_extra_levs = (desc & 3);
          hor_extra_high[0] = (desc & (1<<2))?true:false;
          hor_extra_high[1] = (desc & (1<<3))?true:false;
          hor_extra_high[2] = (desc & (1<<4))?true:false;
          desc >>= 8;
          vert_extra_levs = (desc & 3);
          vert_extra_high[0] = (desc & (1<<2))?true:false;
          vert_extra_high[1] = (desc & (1<<3))?true:false;
          vert_extra_high[2] = (desc & (1<<4))?true:false;
          if (b > 0)
            { 
              int orientation = LL_BAND;
              if (hor_extra_high[0])
                { 
                  if (vert_extra_high[0])
                    orientation = HH_BAND;
                  else
                    orientation = HL_BAND;
                }
              else
                orientation = HH_BAND;
              gain =
                kernels.get_energy_gain(hor_initial_levs,hor_extra_levs,
                                        hor_extra_high);
              gain *=
                kernels.get_energy_gain(vert_initial_levs,vert_extra_levs,
                                        vert_extra_high);
              assert(band_idx > 0);
              int weights_idx = 3*(n+1) - orientation;
              assert(weights_idx >= 0);
              float band_weight;
              if (cod->get(Cband_weights,weights_idx,0,band_weight))
                band_weight *= level_weight;
              else
                band_weight = level_weight;
              if (band_weight < 0.001f)
                band_weight = 0.001f;
              float band_step = ref_step / (band_weight * (float)sqrt(gain));
              qcd->set(Qabs_steps,band_idx--,0,band_step);
            }
        }
      hor_initial_levs += hor_extra_levs;
      vert_initial_levs += vert_extra_levs;
    }
  assert(band_idx == 0);
  gain  = kernels.get_energy_gain(hor_initial_levs,0,NULL);
  gain *= kernels.get_energy_gain(vert_initial_levs,0,NULL);
  float band_step = ref_step / (comp_weight * (float)sqrt(gain));
  qcd->set(Qabs_steps,0,0,band_step);
}

/*****************************************************************************/
/* STATIC            optimize_absolute_steps_for_fix16                       */
/*****************************************************************************/

static void
  optimize_absolute_steps_for_fix16(qcd_params *qcd, kdu_params *cod,
                                    kdu_params *atk, int num_dwt_levels,
                                    int num_bands, int guard_bits,
                                    bool limit_k_max)
  /* This function implements the functionality of "Qfix16=YES".  It scans
     through the subbands working out the cumulative effect of scaling factors
     from each successive horizontal/vertical DWT stage, which are not actually
     apply -- the scaling factors end up getting combined with the quantization
     step sizes so as to save computation and avoid unnecessary numerical
     rounding errors.  From this the function can work out what adjustments
     are required to ensure that the scaling factors that are applied during
     quantiation turn out to be powers of 2.
        Additionally, if `limit_k_max' is true, the function ensures that
     the selected quantization parameters do not produce subbands with more
     than 15 magnitude bits and have a legal mu-eps representation.  This
     last requirement might cause the factors to deviate slightly from the
     ideal values, since the mantissa mu has only 11 precision bits.
        The function relies upon the fact that ATK and COD attributes are
     finalized before the QCD attribute.  Additionally, the function relies
     upon any inherited quantization steps sizes having been locally copied
     within `qcd_params::finalize' whence the call to this function arise. */
{
  int n, b;
  int level_bands;
  kdu_int16 desc, band_descriptors[49];
  float initial_scale=1.0f; // Product of all low-band scales to start of level
  int band_idx = num_bands-1; // We work backwards through the subbands
  
  // Initialize a `kdu_kernels' object
  kdu_kernels kernels;
  if (atk == NULL)
    { 
      int kernel_id=0;  cod->get(Ckernels,0,0,kernel_id);
      kernels.init(kernel_id,false);
    }
  else
    { 
      kdu_kernel_step_info *step_info = NULL;
      float *step_coeffs = NULL;
      try { // Protect against possible memory leaks
        int c, s, Ls=0, num_steps=0, num_coeffs=0;
        for (s=0; atk->get(Ksteps,s,0,Ls); s++)
          num_coeffs += Ls;
        num_steps = s;
        step_info = new kdu_kernel_step_info[num_steps];
        step_coeffs = new float[num_coeffs];
        for (c=s=0; s < num_steps; s++)
          { 
            kdu_kernel_step_info *sp = step_info + s;
            if (!(atk->get(Ksteps,s,0,sp->support_length) &&
                  atk->get(Ksteps,s,1,sp->support_min) &&
                  atk->get(Ksteps,s,2,sp->downshift) &&
                  atk->get(Ksteps,s,3,sp->rounding_offset)))
              assert(0);
            for (n=0; n < sp->support_length; n++, c++)
              atk->get(Kcoeffs,c,0,step_coeffs[c]);
          }
        kernels.init(num_steps,step_info,step_coeffs,false,false,false);
      }
      catch (kdu_exception exc) {
        if (step_info != NULL) delete[] step_info;
        if (step_coeffs != NULL) delete[] step_coeffs;
        throw exc;
      }
      delete[] step_info;
      delete[] step_coeffs;
    }
  int nsteps=0;
  float low_scale=1.0f, high_scale=1.0f;
  kernels.get_lifting_factors(nsteps,low_scale,high_scale);

  // Now work through the levels
  for (n=0; n < num_dwt_levels; n++)
    { 
      int extra_levs, decomp=0;
      cod->get(Cdecomp,n,0,decomp);
      level_bands = cod_params::expand_decomp_bands(decomp,band_descriptors);
      float band_scale = initial_scale;
      for (b=level_bands-1; b >= 0; b--)
        { 
          desc = band_descriptors[b];
          band_scale = initial_scale;
          extra_levs = (desc & 3);
          if (extra_levs > 0)
            { // Adjust band scaling factor for horizontal stages 
              band_scale *= (desc & (1<<2))?high_scale:low_scale;
              if (extra_levs > 1)
                { 
                  band_scale *= (desc & (1<<3))?high_scale:low_scale;
                  if (extra_levs > 2)
                    band_scale *= (desc & (1<<4))?high_scale:low_scale;
                }
            }
          desc >>= 8;
          extra_levs = (desc & 3);
          if (extra_levs > 0)
            { // Adjust band scaling factor for vertical stages 
              band_scale *= (desc & (1<<2))?high_scale:low_scale;
              if (extra_levs > 1)
                { 
                  band_scale *= (desc & (1<<3))?high_scale:low_scale;
                  if (extra_levs > 2)
                    band_scale *= (desc & (1<<4))?high_scale:low_scale;
                }
            }
          if (b > 0)
            { 
              float step=1.0f, adj_step, factor, mantissa;
              int exponent, eps, mu;
              assert(band_idx > 0);
              qcd->get(Qabs_steps,band_idx,0,step);
              factor = step / band_scale;
              mantissa = frexpf(factor,&exponent);
              assert((mantissa >= 0.5f) && (mantissa < 1.0f));
              adj_step = step * 0.5f / mantissa; // Rounds down to power of 2
              if (mantissa > 0.95f)
                adj_step *= 2.0f; // Was close to power of 2; round up instead
              step_to_eps_mu(adj_step,eps,mu);
              if (limit_k_max)
                { 
                  int k_max = guard_bits + eps;
                  if (k_max > 15)
                    { 
                      eps += 15-k_max;
                      eps = (eps < 0)?0:eps; // Just in case quard_bits huge!
                    }
                }
              adj_step = (1.0f+mu/((float)(1<<11))) * kdu_pwrof2f(-eps);
              qcd->set(Qabs_steps,band_idx,0,adj_step);
              band_idx--;
            }
        }
      initial_scale = band_scale;
    }
  assert(band_idx == 0);
  
  { // Finish by making the same adjustments for band-0
    float band_scale = initial_scale;
    float step=1.0f, adj_step, factor, mantissa;
    int exponent, eps, mu;
    qcd->get(Qabs_steps,0,0,step);
    factor = step / band_scale;
    mantissa = frexpf(factor,&exponent);
    assert((mantissa >= 0.5f) && (mantissa < 1.0f));
    adj_step = step * 0.5f / mantissa; // Rounds down to power of 2
    if (mantissa > 0.95f)
      adj_step *= 2.0f; // Was close to power of 2; round up instead
    step_to_eps_mu(adj_step,eps,mu);
    if (limit_k_max)
      { 
        int k_max = guard_bits + eps;
        if (k_max > 15)
          { 
            eps += 15-k_max;
            eps = (eps < 0)?0:eps; // Just in case quard_bits huge!
          }
      }
    adj_step = (1.0f+mu/((float)(1<<11))) * kdu_pwrof2f(-eps);
    qcd->set(Qabs_steps,0,0,adj_step);  
  }
}


/* ========================================================================= */
/*                                 kd_attribute                              */
/* ========================================================================= */

/*****************************************************************************/
/*                         kd_attribute::kd_attribute                        */
/*****************************************************************************/

kd_attribute::kd_attribute(const char *name, const char *comment,
                           int flags, const char *pattern)
{
  char const *ch;

  this->values = NULL;
  this->name = name;
  this->comment = comment;
  this->flags = flags;
  this->pattern = pattern;
  for (ch=pattern, num_fields=0; *ch != '\0'; ch++, num_fields++)
    {
      if ((*ch == 'F') || (*ch == 'B') || (*ch == 'I') || (*ch == 'C'))
        continue;

      char term = '\0';
      if (*ch == '(')
        term = ')';
      else if (*ch == '[')
        term = ']';
      for (ch++; (*ch != term) && (*ch != '\0'); ch++);
      if (*ch == '\0')
        throw pattern;
    }
  num_used_records = 0;
  num_marked_records = 0;
  max_records = 1;
  this->values = new att_val[max_records*num_fields];
  for (ch=pattern, num_fields=0; *ch != '\0'; ch++, num_fields++)
    {
      values[num_fields].pattern = ch;
      if ((*ch == 'F') || (*ch == 'B') || (*ch == 'I') || (*ch == 'C'))
        continue;
      char term = '\0';
      if (*ch == '(')
        term = ')';
      else if (*ch == '[')
        term = ']';
      for (ch++; (*ch != term) && (*ch != '\0'); ch++);
    }
  derived = false;
  parsed = false;
  next = NULL;
}

/*****************************************************************************/
/*                       kd_attribute::augment_records                       */
/*****************************************************************************/

void
  kd_attribute::augment_records(int new_records)
{
  if (new_records <= num_used_records)
    return;
  if (max_records < new_records)
    { // Need to allocate more storage.
      if (!(flags & kdu_params::MULTI_RECORD))
        { KDU_ERROR_DEV(e,4); e <<
            KDU_TXT("Attempting to write multiple records to a code-stream "
            "attribute")
            << ", \"" << name <<
            KDU_TXT("\", which can accept only single attributes!");
        }

      int alloc_records = max_records + new_records;
      att_val *alloc_vals = new att_val[alloc_records*num_fields];
      int rec, fld;
      att_val *new_p, *old_p;

      for (new_p=alloc_vals, old_p=values, rec=0; rec < max_records; rec++)
        for (fld=0; fld < num_fields; fld++)
          *(new_p++) = *(old_p++);
      for (; rec < alloc_records; rec++)
        for (old_p -= num_fields, fld=0; fld < num_fields; fld++)
          { *new_p = *(old_p++); new_p->is_set = false; new_p++; }
      delete[] values;
      values = alloc_vals;
      max_records = alloc_records;
    }
  num_used_records = new_records;
}

/*****************************************************************************/
/*                          kd_attribute::describe                           */
/*****************************************************************************/

void
  kd_attribute::describe(kdu_message &output, bool allow_tiles,
                   bool allow_comps, bool treat_instances_like_components,
                   bool include_comments)
{
  char locators[3];
  int loc_pos = 0;
  if (allow_tiles)
    locators[loc_pos++] = 'T';
  if (allow_comps && !(flags & kdu_params::ALL_COMPONENTS))
    locators[loc_pos++] = 'C';
  if (treat_instances_like_components)
    locators[loc_pos++] = 'I';
  locators[loc_pos++] = '\0';
      
  if (loc_pos < 2)
    output << name << "={";
  else
    output << name << "[:<" << locators << ">]={";
  for (int fnum=0; fnum < num_fields; fnum++)
    {
      if (fnum != 0)
        output << ",";
      char const *cp = values[fnum].pattern;
      assert(cp != NULL);
      if (*cp == 'I')
        output << "<int>";
      else if (*cp == 'B')
        output << "<yes/no>";
      else if (*cp == 'F')
        output << "<float>";
      else if (*cp == 'C')
        output << "<custom int>";
      else if (*cp == '(')
        {
          char buf[80];
          int val;

          output << "ENUM<";
          do {
              cp = parse_translator_entry(cp+1,',',buf,80,val);
              output << buf;
              if (*cp == ',')
                output << *cp;
            } while (*cp == ',');
          output << ">";
        }
      else if (*cp == '[')
        {
          char buf[80];
          int val;

          output << "FLAGS<";
          do {
              cp = parse_translator_entry(cp+1,'|',buf,80,val);
              output << buf;
              if (*cp == '|')
                output << *cp;
            } while (*cp == '|');
          output << ">";
        }
    }
      
  output << "}";
  if (flags & kdu_params::MULTI_RECORD)
    output << ",...\n";
  else
    output << "\n";
  if (include_comments)
    output << "\t" << comment << "\n";
}


/* ========================================================================= */
/*                                 kdu_params                                */
/* ========================================================================= */

/*****************************************************************************/
/*                            kdu_params::kdu_params                         */
/*****************************************************************************/

kdu_params::kdu_params(const char *cluster_name, bool allow_tiles,
                       bool allow_comps, bool allow_insts,
                       bool force_comps, bool treat_instances_like_components)
{
  assert(!(treat_instances_like_components && allow_comps));

  this->cluster_name = cluster_name;
  this->tile_idx = -1;
  this->comp_idx = -1;
  this->inst_idx = 0;
  this->num_comps = 0;
  this->num_tiles = 0;
  this->allow_tiles = allow_tiles;
  this->allow_comps = allow_comps;
  this->allow_insts = allow_insts;
  this->force_comps = force_comps;
  this->treat_instances_like_components = treat_instances_like_components;

  first_cluster = this; next_cluster = NULL;
  refs = &dummy_ref; dummy_ref = this;
  first_inst = this; next_inst = NULL;

  attributes = NULL;
  empty = true;
  changed = false;
  marked = false;

  dependencies[0] = NULL;
}

/*****************************************************************************/
/*                          kdu_params::~kdu_params                          */
/*****************************************************************************/

kdu_params::~kdu_params()
{
  kd_attribute *att;
  kdu_params *csp;

  while ((att=attributes) != NULL)
    {
      attributes = att->next;
      delete(att);
    }

  // Process instance list
  if (first_inst == NULL)
    return; // Function being used to delete all elements of instance list.
  if (this != first_inst)
    { // Relink instance list.
      for (csp=first_inst; this != csp->next_inst; csp=csp->next_inst);
      csp->next_inst = this->next_inst;
      return;
    }
  while ((csp=next_inst) != NULL)
    { // Delete rest of instance list.
      next_inst = csp->next_inst;
      csp->first_inst = NULL; // Prevent further instance list processing.
      delete csp;
    }

  // Update `refs' entry
  assert(this == first_inst);
  kdu_params **sref;
  int n, ref_idx = (tile_idx+1)*(num_comps+1)+(comp_idx+1);
  assert((refs != NULL) && (refs[ref_idx] == this));
  refs[ref_idx] = NULL;

  // Delete all associated tile-components if this is the head of a tile
  if (comp_idx < 0)
    {
      for (sref=refs+ref_idx, n=num_comps; n > 0; n--)
        {
          sref++;
          if (*sref == this)
            *sref = NULL;
          else if (*sref != NULL)
            {
              assert((*sref)->tile_idx == this->tile_idx);
                     // Verify that we are deleting a unique element
              delete *sref;
              assert(*sref == NULL);
            }
        }
    }

  // Delete all associated tile-components if this is the head of a component
  if (tile_idx < 0)
    {
      for (sref=refs+ref_idx, n=num_tiles; n > 0; n--)
        {
          sref += num_comps+1;
          if (*sref == this)
            *sref = NULL;
          else if (*sref != NULL)
            {
              assert((*sref)->comp_idx == this->comp_idx);
                     // Verify that we are deleting a unique element
              delete *sref;
              assert(*sref == NULL);
            }
        }
    }

  // Delete references array, if appropriate
  if ((tile_idx >= 0) || (comp_idx >= 0))
    return;

  if (refs != &dummy_ref)
    delete[] refs;

  // Process cluster list
  if (first_cluster == NULL)
    return; // Function being used to delete all elements of the cluster list.
  if (this != first_cluster)
    { // Relink cluster list.
      for (csp=first_cluster; this!=csp->next_cluster; csp=csp->next_cluster);
      csp->next_cluster = this->next_cluster;
      return;
    }
  while ((csp=next_cluster) != NULL)
    { // Delete entire cluster list.
      next_cluster = csp->next_cluster;
      csp->first_cluster = NULL; // Prevent further cluster list processing.
      delete csp;
    }
}

/*****************************************************************************/
/*                               kdu_params::link                            */
/*****************************************************************************/

kdu_params *
  kdu_params::link(kdu_params *existing, int tile_idx, int comp_idx,
                   int num_tiles, int num_comps)
{
  kdu_params *cluster, *prev_cluster;

  assert((this->tile_idx == -1) && (this->comp_idx == -1) &&
         (this->inst_idx == 0) && (this->refs == &this->dummy_ref) &&
         (tile_idx < num_tiles) && (comp_idx < num_comps) &&
         (tile_idx >= -1) && (comp_idx >= -1));
  this->tile_idx = tile_idx;
  this->comp_idx = comp_idx;
  this->num_tiles = num_tiles;
  this->num_comps = num_comps;
  this->first_cluster = NULL; // May set a non-trivial value later

  if (((!allow_tiles) && (num_tiles > 0)) ||
      ((!allow_comps) && (num_comps > 0)))
    { KDU_ERROR_DEV(e,5); e <<
        KDU_TXT("Illegal tile or component indices supplied to "
        "`kdu_params::link'.  Probably attempting to specialize a parameter "
        "object to a specific tile or component, where the parameter class "
        "in questions does not support tile or component diversity.");
    }

  cluster = existing->first_inst->first_cluster;
  for (prev_cluster=NULL;
       cluster != NULL;
       prev_cluster=cluster, cluster=cluster->next_cluster)
    if (strcmp(cluster->cluster_name,this->cluster_name) == 0)
      break;
  if (cluster == NULL)
    { /* Link as head of new cluster. */
      assert((tile_idx == -1) && (comp_idx == -1));
            /* Cluster head must have component and tile indices of -1. */
      if (prev_cluster != NULL)
        {
          this->first_cluster = prev_cluster->first_cluster;
          prev_cluster->next_cluster = this;
        }
      else
        this->first_cluster = this;
      next_cluster = NULL;
      int n, num_refs = (num_tiles+1)*(num_comps+1);
      refs = NULL;
      refs = new kdu_params *[num_refs];
      for (n=0; n < num_refs; n++)
        refs[n] = this;
      return this;
    }

  if ((cluster->num_comps != num_comps) || (cluster->num_tiles != num_tiles))
    { KDU_ERROR_DEV(e,6); e <<
        KDU_TXT("Call to `kdu_params::link' specifies a different "
        "number of tiles or components to the number with which the first "
        "parameter object of the same class was linked.");
    }

  refs = cluster->refs;
  assert(refs != &dummy_ref);
  int ref_idx = (num_comps+1)*(tile_idx+1) + (comp_idx+1);
  kdu_params *rf = refs[ref_idx];
  if ((rf != NULL) && (rf != this) &&
      (rf->comp_idx == comp_idx) && (rf->tile_idx == tile_idx))
    { // Linking a new instance of current tile-component
      if (!allow_insts)
        { KDU_ERROR_DEV(e,7); e <<
            KDU_TXT("Call to `kdu_params::link' specifies the "
            "same cluster name, tile and component indices as an existing "
            "linked object, which does not support multiple instances.");
        }
      while (rf->next_inst != NULL)
        rf = rf->next_inst;
      this->first_inst = rf->first_inst;
      rf->next_inst = this;
      this->inst_idx = rf->inst_idx + 1;
    }
  else
    refs[ref_idx] = this;

  return this;
}

/*****************************************************************************/
/*                         kdu_params::add_dependency                        */
/*****************************************************************************/

void
  kdu_params::add_dependency(const char *cname)
{
  int i;
  for (i=0; i < KD_MAX_PARAM_DEPENDENCIES; i++)
    if (dependencies[i] == cname)
      return; // Already exists
    else if (dependencies[i] == NULL)
      { dependencies[i] = cname; dependencies[i+1] = NULL; return; }

  assert(0); // Array is too small to accommodate required dependencies
}

/*****************************************************************************/
/*                          kdu_params::new_instance                         */
/*****************************************************************************/

kdu_params *
  kdu_params::new_instance()
{
  if (!allow_insts)
    return NULL; // Multiple instances not allowed
  if ((comp_idx < 0) && (num_comps > 0))
    return NULL; // Can't build instance list on potentially shared default
  if ((tile_idx < 0) && (num_tiles > 0) && !treat_instances_like_components)
    return NULL; // Can't build instance list on potentially shared default

  kdu_params *result = new_object();
  kdu_params *scan = this;
  result->refs = refs;
  result->tile_idx = tile_idx;
  result->comp_idx = comp_idx;
  result->num_tiles = num_tiles;
  result->num_comps = num_comps;
  result->first_cluster = NULL;
  while (scan->next_inst != NULL)
    scan = scan->next_inst;
  scan->next_inst = result;
  result->first_inst = scan->first_inst;
  result->inst_idx = scan->inst_idx+1;
  for (int i=0; i <= KD_MAX_PARAM_DEPENDENCIES; i++)
    result->dependencies[i] = scan->dependencies[i];
  return result;
}

/*****************************************************************************/
/*                            kdu_params::copy_from                          */
/*****************************************************************************/

void
  kdu_params::copy_from(kdu_params *source_ref, int source_tile,
                        int target_tile, int instance,
                        int skip_components, int discard_levels,
                        bool transpose, bool vflip, bool hflip)
{
  if (source_ref->cluster_name != this->cluster_name)
    { KDU_ERROR_DEV(e,8); e <<
        KDU_TXT("Trying to use `kdu_params::copy_from' to copy an "
        "object to one which has been derived differently.");
    }

  kdu_params *source = source_ref;  // Keep `source_ref' for cluster navigation
  kdu_params *target = this;
  if ((source->tile_idx >= 0) || (source->comp_idx >= 0) ||
      (target->tile_idx >= 0) || (target->comp_idx >= 0))
    { KDU_ERROR_DEV(e,9); e <<
        KDU_TXT("Trying to use `kdu_params::copy_from' to copy an "
        "object which is not a cluster head, or to copy to another object "
        "which is not the head of its cluster.");
    }

  if (source_tile < source->num_tiles)
    source = source->refs[(source_tile+1)*(source->num_comps+1)];
  else
    source = NULL; // Can't copy anything in this cluster

  if (target_tile < target->num_tiles)
    {
      target = target->refs[(target_tile+1)*(target->num_comps+1)];
      if ((target != NULL) && (target->tile_idx == -1) && (target_tile >= 0))
        { // See if we need to create a new target object
          if ((source != NULL) && (source->tile_idx >= 0))
            {
              target = target->access_relation(target_tile,-1,0,false);
              assert(target->tile_idx == target_tile);
            }
          else
            target = NULL; // No need to copy anything in this cluster
        }
    }
  else
    target = NULL; // Can't copy anything in this cluster


  int src_c = skip_components;
  int tgt_c = 0;
  while ((source != NULL) && (target != NULL))
    {
      // Start by copying `source' to `target'
      kdu_params *src = source;
      kdu_params *dst = target;
      bool matched_all_instances = false;
      while ((src != NULL) && (dst != NULL) && !matched_all_instances)
        {
          if ((src->inst_idx == instance) || (instance < 0))
            { // Can copy this instance
              if (dst->treat_instances_like_components)
                { // Instance indices need not be contiguous
                  dst = target->access_relation(dst->tile_idx,dst->comp_idx,
                                                src->inst_idx,false);
                  assert(dst != NULL); // Above function will create missing
                                       // instance if necessary.
                }
              if (dst->marked)
                { KDU_ERROR_DEV(e,10); e <<
                    KDU_TXT("Illegal attempt to modify a `kdu_params' "
                    "object which has already been marked!");
                }
              if (dst->empty)
                dst->copy_with_xforms(src,skip_components,discard_levels,
                                      transpose,vflip,hflip);
              if (instance >= 0)
                matched_all_instances = true;
            }
          if (!dst->allow_insts)
            break;

          src = src->next_inst;
          if (!dst->treat_instances_like_components)
            { // Advance destination instance to match source instance;
              // even if there is no further source instance, we should
              // always have one empty instance at the end of the
              // destination list, in case we need to write to it later.
              if (dst->next_inst == NULL)
                dst->new_instance();
              dst = dst->next_inst;
            }
        }

      // Find the next `source' and `target' objects to copy
      do {
          if (src_c >= source->num_comps)
            source = NULL; // No more sources
          else
            source=source->refs[(source_tile+1)*(source->num_comps+1)+src_c+1];
          if (tgt_c >= target->num_comps)
            target = NULL;
          else
            target=target->refs[(target_tile+1)*(target->num_comps+1)+tgt_c+1];
          src_c++;
          tgt_c++;
        } while ((target != NULL) && (target->comp_idx == -1) &&
                 (source != NULL) && (source->comp_idx == -1));
            // Loops over those components for which both the source and
            // target are identical to their defaults
      if ((target != NULL) && (target->comp_idx == -1))
        target = target->access_relation(target_tile,tgt_c-1,0,false);
           // Creates a unique entry for this tile-component to be copied
    }

  // Now see if we need to copy other clusters
  source = source_ref;
  target = this;
  if ((source != source->first_cluster) ||
      (target != target->first_cluster))
    return;
  source = source->next_cluster;
  target = target->next_cluster;
  while ((source != NULL) && (target != NULL))
    {
      target->copy_from(source,source_tile,target_tile,instance,
                        skip_components,discard_levels,transpose,vflip,hflip);
      source = source->next_cluster;
      target = target->next_cluster;
    }
}

/*****************************************************************************/
/*                          kdu_params::copy_all                             */
/*****************************************************************************/

void
  kdu_params::copy_all(kdu_params *source_ref,
                       int skip_components, int discard_levels,
                       bool transpose, bool vflip, bool hflip)
{
  if (source_ref->cluster_name != this->cluster_name)
    { KDU_ERROR_DEV(e,11); e <<
        KDU_TXT("Trying to use `kdu_params::copy_all' to copy an "
        "object to one which has been derived differently.");
    }

  kdu_params *source = source_ref;  // Keep `source_ref' for cluster navigation
  kdu_params *target = this;
  if ((source->tile_idx >= 0) || (source->comp_idx >= 0) ||
      (target->tile_idx >= 0) || (target->comp_idx >= 0))
    { KDU_ERROR_DEV(e,12); e <<
        KDU_TXT("Trying to use `kdu_params::copy_all' to copy an "
        "object which is not a cluster head, or to copy to another object "
        "which is not the head of its cluster.");
    }

  int src_t = 0;
  int tgt_t = 0;
  while ((source != NULL) && (target != NULL))
    {
      // Start by copying all components of `source' to `target'
      int src_c = skip_components;
      int tgt_c = 0;
      kdu_params *tile_source = source;
      kdu_params *tile_target = target;
      while ((tile_source != NULL) && (tile_target != NULL))
        {
          // Start by copying all instances of `tile_source' to `tile_target'.
          kdu_params *src = tile_source;
          kdu_params *dst = tile_target;
          while ((src != NULL) && (dst != NULL))
            {
              if (dst->treat_instances_like_components)
                { // Instance indices need not be contiguous
                  dst =
                    tile_target->access_relation(dst->tile_idx,dst->comp_idx,
                                                 src->inst_idx,false);
                  assert(dst != NULL); // Above function will create missing
                                       // instance if necessary.
                }
              if (dst->marked)
                { KDU_ERROR_DEV(e,13); e <<
                    KDU_TXT("Illegal attempt to modify a `kdu_params' "
                    "object which has already been marked!");
                }
              if (dst->empty)
                dst->copy_with_xforms(src,skip_components,discard_levels,
                                      transpose,vflip,hflip);
              if (!dst->allow_insts)
                break;
              src = src->next_inst;
              if (!dst->treat_instances_like_components)
                { // Advance destination instance to match source instance;
                  // even if there is no further source instance, we should
                  // always have one empty instance at the end of the
                  // destination list, in case we need to write to it later.
                  if (dst->next_inst == NULL)
                    dst->new_instance();
                  dst = dst->next_inst;
                }
            }

          // Now advance the component indices
          do {
              if (src_c >= source->num_comps)
                tile_source = NULL; // No more sources
              else
                tile_source =
                  source->refs[(src_t+1)*(source->num_comps+1)+src_c+1];
              if (tgt_c >= target->num_comps)
                tile_target = NULL;
              else
                tile_target =
                  target->refs[(tgt_t+1)*(target->num_comps+1)+tgt_c+1];
              src_c++;
              tgt_c++;
            } while ((tile_target == target) && (tile_source == source));
              // Loops over those components for which both the source and
              // target are identical to their defaults
          if ((tile_target == target) && (tile_source != NULL))
            tile_target = target->access_relation(tgt_t-1,tgt_c-1,0,false);
              // Creates a unique entry for this tile to be copied
        }

      // Now advance the tile indices
      do {
          if (src_t >= source->num_tiles)
            source = NULL; // No more sources
          else
            source=source->refs[(src_t+1)*(source->num_comps+1)];
          if (tgt_t >= target->num_tiles)
            target = NULL;
          else
            target=target->refs[(tgt_t+1)*(target->num_comps+1)];
          src_t++;
          tgt_t++;
        } while ((target != NULL) && (target->tile_idx == -1) &&
                 (source != NULL) && (source->tile_idx == -1));
            // Loops over those tiles for which both the source and
            // target are identical to their defaults
      if ((target != NULL) && (source != NULL) && (target->tile_idx == -1))
        target = target->access_relation(tgt_t-1,-1,0,false);
           // Creates a unique entry for this tile to be copied
    }

  // Now see if we need to copy other clusters
  source = source_ref;
  target = this;
  if ((source != source->first_cluster) ||
      (target != target->first_cluster))
    return;
  source = source->next_cluster;
  target = target->next_cluster;
  while ((source != NULL) && (target != NULL))
    {
      target->copy_all(source,skip_components,discard_levels,
                       transpose,vflip,hflip);
      source = source->next_cluster;
      target = target->next_cluster;
    }
}

/*****************************************************************************/
/*                        kdu_params::access_cluster                         */
/*****************************************************************************/

kdu_params *
  kdu_params::access_cluster(const char *name)
{
  kdu_params *scan = refs[0]->first_cluster;
  if (name == NULL)
    return(scan);
  while ((scan != NULL) && (strcmp(scan->cluster_name,name) != 0))
    scan = scan->next_cluster;
  return(scan);
}

/*****************************************************************************/
/*                        kdu_params::access_cluster                         */
/*****************************************************************************/

kdu_params *
  kdu_params::access_cluster(int seq)
{
  kdu_params *scan = refs[0]->first_cluster;
  while ((scan != NULL) && (seq > 0))
    { scan = scan->next_cluster; seq--; }
  return(scan);
}

/*****************************************************************************/
/*                        kdu_params::access_relation                        */
/*****************************************************************************/

kdu_params *
  kdu_params::access_relation(int tile_idx, int comp_idx, int inst_idx,
                              bool read_only)
{
  if ((tile_idx >= num_tiles) || (comp_idx >= num_comps))
    return NULL;
  int ref_idx = (comp_idx+1)+(tile_idx+1)*(num_comps+1);
  kdu_params *result = refs[ref_idx];
  if (result == NULL)
    return NULL;
  if ((!read_only) &&
      ((result->tile_idx != tile_idx) || (result->comp_idx != comp_idx)))
    { // Need to return a unique result
      if ((inst_idx != 0) && !treat_instances_like_components)
        return NULL;

      result = new_object();
      result->refs = refs;
      result->tile_idx = tile_idx;
      result->comp_idx = comp_idx;
      result->num_tiles = num_tiles;
      result->num_comps = num_comps;
      result->first_cluster = NULL;
      refs[ref_idx] = result;

      if (comp_idx < 0)
        { // Check each component in the current tile to see if we need to
          // modify a link to a default object
          assert(tile_idx >= 0);
          for (int c=0; c < num_comps; c++)
            {
              ref_idx++;
              if (refs[ref_idx] == refs[0])
                refs[ref_idx] = result;
              else if (refs[ref_idx]->tile_idx < 0)
                access_relation(tile_idx,c,0,false); // Create unique object
            }
        }
      else if (tile_idx < 0)
        { // Check each tile at the current component to see if we need to
          // modify a link to a default object
          for (int t=0; t < num_tiles; t++)
            {
              ref_idx += (num_comps+1);
              if (refs[ref_idx] == refs[0])
                refs[ref_idx] = result;
              else if (refs[ref_idx]->comp_idx < 0)
                access_relation(t,comp_idx,0,false); // Create unique object
            }
        }
      else
        { // Create a tile head for the current tile-component if necessary,
          // since otherwise functions which scan by cluster, by tile and
          // then by tile-component will not find this new unique
          // tile-component object
          ref_idx -= (comp_idx+1);
          if (refs[ref_idx] == refs[0])
            access_relation(tile_idx,-1,0,false); // Create unique object
        }

      // See if we need to create a unique instance in another cluster on
      // whose dependency list we reside
      int n;
      kdu_params *scan;
      for (scan=refs[0]->first_cluster; scan != NULL; scan=scan->next_cluster)
        {
          for (n=0; n < KD_MAX_PARAM_DEPENDENCIES; n++)
            if (scan->dependencies[n] == NULL)
              break;
            else if (strcmp(scan->dependencies[n],this->cluster_name) == 0)
              {
                scan->access_relation(tile_idx,comp_idx,0,false);
                if ((tile_idx >= 0) && scan->allow_comps && !this->allow_comps)
                  for (int c=0; c < scan->num_comps; c++)
                    scan->access_relation(tile_idx,c,0,false);
                break;
              }
        }
    }

  while ((result != NULL) && (result->inst_idx != inst_idx))
    {
      if ((result->next_inst == NULL) ||
          (result->next_inst->inst_idx > inst_idx))
        { // We will not be finding the desired instance in the existing list
          if (!result->treat_instances_like_components)
            return NULL; // Inhertiance and creation disallowed
          else if (read_only && (result->tile_idx < 0))
            return NULL; // Nowhere to inherit from
          else if (read_only)
            {
              assert(result->num_comps == 0);
              result = refs[0]; // Jump to cluster head
              continue; // Try again with the cluster head's instance list
            }
          else
            { // Creation of a new object is required
              kdu_params *new_obj = new_object();
              new_obj->refs = refs;
              new_obj->tile_idx = tile_idx;
              new_obj->comp_idx = comp_idx;
              new_obj->num_tiles = num_tiles;
              new_obj->num_comps = num_comps;
              new_obj->first_cluster = NULL;
              new_obj->next_inst = result->next_inst;
              result->next_inst = new_obj;
              new_obj->first_inst = result->first_inst;
              new_obj->inst_idx = inst_idx;
            }
        }
      result = result->next_inst;
    }

  return(result);
}

/*****************************************************************************/
/*                         kdu_params::access_unique                         */
/*****************************************************************************/

kdu_params *
  kdu_params::access_unique(int tile_idx, int comp_idx, int inst_idx)
{
  if ((tile_idx >= num_tiles) || (comp_idx >= num_comps))
    return NULL;
  int ref_idx = (comp_idx+1)+(tile_idx+1)*(num_comps+1);
  kdu_params *result = refs[ref_idx];
  if ((result == NULL) ||
      (result->tile_idx != tile_idx) || (result->comp_idx != comp_idx))
    return NULL;
  while ((result != NULL) && (result->inst_idx != inst_idx))
    result = result->next_inst;
  return(result);
}

/*****************************************************************************/
/*                           kdu_params::clear_marks                         */
/*****************************************************************************/

void
  kdu_params::clear_marks(bool for_reading)
{
  kdu_params *cluster = refs[0]->first_cluster;

  int c, t;
  kdu_params *par, **rp;
  for (; cluster != NULL; cluster=cluster->next_cluster)
    { 
      for (rp=cluster->refs, t=-1; t < cluster->num_tiles; t++)
        for (c=-1; c < cluster->num_comps; c++, rp++)
          {
            par = *rp;
            if ((par==NULL) || (par->tile_idx != t) || (par->comp_idx != c))
              continue; // Already done this one
            for (; par != NULL; par=par->next_inst)
              { 
                if (par->marked)
                  par->reset_read_state();
                par->changed = par->marked = false;
                if (for_reading)
                  for (kd_attribute *ap=par->attributes;
                       ap != NULL; ap=ap->next)
                    ap->num_marked_records = 0;
              }
          }
    }
}

/*****************************************************************************/
/*                           kdu_params::any_changes                         */
/*****************************************************************************/

bool
  kdu_params::any_changes()
{
  kdu_params *head = refs[0]->first_cluster;
  return head->changed;
}

/*****************************************************************************/
/*                        kdu_params::check_typical_tile                     */
/*****************************************************************************/

bool
  kdu_params::check_typical_tile(int tile_idx, const char *excluded_clusters)
{
  int c;
  kdu_params **tc_ref, *cluster = refs[0]->first_cluster;
  for (; cluster != NULL; cluster=cluster->next_cluster)
    {
      if (cluster->num_tiles <= 0)
        continue;
      if (excluded_clusters != NULL)
        {
          bool match_found = false;
          const char *cp, *xp=excluded_clusters;
          while ((*xp != '\0') && !match_found)
            {
              for (cp=cluster->cluster_name; *xp != '\0'; cp++, xp++)
                if (*cp != *xp)
                  break;
              match_found = (*cp == '\0');
              while ((*xp != ':') && (*xp != '\0'))
                { xp++; match_found=false; }
              if (*xp == ':')
                xp++;
            }
          if (match_found)
            continue; // This is an excluded cluster
        }

      if ((tile_idx < 0) || (tile_idx >= cluster->num_tiles))
        { KDU_ERROR_DEV(e,14); e <<
            KDU_TXT("Invalid `tile_idx' supplied to "
            "`kdu_params::check_typical_tile'.");
        }
      tc_ref = cluster->refs + (cluster->num_comps+1)*(tile_idx+1);
      for (c=0; c <= cluster->num_comps; c++, tc_ref++)
        {
          if (*tc_ref == NULL)
            continue;
          if ((*tc_ref)->tile_idx >= 0)
            { // Tile-specific object found; rough check of actual parameters
              // ensues.
              kd_attribute *ap_tc = (tc_ref[0])->attributes;
              kd_attribute *ap_global = (cluster->refs[0])->attributes;
              kd_attribute *ap_comp = (cluster->refs[c])->attributes;
              kd_attribute *ap_tile =
                ((tc_ref[-c])->tile_idx >= 0)?((tc_ref[-c])->attributes):ap_tc;
              for (; ap_tc != NULL;
                   ap_tc=ap_tc->next,      ap_tile=ap_tile->next,
                   ap_comp=ap_comp->next,  ap_global=ap_global->next)
                {
                  kd_attribute *ap = ap_tc;
                  if (ap->num_marked_records == 0)
                    ap = ap_tile; // Default to the tile value if possible
                  kd_attribute *ap_ref = ap_comp;
                  if (ap_ref->num_marked_records == 0)
                    ap_ref = ap_global; // Main header defaults to global value
                  if ((ap == ap_ref) || (ap->num_marked_records == 0))
                    continue; // Using the main header reference value
                  if ((ap->num_fields > 1) || (ap->num_marked_records != 1) ||
                      (ap_ref->num_marked_records != 1))
                    return false; // No testing of multi-valued attributes
                  if (!(ap->values[0].is_set && ap_ref->values[0].is_set))
                    return false; // No testing of unset values
                  if (ap->values[0].pattern[0] == 'F')
                    {
                      if (ap->values[0].fval != ap_ref->values[0].fval)
                        return false; // Values differ
                    }
                  else
                    {
                      if (ap->values[0].ival != ap_ref->values[0].ival)
                        return false; // Values differ
                    }
                }
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                             kdu_params::get (int)                         */
/*****************************************************************************/

bool
  kdu_params::get(const char *name, int record_idx, int field_idx, int &value,
                  bool allow_inherit, bool allow_extend, bool allow_derived)
{
  kd_attribute *ap;
  att_val *att_ptr;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,15); e <<
        KDU_TXT("Attempt to access a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,16); e <<
        KDU_TXT("Attempt to access a code-stream attribute, with "
        "an invalid field index!\n"
        "The attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  att_ptr = ap->values + field_idx;
  if (att_ptr->pattern[0] == 'F')
    { KDU_ERROR_DEV(e,17); e <<
        KDU_TXT("Attempting to access a floating point code-stream "
        "attribute field with the integer access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
 
  bool have_attribute = (ap->num_marked_records > 0);
  if (ap->derived && !allow_derived)
    have_attribute = false;

  if ((!have_attribute) && allow_inherit &&
      ((inst_idx == 0) || treat_instances_like_components))
    { // Try inheritance.
      kdu_params *summary;
      if ((comp_idx >= 0) &&
          ((summary = access_relation(tile_idx,-1,0,true)) != NULL) &&
          (summary->tile_idx == tile_idx) &&
          summary->get(name,record_idx,field_idx,value,false,
                       allow_extend,allow_derived))
        return true;
      if ((tile_idx >= 0) &&
          ((summary = access_relation(-1,comp_idx,inst_idx,true)) != NULL) &&
          summary->get(name,record_idx,field_idx,value,true,
                       allow_extend,allow_derived))
        return true;
    }
  if (!have_attribute)
    return false;

  if ((ap->num_marked_records <= record_idx) && allow_extend &&
      (ap->flags & this->CAN_EXTRAPOLATE))
    record_idx = ap->num_marked_records - 1;
  att_ptr += ap->num_fields*record_idx;
  if ((record_idx < 0) || (record_idx >= ap->num_marked_records) ||
      !att_ptr->is_set)
    return false;
  value = att_ptr->ival;
  return true;
}

/*****************************************************************************/
/*                             kdu_params::get (bool)                        */
/*****************************************************************************/

bool
  kdu_params::get(const char *name, int record_idx, int field_idx, bool &value,
                 bool allow_inherit, bool allow_extend, bool allow_derived)
{
  kd_attribute *ap;
  att_val *att_ptr;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,18); e <<
        KDU_TXT("Attempt to access a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,19); e <<
        KDU_TXT("Attempt to access a code-stream attribute, with "
        "an invalid field index!\n"
        "The attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  att_ptr = ap->values + field_idx;
  if (att_ptr->pattern[0] != 'B')
    { KDU_ERROR_DEV(e,20);  e <<
        KDU_TXT("Attempting to access a non-boolean code-stream "
        "attribute field with the boolean access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
 
  bool have_attribute = (ap->num_marked_records > 0);
  if (ap->derived && !allow_derived)
    have_attribute = false;

  if ((!have_attribute) && allow_inherit &&
      ((inst_idx == 0) || treat_instances_like_components))
    { // Try inheritance.
      kdu_params *summary;
      if ((comp_idx >= 0) &&
          ((summary = access_relation(tile_idx,-1,0,true)) != NULL) &&
          (summary->tile_idx == tile_idx) &&
          summary->get(name,record_idx,field_idx,value,false,
                       allow_extend,allow_derived))
        return true;
      if ((tile_idx >= 0) &&
          ((summary = access_relation(-1,comp_idx,inst_idx,true)) != NULL) &&
          summary->get(name,record_idx,field_idx,value,true,
                       allow_extend,allow_derived))
        return true;
    }
  if (!have_attribute)
    return false;

  if ((ap->num_marked_records <= record_idx) && allow_extend &&
      (ap->flags & this->CAN_EXTRAPOLATE))
    record_idx = ap->num_marked_records - 1;
  att_ptr += ap->num_fields*record_idx;
  if ((record_idx < 0) || (record_idx >= ap->num_marked_records) ||
      !att_ptr->is_set)
    return false;
  value = (att_ptr->ival)?true:false;
  return true;
}

/*****************************************************************************/
/*                            kdu_params::get (float)                        */
/*****************************************************************************/

bool
  kdu_params::get(const char *name, int record_idx, int field_idx,
                  float &value, bool allow_inherit, bool allow_extend,
                  bool allow_derived)
{
  kd_attribute *ap;
  att_val *att_ptr;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,21); e <<
        KDU_TXT("Attempt to access a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,22); e <<
        KDU_TXT("Attempt to access a code-stream attribute, with "
        "an invalid field index!\n"
        "The attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  att_ptr = ap->values + field_idx;
  if (att_ptr->pattern[0] != 'F')
    { KDU_ERROR_DEV(e,23); e <<
        KDU_TXT("Attempting to access an integer code-stream parameter "
        "attribute field with the floating point access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }

  bool have_attribute = (ap->num_marked_records > 0);
  if (ap->derived && !allow_derived)
    have_attribute = false;

  if ((!have_attribute) && allow_inherit &&
      ((inst_idx == 0) || treat_instances_like_components))
    { // Try inheritance.
      kdu_params *summary;
      if ((comp_idx >= 0) &&
          ((summary = access_relation(tile_idx,-1,0,true)) != NULL) &&
          (summary->tile_idx == tile_idx) &&
          summary->get(name,record_idx,field_idx,value,false,
                       allow_extend,allow_derived))
        return true;
      if ((tile_idx >= 0) &&
          ((summary = access_relation(-1,comp_idx,inst_idx,true)) != NULL) &&
          summary->get(name,record_idx,field_idx,value,true,
                       allow_extend,allow_derived))
        return true;
    }
  if (!have_attribute)
    return false;

  if ((ap->num_marked_records <= record_idx) && allow_extend &&
      (ap->flags & this->CAN_EXTRAPOLATE))
    record_idx = ap->num_marked_records - 1;
  att_ptr += ap->num_fields*record_idx;
  if ((record_idx < 0) || (record_idx >= ap->num_marked_records) ||
      !att_ptr->is_set)
    return false;
  value = att_ptr->fval;
  return true;
}

/*****************************************************************************/
/*                             kdu_params::set (int)                         */
/*****************************************************************************/

void
  kdu_params::set(const char *name, int record_idx, int field_idx, int value)
{
  kd_attribute *ap;
  char const *cp;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,24); e <<
        KDU_TXT("Attempt to set a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if ((ap->flags & ALL_COMPONENTS) && (comp_idx != -1))
    { KDU_ERROR_DEV(e,25); e <<
        KDU_TXT("Attempt to set a non-tile-specific code-stream attribute "
        "in a specific component!\nThe attribute name is")
        << " \"" << name << "\".";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,26); e <<
        KDU_TXT("Attempt to set a code-stream attribute, with an "
        "invalid field index!\nThe attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  cp = ap->values[field_idx].pattern;
  if (*cp == 'F')
    { KDU_ERROR_DEV(e,27);  e <<
        KDU_TXT("Attempting to set a floating point code-stream parameter "
        "attribute field with the integer access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
  else if (*cp == 'B')
    {
      if ((value & 1) != value)
        { KDU_ERROR_DEV(e,28); e <<
            KDU_TXT("Attempting to set a boolean code-stream parameter "
            "attribute field with an integer not equal to 0 or 1!\n"
            "The attribute name is")
            << " \"" << name << "\".";
        }
    }
  else if (*cp == '(')
    {
      char buf[80];
      int val;

      do {
          cp = parse_translator_entry(cp+1,',',buf,80,val);
        } while ((*cp == ',') && (val != value));
      if (val != value)
        { KDU_ERROR_DEV(e,29); e <<
            KDU_TXT("Attempting to set a code-stream attribute "
            "field using an integer value which does not match any "
            "of the defined translation values for the field!\n"
            "The attribute name is")
            << " \"" << name << "\".";
        }
    }
  else if (*cp == '[')
    {
      char buf[80];
      int val, tmp=0;

      do {
          cp = parse_translator_entry(cp+1,'|',buf,80,val);
          if ((value & val) == val)
            tmp |= val; // Word contains this flag (or this set of flags).
        } while (*cp == '|');
      if (tmp != value)
        { KDU_ERROR_DEV(e,30);  e <<
            KDU_TXT("Attempting to set a code-stream attribute "
            "field using an integer value which is incompatible "
            "with the flags defined for the field!\n"
            "The attribute name is")
            << " \"" << name << "\".";
        }
    }
  else
    assert((*cp == 'I') || (*cp == 'C'));
  bool new_change = false;
  if (record_idx >= ap->num_used_records)
    {
      ap->augment_records(record_idx+1);
      new_change = true;
    }
  if (record_idx >= ap->num_marked_records)
    ap->num_marked_records = record_idx+1;
  assert((record_idx >= 0) && (record_idx < ap->num_used_records));
  att_val *av = ap->values + field_idx+ap->num_fields*record_idx;
  if (!(av->is_set && (av->ival == value)))
    new_change = true;
  if (new_change && !changed)
    {
      kdu_params *scan = this; scan->changed = true;
      scan = scan->first_inst; scan->changed = true;
      scan = scan->refs[0]; scan->changed = true;
      scan = scan->first_cluster; scan->changed = true;
    }
  av->is_set = true;
  av->ival = value;
  this->empty = false;
}

/*****************************************************************************/
/*                             kdu_params::set (bool)                        */
/*****************************************************************************/

void
  kdu_params::set(const char *name, int record_idx, int field_idx, bool value)
{
  kd_attribute *ap;
  char const *cp;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,31);  e <<
        KDU_TXT("Attempt to set a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if ((ap->flags & ALL_COMPONENTS) && (comp_idx != -1))
    { KDU_ERROR_DEV(e,32); e <<
        KDU_TXT("Attempt to set a non-tile-specific code-stream attribute "
        "in a specific component!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,33);  e <<
        KDU_TXT("Attempt to set a code-stream attribute, with an "
        "invalid field index!\n"
        "The attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  cp = ap->values[field_idx].pattern;
  if (*cp != 'B')
    { KDU_ERROR_DEV(e,34); e <<
        KDU_TXT("Attempting to set a non-boolean code-stream parameter "
        "attribute field with the boolean access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
  bool new_change = false;
  if (record_idx >= ap->num_used_records)
    {
      ap->augment_records(record_idx+1);
      new_change = true;
    }
  if (record_idx >= ap->num_marked_records)
    ap->num_marked_records = record_idx+1;
  assert((record_idx >= 0) && (record_idx < ap->num_used_records));
  att_val *av = ap->values + field_idx+ap->num_fields*record_idx;
  if (!(av->is_set && (av->ival == ((value)?1:0))))
    new_change = true;
  if (new_change && !changed)
    {
      kdu_params *scan = this; scan->changed = true;
      scan = scan->first_inst; scan->changed = true;
      scan = scan->refs[0]; scan->changed = true;
      scan = scan->first_cluster; scan->changed = true;
    }
  av->is_set = true;
  av->ival = (value)?1:0;
  this->empty = false;
}

/*****************************************************************************/
/*                            kdu_params::set (float)                        */
/*****************************************************************************/

void
  kdu_params::set(const char *name, int record_idx, int field_idx,
                  double value)
{
  kd_attribute *ap;
  char const *cp;

  assert((record_idx >= 0) && (field_idx >= 0));
  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,35); e <<
        KDU_TXT("Attempt to set a code-stream attribute using "
        "the invalid name")
        << ", \"" << name << "\"!";
    }
  if ((ap->flags & ALL_COMPONENTS) && (comp_idx != -1))
    { KDU_ERROR_DEV(e,36); e <<
        KDU_TXT("Attempt to set a non-tile-specific code-stream attribute "
        "in a specific component!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
  if (field_idx >= ap->num_fields)
    { KDU_ERROR_DEV(e,37); e <<
        KDU_TXT("Attempt to set a code-stream attribute, with an "
        "invalid field index!\n"
        "The attribute name is")
        << " \"" << name << "\".\n" <<
        KDU_TXT("The field index is ") << field_idx << ".";
    }
  cp = ap->values[field_idx].pattern;
  if (*cp != 'F')
    { KDU_ERROR_DEV(e,38); e <<
        KDU_TXT("Attempting to set an integer code-stream parameter "
        "attribute field with the floating point access method!\n"
        "The attribute name is")
        << " \"" << name << "\".";
    }
  bool new_change = false;
  if (record_idx >= ap->num_used_records)
    {
      ap->augment_records(record_idx+1);
      new_change = true;
    }
  if (record_idx >= ap->num_marked_records)
    ap->num_marked_records = record_idx+1;
  assert((record_idx >= 0) && (record_idx < ap->num_used_records));
  att_val *av = ap->values + field_idx+ap->num_fields*record_idx;
  if (!(av->is_set && (av->fval == (float) value)))
    new_change = true;
  if (new_change && !changed)
    {
      kdu_params *scan = this; scan->changed = true;
      scan = scan->first_inst; scan->changed = true;
      scan = scan->refs[0]; scan->changed = true;
      scan = scan->first_cluster; scan->changed = true;
    }
  av->is_set = true;
  av->fval = (float) value;
  this->empty = false;
}

/*****************************************************************************/
/*                            kdu_params::set_derived                        */
/*****************************************************************************/

void
  kdu_params::set_derived(const char *name)
{
  kd_attribute *ap;

  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,39); e <<
        KDU_TXT("Invalid attribute name")
        << ", \"" << name << "\", " <<
        KDU_TXT("supplied to the `kdu_params::set_derived' function.");
    }
  ap->derived = true;
}

/*****************************************************************************/
/*                          kdu_params::parse_string                         */
/*****************************************************************************/

bool
  kdu_params::parse_string(const char *string)
{
  kd_attribute *ap;
  char *delim;
  size_t name_len;

  for (delim=(char *) string; *delim != '\0'; delim++)
    if ((*delim == ' ') || (*delim == '\t') || (*delim == '\n'))
      { KDU_ERROR(e,40); e <<
          KDU_TXT("Malformed attribute string")
          << ", \"" << string << "\"!\n" <<
          KDU_TXT("White space characters are illegal!");
      }
    else if ((*delim == ':') || (*delim == '='))
      break;
  name_len = (size_t)(delim - string);
  for (ap=attributes; ap != NULL; ap=ap->next)
    if ((strncmp(ap->name,string,name_len)==0) && (strlen(ap->name)==name_len))
      break;
  if (ap == NULL)
    {
      if (this == first_cluster)
        {
          kdu_params *scan;

          for (scan=this->next_cluster; scan != NULL; scan=scan->next_cluster)
            if (scan->parse_string(string))
              return(true);
        }
      return false;
    }
  assert(ap != NULL);
  if (*delim == '\0')
    { KDU_ERROR(e,41); e <<
        KDU_TXT("Attribute")
        << " \"" << ap->name << "\" " <<
        KDU_TXT("is missing parameters:\n\n\t");
      ap->describe(e,allow_tiles,allow_comps,
                   treat_instances_like_components,true);
      e << KDU_TXT("\nParameter values must be separated from the attribute "
        "name and optional location specifiers by an '=' sign!\n");
    }

  int target_tile = -2;
  int target_comp = -2;
  int target_inst = -1;

  if (*delim == ':')
    {
      delim++;
      while (*delim != '=')
        if (*delim == '\0')
          break;
        else if ((*delim == 'T') && (target_tile < -1))
          target_tile = (int) strtol(delim+1,&delim,10);
        else if ((*delim == 'C') && (target_comp < -1))
          target_comp = (int) strtol(delim+1,&delim,10);
        else if ((*delim == 'I') && (target_inst < 0))
          target_inst = (int) strtol(delim+1,&delim,10);
        else
          { KDU_ERROR(e,42); e <<
              KDU_TXT("Malformed location specifier encountered in attribute "
              "string")
              << ", \"" << string << "\"!\n"; e <<
              KDU_TXT("Tile specifiers following the the colon must have the "
              "form \"T<num>\", while component specifiers must have the "
              "form \"C<num>\" and index specifiers must have the "
              "form \"I<num>\". There may be at most one of each!");
          }
    }
  if (target_tile < -1)
    target_tile = this->tile_idx;
  if (target_comp < -1)
    target_comp = this->comp_idx;
  if (target_inst < 0)
    {
      if ((target_tile == this->tile_idx) && (target_comp == this->comp_idx))
        target_inst = this->inst_idx;
      else if (!treat_instances_like_components)
        target_inst = 0;
      else
        { KDU_ERROR(e,0x21040500); e << // Unique error # = day/month/year/idx
            KDU_TXT("Malformed location specifier encountered in attribute "
            "string")
            << ", \"" << string << "\"!\n"; e <<
            KDU_TXT("You must supply an index specifier of the form "
            "\"I<num>\" for this type of parameter.");
          }
    }
  else if (!treat_instances_like_components)
    { KDU_ERROR(e,0x21040501); e << // Unique error # = day/month/year/idx
        KDU_TXT("Malformed location specifier encountered in attribute "
        "string")
        << ", \"" << string << "\"!\n"; e <<
        KDU_TXT("This type of parameter cannot be used with an index "
        "specifier (i.e., a specifier of the form \"I<num>\").");
    }

  // Locate the object to which the attribute belongs.
  if ((this->tile_idx != target_tile) || (this->comp_idx != target_comp) ||
      (this->inst_idx != target_inst))
    {
      kdu_params *target;

      target = access_relation(target_tile,target_comp,target_inst,false);
      if (target == NULL)
        { KDU_ERROR(e,43); e <<
            KDU_TXT("Attribute string")
            << ", \"" << string << "\", " <<
            KDU_TXT("refers to a non-existent tile-component!");
        }
      return target->parse_string(string);
    }

  // Perform accessibility checks
  if (marked)
    { KDU_ERROR_DEV(e,44); e <<
        KDU_TXT("Illegal attempt to modify a `kdu_params' object "
        "which has already been marked!");
    }

  if ((ap->flags & ALL_COMPONENTS) && (comp_idx != -1))
    { KDU_ERROR(e,45);  e <<
        KDU_TXT("Attempt to set a non-tile-specific code-stream attribute "
        "in a specific component!\n"
        "Problem occurred while parsing the attribute string")
        << ", \"" << string << "\".";
    }

  if (*delim != '=')
    { KDU_ERROR(e,46); e <<
        KDU_TXT("Malformed attribute string")
        << ", \"" << string << "\"!\n" <<
        KDU_TXT("Parameter values must be separated from the attribute "
        "name and optional location specifiers by an '=' sign!");
    }

  if ((ap->num_marked_records > 0) && ap->parsed)
    { // Attribute already set by parsing.
      if ((!allow_insts) || treat_instances_like_components ||
          ((next_inst == NULL) && (new_instance() == NULL)))
        { KDU_ERROR(e,47); e <<
            KDU_TXT("The supplied attribute string")
            << ", \"" << string << "\", " <<
            KDU_TXT("refers to code-stream parameters which have already "
            "been parsed out of some string.  Moreover, multiple instances of "
            "this attribute are not permitted here!");
        }
      assert(next_inst != NULL);
      return next_inst->parse_string(string);
    }

  delete_unparsed_attribute(ap->name);
  ap->parsed = true;

  // Finally, we get around to parsing the parameter values.
  bool new_change = false;
  bool open_record;
  int fld, rec = 0;
  char *cp = delim + 1;
  while (*cp != '\0')
    { // Process a record.
      if (rec > 0)
        {
          if (*cp != ',')
            { KDU_ERROR(e,48); e <<
                KDU_TXT("Malformed attribute string")
                << ", \"" << string << "\"!\n" <<
                KDU_TXT("Problem encountered at")
                << " \"" << cp << "\".\n" <<
                KDU_TXT("Records must be separated by commas.");
            }
          cp++; // Skip over the inter-record comma.
        }
      if ((rec > 0) && !(ap->flags & MULTI_RECORD))
        { KDU_ERROR(e,49); e <<
            KDU_TXT("Malformed attribute string")
            << ", \"" << string << "\"!\n" <<
            KDU_TXT("Attribute does not support multiple parameter "
            "records!");
        }
      open_record = false;
      if (*cp == '{')
        { cp++; open_record = true; }
      else if (ap->num_fields > 1)
        { KDU_ERROR(e,50);  e <<
            KDU_TXT("Malformed attribute string")
            << ", \"" << string << "\"!\n" <<
            KDU_TXT("Problem encountered at")
            << " \"" << cp << "\".\n" <<
            KDU_TXT("Records must be enclosed by curly braces, i.e., "
            "'{' and '}'.");
        }
      if (ap->num_used_records <= rec)
        {
          new_change = true;
          ap->augment_records(rec+1);
        }
      if (rec >= ap->num_marked_records)
        ap->num_marked_records = rec+1;
      for (fld=0; fld < ap->num_fields; fld++)
        {
          if (fld > 0)
            {
              if (*cp != ',')
                { KDU_ERROR(e,51); e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp <<"\".\n" <<
                    KDU_TXT("Fields must be separated by commas.");
                }
              cp++; // Skip over the inter-field comma.
            }
          att_val *att = ap->values + rec*ap->num_fields + fld;
          char const *pp = att->pattern;
          if (*pp == 'F')
            {
              float parsed_fval = (float) strtod(cp,&delim);
              if (!(att->is_set && (att->fval == parsed_fval)))
                new_change = true;
              att->fval = parsed_fval;
              if (delim == cp)
                { KDU_ERROR(e,52); e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp << "\".\n" <<
                    KDU_TXT("Expected a floating point field.");
                }
              cp = delim;
            }
          else if (*pp == 'I')
            {
              int parsed_ival = (int) strtol(cp,&delim,10);
              if (!(att->is_set && (att->ival == parsed_ival)))
                new_change = true;
              att->ival = parsed_ival;
              if (delim == cp)
                { KDU_ERROR(e,53); e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp << "\".\n" <<
                    KDU_TXT("Expected an integer field.");
                }
              cp = delim;
            }
          else if (*pp == 'C')
            {
              int parsed_chars, parsed_ival;
              parsed_chars = custom_parse_field(cp,ap->name,fld,parsed_ival);
              if (!(att->is_set && (att->ival == parsed_ival)))
                new_change = true;
              att->ival = parsed_ival;
              if (parsed_chars <= 0)
                { KDU_ERROR(e,0x26040500); e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp << "\".\n" <<
                    KDU_TXT("Attempt to parse custom string representation "
                    "failed.  Read usage information carefully.");
                }
              cp += parsed_chars;
            }
          else if (*pp == 'B')
            {
              int old_ival = att->ival;
              if (strncmp(cp,"yes",3) == 0)
                { att->ival = 1; cp += 3; }
              else if (strncmp(cp,"no",2) == 0)
                { att->ival = 0; cp += 2; }
              else
                { KDU_ERROR(e,54); e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp << "\".\n" <<
                    KDU_TXT("Expected a boolean field identifier, i.e., "
                    "one of \"yes\" or \"no\".");
                }
              if (!(att->is_set && (old_ival == att->ival)))
                new_change = true;
            }
          else if (*pp == '(')
            {
              char buf[80], *bp, *dp;
              int val=0;
              bool success = false;

              do {
                  pp = parse_translator_entry(pp+1,',',buf,80,val);
                  for (bp=buf, dp=cp; *bp != '\0'; bp++, dp++)
                    if (*bp != *dp)
                      break;
                    success = (*bp == '\0') &&
                      ((*dp==',') || (*dp=='}') || (*dp=='\0'));
                } while ((*pp == ',') && !success);
              if (!success)
                { KDU_ERROR(e,55);  e <<
                    KDU_TXT("Malformed attribute string")
                    << ", \"" << string << "\"!\n" <<
                    KDU_TXT("Problem encountered at")
                    << " \"" << cp << "\".\n";
                    display_options(att->pattern,e);
                }
              if (!(att->is_set && (att->ival == val)))
                new_change = true;
              att->ival = val;
              cp = dp;
            }
          else if (*pp == '[')
            {
              int old_val = att->ival;
              att->ival = 0;
              do {
                  char buf[80], *bp, *dp;
                  int val;
                  bool success = false;
                  
                  pp = att->pattern;
                  if (*cp == '|')
                    cp++;
                  do {
                      pp = parse_translator_entry(pp+1,'|',buf,80,val);
                      for (bp=buf, dp=cp; *bp != '\0'; bp++, dp++)
                        if (*bp != *dp)
                          break;
                        success = (*bp == '\0') &&
                          ((*dp==',')||(*dp=='}')||(*dp=='|')||(*dp=='\0'));
                    } while ((*pp == '|') && !success);
                  if ((!success) && (*cp == '0') &&
                      ((cp[1]==',') || (cp[1]=='}') ||
                       (cp[1]=='|') || (cp[1]=='\0')))
                    {
                      success = true;
                      val = 0;
                      dp = cp+1;
                    }
                  if (!success)
                    { KDU_ERROR(e,56); e <<
                        KDU_TXT("Malformed attribute string")
                        << ", \"" << string << "\"!\n" <<
                        KDU_TXT("Problem encountered at")
                        << " \"" << cp << "\".\n";
                      display_options(att->pattern,e);
                    }
                  att->ival |= val;
                  cp = dp;
                } while (*cp == '|');
              if (!(att->is_set && (old_val == att->ival)))
                new_change = true;
            }
          else
            assert(0);
          att->is_set = true;
        }
      if (*cp == '}')
        cp++;
      else if (open_record)
        { KDU_ERROR(e,57); e <<
            KDU_TXT("Malformed attribute string")
            << ", \"" << string << "\"!\n" <<
            KDU_TXT("Problem encountered at")
            << " \"" << cp << "\".\n" <<
            KDU_TXT("Opening brace for record is not matched by a "
                    "closing brace.");
        }
      rec++;
    }

  if (new_change && !changed)
    {
      kdu_params *scan = this; scan->changed = true;
      scan = scan->first_inst; scan->changed = true;
      scan = scan->refs[0]; scan->changed = true;
      scan = scan->first_cluster; scan->changed = true;
    }

  this->empty = false;
  return true;
}

/*****************************************************************************/
/*                          kdu_params::parse_string                         */
/*****************************************************************************/

bool
  kdu_params::parse_string(const char *string, int which_tile)
{
  int target_tile = -1;
  char *delim = strchr((char *) string,':'); // Need (char *) cast for Solaris
  if (delim != NULL)
    {
      delim++;
      while ((*delim != 'T') && (*delim != '=') && (*delim != '\0'))
        delim++;
      if (*delim == 'T')
        target_tile = (int) strtol(delim+1,NULL,10);
    }
  if (target_tile != which_tile)
    return false;
  return parse_string(string);
}

/*****************************************************************************/
/*                kdu_params::textualize_attributes  (one only)              */
/*****************************************************************************/

void
  kdu_params::textualize_attributes(kdu_message &output, bool skip_derived)
{
  kd_attribute *ap;
  int rec, fld;

  for (ap=attributes; ap != NULL; ap=ap->next)
    {
      if (ap->num_marked_records == 0)
        continue;
      if (ap->derived && skip_derived)
        continue;
      output << ap->name;
      if ((comp_idx >= 0) || (tile_idx >= 0) ||
          treat_instances_like_components)
        { // Write the location specifier.
          output << ':';
          if (tile_idx >= 0)
            output << "T" << tile_idx;
          if (comp_idx >= 0)
            output << "C" << comp_idx;
          if (treat_instances_like_components)
            output << "I" << inst_idx;
        }
      output << '=';
      for (rec=0; rec < ap->num_marked_records; rec++)
        {
          if (rec > 0)
            output << ',';
          if (ap->num_fields > 1)
            output << '{';
          for (fld=0; fld < ap->num_fields; fld++)
            {
              att_val *att = ap->values + rec*ap->num_fields + fld;
              char const *cp;
                
              if (fld > 0)
                output << ',';
              if (!att->is_set)
                { KDU_ERROR_DEV(e,58); e <<
                    KDU_TXT("Attempting to textualize a code-stream parameter "
                    "attribute, which has only partially been set!\n"
                    "Error occurred in attribute")
                    << " \"" << ap->name << "\" " <<
                    KDU_TXT("in field ") << fld <<
                    KDU_TXT(" of record ") << rec << ".";
                }
              cp = att->pattern;
              if (*cp == 'F')
                output << att->fval;
              else if (*cp == 'I')
                output << att->ival;
              else if (*cp == 'B')
                output << ((att->ival)?"yes":"no");
              else if (*cp == 'C')
                custom_textualize_field(output,ap->name,fld,att->ival);
              else if (*cp == '(')
                {
                  char buf[80];
                  int val;
                  
                  do {
                      cp = parse_translator_entry(cp+1,',',buf,80,val);
                      if (val == att->ival)
                        break;
                    } while (*cp == ',');
                  assert(val == att->ival);
                  output << buf;
                }
              else if (*cp == '[')
                {
                  char buf[80];
                  int val, acc=0;
                  
                  if (att->ival == 0)
                    output << 0;
                  do {
                      cp = parse_translator_entry(cp+1,'|',buf,80,val);
                      if (((att->ival & val) == val) && ((acc | val) > acc))
                        {
                          output << buf;
                          acc |= val;
                          if (acc == att->ival)
                            break;
                          output << ',';
                        }
                    } while (*cp == '|');
                  assert(acc == att->ival);
                }
              else
                assert(0);
            }
          if (ap->num_fields > 1)
            output << '}';
        }
      output << '\n';
    }
}

/*****************************************************************************/
/*            kdu_params::textualize_attributes  (list processing)           */
/*****************************************************************************/

void
  kdu_params::textualize_attributes(kdu_message &output, int min_tile,
                                    int max_tile, bool skip_derived)
{
  int min_t = (min_tile < -1)?-1:min_tile;
  int max_t = (max_tile >= num_tiles)?(num_tiles-1):max_tile;
  if (tile_idx >= 0)
    { // One tile only
      if ((tile_idx < min_t) || (tile_idx > max_t))
        return;
      min_t = max_t = tile_idx;
    }
  if (inst_idx != 0)
    {
      textualize_attributes(output,skip_derived);
      return;
    }
  int min_c = -1;
  int max_c = num_comps-1;
  if (comp_idx >= 0)
    min_c = max_c = comp_idx;

  int t, c;
  kdu_params *scan, **rp, **rpp = refs+(min_t+1)*(num_comps+1)+(min_c+1);
  for (t=min_t; t <= max_t; t++, rpp+=num_comps+1)
    for (rp=rpp, c=min_c; c <= max_c; c++, rp++)
      {
        scan = *rp;
        if ((scan->comp_idx != c)  || (scan->tile_idx != t))
          continue; // Whole object is not unique
        for (; scan != NULL; scan=scan->next_inst)
          scan->textualize_attributes(output,skip_derived);
      }
  if (this == first_cluster)
    for (scan=this->next_cluster; scan != NULL; scan=scan->next_cluster)
      scan->textualize_attributes(output,min_tile,max_tile,skip_derived);
}

/*****************************************************************************/
/*                      kdu_params::describe_attributes                      */
/*****************************************************************************/

void
  kdu_params::describe_attributes(kdu_message &output, bool include_comments)
{
  kd_attribute *ap;

  for (ap=attributes; ap != NULL; ap=ap->next)
    ap->describe(output,allow_tiles,allow_comps,
                 treat_instances_like_components,include_comments);
}

/*****************************************************************************/
/*                      kdu_params::describe_attribute                       */
/*****************************************************************************/

void
  kdu_params::describe_attribute(const char *name,
                                 kdu_message &output, bool include_comments)
{
  kd_attribute *ap;

  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,59); e <<
        KDU_TXT("\"kdu_params::describe_attribute\" invoked with an "
        "invalid attribute identifier")
        << ", \"" << name << "\".";
    }
  ap->describe(output,allow_tiles,allow_comps,
               treat_instances_like_components,include_comments);
}

/*****************************************************************************/
/*                          kdu_params::find_string                          */
/*****************************************************************************/

kdu_params *
  kdu_params::find_string(char *string, const char * &name)
{
  kd_attribute *ap;
  char *delim;
  size_t name_len;

  for (delim=string; *delim != '\0'; delim++)
    if ((*delim == ' ') || (*delim == '\t') || (*delim == '\n'))
      return NULL;
    else if ((*delim == ':') || (*delim == '='))
      break;
  name_len = (size_t)(delim - string);
  for (ap=attributes; ap != NULL; ap=ap->next)
    if ((strncmp(ap->name,string,name_len)==0) && (strlen(ap->name)==name_len))
      break;
  if (ap == NULL)
    {
      if (this == first_cluster)
        {
          kdu_params *scan, *result;

          for (scan=this->next_cluster; scan != NULL; scan=scan->next_cluster)
            if ((result = scan->find_string(string,name)) != NULL)
              return(result);
        }
      return NULL;
    }
  assert(ap != NULL);
  name = ap->name;
  if (*delim == '\0')
    return this;

  int target_tile = -2;
  int target_comp = -2;
  int target_inst = -1;

  if (*delim == ':')
    {
      delim++;
      while (*delim != '=')
        if (*delim == '\0')
          break;
        else if ((*delim == 'T') && (target_tile < -1))
          target_tile = (int) strtol(delim+1,&delim,10);
        else if ((*delim == 'C') && (target_comp < -1))
          target_comp = (int) strtol(delim+1,&delim,10);
        else if ((*delim == 'I') && (target_inst < 0))
          target_inst = (int) strtol(delim+1,&delim,10);
        else
          return NULL;
    }
  if (target_tile < -1)
    target_tile = this->tile_idx;
  if (target_comp < -1)
    target_comp = this->comp_idx;
  if (target_inst < 0)
    {
      if ((target_tile == this->tile_idx) && (target_comp == this->comp_idx))
        target_inst = this->inst_idx;
      else if (!treat_instances_like_components)
        target_inst = 0;
      else
        return NULL;
    }

  // Locate the object to which the attribute belongs.
  if ((this->tile_idx != target_tile) || (this->comp_idx != target_comp) ||
      (this->inst_idx != target_inst))
    {
      kdu_params *target;
      target = access_relation(target_tile,target_comp,target_inst,false);
      if (target == NULL)
        return this;
      return target->find_string(string,name);
    }
  return this;
}

/*****************************************************************************/
/*                  kdu_params::delete_unparsed_attribute                    */
/*****************************************************************************/

void
  kdu_params::delete_unparsed_attribute(const char *name)
{
  kd_attribute *ap;

  if ((ap=match_attribute(attributes,name)) == NULL)
    { KDU_ERROR_DEV(e,60); e <<
        KDU_TXT("Attempting to delete a non-existent attribute with "
        "\"kdu_params::delete_unparsed_attribute\".");
    }
  if (!ap->parsed)
    { 
      int num_fields = ap->num_used_records * ap->num_fields;
      if (num_fields && !changed)
        {
          kdu_params *scan = this; scan->changed = true;
          scan = scan->first_inst; scan->changed = true;
          scan = scan->refs[0]; scan->changed = true;
          scan = scan->first_cluster; scan->changed = true;
        }
      for (int n=0; n < num_fields; n++)
        ap->values[n].is_set = false;
      ap->num_used_records = 0;
      ap->num_marked_records = 0;
    }

  kdu_params *scan;
  if (this != first_inst)
    return;

  for (scan=next_inst; scan != NULL; scan=scan->next_inst)
    scan->delete_unparsed_attribute(name);
  
  if (comp_idx >= 0)
    return;

  // Must be at least a tile head; scan all components in the tile
  int n;
  kdu_params **rp;
  for (rp=refs+(tile_idx+1)*(num_comps+1)+1, n=num_comps; n > 0; n--, rp++)
    if (((scan = *rp) != NULL) && (scan != this))
      scan->delete_unparsed_attribute(name);

  if (tile_idx < 0)
    { // Scan all tiles in the cluster
      for (rp=refs+(num_comps+1), n=num_tiles; n > 0; n--, rp+=(num_comps+1))
        if (((scan = *rp) != NULL) && (scan != this))
          scan->delete_unparsed_attribute(name);
    }
}

/*****************************************************************************/
/*                    kdu_params::translate_marker_segment                   */
/*****************************************************************************/

bool
  kdu_params::translate_marker_segment(kdu_uint16 code, int num_bytes,
                                       kdu_byte bytes[], int which_tile,
                                       int tpart_idx)
{
  int c_idx=-1, i_idx=0;
  kdu_params *target, *cluster = refs[0]->first_cluster;
  for (; cluster != NULL; cluster = cluster->next_cluster)
    {
      if (cluster->num_tiles <= which_tile)
        continue;
      if (cluster->check_marker_segment(code,num_bytes,bytes,c_idx))
        break;
    }
  if (cluster == NULL)
    return false;
  if (cluster->treat_instances_like_components)
    {
      i_idx = c_idx;
      c_idx = -1;
    }
  target = cluster->access_relation(which_tile,c_idx,i_idx,false);
      // The above call creates a unique target object, if necessary

  if (target == NULL)
    { KDU_ERROR(e,0x25100601); e <<
        KDU_TXT("Codestream contains a parameter marker segment with an "
                "invalid image component or tile index: parameter type is")
          << " \"" << cluster->cluster_name << "\"; " <<
        KDU_TXT("tile number is")
          << " " << which_tile
          << ((which_tile<0)?" [i.e., global]":" (starting from 0)")
          << "; " <<
        KDU_TXT("component index is")
          << " " << c_idx << ((c_idx<0)?" [i.e., global]":" (starting from 0)")
          << ".";
    }


  if (target->allow_insts && !target->treat_instances_like_components)
    { // Skip over already marked instances
      for (; target != NULL; target=target->next_inst)
        if (!target->marked)
          break;
    }
  if ((target != NULL) &&
      target->read_marker_segment(code,num_bytes,bytes,tpart_idx))
    {
      target->marked = true;
      if (target->allow_insts && !target->treat_instances_like_components)
        target->new_instance();
      target->empty = false;
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                    kdu_params::generate_marker_segments                   */
/*****************************************************************************/

int
  kdu_params::generate_marker_segments(kdu_output *out, int which_tile,
                                       int tpart_idx)
{
  int total_bytes, new_bytes;
  kdu_params *cluster = refs[0]->first_cluster;

  total_bytes = 0;
  for (; cluster != NULL; cluster=cluster->next_cluster)
    {
      if (which_tile >= cluster->num_tiles)
        continue;
      int c, ref_idx = (which_tile+1)*(cluster->num_comps+1);
      kdu_params *tile = cluster->refs[ref_idx];
      for (c=-1; c < cluster->num_comps; c++, ref_idx++)
        {
          kdu_params *last_marked, *inst;
          kdu_params *comp = cluster->refs[ref_idx];
          if ((comp->tile_idx != which_tile) &&
              ((tile->tile_idx != which_tile) || !tile->marked))
            continue; // All parameters inherited from main header default
                      // (global, or component-specific) and there is no
                      // separate tile header, from which parameters would
                      // otherwise be inherited.
          if (comp->comp_idx != c)
            {
              if (!comp->force_comps)
                continue; // All params inherited from tile or cluster default
              assert((comp->comp_idx == -1) && (c >= 0));
              comp = comp->access_relation(which_tile,c,0,false);
              assert((comp==cluster->refs[ref_idx]) && (comp->comp_idx==c));
            }
          if ((c >= 0) && (tile->tile_idx==which_tile) && tile->marked)
            last_marked = tile; // Defaults derived from tile header
          else
            { // Defaults derived from main header
              last_marked = cluster->refs[c+1];
              if (!last_marked->marked)
                last_marked = (cluster->marked)?cluster:NULL;
            }
          if (last_marked == comp)
            last_marked = NULL;

          for (inst=comp; inst != NULL; inst=inst->next_inst)
            {
              if (inst->treat_instances_like_components)
                { // Find `last_marked' object from main header entry with
                  // same instance number, if one exists
                  if (inst->tile_idx < 0)
                    last_marked = NULL;
                  else
                    {
                      for (last_marked=cluster; last_marked != NULL;
                           last_marked=last_marked->next_inst)
                        if (last_marked->inst_idx >= inst->inst_idx)
                          break;
                      if ((last_marked == NULL) ||
                          (last_marked->inst_idx != inst->inst_idx) ||
                          !last_marked->marked)
                        last_marked = NULL;
                    }
                }
              new_bytes =
                inst->write_marker_segment(out,last_marked,tpart_idx);
              if (new_bytes > 0)
                {
                  assert(new_bytes >= 4);
                  total_bytes += new_bytes;
                  inst->marked = true;
                }
              last_marked = (inst->marked)?inst:NULL;
            }
        }
    }
  return(total_bytes);
}

/*****************************************************************************/
/*                          kdu_params::finalize_all                         */
/*****************************************************************************/

void
  kdu_params::finalize_all(bool after_reading)
{
  kdu_params *scan;

  finalize(after_reading);
  
  if (after_reading)
    { 
      bool new_change = false;
      for (kd_attribute *ap=attributes; ap != NULL; ap=ap->next)
        if (ap->remove_unmarked_records())
          new_change = true;
      if (new_change && !changed)
        { 
          kdu_params *scan = this; scan->changed = true;
          scan = scan->first_inst; scan->changed = true;
          scan = scan->refs[0]; scan->changed = true;
          scan = scan->first_cluster; scan->changed = true;
        }
    }  

  if (this == first_inst)
    for (scan=next_inst; scan != NULL; scan=scan->next_inst)
      scan->finalize(after_reading);

  if (comp_idx >= 0)
    return; // Not a tile head

  // Must be at least a tile head; finalize all components in the tile
  for (int c=0; c < num_comps; c++)
    {
      scan = refs[(tile_idx+1)*(num_comps+1)+c+1];
      if ((scan->comp_idx == c) && (scan->tile_idx == tile_idx))
        scan->finalize_all(after_reading);
    }

  if (tile_idx < 0)
    { // Finalize all tiles in the cluster
      for (int t=0; t < num_tiles; t++)
        {
          scan = refs[(t+1)*(num_comps+1)];
          if (scan->tile_idx == t)
            scan->finalize_all(after_reading);
        }
    }

  if (this == first_cluster)
    for (scan=next_cluster; scan != NULL; scan=scan->next_cluster)
      scan->finalize_all(after_reading);
}

/*****************************************************************************/
/*                          kdu_params::finalize_all                         */
/*****************************************************************************/

void
  kdu_params::finalize_all(int which_tile, bool after_reading)
{
  kdu_params *scan;

  if (tile_idx == which_tile)
    { 
      finalize(after_reading);

      if (after_reading)
        { 
          bool new_change = false;
          for (kd_attribute *ap=attributes; ap != NULL; ap=ap->next)
            if (ap->remove_unmarked_records())
              new_change = true;
          if (new_change && !changed)
            { 
              kdu_params *scan = this; scan->changed = true;
              scan = scan->first_inst; scan->changed = true;
              scan = scan->refs[0]; scan->changed = true;
              scan = scan->first_cluster; scan->changed = true;
            }
        }
    
      if (this == first_inst)
        for (scan=next_inst; scan != NULL; scan=scan->next_inst)
          scan->finalize(after_reading);
      if (comp_idx < 0)
        {
          for (int c=0; c < num_comps; c++)
            {
              scan = refs[(tile_idx+1)*(num_comps+1)+c+1];
              if ((scan->comp_idx == c) && (scan->tile_idx == tile_idx))
                scan->finalize_all(after_reading);
            }
        }
    }
  else if ((tile_idx < 0) && (comp_idx < 0) && (which_tile < num_tiles))
    {
      scan = refs[(which_tile+1)*(num_comps+1)];
      if ((scan != NULL) && (scan->tile_idx == which_tile))
        scan->finalize_all(after_reading);
    }

  if (this == first_cluster)
    for (scan=next_cluster; scan != NULL; scan=scan->next_cluster)
      scan->finalize_all(which_tile,after_reading);
}

/*****************************************************************************/
/*                        kdu_params::define_attribute                       */
/*****************************************************************************/

void
  kdu_params::define_attribute(const char *name, const char *comment,
                              const char *pattern, int flags)
  /* Makes sure the attributes appear in the list in the same order in
     which they are added. */
{
  kd_attribute *att, *scan;

  att = new kd_attribute(name,comment,flags,pattern);
  if ((scan = attributes) != NULL)
    {
      while (scan->next != NULL)
        scan = scan->next;
      scan->next = att;
    }
  else
    attributes = att;
}

/* ========================================================================= */
/*                               siz_params                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                       siz_params::siz_params (no args)                    */
/*****************************************************************************/

siz_params::~siz_params() {}
siz_params::siz_params()
  : kdu_params(SIZ_params,false,false,false)
{
  define_attribute(Sprofile,
          "Restricted profile to which the code-stream conforms.  The "
          "value must be an integer in the range 0 to 10, corresponding to "
          "one of the following identifiers: `PROFILE0', `PROFILE1', "
          "`PROFILE2', `PART2', `CINEMA2K', `CINEMA4K', `BROADCAST', "
          "`CINEMA2S', `CINEMA4S', `CINEMASS' or `IMF'.\n"
          "\t   PROFILE0 is the most restrictive profile for Part 1 "
          "conforming codestreams.  PROFILE2 places no restrictions on the "
          "code-stream other than those restrictions defined in "
          "ISO/IEC 15444-1 (JPEG2000, Part 1).  A value of 3 (`PART2') means "
          "that the codestream requires support for one or more features "
          "defined in ISO/IEC 15444-2 (JPEG2000, Part 2) -- "
          "additional information is provided via the `Sextensions' parameter "
          "attribute.\n"
          "\t   Values 4 and 5 (`CINEMA2K' and `CINEMA4K') correspond to "
          "profile restrictions for Digital Cinema, while 6 (`BROADCAST') "
          "identifies profile restrictions for broadcast applications.  The "
          "2K and 4K digital cinema profiles and the broadcast profiles "
          "are closely relatred and very restrictive subsets of JPEG2000 "
          "Part 1, defined by various ammendments to the original standard.\n"
          "\t   The values 7 and 8 (`CINEMA2S' and `CINEMA4S') correspond to "
          "scalable versions of the 2K and 4K digital cinema profiles, "
          "supporting 2 quality layers each, while value 9 (`CINEMASS') "
          "identifies the \"long term storage\" digital cinema profile, that "
          "supports dimensions up to 16384 x 8640 and 5 quality layers.\n"
          "\t   The value 10 is used for the IMF (Interoperable Master "
          "Format) profiles, which are characterized by main- and sub-levels, "
          "reversibility (or lack thereof) and three different maximum "
          "frame sizes (2K, 4K and 8K).  All of this auxiliary information is "
          "provided via the `Simf' attribute.\n"
          "\t   If the system determines that support for Part 2 features is "
          "required, the profile will be set automatically to 3.  Also, if "
          "the `Sbroadcast' attribute is present, the profile is set "
          "automatically to 6, while if `Simf' is present, the profile is "
          "set automatically to 10.  Otherwise the profile is not adjusted by "
          "Kakadu's codestream creation machinery.  However, if the profile "
          "is found to be insufficient, the system will generate a warning at "
          "the point where it first encounters an inconsistency; this might "
          "not occur until near the end of the processing in certain rare "
          "circumstances.\n"
          "\t   The system does perform some extensive checks for compliance "
          "with the Digital Cinema profiles when they are used, but only "
          "during codestream generation.  It makes every effort to set "
          "default values in such a way as to ensure comliance with Digital "
          "Cinema profiles, where they are used, but it is ultimately up to "
          "the user to supply `Creslengths' (and possibly `Cagglengths') "
          "attributes so as to ensure that compressed data sizes match the "
          "application-dependent constraints specified by the Digital Cinema "
          "profile specifications.\n"
          "\t   Similar considerations apply to the broadcast profile, "
          "except that it depends on additional information provided via the "
          "`Sbroadcast' parameter attribute (similar to the connection "
          "between the Part 2 profile and `Sextensions').\n"
          "\t\t[Defaults to Profile-2.]",
          "(PROFILE0=0,PROFILE1=1,PROFILE2=2,PART2=3,"
          "CINEMA2K=4,CINEMA4K=5,BROADCAST=6,CINEMA2S=7,CINEMA4S=8,"
          "CINEMASS=9,IMF=10)");
  define_attribute(Sextensions,
          "Logical OR of any combination of a number of flags, "
          "indicating extended features from Part 2 of the JPEG2000 "
          "standard which may be found in this codestream.  Note "
          "that the Kakadu codestream generation machinery will "
          "set these flags automatically based on features which "
          "are detected in other parameter objects.  Explanation: "
          "DC means arbitrary DC offset; VARQ means variable "
          "quantization; TCQ means trellis-coded quantization; "
          "PRECQ means precinct-dependent quantization; "
          "VIS means visual masking; SSO means single-sample-overlap "
          "transform; DECOMP means arbitrary decomposition styles; "
          "ANY_KNL means arbitrary transform kernels; "
          "SYM_KNL means arbitrary whole sample symmetric transform "
          "kernels; MCT means multi-component transform; "
          "NLT means non-linear point-transformation (formerly called CURVE); "
          "and ROI means extended region-of-interest signalling.\n"
          "\t\t[Defaults to 0.]",
          "[DC=1|VARQ=2|TCQ=4|PRECQ=2048|VIS=8|SSO=16|DECOMP=32"
          "|ANY_KNL=64|SYM_KNL=128|MCT=256|NLT=512|ROI=1024]");
  define_attribute(Sbroadcast,
          "This parameter attribute provides more specific details for the "
          "BROADCAST profile (`Sprofile'=6).  Its 3 fields have the following "
          "interpretation.  The first field identifies the broadcast profile "
          "level, which is currently required to lie in the range 0 through "
          "11.  The profile level identifies the maximum bit-rate (in Mbits/s "
          "meaning 10^6 bits/s) and sample-rate (in Msamples/s meaning "
          "10^6 samples/s) of a compressed video stream, as described in "
          "Ammendment 4 to IS15444-1 and subsequently augmented by "
          "Ammendment 8 to IS15444-1.  Levels are as follows: 0 means "
          "unconstrained; 1 means 200Mbits/s,65Msamples/s; "
          "2 means 200Mbits/s,130Msamples/s; "
          "3 means 200Mbits/s,195Msamples/s; "
          "4 means 400Mbits/s,260Msamples/s; "
          "5 means 800Mbits/s,520Msamples/s; "
          "L >= 6 means = 2^(L-6)*1600Mbits/s, 2^(L-6)*1200Msamples/s.\n"
          "\t   The system does not explicitly impose these constraints, "
          "since they depend upon the intended frame rate -- this can readily "
          "be done at the application level.\n"
          "\t   The second field indicates whether the single-tile (0) or "
          "multi-tile (1) variation of the profile is specified; the "
          "multi-tile variation allows for either 1 or 4 tiles per image, "
          "where multiple tiles must be identical in size and involve either "
          "4 vertical tiles or 2 tiles in each direction.  The third field "
          "indicates whether the irreversible or reversible transform "
          "variation of the profile is specified.\n"
          "\t   During codestream generation, this parameter attribute "
          "defaults to the single-tile, irreversible level 1 variant if "
          "`Sprofile' identifies the BROADCAST profile (6).  Also, if "
          "`Sbroadcast' is specified and `Sprofile' is not explicitly "
          "specified, the system will automatically set `Sprofile' to 6 "
          "(BROADCAST).",
          "I(single=0,multi=1)(irrev=0,rev=1)");
  define_attribute(Simf,
          "This parameter attribute provides detailed profile constraints "
          "to accompany the IMF (Interoperable Master Format) profile "
          "(`Sprofile=10).  Its three fields have the following "
          "interpretation.  The first field is the `main-level' of the "
          "profile, an integer in the range 0 to 11 that describes the "
          "maximum number of samples per second, taking into account "
          "frame rate, frame size and component sub-sampling rate, as "
          "explained in Ammendment 8 of IS15444-1: 0 means unspecified; "
          "1 means 65Msamples/s; 2 means 130Msamples/s; "
          "3 means 195Msamples/s; 4 means 260Msamples/s; "
          "5 means 520Msamples/s; and L >= 6 means 2^(L-6)*1200Msamples/s.  "
          "Here, 1 Msample/s means 10^6 samples/s.\n"
          "\t   The second field is the profile's sub-level, an integer in "
          "the range 0 to 9, which may not exceed max{1,`main_level'-2}, "
          "whose purpose is to identify the maximum bit-rate associated with "
          "the codestreams: 0 means unspecified; other values L mean "
          "2^L * 10^8 bits/s.\n"
          "\t   The third field indicates whether the profile is irreversible "
          "(0) or reversible (1), where the irreversible variant is required "
          "to be untiled, while the reversible case allows for untiled "
          "codestreams or tiled codestreams with tile dimensions (`Stiles') "
          "of 1024x1024, 2048x2048 or 4096x4096.\n"
          "\t   During codestream generation, if `Sprofile' identifies the "
          "IMF profile (10), this parameter attribute defaults to the "
          "main-level 0, sub-level 0, 2K irreversible variant.  Also, if "
          "`Simf' is specified and `Sprofile' is not explicitly specified, "
          "the system automatically sets `Sprofile' to 10 (IMF).",
          "II(irrev=0,rev=1)");
  define_attribute(Scap,
          "Flag indicating whether or not capabilities defined in ammendments "
          "to the standards, as recorded in the `CAP' marker segment, must "
          "be recognized for correct decoding to occur.  Currently, the "
          "only recognized capabilities from ammendments are managed by "
          "the `SCpart2_caps' attribute.  You should not generally set "
          "this attribute explicitly, but rely upon the default "
          "initialization procedure.\n"
          "\t\t[Defaults to false unless a critical capability is found in "
          "`SCpart2_caps'.]",
          "B");
  define_attribute(SCpart2_caps,
          "Logical OR of any combination of a number of flags, specifying "
          "the use of extended capabilities from an ammendment to Part-2 "
          "(IS15444-2) of the JPEG2000 family of standards.  Currently, only "
          "one such capability, EXTENDED_COD, is defined, corresponding to "
          "the possible presence of block-coder extensions in COD/COC marker "
          "segments.  This capability was introduced with IS15444-2/AMD4.\n"
          "\t\t[Defaults to 0.]",
          "[EXTENDED_COD=32768]");
  define_attribute(Ssize,
          "Canvas dimensions: vertical dimension first.\n"
          "\t\t[For compressors, this will "
          "normally be derived from the dimensions of the individual "
          "image components. Explicitly supplying the canvas "
          "dimensions may be desirable if the source image files "
          "do not indicate their dimensions, or if custom "
          "sub-sampling factors are desired.]",
          "II");
  define_attribute(Sorigin,
          "Image origin on canvas: vertical coordinate first.\n"
          "\t\t[Defaults to {0,0}, or the tile origin if one "
          "is given]",
          "II");
  define_attribute(Stiles,
          "Tile partition size: vertical dimension first.\n"
          "\t\t[Defaults to {0,0}]",
          "II");
  define_attribute(Stile_origin,
          "Tile origin on the canvas: vertical coordinate first.\n"
          "\t\t[Defaults to {0,0}]",
          "II");
  define_attribute(Scomponents,
          "Number of codestream image components.\n"
          "\t\t[For compressors, this will normally be deduced from the "
          "number and type of image files supplied to the compressor.  Note "
          "carefully, however, that if a multi-component transform is used, "
          "the number of codestream image components might not be equal to "
          "the number of `output image components' given by `Mcomponents'.  "
          "In this case, the value of `Mcomponents' and the corresponding "
          "`Mprecision' and `Msigned' attributes should generally be "
          "associated with the image files being read (for compression) or "
          "written (for decompression).]",
          "I");
  define_attribute(Ssigned,
          "Indicates whether each codestream image component contains signed "
          "or unsigned sample values.  Compression applications that derive "
          "precision and signed/unsigned information from input image files "
          "are recommended to explicitly set `Nsigned' only, leaving the "
          "internal machinery to automatically transfer `Nsigned' values to "
          "`Ssigned', which happens whenever there is no Part-2 "
          "multi-component transform (i.e., `Mcomponents'=0).  If there is a "
          "multi-component, `Nsigned' is usually sufficient to deduce "
          "`Msigned' automatically, but you will need to explicitly specify "
          "`Ssigned'.  If any value for `Ssigned' is specified explicitly, "
          "all values must be specified -- missing values are derived by "
          "auto-replicating the last value that was specified.",
          "B",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Sprecision,
          "Indicates the bit-depth of each codestream image component.  "
          "Compression applications that derive precision and signed/unsigned "
          "information from input image files are recommended to explicitly "
          "set `Nprecision' only, leaving the values to be copied to "
          "`Sprecision' automatically.  If there is a Part-2 multi-component "
          "transform (`Mcomponents' != 0), however, `Nprecision' is usually "
          "sufficient to deduce `Mprecision' automatically, but you will need "
          "to explicitly specify `Sprecision'.  If any value for `Sprecision' "
          "is specified explicitly, all values must be specified -- missing "
          "values are derived by auto-replicating the last value that was "
          "specified.",
          "I",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Ssampling,
          "Indicates the sub-sampling factors for each codestream image "
          "component. In each record, the vertical factor appears "
          "first, followed by the horizontal sub-sampling factor. "
          "The last supplied record is repeated indefinitely for "
          "all remaining components.\n"
          "\t\t[For compressors, a suitable set of sub-sampling "
          "factors will normally be deduced from the individual "
          "image component dimensions.]",
          "II",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Sdims,
          "Indicates the dimensions (vertical, then horizontal) of "
          "each individual image component. The last supplied "
          "record is repeated indefinitely for all remaining "
          "components.\n"
          "\t\t[For compressors, the image component dimensions will "
          "normally be deduced from the image files supplied to the "
          "compressor, but may be explicitly set if raw input "
          "files are to be used.]",
          "II",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Mcomponents,
          "Number of image components produced at the output of the inverse "
          "multi-component transform -- during compression, you may think of "
          "these as original image comonents.  In any event, we refer to "
          "them as \"MCT output components\", taking the perspective of "
          "the decompressor.  The value of `Mcomponents' may be smaller than "
          "or larger than the `Scomponents' value, which refers to the number "
          "of \"codestream image components\".  The codestream image "
          "components are supplied to the input of the inverse "
          "multi-component transform.  Note carefully, however, that "
          "for Kakadu to perform a forward multi-component transform on image "
          "data supplied to a compressor, the value of `Mcomponents' must be "
          "at least as large as `Scomponents' and the inverse multi-component "
          "transform must provide sufficient invertible transform blocks "
          "to derive the codestream components from the output image "
          "components.  In the special case where `Mcomponents' is 0, or not "
          "specified, there is no multi-component transform.  In this case, "
          "`Scomponents', `Ssigned' and `Sprecision' define the output image "
          "components."
          "\t\t[Defaults to 0.  You must explicitly set a non-zero value for "
          "this attribute if you want to use Part-2 multi-component "
          "transforms.  Compressors might be able to deduce this information "
          "from the input files, if they are aware that you want to perform "
          "a multi-component transform.]",
          "I");
  define_attribute(Msigned,
          "Indicates whether each MCT output component (see `Mcomponents' for "
          "a definition of \"MCT output components\") contains signed "
          "or unsigned sample values.  Compressors that obtain precision and "
          "signed/unsigned information from input image files are recommended "
          "to use this information to explicitly set `Nsigned', leaving "
          "`Msigned' to be deduced automatically (so long as `Mcomponents' > "
          "0). The only condition under which `Msigned' and `Nsigned' need to "
          "be explicitly specified separately is where a non-linear point "
          "transform (see `NLType') needs to change the signed "
          "characteristics of its samples -- probably not common.  If you do "
          "specify any `Msigned' values, you need to supply them all, or be "
          "content with the auto-replicating policy that replicates the last "
          "specified value as required.",
          "B",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Mprecision,
          "Indicates the bit-depth of each MCT output component (see "
          "`Mcomponents' for a definition of \"MCT output components\").  "
          "Compressors that obtain precision and signed/unsigned information "
          "from input image files are recommended to use this information to "
          "explicitly set `Nprecision', leaving `Mprecision' to be deduced "
          "automatically (so long as `Mcomponents' > 0).  The only condition "
          "under which `Mprecision' and `Nprecision' need to be explicitly "
          "specified separately is where a non-linear point transform "
          "(see `NLType') needs to change the precision of its samples.  "
          "If you do specify any `Mprecision' values, you need to supply them "
          "all, or be content with the auto-replicating policy that "
          "replicates the last specified value as required.",
          "I",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Ncomponents,
          "Number of image components produced at the ultimate output of the "
          "decompression process, after applying any required multi-component "
          "transform and any non-linear point transforms that may be defined. "
          "If `Mcomponents' is non-zero (i.e., if there is a Part-2 "
          "multi-component transform), `Ncomponents' and `Mcomponents' must "
          "be identical, so it is not strictly necessary to explicitly "
          "specify `Ncomponents' -- it can be auto-deduced.  If `Mcomponents' "
          "= 0, the values of `Ncomponents' and `Scomponents' must be "
          "identical, so again it is not strictly necessary to specify both.",
          "I");
  define_attribute(Nsigned,
          "Plays the same role as `Msigned' and `Ssigned', but describes the "
          "ultimate signed/unsigned attributes of the samples produced during "
          "decompression after applying any non-linear point transform.  "
          "Compressors are recommended to explicitly specify `Nsigned', "
          "leaving the internal machinery to automatically deduce `Msigned' "
          "or `Ssigned' from `Nsigned' (by copying).  If there is no Part-2 "
          "multi-component transform, `Ssigned' will be copied from `Nsigned' "
          "or vice-versa if either is not specified.  If there is a Part-2 "
          "multi-component transform (i.e., `Mcomponents' > 0), `Msigned' "
          "will be copied from `Nsigned' or vice-versa if either is not "
          "explicitly specified.  If you do specify any `Nsigned' attribute "
          "value, you must specify all `Ncomponents' of them or be content "
          "with the auto-replicating policy that replicates the last "
          "specified value as required.",
          "B",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Nprecision,
          "Plays the same role as `Mprecision' and `Sprecision', but "
          "describes the ultimate precision of the samples produced during "
          "decompression after applying any non-linear point transform.  "
          "Compressors are recommended to explicitly specify `Nprecision', "
          "leaving the internal machinery to automatically deduce "
          "`Mprecision' or `Sprecision' from `Nprecision' (by copying).  If "
          "there is no Part-2 multi-component transform, `Sprecision' will be "
          "copied from `Nprecision' or vice-versa if either is not "
          "specified.  If there is a Part-2 multi-component transform (i.e., "
          "`Mcomponents' > 0), `Mprecision' will be copied from `Nprecision' "
          "or vice-versa if either is not explicitly specified.  If you do "
          "specify any `Nprecision' attribute value, you must specify all "
          "`Ncomponents' of them or be content with the auto-replicating "
          "policy that replicates the last specified value as required.",
          "I",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Scbr,
        "This attribute is currently used only during content generation; it "
        "is not actually recorded in any marker segment in the codestream.  "
        "The presence of an `Scbr' attribute means that incremental flushing "
        "should be performed, or at least simulated (all content can still be "
        "flushed at the end) subject to a low latency objective.  The two "
        "parameters specify an integer flushing interval V and the maximum "
        "size Bmax of a notional \"leaky bucket\" that is inserted between "
        "the incremental flushing process and a constant rate data channel.  "
        "We now briefly explain these concepts.\n"
        "\t\t   The packet progression sequence (`Corder') for each tile is "
        "must be PCRL, and there will usually only be one tile.  No extra "
        "tile-parts are currently allowed, so each tile (usually just one) "
        "undergoes a series of flush operations that are separated by V rows "
        "of lowest resolution precincts from the lowest resolution of the "
        "first tile's first codestream component.  That is, each flush-set "
        "commences with the first packet from precinct row V*k of the lowest "
        "resolution in tile-component 0, where k=0,1,2,...  Normally, V will "
        "be 1, and precinct dimensions will be chosen such that all flush "
        "sets involve the same number of packets/precincts, except possibly "
        "the last one.  If image components have different sizes, you will "
        "most likely need to adjust the number of vertical decomposition "
        "levels and the precinct dimensions on a per-component basis to "
        "ensure that all components can flush the same number of precincts "
        "in each flush set.\n"
        "\t\t   The leaky bucket size Bmax is measured in terms of lines from "
        "the first codestream image component.  Bmax need not be "
        "an integer, but it will be converted internally to a leaky "
        "bucket size that is measured in bytes.  Bmax "
        "must be at least as large as the number of lines from the first "
        "component in each flush-set.  Normally, Bmax should be larger than "
        "this, so as to give the rate control algorithm some flexibility.  "
        "The overall target size L for the codestream, is converted to a "
        "data rate D = L/T, where T is the total number of samples in all "
        "subbands of the codestream.  The gap between one flush set and the "
        "next is considered to be S, where S is the number of samples in the "
        "flush set.  Notice that we are measuring time in terms of image "
        "samples, so the actual time gap between the flush sets is S/Rsample, "
        "where Rsample is the image sample data rate (samples/second), while "
        "the CBR channel has a data rate of D*Rsample bytes/second.  This "
        "inter-flush separation model provides a valid mechanism for "
        "assessing the delay attributed to communication, so long as all "
        "flush sets (except possibly the last one) have the same structure, "
        "so that flush sets naturally become available at a constant rate "
        "from the compressor; an informative error message is generated if "
        "this is not the case.  The maximum delay associated with the leaky "
        "bucket model is then Bmax*Weff/Rsample seconds, where Weff is an "
        "effective image width, being the width of the first component "
        "multiplied by the ratio between the total number of image "
        "samples and the number of samples in the first component.\n"
        "\t\t   The codestream main header is included as part of the first "
        "flush set, while the EOC marker is included as part of the last "
        "flush set.  Apart from this, the first flush set of every tile "
        "includes that tile's tile-part header in its byte count.  "
        "According to the foregoing model, the leaky bucket may not be filled "
        "beyond Bmax*Weff*D bytes as a result of any given flush, after which "
        "S*D bytes are drained from the bucket (S is the number of samples "
        "in the flush) before the next flush.  Accordingly, each flush "
        "must fill the bucket to at least S*D bytes (to avoid underflow) "
        "but to no more than Bmax*Weff*D bytes.  The last flush in the "
        "codestream is a little different; it must fill the bucket to no "
        "more than S*D bytes, so that the total number of bytes dumped "
        "into the bucket by all flush operations does not exceed L, but "
        "it is allowed to output fewer bytes; this is because we assume "
        "that spacer bytes will be introduced between codestreams in low "
        "latency CBR applications, so that each codestream occupies "
        "exactly L bytes; there is then no need to worry about buffer "
        "underflow or codestream indexing.",
        "IF");
}

/*****************************************************************************/
/*                       siz_params::copy_with_xforms                        */
/*****************************************************************************/

void
  siz_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
  /* Note:
        For a thorough understanding of the effect of geometric manipulations
     on canvas coordinates (including an expanation as to why these JPEG2000
     standard allows such geometric maninpulations to be conducted in the
     transformed domain) refer to Section 11.4.2 of the book by Taubman and
     Marcellin. */

{
  bool uses_cap=false;
  int profile=0, extensions=0;
  int canvas_x=0, canvas_y=0, origin_x=0, origin_y=0;
  int tiles_y=0, tiles_x=0, tile_origin_y=0, tile_origin_x=0;

  if (!(source->get(Sprofile,0,0,profile) &&
        source->get(Scap,0,0,uses_cap) &&
        source->get(Sextensions,0,0,extensions) &&
        source->get(Ssize,0,(transpose)?1:0,canvas_y) &&
        source->get(Ssize,0,(transpose)?0:1,canvas_x) &&
        source->get(Sorigin,0,(transpose)?1:0,origin_y) &&
        source->get(Sorigin,0,(transpose)?0:1,origin_x) &&
        source->get(Stiles,0,(transpose)?1:0,tiles_y) &&
        source->get(Stiles,0,(transpose)?0:1,tiles_x) &&
        source->get(Stile_origin,0,(transpose)?1:0,tile_origin_y) &&
        source->get(Stile_origin,0,(transpose)?0:1,tile_origin_x)))
    { KDU_ERROR_DEV(e,61); e <<
        KDU_TXT("Unable to copy SIZ parameters, unless all canvas "
        "coordinates are available.  Try using `siz_params::finalize' "
        "before attempting the copy.");
    }

  if (vflip || hflip)
    profile = 3; // Requires the use of the Part 2 alternate code-block anchor

  set(Sprofile,0,0,profile);
  set(Scap,0,0,uses_cap);
  set(Sextensions,0,0,extensions);
  
  if (profile == Sprofile_BROADCAST)
    { 
      int level=1, multi_tile=Sbroadcast_multi, reversible=Sbroadcast_irrev;
      source->get(Sbroadcast,0,0,level);
      source->get(Sbroadcast,0,1,multi_tile);
      source->get(Sbroadcast,0,2,reversible);
      set(Sbroadcast,0,0,level);
      set(Sbroadcast,0,1,multi_tile);
      set(Sbroadcast,0,2,reversible);
    }
  
  if (profile == Sprofile_IMF)
    { 
      int main_lev=0, sub_lev=0, reversible=Simf_irrev;
      source->get(Simf,0,0,main_lev);
      source->get(Simf,0,1,sub_lev);
      source->get(Simf,0,2,reversible);
      set(Simf,0,0,main_lev);
      set(Simf,0,1,sub_lev);
      set(Simf,0,2,reversible);
    }

  int part2_caps=0;
  if (source->get(SCpart2_caps,0,0,part2_caps))
    set(SCpart2_caps,0,0,part2_caps);

  int num_components=0;
  if (source->get(Scomponents,0,0,num_components))
    {
      if (num_components <= skip_components)
        { KDU_ERROR_DEV(e,62); e <<
            KDU_TXT("Attempting to discard all of the components "
            "from an existing code-stream!");
        }
      set(Scomponents,0,0,num_components-skip_components);
    }

  int num_output_components=0;
  if (source->get(Mcomponents,0,0,num_output_components))
    set(Mcomponents,0,0,num_output_components);
  
  int num_nlt_components=0;
  if (source->get(Ncomponents,0,0,num_nlt_components))
    { 
      int mod_nlt_components = num_nlt_components;
      if (num_output_components == 0)
        mod_nlt_components -= skip_components;
      set(Ncomponents,0,0,mod_nlt_components);
    }

  // Make a first pass through the subsampling structure to figure out
  // the largest common divisor which can be factored out by reducing
  // the canvas and tile dimensions.  Everything else will have to
  // be incorporated into the component sub-sampling factors.
  int n, d, decomp=0;
  kdu_coords gcd_exponent;
  kdu_params *source_cod=NULL, *source_coc;
  if (discard_levels > 0)
    {
      gcd_exponent.x = gcd_exponent.y = discard_levels;
      if ((extensions & Sextensions_DECOMP) &&
          ((source_cod = source->access_cluster(COD_params)) != NULL))
        {
          for (n=skip_components; n < num_components; n++)
            {
              kdu_coords exponent;
              if (((source_coc=source_cod->access_relation(-1,n,0)) != NULL) &&
                  source_coc->get(Cdecomp,0,0,decomp))
                { // Use decomposition information to compute downsampling
                  for (d=0; d < discard_levels; d++)
                    {
                      source_coc->get(Cdecomp,d,0,decomp);
                      exponent.x += (decomp & 1);
                      exponent.y += ((decomp & 2) >> 1);
                    }
                  if (exponent.x < gcd_exponent.x)
                    gcd_exponent.x = exponent.x;
                  if (exponent.y < gcd_exponent.y)
                    gcd_exponent.y = exponent.y;
                }
            }
        }
      if (transpose)
        gcd_exponent.transpose();
      kdu_coords dependencies;
      dependencies.x = canvas_x | origin_x | tiles_x | tile_origin_x;
      dependencies.y = canvas_y | origin_y | tiles_y | tile_origin_y;
      for (n=0; n < gcd_exponent.x; n++, dependencies.x>>=1)
        if (dependencies.x & 1)
          { gcd_exponent.x = n; break; }
      for (n=0; n < gcd_exponent.y; n++, dependencies.y>>=1)
        if (dependencies.y & 1)
          { gcd_exponent.y = n; break; }
    }

  canvas_x >>= gcd_exponent.x;       canvas_y >>= gcd_exponent.y;
  origin_x >>= gcd_exponent.x;       origin_y >>= gcd_exponent.y;
  tiles_x >>= gcd_exponent.x;        tiles_y >>= gcd_exponent.y;
  tile_origin_x >>= gcd_exponent.x;  tile_origin_y >>= gcd_exponent.y;

  int tmp;
  if (hflip)
    {
      tmp = 1-canvas_x;  canvas_x = 1-origin_x;  origin_x = tmp;
      tile_origin_x = 1-tile_origin_x;
      while (tile_origin_x > origin_x)
        tile_origin_x -= tiles_x;
    }
  if (vflip)
    {
      tmp = 1-canvas_y;  canvas_y = 1-origin_y;  origin_y = tmp;
      tile_origin_y = 1-tile_origin_y;
      while (tile_origin_y > origin_y)
        tile_origin_y -= tiles_y;
    }
  set(Ssize,0,0,canvas_y);             set(Ssize,0,1,canvas_x);
  set(Sorigin,0,0,origin_y);           set(Sorigin,0,1,origin_x);
  set(Stiles,0,0,tiles_y);             set(Stiles,0,1,tiles_x);
  set(Stile_origin,0,0,tile_origin_y); set(Stile_origin,0,1,tile_origin_x);

  bool is_signed=false;
  int precision=0, subs_x=0, subs_y=0;
  for (n=skip_components; n < num_components; n++)
    { /* Note that we deliberately do not copy the image dimensions since these
         should be automatically recomputed based on any discarded resolution
         levels. */
      if (source->get(Sprecision,n,0,precision))
        set(Sprecision,n-skip_components,0,precision);
      if (source->get(Ssigned,n,0,is_signed))
        set(Ssigned,n-skip_components,0,is_signed);
      if (num_output_components == 0)
        { // We can introduce the skipping into any NLT component info
          if (source->get(Nprecision,n,0,precision))
            set(Nprecision,n-skip_components,0,precision);
          if (source->get(Nsigned,n,0,is_signed))
            set(Nsigned,n-skip_components,0,is_signed);
        }
      
      if (source->get(Ssampling,n,(transpose)?1:0,subs_y) &&
          source->get(Ssampling,n,(transpose)?0:1,subs_x))
        {
          kdu_coords exponent;
          if ((source_cod != NULL) &&
              ((source_coc=source_cod->access_relation(-1,n,0)) != NULL) &&
              source_coc->get(Cdecomp,0,0,decomp))
            { // Use decomposition information to compute downsampling
              for (d=0; d < discard_levels; d++)
                {
                  source_coc->get(Cdecomp,d,0,decomp);
                  exponent.x += (decomp & 1);
                  exponent.y += ((decomp & 2) >> 1);
                }
            }
          else
            { // Just use `discard_levels' directly -- assume default DFS
              exponent.x = exponent.y = discard_levels;
            }
          if (transpose)
            exponent.transpose();
          exponent -= gcd_exponent;
          assert((exponent.x >= 0) && (exponent.y >= 0));
          subs_x <<= exponent.x;
          subs_y <<= exponent.y;
          if ((subs_x > 255) || (subs_y > 255))
            { KDU_ERROR(e,63); e <<
              KDU_TXT("Cannot apply requested resolution reduction "
              "without creating a SIZ marker segment with illegal "
              "component sub-sampling factors.  For the current "
              "code-stream, sub-sampling factors would be required "
              "which exceed the legal range of 1 to 255.");
            }
          set(Ssampling,n-skip_components,0,subs_y);
          set(Ssampling,n-skip_components,1,subs_x);
        }
    }

  int cbr_V=0; float cbr_Bmax=0;
  if (source->get(Scbr,0,0,cbr_V) && source->get(Scbr,0,1,cbr_Bmax))
    { set(Scbr,0,0,cbr_V); set(Scbr,0,1,cbr_Bmax); }

  if (num_output_components == 0)
    return;
  
  for (n=0; (n < num_output_components) &&
             source->get(Mprecision,n,0,precision,false,false); n++)
    set(Mprecision,n,0,precision);
  for (n=0; (n < num_output_components) &&
             source->get(Msigned,n,0,is_signed,false,false); n++)
    set(Msigned,n,0,is_signed);
  for (n=0; (n < num_nlt_components) &&
       source->get(Nprecision,n,0,precision,false,false); n++)
    set(Nprecision,n,0,precision);
  for (n=0; (n < num_nlt_components) &&
       source->get(Nsigned,n,0,is_signed,false,false); n++)
    set(Nsigned,n,0,is_signed);
}

/*****************************************************************************/
/*                       siz_params::write_marker_segment                    */
/*****************************************************************************/

int
  siz_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  bool uses_cap=false;
  int profile=0, extensions=0;
  int x=0, y=0, xo=0, yo=0, xt=0, yt=0, xto=0, yto=0, num_components=0;

  assert(last_marked == NULL);
  if (tpart_idx != 0)
    return 0;
  if (!(get(Sprofile,0,0,profile) &&
        get(Scap,0,0,uses_cap) &&
        get(Sextensions,0,0,extensions) &&
        get(Ssize,0,0,y) && get(Ssize,0,1,x) &&
        get(Sorigin,0,0,yo) && get(Sorigin,0,1,xo) &&
        get(Stiles,0,0,yt) && get(Stiles,0,1,xt) &&
        get(Stile_origin,0,0,yto) && get(Stile_origin,0,1,xto) &&
        get(Scomponents,0,0,num_components)))
    { KDU_ERROR_DEV(e,64); e <<
        KDU_TXT("Unable to write SIZ marker segment yet!");
    }

  if ((num_components < 1) || (num_components > 16384))
    { KDU_ERROR_DEV(e,65); e <<
        KDU_TXT("Illegal number of image components! Must be in the "
        "range 1 to 16384.");
    }

  int siz_length = 4 + 2 + 4*8 + 2 + 3*num_components;
  int cbd_length = 0;
  int cap_length = 0;
  int num_output_components = 0;
  if (get(Mcomponents,0,0,num_output_components) &&
      (num_output_components != 0))
    {
      if ((num_output_components < 1) || (num_output_components > 16384))
        { KDU_ERROR_DEV(e,0x19070501); e <<
            KDU_TXT("Illegal number of MCT output components, `Mcomponents'! "
            "Must be in the range 0 to 16384.");
        }
      cbd_length = 6 + num_output_components;
    }
  int part2_caps = 0;
  if (get(SCpart2_caps,0,0,part2_caps) && (part2_caps != 0))
    { 
      if (part2_caps & 0xFFFF0000)
        { KDU_ERROR_DEV(e,0x17031201); e <<
            KDU_TXT("Illegal `SCpart2_caps' attribute value.  Attribute must "
                    "be an unsigned 16-bit flags word.");
        }
      cap_length = 4 + 4 + 2;
    }
  
  if (out == NULL)
    return siz_length + cbd_length + cap_length;

  // Deal with negative canvas coordinates.
  if ((xto < 0) || (yto < 0)) // These are the most negative coordinates
    { /* For a better understanding of canvas coordinate equivalence
         transformations, consult Section 11.4.2 of the book by Taubman
         and Marcellin. */

      int x_lcm = xt; // Coordinate shifts must at least be multiples
      int y_lcm = yt; // of the relevant tile dimensions.

      // Before proceeding, let's see if there is in fact only one tile
      // (across, down, or in both directions).  If so, the amount of
      // translation need not be divisible by the tile size, so long as
      // we ensure that the output image also has only one tile (across, down
      // or in both directions).  We can arrange for this by modifying both
      // the LCM values initialized above and the tile origin position.
      if ((xto+xt) >= x)
        { x_lcm = 1;  xto = xo; }
      if ((yto+yt) >= y)
        { y_lcm = 1;  yto = yo; }

      kdu_params *cod = this->access_cluster(COD_params);
      int num_tiles = ceil_ratio(x-xo,xt) * ceil_ratio(y-yo,yt);
      for (int t=-1; t < num_tiles; t++)
        for (int c=0; c < num_components; c++)
          {
            int xr=1, yr=1;
            get(Ssampling,c,0,yr); get(Ssampling,c,1,xr);
            int levels=0;
            bool use_precincts=false;
            if ((cod == NULL) ||
                ((cod = cod->access_relation(t,c,0,true)) == NULL) ||
                !(cod->get(Clevels,0,0,levels) &&
                  cod->get(Cuse_precincts,0,0,use_precincts)))
              { KDU_ERROR_DEV(e,66); e <<
                  KDU_TXT("Attempting to write geometrically "
                  "transformed SIZ marker information without attaching and "
                  "finalizing all tile-component COD marker information.  "
                  "This is essential to establish canvas coordinate "
                  "equivalence relationships.");
              }
            int r=levels, res_exp=0;
            for (; r >= 0; r--, res_exp++)
              {
                int xp=15, yp=15;
                if (use_precincts)
                  { cod->get(Cprecincts,(levels-r),0,yp);
                    cod->get(Cprecincts,(levels-r),1,xp); }
                kdu_int32 x_precinct = xr<<(xp+r); // Precinct dimensions on
                kdu_int32 y_precinct = yr<<(yp+r); // full resolution canvas
                if ((x_precinct <= 0) || (y_precinct <= 0))
                  { KDU_ERROR(e,67); e <<
                      KDU_TXT("Error attempting to convert "
                      "geometrically transformed canvas coordinates to legal "
                      "marker ranges.  Try using a smaller precinct size (you "
                      "can use the transcoding utility to achieve this "
                      "at the same time as geometric transformations.");
                  }
                x_lcm = find_lcm(x_lcm,x_precinct);
                y_lcm = find_lcm(y_lcm,y_precinct);
              }
          }
        // Finally, we can determine canvas translates which will lead to
        // non-negative coordinates which are equivalent to the originals.
        int x_off = (xto < 0)?(x_lcm * ceil_ratio(-xto,x_lcm)):0;
        int y_off = (yto < 0)?(y_lcm * ceil_ratio(-yto,y_lcm)):0;
        x += x_off; xo += x_off; xto += x_off;
        y += y_off; yo += y_off; yto += y_off;
        assert((xto >= 0) && (yto >= 0));
    }

  // Check for legal sequencing of the tile and image origins.
  if ((xo < xto) || (yo < yto) || (xo >= (xto+xt)) || (yo >= (yto+yt)) ||
      (x <= xo) || (y <= yo))
    { KDU_ERROR_DEV(e,68); e <<
        KDU_TXT("Cannot write SIZ marker with illegal canvas "
        "coordinates.  The first tile is required to have a non-empty "
        "intersection with the image region on the canvas.");
    }
  
  int acc_length = 0; // Accumulate actual length for consistency checking.
  acc_length += out->put(KDU_SIZ);
  acc_length += out->put((kdu_uint16)(siz_length-2));

  kdu_uint16 r_siz = 0;
  if (profile == Sprofile_PART2)
    r_siz = ((kdu_uint16) extensions) | 0x8000; // Part-2 codestream
  else if ((extensions != 0) || (profile > Sprofile_IMF) || (profile < 0))
    { KDU_ERROR(e,0x21040502); e <<
        KDU_TXT("Illegal profile index.  \"Sprofile\" must be set "
        "in the range 0 to 9; 3 is required if Part 2 features are to be "
        "used.");
    }
  else if (profile == Sprofile_PROFILE2)
    r_siz = 0; // Generic Part-1 codestream
  else if (profile == Sprofile_PROFILE0)
    r_siz = 1;
  else if (profile == Sprofile_PROFILE1)
    r_siz = 2;
  else if (profile == Sprofile_CINEMA2K)
    r_siz = 3;
  else if (profile == Sprofile_CINEMA4K)
    r_siz = 4;
  else if (profile == Sprofile_CINEMA2S)
    r_siz = 5;
  else if (profile == Sprofile_CINEMA4S)
    r_siz = 6;
  else if (profile == Sprofile_CINEMASS)
    r_siz = 7;
  else if (profile == Sprofile_BROADCAST)
    { 
      int level=1, multi_tile=Sbroadcast_multi, reversible=Sbroadcast_irrev;
      get(Sbroadcast,0,0,level);
      get(Sbroadcast,0,1,multi_tile);
      get(Sbroadcast,0,2,reversible);
      r_siz = (kdu_uint16)(0x100 + (level & 7));
      if (multi_tile)
        r_siz += 0x100;
      if (reversible)
        r_siz += 0x100;
    }
  else if (profile == Sprofile_IMF)
    { 
      int main_level=0, sub_level=0, reversible=Simf_irrev;
      get(Simf,0,0,main_level);
      get(Simf,0,1,sub_level);
      get(Simf,0,2,reversible);
      r_siz = (kdu_uint16)((sub_level<<4) + main_level);
      if ((x <= 2048) && (y <= 1556))
        r_siz += 0x400;
      else if ((x <= 4096) && (y <= 3112))
        r_siz += 0x500;
      else
        r_siz += 0x600;
      if (reversible)
        r_siz += 0x300;
    }
  else
    assert(0);
  if (uses_cap)
    r_siz |= 0x4000;
  acc_length += out->put(r_siz);

  acc_length += out->put((kdu_uint32) x);
  acc_length += out->put((kdu_uint32) y);
  acc_length += out->put((kdu_uint32) xo);
  acc_length += out->put((kdu_uint32) yo);
  acc_length += out->put((kdu_uint32) xt);
  acc_length += out->put((kdu_uint32) yt);
  acc_length += out->put((kdu_uint32) xto);
  acc_length += out->put((kdu_uint32) yto);
  acc_length += out->put((kdu_uint16) num_components);
  int c, is_signed=0, precision=0, xr=0, yr=0;
  for (c=0; c < num_components; c++)
    {
      if (!(get(Ssigned,c,0,is_signed) && get(Sprecision,c,0,precision) &&
            get(Ssampling,c,0,yr) && get(Ssampling,c,1,xr)))
        { KDU_ERROR_DEV(e,70); e <<
            KDU_TXT("Unable to write SIZ marker segment! Precision "
            "or sub-sampling information missing for at least one component.");
        }
      if ((precision < 1) || (precision > 38))
        { KDU_ERROR_DEV(e,71); e <<
            KDU_TXT("Illegal image sample bit-depth, ") << precision <<
            KDU_TXT(". Legal range is from 1 to 38 bits per sample.");
        }
      if ((xr < 1) || (xr > 255) || (yr < 1) || (yr > 255))
        { KDU_ERROR_DEV(e,72); e <<
            KDU_TXT("Illegal component sub-sampling factors, {")
            << yr << "," << xr <<
            KDU_TXT("}. Legal range is from 1 to 255.");
        }
      acc_length += out->put((kdu_byte)(precision-1 + (is_signed<<7)));
      acc_length += out->put((kdu_byte) xr);
      acc_length += out->put((kdu_byte) yr);
    }
  assert(acc_length == siz_length);
  
  if (cbd_length > 0)
    { // We must also write a CBD marker segment
      acc_length = 0;
      acc_length += out->put(KDU_CBD);
      acc_length += out->put((kdu_uint16)(cbd_length-2));
      acc_length += out->put((kdu_uint16) num_output_components);
      for (c=0; c < num_output_components; c++)
        { 
          if (!(get(Msigned,c,0,is_signed) && get(Mprecision,c,0,precision)))
            { KDU_ERROR_DEV(e,0x19070502); e <<
              KDU_TXT("Unable to write CBD marker segment! Precision "
                      "or signed/unsigned information missing for at least one "
                      "MCT output component.");
            }
          if ((precision < 1) || (precision > 38))
            { KDU_ERROR_DEV(e,0x19070503); e <<
              KDU_TXT("Illegal precision for MCT output component, ")
              << precision <<
              KDU_TXT(". Legal range is from 1 to 38 bits per sample.");
            }
          acc_length += out->put((kdu_byte)(precision-1 + (is_signed<<7)));
        }
      assert(acc_length == cbd_length);
    }
  
  if (cap_length > 0)
    { // We must also write a CAP marker segment
      acc_length = 0;
      acc_length += out->put(KDU_CAP);
      acc_length += out->put((kdu_uint16)(cap_length-2));
      kdu_uint32 pcap = 0x40000000;
      acc_length += out->put(pcap);
      acc_length += out->put((kdu_uint16) part2_caps);
      assert(acc_length == cap_length);
    }
  
  return siz_length + cbd_length + cap_length;
}

/*****************************************************************************/
/*                       siz_params::check_marker_segment                    */
/*****************************************************************************/

bool
  siz_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  c_idx = -1;
  if (code == KDU_SIZ)
    return true;
  else if (code == KDU_CBD)
    { // If this is a CBD marker segment and the SIZ marker segment has
      // already been parsed, clear the `marked' flag so as to allow the
      // CBD marker segment to be parsed by this object as well.
      int output_comps=0;
      if (!get(Mcomponents,0,0,output_comps))
        marked = false;
      return true;
    }
  else if (code == KDU_CAP)
    { // If this is a CAP marker segment and the SIZ marker segment has
      // already been parsed, clear the `marked' flag so as to allow the
      // CAP marker segment to be parsed by this object as well.
      int part2_caps=0;
      if (!get(SCpart2_caps,0,0,part2_caps))
        marked = false;
      return true;
    }
  else
    return false;
}

/*****************************************************************************/
/*                        siz_params::read_marker_segment                    */
/*****************************************************************************/

bool
  siz_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  if (tpart_idx != 0)
    return false;
  kdu_byte *bp=bytes, *end=bytes+num_bytes;
  int c, num_components, precision;

  if (code == KDU_SIZ)
    { // Parse SIZ marker segment
      int profile, extensions;
      try {
          profile = kdu_read(bp,end,2);
          bool uses_cap = (profile & 0x4000)?true:false;
          if (profile & 0x8000)
            {
              extensions = profile & 0x3FFF;
              profile = Sprofile_PART2;
            }
          else
            { 
              extensions = 0;
              kdu_uint16 original_profile = profile;
              profile &= ~0x4000;
              if (profile == 0)
                profile = Sprofile_PROFILE2;
              else if (profile == 1)
                profile = Sprofile_PROFILE0;
              else if (profile == 2)
                profile = Sprofile_PROFILE1;
              else if (profile == 3)
                profile = Sprofile_CINEMA2K;
              else if (profile == 4)
                profile = Sprofile_CINEMA4K;
              else if (profile == 5)
                profile = Sprofile_CINEMA2S;
              else if (profile == 6)
                profile = Sprofile_CINEMA4S;
              else if (profile == 7)
                profile = Sprofile_CINEMASS;
              else if ((profile >= 0x100) && (profile <= 0x30F) &&
                       ((profile & 0xF0F0) == 0))
                { 
                  set(Sbroadcast,0,0,(profile&15)); // broadcast profile level
                  int multi=0, rev=0;
                  switch ((profile >> 8) & 3) {
                    case 1: multi=Sbroadcast_single; rev=Sbroadcast_irrev;
                            break;
                    case 2: multi=Sbroadcast_multi; rev=Sbroadcast_irrev;
                            break;
                    case 3: multi=Sbroadcast_multi; rev=Sbroadcast_rev;
                            break;
                    default: assert(0);
                  }
                  set(Sbroadcast,0,1,multi);
                  set(Sbroadcast,0,2,rev);
                  profile = Sprofile_BROADCAST;
                }
              else if ((profile >= 0x400) && (profile <= 0x9FF))
                { 
                  set(Simf,0,0,(profile&15)); // IMF main-level
                  set(Simf,0,1,(profile>>4)&15); // IMF sub-level
                  int rev=0, fcat = ((profile - 0x400) >> 8);
                  if (fcat >= 3)
                    { rev = 1; fcat -= 3; }
                  set(Simf,0,2,rev);
                  profile = Sprofile_IMF;
                }
              else
                { KDU_ERROR(e,0x21040503); e <<
                  KDU_TXT("Invalid Rsiz word encountered in SIZ marker "
                          "segment!  Value is: ") << original_profile;
                }
            }
          set(Sprofile,0,0,profile);
          set(Sextensions,0,0,extensions);
          set(Scap,0,0,uses_cap);

          // Read global dimensions
          kdu_coords size, origin, tiles, tile_origin;
          set(Ssize,0,1,size.x=kdu_read(bp,end,4));
          set(Ssize,0,0,size.y=kdu_read(bp,end,4));
          set(Sorigin,0,1,origin.x=kdu_read(bp,end,4));
          set(Sorigin,0,0,origin.y=kdu_read(bp,end,4));
          set(Stiles,0,1,tiles.x=kdu_read(bp,end,4));
          set(Stiles,0,0,tiles.y=kdu_read(bp,end,4));
          set(Stile_origin,0,1,tile_origin.x=kdu_read(bp,end,4));
          set(Stile_origin,0,0,tile_origin.y=kdu_read(bp,end,4));

          if ((tiles.x < 0) && (size.x >= 0))
            set(Stiles,0,1,tiles.x=size.x);
          if ((tiles.y < 0) && (size.y >= 0))
            set(Stiles,0,0,tiles.y=size.y);
          // Above code attempts to deal with negative tile dimensions due to
          // overflow from unsigned 32-bit values.  If the canvas dimensions
          // are smaller than the tile dimensions, we can just as well use
          // them instead.

          // Read component information
          set(Scomponents,0,0,num_components = kdu_read(bp,end,2));
          for (c=0; c < num_components; c++)
            {
              precision = kdu_read(bp,end,1);
              set(Ssigned,c,0,(precision>>7)&1);
              set(Sprecision,c,0,(precision&0x7F)+1);
              set(Ssampling,c,1,kdu_read(bp,end,1));
              set(Ssampling,c,0,kdu_read(bp,end,1));
            }
          if (bp != end)
            { KDU_ERROR(e,74); e <<
                KDU_TXT("Malformed SIZ marker segment encountered. The final ")
                << (int)(end-bp) <<
                KDU_TXT(" bytes were not consumed!");
            }
        } // End of try block.
      catch(kdu_byte *)
        { KDU_ERROR(e,75); e <<
            KDU_TXT("Malformed SIZ marker segment encountered. "
            "Marker segment is too small.");
        }
      return true;
    }
  else if (code == KDU_CBD)
    { // Parse CBD marker segment
      try {
          num_components = kdu_read(bp,end,2);
          bool all_same = (num_components & 0x8000)?true:false;
          num_components &= 0x7FFF;
          set(Mcomponents,0,0,num_components);
          precision=0;
          for (c=0; c < num_components; c++)
            {
              if ((precision == 0) || !all_same)
                {
                  precision = kdu_read(bp,end,1);
                  set(Msigned,c,0,(precision>>7)&1);
                  set(Mprecision,c,0,(precision&0x7F)+1);
                }
            }
          if (bp != end)
            { KDU_ERROR(e,0x19070504); e <<
                KDU_TXT("Malformed CBD marker segment encountered. The final ")
                << (int)(end-bp) <<
                KDU_TXT(" bytes were not consumed!");
            }
        } // End of try block.
      catch(kdu_byte *)
        { KDU_ERROR(e,0x19070505); e <<
            KDU_TXT("Malformed CBD marker segment encountered. "
            "Marker segment is too small.");
        }
      return true;
    }
  else if (code == KDU_CAP)
    { // Parse CAP marker segment
      try { 
        bool unrecognized_caps=false;
        int pcap = kdu_read(bp,end,4);
        int part_idx=1, parts_found=0;
        int part2_caps=0;
        for (; pcap != 0; pcap+=pcap, part_idx++)
          if (pcap < 0)
            { // Have capabilities extensions for Part-`part_idx'
              if (part_idx == 2)
                part2_caps = parts_found+1; // Remember to read the capabilities
              else
                unrecognized_caps = true;
              parts_found++;
            }
        if (part2_caps > 0)
          { 
            while ((--part2_caps) > 0)
              kdu_read(bp,end,2); // Skip over unrecognized caps
            part2_caps = kdu_read(bp,end,2);
          }
        if ((part2_caps != 0) && (part2_caps != SCpart2_caps_EXTENDED_COD))
          unrecognized_caps = true;
        set(SCpart2_caps,0,0,part2_caps);
        bool have_scap=false;
        if (unrecognized_caps && get(Scap,0,0,have_scap) && have_scap)
          { KDU_WARNING(w,0x17031203); w <<
              KDU_TXT("Codestream contains unrecognized capability extensions "
                      "found in a `CAP' marker segment; moreover, the "
                      "`Scap' attribute flag is recorded in the `SIZ' marker "
                      "segment.  It is likely that we will not be able to "
                      "decode this codestream correctly.");
          }
      } // End of try block
      catch(kdu_byte *)
        { KDU_ERROR(e,0x17031202); e <<
            KDU_TXT("Malformed CAP marker segment encountered.  "
                    "Marker segment is too small.");
        }
      return true;
    }

  return false;
}

/*****************************************************************************/
/*                             siz_params::finalize                          */
/*****************************************************************************/

void
  siz_params::finalize(bool after_reading)
{
  int components=0, val=0, n;
  bool have_sampling, have_dims, have_components, have_size;
  int size_x=0, size_y=0, origin_x=0, origin_y=0;

  // First collect some basic facts.
  have_components = get(Scomponents,0,0,components);
  have_sampling = get(Ssampling,0,0,val);
  have_dims = get(Sdims,0,0,val);
  have_size = get(Ssize,0,0,size_y) && get(Ssize,0,1,size_x);
  if (!(get(Sorigin,0,0,origin_y) && get(Sorigin,0,1,origin_x)))
    { set(Sorigin,0,0,origin_y=0); set(Sorigin,0,1,origin_x=0); }
  if (have_sampling && !have_components)
    { 
      for (components=1;
           get(Ssampling,components,0,val,false,false);
           components++);
      have_components = true;
    }
  if (have_dims && !have_components)
    {
      for (components=1;
           get(Sdims,components,0,val,false,false);
           components++);
      have_components = true;
    }
  if (!have_components)
    { KDU_ERROR(e,76); e <<
        KDU_TXT("Problem trying to finalize SIZ information. "
        "Insufficient data supplied to determine the number of "
        "image components! Available information is as follows:\n\n");
      this->textualize_attributes(e); e << "\n";
    }

  int *dim_x = new int[components];
  int *dim_y = new int[components];
  int *sub_x = new int[components];
  int *sub_y = new int[components];
  try { // Protect against possible memory leaks
    if (have_dims)
      for (n=0; n < components; n++)
        if (!(get(Sdims,n,0,dim_y[n]) && get(Sdims,n,1,dim_x[n])))
          { KDU_ERROR(e,77); e <<
            KDU_TXT("Problem trying to finalize SIZ information. "
                    "Component dimensions are only partially available!");
          }
    if (have_sampling)
    	{
        for (n=0; n < components; n++)
          if (!(get(Ssampling,n,0,sub_y[n]) && get(Ssampling,n,1,sub_x[n])))
            { KDU_ERROR(e,78); e <<
              KDU_TXT("Problem trying to finalize SIZ information "
                      "Component sub-sampling factors are only partially "
                      "available!");
            }
          else if ((sub_x[n] < 1) || (sub_y[n] < 1))
            { KDU_ERROR(e,79); e <<
              KDU_TXT("Image component sub-sampling factors must be "
                      "strictly positive!");
            }
      }
    
    // Now for the information synthesis problem.
    if ((!have_sampling) && (!have_dims))
      { KDU_ERROR(e,80); e <<
        KDU_TXT("Problem trying to finalize SIZ information. "
                "Must have either the individual component dimensions or the "
                "component sub-sampling factors.");
      }
    if (!have_dims)
      { // Have sub-sampling factors and need to synthesize dimensions.
        if (!have_size)
          { KDU_ERROR(e,81); e <<
            KDU_TXT("Problem trying to finalize SIZ information. "
                    "Must have either the image component dimensions or the "
                    "canvas size! Available information is as follows:\n\n");
            this->textualize_attributes(e); e << "\n";
          }
        for (n=0; n < components; n++)
          { 
            dim_x[n]=ceil_ratio(size_x,sub_x[n])-ceil_ratio(origin_x,sub_x[n]);
            dim_y[n]=ceil_ratio(size_y,sub_y[n])-ceil_ratio(origin_y,sub_y[n]);
            set(Sdims,n,0,dim_y[n]); set(Sdims,n,1,dim_x[n]);
          }
        have_dims = true;
      }

    if ((!have_size) && (!have_sampling))
      { // Synthesize a compatible canvas size.
        assert(have_dims);
        if (!(synthesize_canvas_size(components,dim_x,origin_x,size_x) &&
              synthesize_canvas_size(components,dim_y,origin_y,size_y)))
          { KDU_ERROR(e,82); e <<
            KDU_TXT("Problem trying to finalize SIZ information "
                    "Component dimensions are not consistent with a "
                    "legal set of sub-sampling factors. Available information "
                    "is as follows:\n\n");
            this->textualize_attributes(e); e << "\n";
          }
        set(Ssize,0,0,size_y); set(Ssize,0,1,size_x);
        have_size = true;
      }

    if (!have_sampling)
      { // Synthesize sub-sampling factors from canvas size & image dimensions
        assert(have_dims && have_size);
        for (n=0; n < components; n++)
          { 
            val = (size_x - origin_x) / dim_x[n]; // Rough estimate.
            if (val <= 0)
              val = 1;
            while ((val > 1) &&
                   ((ceil_ratio(size_x,val) -
                     ceil_ratio(origin_x,val)) < dim_x[n]))
              val--;
            while ((ceil_ratio(size_x,val) -
                    ceil_ratio(origin_x,val)) > dim_x[n])
              val++;
            if ((ceil_ratio(size_x,val)-ceil_ratio(origin_x,val)) != dim_x[n])
              { KDU_ERROR(e,83); e <<
                KDU_TXT("Problem trying to finalize SIZ information. "
                        "Horizontal canvas and image component dimensions are "
                        "not compatible.  Available information is as "
                        "follows:\n\n");
                this->textualize_attributes(e); e << "\n";
              }
            sub_x[n] = val;
            val = (size_y - origin_y) / dim_y[n]; // Rough estimate.
            if (val <= 0)
              val = 1;
            while ((val > 1) &&
                   ((ceil_ratio(size_y,val) -
                     ceil_ratio(origin_y,val)) < dim_y[n]))
              val--;
            while ((ceil_ratio(size_y,val) -
                    ceil_ratio(origin_y,val)) > dim_y[n])
              val++;
            if ((ceil_ratio(size_y,val)-ceil_ratio(origin_y,val)) != dim_y[n])
              { KDU_ERROR(e,84); e <<
                KDU_TXT("Problem trying to finalize SIZ information. "
                        "Vertical canvas and image component dimensions are "
                        "not compatible.  Available information is as "
                        "follows:\n\n");
                this->textualize_attributes(e); e << "\n";
              }
            sub_y[n] = val;
            set(Ssampling,n,0,sub_y[n]); set(Ssampling,n,1,sub_x[n]);
          }
        have_sampling = true;
      }

    if (!have_size)
      { // Synthesize canvas size from sub-sampling factors & image dimensions
        assert(have_dims && have_sampling);
        int min_size=0, max_size=0, max, min;
        for (n=0; n < components; n++)
          { 
            max = (ceil_ratio(origin_x,sub_x[n])+dim_x[n])*sub_x[n];
            min = max - sub_x[n] + 1;
            if ((n==0) || (max < max_size)) max_size = max;
            if ((n==0) || (min > min_size)) min_size = min;
          }
        if (min_size > max_size)
          { KDU_ERROR(e,85); e <<
            KDU_TXT("Problem trying to finalize SIZ information. "
                    "Horizontal component dimensions and sub-sampling factors "
                    "are incompatible. Available information is as "
                    "follows:\n\n");
            this->textualize_attributes(e); e << "\n";
          }
        size_x = min_size;
        for (n=0; n < components; n++)
          { 
            max = (ceil_ratio(origin_y,sub_y[n])+dim_y[n])*sub_y[n];
            min = max - sub_y[n] + 1;
            if ((n==0) || (max < max_size)) max_size = max;
            if ((n==0) || (min > min_size)) min_size = min;
          }
        if (min_size > max_size)
          { KDU_ERROR(e,86); e <<
            KDU_TXT("Problem trying to finalize SIZ information. "
                    "Vertical component dimensions and sub-sampling factors "
                    "are incompatible. Available information is as "
                    "follows:\n\n");
            this->textualize_attributes(e); e << "\n";
          }
        size_y = min_size;
        set(Ssize,0,0,size_y); set(Ssize,0,1,size_x);
        have_size = true;
      }
  
    // Now for verification that all quantities are consistent.
    // Here we set the tiling parameters if necessary.
    assert(have_sampling && have_dims && have_size);
    for (n=0; n < components; n++)
      { 
        if (((ceil_ratio(size_x,sub_x[n]) -
              ceil_ratio(origin_x,sub_x[n])) != dim_x[n]) ||
            ((ceil_ratio(size_y,sub_y[n]) -
              ceil_ratio(origin_y,sub_y[n])) != dim_y[n]))
          { KDU_ERROR(e,87); e <<
            KDU_TXT("Problem trying to finalize SIZ information. "
                    "Dimensions are inconsistent.  Available information is "
                    "as follows:\n\n");
            this->textualize_attributes(e); e << "\n";
          }
      }
  }
  catch (kdu_exception exc) { 
    delete[] dim_x;  delete[] dim_y;  delete[] sub_x;  delete[] sub_y;
    throw exc;
  }
  delete[] dim_x;  delete[] dim_y;  delete[] sub_x;  delete[] sub_y;
  
  int tile_x=0, tile_y=0, tile_ox=0, tile_oy=0;

  if (!(get(Stile_origin,0,0,tile_oy) && get(Stile_origin,0,1,tile_ox)))
    { set(Stile_origin,0,0,tile_oy=origin_y);
      set(Stile_origin,0,1,tile_ox=origin_x); }
  if (!(get(Stiles,0,0,tile_y) && get(Stiles,0,1,tile_x)))
    {
      tile_x = size_x - tile_ox;
      tile_y = size_y - tile_oy;
      set(Stiles,0,0,tile_y); set(Stiles,0,1,tile_x);
    }
  if ((tile_ox > origin_x) || ((tile_ox+tile_x) <= origin_x) ||
      (tile_oy > origin_y) || ((tile_oy+tile_y) <= origin_y))
    { KDU_ERROR(e,88); e <<
        KDU_TXT("Problems trying to finalize SIZ information. "
                "Illegal tile origin coordinates.  The first tile must have "
                "a non-empty intersection with the image region on the "
                "canvas. Available information is as follows:\n\n");
      this->textualize_attributes(e); e << "\n";
    }

  // See if we need to set the profile and perform some basic checks
  int broadcast_level = 1, broadcast_multi_tile=0, broadcast_reversible=0;  
  bool have_broadcast = (get(Sbroadcast,0,0,broadcast_level) &&
                         get(Sbroadcast,0,1,broadcast_multi_tile) &&
                         get(Sbroadcast,0,2,broadcast_reversible));
  int imf_main_lev=0, imf_sub_lev=0, imf_reversible=0;
  bool have_imf = (get(Simf,0,0,imf_main_lev) &&
                   get(Simf,0,1,imf_sub_lev) &&
                   get(Simf,0,2,imf_reversible));
  int extensions=0;
  if (!get(Sextensions,0,0,extensions))
    set(Sextensions,0,0,extensions=0);
  int profile=0;
  if (!get(Sprofile,0,0,profile))
    { 
      if (extensions != 0)
        set(Sprofile,0,0,profile=Sprofile_PART2);
      else if (have_broadcast)
        set(Sprofile,0,0,profile=Sprofile_BROADCAST);
      else if (have_imf)
        set(Sprofile,0,0,profile=Sprofile_IMF);
      else
        set(Sprofile,0,0,profile=Sprofile_PROFILE2);
    }
  if ((profile < 0) || (profile > 10))
    { KDU_ERROR(e,0x06121401); e <<
        KDU_TXT("Illegal profile index.  \"Sprofile\" must be in the "
        "range 0 to 10: Profile-0 is the most restrictive original Part-1 "
        "profile; Profile-2 is the unrestricted Part-1 profile; 3 means "
        "that the codestream conforms to Part-2 of the standard; 4 and 5 "
        "are for the restrictive Digital Cinema profiles, added by an "
        "ammendment to Part-1 of the standard; 6 is for the similarly "
        "restrictive Broadcast profile, also added as an ammendment to "
        "Part-1 of the standard; 7, 8 and 9 are for scalable digital "
        "cinema pofiles, added by another ammendment to Part-1 of the "
        "standard; and 10 is for the Interoperable Master Format (IMF), added "
        "by yet another ammendment to Part-1 of the standard.");
    }
  
  // Auto-set Ncomponents and Mcomponents, if necessary
  int num_output_components=0;
  int num_nlt_components=0;
  if (!get(Mcomponents,0,0,num_output_components))
    set(Mcomponents,0,0,num_output_components=0);
  if (after_reading || !get(Ncomponents,0,0,num_nlt_components))
    { 
      num_nlt_components = num_output_components;
      if (num_nlt_components <= 0)
        num_nlt_components = components;
      set(Ncomponents,0,0,num_nlt_components);
    }
  else if (((num_output_components > 0) &&
            (num_nlt_components != num_output_components)) ||
           ((num_output_components <= 0) &&
            (num_nlt_components != components)))
    { KDU_ERROR(e,0x13011601); e <<
      KDU_TXT("The `Ncomponents' attribute must agree with `Mcomponents', if "
              "it is non-zero, else with `Scomponents'.  You can let the "
              "machinery auto-fill the `Ncomponents' value.");
    }
  
  // Auto-set Nsigned, Msigned or Ssigned if necessary
  bool signed_val=false;
  if (after_reading || !get(Nsigned,0,0,signed_val))
    { 
      if (num_output_components > 0)
        { 
          for (n=0; n < num_output_components; n++)
            if (get(Msigned,n,0,signed_val))
              set(Nsigned,n,0,signed_val);
        }
      else
        for (n=0; n < components; n++)
          if (get(Ssigned,n,0,signed_val))
            set(Nsigned,n,0,signed_val);
    }
  else if (num_output_components > 0)
    { 
      if (!get(Msigned,0,0,signed_val))
        for (n=0; n < num_output_components; n++)
          if (get(Nsigned,n,0,signed_val))
            set(Msigned,n,0,signed_val);
    }
  else if (!get(Ssigned,0,0,signed_val))
    for (n=0; n < components; n++)
      if (get(Nsigned,n,0,signed_val))
        set(Ssigned,n,0,signed_val);
  
  // Auto-set Nprecision, Mprecision or Sprecision if necessary
  int prec_val=0;
  if (after_reading || !get(Nprecision,0,0,prec_val))
    { 
      if (num_output_components > 0)
        { 
          for (n=0; n < num_output_components; n++)
            if (get(Mprecision,n,0,prec_val))
              set(Nprecision,n,0,prec_val);
        }
      else
        for (n=0; n < components; n++)
          if (get(Sprecision,n,0,prec_val))
            set(Nprecision,n,0,prec_val);
    }
  else if (num_output_components > 0)
    { 
      if (!get(Mprecision,0,0,prec_val))
        for (n=0; n < num_output_components; n++)
          if (get(Nprecision,n,0,prec_val))
            set(Mprecision,n,0,prec_val);
    }
  else if (!get(Sprecision,0,0,prec_val))
    for (n=0; n < components; n++)
      if (get(Nprecision,n,0,prec_val))
        set(Sprecision,n,0,prec_val);
  
  if (after_reading)
    return;
 
  // Prepare to check for Part-2 features
  int new_profile=profile, new_extensions=extensions;
  if ((extensions != 0) && (profile != Sprofile_PART2))
    set(Sprofile,0,0,profile=Sprofile_PART2);
  int new_part2_caps=0, part2_caps=0;
  if (get(SCpart2_caps,0,0,part2_caps))
    new_part2_caps = part2_caps;
  
  // Finalize CBD information
  if (num_output_components > 0)
    {
      new_profile = Sprofile_PART2;
      new_extensions |= Sextensions_MCT;
    }
  else
    new_extensions &= ~Sextensions_MCT;

  // Scan through the other attributes to see if there are
  // additional Part-2 extensions.
  kdu_params *cod, *cod_root = access_cluster(COD_params);
  if (cod_root != NULL)
    { // Otherwise, the SIZ marker segment is all by itself
      int t, num_t=cod_root->get_num_tiles();
      int c, num_c=cod_root->get_num_comps();
      bool b_val=0;
      for (t=-1; t < num_t; t++)
        for (c=-1; c < num_c; c++)
          if ((cod = cod_root->access_unique(t,c,0)) != NULL)
            { 
              if ((cod->get(Cads,0,0,val,false) && (val != 0)) ||
                  ((t < 0) && cod->get(Cdfs,0,0,val,false) && (val != 0)))
                {
                  new_profile = Sprofile_PART2;
                  new_extensions |= Sextensions_DECOMP;
                }
              else
                for (int p=0; cod->get(Cdecomp,p,0,val,false,false); p++)
                  if (val != 3)
                    {
                      new_profile = Sprofile_PART2;
                      new_extensions |= Sextensions_DECOMP;
                      break;
                    }

              if (cod->get(Catk,0,0,val,false) && (val != 0))
                {
                  new_profile = Sprofile_PART2;
                  kdu_params *atk = access_cluster(ATK_params);
                  if ((atk != NULL) &&
                      ((atk = atk->access_relation(t,-1,val,true)) != NULL) &&
                      atk->get(Ksymmetric,0,0,b_val) && b_val)
                    new_extensions |= Sextensions_SYM_KNL;
                  else
                    new_extensions |= Sextensions_ANY_KNL;
                }
            
              if ((cod->get(Calign_blk_last,0,0,b_val) && b_val) ||
                  (cod->get(Calign_blk_last,0,1,b_val) && b_val))
                new_profile = Sprofile_PART2;
            
              int modes=0;
              if (cod->get(Cmodes,0,0,modes) &&
                  ((modes & (Cmodes_BYPASS_E1 | Cmodes_BYPASS_E2)) != 0) &&
                  ((modes & Cmodes_BYPASS) != 0))
                { 
                  new_part2_caps |= SCpart2_caps_EXTENDED_COD;
                  new_profile = Sprofile_PART2;
                }
            }
    }
  
  kdu_params *nlt, *nlt_root = access_cluster(NLT_params);
  if (nlt_root != NULL)
    { // Otherwise, the SIZ marker segment is all by itself
      bool global_signed=false, have_global_nlt=false;
      int global_prec=-1;
      int c, num_c=nlt_root->get_num_comps();
      for (c=-1; c < num_c; c++)
        if ((nlt = nlt_root->access_unique(-1,c,0)) != NULL)
          { 
            int nl_type=0, mval=0;
            float fval=0;
            if (nlt->get(NLType,0,0,nl_type) ||
                nlt->get(NLTgamma,0,0,fval) ||
                nlt->get(NLTlut,0,0,fval) ||
                nlt->get(NLTmake,0,0,mval))
              { 
                new_profile = Sprofile_PART2;
                new_extensions |= Sextensions_NLT;
                if (c < 0)
                  have_global_nlt = true;
              }
          }
        else if ((c >= 0) &&
                 get(Nsigned,c,0,signed_val) &&
                 get(Nprecision,c,0,prec_val))
          { // Check consistency of the Nprecision/Nsigned values
            if (have_global_nlt && (c == 0))
              { global_prec=prec_val; global_signed=signed_val; }
            else if (have_global_nlt &&
                     ((global_prec != prec_val) ||
                      (global_signed != signed_val)))
              { KDU_ERROR(e,0x13011602); e <<
                KDU_TXT("You have specified global (component-wide) NLT "
                        "(non-linear transform) atributes, so all "
                        "image components that do not have their own "
                        "overriding NLT attributes must have the same "
                        "Nprecision and Nsigned values as component 0 -- we "
                        "adopt component 0's precision/signed information "
                        "when writing a global NLT marker segment.");
              }
            else if ((!have_global_nlt) && (num_output_components > 0))
              { 
                int ref_prec=0; bool ref_signed=false;
                if (get(Mprecision,c,0,ref_prec) &&
                    get(Msigned,c,0,ref_signed) &&
                    ((prec_val != ref_prec) || (ref_signed != signed_val)))
                  { KDU_ERROR(e,0x13011603); e <<
                    KDU_TXT("Nprecision/Nsigned values must match "
                            "Mprecision/Msigned for components that have "
                            "no NLT (no `NLTtype' attribute).");
                  }
              }
            else if (!have_global_nlt)
              { 
                int ref_prec=0; bool ref_signed=false;
                if (get(Sprecision,c,0,ref_prec) &&
                    get(Ssigned,c,0,ref_signed) &&
                    ((prec_val != ref_prec) || (ref_signed != signed_val)))
                  { KDU_ERROR(e,0x13011604); e <<
                    KDU_TXT("Nprecision/Nsigned values must match "
                            "Sprecision/Ssigned for components that have "
                            "no NLT (no `NLTtype' attribute) with no MCT.");
                  }                
              }
          }
    }
  
  if (new_profile != profile)
    set(Sprofile,0,0,new_profile);
  if (new_extensions != extensions)
    set(Sextensions,0,0,new_extensions);
  if (new_part2_caps != part2_caps)
    set(SCpart2_caps,0,0,new_part2_caps);

  bool scap_val = false;
  if ((new_part2_caps != 0) || !get(Scap,0,0,scap_val))
    set(Scap,0,0,scap_val = (new_part2_caps != 0));
  
  // Check for incompatible broadcast profile specifications
  if ((!have_broadcast) && (profile == Sprofile_BROADCAST))
    { // Install the default broadcast profile values
      set(Sbroadcast,0,0,broadcast_level=1);
      set(Sbroadcast,0,1,broadcast_multi_tile=0);
      set(Sbroadcast,0,2,broadcast_reversible=0);
      have_broadcast = true;
    }
  if (have_broadcast)
    { 
      if (profile != Sprofile_BROADCAST)
        { KDU_ERROR(e,0x25041006); e <<
          KDU_TXT("The `Sprofile' and `Sbroadcast' parameter attributes are "
                  "incompatible.  You have specified `Sbroadcast' parameters, "
                  "yet the `Sprofile' value was set to something other than "
                  "\"BROADCAST\".");
        }
      if ((broadcast_level < 0) || (broadcast_level > 11) ||
          ((broadcast_level < 6) && broadcast_reversible) ||
          ((broadcast_level > 7) && broadcast_reversible))
        { KDU_ERROR(e,0x06121402); e <<
          KDU_TXT("The `Sbroadcast' parameter attributes are incompatible.  "
                  "The first first field of this attribute identifies the "
                  "broadcast level, which must be in the range 0 to 11.  The "
                  "multi-tile reversible profile is restricted to levels 6 "
                  "and 7 only, but no level restrictions apply to the "
                  "single-tile or multi-tile irreversible profiles, since "
                  "the introduction of IS15444-1/AMD8.");
        }
    }
  
  // Check for incompatible IMF profile specifications
  int imf_size_class = 0; // 2048x1556
  if ((!have_imf) && (profile == Sprofile_IMF))
    { // Install the default broadcast profile values
      set(Simf,0,0,imf_main_lev=0);
      set(Simf,0,1,imf_sub_lev=0);
      set(Simf,0,2,imf_reversible=0);
      have_imf = true;
    }
  if (have_imf)
    { 
      if (profile != Sprofile_IMF)
        { KDU_ERROR(e,0x06121403); e <<
          KDU_TXT("The `Sprofile' and `Simf' parameter attributes are "
                  "incompatible.  You have specified `Simf' parameters, "
                  "yet the `Sprofile' value was set to something other "
                  "than \"IMF\".");
        }
      if ((imf_sub_lev < 0) || (imf_sub_lev > 9) ||
          (imf_main_lev < 0) || (imf_main_lev > 11) ||
          ((imf_sub_lev > 1) && (imf_sub_lev > (imf_main_lev-2))))
        { KDU_ERROR(e,0x06121404); e <<
          KDU_TXT("The `Simf' main-level and sub-level parameters are "
                  "incompatible.  The main-level must lie in the range "
                  "0 to 11, while the sub-level must lie in the range 0 "
                  "to 9.  Moreover, sub-level may not exceed "
                  "the larger of 1 and the main-level minus 2.");
        }
      if ((size_x > 2048) || (size_y > 1556))
        { 
          imf_size_class = 1;
          if ((size_x > 4096) || (size_y > 3112))
            imf_size_class = 2;
        }
    }
  
  // Perform some Digital Cinema (or Broadcast) profile checks
  if ((profile < Sprofile_CINEMA2K) || (profile > Sprofile_IMF))
    return;
  if (profile != new_profile)
    { KDU_WARNING(w,0x11110810); w <<
        KDU_TXT("Code-stream profile had to be changed from a digital cinema, "
                "broadcast or IMF profile to a PART2 profile, since one or "
                "more PART2 features have been used.");
      return;
    }
  bool broadcast_422 = false;
  if (profile != Sprofile_CINEMASS)
    { // Note that CINEMASS does not restrict sub-sampling, or sample
      // precision/signed characteristics
      for (n=0; n < components; n++)
        { 
          int sub_val_x=0, sub_val_y=0;
          get(Ssampling,n,0,sub_val_y);
          get(Ssampling,n,1,sub_val_x);
          get(Ssigned,n,0,signed_val);
          get(Sprecision,n,0,prec_val);
          int min_sub_x=1, max_sub_x=1, min_prec=12, max_prec=12;
          if (have_broadcast || have_imf)
            { 
              min_prec = 8;
              if (have_imf)
                max_prec = 16;
              if (n == 1)
                { 
                  max_sub_x = 2;
                  broadcast_422 = (sub_val_x == 2);
                }
              else if ((n == 2) && broadcast_422)
                min_sub_x = max_sub_x = 2;
            }
          if (signed_val || (sub_val_y != 1) ||
              (sub_val_x < min_sub_x) || (sub_val_x > max_sub_x) ||
              (prec_val > max_prec) || (prec_val < min_prec))
            { KDU_ERROR(e,0x06121405); e <<
              KDU_TXT("Profile violation detected.  Code-streams with one "
                      "of the Digital Cinema profiles CINEMA2K, CINEMA4K, "
                      "CINEMA2S or CINEMA4S must have unsigned image "
                      "components with a precision of 12 bits and all "
                      "sub-sampling factors equal to 1.  Those marked "
                      "with a BROADCAST profile have similar restrictions, "
                      "except that the precision can be anywhere from 8 to 12 "
                      "and the second and third image components may "
                      "alternatively both have horizontal sub-sampling "
                      "factors of 2.  Codestreams with an IMF profile "
                      "have almost the same restrictions as the BROADCAST "
                      "case, except that the maximum allowed precision is "
                      "16.");
            }
        }
    }
  
  int max_components=3, min_components=3;
  if (have_broadcast)
    { max_components=4; min_components=1; }
  else if (profile==Sprofile_CINEMASS)
    { max_components=8; min_components=1; }
  else if (have_imf)
    min_components=1;
  if ((components < min_components) || (components > max_components))
    { KDU_ERROR(e,0x25041008); e <<
      KDU_TXT("Profile violation detected.  Code-streams marked with one of "
              "the Digital Cinema profiles CINEMA2K, CINEMA4K, CINEMA2S or "
              "CINEMA4S must have exactly 3 image components; those marked "
              "with a BROADCAST profile may have at most 4 image components, "
              "while those marked with an IMF profile may have at most 3 "
              "components and the \"long term storage\" cinema "
              "profile allows at most 8 image components.");
    }
  if ((origin_x != 0) || (origin_y != 0) || (tile_ox != 0) || (tile_oy != 0))
    { KDU_ERROR(e,0x25041009); e <<
      KDU_TXT("Profile violation detected.  Code-streams marked with a "
              "Digital Cinema (CINEMA2K, CINEMA4K, CINEMA2S, CINEMA4S or "
              "CINEMASS), BROADCAST or IMF profile must have their image and "
              "tile partition anchored at the origin.");
    }
  int h_tiles=ceil_ratio(size_x,tile_x), v_tiles=ceil_ratio(size_y,tile_y);
  if (profile == Sprofile_CINEMASS)
    { 
      if (((h_tiles > 1) && (tile_x < 1024)) ||
          ((v_tiles > 1) && (tile_y < 512)))
        { KDU_ERROR(e,0x19091301); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with the "
                  "\"long term storage\" cinema profile must have tiles "
                  "that are at least 1024 wide and 512 high, or else use "
                  "only a single tile.");
        }
    }
  else if (have_broadcast && broadcast_multi_tile)
    { 
      if (!(((v_tiles == 1) && (h_tiles == 1)) ||
            ((v_tiles == 2) && (h_tiles == 2)) ||
            ((v_tiles == 4) && (h_tiles == 1))))
        { KDU_ERROR(e,0x250410B); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with a "
                  "multi-tile BROADCAST profile may have either 1 tile, or 4 "
                  "tiles all of the same size, where multiple tiles must be "
                  "arranged either as 2 across by 2 down, or 1 across by 4 "
                  "down.");
        }
    }
  else if (have_imf && imf_reversible && ((h_tiles > 1) || (v_tiles > 1)))
    { 
      int imf_test=0, tile_size_test=1024;
      for (; imf_test <= imf_size_class; imf_test++, tile_size_test<<=1)
        if ((tile_x == tile_size_test) && (tile_y == tile_size_test))
          break;
      if (imf_test > imf_size_class)
        { KDU_ERROR(e,0x06121406); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with a "
                  "reversible IMF profile must either be untiled, or else "
                  "tiled with tiles of size 1024x1024, 2048x2048 or "
                  "4096x4096; moreover, the larger tiles are only compatible "
                  "with images large enough to require the larger format "
                  "IMF profile variants.");
        }
    }
  else if ((h_tiles != 1) || (v_tiles != 1))
    { KDU_ERROR(e,0x2504100A); e <<
      KDU_TXT("Profile violation detected.  Code-streams marked with a "
              "Digital Cinema (CINEMA2K or CINEMA4K) or a single-tile "
              "BROADCAST or IMF profile must have only one tile.");
    }
  
  if ((profile == Sprofile_CINEMA2K) || (profile == Sprofile_CINEMA2S))
    {
      if ((size_x > 2048) || (size_y > 1080))
        { KDU_ERROR(e,0x11110814); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with the "
                  "CINEMA2K Digital Cinema profile may have a maximum width "
                  "of 2048 and a maximum height of 1080.");
        }
    }
  else if ((profile == Sprofile_CINEMA4K) || (profile == Sprofile_CINEMA4S))
    {
      if ((size_x > 4096) || (size_y > 2160))
        { KDU_ERROR(e,0x11110815); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with the "
                  "CINEMA4K Digital Cinema profile may have a maximum width "
                  "of 4096 and a maximum height of 2160.");
        }
    }
  else if (profile == Sprofile_CINEMASS)
    { 
      if ((size_x > 16386) || (size_y > 8640))
        { KDU_ERROR(e,0x19091302); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with the "
                  "CINEMASS Digital Cinema profile (for \"long term "
                  "storage\") may have a maximum width of 16384 and a maximum "
                  "height of 8640.");
        }
    }
  else if (profile == Sprofile_IMF)
    { 
      if ((size_x > (2048<<imf_size_class)) ||
          (size_y > (1556<<imf_size_class)))
        { KDU_ERROR(e,0x06121408); e <<
          KDU_TXT("Profile violation detected.  Code-streams marked with an "
                  "IMF profile may not have dimensions that exceed the 8K "
                  "limit of 6224x8192 (height x width).");
        }
    }
  
  kdu_params *org = access_cluster(ORG_params);
  if (org != NULL)
    { 
      int tparts = 0;
      if (!org->get(ORGtparts,0,0,tparts))
        org->set(ORGtparts,0,0,tparts=ORGtparts_C);
      if (tparts != ORGtparts_C)
        { KDU_WARNING(w,0x26041001); w <<
          KDU_TXT("Profile violation detected.  Code-streams marked with a "
                  "Digital Cinema profile (CINEMA2K, CINEMA4K, CINEMA2S, "
                  "CINEMA4S or CINEMASS), a BROADCAST or an IMF profile "
                  "must have tile-parts generated according to the "
                  "\"ORGtparts=C\" flavour.  Letting this go for now, since "
                  "you may be intending to reorganize tile-parts yourself "
                  "later on.");
        }
      
      int num_tparts = components * h_tiles * v_tiles;
      if ((profile == Sprofile_CINEMA4K) || (profile == Sprofile_CINEMA2S))
        num_tparts *= 2;
      else if (profile == Sprofile_CINEMA4S)
        num_tparts *= 4;
      org->set(ORGgen_tlm,0,0,num_tparts);

      if ((profile == Sprofile_CINEMA2S) || (profile == Sprofile_CINEMA4S) ||
          (profile == Sprofile_CINEMASS))
        { // Need to include PLT marker segments
          bool gen_plt = false;
          if (!org->get(ORGgen_plt,0,0,gen_plt))
            org->set(ORGgen_plt,0,0,gen_plt=true);
          if (!gen_plt)
            { KDU_WARNING(w,0x21091304); w <<
              KDU_TXT("Profile violation detected.  Code-streams marked with "
                      "one of the scalable digital cinema profiles "
                      "CINEMA2S, CINEMA4S or CINEMASS are required to have "
                      "PLT marker segments in each tile-part header, yet "
                      "it appears that you have explicitly disabled this.  "
                      "Letting this go for now, since you may be intending "
                      "to add these marker segments back in yourself later.");
            }
        }
    }
  
  if (cod_root == NULL)
    return;
  int num_levels=0;
  if (!cod_root->get(Clevels,0,0,num_levels))
    { // Sets defaults which will be acceptable within the profile 
      num_levels = 5;
      if ((profile == Sprofile_CINEMA4K) || (profile == Sprofile_CINEMA4S))
        num_levels = 6;
      else if (profile == Sprofile_CINEMASS)
        { 
          while (((64<<num_levels) < size_x) || ((64<<num_levels) < size_y))
            num_levels++;
        }
      else if (have_imf)
        { // Special considerations for IMF decomposition levels
          num_levels = 5 + imf_size_class;
          if ((h_tiles > 1) || (v_tiles > 1))
            while ((num_levels > 4) &&
                   ((tile_x < (64<<num_levels)) ||
                    (tile_y < (64<<num_levels))))
              num_levels--;
        }
      cod_root->set(Clevels,0,0,num_levels);
    }
  kdu_params *poc = access_cluster(POC_params);
  if ((poc == NULL) ||
      (profile < Sprofile_CINEMA2K) || (profile > Sprofile_CINEMASS))
    return;
  int params_4K[12] =
    { 0,0,1,num_levels,3,Corder_CPRL,
      num_levels,0,1,num_levels+1,3,Corder_CPRL};
  int params_2S[12] =
    { 0,0,1,num_levels+1,3,Corder_CPRL,
      0,0,2,num_levels+1,3,Corder_CPRL};
  int params_4S[24] =
    { 0,0,1,num_levels,3,Corder_CPRL,
      num_levels,0,1,num_levels+1,3,Corder_CPRL,
      0,0,2,num_levels,3,Corder_CPRL,
      num_levels,0,2,num_levels+1,3,Corder_CPRL};
  poc->delete_unparsed_attribute(Porder); // Helps with transcoding
  int pval, num_params, *params;
  if (profile == Sprofile_CINEMA4K)
    { num_params=12; params=params_4K; }
  else if (profile == Sprofile_CINEMA2S)
    { num_params=12; params=params_2S; }
  else if (profile == Sprofile_CINEMA4S)
    { num_params=24; params=params_4S; }
  else
    { num_params = 0; params = NULL; }
  bool existing_error = false;
  for (n=0; n < num_params; n++)
    if (!poc->get(Porder,n/6,n%6,pval))
      break;
    else if (pval != params[n])
      { existing_error = true; break; }
  if ((n == num_params) && poc->get(Porder,num_params/6,0,pval))
    existing_error = true;
  if (existing_error || ((n != 0) && (n != num_params)))
    { KDU_ERROR(e,0x11110832); e <<
      KDU_TXT("Profile violation detected.  You have supplied `Porder' "
              "attributes which are not consistent with the Digital "
              "Cinema or BROADCAST profile specified.  You should let the "
              "parameter management system set the `Porder' attributes "
              "for you, to ensure profile compliance.");
    }
  if (n == 0)
    for (n=0; n < num_params; n++)
      poc->set(Porder,n/6,n%6,params[n]);
}


/* ========================================================================= */
/*                                mct_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                       mct_params::mct_params (no args)                    */
/*****************************************************************************/

mct_params::~mct_params() {}
mct_params::mct_params()
  : kdu_params(MCT_params,true,false,true,false,true)
{
  define_attribute(Mmatrix_size,
    "Identifies the number of matrix elements, if any, represented "
    "by this object.  The actual matrix coefficients are represented "
    "by the `Mmatrix_coeffs' attribute.  Matrices are used to describe "
    "reversible and irreversible inverse component decorrelation "
    "transforms.  This is done by referencing the current attribute's "
    "instance index from the second field in each record of the "
    "`Mstage_xforms' attribute used to describe a multi-component "
    "transformation stage.  Thus, for example, "
    "\"Mstage_xforms:I1={MATRIX,1,4,0,0},{MATRIX,3,0,1,0}\" declares that "
    "a given multi-component transform stage, having instance index 1, "
    "and two component collections, employs matrix transforms for both "
    "collections.  The first collection's matrix is described by "
    "`Mmatrix_size:I1' and `Mmatrix_coeffs:I1', while the second "
    "collection's matrix is described by `Mmatrix_size:I3' and "
    "`Mmatrix_coeffs:I3'.  To understand the last two fields in each "
    "record of the `Mstage_xforms' attribute, please refer to the "
    "separate description of that attribute.",
    "I");
  define_attribute(Mmatrix_coeffs,
    "Coefficients of the matrix, if there is one, whose number of "
    "elements is given by `Mmatrix_size'.  The coefficients appear "
    "in row-major order (first row, then second row, etc.).  The "
    "height and width of the matrix are not recorded here, but "
    "matrices are not required to be square.  For reversible transforms, "
    "the matrix coefficients are required to be integers.",
    "F",MULTI_RECORD);
  define_attribute(Mvector_size,
    "Identifies the number of vector elements, if any, represented "
    "by this object.  The actual vector coefficients are represented "
    "by the `Mvector_coeffs' attribute.  Vectors are used to describe "
    "offsets to be applied to the component sample values after "
    "inverse transformation.  This is done by referencing the current "
    "attribute's instance index from the third field in each record of "
    "the `Mstage_xforms' attribute used to describe a multi-component "
    "transform stage.  Thus, for example, "
    "\"Mstage_xforms:I1={MATRIX,1,4,0,0},{MATRIX,3,0,1,0}\" declares that "
    "a given multi-component transform stage, having instance index 1, "
    "and two component collections, employs matrix transforms for both "
    "collections.  The first collection also involves offsets, described "
    "via `Mvector_size:I4' and `Mvector_coeffs:I4', while the second "
    "collection does not use offsets.  to understand the remaining fields "
    "in each record of the `Mstage_xforms' attribute, consult the separate "
    "description of that attribute.",
    "I");
  define_attribute(Mvector_coeffs,
    "Coefficients of the vector, if there is one, whose number of "
    "elements is given by `Mvector_size'.  Unlike `Mmatrix_coeffs' and "
    "`Mtriang_coeffs', this attribute is extrapolated if insufficient "
    "parameters are supplied -- that is, the last supplied value is "
    "replicated as required in order to provide all `Mvector_size' "
    "vector elements.",
    "F",MULTI_RECORD|CAN_EXTRAPOLATE);
  define_attribute(Mtriang_size,
    "Identifies the total number of sub-triangular matrix elements, "
    "if any, represented by this object.  A sub-triangular matrix is "
    "square, with no coefficients above the diagonal and at least one "
    "coefficient missing from the diagonal.  A strictly sub-triangular "
    "M x M matrix will have M*(M-1)/2 coefficients, all below the diagonal.  "
    "Matrices of this form are used to describe irreversible multicomponent "
    "dependency transforms.  Reversible dependency transforms, however, "
    "include all but the upper left diagonal entry, for a total of "
    "M*(M+1)/2-1 coefficients.  Dependency transforms are described by "
    "referencing the current attribute's instance index from the second "
    "field in each record of the `Mstage_xforms' attribute used to "
    "describe a multi-component transform stage.  Thus, for example, "
    "\"Mstage_xforms:I1={DEP,5,0,0,0}\" declares that "
    "a given multi-component transform stage, having instance index 1, "
    "and one component collection, employs a dependency transform, whose "
    "coefficients are counted by `Mtriang_size:I5' and found in "
    "`Mtriang_coeffs:I5'.",
    "I");
  define_attribute(Mtriang_coeffs,
    "Coefficients of the sub-triangular matrix, if any, whose number "
    "of elements is represented by the `Mtriang_size' attribute.  The "
    "coefficients are arranged in row-major order.  Thus, for a "
    "dependency transform with M inputs and outputs, the first coefficient "
    "(first two for reversible transforms) comes from the second "
    "row of the matrix, the next two (three for reversible transforms) "
    "comes from the third row of the matrix, and so forth.  For "
    "reversible transforms, the coefficients must all have integer values.",
    "F",MULTI_RECORD);
  
  Zmct_matrix_next = 0;  Zmct_matrix_final = -1;
  Zmct_vector_next = 0;  Zmct_vector_final = -1;
  Zmct_triang_next = 0;  Zmct_triang_final = -1;
}

/*****************************************************************************/
/*                          mct_params::new_object                           */
/*****************************************************************************/

kdu_params *mct_params::new_object()
{
  return new mct_params(); 
}

/*****************************************************************************/
/*                       mct_params::copy_with_xforms                        */
/*****************************************************************************/

void
  mct_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  float coeff=0.0f;
  int n, count=0;
  if (source->get(Mmatrix_size,0,0,count,false) && (count > 0))
    {
      set(Mmatrix_size,0,0,count);
      for (n=0; (n < count) && source->get(Mmatrix_coeffs,n,0,coeff); n++)
        set(Mmatrix_coeffs,n,0,coeff);
    }
  if (source->get(Mvector_size,0,0,count,false) && (count > 0))
    {
      set(Mvector_size,0,0,count);
      for (n=0; (n < count) && source->get(Mvector_coeffs,n,0,coeff); n++)
        set(Mvector_coeffs,n,0,coeff);
    }
  if (source->get(Mtriang_size,0,0,count,false) && (count > 0))
    {
      set(Mtriang_size,0,0,count);
      for (n=0; (n < count) && source->get(Mtriang_coeffs,n,0,coeff); n++)
        set(Mtriang_coeffs,n,0,coeff);
    }
}

/*****************************************************************************/
/*                           mct_params::finalize                            */
/*****************************************************************************/

void
  mct_params::finalize(bool after_reading)
{
  if (after_reading)
    {
      if ((Zmct_matrix_next <= Zmct_matrix_final) ||
          (Zmct_vector_next <= Zmct_vector_final) ||
          (Zmct_triang_next <= Zmct_triang_final))
        { KDU_ERROR(e,0x20070500); e <<
            KDU_TXT("Failed to read all MCT marker segments in a series "
            "associated with a given `Imct' index within a main or initial "
            "tile-part header.  Codestream is not correctly constructed.");
        }
      return;
    }
  int matrix_size=0, vector_size=0, triang_size=0;
  float coeff=0.0f;
  if ((get(Mmatrix_size,0,0,matrix_size,false) && (matrix_size <= 0)) ||
      (get(Mvector_size,0,0,vector_size,false) && (vector_size <= 0)) ||
      (get(Mtriang_size,0,0,triang_size,false) && (triang_size <= 0)))
    { KDU_ERROR(e,0x20070501); e <<
        KDU_TXT("Illegal value (anything <= 0) found for `Mmatrix_size', "
        "`Mvector_size' or `Mtriang_size' attribute while finalizing the "
        "MCT transform coefficients.");
    }
  if ((inst_idx == 0) && (matrix_size | vector_size | triang_size))
    { KDU_ERROR(e,0x21070501); e <<
        KDU_TXT("It is illegal to supply `Mvector_size', `Mtriang_size' "
        "or `Mvector_size' attributes with zero-valued instance indices "
        "(equivalently, with missing instance indices).  Use the \":I=...\" "
        "suffix when providing MCT transform coefficients.");
    }
  if (((matrix_size > 0) && !get(Mmatrix_coeffs,matrix_size-1,0,coeff)) ||
      ((vector_size > 0) && !get(Mvector_coeffs,vector_size-1,0,coeff)) ||
      ((triang_size > 0) && !get(Mtriang_coeffs,triang_size-1,0,coeff)))
    { KDU_ERROR(e,0x20070502); e <<
        KDU_TXT("The number of `Mmatrix_coeffs', `Mvector_coeffs' or "
        "`Mtriang_coeffs' entries found while finalizing MCT transform "
        "coefficients does not match the corresponding `Mmatrix_size', "
        "`Mvector_size' or `Mtriang_size' value.");
    }
}

/*****************************************************************************/
/*                     mct_params::check_marker_segment                      */
/*****************************************************************************/

bool
  mct_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_MCT) || (num_bytes < 4))
    return false;
  int i_mct = bytes[2];  i_mct = (i_mct<<8) + bytes[3];
  c_idx = i_mct & 0x00FF;
  return ((c_idx >= 1) && (c_idx <= 255) && (((i_mct >> 8) & 3) != 3));
}

/*****************************************************************************/
/*                        mct_params::read_marker_segment                    */
/*****************************************************************************/

bool
  mct_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  if ((code != KDU_MCT) || (num_bytes < 4) || (inst_idx == 0))
    return false;

  bp = bytes;
  end = bp + num_bytes;

  int z_mct = *(bp++);  z_mct = (z_mct<<8) + *(bp++);
  int i_mct = *(bp++);  i_mct = (i_mct<<8) + *(bp++);
  int which_inst = i_mct & 0x00FF;
  int xform_type = (i_mct >> 8) & 3;
  int coeff_type = (i_mct >> 10) & 3;
  if ((which_inst != inst_idx) || (xform_type == 3))
    return false;

  try {
      const char *size_name=NULL, *coeffs_name=NULL;
      int y_mct = (z_mct==0)?(kdu_read(bp,end,2)):0;
      int z_next=0, z_final=0;
      switch (xform_type) {
        case 0: // Dependency transform coefficients
          if (z_mct == 0) Zmct_triang_final = y_mct;
          z_next = Zmct_triang_next++;  z_final = Zmct_triang_final;
          size_name = Mtriang_size;  coeffs_name = Mtriang_coeffs;
          break;
        case 1: // Decorrelation matrix coefficients
          if (z_mct == 0) Zmct_matrix_final = y_mct;
          z_next = Zmct_matrix_next++;  z_final = Zmct_matrix_final;
          size_name = Mmatrix_size;  coeffs_name = Mmatrix_coeffs;
          break;
        case 2: // Offset vector coefficients
          if (z_mct == 0) Zmct_vector_final = y_mct;
          z_next = Zmct_vector_next++;  z_final = Zmct_vector_final;
          size_name = Mvector_size;  coeffs_name = Mvector_coeffs;
          break;
        case 3:
          abort();
        }

      if ((z_mct > z_final) || (z_mct < z_next))
        { KDU_ERROR(e,0x20070503); e <<
            KDU_TXT("Encountered repeat or out-of-range `Zmct' field while "
            "parsing an MCT marker segment.  The `Zmct' field is used to "
            "enumerate marker segments which belong to a common series, but "
            "the value encountered is inconsistent with the rest of the "
            "series.  This is a malformed codestream.");
        }
      if (z_mct != z_next)
        { KDU_ERROR(e,0x20070504); e <<
            KDU_TXT("Encountered out-of-order `Zmct' field while "
            "parsing MCT marker segments belonging to a series.  While "
            "this is not strictly illegal, it makes no sense for a content "
            "creator to write MCT marker segments out of order.  Kakadu "
            "does not currently support reordering of these optional Part-2 "
            "marker segments.");
        }

      int bytes_per_coeff=0;
      switch (coeff_type) {
        case 0: bytes_per_coeff = 2; break;
        case 1:
        case 2: bytes_per_coeff = 4; break;
        case 3: bytes_per_coeff = 8; break;
        }

      int num_coeffs = ((int)(end-bp)) / bytes_per_coeff;
      int c, existing_coeffs = 0;
      if (z_next > 0)
        get(size_name,0,0,existing_coeffs);
      set(size_name,0,0,existing_coeffs+num_coeffs);
      for (c=0; c < num_coeffs; c++)
        {
          int ival;
          float coeff=0.0F;
          switch (coeff_type) {
            case 0: // 16-bit integer coefficients
              ival = kdu_read(bp,end,2);
              ival -= (ival & 0x8000)?(1<<16):0; // Convert to 2's comp
              coeff = (float) ival;
              break;
            case 1: // 32-bit integer coefficients
              ival = kdu_read(bp,end,4);
              coeff = (float) ival;
              break;
            case 2: // 32-bit floating point coefficients
              coeff = kdu_read_float(bp,end);
              break;
            case 3: // 64-bit floating point coefficients
              coeff = (float) kdu_read_double(bp,end);
              break;
            }
          set(coeffs_name,existing_coeffs+c,0,coeff);
        }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,0x20070505); e <<
        KDU_TXT("Malformed MCT marker segment encountered.  Marker segment "
        "is too small.");
    }
  if (bp != end)
    { KDU_ERROR(e,0x20070506); e <<
        KDU_TXT("Malformed MCT marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  return true;
}

/*****************************************************************************/
/*                       mct_params::write_marker_segment                    */
/*****************************************************************************/

int
  mct_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  if ((inst_idx < 1) || (inst_idx > 255) || (tpart_idx != 0) ||
      (comp_idx >= 0))
    return 0;

  // Collect the array sizes
  int typed_sizes[3]={0,0,0}; // 0 for triang, 1 for matrix, 2 for vector
  get(Mtriang_size,0,0,typed_sizes[0],false);
  get(Mmatrix_size,0,0,typed_sizes[1],false);
  get(Mvector_size,0,0,typed_sizes[2],false);

  // Calculate the length of all marker segments
  int t, length = 0;
  for (t=0; t < 3; t++)
    {
      if (typed_sizes[t] == 0)
        continue; // Nothing for this type
      int max_mct_coeffs = (16384-16)/4; // Leaves up to 16 bytes for header
      int num_segs = 1 + ((typed_sizes[t]-1) / max_mct_coeffs);
      length += 10; // Marker code, length, Zmct, Imct and Ymct fields
      length += 8 * (num_segs-1); // Extra segs don't have Ymct
      length += typed_sizes[t] * 4; // the coefficients themselves
    }

  if ((out == NULL) || (length == 0))
    return length;

  // If we get here, we are committed to actually writing the marker segments
  int acc_length = 0; // Accumulate actual length for verification purposes.
  const char *typed_names[3] = {Mtriang_coeffs,Mmatrix_coeffs,Mvector_coeffs};
  for (t=0; t < 3; t++)
    {
      if (typed_sizes[t] == 0)
        continue; // Nothing for this type

      // See if we should use integers or floats to represent the data
      int n;
      float coeff=0.0F;
      bool use_ints = true;
      for (n=0; n < typed_sizes[t]; n++)
        {
          get(typed_names[t],n,0,coeff);
          coeff -= (float) floor(coeff+0.5);
          if ((coeff > 0.0001F) || (coeff < 0.0001F))
            { use_ints = false; break; }
        }

      // Now write the marker segments
      int max_mct_coeffs = (16384-16)/4; // Leaves up to 16 bytes for header
      int z, num_segs = 1 + ((typed_sizes[t]-1) / max_mct_coeffs);
      int seg_mct_coeffs, written_mct_coeffs=0;
      for (z=0; z < num_segs; z++, written_mct_coeffs+=seg_mct_coeffs)
        {
          seg_mct_coeffs = typed_sizes[t] - written_mct_coeffs;
          if (seg_mct_coeffs > max_mct_coeffs)
            seg_mct_coeffs = max_mct_coeffs;
          int seg_length = 8 + 4*seg_mct_coeffs + ((z==0)?2:0);
          acc_length += out->put(KDU_MCT);
          acc_length += out->put((kdu_uint16)(seg_length-2));
          acc_length += out->put((kdu_uint16) z);
          int i_mct = inst_idx + (t<<8);
          if (use_ints)
            i_mct += (1<<10); // Record as 32-bit integers
          else
            i_mct += (2<<10); // Record as 32-bit floats
          acc_length += out->put((kdu_uint16) i_mct);
          if (z == 0)
            acc_length += out->put((kdu_uint16)(num_segs-1));
          for (n=0; n < seg_mct_coeffs; n++)
            {
              get(typed_names[t],written_mct_coeffs+n,0,coeff);
              if (use_ints)
                {
                  int int_coeff = (int) floor(coeff + 0.5);
                  acc_length += out->put((kdu_uint32) int_coeff);
                }
              else
                acc_length += out->put(coeff);
            }
        }
    }

  assert(length == acc_length);
  return length;
}


/* ========================================================================= */
/*                                mcc_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          mcc_params::new_object                           */
/*****************************************************************************/

kdu_params *mcc_params::new_object()
{
  return new mcc_params(); 
}

/*****************************************************************************/
/*                       mcc_params::mcc_params (no args)                    */
/*****************************************************************************/

mcc_params::~mcc_params() {}
mcc_params::mcc_params()
  : kdu_params(MCC_params,true,false,true,false,true)
{
  define_attribute(Mstage_inputs,
    "This attribute is used to describe a list of input component "
    "indices which are used by all transform blocks in a single stage of the "
    "multi-component transform.  This list of component indices is a "
    "concatenation of the index ranges <A1>-<B1>, <A2>-<B2>, ..., where "
    "An <= Bn are the first and second fields in the n'th record of the "
    "`Mstage_inputs' attribute.   The list of input component indices may "
    "contain repeated values, but must cover all components produced by the "
    "previous stage (or all codestream component indices, if this is "
    "the first stage).  In particular, it must always include 0.  "
    "The first transform block operates on the first N1 components "
    "identified by this list; the second transform block operates on the "
    "next N2 components in this list; and so forth.",
    "II",MULTI_RECORD);
  define_attribute(Mstage_outputs,
    "This attribute is used to describe a list of output component "
    "indices which are produced by this stage.  This list  of component "
    "indices is a concatenation of the index ranges <A1>-<B1>, <A2>-<B2>, "
    "..., where An <= Bn are the first and second fields in the n'th "
    "record of the `Mstage_outputs' attribute.  The list of output "
    "component indices may not contain any repeated component indices, "
    "but it may contain \"holes\".  The transform stage is considered "
    "to generate components with indices from 0 to the largest index "
    "in the output list; any components in this range which are not "
    "listed (these are the holes) are taken to be identically equal to "
    "0.  The first transform block in the stage processes the first N1 "
    "components in the list to produces the first M1 components in the "
    "output list; the second transform block in the stage processes the "
    "next N1 components in the input list, producing the next M2 "
    "components in the output list; and so forth.",
    "II",MULTI_RECORD);
  define_attribute(Mstage_collections,
    "This attribute provides the values Nc and Mc which appear in the "
    "descriptions of `Mstage_inputs' and `Mstage_outputs', for each "
    "transform block (equivalently, each component collection), c.  "
    "The `Mstage_collections' parameter attribute should contain one "
    "record for each transform.  Each record contains two strictly "
    "positive integers, identifying the number of input components Nk, "
    "and the number of output components, Mk, produced by the k'th "
    "transform.  No transform may consume or produce 0 components.  "
    "Between them, the various transform blocks must consume all "
    "components in the input list described by `Mstage_inputs' and "
    "produce all components in the output list described by "
    "`Mstage_outputs'.",
    "II",MULTI_RECORD);
  define_attribute(Mstage_xforms,
    "This attribute provides one record for each transform block, "
    "which describes the type of transform to be implemented in that "
    "block and the parameters of the transform.  The first field "
    "identifies the transform as one of \"dependency transform\" "
    "(`DEP'), \"decorrelation matrix transform\" (`MATRIX'), or \"discrete "
    "wavelet transform\" (`DWT').  Do not use the `MAT' option; that option "
    "is provided to catch backward compatibility problems with Kakadu "
    "versions prior to v6.0, in which reversible decorrelation matrix "
    "transforms used a non-compliant organization for the coefficient "
    "values.  Kakadu will refuse to generate codestreams which use the "
    "`MAT' option, although it should be able to correctly recover and "
    "render codestreams generated with this option prior to v6.0.  It "
    "does this by recognizing the absence of the `Cmct' parameter "
    "attribute (another oversight prior to v6.0) as an indication that "
    "the non-compliant organization is being used.\n"
    "\t\t   The 2'nd field of each record holds the instance index of the "
    "`Mtriang_coeffs' (for dependency transforms) or `Mmatrix_coeffs' "
    "(for decorrelation matrix transforms) attributes, which provide "
    "the actual transform coefficients, unless the transform is a DWT; "
    "in this last case the 2'nd field holds 0 for the 9/7 DWT, 1 "
    "for the 5/3 DWT, or the instance index (in the range 2 to 255) of an "
    "`ATK' marker segment whose `Kreversible', `Ksymmetric', `Kextension', "
    "`Ksteps' and `Kcoeffs' attributes describe the DWT kernel.  Apart from "
    "DWT transforms, a 0 for this field means that the transform block "
    "just passes its inputs through to its outputs (setting any extra "
    "output components equal to 0) and adds any offsets specified via the "
    "3'rd field -- we refer to this as a \"null\" transform block.\n"
    "\t\t   The 3'rd field of each record holds the instance index of the "
    "`Mvector_coeffs' attribute which describes any offsets to be "
    "applied after inverse transformation of the input components to "
    "the block.  A value of 0 for this field means that there is no "
    "offset; otherwise, the value must be in the range 1 to 255.\n"
    "\t\t   For DWT transforms, the 4'th field in the record identifies "
    "the number of DWT levels to be used, in the range 0 to 32, while "
    "the final field holds the transform origin, which plays the "
    "same role as `Sorigin', but along the component axis.  For "
    "dependency and decorrelation transforms, the 4'th field "
    "must hold 0 if the transform is irreversible, or 1 if it is "
    "reversible, while the 5'th field must hold 0.",
    "(DEP=0,MATRIX=9,DWT=3,MAT=1000)IIII",MULTI_RECORD);
}

/*****************************************************************************/
/*                           mcc_params::finalize                            */
/*****************************************************************************/

void
  mcc_params::finalize(bool after_reading)
{
  if (after_reading)
    return;

  int n, from=0, to=0;
  int input_components=0, output_components=0;
  for (n=0; get(Mstage_inputs,n,0,from,false,false) &&
            get(Mstage_inputs,n,1,to,false,false); n++)
    {
      if ((from > to) || (from < 0) || (to >= 16384))
        { KDU_ERROR(e,0x21070503); e <<
            KDU_TXT("Illegal parameters supplied for `Mstage_inputs' "
            "attribute.  Component index ranges must have lower bounds "
            "which do not exceed their corresponding upper bounds, both "
            "of which must be in the range 0 to 16383.");
        }
      input_components += to+1-from;
    }
  for (n=0; get(Mstage_outputs,n,0,from,false,false) &&
            get(Mstage_outputs,n,1,to,false,false); n++)
    {
      if ((from > to) || (from < 0) || (to >= 16384))
        { KDU_ERROR(e,0x21070504); e <<
            KDU_TXT("Illegal parameters supplied for `Mstage_outputs' "
            "attribute.  Component index ranges must have lower bounds "
            "which do not exceed their corresponding upper bounds, both "
            "of which must be in the range 0 to 16383.");
        }
      output_components += to+1-from;
    }
  int num_blocks=0, block_in=0, block_out=0;
  for (n=0; get(Mstage_collections,n,0,block_in,false,false) &&
            get(Mstage_collections,n,1,block_out,false,false); n++)
    {
      num_blocks++;
      input_components -= block_in;
      output_components -= block_out;
      if ((block_in < 1) || (block_out < 1))
        { KDU_ERROR(e,0x21070505); e <<
            KDU_TXT("Malformed `Mstage_collections' attribute encountered in "
            "`mcc_params::finalize'.  Each transform block must be assigned "
            "a strictly positive number of input and output components.");
        }
    }
  if ((input_components != 0) || (output_components != 0))
    { KDU_ERROR(e,0x21070506); e <<
        KDU_TXT("Malformed `Mstage_collections' attribute encountered in "
        "`mcc_params::finalize'.  The transform blocks must together consume "
        "all input components defined by `Mstage_inputs' (no more and no "
        "less) and produce all output components defined by `Mstage_outputs' "
        "(no more and no less).");
    }

  int num_xforms=0, xform_type=0;
  for (n=0; get(Mstage_xforms,n,0,xform_type,false,false); n++)
    {
      num_xforms++;
      int xform_id=0, offset_id=0, aux1=0, aux2=0;
      if ((!(get(Mstage_xforms,n,1,xform_id,false,false) &&
             get(Mstage_xforms,n,2,offset_id,false,false) &&
             get(Mstage_xforms,n,3,aux1,false,false) &&
             get(Mstage_xforms,n,4,aux2,false,false))) ||
          (xform_id < 0) || (xform_id > 255) ||
          (offset_id < 0) || (offset_id > 255))
        { KDU_ERROR(e,0x21070507); e <<
            KDU_TXT("Malformed `Mstage_xforms' attribute encountered in "
            "`mcc_params::finalize'.  Each record must have 5 fields, "
            "the second and third of which must lie in the range 0 to 255.");
        }
      if ((xform_type == Mxform_DWT) && ((aux1 < 0) || (aux1 > 32)))
        { KDU_ERROR(e,0x21070508); e <<
            KDU_TXT("Malformed `Mstage_xforms' attribute encountered in "
            "`mcc_params::finalize'.  The fourth field in a DWT record "
            "must contain a number of DWT levels in the range 0 to 32.");
        }
      if ((xform_type != Mxform_DWT) && ((aux1 != (aux1&1)) || (aux2 != 0)))
        { KDU_ERROR(e,0x21070509); e <<
            KDU_TXT("Malformed `Mstage_xforms' attribute encountered in "
            "`mcc_params::finalize'.  The fourth field in a DEP or MATRIX "
            "record must hold one of the values 0 (irreversible) or 1 "
            "(reversible), with the fifth field equal to zero.");
        }
    }
  if (num_blocks != num_xforms)
    { KDU_ERROR(e,0x2107050a); e <<
        KDU_TXT("Malformed `Mstage_xforms' attribute encountered in "
        "`mcc_params::finalize'.  The number of records in this attribute "
        "must be identical to the number of records in `Mstage_collections'.");
    }
}

/*****************************************************************************/
/*                       mcc_params::copy_with_xforms                        */
/*****************************************************************************/

void
  mcc_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int n, from=0, to=0;
  for (n=0; source->get(Mstage_inputs,n,0,from,false,false) &&
            source->get(Mstage_inputs,n,1,to,false,false); n++)
    { set(Mstage_inputs,n,0,from);  set(Mstage_inputs,n,1,to); }
  for (n=0; source->get(Mstage_outputs,n,0,from,false,false) &&
            source->get(Mstage_outputs,n,1,to,false,false); n++)
    { set(Mstage_outputs,n,0,from);  set(Mstage_outputs,n,1,to); }

  int block_in=0, block_out=0;
  for (n=0; source->get(Mstage_collections,n,0,block_in,false,false) &&
            source->get(Mstage_collections,n,1,block_out,false,false); n++)
    { set(Mstage_collections,n,0,block_in);
      set(Mstage_collections,n,1,block_out); }

  int xform_type=0, xform_idx=0, offset_idx=0, aux1=0, aux2=0;
  for (n=0; source->get(Mstage_xforms,n,0,xform_type,false,false) &&
            source->get(Mstage_xforms,n,1,xform_idx,false,false) &&
            source->get(Mstage_xforms,n,2,offset_idx,false,false) &&
            source->get(Mstage_xforms,n,3,aux1,false,false) &&
            source->get(Mstage_xforms,n,4,aux2,false,false); n++)
    {
      set(Mstage_xforms,n,0,xform_type);
      set(Mstage_xforms,n,1,xform_idx);  set(Mstage_xforms,n,2,offset_idx);
      set(Mstage_xforms,n,3,aux1);       set(Mstage_xforms,n,4,aux2);
    }
}

/*****************************************************************************/
/*                     mcc_params::check_marker_segment                      */
/*****************************************************************************/

bool
  mcc_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_MCC) || (num_bytes < 3))
    return false;
  c_idx = bytes[2];
  return true;
}

/*****************************************************************************/
/*                        mcc_params::read_marker_segment                    */
/*****************************************************************************/

bool
  mcc_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  if ((code != KDU_MCC) || (num_bytes < 3))
    return false;

  bp = bytes;
  end = bp + num_bytes;

  int z_mcc = *(bp++);  z_mcc = (z_mcc<<8) + *(bp++);
  int which_inst = *(bp++);
  if (which_inst != inst_idx)
    return false;

  if ((z_mcc != 0) || (kdu_read(bp,end,2) != 0))
    { KDU_ERROR(e,0x2107050b); e <<
        KDU_TXT("Encountered MCC (Multi-component transform Component "
        "Collection) information which has been split across multiple "
        "marker segments.  While this is not illegal, Kakadu does not "
        "currently support such massive multi-component transform "
        "descriptions.  It is a rare application indeed that would need "
        "multiple marker segments.");
    }

  try {
      int in_range_idx=0, out_range_idx=0;
      int from, to, c_idx, idx_bytes, n, b;
      int num_blocks = kdu_read(bp,end,2);
      for (b=0; b < num_blocks; b++)
        {
          int xform_type = kdu_read(bp,end,1);
          if (xform_type == 0)
            xform_type = Mxform_DEP;
          else if (xform_type == 1)
            xform_type = Mxform_MATRIX;
          else if (xform_type == 3)
            xform_type = Mxform_DWT;
          else
            xform_type = -1; // Force generation of error below.

          int Nmcc = kdu_read(bp,end,2);
          int block_in = Nmcc & 0x7FFF;
          idx_bytes = 1 + ((Nmcc >> 15) & 1);
          for (from=to=-1, n=0; n < block_in; n++)
            {
              c_idx = kdu_read(bp,end,idx_bytes);
              if (to < 0)
                from = to = c_idx;
              else if (c_idx == (to+1))
                to++;
              else
                {
                  set(Mstage_inputs,in_range_idx,0,from);
                  set(Mstage_inputs,in_range_idx,1,to);
                  in_range_idx++;
                  from = to = c_idx;
                }
            }
          if (to >= 0)
            {
              set(Mstage_inputs,in_range_idx,0,from);
              set(Mstage_inputs,in_range_idx,1,to);
              in_range_idx++;
            }

          int Mmcc = kdu_read(bp,end,2);
          int block_out = Mmcc & 0x7FFF;
          idx_bytes = 1 + ((Mmcc >> 15) & 1);
          for (from=to=-1, n=0; n < block_out; n++)
            {
              c_idx = kdu_read(bp,end,idx_bytes);
              if (to < 0)
                from = to = c_idx;
              else if (c_idx == (to+1))
                to++;
              else
                {
                  set(Mstage_outputs,out_range_idx,0,from);
                  set(Mstage_outputs,out_range_idx,1,to);
                  out_range_idx++;
                  from = to = c_idx;
                }
            }
          if (to >= 0)
            {
              set(Mstage_outputs,out_range_idx,0,from);
              set(Mstage_outputs,out_range_idx,1,to);
              out_range_idx++;
            }
  
          set(Mstage_collections,b,0,block_in);
          set(Mstage_collections,b,1,block_out);
  
          int Tmcc = kdu_read(bp,end,3);
          int xform_idx = Tmcc & 0xFF;
          int offset_idx = (Tmcc >> 8) & 0xFF;
          int aux1=0, aux2=0;
          if (xform_type == Mxform_DWT)
            {
              aux1 = (Tmcc >> 16) & 63;
              aux2 = kdu_read(bp,end,4);
            }
          else
            aux1 = (Tmcc >> 16) & 1;

          if ((block_in < 1) || (block_out < 1) ||
              ((xform_type != Mxform_DEP) && (xform_type != Mxform_MATRIX) &&
               (xform_type != Mxform_DWT)) || (aux1 > 32) ||
              ((block_in != block_out) && (xform_type != Mxform_MATRIX)))
            { KDU_ERROR(e,0x2107050c); e <<
                KDU_TXT("Malformed MCC marker segment encountered.  "
                "Invalid component collection dimensions, transform type "
                "or number of DWT levels.");
            }

          set(Mstage_xforms,b,0,xform_type);
          set(Mstage_xforms,b,1,xform_idx);
          set(Mstage_xforms,b,2,offset_idx);
          set(Mstage_xforms,b,3,aux1);
          set(Mstage_xforms,b,4,aux2);
        }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,0x2107050d); e <<
        KDU_TXT("Malformed MCC marker segment encountered.  Marker segment "
        "is too small.");
    }
  if (bp != end)
    { KDU_ERROR(e,0x2107050e); e <<
        KDU_TXT("Malformed MCC marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  return true;
}

/*****************************************************************************/
/*                       mcc_params::write_marker_segment                    */
/*****************************************************************************/

int
  mcc_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  if ((inst_idx > 255) || (tpart_idx != 0) || (comp_idx >= 0))
    return 0;

  // Determine the precision with which component indices will be represented
  // and the total number of input and output components
  int n, from=0, to=0;
  int idx_bytes=1, comps_in=0, comps_out=0;
  for (n=0; get(Mstage_inputs,n,0,from,false,false) &&
            get(Mstage_inputs,n,1,to,false,false); n++)
    {
      comps_in += to+1-from;
      if ((from > 255) || (to > 255))
        idx_bytes = 2;
    }
  for (n=0; get(Mstage_outputs,n,0,from,false,false) &&
            get(Mstage_outputs,n,1,to,false,false); n++)
    {
      comps_out += to+1-from;
      if ((from > 255) || (to > 255))
        idx_bytes = 2;
    }

  // Calculate the length
  int length = 11; // Marker code, length, Zmcc, Imcc, Ymcc and Qmcc fields
  length += idx_bytes * (comps_in + comps_out);
  int b, xform_type=0;
  for (b=0; get(Mstage_xforms,b,0,xform_type,false,false); b++)
    {
      length += 8; // For Xmcc, Nmcc, Mmcc and Tmcc fields.
      if (xform_type == Mxform_DWT)
        length += 4; // For the Omcc field
    }
  int num_blocks = b;

  if (length > 65537)
    { KDU_ERROR(e,0x2107050f); e <<
        KDU_TXT("Unable to write MCC (Multi-component transform Component "
        "Collection) marker segment, since the amount of "
        "information is too large to fit within a single marker segment.  "
        "The codestream syntax for this Part-2 marker segment allows the "
        "information to be split across multiple marker segments, but this "
        "feature is not yet implemented in Kakadu -- it is a rare application "
        "indeed that should need this.");
    }

  if (num_blocks == 0)
    return 0;

  if (out == NULL)
    return length;

  // If we get here, we are committed to actually writing the marker segments
  int acc_length = 0; // Accumulate actual length for verification purposes.
  acc_length += out->put(KDU_MCC);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_uint16) 0); // Zmcc value
  acc_length += out->put((kdu_byte) inst_idx); // Imcc value
  acc_length += out->put((kdu_uint16) 0); // Ymcc value
  acc_length += out->put((kdu_uint16) num_blocks); // Qmcc value

  int next_range_idx_in=0, next_range_idx_out=0;
  int from_in=0, to_in=-1, from_out=0, to_out=-1;
  int c, block_in=0, block_out=0, xform_idx=0, offset_idx=0, aux1=0, aux2=0;
  for (b=0; b < num_blocks; b++)
    { 
      get(Mstage_collections,b,0,block_in);
      get(Mstage_collections,b,1,block_out);
      get(Mstage_xforms,b,0,xform_type);
      get(Mstage_xforms,b,1,xform_idx);  get(Mstage_xforms,b,2,offset_idx);
      get(Mstage_xforms,b,3,aux1);  get(Mstage_xforms,b,4,aux2);
      if (xform_type == Mxform_DEP)
        acc_length += out->put((kdu_byte) 0);
      else if (xform_type == Mxform_MATRIX)
        acc_length += out->put((kdu_byte) 1);
      else if (xform_type == Mxform_DWT)
        acc_length += out->put((kdu_byte) 3);
      else
        { KDU_ERROR(e,0x04060701); e <<
            KDU_TXT("You can no longer generate a codestream which uses "
            "the `MAT' (`Mxform_MAT' in source code) option for the "
            "`Mstage_xforms' parameter attribute.  From Kakadu v6.0, "
            "matrix-based multi-component transform blocks should use the "
            "`MATRIX' (`Mxform_MATRIX' in source code) identifier.  This "
            "change is designed to force awareness of the fact that "
            "the organization of matrix coefficients for reversible "
            "matrix decorrelation transforms has been altered, in order to "
            "comply with IS15444-2.  In particular, versions prior to v6.0 "
            "considered the coefficients for reversible (SERM) matrix "
            "transforms to have a transposed order, relative to that "
            "specified in the standard.  If you are trying to generate "
            "a codestream which employs only irreversible matrix "
            "transforms, you have simply to substitute `MATRIX' for `MAT'.  "
            "If you are working with reversible matrix multi-component "
            "transforms, you should first transpose the coefficient "
            "matrix.");
        }

      int Nmcc = ((idx_bytes-1)<<15) + block_in;
      acc_length += out->put((kdu_uint16) Nmcc);
      for (c=0; c < block_in; c++)
        {
          if (from_in > to_in)
            {
              get(Mstage_inputs,next_range_idx_in,0,from_in);
              get(Mstage_inputs,next_range_idx_in,1,to_in);
              next_range_idx_in++;
            }
          if (idx_bytes == 1)
            acc_length += out->put((kdu_byte) from_in);
          else
            acc_length += out->put((kdu_uint16) from_in);
          from_in++;
        }

      int Mmcc = ((idx_bytes-1)<<15) + block_out;
      acc_length += out->put((kdu_uint16) Mmcc);
      for (c=0; c < block_out; c++)
        {
          if (from_out > to_out)
            {
              get(Mstage_outputs,next_range_idx_out,0,from_out);
              get(Mstage_outputs,next_range_idx_out,1,to_out);
              next_range_idx_out++;
            }
          if (idx_bytes == 1)
            acc_length += out->put((kdu_byte) from_out);
          else
            acc_length += out->put((kdu_uint16) from_out);
          from_out++;
        }

      acc_length += out->put((kdu_byte) aux1); // MSB of Tmcc
      acc_length += out->put((kdu_byte) offset_idx); // Second byte of Tmcc
      acc_length += out->put((kdu_byte) xform_idx); // LSB of Tmcc
      if (xform_type == Mxform_DWT)
        acc_length += out->put((kdu_uint32) aux2); // Omcc field
    }
  assert(length == acc_length);
  return length;
}


/* ========================================================================= */
/*                               mco_params                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                          mco_params::new_object                           */
/*****************************************************************************/

kdu_params *mco_params::new_object()
{
  return new mco_params(); 
}

/*****************************************************************************/
/*                       mco_params::mco_params (no args)                    */
/*****************************************************************************/

mco_params::~mco_params() {}
mco_params::mco_params()
  : kdu_params(MCO_params,true,false,false)
{
  define_attribute(Mnum_stages,
    "Identifies the number of stages in the multi-component transform "
    "to be applied to this tile, or (for main header attributes) as "
    "a default for tiles which do not specify the `Mnum_stages' attribute.  "
    "If this value is 0, the spatially transformed codestream components "
    "associated with the relevant tile are mapped directly to the "
    "output components specified via the global `Mcomponents', "
    "`Msigned' and `Mprecision' attributes.  If `Mcomponents' is larger "
    "than `Scomponents', some final components are automatically "
    "set to 0.  Where the number of stages is 0, codestream components "
    "which are identified as unsigned by the `Ssigned' attribute are "
    "first offset (at least nominally) by half their dynamic range, "
    "in the usual fashion.  If, on the other hand, `Mnum_stages' specifies "
    "a non-zero number of transform stages, component offsets must "
    "be provided by the multi-component transform stages themselves.\n"
    "\t\t   It is worth noting that the above description applies to "
    "inverse transformation (synthesis) during decompression.  For a "
    "discussion of the conditions under which an appropriate forward "
    "transform can be performed during compression, see the description "
    "of the `Mcomponents' attribute.\n"
    "\t\t[This attribute defaults to 0 if a non-zero `Mcomponents' value "
    "exists, indicating the presence of a multi-component transform.]",
    "I");
  define_attribute(Mstages,
    "Provides `Mnum_stages' records, each of which holds the instance index "
    "(in the range 0 to 255) associated with the `Mstage_inputs', "
    "`Mstage_outputs', `Mstage_collections' and `Mstage_xforms' attributes "
    "which describe the corresponding stage in the inverse multi-component "
    "transform procedure.  The last stage is the one which produces the "
    "final decompressed components described by `Mcomponents', `Msigned' "
    "and `Mprecision'.",
    "I",MULTI_RECORD);
}

/*****************************************************************************/
/*                       mco_params::copy_with_xforms                        */
/*****************************************************************************/

void
  mco_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int num_stages=0;
  int n, stage_idx=0;

  if (!source->get(Mnum_stages,0,0,num_stages))
    return;

  // We may need to create an extra transform stage to account for lost
  // codestream image components.
  int old_components=1, new_components=1;
  kdu_params *siz = access_cluster(SIZ_params);
  kdu_params *source_siz = source->access_cluster(SIZ_params);
  if (siz != NULL)
    siz->get(Scomponents,0,0,new_components);
  if (source_siz != NULL)
    source_siz->get(Scomponents,0,0,old_components);
  int extra_output_stages = 0;
  if ((skip_components > 0) || (new_components != old_components))
    { // Create an appropriate null transform
      kdu_params *mcc = access_cluster(MCC_params);
      mcc = access_relation(tile_idx,-1,0);
      assert(mcc != NULL);
      int free_inst=1;
      bool found_free_inst = false;
      while (!found_free_inst)
        {
          int dummy_val;
          kdu_params *scan;
          found_free_inst = true;
          for (scan=mcc; scan != NULL; scan=scan->access_next_inst())
            if (scan->get_instance() == free_inst)
              {
                found_free_inst = !scan->get(Mstage_inputs,0,0,dummy_val);
                break;
              }
          if (!found_free_inst)
            free_inst++;
        }

      if (free_inst > 255)
        { KDU_ERROR(e,0x22070500); e <<
            KDU_TXT("Unable to modify the existing multi-component transform "
            "to work with a reduced number of codestream image components "
            "during transcoding.  Cannot create a taylored null transform "
            "to interface the components, since all allowed MCC marker "
            "segment instance indices have been used up already.");
        }

      extra_output_stages = 1;
      set(Mstages,0,0,free_inst);

      mcc = mcc->access_relation(tile_idx,-1,free_inst); // Creates a new one
      mcc->set(Mstage_inputs,0,0,0);
      mcc->set(Mstage_inputs,0,1,new_components-1);
      mcc->set(Mstage_outputs,0,0,skip_components);
      mcc->set(Mstage_outputs,0,1,skip_components+new_components-1);
      if (skip_components > 0)
        {
          mcc->set(Mstage_outputs,1,0,0);
          mcc->set(Mstage_outputs,1,1,skip_components-1);
        }
      if ((skip_components+new_components) < old_components)
        {
          mcc->set(Mstage_outputs,2,0,skip_components+new_components);
          mcc->set(Mstage_outputs,2,1,old_components-1);
        }
      mcc->set(Mstage_collections,0,0,new_components);
      mcc->set(Mstage_collections,0,1,old_components);
      mcc->set(Mstage_xforms,0,0,Mxform_MATRIX);
      mcc->set(Mstage_xforms,0,1,0);
      mcc->set(Mstage_xforms,0,2,0);
      mcc->set(Mstage_xforms,0,3,0);
      mcc->set(Mstage_xforms,0,4,0);
    }

  set(Mnum_stages,0,0,num_stages+extra_output_stages);
  for (n=0; (n < num_stages) && source->get(Mstages,n,0,stage_idx); n++)
    set(Mstages,n+extra_output_stages,0,stage_idx);
}

/*****************************************************************************/
/*                       mco_params::check_marker_segment                    */
/*****************************************************************************/

bool
  mco_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  c_idx = -1;
  return (code == KDU_MCO);
}

/*****************************************************************************/
/*                            mco_params::finalize                           */
/*****************************************************************************/

void
  mco_params::finalize(bool after_reading)
{
  if (after_reading)
    return;

  int mct_components = 0;
  kdu_params *siz = access_cluster(SIZ_params);
  if (siz != NULL)
    siz->get(Mcomponents,0,0,mct_components);

  int num_stages = 0;
  if (!get(Mnum_stages,0,0,num_stages))
    {
      if (mct_components > 0)
        set(Mnum_stages,0,0,num_stages=0);
          // Set default value of 0 stages where MCT is being used
    }
  else if (mct_components == 0)
    { KDU_ERROR(e,0x21070510); e <<
        KDU_TXT("You may not provide a value for the `Mnum_stages' attribute "
        "without also supplying a non-zero number of MCT output components "
        "via the `Mcomponents' attribute.");
    }
  int c_idx=0;
  if ((num_stages > 0) && !get(Mstages,num_stages-1,0,c_idx))
    { KDU_ERROR(e,0x21070511); e <<
        KDU_TXT("The number of records supplied for the `Mstages' attribute "
        "must match the value identified by `Mnum_stages'.");
    }
}

/*****************************************************************************/
/*                      mco_params::read_marker_segment                      */
/*****************************************************************************/

bool
  mco_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  bp = bytes;
  end = bp + num_bytes;
  try {
      int n, num_stages = kdu_read(bp,end,1);
      set(Mnum_stages,0,0,num_stages);
      for (n=0; n < num_stages; n++)
        {
          int idx = kdu_read(bp,end,1);
          set(Mstages,n,0,idx);
        }
      if (bp != end)
        { KDU_ERROR(e,0x21070512); e <<
            KDU_TXT("Malformed MCO marker segment encountered. The final ")
            << (int)(end-bp) <<
            KDU_TXT(" bytes were not consumed!");
        }
    } // End of try block.
  catch(kdu_byte *)
    { KDU_ERROR(e,0x21070513); e <<
        KDU_TXT("Malformed MCO marker segment encountered. "
        "Marker segment is too small.");
    }
  return true;
}

/*****************************************************************************/
/*                       mco_params::write_marker_segment                    */
/*****************************************************************************/

int
  mco_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  mco_params *ref = (mco_params *) last_marked;
  if ((inst_idx != 0) || (comp_idx >= 0))
    return 0;

  int n, idx=0, num_stages=0;
  if (!get(Mnum_stages,0,0,num_stages,false))
    return 0;

  if (num_stages > 255)
    { KDU_ERROR(e,0x21070514); e <<
        KDU_TXT("Cannot write MCO marker segment; `Mnum_stages' value "
        "exceeds the limit of 255.");
    }

  if (ref != NULL)
    { // See if we can skip marker segment generation.
      if (ref->compare(Mnum_stages,0,0,num_stages))
        { // So far so good.  Now let's check the stage indices themselves
          for (n=0; n < num_stages; n++)
            {
              if (!get(Mstages,n,0,idx))
                assert(0);
              if (!ref->compare(Mstages,n,0,idx))
                break;
            }
          if (n == num_stages)
            return 0; // Object identical to reference
        }
    }

  // We are now committed to writing (or simulating) a marker segment.
  int length = 4 + 1 + num_stages;
  if (out == NULL)
    return length;

  // If we get here, we are committed to actually writing the marker segment
  int acc_length = 0; // Accumulate actual length for verification purposes.
  acc_length += out->put(KDU_MCO);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_byte) num_stages);
  for (n=0; n < num_stages; n++)
    {
      get(Mstages,n,0,idx);
      acc_length += out->put((kdu_byte) idx);
    }
  assert(length == acc_length);
  return length;
}


/* ========================================================================= */
/*                                nlt_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          nlt_params::new_object                           */
/*****************************************************************************/

kdu_params *nlt_params::new_object()
{
  return new nlt_params(); 
}

/*****************************************************************************/
/*                       nlt_params::nlt_params (no args)                    */
/*****************************************************************************/

nlt_params::~nlt_params() {}
nlt_params::nlt_params()
  : kdu_params(NLT_params,true,true,false,false,false)
{
  define_attribute(NLType,
      "This attribute identifies the type of non-linear point transform "
      "to be applied.  In the case of NONE, no additional information is "
      "required.  The same is true for SMAG and UMAG.  "
      "The SMAG and UMAG options are intended for use in the compression "
      "of floating point data (including half floats) that have been "
      "cast to integers (bits have been re-interpreted as integers without "
      "any numerical processing).  This allows efficient compression of "
      "high dynamic range floating point data.  SMAG and UMAG can be "
      "used with any precision, but 16 would be common for the "
      "compression of half-floats.  The SMAG option is "
      "allowed only if the input and output precisions identified by "
      "`Sprecision'/`Mprecision' and `Nprecision' are identical and the "
      "`Ssigned'/`Msigned' and `Nsigned' attributes are both true; in this "
      "case, -ve values y are mapped to z = -2^{B-1} - y - 1, where B is "
      "the component bit-depth.  The UMAG option has the same constraints, "
      "but `Ssigned'/`Msigned' and `Nsigned' attributes must both be false; "
      "this means that true negative samples should never occur, so no "
      "transformation processing strictly needs to be performed, but "
      "numerical processing machinery needs to be especially careful not "
      "to allow the appearance of negative values that might otherwise "
      "appear as a result of quantization and numerical inaccuracies.  "
      "For the other two types, GAMMA and LUT, the description is not "
      "complete without additional information supplied via `NLTgamma' or "
      "`NLTlut' and `NLTdata' attributes, respectively.  During codestream "
      "generation, these auxiliary attributes can be derived from `NLTmake', "
      "which provides simpler methods for specifying useful non-linear "
      "transforms.  Moreover, the `NLType' value itself can usually be "
      "automatically filled in based on the presence of any of the NLT "
      "auxiliary attributes.",
      "(NONE=0,GAMMA=1,LUT=2,SMAG=3,UMAG=4)");
  define_attribute(NLTgamma,
     "This attribute provides the 5 r+ve real-valued parameters that define "
     "a gamma-based non-linear point transformation.  The parameters are "
     "identified as E (exponent), S (toe slope), T (toe threshold), "
     "A and B (continuity parameters).  In a well-defined gamma expression, "
     "these 5 parameters are not all independent, but they are separately "
     "recorded in the NLT marker segment.  The transform from the decoded "
     "output z of the NLT back to its input y is perhaps easiest to "
     "describe as y=S*z if |z|<=T/S, else y=sign(z) * (A*z^E - B).  In "
     "this description, both y and z are considered normalized so that "
     "the maximum nominal absolute value for both z and y is 1.0.  Actual "
     "values are stretched to fill the true nominal range.  The first "
     "obvious constraint that should be satisfied by a good gamma mapping "
     "is that A - B should be 1.0.  A second obvious constraint is that "
     "A*(T/S)^E should equal B+T -- this ensures continuity of the mapping.  "
     "There is one other condition that is satisfied by most gamma "
     "mappings; namely the condition that the mapping exhibit a continuous "
     "first derivative.  It can be seen that this requires S = A*E*T^{E-1}.  "
     "Note, however, that we do not enforce any of these conditions, since "
     "there might be some reason to intentionally use non-linear point "
     "transforms that do not satisfy them.  Instead, the `NLTmake' "
     "attribute provides a simple way to specify gamma functions that "
     "do satisfy all the conditions.  We conclude by noting that typical "
     "gamma functions involve E < 1 and T, B << 1, but all parameters are "
     "permitted to take values in the half-open interval [0,256).",
     "FFFFF");
  define_attribute(NLTlut,
     "This attribute serves to dimension the lookup table associated with "
     "a non-linear point transform having `NLType' = LUT.  "
     "The attribute takes a single record with four parameters.  The "
     "first two parameters are real-valued quantities Dmin and "
     "Dmax, while the last two parameters are integers N and P, "
     "respectively.  Here N >= 2 is the number of entries in the lookup "
     "table; P >= 1 is the number of bits used to represent the parameter "
     "values, meaning that they will be quantized to multiples of "
     "1 / (2^P - 1); 0 <= Dmin < Dmax <= 1.0 are the domain bounds, over "
     "which the entries to the lookup table are spaced uniformly.  "
     "These parameters describe the production of a decoded output value z, "
     "from its input value y, where y is the value produced at the output "
     "of the multi-component transform (or just the decoded codestream "
     "component, if there is no MCT) during decompression.  y values are "
     "first normalized to the interval [0,1] by level-shifting signed "
     "values and then scaling the values by 1 / (2^B-1), where B is the "
     "precision of the original y values.  y values are clipped to the "
     "range [Dmin,Dmax], after which an LUT index k is formed from "
     "k=floor(N*(y-Dmin)/(Dmax-Dmin)).   Then z is formed by starting with "
     "the interpolated value t_k+(N*(y-Dmin)/(Dmax-Dmin)-k)*(t_{k+1}-t_k), "
     "scaling it by (2^C-1), where C is the decoded output precision from "
     "`Nprecision' and finally level shifting it if `Nsigned' is true.  "
     "Here, the t_k values come from the separate `NLTdata' attribute.  "
     "Rather than specifying `NLTlut' and `NLTdata' attributes explicitly, "
     "it is usually much more convenient to generate them "
     "automatically via `NLTmake' instructions.",
     "FFII");
  define_attribute(NLTdata,
     "This attribute provides the N values of the lookup table, where "
     "N is the number of lookup table entries supplied via the "
     "`NLTlut' attribute.  All of the real-valued parameters here must "
     "lie in the range 0.0 to 1.0, since the lookup table is expressed "
     "in a normalized domain, relative to the total dynamic range of "
     "the output values, being from 0 to 2^C-1, where C is the "
     "precision of the decoded values ultimately produced by the " 
     "non-linear point transform, as identified via the `Nprecision' "
     "attribute.",
     "F",MULTI_RECORD);
  define_attribute(NLTmake,
     "This attribute provides a convenient way to construct a variety "
     "of useful non-linear point transforms, as an alternative to "
     "directly specifying `NLTgamma' and `NLTlut'/`NLTdata' parameters. "
     "Each record in the multi-record attribute describes a single "
     "gamma or log-like point transform or its inverse.  The complete "
     "transform from the linear output samples y produced by wavelet "
     "synthesis (and optionally multi-component transformation) to the "
     "non-linear transform output samples z produced by a decompressor, "
     "is formed by concatenating the transforms described by each "
     "successive record supplied here, in sequence.  Each record commences "
     "with a transform type instruction that is one of GAMMA, IGAMMA, LOG "
     "or ILOG, followed by two real-valued parameters P1 and P2, and finally "
     "an integer number of points N for LUT approximations of the operator.  "
     "For GAMMA transforms, P1 is the reciprocal exponent 1/E and P2 is "
     "the offset parameter B from the 5 parameters defined in "
     "connection with `NLTgamma'; the other 3 parameters are obtained by "
     "enforcing continuity constraints.  This is arguably the most "
     "natural way of specifying gamma functions; the sRGB gamma "
     "function, for example, takes parameters 2.4 and 0.055.  The IGAMMA "
     "instruction builds the inverse of the gamma transform that would "
     "result from the same parameters.  This means that a cascade of two "
     "records specifying GAMMA and then IGAMMA but using the same "
     "P1 and P2 parameters would result in the identity transform.  LOG "
     "and ILOG have the same inverse relationship, so we need only explain "
     "the LOG form.    The LOG option describes a log-like transformation "
     "from original image samples z to transformed samples y that are "
     "subjected to linear transformation and coding during compression.  "
     "The transformation that occurs during decompression, as described "
     "by the generated lookup table, is that exponential (i.e., the "
     "reverse transform).  The forward log transform z in [0,1] to "
     "y in [0,1] according to y = A*(z/B) if z <= B, else "
     "y = A*(1+log(z/B)), where P1 holds the B parameter (extent of the "
     "linear part of the mapping) and A = 1 / (1-log(B)).  The parameter "
     "P2 is not used for LOG and ILOG forms.    Both LOG/ILOG and "
     "GAMMA/IGAMMA functions build transforms that work as expected on "
     "both signed and unsigned data, processing positive values in the "
     "manner described above, and processing negative values in an "
     "anti-symmetric manner.  As mentioned, the last "
     "parameter in each record holds the number of entries N used for "
     "lookup table approximations of the transform.  Lookup tables are "
     "required for anything other than a single record of type GAMMA, "
     "except perhaps with degenerate parameter values.  Where there are "
     "multiple records, the largest specified N value is the one that "
     "is used.","(GAMMA=0,IGAMMA=1,LOG=2,ILOG=3)FFI",MULTI_RECORD);
}

/*****************************************************************************/
/*                       nlt_params::copy_with_xforms                        */
/*****************************************************************************/

void
  nlt_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int tval=0;
  if (source->get(NLType,0,0,tval,false))
    set(NLType,0,0,tval);
  float gamma_params[5];
  if (source->get(NLTgamma,0,0,gamma_params[0],false) &&
      source->get(NLTgamma,0,1,gamma_params[1],false) &&
      source->get(NLTgamma,0,2,gamma_params[2],false) &&
      source->get(NLTgamma,0,3,gamma_params[3],false) &&
      source->get(NLTgamma,0,4,gamma_params[4],false))
    { 
      set(NLTgamma,0,0,gamma_params[0]);
      set(NLTgamma,0,1,gamma_params[1]);
      set(NLTgamma,0,2,gamma_params[2]);
      set(NLTgamma,0,3,gamma_params[3]);
      set(NLTgamma,0,4,gamma_params[4]);
    }
  float dmin=0, dmax=0;
  int lut_N=0, lut_P=0;
  if (source->get(NLTlut,0,0,dmin,false) &&
      source->get(NLTlut,0,1,dmax,false) &&
      source->get(NLTlut,0,2,lut_N,false) &&
      source->get(NLTlut,0,3,lut_P,false))
    { 
      set(NLTlut,0,0,dmin);
      set(NLTlut,0,1,dmax);
      set(NLTlut,0,2,lut_N);
      set(NLTlut,0,3,lut_P);      
      float fval=0.0f;
      for (int n=0; (n < lut_N) && get(NLTdata,n,0,fval,false); n++)
        set(NLTdata,n,0,fval);
    }
}

/*****************************************************************************/
/*                     nlt_params::write_marker_segment                      */
/*****************************************************************************/

int
  nlt_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  nlt_params *ref = (nlt_params *) last_marked;
  int nl_type=0;
  if (!get(NLType,0,0,nl_type))
    return 0; // Nothing to write
  
  if ((nl_type < NLType_NONE) || (nl_type > NLType_UMAG))
    { KDU_ERROR_DEV(e,0x10011601); e <<
      KDU_TXT("Unable to write NLT marker segment yet.  NLType value "
              "not in valid range.");
    }
  float gamma[5]={0,0,0,0,0};
  float fval, dmin=0.0f, dmax=0.0f;
  int n, lut_N=0, lut_P=0, lut_entry_bytes=0;
  if (nl_type == NLType_GAMMA)
    { 
      for (n=0; n < 5; n++)
      if (!(get(NLTgamma,0,n,gamma[n]) &&
            (gamma[n] >= 0.0f) && (gamma[n] <= 256.0f)))
        { KDU_ERROR_DEV(e,0x10011602); e <<
          KDU_TXT("Unable to write NLT marker segment yet.  NLTgamma "
                  "parameters missing or not in the valid range of 0.0 "
                  "to 256.0.");
        }
    }
  else if (nl_type == NLType_LUT)
    { 
      if (!(get(NLTlut,0,0,dmin) && get(NLTlut,0,1,dmax) &&
            get(NLTlut,0,2,lut_N) && get(NLTlut,0,3,lut_P) &&
            (dmin >= 0.0f) && (dmax > dmin) && (dmax <= 1.0f) &&
            (lut_N >= 2) && (lut_N <= 8192) && (lut_P >= 1) && (lut_P <= 32)))
        { KDU_ERROR_DEV(e,0x10011603); e <<
          KDU_TXT("Unable to write NLT marker segment yet.  NLTlut "
                  "parameters missing or invalid: need 0 <= Dmin < Dmax <= 1 "
                  "and 2 <= N <= 8192 and 1 <= P <= 32.");
        }
      lut_entry_bytes = 1;
      if (lut_P > 8)
        lut_entry_bytes = (lut_P > 16)?4:2;
      if (!get(NLTdata,lut_N-1,0,fval))
        { KDU_ERROR_DEV(e,0x10011604); e <<
          KDU_TXT("Unable to write NLT marker segment yet.  NLTdata "
                  "parameters missing -- you must supply at least as "
                  "many parameters as is identified via the NLTlut "
                  "attribute.");
        }
    }
  
  if ((ref != NULL) && ref->compare(NLType,0,0,nl_type))
    { // See if we can skip marker segment generation
      if (nl_type == NLType_GAMMA)
        { 
          if (ref->compare(NLTgamma,0,0,gamma[0]) &&
              ref->compare(NLTgamma,0,1,gamma[1]) &&
              ref->compare(NLTgamma,0,2,gamma[2]) &&
              ref->compare(NLTgamma,0,3,gamma[3]) &&
              ref->compare(NLTgamma,0,4,gamma[4]))
            return 0; // No marker segment needed -- type + gamma data agree
        }
      else if (nl_type == NLType_LUT)
        { 
          if (ref->compare(NLTlut,0,0,dmin) &&
              ref->compare(NLTlut,0,1,dmax) &&
              ref->compare(NLTlut,0,2,lut_N) &&
              ref->compare(NLTlut,0,3,lut_P))
            { // Just need to check the LUT data itself now
              for (n=0; n < lut_N; n++)
                if (!(get(NLTdata,n,0,fval) && ref->compare(NLTdata,n,0,fval)))
                  break;
              if (n == lut_N)
                return 0; // No marker segment needed -- type + lut data agree
            }
        }
      else
        return 0; // No marker segment needed -- type already agrees + no data.
    }
  
  // We are now committed to writing (or simulating) a marker segment.
  int length = 8;
  if (nl_type == NLType_GAMMA)
    length += 15;
  else if (nl_type == NLType_LUT)
    length += 11 + lut_N * lut_entry_bytes;
  if (out == NULL)
    return length;
  
  // We are now committed to actually writing the marker; start by finding
  // the component precision/signed information.
  int prec_val=0, is_signed=0;
  int c_ref = (comp_idx < 0)?0:comp_idx;
  kdu_params *siz = access_cluster(SIZ_params);
  if (!(siz->get(Nprecision,c_ref,0,prec_val) &&
        siz->get(Nsigned,c_ref,0,is_signed)))
    { KDU_ERROR_DEV(e,0x10011605); e <<
      KDU_TXT("Unable to write NLT marker segment yet.  `Nprecision' and/or "
              "`Nsigned' attributes for the relevant component are not "
              "yet available.");
    }
  if ((prec_val < 1) || (prec_val > 38))
    { KDU_ERROR_DEV(e,0x10011606); e <<
      KDU_TXT("Illegal precision for NLT marker generation, ")
      << prec_val <<
      KDU_TXT("found in `Nprecision' attribute. Legal range is from 1 to 38 "
              "bits per sample.");
    }
  if (nl_type == NLType_SMAG)
    { 
      if (!is_signed)
        { KDU_ERROR_DEV(e,0x15011601); e <<
          KDU_TXT("Invalid use of the SMAG non-linear point transform "
                  "type in the `NLType' attribute; use this type with "
                  "signed sample data only (`Nsigned'=true).  Use UMAG "
                  "with unsigned sample data.");
        }
    }
  else if (nl_type == NLType_UMAG)
    { 
      if (is_signed)
        { KDU_ERROR_DEV(e,0x15011602); e <<
          KDU_TXT("Invalid use of the UMAG non-linear point transform "
                  "type in the `NLType' attribute; use this type with "
                  "unsigned sample data only (`Nsigned'=false).  Use SMAG "
                  "with signed sample data.");
        }
      nl_type = NLType_SMAG; // Write as SMAG in the NLT marker seg itself
    }
    
  int acc_length = 0;
  acc_length += out->put(KDU_NLT);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_uint16)((comp_idx<0)?0xFFFF:comp_idx));
  acc_length += out->put((kdu_byte)(prec_val-1 + (is_signed<<7)));
  acc_length += out->put((kdu_byte)nl_type);
  if (nl_type == NLType_GAMMA)
    { // Write the gamma parameters
      for (n=0; n < 5; n++)
        { 
          kdu_int32 fix_val = (kdu_int32)(gamma[n]*(float)(1<<16)+0.5f);
          if (fix_val & 0xFF000000)
            fix_val = (fix_val < 0)?0:0x00FFFFFF;
          acc_length += out->put((kdu_byte)(fix_val>>16)); // 8 MSBs
          acc_length += out->put((kdu_uint16) fix_val); // 16 LSBs
        }
    }
  else if (nl_type == NLType_LUT)
    { // Write the LUT parameters
      acc_length += out->put((kdu_uint16)(lut_N-1));
      kdu_uint32 fix_dmin, fix_dmax;      
      if (dmax >= 1.0f)
        fix_dmax = 0xFFFFFFFF;
      else
        fix_dmax = (kdu_uint32)(dmax * (float)((kdu_uint32)0xFFFFFFFF) + 0.5f);
      if (dmin >= 1.0f)
        fix_dmin = 0xFFFFFFFF;
      else
        fix_dmin = (kdu_uint32)(dmin * (float)((kdu_uint32)0xFFFFFFFF) + 0.5f);
      if ((fix_dmin >= fix_dmax) && (fix_dmin < 0xFFFFFFFF))
        fix_dmax = fix_dmin+1;
      if ((fix_dmin >= fix_dmax) && (fix_dmax > 0))
        fix_dmin = fix_dmax-1;
      acc_length += out->put(fix_dmin);
      acc_length += out->put(fix_dmax);
      acc_length += out->put((kdu_byte)lut_P);
      kdu_uint32 mask = 0xFFFFFFFF;  mask = ~(mask << lut_P);
      for (n=0; n < lut_N; n++)
        { 
          if (!get(NLTdata,n,0,fval))
            { KDU_ERROR_DEV(e,0x10011607); e <<
              KDU_TXT("Unable to access all `NLTdata' parameters required "
                      "to write lookup-based NLT marker segment.");
            }
          kdu_uint32 fix_val;
          fval = floorf(0.5f + fval * (float) mask);
          if (fval < 0.0f)
            fix_val = 0;
          else if (fval >= (float) mask)
            fix_val = mask;
          else
            fix_val = (kdu_uint32) fval;
          if (lut_entry_bytes == 1)
            acc_length += out->put((kdu_byte) fix_val);
          else if (lut_entry_bytes == 2)
            acc_length += out->put((kdu_uint16) fix_val);
          else
            acc_length += out->put((kdu_uint32)fix_val);
        }
    }
  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                       nlt_params::check_marker_segment                    */
/*****************************************************************************/

bool
  nlt_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_NLT) || (num_bytes < 2))
    return false;
  kdu_uint16 Cnlt = bytes[0];
  Cnlt = (Cnlt<<8) + (kdu_uint16) bytes[1];
  if (Cnlt == 0xFFFF)
    { // This is a global NLT marker segment
      c_idx = -1;
      return true;
    }
  else if (Cnlt < (kdu_uint16)num_comps)
    { // This is a component-specific NLT marker segment 
      c_idx = Cnlt;
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                        nlt_params::read_marker_segment                    */
/*****************************************************************************/

bool
  nlt_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;
  
  if (tpart_idx != 0)
    return false;
  bp = bytes;
  end = bp + num_bytes;
  
  try { 
    int Cnlt = kdu_read(bp,end,2);
    if (Cnlt == 0xFFFF)
      Cnlt = -1;
    if (Cnlt != comp_idx)
      return false;
    int prec_val = kdu_read(bp,end,1);
    bool sign_val = ((prec_val & 0x80) != 0);
    prec_val = (prec_val & 0x7F) + 1;
    if (prec_val > 38)
      { KDU_ERROR(e,0x10011608); e <<
        KDU_TXT("Invalid bit-depth value found in NLT marker segment.  "
                "Bit-depths must lie in the range 1 to 38.");
      }
    kdu_params *siz = access_cluster(SIZ_params);
    int extensions = 0;
    if (!(siz->get(Sextensions,0,0,extensions) &&
          (extensions & Sextensions_NLT)))
      { KDU_WARNING(w,0x10011601); w <<
        KDU_TXT("NLT marker segment found in code-stream that does not "
                "advertise the presence of non-linear point transform "
                "features via the relevant extensions flag in the Rsiz "
                "member of the SIZ marker segment.  NLT specifications will "
                "be ignored.");
        return false;
      }
    int c_min = (comp_idx < 0)?0:comp_idx;
    int c_max = (comp_idx < 0)?(num_comps-1):comp_idx;
    bool warning_generated=false;
    if (tile_idx < 0)
      { // Main header: need to configure the Nsigned/Nprecision vals
        for (int c=c_min; c <= c_max; c++)
          { 
            siz->set(Nprecision,c,0,prec_val);
            siz->set(Nsigned,c,0,sign_val);
          }
      }
    else
      { // Cannot alter Nsigned/Nprecision; just check compatibility
        int existing_prec=0;
        bool existing_signed=false;
        for (int c=c_min; c <= c_max; c++)
          if (!(siz->get(Nprecision,c,0,existing_prec) &&
                siz->get(Nsigned,c,0,existing_signed) &&
                ((existing_prec != prec_val) ||
                 (existing_signed != sign_val)) && !warning_generated))
            { KDU_WARNING(w,0x10011602); w <<
              KDU_TXT("Tile-specific component bit-depth/signed "
                      "specification encountered in NLT marker segment is "
                      "not compatibile with the bith-depth/signed information "
                      "deduced from the main codestream header -- this is "
                      "not strictly illegal, but would clearly break most "
                      "applications.  Forcing consistency, which may alter "
                      "the decoded result.");
              warning_generated = true;
            }
      }
    
    int nl_type = kdu_read(bp,end,1);
    if (nl_type == NLType_SMAG)
      { 
        if (!sign_val)
          nl_type = NLType_UMAG;
      }
    else if ((nl_type != NLType_NONE) && (nl_type != NLType_GAMMA) &&
             (nl_type != NLType_LUT))
      { KDU_ERROR(e,0x15011603); e <<
        KDU_TXT("Malformed NLT marker segment encountered; transform "
                "type code must be in the range 0 to 3.");
      }
    set(NLType,0,0,nl_type);
    if (nl_type == NLType_GAMMA)
      { 
        float gamma[5]={0,0,0,0,0};
        for (int g=0; g < 5; g++)
          { 
            int int_part = kdu_read(bp,end,1);
            int frac_part = kdu_read(bp,end,2);
            gamma[g] = int_part + (((float) frac_part)*(1.0f/(float)0xFFFF));
            set(NLTgamma,0,g,gamma[g]);
          }
      }
    else if (nl_type == NLType_LUT)
      { 
        int lut_N = kdu_read(bp,end,2) + 1;
        kdu_uint32 fix_dmin = (kdu_uint32)kdu_read(bp,end,4);
        kdu_uint32 fix_dmax = (kdu_uint32)kdu_read(bp,end,4);
        int lut_P = kdu_read(bp,end,1);
        if ((lut_N < 1) || (lut_N > 8182) || (lut_P < 1) || (lut_P > 32))
          { KDU_ERROR(e,0x10011609); e <<
            KDU_TXT("Malformed NLT marker segment encountered.  "
                    "Lookup table parameters outside legal range!");
          }
        float dmin=0, dmax=0;
        if (fix_dmax == 0xFFFFFFFF)
          dmax = 1.0f;
        else
          dmax = ((float) fix_dmax) / ((float)((kdu_uint32) 0xFFFFFFFF));
        dmin = ((float) fix_dmin) / ((float)((kdu_uint32) 0xFFFFFFFF));
        if (dmin >= dmax)
          { // Make tiny adjustments to avoid possibility of divide-by-0
            dmax += 1.0f / (float)(1<<22); // Fits inside the 23bit FP mantissa
            if (dmax > 1.0f)
              { dmin -= (dmax-1.0f); dmax = 1.0f; }
          }
        set(NLTlut,0,0,dmin);
        set(NLTlut,0,1,dmax);
        set(NLTlut,0,2,lut_N);
        set(NLTlut,0,3,lut_P);
        kdu_uint32 uval, mask = 0xFFFFFFFF;  mask = ~(mask << lut_P);
        float fval, scale = 1.0f / (float) mask;
        for (int n=0; n < lut_N; n++)
          { // Read the lookup table values
            if (lut_P <= 8)
              uval = (kdu_uint32) kdu_read(bp,end,1);
            else if (lut_P <= 16)
              uval = (kdu_uint32) kdu_read(bp,end,2);
            else
              uval = (kdu_uint32) kdu_read(bp,end,4);
            uval &= mask;
            fval = (uval == mask)?1.0f:(scale*(float)uval);
            set(NLTdata,n,0,fval);
          }
      }
  } catch (kdu_byte *) { 
    KDU_ERROR(e,0x10011610); e <<
    KDU_TXT("Malformed NLT marker segment encountered.  Marker segment "
            "is too small.");
  }
  
  return true;
}

/*****************************************************************************/
/*                             nlt_params::finalize                          */
/*****************************************************************************/

void
  nlt_params::finalize(bool after_reading)
{
  if (after_reading)
    return;
  
  // Figure out what the `nl_type' is, setting it automatically if appropriate.
  int make_opt=0;
  int nl_type=-1; // Means not known yet
  if (!get(NLType,0,0,nl_type,false,false,false))
    { // Type is not specified explicitly for this tile-component
      float fval=0.0f;
      if (get(NLTgamma,0,0,fval,false,false,false))
        set(NLType,0,0,nl_type=NLType_GAMMA);
      else if (get(NLTlut,0,0,fval,false,false,false))
        set(NLType,0,0,nl_type=NLType_LUT);
      else if (get(NLTmake,0,0,make_opt,false,false,false))
        { 
          if ((make_opt == NLTmake_GAMMA) && !get(NLTmake,1,0,make_opt))
            nl_type = NLType_GAMMA; // Single make instruction makes gamma
          else
            nl_type = NLType_LUT; // Multiple or non-gamma make instructions
          set(NLType,0,0,nl_type);
        }
      else
        return; // Nothing to finalize
    }
  if ((nl_type < NLType_NONE) || (nl_type > NLType_UMAG))
    { KDU_ERROR(e,0x11011601); e <<
      KDU_TXT("`NLType' attribute must be one of NONE=0, GAMMA=1, LUT=2, "
              "SMAG=3 or UMAG=4.");
    }

  kdu_params *siz = access_cluster(SIZ_params);
  int extensions=0, nlt_precision=0, c_test=(comp_idx<0)?0:comp_idx;
  bool nlt_signed=false;
  if (!(siz->get(Sextensions,0,0,extensions) &&
        (extensions & Sextensions_NLT) &&
        siz->get(Nprecision,c_test,0,nlt_precision) &&
        siz->get(Nsigned,c_test,0,nlt_signed) &&
        (nlt_precision >= 1) && (nlt_precision <= 38)))
    { KDU_ERROR(e,0x10011611); e <<
      KDU_TXT("NLT attributes `NLTtype', `NLTgamma', `NLTlut' and `NLTdata' "
              "can be used only if `Sextensions' contains the NLT flag and "
              "the relevant output component(s) have defined `Nprecision' "
              "and `Nsigned' parameter attributes.");
    }
  int linear_precision=0;
  bool linear_signed=false;
  if (!(siz->get(Mprecision,c_test,0,linear_precision) &&
        siz->get(Msigned,c_test,0,linear_signed)))
    { 
      siz->get(Sprecision,c_test,0,linear_precision);
      siz->get(Ssigned,c_test,0,linear_signed);
    }
  
  if (get(NLTmake,0,0,make_opt,false,false,false))
    { // Convert the make instructions into gamma/lut/data specifications 
      float p1=0, p2=0;
      int r, n, lut_N=0;
      if (!((make_opt >= 0) && (make_opt <= 3) &&
            get(NLTmake,0,1,p1,false,false,false) &&
            get(NLTmake,0,2,p2,false,false,false) &&
            get(NLTmake,0,3,lut_N,false,false,false)))
        { KDU_ERROR(e,0x11011602); e <<
          KDU_TXT("First record of `NLTmake' attribute has missing "
                  "parameters P1, P2 or N or an invalid operation: must be "
                  "one of GAMMA, IGAMMA, LOG or ILOG.");
        }
      if ((make_opt == NLTmake_GAMMA) &&
          !get(NLTmake,1,0,n,false,false,false))
        { // Build gamma transform
          if (nl_type != NLType_GAMMA)
            set(NLType,0,0,nl_type=NLType_GAMMA);
          float gamma[5]={0,0,0,0,0};
          make_gamma_params(p1,p2,gamma);
          for (n=0; n < 5; n++)
            set(NLTgamma,0,n,gamma[n]);
        }
      else
        { // Create an LUT approximation
          for (r=1; get(NLTmake,r,3,n,false,false,false); r++)
            if (n > lut_N)
              lut_N = n;
          if (lut_N > 8192)
            lut_N = 8192; // Max entries allowed
          if (nl_type != NLType_LUT)
            set(NLType,0,0,nl_type=NLType_LUT);
          set(NLTlut,0,0,0.0f); // Dmin=0.0
          set(NLTlut,0,1,1.0f); // Dmax=1.0
          set(NLTlut,0,2,lut_N); // N = lut_N
          set(NLTlut,0,3,16); // 16-bit precision should be plenty
          float y, gap, *lut = new float[lut_N];
          if (linear_signed)
            { // Initialize the LUT with values from -1.0 to 1.0-delta
              float delta = powf(2.0f,(float)(-linear_precision));
              gap = 2.0f * (1.0f - delta) / (lut_N-1);
              for (y=-1.0f, n=0; n < lut_N; n++, y+=gap)
                lut[n] = y;
            }
          else
            { // Initialize the LUT with values from 0.0 to (exactly) 1.0
              gap = 1.0f / (lut_N-1);
              for (y=0.0f, n=0; n < lut_N; n++, y+=gap)
                lut[n] = y;
            }
          r = 0;
          do {
            if (make_opt == NLTmake_GAMMA)
              { 
                float gamma[5]={0,0,0,0,0};
                make_gamma_params(p1,p2,gamma);
                apply_rev_gamma(lut,lut_N,gamma);
              }
            else if (make_opt == NLTmake_IGAMMA)
              { 
                float gamma[5]={0,0,0,0,0};
                make_gamma_params(p1,p2,gamma);
                apply_fwd_gamma(lut,lut_N,gamma);
              }
            else if (make_opt == NLTmake_LOG)
              apply_rev_log(lut,lut_N,p1);
            else
              apply_fwd_log(lut,lut_N,p1);
            r++; // Advance to the next record, if there is one
          } while (get(NLTmake,r,0,make_opt,false,false,false) &&
                   get(NLTmake,r,1,p1,false,false,false) &&
                   get(NLTmake,r,2,p2,false,false,false) &&
                   get(NLTmake,r,3,n,false,false,false) &&
                   (make_opt >= 0) && (make_opt <= 3));
          
          if (nlt_signed)
            { // Signed outputs: map -1.0 to 0.0 and 1.0-delta to 1.0
              float delta = 1.0f / powf(2.0f,(float)nlt_precision);
              float scale = 1.0f / (2.0f - delta);
              for (n=0; n < lut_N; n++)
                { 
                  float tval = lut[n];
                  tval = (tval + 1.0f)*scale;
                  tval = (tval < 0.0f)?0.0f:tval;
                  tval = (tval > 1.0f)?1.0f:tval;
                  set(NLTdata,n,0,tval);
                }
            }
          else
            { // Unsigned output: take absolute values
              for (n=0; n < lut_N; n++)
                { 
                  float tval = fabsf(lut[n]);                  
                  tval = (tval > 1.0f)?1.0f:tval;
                  set(NLTdata,n,0,tval);
                }
            }
          delete[] lut;
        }
    }
  else if (nl_type == NLType_GAMMA)
    { 
      float gamma[5];
      for (int g=0; g < 5; g++)
        if (!(get(NLTgamma,0,g,gamma[g]) &&
              (gamma[g] >= 0.0f) && (gamma[g] < 256.0f)))
          { KDU_ERROR_DEV(e,0x10011613); e <<
            KDU_TXT("`NLTgamma' attribute missing or has invalid parameters, "
                    "where `NLType' identifies a GAMMA mapping.  Valid "
                    "`NLTgamma' parameters must lie in the half-open interval "
                    "[0,256).");
          }
    }
  else if (nl_type == NLType_LUT)
    { 
      float fval=0.0f, dmin=0.0f, dmax=0.0f;
      int lut_N=0, lut_P=0;
      if (!(get(NLTlut,0,0,dmin) && get(NLTlut,0,0,dmax) &&
            get(NLTlut,0,2,lut_N) && get(NLTlut,0,3,lut_P) &&
            (dmin >= 0.0f) && (dmax > dmin) && (dmax <= 1.0f) &&
            (lut_N >= 2) && (lut_N <= 8192) && (lut_P >= 1) && (lut_P <= 32)))
        { KDU_ERROR_DEV(e,0x10011614); e <<
          KDU_TXT("`NLTlut' attribute missing or has invalid parameters, "
                  "where `NLType' identifies an LUT mapping.  Valid "
                  "`NLTlut' parameters have 0 <= dmin,dmax <= 1, "
                  "2 <= N <= 8192 and 1 <= P <= 32.");
        }
      for (int n=0; n < lut_N; n++)
        if (!(get(NLTdata,n,0,fval) && (fval >= 0.0f) && (fval <= 1.0f)))
        { KDU_ERROR_DEV(e,0x10011615); e <<
          KDU_TXT("`NLTdata' attribute missing or has invalid parameters, "
                  "where `NLType' identifies an LUT mapping.  All data "
                  "values must lie in the range 0.0 to 1.0.");
        }
    }
  else if (nl_type == NLType_SMAG)
    { 
      if ((linear_precision != nlt_precision) ||
          !(linear_signed && nlt_signed))
        { KDU_ERROR(e,0x11011603); e <<
          KDU_TXT("The non-linear point transform type `NLType' SMAG=3 can "
                  "only be used in contexts where the input and output "
                  "samples have identical precision and both represent "
                  "signed data -- use UMAG for unsigned data.");
        }
    }
  else if (nl_type == NLType_UMAG)
    { 
      if ((linear_precision != nlt_precision) || linear_signed || nlt_signed)
        { KDU_ERROR(e,0x15011604); e <<
          KDU_TXT("The non-linear point transform type `NLType' UMAG=4 can "
                  "only be used in contexts where the input and output "
                  "samples have identical precision and both represent "
                  "unsigned data -- use SMAG for signed data.");
        }      
    }
}

/*****************************************************************************/
/* STATIC               nlt_params::make_gamma_params                        */
/*****************************************************************************/

void nlt_params::make_gamma_params(float p1, float p2, float gamma[])
{
  float Eval, Sval, Tval, Aval, Bval;
  if (p1 < 1.0f)
    Eval = 1.0f; // Else we would need Bval to be -ve (not supported)
  else
    { 
      Eval = 1.0f / p1;
      if (Eval < 0.01f) Eval = 0.01f; // Don't let it get too silly
    }
  if (p2 < 0.0f)
    Bval = 0.0f; // Cannot represent negative values
  else if (p2 > 254.0f)
    Bval = 254.0f; // Else B and A cannot both be represented
  else
    Bval = p2;
  Aval = 1.0f + Bval;
  if (Eval == 1.0f)
    Sval = Tval = 1.0f; // Just a straight line!
  else if (Bval == 0.0f)
    { Sval = 1.0f; Tval = 0.0f; }
  else
    { 
      float eps = powf(Bval / (Aval*(1.0f-Eval)), 1.0f/Eval); // = T/S
      if (eps > 1.0f) eps = 1.0f; // Can't transition beyond 1.0
      Sval = Bval * Eval / (eps * (1.0f - Eval));
      if (Sval > 255.0f)
        Sval = 255.0f;
      Tval = eps * Sval;
    }
  gamma[0] = Eval;  gamma[1] = Sval;  gamma[2] = Tval;
  gamma[3] = Aval;  gamma[4] = Bval;
}

/*****************************************************************************/
/* STATIC                 nlt_params::apply_fwd_gamma                        */
/*****************************************************************************/

void nlt_params::apply_fwd_gamma(float lut[], int num_entries, float gamma[])
{
  float Eval=gamma[0], Sval=gamma[1], Tval=gamma[2];
  float Aval=gamma[3], Bval=gamma[4];
  assert(Sval > 0.0f); // Guaranteed by `make_gamma_params'.
  float eps=Tval/Sval;
  for (int n=0; n < num_entries; n++)
    { 
      float z = lut[n];
      if (z < -eps)
        lut[n] = Bval - Aval*powf(-z,Eval);
      else if (z <= eps)
        lut[n] = Sval*z;
      else
        lut[n] = Aval*powf(z,Eval) - Bval;
    }
}

/*****************************************************************************/
/* STATIC                 nlt_params::apply_rev_gamma                        */
/*****************************************************************************/

void nlt_params::apply_rev_gamma(float lut[], int num_entries, float gamma[])
{
  float Eval=gamma[0], Sval=gamma[1], Tval=gamma[2];
  float Aval=gamma[3], Bval=gamma[4];
  assert(Sval > 0.0f); // Guaranteed by `make_gamma_params'.
  float Sinv = 1.0f / Sval;
  assert(Eval > 0.0f); // Also guaranteed by `make_gamma_params'.
  float Einv = 1.0f / Eval;
  assert(Aval > 0.0f); // Also guaranteed by `make_gamma_params'.
  float Ainv = 1.0f / Aval;
  for (int n=0; n < num_entries; n++)
    { 
      float y = lut[n];
      if (y < -Tval)
        lut[n] = -powf((Bval-y)*Ainv,Einv);
      else if (y <= Tval)
        lut[n] = Sinv*y;
      else
        lut[n] = powf((Bval+y)*Ainv,Einv);
    }
}

/*****************************************************************************/
/* STATIC                  nlt_params::apply_fwd_log                         */
/*****************************************************************************/

void nlt_params::apply_fwd_log(float lut[], int num_entries, float Bval)
{
  if (Bval < (1.0f / (1<<16)))
    Bval = 1.0f / (1<<16); // Ridiculously small linear zone!
  else if (Bval > 1.0f)
    Bval = 1.0f;
  float Aval = 1.0f / (1.0f - logf(Bval));
  float Binv = 1.0f / Bval;
  for (int n=0; n < num_entries; n++)
    { 
      float y, z = lut[n] * Binv;
      if (z < -1.0f)
        y = -(1.0f + log(-z));
      else if (z <= 1.0f)
        y = z;
      else
        y = (1.0f + log(z));
      lut[n] = Aval * y;
    }
}

/*****************************************************************************/
/* STATIC                  nlt_params::apply_rev_log                         */
/*****************************************************************************/

void nlt_params::apply_rev_log(float lut[], int num_entries, float Bval)
{
  if (Bval < (1.0f / (1<<16)))
    Bval = 1.0f / (1<<16); // Ridiculously small linear zone!
  else if (Bval > 1.0f)
    Bval = 1.0f;
  float Aval = 1.0f / (1.0f - logf(Bval));
  float Ainv = 1.0f / Aval;
  for (int n=0; n < num_entries; n++)
    { 
      float z, y = lut[n] * Ainv;
      if (y < -1.0f)
        z = exp(-y-1.0f);
      else if (y <= 1.0f)
        z = y;
      else
        z = exp(y-1.0f);
      lut[n] = Bval * z;
    }  
}


/* ========================================================================= */
/*                                atk_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          atk_params::new_object                           */
/*****************************************************************************/

kdu_params *atk_params::new_object()
{
  return new atk_params(); 
}

/*****************************************************************************/
/*                       atk_params::atk_params (no args)                    */
/*****************************************************************************/

atk_params::~atk_params() {}
atk_params::atk_params()
  : kdu_params(ATK_params,true,false,true,false,true)
{
  define_attribute(Kkernels,
     "This attribute is provided as a convenient way to generate ATK "
     "(arbitrary transform kernel) specifications based on pre-defined "
     "templates.  The set of pre-defined templates may grow in the future, "
     "but is currently limited to the following options: R1X1 and I1X1 are "
     "reversible and irreversible versions of the \"lazy wavelet\", meaning "
     "no transform at all; I2X2 means the irreversible Haar transform; R2X2 "
     "means the reversible Haar transform; and I5X3 means the irreversible "
     "LeGall 5/3 transform, which is the irreversible version of the default "
     "Part-1 reversible 5/3 transform.  The `Kkernels' attribute itself is "
     "not recorded in the codestream, but is used to configure `Kreversible', "
     "`Ksymmetric', `Kextension', `Ksteps' and `Kcoeffs' attributes "
     "associated with the same parameter instance (the \":I\" suffix, as in "
     "\"Kkernels:I2=I5X3\").  The lazy wavelets (R1X1 and I1X1) pass image "
     "samples directly through to the subbands (after de-interleaving) to be "
     "either coded directly (reversible) or quantized and then coded "
     "(irreversible).  These can be useful for experimental applications, "
     "where a transform of interest is performed by a separate application "
     "and the interleaved subbands are supplied directly to Kakadu for "
     "compression, inverting the process during decompression.",
     "(I1X1=0,R1X1=1,I2X2=2,R2X2=3,I5X3=4)");
  
  define_attribute(Kreversible,
     "This attribute determines how the `Ksteps' and `Kcoeffs' attributes "
     "should be treated.  In the end, this parameter attribute must agree "
     "with the value of the `Creversible' attribute, for any tile-component "
     "which uses this transformation kernel.  However this consistency "
     "may be created by specifying `Kreversible' and leaving `Creversible' "
     "unspecified, so that an appropriate default value will be selected.\n"
     "\t\t[A default value cannot be created automatically, so you must "
     "explicitly specify a value if you want ATK information to become "
     "available for any particular instance index in the main header "
     "or a tile header.]",
     "B");

  define_attribute(Ksymmetric,
     "If true, the transform kernel belongs to the whole-sample "
     "symmetric class, which is treated specially by JPEG2000 Part-2.  "
     "The translated impulse responses of these kernels are all "
     "symmetric about 0 -- see the Taubman & Marcellin book, Chapter 6, "
     "for a definition of translated impulse responses.  Equivalently, "
     "all lifting steps involve even-length symmetric lifting coefficients, "
     "Cs[n], where the coefficients associated with steps s=0, 2, 4, ... "
     "are symmetric about n = 1/2 and the coefficients associated with "
     "steps s=1, 3, 5, ... are symmetric about n = -1/2.\n"
     "\t\t[If you do not explicitly specify this attribute, it will be "
     "determined automatically from the lifting coefficient values "
     "supplied via `Krev_coeffs' or `Kirv_coeffs', as appropriate.]",
     "B");

  define_attribute(Kextension,
     "Identifies the boundary extension method to be applied in each "
     "lifting step.  If `CON', boundary samples are simply replicated.  "
     "The other possible value, `SYM', means that boundary samples "
     "are symmetrically extended.  The centre of symmetry in this case "
     "is the boundary sample location within an interleaved representation "
     "in which low-pass samples occupy the even indexed locations and "
     "high-pass samples occupy the odd indexed locations.  The `SYM' "
     "method must be used if `Ksymmetric' is true.  Conversely, for "
     "filters which do not have the whole-sample symmetric property, "
     "you are strongly recommended to use the `CON' extension method.\n"
     "\t\t[Defaults to `SYM' if the filters are whole-sample symmetric "
     "and `CON' otherwise.]",
     "(CON=0,SYM=1)");

  define_attribute(Ksteps,
     "Array with one entry for each lifting step.  The first entry "
     "corrsponds to lifting step s=0, which updates odd indexed samples, "
     "based on even indexed samples; the second entry corresponds to "
     "lifting step s=1, which updates even indexed samples, based on "
     "odd indexed samples; and so forth.  The first field in each record "
     "holds the length, Ls, of the coefficient array Cs[n], for the "
     "relevant step s.  The second field is the location of the first "
     "entry, Ns, where Cs[n] is defined for n=Ns to Ns+Ls-1.  The value "
     "of Ns is typically negative, but need not be.  For symmetric "
     "kernels, Ls must be even and Ns must satisfy Ns=-floor((Ls+p-1)/2), "
     "where p is the lifting step parity (0 if s is even, 1 if s is odd).  "
     "The third and fourth fields must both be 0 if `Kreversible' is "
     "false.  For reversible transform kernels, however, the third "
     "field holds the downshift value, Ds, while the fourth field holds "
     "the rounding offset, Rs, to be added immediately prior to "
     "downshifting.",
     "IIII",MULTI_RECORD);

  define_attribute(Kcoeffs,
     "Holds the lifting coefficients, Cs[n].  The first L0 records "
     "describe the coefficients of the first lifting step.  These are "
     "followed by the L1 coefficients of the second lifting step, and "
     "so forth.  The Ls values are identified by the first field in "
     "each `Ksteps' record.  Lifting step s may be described by "
     "X_s[2k+1-p] += TRUNC(sum_{Ns<=n<Ns+Ls} Cs[n]*X_{s-1}[2k+p+2n]).  "
     "In the case of an irreversible transform, the TRUNC operator "
     "does nothing and all arithmetic is performed (at least "
     "notionally) in floating point.  For reversible transforms, "
     "TRUNC(a) = floor(a + Rs*2^{-Ds}) and Cs[n] is guaranteed to be "
     "an integer multiple of 2^{-Ds}.",
     "F",MULTI_RECORD);
}

/*****************************************************************************/
/*                       atk_params::copy_with_xforms                        */
/*****************************************************************************/

void
  atk_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  bool reversible=false, symmetric=false;
  if (!source->get(Kreversible,0,0,reversible))
    return; // Nothing to copy
  if (!source->get(Ksymmetric,0,0,symmetric))
    {
      source->finalize(false); // In case user forgot to finalize source params
      if (!source->get(Ksymmetric,0,0,symmetric))
        return; // Something wrong with the source
    }

  set(Kreversible,0,0,reversible);
  set(Ksymmetric,0,0,symmetric);

  int extension=0;
  if (source->get(Kextension,0,0,extension))
    set(Kextension,0,0,extension);

  bool flip_taps = false;
  if ((vflip || hflip) && !symmetric)
    {
      flip_taps = true;
      if (vflip != hflip)
        { KDU_ERROR(e,0x19050505); e <<
            KDU_TXT("Cannot transpose ATK marker segment information "
                    "to a new codestream which has flippped geometry unless "
                    "the transform filters are whole-sample symmetric, or "
                    "flipping is to be applied in both the vertical and "
                    "horizontal directions.  The reason for this is that "
                    "the same transform kernels must be used in both "
                    "directions, only one of which requires reversal of "
                    "the lifting coefficients.");
        }
    }

  float coeff=0.0F;
  int s, n, n_off, Ls=0, Ns=0, Ds=0, Rs=0;
  for (s=0, n_off=0; source->get(Ksteps,s,0,Ls,false,false) &&
                     source->get(Ksteps,s,1,Ns,false,false) &&
                     source->get(Ksteps,s,2,Ds,false,false) &&
                     source->get(Ksteps,s,3,Rs,false,false);
       s++, n_off+=Ls)
    {
      if (flip_taps)
        Ns = -(Ns + Ls - 1) + 1-2*(s&1);
      set(Ksteps,s,0,Ls);
      set(Ksteps,s,1,Ns);
      set(Ksteps,s,2,Ds);
      set(Ksteps,s,3,Rs);
      for (n=0; n < Ls; n++)
        { // Copy the coefficients for this lifting step
          source->get(Kcoeffs,n_off+n,0,coeff);
          if (flip_taps)
            set(Kcoeffs,n_off+Ls-1-n,0,coeff);
          else
            set(Kcoeffs,n_off+n,0,coeff);
        }
    }
}

/*****************************************************************************/
/*                           atk_params::finalize                            */
/*****************************************************************************/

void
  atk_params::finalize(bool after_reading)
{
  int predef = -1;
  if (get(Kkernels,0,0,predef))
    { // Construct pre-defined transform kernel
      if (predef == Kkernels_I1X1)
        { // Build "Lazy wavelet" using 1 step which does nothing, rather than
          // zero steps -- it is only for experimental applications anyway.
          set(Kreversible,0,0,false);
          set(Ksymmetric,0,0,false);
          set(Kextension,0,0,Kextension_CON);
          set(Ksteps,0,0,1);  set(Ksteps,0,1,0);
          set(Ksteps,0,2,0);  set(Ksteps,0,3,0);
          set(Kcoeffs,0,0,0.0f);
        }
      else if (predef == Kkernels_R1X1)
        { // As above, build "lazy wavelet" using a single "predict" step that
          // actually does nothing.
          set(Kreversible,0,0,true);
          set(Ksymmetric,0,0,false);
          set(Kextension,0,0,Kextension_CON);
          set(Ksteps,0,0,1);  set(Ksteps,0,1,0);
          set(Ksteps,0,2,0);  set(Ksteps,0,3,0);
          set(Kcoeffs,0,0,0.0f);
        }
      else if (predef == Kkernels_I2X2)
        { // Irreversible Haar transform
          set(Kreversible,0,0,false);
          set(Ksymmetric,0,0,false);
          set(Kextension,0,0,Kextension_CON);
          set(Ksteps,0,0,1);  set(Ksteps,0,1,0);
          set(Ksteps,0,2,0);  set(Ksteps,0,3,0);
          set(Ksteps,1,0,1);  set(Ksteps,1,1,0);
          set(Ksteps,1,2,0);  set(Ksteps,1,3,0);
          set(Kcoeffs,0,0,-1.0f);  set(Kcoeffs,1,0,0.5f);          
        }
      else if (predef == Kkernels_R2X2)
        { // Reversible Haar (a.k.a. S-transform)
          set(Kreversible,0,0,true);
          set(Ksymmetric,0,0,false);
          set(Kextension,0,0,Kextension_CON);
          set(Ksteps,0,0,1);  set(Ksteps,0,1,0);
          set(Ksteps,0,2,0);  set(Ksteps,0,3,0);
          set(Ksteps,1,0,1);  set(Ksteps,1,1,0);
          set(Ksteps,1,2,1);  set(Ksteps,1,3,1);
          set(Kcoeffs,0,0,-1.0f);  set(Kcoeffs,1,0,0.5f);
        }
      else if (predef == Kkernels_I5X3)
        { // Irreversible 5/3 transform
          set(Kreversible,0,0,false);
          set(Ksymmetric,0,0,true);
          set(Kextension,0,0,Kextension_SYM);
          set(Ksteps,0,0,2);  set(Ksteps,0,1,0);
          set(Ksteps,0,2,0);  set(Ksteps,0,3,0);
          set(Ksteps,1,0,2);  set(Ksteps,1,1,-1);
          set(Ksteps,1,2,0);  set(Ksteps,1,3,0);
          set(Kcoeffs,0,0,-0.5f);  set(Kcoeffs,1,0,-0.5f);
          set(Kcoeffs,2,0,0.25f);  set(Kcoeffs,3,0,0.25f);
        }
      else
        { KDU_ERROR(e,0x05011701); e <<
          KDU_TXT("Unrecognized pre-defined kernel identifier supplied to "
                  "`Kkernels' attribute.");
        }
    }

  int s=0, n, n_off=0, Ls=0, Ns=0, Ds=0, Rs=0;
  float coeff=0.0f;
  bool reversible=false;
  if (!get(Kreversible,0,0,reversible))
    {
      if (get(Ksteps,0,0,Ls) || get(Kcoeffs,0,0,coeff))
        { KDU_ERROR(e,23050500); e <<
            KDU_TXT("You cannot provide custom transform kernel data via "
            "`Ksteps' or `Kcoeffs' without also providing a value for "
            "the `Kreversible' attribute.");
        }
      else
        return; // Nothing here
    }

  // Check lifting step information
  bool is_symmetric = true; // Until proven otherwise
  double dc_gain=1.0, gain_p=1.0, gain_pp=1.0;
  for (n_off=0, s=0; get(Ksteps,s,0,Ls); s++, n_off+=Ls)
    {
      if (!(get(Ksteps,s,1,Ns) && get(Ksteps,s,2,Ds) && get(Ksteps,s,3,Rs)))
        { KDU_ERROR(e,0x20050501); e <<
            KDU_TXT("Incomplete `Ksteps' record (need 4 fields "
            "in each record) found while in `atk_params::finalize'.");
        }
      if ((Ds < 0) || (Ds > 24))
        { KDU_ERROR(e,0x20050502); e <<
            KDU_TXT("Invalid downshifting value (3'rd field) found "
            "while checking supplied `Ksteps' attributes.  "
            "Values must be in the range 0 to 24.");
        }
      if ((!reversible) && ((Ds > 0) || (Rs > 0)))
        { KDU_ERROR(e,0x23050501); e <<
            KDU_TXT("For irreversible transforms (`Kreversible' = false), "
            "the third and fourth fields in each record of the `Ksteps' "
            "attribute must both be 0.");
        }
      if ((Ls & 1) || (Ns != -((Ls + (s&1)-1)>>1)))
        is_symmetric = false;

      dc_gain = 0.0;
      for (n=0; n < Ls; n++)
        {
          if (!get(Kcoeffs,n_off+n,0,coeff))
            { KDU_ERROR(e,0x20050503); e <<
                KDU_TXT("Insufficient `Kcoeffs' records found "
                "while in `atk_params::finalize'.  The number of "
                "coefficients must be equal to the sum of the "
                "lifting step lengths recorded in the `Ksteps' "
                "attribute.");
            }
          dc_gain += coeff;
          if (is_symmetric && (n < (Ls>>1)))
            {
              float sym_coeff;
              if (!(get(Kcoeffs,n_off+Ls-1-n,0,sym_coeff) &&
                    (coeff == sym_coeff)))
                is_symmetric = false;
            }
        }
      dc_gain = gain_pp + dc_gain*gain_p;
      gain_pp = gain_p;  gain_p = dc_gain;
    }
  if (get(Kcoeffs,n_off,0,coeff))
    { KDU_ERROR(e,0x20050504); e <<
        KDU_TXT("Too many `Kcoeffs' records found while in "
        "`atk_params::finalize'.  The number of coefficients must "
        "be equal to the sum of the lifting step lengths recorded "
        "in the `Ksteps' attribute.");
    }

  dc_gain = (s & 1)?gain_pp:gain_p;
  if (reversible && ((dc_gain > 1.001) || (dc_gain < 0.999)))
    { KDU_ERROR(e,0x20050505); e <<
        KDU_TXT("Reversible lifting steps defined by `Ksteps' and "
        "`Kcoeffs' produce a low-pass analysis filter whose "
        "DC gain is not equal to 1.");
    }

  int extension=0;
  if (!get(Kextension,0,0,extension))
    set(Kextension,0,0,
        extension=((is_symmetric)?Kextension_SYM:Kextension_CON));
  if (extension != Kextension_SYM)
    is_symmetric = false;
 
  bool check_symmetric=false;
  if (!get(Ksymmetric,0,0,check_symmetric,false))
    set(Ksymmetric,0,0,is_symmetric);
  else if (check_symmetric && !is_symmetric)
    { KDU_ERROR(e,0x20050509); e <<
        KDU_TXT("Invalid `Ksymmetric' value found while in "
        "`atk_params::finalize'.  The lifting step alignment and "
        "coefficients are not compatible with the whole-sample symmetric "
        "class of wavelet kernels defined by Part-2 of the JPEG2000 "
        "standard.");
    }
}

/*****************************************************************************/
/*                     atk_params::check_marker_segment                      */
/*****************************************************************************/

bool
  atk_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_ATK) || (num_bytes < 2))
    return false;
  int s_atk = *(bytes++);  s_atk = (s_atk<<8) + *(bytes++);
  c_idx = s_atk & 0x00FF;
  return ((c_idx >= 2) && (c_idx <= 255));
}

/*****************************************************************************/
/*                        atk_params::read_marker_segment                    */
/*****************************************************************************/

bool
  atk_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  if ((code != KDU_ATK) || (num_bytes < 2))
    return false;

  bp = bytes;
  end = bp + num_bytes;

  int s_atk = *(bp++);  s_atk = (s_atk<<8) + *(bp++);
  int which_inst = s_atk & 0x00FF;
  if (which_inst != inst_idx)
    return false;

  int bytes_per_coeff = 1 << ((s_atk >> 8) & 3);
  if (bytes_per_coeff > 8)
    { KDU_ERROR(e,0x2005050a); e <<
        KDU_TXT("Cannot process ATK marker segment which uses more "
        "than 8 bytes to represent each lifting step coefficient.");
    }

  bool symmetric = ((s_atk >> 11) & 1)?true:false;
  bool reversible = ((s_atk >> 12) & 1)?true:false;
  int m_last = ((s_atk >> 13) & 1);
  int extension = ((s_atk >> 14) & 1)?Kextension_SYM:Kextension_CON;

  if (symmetric && (extension != Kextension_SYM))
    { KDU_ERROR(e,0x2005050b); e <<
        KDU_TXT("Malformed ATK marker segment encountered.  Transform "
        "kernels identified as whole-sample symmetric must also use the "
        "symmetric boundary extension method.");
    }

  if (reversible && (bytes_per_coeff > 2))
    { KDU_ERROR(e,0x21050500); e <<
        KDU_TXT("Cannot process ATK marker segment describing a reversible "
        "transform kernel with floating-point coefficient values.");
    }

  set(Kreversible,0,0,reversible);
  set(Ksymmetric,0,0,symmetric);
  set(Kextension,0,0,extension);

  int num_coeffs = 0;
  try {
      if (!reversible)
        bp += bytes_per_coeff; // Skip over the K value -- it is redundant
      int num_steps = kdu_read(bp,end,1);
      int s, s_min=0, s_max=num_steps-1;
      int Ls=0, Ns=0, Ds, Rs, n;
      if ((s_max & 1) != (1-m_last))
        { s_max++; s_min++; }
      for (s=s_max; s >= s_min; s--)
        { // ATK marker segment records lifting steps in synthesis order, but
          // we store them in analysis order
          if (!symmetric)
            {
              Ns = kdu_read(bp,end,1);
              Ns -= (Ns & 0x80)?256:0; // Convert to 2's complement
            }
          if (reversible)
            {
              Ds = kdu_read(bp,end,1);
              Rs = kdu_read(bp,end,bytes_per_coeff);
              if (bytes_per_coeff == 1)
                Rs -= (Rs & 0x80)?(1<<8):0; // Convert to 2's complement
              else if (bytes_per_coeff == 2)
                Rs -= (Rs & 0x8000)?(1<<16):0; // Convert to 2's complement
            }
          else
            Ds = Rs = 0;
          Ls = kdu_read(bp,end,1);
          if (symmetric)
            {
              Ls <<= 1; // Only half the entries signalled in ATK segment
              Ns = -((Ls + (s&1) - 1) >> 1);
            }
          set(Ksteps,s,0,Ls);  set(Ksteps,s,1,Ns);
          set(Ksteps,s,2,Ds);  set(Ksteps,s,3,Rs);
          if (symmetric)
            Ls >>= 1; // Go back to number of entries in ATK segment
          for (n=0; n < Ls; n++)
            { // Read coefficients, writing them in reverse order into the
              // `Kcoeffs' array, as appropriate.  Later on, we will reverse
              // the entire array.
              int ival=0;
              float coeff=0.0F;
              switch (bytes_per_coeff) {
                case 1:
                  ival = kdu_read(bp,end,1);
                  ival -= (ival & 0x80)?(1<<8):0; // Convert to 2's comp
                  coeff = (float) ival;
                  break;
                case 2:
                  ival = kdu_read(bp,end,2);
                  ival -= (ival & 0x8000)?(1<<16):0; // Convert to 2's comp
                  coeff = (float) ival;
                  break;
                case 4:
                  coeff = kdu_read_float(bp,end);
                  assert(!reversible);
                  break;
                case 8:
                  coeff = (float) kdu_read_double(bp,end);
                  assert(!reversible);
                  break;
                default: assert(0);
                };
              if (reversible)
                coeff /= (float)(1<<Ds); 
              set(Kcoeffs,num_coeffs+Ls-1-n,0,coeff);
              if (symmetric)
                { // Write second, not reversed copy of coefficients
                  set(Kcoeffs,num_coeffs+Ls+n,0,coeff);
                }
            }
          num_coeffs += (symmetric)?(Ls+Ls):Ls;
        }
      if (s == 0)
        { // First lifting step is empty; not recorded in ATK marker segment
          set(Ksteps,s,0,0);  set(Ksteps,s,1,0);
          set(Ksteps,s,2,0);  set(Ksteps,s,3,0);
        }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,0x205050c); e <<
        KDU_TXT("Malformed ATK marker segment encountered.  Marker segment "
        "is too small.");
    }
  if (bp != end)
    { KDU_ERROR(e,0x2005050d); e <<
        KDU_TXT("Malformed ATK marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  // Now all we have to do is reverse the order of the coefficient entries
  int n, p;
  for (n=0, p=num_coeffs-1; n < p; n++, p--)
    {
      float n_val=0, p_val=0;
      get(Kcoeffs,n,0,n_val);  get(Kcoeffs,p,0,p_val);
      set(Kcoeffs,n,0,p_val);  set(Kcoeffs,p,0,n_val);
    }

  return true;
}

/*****************************************************************************/
/*                       atk_params::write_marker_segment                    */
/*****************************************************************************/

int
  atk_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  atk_params *ref = (atk_params *) last_marked;
  if ((inst_idx < 2) || (inst_idx > 255) || (tpart_idx != 0) ||
      (comp_idx >= 0))
    return 0;

  // Collect the scalar parameters
  bool reversible=false, symmetric=false;
  int extension=0;

  if (!(get(Kreversible,0,0,reversible) &&
        get(Ksymmetric,0,0,symmetric) &&
        get(Kextension,0,0,extension)))
    { KDU_ERROR_DEV(e,0x2007050a); e <<
        KDU_TXT("Unable to write ATK marker segment yet! Some info missing.");
    }

  int s, Ls=0, Ns=0, Ds=0, Rs=0;
  int n, n_off;
  float coeff=0.0f;
  if (ref != NULL)
    { // See if we can skip marker segment generation.
      if (ref->compare(Kreversible,0,0,reversible) &&
          ref->compare(Ksymmetric,0,0,symmetric) &&
          ref->compare(Kextension,0,0,extension))
        { // So far so good.  Now let's check the lifting steps themselves
          int tested_steps = 0;
          for (n_off=0, s=0;
               get(Ksteps,s,0,Ls) && get(Ksteps,s,1,Ns) &&
               get(Ksteps,s,2,Ds) && get(Ksteps,s,3,Rs);
               s++, n_off+=Ls)
            {
              tested_steps = s+1;
              if (!(ref->compare(Ksteps,s,0,Ls) &&
                    ref->compare(Ksteps,s,1,Ns) &&
                    ref->compare(Ksteps,s,2,Ds) &&
                    ref->compare(Ksteps,s,3,Rs)))
                break;
              for (n=0; n < Ls; n++)
                {
                  get(Kcoeffs,n_off+n,0,coeff);
                  if (!ref->compare(Kcoeffs,n_off+n,0,coeff))
                    break;
                }
              if (n != Ls)
                break;
            }

          if (s == tested_steps)
            return 0; // Object identical to reference; did not break early.
        }
    }

  // We are now committed to writing (or simulating) a marker segment.
  int num_steps=0, num_coeffs=0;
  for (s=0; get(Ksteps,s,0,Ls) && get(Ksteps,s,1,Ns) &&
            get(Ksteps,s,2,Ds) && get(Ksteps,s,3,Rs); s++)
    { num_steps++;  num_coeffs += Ls; }

  int num_coeff_entries = (symmetric)?(num_coeffs>>1):num_coeffs;

  int length = 4 + 2; // Marker code, length and Satk
  if (!reversible)
    length += 4; // For signalling the K value
  length++; // For signalling num_steps
  if (reversible)
    length += num_steps*5 + num_coeff_entries*2; // 2-byte integer coeffs
  else
    length += num_steps*2 + num_coeff_entries*4; // 4-byte float coeffs
  if (symmetric)
    length -= num_steps; // Don't need to signal the Ns value.

  if (out == NULL)
    return length;

  // If we get here, we are committed to actually writing the marker segment
  int s_atk = inst_idx;
  if (symmetric)
    s_atk += (1<<11);
  if (reversible)
    s_atk += (1 << 12);
  if (num_steps & 1)
    s_atk += (1 << 13);
  if (extension == Kextension_SYM)
    s_atk += (1 << 14);

  if (reversible)
    s_atk += (1<<8); // 2-byte integer coefficients
  else
    s_atk += (2<<8); // 4-byte floating point coefficients

  int acc_length = 0; // Accumulate actual length for verification purposes.
  acc_length += out->put(KDU_ATK);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_uint16) s_atk);

  if (!reversible)
    { // Calculate and write the K value
      double dc_gain=1.0, gain_p=1.0, gain_pp=1.0;
      for (n_off=0, s=0; get(Ksteps,s,0,Ls); s++, n_off+=Ls)
        {
          dc_gain = 0.0;
          for (n=0; n < Ls; n++)
            {
              get(Kcoeffs,n_off+n,0,coeff);
              dc_gain += coeff;
            }
          dc_gain = gain_pp + dc_gain*gain_p;
          gain_pp = gain_p;  gain_p = dc_gain;
        }
      dc_gain = (s & 1)?gain_pp:gain_p;
      acc_length += out->put((float) dc_gain); // This is the K value
    }

  if (num_steps > 255)
    { KDU_ERROR_DEV(e,0x20020701); e <<
        KDU_TXT("Cannot write ATK (arbitrary transform kernel) marker "
                "segment with") << " " << num_steps << " " <<
        KDU_TXT("lifting steps.  Maximum number of lifting steps is 255.");
    }
  acc_length += out->put((kdu_byte) num_steps);

  for (n_off=num_coeffs, s=num_steps-1; s >= 0; s--)
    { // Write the parameters for each lifting step, in synthesis order
      get(Ksteps,s,0,Ls);  get(Ksteps,s,1,Ns);
      get(Ksteps,s,2,Ds);  get(Ksteps,s,3,Rs);
      if (!symmetric)
        {
          if ((Ns > 255) || (Ls > 255))
            { KDU_ERROR_DEV(e,0x20020702); e <<
                KDU_TXT("Cannot write ATK (arbitrary transform kernel) marker "
                        "segment with a non-symmetric wavelet kernel whose "
                        "Ns or Ls value (see `Ksteps') is greater than 255.");
            }
          acc_length += out->put((kdu_byte)(Ns & 0x00FF));
        }
      if (reversible)
        {
          acc_length += out->put((kdu_byte) Ds);
          acc_length += out->put((kdu_uint16)(Rs & 0x00FFFF));
        }
      if (symmetric)
        {
          if (Ls > 510)
            { KDU_ERROR_DEV(e,0x20020703); e <<
                KDU_TXT("Cannot write ATK (arbitrary transform kernel) marker "
                        "segment with a symmetric wavelet kernel whose "
                        "Ls value (see `Ksteps') is greater than 510.");
            }
          Ls >>= 1; // Only write half the coefficients
        }
      n_off -= Ls;
      acc_length += out->put((kdu_byte) Ls);

      for (n=0; n < Ls; n++)
        {
          get(Kcoeffs,n_off+n,0,coeff);
          if (reversible)
            {
              int ival = (int) floor(coeff*((float)(1<<Ds)) + 0.5F);
              acc_length += out->put((kdu_uint16)(ival & 0x00FFFF));
            }
          else
            acc_length += out->put(coeff);
        }

      if (symmetric)
        n_off -= Ls; // Skip over remaining ones
    }

  assert(length == acc_length);
  return length;
}


/* ========================================================================= */
/*                                cod_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          cod_params::new_object                           */
/*****************************************************************************/

kdu_params *cod_params::new_object()
{
  return new cod_params(); 
}

/*****************************************************************************/
/*                       cod_params::cod_params (no args)                    */
/*****************************************************************************/

cod_params::~cod_params() {}
cod_params::cod_params()
  : kdu_params(COD_params,true,true,false)
{
  add_dependency(QCD_params);
  define_attribute(Cycc,
                   "RGB to Luminance-Chrominance conversion?\n"
                   "\t\t[Default is to convert images with 3 or more "
                   "components, unless a Part 2 multi-component transform "
                   "is defined -- `Mcomponents' > 0]",
                   "B",ALL_COMPONENTS);
  define_attribute(Cmct,
    "This parameter should be 0 unless a Part 2 multi-component transform "
    "is being used, in which case it contains one or both of the "
    "`ARRAY' and `DWT' options -- if both options are present, they are "
    "separated by a `|'.  The `ARRAY' option will be present if and only "
    "if an array-based multi-component transform block is associated with "
    "the image, or the relevant tile (for tile-specific instances of the "
    "attribute).  The `DWT' option will be present if and only if a DWT-based "
    "multi-component transform block is associated with the image, or the "
    "relevant tile (for tile-specific instances of the COD marker segment).  "
    "Both flags will be present if both types of multi-component transform "
    "block are employed for the image or tile, as appropriate.  During "
    "codestream generation, the information in this parameter is generated "
    "automatically to conform with the information provided via the `Mstages' "
    "and `Mstage_xforms' attributes.  "
    "When reading an existing codestream, the information in this parameter "
    "should either be correct or 0.  In the latter case, the internal "
    "machinery interprets the codestream as one generated by versions of "
    "Kakadu prior to v6.0, wherein the transform coefficients for "
    "reversible matrix-based transforms were accidentally transposed -- the "
    "transposition error is corrected automatically in this case.",
                   "[ARRAY=2|DWT=4]",ALL_COMPONENTS);
  define_attribute(Clayers,
                   "Number of quality layers. May not exceed 16384.\n"
                   "\t\t[Default is 1]",
                   "I",ALL_COMPONENTS);
  define_attribute(Cuse_sop,
                   "Include SOP markers (i.e., resync markers)?\n"
                   "\t\t[Default is no SOP markers]",
                   "B",ALL_COMPONENTS);
  define_attribute(Cuse_eph,
                   "Include EPH markers (marker end of each packet header)?\n"
                   "\t\t[Default is no EPH markers]",
                   "B",ALL_COMPONENTS);
  define_attribute(Corder,
                   "Default progression order (may be overridden by "
                   "Porder).  The four character identifiers have the "
                   "following interpretation: L=layer; R=resolution; "
                   "C=component; P=position. The first character in the "
                   "identifier refers to the index which progresses most "
                   "slowly, while the last refers to the index which "
                   "progresses most quickly.\n"
                   "\t\t[Default is LRCP]",
                   "(LRCP=0,RLCP=1,RPCL=2,PCRL=3,CPRL=4)",ALL_COMPONENTS);
  define_attribute(Calign_blk_last,
                   "If \"yes\", the code-block partition is aligned so that "
                   "the last sample in each nominal block (ignoring the "
                   "effect of boundaries) is aligned at a multiple of "
                   "the block dimension (a power of 2).  Equivalently, the "
                   "first sample in each nominal block lies at a location "
                   "which is a multiple of the block dimension, plus 1. By "
                   "default (i.e., \"no\"), the first sample of each block is "
                   "aligned at a multiple of the block dimension. The "
                   "alignment is specified separately for both dimensions, "
                   "with the vertical dimension specified first.",
                   "BB",ALL_COMPONENTS);
  define_attribute(Clevels,
                   "Number of wavelet decomposition levels, or stages.  May "
                   "not exceed 32.\n"
                   "\t\t[Default is 5]",
                   "I");
  define_attribute(Cads,
                   "Index of the ADS marker segment used to hold Arbitrary "
                   "Downsampling Style information.  If ADS information is "
                   "involved, the value of the `Cads' index must lie in the "
                   "range 1 to 127.  A value of 0 means that no ADS marker "
                   "segment is referenced.  You will not normally set this "
                   "parameter yourself.  It is preferable to allow the "
                   "internal machinery to find a suitable index.  In any "
                   "event, the ADS information recorded in the `DOads' and "
                   "`DSads' attributes will be generated automatically from "
                   "information contained in `Cdecomp'.  During marker "
                   "segment reading, the ADS information is used together "
                   "with any DFS information (see `Cdfs') in order to "
                   "reconstruct the `Cdecomp' attribute.\n"
                   "\t\t[Best not to set this yourself.  An index is selected "
                   "automatically if `Cdecomp' defines a non-trivial "
                   "decomposition.]",
                   "I");
  define_attribute(Cdfs,
                   "Index of the DFS marker segment used to hold Downsampling "
                   "Factor Style information.  If DFS information is "
                   "involved, the value of the `Cdfs' index must be in the "
                   "range 1 to 127.  A value of 0 means that no DFS marker "
                   "segment is referenced.  This attribute is ignored "
                   "outside of the main header (i.e., for non-negative tile "
                   "indices).  You will not normally set this parameter "
                   "yourself.  Rather, it is preferable to allow the "
                   "internal machinery to find a suitable index for you.  "
                   "In any event, the DFS instructions recorded in the "
                   "`DSdfs' attribute will be generated automatically "
                   "from information contained in `Cdecomp'.  During "
                   "marker segment reading, the DFS instructions will be "
                   "read, along with any ADS information (see `Cads') in "
                   "order to reconstruct the `Cdecomp' attribute.\n"
                   "\t\t[Best not to set this yourself.  An index is selected "
                   "automatically if `Cdecomp' defines a non-trivial "
                   "decomposition.]",
                   "I");
  define_attribute(Cdecomp,
                   "Manages the information associated with the JPEG2000 "
                   "Part-2 `ADS' and `DFS' marker segments, if any, as "
                   "referenced by the `Cads' and `Cdfs' attributes.  If "
                   "neither of these is present, the default value of 3 "
                   "is used, which yields the conventional Mallat "
                   "decomposition structure.  Each record describes the "
                   "subband splitting processes for one entire DWT level, "
                   "starting from the first (highest frequency) level.  "
                   "The textual form of each record commences with a "
                   "primary splitting code, which is one of the "
                   "characters: `B' (split both ways a la Mallat); `H' "
                   "(split only horizontally); `V' (split only vertically); "
                   "or `-' (do not split at all -- degenerate case).  The "
                   "first option produces three primary detail subbands, "
                   "denoted HL, LH and HH.  The second and third options "
                   "produce only one primary detail subband (HX or XH), "
                   "while the last option produces no detail subbands at "
                   "all.  The primary splitting code is followed by "
                   "parentheses, containing 0, 1 or 3 colon-separated "
                   "sub-strings, each of which describes the additional "
                   "splitting operations to be applied to each primary "
                   "detail subband.  Each sub-string consists of 1, 3 or 5 "
                   "characters, drawn from `-' (meaning no-split), `H' "
                   "(meaning a horizontal split), `V' (meaning a vertical "
                   "split) and `B' (meaning a bi-directional split).  If the "
                   "sub-string commences with `H' or `V', two additional "
                   "characters may be provided to describe further splitting "
                   "of the low- and high-pass subbands produced by the "
                   "first split.  If the sub-string commences with `B', "
                   "four additional characters may be provided to describe "
                   "further splitting of the LL, HL, LH and HH "
                   "subbands produced by the primary split.  Alternatively, "
                   "the sub-string may consist only of the initial character, "
                   "in which case no further splitting is involved.  Thus, "
                   "\"B\" and \"B----\" are equivalent sub-strings, as are "
                   "\"H\" and \"H--\".\n"
                   "\t   If insufficient parameters are supplied to "
                   "accommodate the number of desired DWT levels, the final "
                   "value is simply replicated.  Note, however, that the "
                   "last value must conform to some specific rules, "
                   "which derive from the way in which JPEG2000 Part-2 "
                   "defines extrapolation for information found in the ADS "
                   "and DFS marker segments.  In particular, the terminal "
                   "parameter must have identical splitting descriptors for "
                   "all primary detail subbands (remember there are 0, 1 or "
                   "3 of these).  Moreover, within each of these "
                   "descriptors, all splitting codes (`-', `H', 'V' and 'B') "
                   "must be identical.  The only exception to this occurs "
                   "where all primary detail subbands are split only once, "
                   "in which case all primary detail subbands must have "
                   "identical sub-strings holding one of the patterns, "
                   "\"B----\", \"H--\", \"V--\" or \"-\".  Thus, "
                   "\"B(-:-:-)\", \"H(BBBBB)\", \"B(HHH:HHH:HHH)\", "
                   "\"V(H--)\" and \"B(V--:V--:V--)\" are all legal "
                   "terminal values, while \"B(B:B:-)\" and \"V(VV-)\" are "
                   "not legal.\n"
                   "\t\t[If `Cdecomp' is not specified, a value is determined "
                   "from the ADS and/or DFS information referenced by "
                   "`Cads' and `Cdfs'.  If there is no such information, the "
                   "default `Cdecomp' value is \"B(-,-,-)\", which translates "
                   "to the integer value, 3.  All Part-1 codestreams must "
                   "use this Mallat decomposition style.]",
                   "C", MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Creversible,
                   "Reversible compression?\n"
                   "\t\t[Default is irreversible, if `Ckernels' and "
                   "`Catk' are not used, except where this conflicts with a "
                   "specific profile requirement such as that of a reversible "
                   "BROADCAST or IMF profile.  Otherwise, the reversibility "
                   "is derived from those values.]",
                   "B");
  define_attribute(Ckernels,
                   "Wavelet kernels to use.  The special value, `ATK' "
                   "means that an ATK (Arbitrary Transform Kernel) marker "
                   "segment is used to store the DWT kernel.  In this case, "
                   "the `Catk' attribute must be non-zero.\n"
                   "\t\t[Default is W5X3 if `Creversible' is true, "
                   "W9X7 if `Creversible' is false, and ATK if "
                   "`Catk' is non-zero.]",
                   "(W9X7=0,W5X3=1,ATK=-1)");
  define_attribute(Catk,
                   "A value of 0 means that the DWT kernel is one of W5X3 "
                   "or W9X7, as specified by the `Ckernels' attribute.  "
                   "Otherwise, this attribute holds the index of the ATK "
                   "marker segment which defines the transform kernel.  "
                   "The index must lie in the range 2 to 255 and "
                   "corresponding `Kreversible', `Krev_steps' or "
                   "`Kirv_steps' attributes must exist, which have the same "
                   "index (instance) value.  Thus, for example, if `Catk=3', "
                   "you must also supply a value for `Kreversible:I3' and/or "
                   "`Krev_steps:I3' or `Kirv_steps:I3', as appropriate.  "
                   "This information allows the internal machinery to deduce "
                   "whether the transform is reversible or not.  The ATK "
                   "information in these parameter attributes can also "
                   "be tile-specific.  To construct an arbitrary transform "
                   "kernel, you can either explicitly provide the transform "
                   "attributes via `Kreversible', `Ksymmetric', `Kextension', "
                   "`Ksteps' and `Kcoeffs', or you can use the convenient "
                   "`Kkernels' meta-attribute to build the parameters for "
                   "pre-defined transforms that might exist; for example, "
                   "\"Kkernels:I3=R2X2\" configures the reversible Haar "
                   "transform for use with \"Catk=3\".\n"
                   "\t\t[Default is 0]",
                   "I");
  define_attribute(Cuse_precincts,
                   "Explicitly specify whether or not precinct dimensions "
                   "are supplied.\n"
                   "\t\t[Default is \"no\" unless `Cprecincts' is "
                   "used]",
                   "B");
  define_attribute(Cprecincts,
                   "Precinct dimensions (must be powers of 2). Multiple "
                   "records may be supplied, in which case the first record "
                   "refers to the highest resolution level and subsequent "
                   "records to lower resolution levels. The last specified "
                   "record is used for any remaining lower resolution levels."
                   "Inside each record, vertical coordinates appear first.",
                   "II",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Cblk,
                   "Nominal code-block dimensions (must be powers of 2, "
                   "no less than 4 and no greater than 1024, whose product "
                   "may not exceed 4096). Actual dimensions are subject "
                   "to precinct, tile and image dimensions. Vertical "
                   "coordinates appear first.\n"
                   "\t\t[Default block dimensions are {64,64}]",
                   "II");
  define_attribute(Cmodes,
                   "Block coder mode switches. Any combination is legal.  Note "
                   "that the `BYPASS_E2' and `BYPASS_E2' mode switches are "
                   "specific to Part-2 codestreams that conform to the "
                   "fast-mode defined in IS15444-2/AMD4.  These switches are "
                   "meaningless unless combined with the `BYPASS' mode switch, "
                   "in which case the arithmetic coder is bypassed earlier "
                   "than it otherwise would be in the MR and SP coding "
                   "passes by 2*`BYPASS_E2' + 1*`BYPASS_E1' bit-planes.\n"
                   "\t\t[By default, all mode switches are turned off]",
                   "[BYPASS=1|RESET=2|RESTART=4|CAUSAL=8|ERTERM=16|SEGMARK=32"
                   "|BYPASS_E1=64|BYPASS_E2=128]");
  define_attribute(Cweight,
                   "Multiplier for subband weighting factors (see "
                   "`Clev_weights' and `Cband_weights' below).  Scaling all "
                   "the weights by a single quantity has no impact on their "
                   "relative significance.  However, you may supply a "
                   "separate weight for each component, or even each "
                   "tile-component, allowing you to control the relative "
                   "signicance of image components or tile-components in a "
                   "simple manner.",
                   "F");
  define_attribute(Clev_weights,
                   "Weighting factors for each successive resolution "
                   "level, starting from the highest resolution and working "
                   "down to the lowest (but not including the LL band!!). The "
                   "last supplied weight is repeated as necessary.  Weight "
                   "values are squared to obtain energy weights for weighted "
                   "MSE calculations.  The LL subband always has a weight of "
                   "1.0, regardless of the number of resolution levels.  "
                   "However, the weights associated with all subbands, "
                   "including the LL band, are multiplied by the value "
                   "supplied by `Cweight', which may be specialized to "
                   "individual components or tile-components.",
                   "F",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Cband_weights,
                   "Weighting factors for each successive subband, "
                   "starting from the highest frequency subbands and working "
                   "down (i.e., HH1, LH1, HL1, HH2, ...). The last supplied "
                   "weight is repeated as necessary for all remaining "
                   "subbands (except the LL band). If `Clev_weights' is also "
                   "supplied, both sets of weighting factors are combined "
                   "(multiplied).  Weight values are squared to obtain energy "
                   "weights for weighted MSE calculations.  The LL subband "
                   "always has a weight of 1.0, which avoids "
                   "problems which may occur when image components or "
                   "tiles have different numbers of resolution levels.  "
                   "To modify the relative weighting of components or "
                   "tile-components, including their LL subbands, use "
                   "the `Cweight' option; its weighting factors are "
                   "multiplied by those specified using `Cband_weights' and "
                   "`Clev_weights'.  If the `Cdecomp' attribute is used to "
                   "describe more general packet wavelet transforms, all "
                   "subbands obtained by splitting an HL, LH or HH subband "
                   "will be assigned the same weight.  No mechanism is "
                   "provided for specifying their weights separately.  "
                   "Moreover, all three weights (HL, LH and HH) are "
                   "present for each resolution level, even if that "
                   "level only involves horizontal or vertical splitting, "
                   "and even in the degenerate case of no splitting at "
                   "all.  For horizontal splitting only, subbands derived "
                   "from HX use the corresponding HL weight; HH and LH "
                   "weights are then ignored.  Similarly for vertical "
                   "splitting only, subbands derived from XH use the "
                   "corresponding LH weight; HH and HL weights are then "
                   "ignored.",
                   "F",MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(Cvis,
       "Activates a human visual contrast masking model that can improve "
       "visual quality at a given bit-rate, so long as the image is large and "
       "contains sufficiently diverse content.  This option should be more "
       "effective with smaller code-block sizes (e.g., Cblk={32,32}), but "
       "can work quite well even with the default block size of 64x64.  "
       "The single real-valued parameter is interpreted as a \"visibility "
       "floor\", typically in the range of 0.01 to 0.0001.  Notionally, the "
       "visibility floor is equal to the amplitude of the smallest subband "
       "signal that can be detected; this is expressed relative to the unit "
       "nominal range convention, in which 0.5 is the maximum amplitude one "
       "could possibly get in any subband, in response to any sinusoidal "
       "input stimulus.  At or below this detection threshold, the strength "
       "of neighbouring subband samples is considered to have no impact on "
       "the visibility of quantization artifacts.  Above the visibility "
       "floor, their masking impact is considered to grow as the square root "
       "of their amplitude.  Another way to understand the visibility floor "
       "is that its reciprocal is the maximum amount by which the distortion "
       "contribution from any sample can be magnitied via the contrast "
       "masking mechanism.  For example, a value of 0.01 yields squared error "
       "distortion magnification factors in the range 2 to 100 (i.e., a range "
       "of 50:1), which is equivalent to adjusting `Cband_weights' values "
       "through a range of roughly 7:1 in a spatially varying, content "
       "dependent manner -- note that the `Cband_weights' and other global "
       "weighting factors are all squared before being applied to squared "
       "error distortion contributions.  This option is somewhat experimental "
       "at the current time.",
       "F");
  define_attribute(Creslengths,
       "Maximum number of compressed bytes (packet headers plus packet "
       "bodies) that can be produced for each successive image resolution, "
       "starting from the highest resolution and working down to the lowest.  "
       "The limit applies to the cumulative number of bytes generated for "
       "the resolution in question and all lower resolutions.  If the "
       "attribute is global to the entire codestream (no T or C "
       "specifier), the limit for each resolution applies to the "
       "cumulative number of bytes up to that resolution in all tiles and "
       "all image components.  If the attribute is tile-specific "
       "but not component-specific, the limit for each resolution applies "
       "to the cumulative number of bytes up to that resolution for all "
       "image components within the tile.  If the attribute is "
       "component-specific, the limit applies to the cumulative number of "
       "bytes up to the resolution in question across all tiles, but only "
       "in that image component.  Finally, if the attribute is "
       "component-specific and tile-specific, the limit applies to the "
       "cumulative number of bytes up to the resolution in question, within "
       "just that tile-component.  You can provide limits of all four types.  "
       "Moreover, you need not provide limits for all resolutions. "
       "The initial set of byte limits applies only to the first quality "
       "layer to be generated during compression.  Limits for additional "
       "quality layers may be supplied by inserting zero or negative values "
       "into the list; these are treated as layer delimiters.  So, for "
       "example, the parameter string \"1000,700,0,3000,2000,0,10000\" "
       "provides limits of 1000 and 700 bytes for the highest and second "
       "highest resolutions in the first quality layer, 3000 and 2000 bytes "
       "for the same resolutions in the second quality layer, and a limit "
       "of 10000 bytes only to the highest resolution in the third quality "
       "layer.  Any subsequent quality layers are not restricted by this "
       "parameter attribute.  Note that each quality layer that is "
       "affected by the `Creslengths' attribute should be governed by "
       "an explicit target length (i.e., target bit-rate) or an explicit "
       "target distortion-length slope for rate-control purposes.  Implicit "
       "target lengths (or bit-rates), corresponding to zero-valued "
       "entries in the array of target lengths passed to "
       "`kdu_codestream::flush' or an equivalent function, may produce "
       "unexpected outcomes when used with the `Creslengths' attribute.  "
       "Note also that `Creslengths' constraints will not currently be "
       "applied correctly if incremental codestream flushing is used -- this "
       "can be arranged if a significant need arises for the combined "
       "functionality.  Finally, note that the interpretation of the "
       "`Creslengths' attribute may be modified by the presence of a "
       "`Cagglengths' attribute.",
       "I",MULTI_RECORD);
    define_attribute(Cagglengths,
       "This attribute has meaning only when used in conjunction with "
       "`Creslengths'.  Moreover, only component specific and tile-component "
       "specific forms of the `Cagglengths' attribute are meaningful.  "
       "Suppose the attribute is applied to image component C; then the "
       "attribute directs the rate control machinery to combine (aggregate) "
       "the compressed bytes (packet headers plus packet bodies) generated "
       "for each successively lower resolution of component C with the "
       "corresponding compressed bytes generated for a (possibly different) "
       "specified image component, whose `Creslengths' constraints shall "
       "be used to constrain the aggregated total.  The first parameter "
       "identifies the image component index c_0 (zero based) with which "
       "the full compressed data tally for component C should be aggregated "
       "before applying the corresponding `Creslengths' constraint for "
       "component c_0.  The next parameter identifies the image component "
       "index c_1 with which the next lower resolution compressed data tally "
       "for component C should be aggregated, and so forth.  The last "
       "supplied parameter is replicated (extrapolated) as required to cover "
       "all resolutions for component C.  The purpose of the `Cagglengths' "
       "attribute is to provide a means for explicitly constraining the "
       "aggregated amount of data generated for specific sets of image "
       "components.  Multiple image components C may direct their "
       "cumulative compressed data tallies to be aggregated with any given "
       "other component prior to assessing `Creslengths' constraints.  If "
       "a component index supplied with the `Cagglengths' attribute "
       "identifies an image component that does not exist, or that has "
       "no component-specific `Creslengths' constraints, no constraints "
       "will apply in regard to that resolution of the component to "
       "which the `Cagglengths' attribute belongs.  Tile-component specific "
       "forms of the `Cagglengths' attribute describe the aggregation of "
       "cumulative compressed byte counts from one tile-component with "
       "another tile-component in the same tile, the aggregated total "
       "being subjected to any constraints supplied via a `Creslengths' "
       "attribute for the target tile-component.",
       "I",MULTI_RECORD | CAN_EXTRAPOLATE);
}

/*****************************************************************************/
/*                       cod_params::copy_with_xforms                        */
/*****************************************************************************/

void
  cod_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  if (comp_idx < 0)
    { // Start with the attributes which are common to all components
      bool use_ycc=false;
      int mct_flags=0;
      int num_layers=0;
      bool use_sop=false, use_eph=false;
      int order=0;
      bool x_last=false, y_last=false;

      if (source->get(Cycc,0,0,use_ycc,false))
        {
          if (skip_components)
            use_ycc = false;
          set(Cycc,0,0,use_ycc);
        }
      if (source->get(Cmct,0,0,mct_flags,false))
        set(Cmct,0,0,mct_flags);
      if (source->get(Clayers,0,0,num_layers,false))
        set(Clayers,0,0,num_layers);
      if (source->get(Cuse_sop,0,0,use_sop,false))
        set(Cuse_sop,0,0,use_sop);
      if (source->get(Cuse_eph,0,0,use_eph,false))
        set(Cuse_eph,0,0,use_eph);
      if (source->get(Corder,0,0,order,false))
        set(Corder,0,0,order);
      if (source->get(Calign_blk_last,0,(transpose)?1:0,y_last,false) &&
          source->get(Calign_blk_last,0,(transpose)?0:1,x_last,false))
        {
          if (hflip) x_last = !x_last;
          if (vflip) y_last = !y_last;
          set(Calign_blk_last,0,0,y_last); set(Calign_blk_last,0,1,x_last);
        }
    }

  // Now for the tile-component specific attributes
  int num_levels=0, dfs_idx=0;
  bool reversible=false;
  int kernels=0, atk_idx=0;
  bool use_precincts=false;
  int blk_x=0, blk_y=0;
  int modes=0;

  if (source->get(Clevels,0,0,num_levels,false))
    {
      num_levels -= discard_levels;
      if (num_levels < 0)
        { KDU_ERROR_DEV(e,90); e <<
            KDU_TXT("Attempting to discard too many resolution "
            "levels!  Cannot discard more resolution levels than there are "
            "DWT levels.");
        }
      set(Clevels,0,0,num_levels);
    }
  if (source->get(Cdfs,0,0,dfs_idx,false))
    set(Cdfs,0,0,dfs_idx);

       // Note that we deliberately do not copy `Cads' since it is best
       // to let the `finalize' function assign new ADS indices during
       // transcoding

  int n, decomp_val=0;
  for (n=0; source->get(Cdecomp,n,0,decomp_val,false,false); n++)
    {
      if (transpose)
        decomp_val = transpose_decomp(decomp_val);
      if (n >= discard_levels)
        set(Cdecomp,n-discard_levels,0,decomp_val);
    }
  if ((n > 0) && (n <= discard_levels))
    set(Cdecomp,0,0,decomp_val); // We discarded it all; replicate last entry

  if (source->get(Creversible,0,0,reversible,false))
    set(Creversible,0,0,reversible);
  if (source->get(Ckernels,0,0,kernels,false))
    set(Ckernels,0,0,kernels);
  if (source->get(Catk,0,0,atk_idx,false))
    set(Catk,0,0,atk_idx);
  if (source->get(Cuse_precincts,0,0,use_precincts,false))
    set(Cuse_precincts,0,0,use_precincts);
  if (source->get(Cblk,0,(transpose)?1:0,blk_y,false) &&
      source->get(Cblk,0,(transpose)?0:1,blk_x,false))
    { set(Cblk,0,0,blk_y); set(Cblk,0,1,blk_x); }
  if (source->get(Cmodes,0,0,modes,false))
    set(Cmodes,0,0,modes);

  int x=0, y=0;
  if (source->get(Cprecincts,discard_levels,(transpose)?1:0,y,false) &&
      source->get(Cprecincts,discard_levels,(transpose)?0:1,x,false))
    { // Copy precinct dimensions
      set(Cprecincts,0,0,y); set(Cprecincts,0,1,x);
      for (n=1; source->get(Cprecincts,discard_levels+n,(transpose)?1:0,y,
                            false,false) &&
                source->get(Cprecincts,discard_levels+n,(transpose)?0:1,x,
                            false,false); n++)
        { set(Cprecincts,n,0,y); set(Cprecincts,n,1,x); }
    }

  float weight=0.0f;
  if (source->get(Cweight,0,0,weight,false))
    set(Cweight,0,0,weight);
  for (n=0; source->get(Clev_weights,n,0,weight,false,false); n++)
    set(Clev_weights,n,0,weight);
  for (n=0; source->get(Cband_weights,n,0,weight,false,false); n++)
    set(Cband_weights,n,0,weight);
  
  float vis_floor=0.0f;
  if (source->get(Cvis,0,0,vis_floor,false))
    set(Cvis,0,0,vis_floor);
  
  int max_bytes=0;
  for (n=0; source->get(Creslengths,n,0,max_bytes,false,false); n++)
    set(Creslengths,n,0,max_bytes);
  
  int tgt_comp_idx=0;
  for (n=0; source->get(Cagglengths,n,0,tgt_comp_idx,false,false); n++)
    set(Cagglengths,n,0,tgt_comp_idx);
}

/*****************************************************************************/
/*                      cod_params::write_marker_segment                     */
/*****************************************************************************/

int
  cod_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  cod_params *ref = (cod_params *) last_marked;
  int length, n;
  bool use_ycc=false, use_sop=false, use_eph=false;
  bool align_last_x=false, align_last_y=false;
  int mct_flags=0, layers=0, order=0;
  bool reversible=false, use_precincts=false;
  int levels=0, ads_idx=0, dfs_idx=0;
  int kernels=0, atk_idx=0, blk_y=0, blk_x=0, modes=0;
  int ppx=0, ppy=0;

  if (tpart_idx != 0)
    return 0;

  // Collect most of the parameters.
  if (!(get(Cycc,0,0,use_ycc) && get(Cmct,0,0,mct_flags) &&
        get(Clayers,0,0,layers) && get(Cuse_sop,0,0,use_sop) &&
        get(Cuse_eph,0,0,use_eph) && get(Corder,0,0,order) &&
        get(Calign_blk_last,0,0,align_last_y) &&
        get(Calign_blk_last,0,1,align_last_x) &&
        get(Clevels,0,0,levels) &&
        get(Cdfs,0,0,dfs_idx) && get(Cads,0,0,ads_idx)  &&
        get(Creversible,0,0,reversible) && get(Ckernels,0,0,kernels) &&
        get(Catk,0,0,atk_idx) && get(Cblk,0,0,blk_y) && get(Cblk,0,1,blk_x) &&
        get(Cmodes,0,0,modes) && get(Cuse_precincts,0,0,use_precincts)))
    { KDU_ERROR_DEV(e,91); e <<
        KDU_TXT("Unable to write COD marker segment yet! Some info missing.  "
        "Perhaps you forgot to call `kdu_params::finalize_all'.");
    }
  if ((levels < 0) || (levels > 32))
    { KDU_ERROR_DEV(e,0x21101303); e <<
      KDU_TXT("Attempting to write COD/COC marker segment with too many "
              "DWT levels -- max value is 32!");
    }
  
  if (ref != NULL)
    { // See if we can skip marker segment generation.
      if (ref->compare(Cycc,0,0,use_ycc) &&
          ref->compare(Cmct,0,0,mct_flags) &&
          ref->compare(Clayers,0,0,layers) &&
          ref->compare(Cuse_sop,0,0,use_sop) &&
          ref->compare(Cuse_eph,0,0,use_eph) &&
          ref->compare(Corder,0,0,order) &&
          ref->compare(Calign_blk_last,0,0,align_last_y) &&
          ref->compare(Calign_blk_last,0,1,align_last_x) &&
          ref->compare(Clevels,0,0,levels) &&
          ref->compare(Cads,0,0,ads_idx) && ref->compare(Cdfs,0,0,dfs_idx) &&
          ((ads_idx == 0) || (ref->tile_idx >= 0)) &&
          ((dfs_idx == 0) || (this->tile_idx >= 0)) &&
          ref->compare(Creversible,0,0,reversible) &&
          ref->compare(Ckernels,0,0,kernels) &&
          ref->compare(Catk,0,0,atk_idx) &&
          ref->compare(Cblk,0,0,blk_y) && ref->compare(Cblk,0,1,blk_x) &&
          ref->compare(Cmodes,0,0,modes) &&
          ref->compare(Cuse_precincts,0,0,use_precincts))
        { // So far, so good. Just need to check on precincts now.
          if (!use_precincts)
            return 0; // Object identical to reference.
          for (n=0; n <= levels; n++)
            if (!(get(Cprecincts,n,0,ppy) && get(Cprecincts,n,1,ppx) &&
                  ref->compare(Cprecincts,n,0,ppy) &&
                  ref->compare(Cprecincts,n,1,ppx)))
              break;
          if (n > levels)
            return 0; // Object identical to reference.
        }
    }

  if ((ads_idx != 0) && (tile_idx >= 0) &&
      ((ref==NULL) || !ref->compare(Clevels,0,0,levels)))
    { KDU_ERROR(e,0x25040501); e <<
        KDU_TXT("Illegal combination of `Clevels' and `Cads' values "
                "detected.  JPEG2000 Part-2 codestreams record the ADS "
                "table index associated with the `Cads' parameter in "
                "a tile-specific COD or COC marker segment in place of "
                "the `Clevels' value.  Thus, you cannot use a different "
                "`Clevels' value to the default inherited from a COD/COC "
                "marker segment with broader scope, while also "
                "specifying arbitrary decomposition styles via "
                "`Cads' and/or `Cdecomp'.");
    }
  if ((dfs_idx != 0) && (tile_idx < 0) && (comp_idx >= 0) &&
      ((ref==NULL) || !ref->compare(Clevels,0,0,levels)))
    { KDU_ERROR(e,0x24050500); e <<
        KDU_TXT("Illegal combination of `Clevels' and `Cdfs' values "
                "detected.  JPEG2000 Part-2 codestreams record the DFS "
                "table index associated with the `Cdfs' parameter in "
                "a main header COC marker segment in place of the `Clevels' "
                "value.  Thus, you cannot use a arbitrary downsampling "
                "factor styles while also providing a new value for "
                "`Clevels'.");
    }
  int ext_bypass = 0;
  if ((modes & (Cmodes_BYPASS_E1 | Cmodes_BYPASS_E2)) &&
      (modes & Cmodes_BYPASS))
    { 
      if (modes & Cmodes_BYPASS_E1)
        ext_bypass = 1;
      if (modes & Cmodes_BYPASS_E2)
        ext_bypass += 2;
      ext_bypass = 4-ext_bypass;
    }
  modes &= ~(Cmodes_BYPASS_E1 | Cmodes_BYPASS_E2);
  
  // We are now committed to writing (or simulating) a marker segment.
  kdu_params *siz = access_cluster(SIZ_params); // We will need this.
  assert(siz != NULL);
  if ((layers < 0) || (layers >= (1<<16)))
    { KDU_ERROR_DEV(e,92); e <<
        KDU_TXT("Illegal number of quality layers, ") << layers << ".";
    }
  if ((levels < 0) || (levels > 32))
    { KDU_ERROR_DEV(e,93); e <<
        KDU_TXT("Illegal number of DWT levels, ") << levels <<
        KDU_TXT(". Legal range is 0 to 32!");
    }

  int component_bytes = (get_num_comps() <= 256)?1:2;

  if (comp_idx < 0)
    { // Generating a COD marker.
      length = 4 + 1 + (1+2+1);
      if (use_ycc)
        { // Check compatibility
          if (mct_flags != 0)
            { KDU_ERROR(e,0x04060700); e <<
                KDU_TXT("You cannot use a Part-1 colour transform "
                "(`Cycc=yes') at the same time as a Part-2 multi-component "
                "transform (`Cmct != 0').");
            }
          kdu_params *coc[3];
          coc[0] = access_relation(tile_idx,0,0,true);
          coc[1] = access_relation(tile_idx,1,0,true);
          coc[2] = access_relation(tile_idx,2,0,true);
          bool rev_test=false;
          coc[0]->get(Creversible,0,0,rev_test);
          int prec_test=0;
          siz->get(Sprecision,0,0,prec_test);
          for (n=1; (n < 3) && (coc[n] != NULL); n++)
            {
              if (!(coc[n]->compare(Creversible,0,0,rev_test) &&
                    siz->compare(Sprecision,n,0,prec_test)))
                { KDU_ERROR(e,94); e <<
                    KDU_TXT("You cannot use a colour transform "
                    "unless the first 3 image components have identical "
                    "bit-depths and are either all reversible or all "
                    "irreversible.");
                }
            }
        }
    }
  else
    { // Generating a COC marker.
      length = 4 + component_bytes + 1;
    }
  length += 5 + use_precincts*(levels+1);
  if (ext_bypass != 0)
    length += 2; // Need to include 2-byte SXcod field
  if (out == NULL)
    return length;

  // Now we are committed to actually writing the marker segment.
  int profile = 2; siz->get(Sprofile,0,0,profile);
  if (tile_idx >= 0)
    { // Check for profile compatibility.
      if (profile == 0)
        { KDU_WARNING(w,0); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  COD/COC marker segments may only appear in the "
            "main header of a Profile-0 code-stream.  You should set "
            "\"Sprofile\" to 1 or 2.  Problem detected in tile ")
            << tile_idx << ".";
        }
      else if ((profile >= Sprofile_CINEMA2K) &&
               (profile <= Sprofile_IMF))
        { KDU_ERROR(e,0x11110807); e <<
          KDU_TXT("Profile violation detected.  COD/COC marker segments may "
                  "only appear in the main header of a code-stream marked "
                  "with one of the Digital Cinema profiles (CINEMA2K, "
                  "CINEMA4K, CINEMA2S, CINEMA4S or CINEMASS) or with a "
                  "BROADCAST or IMF profile.");
        }
    }
  if ((comp_idx >= 0) && (profile >= Sprofile_CINEMA2K) &&
      (profile <= Sprofile_IMF) &&
      (ref != NULL) && !ref->compare(Clevels,0,0,levels))
    { KDU_ERROR(e,0x26041004); e <<
        KDU_TXT("Profile violation detected.  About to write a "
                "component-specific COD/COC marker segment, which identifies "
                "a different number of DWT decomposition levels (Clevels) "
                "to that identified in the main COD marker segment.  This is "
                "not legal for the Digital Cinema, BROADCAST or IMF "
                "profiles.");
    }
  if ((comp_idx >= 0) && (profile >= Sprofile_CINEMA2K) && 
      (profile <= Sprofile_IMF) && (ref != NULL) &&
      !(ref->compare(Cblk,0,0,blk_y) && ref->compare(Cblk,0,1,blk_x)))
    { KDU_ERROR(e,0x26041005); e <<
      KDU_TXT("Profile violation detected.  About to write a "
              "component-specific COD/COC marker segment, which identifies "
              "different code-block dimensions to those identified in the "
              "main COD marker segment.  This is not legal for code-streams "
              "marked with a Digital Cinema, BROADCAST or IMF profile.");
    }

  if (atk_idx == 0)
    {
      if ((reversible && (kernels != Ckernels_W5X3)) ||
          ((!reversible) && (kernels != Ckernels_W9X7)))
        { KDU_ERROR(e,0x19050501); e <<
            KDU_TXT("Illegal `Ckernels' value found when preparing to "
                    "generate COD/COC marker segment.  Reversible processes "
                    "must use the W5X3 kernel, while irreversible processes "
                    "must use the W9X7 kernel, unless you have explicitly "
                    "identified a different (Part-2) kernel, via the `Catk' "
                    "attribute.");
        }
    }
  else if ((atk_idx < 2) || (atk_idx > 255))
    { KDU_ERROR(e,0x19050500); e <<
        KDU_TXT("Illegal ATK instance index found when preparing to generate "
                "COD/COC marker segment.  Legal values are in the range "
                "2 to 255.");
    }

  int acc_length = 0; // Accumulate actual length for verification purposes.

  if (comp_idx < 0)
    {
      int style = (((int) use_precincts) << 0) + (((int) use_sop) << 1) +
                  (((int) use_eph) << 2) + (((int) align_last_x) << 3) +
                  (((int) align_last_y) << 4) +
                  ((ext_bypass > 0)?32:0);
      acc_length += out->put(KDU_COD);
      acc_length += out->put((kdu_uint16)(length-2));
      acc_length += out->put((kdu_byte) style);
      acc_length += out->put((kdu_byte) order);
      acc_length += out->put((kdu_uint16) layers);
      acc_length += out->put((kdu_byte)(mct_flags | ((use_ycc)?1:0)));
    }
  else
    {
      int style = (((int) use_precincts) << 0) +
                  ((ext_bypass > 0)?32:0);
      acc_length += out->put(KDU_COC);
      acc_length += out->put((kdu_uint16)(length-2));
      if (component_bytes == 1)
        acc_length += out->put((kdu_byte) comp_idx);
      else
        acc_length += out->put((kdu_uint16) comp_idx);
      acc_length += out->put((kdu_byte) style);
    }

  int xcb, ycb=0;

  if (((xcb = int2log(blk_x)) < 0) || ((ycb = int2log(blk_y)) < 0))
    { KDU_ERROR_DEV(e,95); e <<
        KDU_TXT("Maximum code-block dimensions must be powers of 2!");
    }
  if ((xcb < 2) || (ycb < 2) || ((xcb+ycb) > 12))
    { KDU_ERROR_DEV(e,96); e <<
        KDU_TXT("Maximum code-block dimensions must be no less than 4 "
        "and the maximum code-block area must not exceed 4096 samples!");
    }

  if (tile_idx < 0)
    { // Record either `Clevels' or `Cdfs'
      if ((dfs_idx == 0) || (comp_idx < 0))
        acc_length += out->put((kdu_byte) levels);
      else
        acc_length += out->put((kdu_byte)(0x80 | dfs_idx));
    }
  else
    { // Record either `Clevels' or `Cads'
      if (ads_idx == 0)
        acc_length += out->put((kdu_byte) levels);
      else
        acc_length += out->put((kdu_byte)(0x80 | ads_idx));
    }

  acc_length += out->put((kdu_byte)(xcb-2));
  acc_length += out->put((kdu_byte)(ycb-2));
  acc_length += out->put((kdu_byte) modes);
  acc_length += out->put((kdu_byte)((atk_idx==0)?kernels:atk_idx));

  if (use_precincts)
    for (n=0; n <= levels; n++)
      {
        int ppx=0, ppy=0;

        if (!(get(Cprecincts,levels-n,0,ppy) &&
              get(Cprecincts,levels-n,1,ppx)))
          { KDU_ERROR_DEV(e,98); e <<
              KDU_TXT("No precinct dimensions supplied for COD/COC!");
          }
        if (((ppx = int2log(ppx)) < 0) || ((ppy = int2log(ppy)) < 0))
          { KDU_ERROR_DEV(e,99); e <<
              KDU_TXT("Precinct dimensions must be exact powers of 2!");
          }
        if ((ppx > 15) || (ppy > 15))
          { KDU_ERROR_DEV(e,100); e <<
              KDU_TXT("Precinct dimensions may not exceed 2^15!");
          }
        acc_length += out->put((kdu_byte)(ppx+(ppy<<4)));
      }
  
  if (ext_bypass > 0)
    { 
      kdu_uint16 sx_cod = (kdu_uint16) ext_bypass;
      acc_length += out->put(sx_cod);
    }

  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                     cod_params::check_marker_segment                      */
/*****************************************************************************/

bool
  cod_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if (code == KDU_COD)
    {
      c_idx = -1;
      return true;
    }
  if ((code == KDU_COC) && (num_bytes >= 2))
    {
      c_idx = *(bytes++);
      if (num_comps > 256)
        c_idx = (c_idx<<8) + *(bytes++);
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                      cod_params::read_marker_segment                      */
/*****************************************************************************/

bool
  cod_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;
  bool use_precincts=false;

  if (tpart_idx != 0)
    return false;
  bp = bytes;
  end = bp + num_bytes;
  int style = 0;
  if (comp_idx < 0)
    { // Need COD
      if (code != KDU_COD)
        return false;
      try {
          style = kdu_read(bp,end,1);
          if (style != (style & 63))
            { KDU_ERROR(e,101); e <<
                KDU_TXT("Malformed COD marker segment encountered. "
                "Invalid \"Scod\" value!");
            }
          set(Cuse_precincts,0,0,use_precincts = (style&1)?true:false);
          set(Cuse_sop,0,0,(style&2)?true:false);
          set(Cuse_eph,0,0,(style&4)?true:false);
          set(Calign_blk_last,0,1,(style&8)?true:false);
          set(Calign_blk_last,0,0,(style&16)?true:false);
          set(Corder,0,0,kdu_read(bp,end,1));
          set(Clayers,0,0,kdu_read(bp,end,2));
          int sgcod = kdu_read(bp,end,1);
          set(Cycc,0,0,(sgcod & 1)?true:false);
          set(Cmct,0,0,(sgcod & 1)?0:(sgcod & 6));
        } // End of try block.
      catch (kdu_byte *)
        { KDU_ERROR(e,102); e <<
            KDU_TXT("Malformed COD marker segment encountered. "
            "Marker segment is too small.");
        }
    }
  else
    { // Need COC for success
      int which_comp;

      if (code != KDU_COC)
        return false;
      if (get_num_comps() <= 256)
        which_comp = *(bp++);
      else
        { which_comp = *(bp++); which_comp = (which_comp<<8) + *(bp++); }
      if (which_comp != comp_idx)
        return false;
      try {
        style = kdu_read(bp,end,1);
        if (style != (style & 33))
          { KDU_ERROR(e,103); e <<
              KDU_TXT("Malformed COC marker segment. "
              "Invalid \"Scoc\" value!");
          }
        set(Cuse_precincts,0,0,use_precincts = (style&1)?true:false);
        } // End of try block.
      catch (kdu_byte *)
        { KDU_ERROR(e,104); e <<
            KDU_TXT("Malformed COC marker segment encountered. "
            "Marker segment is too small.");
        }
    }

  if (tile_idx >= 0)
    { // Check for profile compatibility.
      kdu_params *siz = access_cluster(SIZ_params);
      assert(siz != NULL);
      int profile = 2; siz->get(Sprofile,0,0,profile);
      if (profile == 0)
        { KDU_WARNING(w,3); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  COD/COC marker segments may only appear in the "
            "main header of a Profile-0 code-stream.  You should set "
            "\"Sprofile\" to 1 or 2.  Problem detected in tile ")
            << tile_idx << ".";
        }
    }

  try {
    bool reversible;
    int levels, n;

    levels = kdu_read(bp,end,1);
    if (levels & 0x80)
      {
        if (tile_idx < 0)
          {
            set(Cdfs,0,0,(levels & 0x7F));
            set(Cads,0,0,0);
          }
        else
          set(Cads,0,0,(levels & 0x7F));
        if (!get(Clevels,0,0,levels))
          { KDU_ERROR(e,0x21101304); e <<
            KDU_TXT("Found DFS (downsampling factor style) or ADS (arbitrary "
                    "decomposition style) extension information in a COD/COC "
                    "marker segment, where the number of decomposition "
                    "levels should be recorded instead -- DFS/ADS info "
                    "is allowed only where the number of levels can be "
                    "inherited from other COD/COC marker segments.");
          }
      }
    else
      { 
        set(Cads,0,0,0);
        if (tile_idx < 0)
          set(Cdfs,0,0,0);
        if (levels > 32)
          { KDU_ERROR(e,0x21101305); e <<
            KDU_TXT("Illegal number of DWT levels recorded in COD/COC "
                    "marker segment -- max value is 32.  Codestream is "
                    "likely corrupt!");
          }
        set(Clevels,0,0,levels);
      }

    int blk_rx = 2 + kdu_read(bp,end,1);
    int blk_ry = 2 + kdu_read(bp,end,1);
    set(Cblk,0,1,(1<<blk_rx));
    set(Cblk,0,0,(1<<blk_ry));
    if ((blk_rx > 10) || (blk_ry > 10) ||
        ((blk_rx+blk_ry) > 12))
      { KDU_ERROR(e,0x13051302); e <<
        KDU_TXT("Illegal code-block width/height exponents found in COD/COC "
                "marker segment.  Code-block dimensions must be powers of 2, "
                "in the range 4-1024, whose product lies in the range "
                "16-4096.");
      }    
    kdu_byte modes = kdu_read(bp,end,1);
    if (modes & ~63)
      { KDU_ERROR(e,0x18031201); e <<
          KDU_TXT("Unrecognized code-block style byte found in "
                  "COD/COC marker segment.  Most significant 2 bits "
                  "should be 0.");
      }
    int xforms = kdu_read(bp,end,1);
    if (xforms >= 2)
      {
        set(Catk,0,0,xforms);
        set(Ckernels,0,0,Ckernels_ATK);
      }
    else
      {
        set(Catk,0,0,0);
        set(Creversible,0,0,reversible=(xforms==1));
        set(Ckernels,0,0,(reversible)?Ckernels_W5X3:Ckernels_W9X7);
      }
    if (use_precincts)
      for (n=0; n <= levels; n++)
        {
          int ppx, ppy;

          ppx = kdu_read(bp,end,1);
          ppy = ppx >> 4;
          ppx &= 0x0F;
          set(Cprecincts,levels-n,0,1<<ppy);
          set(Cprecincts,levels-n,1,1<<ppx);
        }
    if (style & 32)
      { 
        int sx_cod = kdu_read(bp,end,2);
        int ext_bypass = sx_cod & 3;
        if ((ext_bypass != 0) && (modes & Cmodes_BYPASS))
          { 
            if ((4-ext_bypass) & 2)
              modes |= Cmodes_BYPASS_E2;
            if ((4-ext_bypass) & 1)
              modes |= Cmodes_BYPASS_E1;
          }
        if (sx_cod & ~3)
          { KDU_ERROR(e,0x17031205); e <<
              KDU_TXT("Unrecognized block-coding extensions flags found in "
                      "COD/COC marker segment -- value is ")
              << sx_cod << ".";
          }
      }
    set(Cmodes,0,0,modes);
    if (bp != end)
      { KDU_ERROR(e,106); e <<
          KDU_TXT("Malformed COD/COC marker segment encountered. The final ")
          << (int)(end-bp) <<
          KDU_TXT(" bytes were not consumed!");
      }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,107);  e <<
        KDU_TXT("Malformed COD/COC marker segment encountered. "
        "Marker segment is too small.");
    }

  return true;
}

/*****************************************************************************/
/*                            cod_params::finalize                           */
/*****************************************************************************/

void
  cod_params::finalize(bool after_reading)
{
  
  int n, val=0, ads_idx=0, dfs_idx=0, decomp=0;
  bool bval=false;

  // Find `siz' and current profile and dimension attributes
  kdu_params *siz = access_cluster(SIZ_params);
  int profile = 0;  siz->get(Sprofile,0,0,profile);
  bool dcinema = (profile==Sprofile_CINEMA2K) || (profile==Sprofile_CINEMA4K);
  bool dcinema2 = (profile==Sprofile_CINEMA2S) || (profile==Sprofile_CINEMA4S);
  bool dstorage = (profile==Sprofile_CINEMASS);
  bool broadcast = (profile == Sprofile_BROADCAST);
  bool imf = (profile == Sprofile_IMF);
  int broadcast_rev=0, imf_rev=0;
  if (broadcast)
    siz->get(Sbroadcast,0,2,broadcast_rev);
  if (imf)
    siz->get(Simf,0,2,imf_rev);
  kdu_coords image_span, image_off;
  siz->get(Ssize,0,0,image_span.y);     siz->get(Ssize,0,1,image_span.x);
  siz->get(Sorigin,0,0,image_off.y); siz->get(Sorigin,0,1,image_off.x);
  image_span -= image_off; // We need this to test compliance for `dstorage'
  
  // Configure missing parameters
  if (!after_reading)
    {
      if (!get(Clayers,0,0,val))
        set(Clayers,0,0,1);
      else if (val > 16384)
        { KDU_ERROR(e,108); e <<
            KDU_TXT("Illegal number of quality layers, ") << val <<
            KDU_TXT(", detected by coding parameter sub-system; legal "
            "code-streams may have no more than 16384 quality layers.");
        }
      if (!get(Cuse_sop,0,0,bval))
        { 
          bval = dstorage;
          set(Cuse_sop,0,0,bval);
        }
      if (!get(Cuse_eph,0,0,bval))
        { 
          bval = dstorage;
          set(Cuse_eph,0,0,bval);
        }
      if (!get(Calign_blk_last,0,0,val))
        {
          assert(!get(Calign_blk_last,0,1,val));
          set(Calign_blk_last,0,0,0);
          set(Calign_blk_last,0,1,0);
        }
      if (!get(Clevels,0,0,val))
        set(Clevels,0,0,5);

      bool need_dfs=false, need_ads=false;
      for (n=0; get(Cdecomp,n,0,decomp,false,false); n++)
        { // Check decomposition structural information supplied here
          if ((decomp & 3) != 3)
            need_dfs = true;
          if ((decomp & ~3) != 0)
            need_ads = true;
        }
  
      if (need_dfs)
        {
          if (!get(Cdfs,0,0,dfs_idx,false))
            set(Cdfs,0,0,dfs_idx=find_suitable_dfs_idx());
        }
      else if (n > 0)
        set(Cdfs,0,0,dfs_idx=0); // Have new decomp style with no need for DFS

      if (need_ads)
        {
          if (!get(Cads,0,0,ads_idx,false))
            set(Cads,0,0,ads_idx=find_suitable_ads_idx());
        }
      else if (n > 0)
        set(Cads,0,0,ads_idx=0); // Have new decomp style with no need for ADS
    }

  if (!get(Cads,0,0,ads_idx))
    ads_idx = 0;
  if (!get(Cdfs,0,0,dfs_idx))
    dfs_idx = 0;
  if (((ads_idx != 0) || (dfs_idx != 0)) && after_reading)
    { // Use ADS and/or DFS to derive `Cdecomp' information
      kdu_params *ads = NULL;
      if ((ads_idx > 0) &&
          (((ads = access_cluster(ADS_params)) == NULL) ||
           ((ads = ads->access_relation(tile_idx,-1,ads_idx,true)) == NULL)))
        { KDU_ERROR(e,0x2450501); e <<
            KDU_TXT("ADS table index encountered while finalizing COD/COD "
            "marker segment information does not refer to accessible "
            "ADS (Arbitrary Decomposition Styles) information.");
        }
      kdu_params *dfs = NULL;
      if ((dfs_idx > 0) &&
          (((dfs = access_cluster(DFS_params)) == NULL) ||
           ((dfs = dfs->access_relation(-1,-1,dfs_idx,true)) == NULL)))
        { KDU_ERROR(e,0x2450502); e <<
            KDU_TXT("DFS table index encountered while finalizing COD/COD "
            "marker segment information does not refer to accessible "
            "DFS (Downsampling Factor Styles) information.");
        }

      if ((comp_idx < 0) && (tile_idx >= 0) && (ads != NULL))
        { // See if we should defer resolution of the decomposition structure
          // until we are working with an actual tile-component.
          for (n=0; n < num_comps; n++)
            if (access_unique(tile_idx,n) == NULL)
              break;
          if (n == num_comps)
            {
              ads = dfs = NULL;
              ads_idx = dfs_idx = 0;
            }
        }
      if ((dfs != NULL) || (ads != NULL))
        derive_decomposition_structure(dfs,ads);
    }
  
  if (!get(Cdecomp,0,0,decomp))
    set(Cdecomp,0,0,decomp=3); // Default Mallat structure

  if (!after_reading)
    {
      if (!get(Cads,0,0,ads_idx))
        set(Cads,0,0,ads_idx=0);
      if (!get(Cdfs,0,0,dfs_idx))
        set(Cdfs,0,0,dfs_idx=0);
    }

  if (dfs_idx > 0)
    validate_dfs_data(dfs_idx);
  if ((tile_idx >= 0) && (ads_idx > 0))
    validate_ads_data(ads_idx);

  if (!after_reading)
    {
      if ((comp_idx < 0) && (tile_idx < 0) && (dfs_idx > 0))
        { // Need to instantiate phsical objects in each component to make sure
          // the non-trivial `Cdfs' value in this cluster head actually gets
          // written in component-specific COC marker segments, as required.
            for (int c=0; c < num_comps; c++)
              access_relation(-1,c,0,false); // Forces creation of new object
        }

      if ((tile_idx < 0) && (ads_idx > 0))
        { // Need to instantiate physical objects in each tile to make sure the
          // non-trivial `Cads' value in this main header object actually
          // gets written in a tile-specific COD/COC marker segment.  This is
          // because `Cads' cannot be written in a main header COD/COC marker
          // segment.
          for (int t=0; t < num_tiles; t++)
            access_relation(t,comp_idx,0,false);// Force creation of new object
        }
    }

  // Check for one or another of Catk, Ckernels, Creversible being specified
  // explicitly for this object -- may require adjusting the other two
  // quantities, if incompatible.
  bool reversible=false;
  int kernels=0, atk_idx=0;
  if (!get(Catk,0,0,atk_idx))
    set(Catk,0,0,atk_idx=0); // Can't make a non-trivial default for ATK index
  if (get(Catk,0,0,atk_idx,false) && (atk_idx != 0))
    { // ATK provided specifically for this tile-component.
      if ((!get(Ckernels,0,0,kernels,false)) || (kernels != Ckernels_ATK))
        set(Ckernels,0,0,kernels=Ckernels_ATK);
    }
  else if (get(Ckernels,0,0,kernels,false) && (kernels != Ckernels_ATK))
    { // One of the Part-1 kernels specified explicitly for this tile-component
      if ((!get(Catk,0,0,atk_idx,false)) || (atk_idx != 0))
        set(Catk,0,0,atk_idx=0);
      if (kernels == Ckernels_W5X3)
        {
          if ((!get(Creversible,0,0,reversible,false)) || !reversible)
            set(Creversible,0,0,reversible=true);
        }
      else if (kernels == Ckernels_W9X7)
        {
          if ((!get(Creversible,0,0,reversible,false)) || reversible)
            set(Creversible,0,0,reversible=false);
        }
      else
        assert(0); // Should not be possible
    }
  else if (get(Creversible,0,0,reversible,false) &&
           ((!get(Catk,0,0,atk_idx)) || (atk_idx == 0)))
    { // Reversibility explicitly set here, and there is no ATK index
      if ((!get(Ckernels,0,0,kernels)) ||
          (kernels != ((reversible)?Ckernels_W5X3:Ckernels_W9X7)))
        set(Ckernels,0,0,kernels=((reversible)?Ckernels_W5X3:Ckernels_W9X7));
    }

  // Find reversibility from ATK info if necessary
  if (atk_idx != 0)
    {
      bool k_rev=false;
      kdu_params *atk = access_cluster(ATK_params);
      if ((atk == NULL) ||
          ((atk = atk->access_relation(tile_idx,-1,atk_idx,true)) == NULL) ||
          (!atk->get(Kreversible,0,0,k_rev)))
        { KDU_ERROR(e,0x19050502); e <<
            KDU_TXT("Unable to access the `Kreversible' attribute associated "
            "with the `Catk' index found while finalizing a COD/COC marker "
            "segment.  The value of the `Catk' index is")
            << " " << atk_idx;
        }
      if ((!get(Creversible,0,0,reversible,false)) || (reversible != k_rev))
        set(Creversible,0,0,reversible=k_rev);
    }

  // Install default values for Ckernels and Creversible if necessary
  if ((!get(Creversible,0,0,reversible)) && !after_reading)
    set(Creversible,0,0,reversible=(broadcast_rev||imf_rev));
  if ((!get(Ckernels,0,0,kernels)) && !after_reading)
    set(Ckernels,0,0,kernels=(reversible)?Ckernels_W5X3:Ckernels_W9X7);

  // Check for compatibility of Ckernels, Catk, Creversible
  if (atk_idx == 0)
    {
      if ((reversible && (kernels != Ckernels_W5X3)) ||
          ((!reversible) && (kernels != Ckernels_W9X7)))
        { KDU_ERROR(e,0x19050503); e <<
            KDU_TXT("Illegal `Ckernels' value found while finalizing a "
                    "COD/COC marker segment.  Reversible processes "
                    "must use the W5X3 kernel, while irreversible processes "
                    "must use the W9X7 kernel, unless you have explicitly "
                    "identified a different (Part-2) kernel, via the `Catk' "
                    "attribute.");
        }
    }
  else if ((atk_idx < 2) || (atk_idx > 255))
    { KDU_ERROR(e,0x19050504); e <<
        KDU_TXT("Illegal ATK instance index found when finalizing a "
                "COD/COC marker segment.  Legal values are in the range "
                "2 to 255.");
    }

  if (after_reading)
    return;

  // Now we can look at filling out the `Cmct' attribute
  int m_components = 0;  siz->get(Mcomponents,0,0,m_components);
  int mct_flags = 0; get(Cmct,0,0,mct_flags);
  if ((comp_idx < 0) && !after_reading)
    {
      mct_flags = 0;
      kdu_params *mco=NULL, *mcc_base=NULL;
      int num_stages = 0;
      if ((m_components > 0) && ((mco = access_cluster(MCO_params)) != NULL) &&
          ((mco = mco->access_relation(tile_idx,-1,0,true)) != NULL) &&
          mco->get(Mnum_stages,0,0,num_stages) && (num_stages > 0) &&
          ((mcc_base = access_cluster(MCC_params)) != NULL))
        {
          kdu_params *mcc;
          int inst_idx, stage_idx;
          for (stage_idx=0; stage_idx < num_stages; stage_idx++)
            if (mco->get(Mstages,stage_idx,0,inst_idx) &&
                ((mcc = mcc_base->access_relation(tile_idx,-1,inst_idx,
                                                  true)) != NULL))
              {
                int block_idx=0, xform_type=0;
                for (block_idx=0;
                     mcc->get(Mstage_xforms,block_idx,0,
                              xform_type,true,false,false);
                     block_idx++)
                  {
                    if ((block_idx == Mxform_DEP) ||
                        (block_idx == Mxform_MATRIX))
                      mct_flags |= Cmct_ARRAY;
                    else if (block_idx == Mxform_DWT)
                      mct_flags |= Cmct_DWT;
                  }
              }
        }
      set(Cmct,0,0,mct_flags);
    }
  bool use_ycc=false;
  if (!get(Cycc,0,0,use_ycc))
    { // Determine the default, based on component compatibility
      assert(comp_idx < 0);
      int components = get_num_comps();
      use_ycc = false; // No transform unless the following conditions are met
      if ((components >= 3) && (m_components == 0))
        {
          int precision=1, last_precision=1;
          bool rev=false, last_rev=false;
          int sub_x=1, last_sub_x=1, sub_y=1, last_sub_y=1, c;
          assert(siz != NULL);
          for (c=0; c < 3; c++, last_rev=rev, last_precision=precision,
               last_sub_x=sub_x, last_sub_y=sub_y)
            {
              kdu_params *coc = access_relation(tile_idx,c,0,true);
              if (!(coc->get(Creversible,0,0,rev) &&
                  siz->get(Sprecision,c,0,precision) &&
                  siz->get(Ssampling,c,0,sub_y) &&
                  siz->get(Ssampling,c,1,sub_x)))
                abort();
              if (c == 0)
                {
                  last_rev = rev; last_precision = precision;
                  last_sub_x = sub_x; last_sub_y = sub_y;
                }
              else if ((rev != last_rev) || (precision != last_precision) ||
                       (sub_x != last_sub_x) || (sub_y != last_sub_y))
                break;
            }
          if (c == 3)
            use_ycc = true; // All conditions satisfied for component transform
        }
      set(Cycc,0,0,use_ycc);
    }
  if (use_ycc && (this->get_num_comps() < 3))
    set(Cycc,0,0,false);
  if (use_ycc && (m_components != 0))
    set(Cycc,0,0,false);

  // Finally, consider code-blocks, precincts and the Digital Cinema profiles
  kdu_coords blk_size;
  if (!(get(Cblk,0,0,blk_size.y) && get(Cblk,0,1,blk_size.x)))
    {
      blk_size.x = blk_size.y = (dcinema || dcinema2)?32:64;
      set(Cblk,0,0,blk_size.y);  set(Cblk,0,1,blk_size.x);
    }
  int modes=0;
  if (!get(Cmodes,0,0,modes))
    set(Cmodes,0,0,modes=0);
  int progression_order=0;
  if (!get(Corder,0,0,progression_order))
    {
      progression_order =
        (dcinema||dcinema2||dstorage||broadcast||imf)?Corder_CPRL:Corder_LRCP;
      set(Corder,0,0,progression_order);
    }
  int dwt_levels=0;
  get(Clevels,0,0,dwt_levels);
  if (dwt_levels > 32)
    { KDU_ERROR(e,109); e <<
      KDU_TXT("Illegal number of DWT levels, ") << dwt_levels <<
      KDU_TXT(", detected by coding parameter sub-system; legal "
              "code-streams may have no more than 32 DWT levels in any given "
              "tile-component.");
    }

  if (get(Cprecincts,0,0,val,false,false))
    { // Precinct dimensions explicitly set for this tile-component.
      set(Cuse_precincts,0,0,true);
    }
  else if (dcinema || dcinema2 || broadcast || imf)
    { // Force the use of precincts for cinema/broadcast profiles
      for (int lev=0; lev <= dwt_levels; lev++)
        {
          set(Cprecincts,lev,0,(lev==dwt_levels)?128:256);
          set(Cprecincts,lev,1,(lev==dwt_levels)?128:256);
        }
      set(Cuse_precincts,0,0,true);
    }
  else if (!get(Cuse_precincts,0,0,bval))
    { // Default is not to use precincts.
      if (dstorage)
        { // Encourage compatibility with other cinema profiles
          for (int lev=0; lev <= dwt_levels; lev++)
            { 
              set(Cprecincts,lev,0,(lev==dwt_levels)?128:256);
              set(Cprecincts,lev,1,(lev==dwt_levels)?128:256);
            }
          set(Cuse_precincts,0,0,true);
        }
      else
        set(Cuse_precincts,0,0,false);
    }

  // Test for legal code-block dimensions; we do this also during the
  // `write_marker_segment' function, but that is not sufficient, because
  // violation of the maximum code-block bounds may cause memory allocation
  // errors inside the block encoding machinery which may be exercised
  // before the marker segment actually gets written.
  int xcb, ycb;
  if (((xcb=int2log(blk_size.x)) < 0) || ((ycb=int2log(blk_size.y)) < 0) ||
      (xcb < 2) || (ycb < 2) || ((xcb+ycb) > 12))
    { KDU_ERROR(e,0x13051301); e <<
      KDU_TXT("Illegal code-block dimensions supplied for `Cblk' parameter "
              "attribute -- code-block width and height values must be "
              "powers of 2 in the range 4 to 1024, whose product does not "
              "exceed 4096.");
    }
  
  // Perform profile-specific tests for legal parameters
  if (dcinema || dcinema2 || dstorage || broadcast || imf)
    { 
      get(Clayers,0,0,val);
      if ((val != 1) && (dcinema || broadcast || imf))
        { KDU_WARNING(w,0x26041006); w <<
          KDU_TXT("Profile violation detected.  The Digital Cinema profiles "
                  "CINEMA2K and CINEMA4K and the BROADCAST and IMF profiles "
                  "do not strictly allow the use of multiple "
                  "quality layers (i.e., Clayers should strictly be "
                  "1).  Letting it go for now, since you could strip the "
                  "extra quality layers later on to make it legal.");
        }
      else if ((val != 2) && dcinema2)
        { KDU_WARNING(w,0x19091303); w <<
          KDU_TXT("Profile violation detected.  The Scalable Digital Cinema "
                  "profiles CINEMA2S and CINEMA4S expect exactly two (2) "
                  "quality layers (i.e., Clayers should strictly be "
                  "2).  Letting it go for now, since you could strip the "
                  "extra quality layers later on to make it legal.");
        }
      else if ((val > 5) && dstorage)
        { KDU_WARNING(w,0x19091304); w <<
          KDU_TXT("Profile violation detected.  The Scalable Digital Cinema "
                  "\"long term storage\" profile CINEMASS expects at most "
                  "5 quality layers (i.e., Clayers should strictly be less "
                  "than 6).  Letting it go for now, since you could strip the "
                  "extra quality layers later on to make it legal.");
        }
      
      bool use_eph=false, use_sop=false;
      get(Cuse_eph,0,0,use_eph);  get(Cuse_sop,0,0,use_sop);
      if (dstorage && !(use_eph && use_sop))
        { KDU_WARNING(w,0x21091301); w <<
          KDU_TXT("Profile violation detected.  The \"long term storage\" "
                  "cinema profile requires EPH markers and SOP marker "
                  "segments to be included in the code-stream.  Letting "
                  "this go for now, since you could add these in later.");
        }
      else if ((dcinema || dcinema2) && (use_eph || use_sop))
        { KDU_WARNING(w,0x21091302); w <<
          KDU_TXT("Profile violation detected.  Neither EPH markers nor "
                  "SOP marker segments may be used with Digital Cinema "
                  "profiles CINEMA2K, CINEMA4K, CINEMA2S and CINEMA4S, or "
                  "with a BROADCAST profile.  Letting this go for now, "
                  "since you could remove these later.");
        }
    
      if ((dcinema || dcinema2 || imf) &&
          ((blk_size.x != 32) || (blk_size.y != 32)))
        { KDU_ERROR(e,0x11110802); e <<
          KDU_TXT("Profile violation detected.  The Digital "
                  "Cinema profiles CINEMA2K, CINEMA4K, CINEMA2S and the IMF "
                  "profiles require a code-block size of exactly 32x32.  You "
                  "have specified something different.");
        }
      if (dstorage && ((blk_size.x > 64) || (blk_size.y > 64)))
        { KDU_ERROR(e,0x19091305); e <<
          KDU_TXT("Profile violation detected.  The \"long term storage\" "
                  "cinema profile CINEMASS requires code-block dimensions "
                  "to be no larger than 64 in each direction.");
        }
      if (broadcast && ((blk_size.x < 32) || (blk_size.x > 128) ||
                        (blk_size.y < 32) || (blk_size.y > 64)))
        { KDU_ERROR(e,0x26041007); e <<
          KDU_TXT("Profile violation detected.  The BROADCAST profiles "
                  "require horizontal code-block sizes of 32, 64 or 128 and "
                  "vertical code-block sizes of 32 or 64.");
        }

      if (dstorage)
        { // Long term storage profile allows some mode options
          if (modes & (Cmodes_CAUSAL | Cmodes_RESET))
            { KDU_ERROR(e,0x20091301); e <<
              KDU_TXT("Profile violation detected.  Requested block coding "
                      "mode is incompatible with the use of the \"long "
                      "term storage\" digital cinema profile.");
            }
        }
      else if (modes != 0)
        { KDU_ERROR(e,0x26041008); e <<
          KDU_TXT("Profile violation detected.  Requested block coding mode "
                  "is incompatible with the use of Digital Cinema profiles "
                  "CINEMA2K, CINEMA4K, CINEMA2S or CINEMA4S and is also "
                  "incompatible with all BROADCAST and IMF profiles.");
        }
      
      for (int lev=0; lev <= dwt_levels; lev++)
        { 
          kdu_coords precinct_siz;
          get(Cprecincts,lev,0,precinct_siz.y);
          get(Cprecincts,lev,1,precinct_siz.x);
          if (!dstorage)
            { 
              kdu_coords reqd_siz(128,128);
              if (lev < dwt_levels)
                reqd_siz.x = reqd_siz.y = 256;
              if (precinct_siz != reqd_siz)
                { KDU_ERROR(e,0x11110806); e <<
                  KDU_TXT("Profile violation detected.  Requested precinct "
                          "dimensions are incompatible with the use of "
                          "Digital Cinema profiles CINEMA2K, CINEMA4K, "
                          "CINEMA2S, CINEMA4S or a BROADCAST or IMF profile.  "
                          "All precincts need to be of size 256x256 except "
                          "the lowest resolution level which needs 128x128");
                }
            }
          else
            { // The long-term storage profile is less restrictive
              if ((precinct_siz.x < blk_size.x) ||
                  (precinct_siz.y < blk_size.y))
                { KDU_ERROR(e,0x20091302); e <<
                  KDU_TXT("Profile violation detected.  Requested precinct "
                          "dimensions are incompatible with the \"long "
                          "term storage\" cinema profile CINEMASS.  Precinct "
                          "dimensions, as recorded in the COD marker segment, "
                          "must be no smaller than code-block dimensions in "
                          "this case.");
                }
            }
        }
      if ((this->comp_idx < 0) && (dcinema || dcinema2) &&
          !get(Creslengths,0,0,val))
        { KDU_WARNING(w,0x11110820); w <<
          KDU_TXT("Profile violation may occur without noticing, since you "
                  "have requested Digital Cinema profile CINEMA2K, CINEMA4K, "
                  "CINEMA2S or CINEMA4S, without providing a `Creslengths' "
                  "attribute.  The latter is generally required to be sure "
                  "that the compressed lengths of resolution/component "
                  "subsets conform to the tight Digital Cinema prescription.  "
                  "The actual allowed sizes depend upon the projected frame "
                  "rate, so are not enforced internally.  By way of example, "
                  "you can generate a legal CINEMA2K codestream for 24 fps by "
                  "supplying \"Creslengths=1302083\" together with "
                  "\"Creslengths:Cn=1041666\" replacing n=0,1,2.  For 48 fps, "
                  "use 651041 and 520833 instead.  For a CINEMA4K codestream "
                  "at 24 fps, use \"Creslengths=1302083\", together with "
                  "\"Creslenths:Cn=1302083,1041666\", with the same "
                  "replacements for 48 fps.  For a CINEMA2S codestream at, "
                  "48fps, use \"Creslengths=651041,0,1302083\" and "
                  "\"Creslengths:Cn=1520833,0,104166.  For more examples, "
                  "see the Kakadu \"Usage_Examples.txt\" file and be sure "
                  "to read the official size constraints defined for the "
                  "Digital Cinema profile that is of interest to you.");
        }
      int min_levels=1, max_levels=5;
      if ((profile==Sprofile_CINEMA2K) || (profile==Sprofile_CINEMA2S))
        { min_levels = 0; max_levels = 5; }
      else if ((profile==Sprofile_CINEMA4K) || (profile==Sprofile_CINEMA4S))
        { min_levels=1; max_levels = 6; }
      else if (profile == Sprofile_CINEMASS)
        { 
          max_levels = 33;
          min_levels = 0;
          while (((image_span.x>>min_levels) > 64) ||
                 ((image_span.y>>min_levels) > 64))
            min_levels++;
        }
      else if (imf)
        { // Much more tricky to work out allowed DWT levels for IMF
          int imf_size_class = 0;
          if ((image_span.x > 2048) || (image_span.y > 1556))
            { 
              imf_size_class = 1;
              if ((image_span.x > 4096) || (image_span.y > 3112))
                imf_size_class = 2;
            }
          max_levels += imf_size_class;
          if (imf_rev)
            { // Need to take tiling into account
              kdu_coords tile_span;
              siz->get(Stiles,0,0,tile_span.y);
              siz->get(Stiles,0,1,tile_span.x);
              if ((tile_span.x < image_span.x) || (tile_span.y < image_span.y))
                { // We have a multi-tile IMF profile
                  while ((max_levels > 4) &&
                         ((tile_span.x < (64<<max_levels)) ||
                          (tile_span.y < (64<<max_levels))))
                    max_levels--;
                }
            }
        }
      if ((dwt_levels < min_levels) || (dwt_levels > max_levels))
        { KDU_ERROR(e,0x06121409); e <<
          KDU_TXT("Profile violation detected.  The Digital Cinema, "
                  "BROADCAST and IMF profiles impose strict limits on the "
                  "number of DWT decomposition levels.  The maximum number of "
                  "levels is 5 for CINEMA2K, CINEMA2S and BROADCAST profiles, "
                  "6 for CINEMA4K and CINEMA4S profiles, and somewhere "
                  "between 4 and 7 for IMF profiles, depending on the tile "
                  "dimensions, if tiled, and image dimensions.  The minimum "
                  "number of levels is 1 for CINEMA4K, CINEMA4S, BROADCAST "
                  "and IMF profiles, while the \"long term storage\" "
                  "profile CINEMASS requires sufficient levels to ensure "
                  "that the LL subband's dimensions will be at most 64.");
        }
      if (progression_order != Corder_CPRL)
        { KDU_ERROR(e,0x2604100A); e <<
          KDU_TXT("Profile violation detected.  Digital Cinema, BROADCAST "
                  "and IMF profiles require the CPRL packet progression "
                  "order.");
        }
      if ((broadcast && (reversible != (broadcast_rev != 0))) ||
          (imf && (reversible != (imf_rev != 0))))
        { KDU_ERROR(e,0x2604100B); e <<
          KDU_TXT("Profile violation detected.  The reversibility of the "
                  "selected wavelet transform is incompatible with the "
                  "reversibility indicated via an `Sbroadcast' or "
                  "`Simf' attribute.");
        }
      if (reversible && (dcinema || dcinema2))
        { KDU_ERROR(e,0x20091303); e <<
          KDU_TXT("Profile violation detected.  Digital Cinema profiles "
                  "CINEMA2K, CINEMA4K, CINEMA2S and CINEMA4S, must use "
                  "the irreversible processing path, with the 9/7 DWT.");
          
        }
    }
}

/*****************************************************************************/
/*                     cod_params::find_suitable_dfs_idx                     */
/*****************************************************************************/

int
  cod_params::find_suitable_dfs_idx()
{
  kdu_params *rel;
  int dfs_idx=0;

  if (tile_idx >= 0)
    { // Must use main header DFS; can't redefine here
      rel = access_relation(-1,comp_idx,0);
      rel->get(Cdfs,0,0,dfs_idx);
      if (dfs_idx == 0)
        { KDU_ERROR(e,0x24050503); e <<
            KDU_TXT("You are attempting to define a decomposition "
            "structure within a tile, which involves a different "
            "downsampling structure (different primary subband "
            "decomposition -- first character code of each record in "
            "`Cdecomp' attribute) to that defined (implicitly or explicitly) "
            "for the main codestream header.  This is illegal.");
        }
      return dfs_idx;
    }

  int c, max_dfs_idx = 0;
  for (c=-1; c < comp_idx; c++)
    if (((rel = access_unique(-1,c,0)) != NULL) &&
        rel->get(Cdfs,0,0,dfs_idx) && (dfs_idx != 0))
      { // Found potential result
        max_dfs_idx = dfs_idx;

        int n=0, decomp1=3, decomp2=3;
        bool compatible_decomp = true;
        bool have_Cdecomp, have_Ddecomp;
        do {
            have_Cdecomp = get(Cdecomp,n,0,decomp1,false,false);
            have_Ddecomp = rel->get(Cdecomp,n,0,decomp2,false,false);
            if ((decomp1 & 3) != (decomp2 & 3))
              { compatible_decomp = false; break; }
            n++;
          } while (have_Cdecomp || have_Ddecomp);
        if (compatible_decomp)
          return dfs_idx;
      }

  // If we get here, we need to create a new DFS table
  dfs_idx = max_dfs_idx + 1;
  rel = access_cluster(DFS_params);
  if (rel != NULL)
    rel->access_relation(-1,-1,dfs_idx,false);

  return dfs_idx;
}

/*****************************************************************************/
/*                     cod_params::find_suitable_ads_idx                     */
/*****************************************************************************/

int
  cod_params::find_suitable_ads_idx()
{
  kdu_params *ads_root = access_cluster(ADS_params);
  int val, ads_idx, max_ads_idx = 0;
  kdu_params *ads = NULL;
  bool tile_specific = false;
  if ((ads = ads_root->access_unique(tile_idx,-1,0)) != NULL)
    tile_specific = true;
  else
    ads = ads_root;
  while (ads != NULL)
    {
      ads_idx = ads->get_instance();
      if (ads->get(Ddecomp,0,0,val) || ads->get(DOads,0,0,val) ||
          ads->get(DSads,0,0,val))
        { // ADS table non-empty
          if (ads_idx > max_ads_idx)
            {
              max_ads_idx = ads_idx;
          
              bool compatible_decomp = true;
              int n=0, decomp1=3, decomp2=3;
              bool have_Cdecomp, have_Ddecomp;
              do {
                  have_Cdecomp = get(Cdecomp,n,0,decomp1,false,false);
                  have_Ddecomp = ads->get(Ddecomp,n,0,decomp2,false,false);
                  if (decomp1 != decomp2)
                    { compatible_decomp = false; break; }
                  n++;
                } while (have_Cdecomp || have_Ddecomp);
              if (compatible_decomp)
                return ads_idx;
            }
        }
      ads = ads->access_next_inst();
      if ((ads == NULL) && tile_specific)
        { // Try looking in main header
          ads = ads_root;
          tile_specific = false;
        }
    }

  // If we get here, we need to create a new ADS table
  ads_idx = max_ads_idx + 1;
  ads_root->access_relation(tile_idx,-1,ads_idx,false);
  return ads_idx;
}

/*****************************************************************************/
/*                cod_params::derive_decomposition_structure                 */
/*****************************************************************************/

void
  cod_params::derive_decomposition_structure(kdu_params *dfs, kdu_params *ads)
{
  int n, val=0;
  if (ads == NULL)
    {
      assert(dfs != NULL); // Can't both be NULL
      for (n=0; dfs->get(DSdfs,n,0,val,true,false); n++)
        set(Cdecomp,n,0,val);
      return;
    }

  int sub_levs=1; // Default number of sub-levels
  int dfs_val=3; // Default is Mallat
  int code, next_code=0; // Default splitting code
  bool Oads_exhausted =
    ((ads == NULL) || !ads->get(DOads,0,0,sub_levs,true,false));
  bool Sads_exhausted =
    ((ads == NULL) || !ads->get(DSads,0,0,next_code,true,false));
  bool dfs_exhausted =
    ((dfs == NULL) || !dfs->get(DSdfs,0,0,dfs_val,true,false));
  val = 0;
  n = 0; // Index of current `DOads' and `DSdfs' parameters
  int code_idx = 1; // Index of next `DSads' parameter to be recovered
  do {
      val = dfs_val & 3;
      int k, num_primary_bands=0, num_secondary_bands=0;
      switch (val) {
          case 0: num_primary_bands = 0; break;
          case 1:
          case 2: num_primary_bands = 1; break;
          case 3: num_primary_bands = 3; break;
        };
      for (k=num_primary_bands-1; k >= 0; k--)
        {
          if (sub_levs == 1)
            continue;
          code = next_code;
          if ((!Sads_exhausted) &&
              !ads->get(DSads,code_idx++,0,next_code,true,false))
            Sads_exhausted = true;
          val |= (code << (2+k*10));
          if ((code == 0) || (sub_levs == 2))
            continue;

          num_secondary_bands = (code==3)?4:2;
          for (; num_secondary_bands > 0; num_secondary_bands--)
            {
              code = next_code;
              if ((!Sads_exhausted) &&
                  !ads->get(DSads,code_idx++,0,next_code,true,false))
                Sads_exhausted = true;
              val |= (code << (2+k*10+num_secondary_bands*2));
            }
        }
      set(Cdecomp,n,0,val);

      n++; // Advance to next index

      if ((!dfs_exhausted) && !dfs->get(DSdfs,n,0,dfs_val,true,false))
        dfs_exhausted = true;
      if ((!Oads_exhausted) && !ads->get(DOads,n,0,sub_levs,true,false))
        Oads_exhausted = true;
      if (Oads_exhausted && (sub_levs < 2))
        Sads_exhausted = true; // Will never read more Sads values
    } while (!(dfs_exhausted && Oads_exhausted && Sads_exhausted &&
               is_valid_decomp_terminator(val)));
}

/*****************************************************************************/
/*                       cod_params::validate_ads_data                       */
/*****************************************************************************/

void
  cod_params::validate_ads_data(int ads_idx)
{
  if (ads_idx == 0)
    return;
  kdu_params *ads = access_cluster(ADS_params);
  if ((ads == NULL) ||
      ((ads = ads->access_relation(tile_idx,-1,ads_idx,true)) == NULL))
    assert(0); // Should not be able to happen

  int n, val=0;
  bool validate = ads->get(Ddecomp,0,0,val);
  for (n=0; get(Cdecomp,n,0,val,true,false); n++)
    {
      if (!validate)
        ads->set(Ddecomp,n,0,val);
      else if (!ads->compare(Ddecomp,n,0,val))
        { KDU_ERROR(e,0x24050501); e <<
            KDU_TXT("Unacceptable interaction between ADS ("
            "Arbitrary Decomposition Style) and DFS (Downsampling Factor "
            "Styles) information in Part-2 codestream.  It makes no sense "
            "to use the same ADS table for two tile-components which have "
            "different downsampling factor styles, since downsampling "
            "styles have a strong effect on the interpretation of "
            "information recorded in the ADS marker segment.");
        }
    }

  if ((n > 0) && !is_valid_decomp_terminator(val))
    {
      char buf[21];  textualize_decomp(buf,val);
      KDU_ERROR(e,0x27040500) e <<
        KDU_TXT("Encountered invalid terminal `Cdecomp' attribute value")
        << ", \"" << buf << "\".  " <<
        KDU_TXT("Terminal splitting styles must have identical splitting "
        "instructions for all primary detail subbands (i.e., identical "
        "colon-separated sub-strings), in each of which all 2-bit splitting "
        "codes must be identical (i.e., sub-strings must consist of identical "
        "characters from the set, `-', `H', `V' and `B'.  The only exception "
        "to this rule is that where each primary subband is split only once, "
        "in which case it is sufficient for all primary subbands to be "
        "split once in the same direction (i.e., all `-', all `H--', all "
        "`V--' or all `B----').  These rules derive from the way in which "
        "Part-2 of the JPEG2000 standard extrapolates information found in "
        "ADS and DFS marker segments.");
    }
}

/*****************************************************************************/
/*                        cod_params::validate_dfs_data                      */
/*****************************************************************************/

void
  cod_params::validate_dfs_data(int dfs_idx)
{
  if (dfs_idx == 0)
    return;
  kdu_params *dfs = access_cluster(DFS_params);
  if ((dfs == NULL) ||
      ((dfs = dfs->access_relation(-1,-1,dfs_idx,true)) == NULL))
    assert(0); // Should not be able to happen

  int n, val=0;
  bool validate = dfs->get(DSdfs,0,0,val);
  for (n=0; get(Cdecomp,n,0,val,true,false); n++)
    {
      if (!validate)
        dfs->set(DSdfs,n,0,val&3);
      else if (!dfs->compare(DSdfs,n,0,val&3))
        { KDU_ERROR_DEV(e,0x24050502); e <<
            KDU_TXT("Incompatible `DSdfs' and `Cdecomp' values seem to have "
            "been created.  Should not be possible.");
        }
    }
}

/*****************************************************************************/
/* STATIC PUBLIC        cod_params::textualize_decomp                       */
/*****************************************************************************/

void
  cod_params::textualize_decomp(char *buf, int val)
{
  char *bp=buf;
  int b, num_primary_bands=0;

  switch (val & 3) {
      case 0:
        *(bp++) = '-'; num_primary_bands = 0; break;
      case 1:
        *(bp++) = 'H'; num_primary_bands = 1; break;
      case 2:
        *(bp++) = 'V'; num_primary_bands = 1; break;
      case 3:
        *(bp++) = 'B'; num_primary_bands = 3; break;
    }

  *(bp++) = '(';

  for (val>>=2, b=0; b < num_primary_bands; b++, val>>=10)
    {
      if (b > 0)
        *(bp++) = ':';
      int tmp, num_chars = 1;
      if ((val & 3) == 3)
        num_chars += 4;
      else if ((val & 3) != 0)
        num_chars += 2;
      for (tmp=val; num_chars > 0; num_chars--, tmp >>= 2)
        switch (tmp & 3) {
            case 0: *(bp++) = '-'; break;
            case 1: *(bp++) = 'H'; break;
            case 2: *(bp++) = 'V'; break;
            case 3: *(bp++) = 'B'; break;
          }
    }
  *(bp++) = ')';
  *bp = '\0';
  assert((bp-buf) <= 20);
}

/*****************************************************************************/
/*                      cod_params::custom_textualize_field                  */
/*****************************************************************************/

void
  cod_params::custom_textualize_field(kdu_message &output, const char *name,
                                      int field_idx, int val)
{
  if ((strcmp(name,Cdecomp) == 0) && (field_idx == 0))
    {
      char buf[21];
      textualize_decomp(buf,val);
      output << buf;
    }
}

/*****************************************************************************/
/*                        cod_params::custom_parse_field                     */
/*****************************************************************************/

int
  cod_params::custom_parse_field(const char *string, const char *name,
                                 int field_idx, int &val)
{
  if ((strcmp(name,Cdecomp) != 0) || (field_idx != 0))
    return (val = 0);
  const char *string_start = string;
  int num_primary_bands;
  switch (*string) {
      case '-': val = 0; num_primary_bands = 0; break;
      case 'H': val = 1; num_primary_bands = 1; break;
      case 'V': val = 2; num_primary_bands = 1; break;
      case 'B': val = 3; num_primary_bands = 3; break;
      default:
        return (val = 0); // Error condition; no valid characters
    };
  string++;
  if (*(string++) != '(')
    return (val = 0); // Error condition; no valid characters

  int b;
  for (b=0; b < num_primary_bands; b++)
    {
      if (b > 0)
        {
          if ((*string != ':') && (*string != ','))
            return (val = 0); // Error condition; no valid characters
          string++;
        }

      int n, sub_val, sub_chars, code;
      for (n=0, sub_chars=1, sub_val=0; n < sub_chars; n++, string++)
        {
          if (*string == '-')
            code = 0;
          else if (*string == 'H')
            code = 1;
          else if (*string == 'V')
            code = 2;
          else if (*string == 'B')
            code = 3;
          else
            return (val = 0); // Error condition
          sub_val += code << (2*n);
          if ((n == 0) && (string[1] != ':') &&
              (string[1] != ')') && (string[1] != ','))
            {
              sub_chars += (code > 0)?2:0;
              sub_chars += (code == 3)?2:0;
            }
        }
      val |= (sub_val << ((10*b)+2));
    }

  if (*(string++) != ')')
    return (val = 0); // Error condition

  return (int)(string-string_start);
}

/*****************************************************************************/
/* STATIC PUBLIC         cod_params::transpose_decomp                        */
/*****************************************************************************/

int
  cod_params::transpose_decomp(int val)
{
  if ((val & 3) == 3) // F0 == 3
    { // Swap LH and HL primary detail subband splitting descriptors
      val = (val & 0xFFC00003) |
        ((val & 0x00000FFC) << 10) | ((val >> 10) & 0x00000FFC);
    }

  if (((val >> 2) & 3) == 3) // F1 == 3
    { // Swap F3 (HL) with F4 (LH) secondary splitting descriptors
      val = (val & 0xFFFFFC3F) |
        ((val & 0x000000C0) << 2) | ((val >> 2) & 0x000000C0);
    }
  if (((val >> 12) & 3) == 3) // F6 == 3
    { // Swap F8 (HL) with F9 (LH) secondary splitting descriptors
      val = (val & 0xFFF0FFFF) |
        ((val & 0x00030000) << 2) | ((val >> 2) & 0x00030000);
    }
  if (((val >> 22) & 3) == 3) // F11 == 3
    { // Swap F13(HL) with F14(LH) secondary splitting descriptors
      val = (val & 0xC3FFFFFF) |
        ((val & 0x0C000000) << 2) | ((val >> 2) & 0x0C000000);
    }

  // Finally, swap the odd and even bit positions of F0 through F15
  val = ((val & 0x55555555) << 1) | ((val >> 1) & 0x55555555);

  return val;
}

/*****************************************************************************/
/* STATIC PUBLIC        cod_params::get_max_decomp_levels                    */
/*****************************************************************************/

void
  cod_params::get_max_decomp_levels(int decomp_val,
                                    int &horizontal_levels,
                                    int &vertical_levels)
{
  int val;
  int h1, v1, h2, v2;
  int num_hor1, num_vert1, num_hor2, num_vert2;
  int count1_h, count1_v, count2_h, count2_v, count3_h, count3_v;

  num_hor1 = (decomp_val & 1) + 1;
  num_vert1 = ((decomp_val >> 1) & 1) + 1;
  
  count1_h = num_hor1-1;
  count1_v = num_vert1-1;

  horizontal_levels = count1_h;
  vertical_levels = count1_v;

  decomp_val >>= 2;
  for (v1=0; v1 < num_vert1; v1++)
    for (h1=0; h1 < num_hor1; h1++)
      {
        if ((h1 == 0) && (v1 == 0))
          continue;

        val = decomp_val; decomp_val >>= 10;
        if ((val & 3) == 0)
          continue;

        num_hor2 = (val & 1) + 1;
        num_vert2 = ((val >> 1) & 1) + 1;

        count2_h = count1_h + (num_hor2-1);
        count2_v = count1_v + (num_vert2-1);

        for (v2=0; v2 < num_vert2; v2++)
          for (h2=0; h2 < num_hor2; h2++)
            {
              val >>= 2;
              count3_h = count2_h + (val & 1);
              count3_v = count2_v + ((val >> 1) & 1);

              if (count3_h > horizontal_levels)
                horizontal_levels = count3_h;
              if (count3_v > vertical_levels)
                vertical_levels = count3_v;
            }
      }
}

/*****************************************************************************/
/* STATIC PUBLIC        cod_params::expand_decomp_bands                      */
/*****************************************************************************/

int
  cod_params::expand_decomp_bands(int decomp_val, kdu_int16 descriptors[])
{
  int num_bands = 0;
  int val;
  int h1, v1, h2, v2, h3, v3;
  int num_hor1, num_vert1, num_hor2, num_vert2, num_hor3, num_vert3;
  int count1_h, count1_v, count2_h, count2_v, count3_h, count3_v;
  int high1_h, high1_v, high2_h, high2_v, high3_h, high3_v;

  num_hor1 = (decomp_val & 1) + 1;
  num_vert1 = ((decomp_val >> 1) & 1) + 1;
  
  count1_h = num_hor1-1;
  count1_v = num_vert1-1;

  decomp_val >>= 2;
  for (v1=0; v1 < num_vert1; v1++)
    for (h1=0; h1 < num_hor1; h1++)
      {
        high1_h = h1;
        high1_v = v1;
        if ((h1 == 0) && (v1 == 0))
          { // This is the subband which is passed on to the next DWT level
            descriptors[num_bands++] = (kdu_int16)
              (count1_h + (high1_h<<2) + (count1_v<<8) + (high1_v<<10));
            continue;
          }

        val = decomp_val; decomp_val >>= 10;
        if ((val & 3) == 0)
          { // This subband is refined no further
            descriptors[num_bands++] = (kdu_int16)
              (count1_h + (high1_h<<2) + (count1_v<<8) + (high1_v<<10));
            continue;
          }

        num_hor2 = (val & 1) + 1;
        num_vert2 = ((val >> 1) & 1) + 1;

        count2_h = count1_h + (num_hor2-1);
        count2_v = count1_v + (num_vert2-1);

        for (v2=0; v2 < num_vert2; v2++)
          for (h2=0; h2 < num_hor2; h2++)
            {
              high2_h = high1_h | (h2 << count1_h);
              high2_v = high1_v | (v2 << count1_v);

              val >>= 2;
              if ((val & 3) == 0)
                { // This subband is refined no further
                  descriptors[num_bands++] = (kdu_int16)
                    (count2_h + (high2_h<<2) + (count2_v<<8) + (high2_v<<10));
                  continue;
                }
              num_hor3 = (val & 1) + 1;
              num_vert3 = ((val >> 1) & 1) + 1;

              count3_h = count2_h + (num_hor3-1);
              count3_v = count2_v + (num_vert3-1);
              
              for (v3=0; v3 < num_vert3; v3++)
                for (h3=0; h3 < num_hor3; h3++)
                  {
                    high3_h = high2_h | (h3 << count2_h);
                    high3_v = high2_v | (v3 << count2_v);
                    descriptors[num_bands++] = (kdu_int16)
                    (count3_h + (high3_h<<2) + (count3_v<<8) + (high3_v<<10));
                  }
            }
      }
  assert(num_bands <= 49);
  return num_bands;
}

/*****************************************************************************/
/* STATIC PUBLIC     cod_params::is_valid_decomp_terminator                  */
/*****************************************************************************/

bool
  cod_params::is_valid_decomp_terminator(int val)
{
  if ((val & 3) == 3)
    {
      if ((((val >> 2) & 0x000003FF) != ((val >> 12) & 0x000003FF)) ||
          (((val >> 2) & 0x000003FF) != ((val >> 22) & 0x000003FF)))
        return false; // Different splitting operations for the three
                      // primary detail subbands.
    }
  if (((val >> 4) & 0x000000FF) == 0)
    return true; // At most one level of additional splitting

  switch ((val >> 2) & 3) {
      case 0: break;
      case 1:
        if (((val >> 4) & 0x0F) != 0x05)
          return false;
        break;
      case 2:
        if (((val >> 4) & 0x0F) != 0x0C)
          return false;
        break;
      case 3:
        if (((val >> 4) & 0xFF) != 0xFF)
          return false;
        break;
    }
  return true;
}


/* ========================================================================= */
/*                                ads_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          ads_params::new_object                           */
/*****************************************************************************/

kdu_params *ads_params::new_object()
{
  return new ads_params(); 
}

/*****************************************************************************/
/*                       ads_params::ads_params (no args)                    */
/*****************************************************************************/

ads_params::~ads_params() {}
ads_params::ads_params()
  : kdu_params(ADS_params,true,false,true,false,true)
{
  define_attribute(Ddecomp,
                   "This attribute is ultimately set so as to hold the same "
                   "information as the `Cdecomp' attribute of the COD/COC "
                   "marker segment whose `Cads' holds our instance index.  "
                   "Thus, for example, if `Cads'=3 then `Cdecomp' must be "
                   "identical to `Ddecomp:I3'.  This identification is "
                   "created by the internal machinery, however.  You should "
                   "not explicitly set `Ddecomp' values yourself.",
                   "C", MULTI_RECORD);
  define_attribute(DOads,
                   "Number of sub-levels in each successive DWT level, "
                   "starting from the highest level.  Accesses to "
                   "non-existent values are supported by repeating the "
                   "last available value.  All entries must lie in the "
                   "range 1 to 3.  For the meaning of sub-levels in "
                   "JPEG2000 Part-2, the reader is referred to Annex F of "
                   "IS 15444-2.\n"
                   "\t\t[You would not normally set values for this "
                   "parameter attribute yourself.]",
                   "I", MULTI_RECORD | CAN_EXTRAPOLATE);
  define_attribute(DSads,
                   "Array of splitting instructions, whose interpretation "
                   "generally depends upon the way in which ADS and DFS "
                   "tables are jointly referenced from COD/COC marker "
                   "segments, as identified by the `Cads' and `Cdfs' "
                   "attributes.  Each splitting instruction must take one "
                   "of the values: 3 (split horizontally and vertically); "
                   "2 (split vertically); 1 (split horizontally); or 0 "
                   "(do not split).  The last value is repeated as "
                   "necessary, if accesses are made beyond the end of the "
                   "array.  For the meaning of these splitting instructions, "
                   "the reader is referred to Annex F of IS 15444-2.\n"
                   "\t\t[You would not normally set values for this parameter "
                   "attribute yourself.]",
                   "(X=0,H=1,V=2,B=3)", MULTI_RECORD | CAN_EXTRAPOLATE);
}

/*****************************************************************************/
/*                               ads_params::finalize                        */
/*****************************************************************************/

void
  ads_params::finalize(bool after_reading)
{
  if (after_reading)
    return;

  int n, k, val=0;
  int code_idx=0; // Index of next splitting code to be written to `DSads'.
  for (n=0; get(Ddecomp,n,0,val,false,false,false); n++)
    {
      // Start with validity checks
      if ((inst_idx < 1) || (inst_idx > 127))
        { KDU_ERROR(e,0x26040501) e <<
            KDU_TXT("The `Ddecomp' attribute may be defined only for "
                    "index values in the range 1 to 127.  Perhaps your "
                    "decomposition structure requires too many distinct "
                    "ADS marker segments.");
        }
      bool valid = true;
      int num_primary_bands=0;
      switch (val & 3) {
          case 0:
            valid = (val == 0);
            num_primary_bands = 0;
            break;
          case 1:
          case 2:
            valid = ((val >> 12) == 0);
            num_primary_bands = 1;
            break;
          case 3:
            num_primary_bands = 3;
            break;
        };
      for (k=0; k < 3; k++)
        switch ((val >> (10*k+2)) & 3) {
            case 0:
              if (((val >> (10*k+4)) & 0x000000FF) != 0)
                valid = false;
              break;
            case 1:
            case 2:
              if (((val >> (10*k+8)) & 0x0000000F) != 0)
                valid = false;
              break;
            case 3:
              break;
          };
      if (!valid)
        { KDU_ERROR(e,0x26040502) e <<
            KDU_TXT("Encountered invalid `Ddecomp' attribute value") << ", 0x";
            e.set_hex_mode(true); e << val;  e.set_hex_mode(false); e << ".";
        }

      // Now create `DOads' and `DSads' values
      if (n == 0)
        { // Erase any existing values just in case
          delete_unparsed_attribute(DOads);
          delete_unparsed_attribute(DSads);
        }

      int sub_levs;
      int or_val = ((val >> 2) | (val >> 12) | (val >> 22)) & 0x3FF;
      if (or_val == 0)
        sub_levs = 1; // No splitting of any primary detail subband
      else if ((or_val >> 2) == 0)
        sub_levs = 2; // Need one extra sub-level
      else
        sub_levs = 3; // Need two extra sub-levels
      set(DOads,n,0,sub_levs);

      if (sub_levs == 1)
        continue; // No need for any splitting codes

      int code, sub_val;
      for (k=num_primary_bands-1; k >= 0; k--)
        { // Work from HH to LH to HL primary detail subbands
          sub_val = (val >> (2+k*10)) & 0x3FF;
          code = sub_val & 3;
          set(DSads,code_idx++,0,code);
          if ((sub_levs == 2) || (code == 0))
            continue;

          int num_extra_codes = (code==3)?4:2;
          for (; num_extra_codes > 0; num_extra_codes--)
            {
              code = (sub_val >> (2*num_extra_codes)) & 3;
              set(DSads,code_idx++,0,code);
            }
        }
    }

  assert((n == 0) || cod_params::is_valid_decomp_terminator(val));
}

/*****************************************************************************/
/*                       ads_params::write_marker_segment                    */
/*****************************************************************************/

int
  ads_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  ads_params *ref = (ads_params *) last_marked;
  int n, val=0;

  if ((inst_idx < 1) || (inst_idx > 127) || (tpart_idx != 0) ||
      (comp_idx >= 0))
    return 0;

  if (ref != NULL)
    {
      bool identical = true;
      assert((ref->tile_idx < 0) && (ref->inst_idx == this->inst_idx));
      for (n=0; identical && get(DOads,n,0,val,false,false); n++)
        identical = ref->compare(DOads,n,0,val);
      for (; identical && ref->get(DOads,n,0,val,false,false); n++)
        identical = compare(DOads,n,0,val);
      for (n=0; identical && get(DSads,n,0,val,false,false); n++)
        identical = ref->compare(DSads,n,0,val);
      for (; identical && ref->get(DSads,n,0,val,false,false); n++)
        identical = compare(DSads,n,0,val);
      if (identical)
        return 0;
    }

  for (n=0; get(DOads,n,0,val,false,false); n++);
  int num_theta = n;

  for (n=0; get(DSads,n,0,val,false,false); n++);
  int num_codes = n;

  if ((num_theta == 0) && (num_codes == 0))
    return 0; // Nothing to write


  int length = 7 + ((num_theta+3)>>2) + ((num_codes+3)>>2);
  if (out == NULL)
    return length;

  kdu_byte byte, bits_left;
  int acc_length = 0;

  acc_length += out->put(KDU_ADS);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_byte) inst_idx);

  acc_length += out->put((kdu_byte) num_theta);
  for (byte=0, bits_left=8, n=0; n < num_theta; n++)
    {
      get(DOads,n,0,val);
      bits_left -= 2;
      byte |= (kdu_byte)(val << bits_left);
      if (bits_left == 0)
        {
          acc_length += out->put(byte);
          byte = 0; bits_left = 8;
        }
    }
  if (bits_left < 8)
    acc_length += out->put(byte);

  acc_length += out->put((kdu_byte) num_codes);
  for (byte=0, bits_left=8, n=0; n < num_codes; n++)
    {
      get(DSads,n,0,val);
      if (val > 0)
        val = (val==3)?1:(val+1); // Deal with silly numbering
      bits_left -= 2;
      byte |= (kdu_byte)(val << bits_left);
      if (bits_left == 0)
        {
          acc_length += out->put(byte);
          byte = 0; bits_left = 8;
        }
    }
  if (bits_left < 8)
    acc_length += out->put(byte);

  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                     ads_params::check_marker_segment                      */
/*****************************************************************************/

bool
  ads_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_ADS) || (num_bytes < 1))
    return false;
  c_idx = *bytes;
  return ((c_idx >= 1) && (c_idx <= 127));
}

/*****************************************************************************/
/*                        ads_params::read_marker_segment                    */
/*****************************************************************************/

bool
  ads_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  int n, val;
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  if ((code != KDU_ADS) || (num_bytes < 2))
    return false;

  bp = bytes;
  end = bp + num_bytes;

  int which_inst = *(bp++);
  if (which_inst != inst_idx)
    return false;

  try {
      int byte=0, bits_left;
      int num_theta = kdu_read(bp,end,1);
      for (n=0, bits_left=0; n < num_theta; n++)
        {
          if (bits_left == 0)
            { byte = kdu_read(bp,end,1); bits_left=8; }
          bits_left -= 2;
          val = (byte >> bits_left) & 3;
          set(DOads,n,0,val);
        }

      int num_codes = kdu_read(bp,end,1);
      for (n=0, bits_left=0; n < num_codes; n++)
        {
          if (bits_left == 0)
            { byte = kdu_read(bp,end,1); bits_left=8; }
          bits_left -= 2;
          val = (byte >> bits_left) & 3;
          if (val > 0)
            val = (val==1)?3:(val-1); // Deal with silly numbering
          set(DSads,n,0,val);
        }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,0x26040503); e <<
        KDU_TXT("Malformed ADS marker segment encountered. "
        "Marker segment is too small.");
    }
  if (bp != end)
    { KDU_ERROR(e,0x26040504); e <<
        KDU_TXT("Malformed ADS marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  return true;
}

/*****************************************************************************/
/*                      ads_params::custom_textualize_field                  */
/*****************************************************************************/

void
  ads_params::custom_textualize_field(kdu_message &output, const char *name,
                                      int field_idx, int val)
{
  if ((strcmp(name,Ddecomp) == 0) && (field_idx == 0))
    {
      char buf[21];
      cod_params::textualize_decomp(buf,val);
      output << buf;
    }
}


/* ========================================================================= */
/*                                dfs_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          dfs_params::new_object                           */
/*****************************************************************************/

kdu_params *dfs_params::new_object()
{
  return new dfs_params(); 
}

/*****************************************************************************/
/*                       dfs_params::dfs_params (no args)                    */
/*****************************************************************************/

dfs_params::~dfs_params() {}
dfs_params::dfs_params()
  : kdu_params(DFS_params,false,false,true,false,true)
{
  define_attribute(DSdfs,
                   "Describes the primary subband decomposition type "
                   "associated with each DWT level, starting from the "
                   "highest resolution (1'st level).  The value may be one "
                   "of `B' (split in both directions), `H' (split "
                   "horizontally), `V' (split vertically) or `X' (don't "
                   "split at all).  The last case is degenerate, since "
                   "it means that the DWT level in question produces no "
                   "detail subbands whatsoever, simply passing its input "
                   "image through to the next DWT level.  However, this can "
                   "be useful in some circumstances.  The primary subband "
                   "decomposition determines the downsampling factors "
                   "between each successive resolution level.  If there are "
                   "more DWT levels than `DSdfs' values, the last available "
                   "value is replicated, as required.\n"
                   "\t\t[You would not normally set values for this parameter "
                   "attribute yourself.]",
                   "(X=0,H=1,V=2,B=3)", MULTI_RECORD | CAN_EXTRAPOLATE);
}

/*****************************************************************************/
/*                               dfs_params::finalize                        */
/*****************************************************************************/

void
  dfs_params::finalize(bool after_reading)
{
  if (after_reading)
    return;
  int n, val=0;
  for (n=0; get(DSdfs,n,0,val,false,false,false); n++)
    {
      if ((inst_idx < 1) || (inst_idx > 127))
        { KDU_ERROR(e,0x2604050a) e <<
            KDU_TXT("The `DSdfs' attribute may be defined only for "
                    "index values in the range 1 to 127.  Perhaps your "
                    "decomposition structure requires too many distinct "
                    "DFS marker segments.");
        }
      if ((val < 0) || (val > 3))
        { KDU_ERROR(e,0x25050500); e <<
            KDU_TXT("Illegal `DSdfs' attribute value encountered.  Legal "
            "values must be in the range 0 to 3.");
        }
    }
}

/*****************************************************************************/
/*                     dfs_params::check_marker_segment                      */
/*****************************************************************************/

bool
  dfs_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_DFS) || (num_bytes < 2))
    return false;
  int s_dfs = *(bytes++);  s_dfs = (s_dfs << 8) + *(bytes++);
  c_idx = s_dfs & 0x00FF;
  return ((c_idx >= 1) && (c_idx <= 127));
}

/*****************************************************************************/
/*                        dfs_params::read_marker_segment                    */
/*****************************************************************************/

bool
  dfs_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  int n, val;
  kdu_byte *bp, *end;

  if ((tpart_idx != 0) || (tile_idx >= 0) || (comp_idx >= 0))
    return false;
  if ((code != KDU_DFS) || (num_bytes < 2))
    return false;

  bp = bytes;
  end = bp + num_bytes;

  int s_dfs = *(bp++);  s_dfs = (s_dfs << 8) + *(bp++);
  if ((s_dfs & 0x00FF) != inst_idx)
    return false;

  try {
      int byte=0, bits_left;
      int num_codes = kdu_read(bp,end,1);
      for (n=0, bits_left=0; n < num_codes; n++)
        {
          if (bits_left == 0)
            { byte = kdu_read(bp,end,1); bits_left=8; }
          bits_left -= 2;
          val = (byte >> bits_left) & 3;
          if (val > 0)
            val = (val==1)?3:(val-1); // Deal with silly numbering
          set(DSdfs,n,0,val);
        }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,0x25050501); e <<
        KDU_TXT("Malformed DFS marker segment encountered. "
        "Marker segment is too small.");
    }
  if (bp != end)
    { KDU_ERROR(e,0x25050502); e <<
        KDU_TXT("Malformed DFS marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  return true;
}

/*****************************************************************************/
/*                       dfs_params::write_marker_segment                    */
/*****************************************************************************/

int
  dfs_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  int n, val=0;

  if ((inst_idx < 1) || (inst_idx > 127) || (tpart_idx != 0) ||
      (comp_idx >= 0) || (tile_idx >= 0))
    return 0;

  for (n=0; get(DSdfs,n,0,val,false,false); n++);
  int num_codes = n;

  int length = 7 + ((num_codes+3)>>2);
  if (out == NULL)
    return length;

  kdu_byte byte, bits_left;
  int acc_length = 0;

  acc_length += out->put(KDU_DFS);
  acc_length += out->put((kdu_uint16)(length-2));
  acc_length += out->put((kdu_uint16) inst_idx);
  acc_length += out->put((kdu_byte) num_codes);
  for (byte=0, bits_left=8, n=0; n < num_codes; n++)
    {
      get(DSdfs,n,0,val);
      if (val > 0)
        val = (val==3)?1:(val+1); // Deal with silly numbering
      bits_left -= 2;
      byte |= (kdu_byte)(val << bits_left);
      if (bits_left == 0)
        {
          acc_length += out->put(byte);
          byte = 0; bits_left = 8;
        }
    }
  if (bits_left < 8)
    acc_length += out->put(byte);

  assert(length == acc_length);
  return length;
}


/* ========================================================================= */
/*                                qcd_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          qcd_params::new_object                           */
/*****************************************************************************/

kdu_params *qcd_params::new_object()
{
  return new qcd_params(); 
}

/*****************************************************************************/
/*                       qcd_params::qcd_params (no args)                    */
/*****************************************************************************/

qcd_params::~qcd_params() {}
qcd_params::qcd_params()
  : kdu_params(QCD_params,true,true,false)
{
  add_dependency(COD_params);
  add_dependency(ADS_params);

  define_attribute(Qguard,
                   "Number of guard bits to prevent overflow in the magnitude "
                   "bit-plane representation. Typical values are 1 or 2.\n"
                   "\t\t[Default is 1]",
                   "I");
  define_attribute(Qderived,
                   "Quantization steps derived from LL band parameters? "
                   "If \"yes\", all quantization step sizes will be related "
                   "to the LL subband's step sizes through appropriate powers "
                   "of 2 and only the LL band step size will be written in "
                   "code-stream markers. Otherwise, a separate step size will "
                   "be recorded for every subband. You cannot use this option "
                   "with reversible compression.\n"
                   "\t\t[Default is not derived]",
                   "B");
  define_attribute(Qstep,
                   "Base step size to be used in deriving irreversible "
                   "quantization step sizes for every subband. The base "
                   "step parameter should be in the range 0 to 2.\n"
                   "\t\t[Default is 1/256]",
                   "F");
  define_attribute(Qfix16,
                   "If this attribute is present, the quantization step sizes "
                   "that are otherwise configured in `Qabs_steps' (either "
                   "directly or with the aid of `Qstep' and/or `Qweights') "
                   "are optimized for 16-bit fixed-point processing.  Step "
                   "sizes are adjusted (mostly downwards) so that the "
                   "quantization and dequantization operators encountered "
                   "in fixed-point processing should involve scaling only by "
                   "powers of 2.  If the value of the attribute is \"LIMIT\", "
                   "the quantization step sizes are additionally adjusted so "
                   "as to prevent any subband from receiving more than 15 "
                   "magnitude bits (including guard bits), ignoring the "
                   "impact of any max-shift based ROI adjustments (see "
                   "`Rshift').  If the value is \"FREE\", this additional "
                   "adjustment is not made, allowing some (typically low "
                   "frequency) subbands to receive very fine quantization, "
                   "perhaps at the expense of a more expensive block coding "
                   "opertion.  The first set of adjustments, leaving power of "
                   "2 quantization operators, avoids the introduction of "
                   "numerical rounding errors in 16-bit fixed-point "
                   "implementations.  While step sizes generally need to be "
                   "adjusted, there is no strong reason to believe that this "
                   "should adversely affect the performance of irreversible "
                   "compression when EBCOT's PCRD algorithm is used for rate "
                   "control (the default approach).  At very high bit-rates, "
                   "the performance of irreversible compression is usually "
                   "improved by specifying the `Qfix16' attribute.  Also, "
                   "the discrepancy between fixed-point and floating-point "
                   "decoded versions of the same code-stream is reduced "
                   "by adopting `Qfix16'-modified quantization parameters.  "
                   "This attribute has no effect if processing is reversible "
                   "(`Creversible' is true) or if `Qimplicit' is true.",
                   "(FREE=0,LIMIT=1)");
  define_attribute(Qabs_steps,
                   "Absolute quantization step sizes for each subband, "
                   "expressed as a fraction of the nominal dynamic range "
                   "for that subband. The nominal range is equal to 2^B "
                   "(B is the image sample bit-depth) multiplied by "
                   "the DC gain of each low-pass subband analysis filter and "
                   "the AC gain of each high-pass subband analysis filter, "
                   "involved in the construction of the relevant subband. "
                   "The bands are described one by one, in the following "
                   "sequence: LL_D, HL_D, LH_D, ..., HL_1, LH_1, HH_1.  "
                   "Here, D denotes the number of DWT levels.  Also, note "
                   "that the actual set of subbands for which values "
                   "are provided depends upon the decomposition structure "
                   "identified via `Cdecomp'.  A single step size must "
                   "be supplied for every subband (there is no "
                   "extrapolation), except in the event that `Qderived' is "
                   "set to \"yes\" -- then, only one parameter is allowed, "
                   "corresponding to the LL_D subband.\n"
                   "\t\t[For compressors, the absolute "
                   "step sizes are ignored if `Qstep' has been used.]",
                   "F",MULTI_RECORD); // No extrapolation.
  define_attribute(Qabs_ranges,
                   "Number of range bits used to code each subband during "
                   "reversible compression.  Subbands appear in the sequence, "
                   "LL_D, HL_D, LH_D, ..., HL_1, LH_1, HH_1, where D denotes "
                   "the number of DWT levels.  Note that the actual set of "
                   "subbands for which values are provided depends upon the "
                   "decomposition structure, identified via `Cdecomp'.  The "
                   "number of range bits for a reversibly compressed subband, "
                   "plus the number of guard bits (see `Qguard'), is equal "
                   "to 1 plus the number of magnitude bit-planes which are "
                   "used for coding its samples.\n"
                   "\t\t[For compressors, most users will accept the "
                   "default policy, which sets the number of range bits to "
                   "the smallest value which is guaranteed to avoid overflow "
                   "or underflow in the bit-plane representation, "
                   "assuming that the RCT (colour transform) is used.  If "
                   "explicit values are supplied, they must be given for "
                   "each and every subband.]",
                   "I",MULTI_RECORD); // No extrapolation.
  define_attribute(Qweights,
                   "Used only when subband quantization steps sizes are "
                   "derived using `Qstep' and the `Qderived' attribute is "
                   "false (i.e., no).  If the value of this boolean "
                   "parameter attribute is true (yes), the quantization step "
                   "sizes that would otherwise be derived are scaled by the "
                   "reciprocal of any weighting factors supplied via "
                   "`Cband_weights', `Cweight' and/or `Clev_weights'.  The "
                   "value of this option is that it provides a simple way "
                   "to arrange for the number of discarded coding passes "
                   "from each code-block to be similar amongst all image "
                   "subbands of all image components.  Otherwise, when "
                   "visual weights are applied, it is common to wind up "
                   "discarding many more coding passes from the high "
                   "frequency subbands than the low frequency subbands, "
                   "which may mean that the encoder does more work than it "
                   "needs to.\n"
                   "\t\t[default=no]",
                   "B"); 
}

/*****************************************************************************/
/*                       qcd_params::copy_with_xforms                        */
/*****************************************************************************/

void
  qcd_params::copy_with_xforms(kdu_params *src,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  qcd_params *source = (qcd_params *) src;
  int guard=0;
  bool derived=false;
  if (source->get(Qguard,0,0,guard,false))
    set(Qguard,0,0,guard);
  if (source->get(Qderived,0,0,derived,false))
    set(Qderived,0,0,derived);

  kdu_params *source_cod = source->access_cluster(COD_params);
  if ((source_cod == NULL) ||
      ((source_cod =
        source_cod->access_relation(source->tile_idx,source->comp_idx,
                                    0,true)) == NULL))
    { assert(0); return; }

  int source_levels=0;
  bool source_reversible=false;
  source_cod->get(Clevels,0,0,source_levels);
  source_cod->get(Creversible,0,0,source_reversible);

  int n; // Represents resolution level
  int b_in, b_out, b_offset; // Num quant vals written from previous levels
  int num_level_bands;
  kdu_int16 source_bands[49];
  kdu_int16 target_bands[49];
  source_bands[0] = target_bands[0] = 0; // Degenerate case for resolution 0
  for (n=0, b_offset=0, num_level_bands=1;
       n <= (source_levels-discard_levels); n++)
    { // Each iteration of loop processes one resolution level, until there
      // are no more resolution levels for which quantization information is
      // available

      if (n > 0)
        { // Find description for the new resolution level
	  if (derived && !source_reversible)
            break; // Qabs_steps should have only one value
          int source_decomp=0;
          if (!source_cod->get(Cdecomp,source_levels-n,0,source_decomp))
            assert(0); // Forgot to finalize source parameter before copying
          num_level_bands =
            cod_params::expand_decomp_bands(source_decomp,source_bands);
          if (transpose)
            {
              int target_decomp = cod_params::transpose_decomp(source_decomp);
              cod_params::expand_decomp_bands(target_decomp,target_bands);
            }
        }

      for (b_out=(n==0)?0:1; b_out < num_level_bands; b_out++)
        {
          if (transpose)
            {
              kdu_int16 desc_out, desc_in;
              desc_out = target_bands[b_out];
              desc_in = ((desc_out & 0x00FF)<<8) | ((desc_out>>8) & 0x00FF);
              for (b_in=0; b_in < num_level_bands; b_in++)
                if (source_bands[b_in] == desc_in)
                  break;
              assert(b_in < num_level_bands);
            }
          else
            b_in = b_out;

          if (source_reversible)
            {
              int range=0;
              source->get(Qabs_ranges,b_offset+b_in,0,range);
              set(Qabs_ranges,b_offset+b_out,0,range);
            }
          else
            {
              float step=0.0f;
              source->get(Qabs_steps,b_offset+b_in,0,step);
              set(Qabs_steps,b_offset+b_out,0,step);
            }
        }

      b_offset += num_level_bands - 1;
    }
}

/*****************************************************************************/
/*                       qcd_params::write_marker_segment                    */
/*****************************************************************************/

int
  qcd_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  qcd_params *ref = (qcd_params *) last_marked;
  int length, n;
  int guard=0, levels=0;
  bool reversible=false, derived=false;

  if (tpart_idx != 0)
    return 0;
  kdu_params *cod = access_cluster(COD_params);
  assert(cod != NULL);
  cod = cod->access_relation(tile_idx,comp_idx,0,true);
  if (!((cod != NULL) &&
        cod->get(Clevels,0,0,levels) &&
        cod->get(Creversible,0,0,reversible)))
    { KDU_ERROR_DEV(e,110); e <<
        KDU_TXT("Cannot write QCD/QCC marker segment without first "
        "completing relevant COD/COC information!");
    }
  if (!get(Qguard,0,0,guard))
    { KDU_ERROR_DEV(e,111); e <<
        KDU_TXT("Cannot write QCD/QCC marker segment yet! "
        "No info on guard bits.");
    }
  if (reversible)
    derived = false;
  else if (!get(Qderived,0,0,derived))
    { KDU_ERROR_DEV(e,112); e <<
        KDU_TXT("Cannot write QCD/QCC marker segment yet!  Not "
        "clear whether quant steps are derived from the LL band step size.");
    }

  kdu_int16 band_descriptors[49];
  int num_bands = 1;
  if (!derived)
    {
      int decomp=0;
      for (n=0; n < levels; n++)
        {
          cod->get(Cdecomp,n,0,decomp);
          num_bands +=
            cod_params::expand_decomp_bands(decomp,band_descriptors) - 1;
        }
    }

  if (ref != NULL)
    { // See if we can avoid writing this marker.
      int ref_levels=0;
      bool ref_reversible=false;
      kdu_params *ref_cod =
        cod->access_relation(ref->tile_idx,ref->comp_idx,0,true);
      assert(ref_cod != NULL);
      if (!((ref_cod != NULL) &&
            ref_cod->get(Clevels,0,0,ref_levels) &&
            ref_cod->get(Creversible,0,0,ref_reversible)))
        { KDU_ERROR_DEV(e,113); e <<
            KDU_TXT("Cannot write QCD/QCC marker segment without "
            "first completing all relevant COD/COC information!");
        }
      if ((ref_reversible == reversible) && (ref_levels == levels) &&
          ref->compare(Qguard,0,0,guard) &&
          (reversible || ref->compare(Qderived,0,0,derived)))
        { // Just need to check the actual quantization parameters now.
          for (n=0; n < num_bands; n++)
            if (reversible)
              {
                int range=0;
                if (!get(Qabs_ranges,n,0,range))
                  { KDU_ERROR_DEV(e,114); e <<
                      KDU_TXT("Cannot write QCD/QCC marker segment yet!  "
                      "Absolute reversible ranging information not "
                      "available.");
                  }
                if (!ref->compare(Qabs_ranges,n,0,range))
                  break;
              }
            else
              {
                float step=0.0f;
                if (!get(Qabs_steps,n,0,step))
                  { KDU_ERROR_DEV(e,115); e <<
                      KDU_TXT("Cannot write QCD/QCC marker segment yet!  "
                      "Absolute step size information not available.");
                  }
                if (!ref->compare(Qabs_steps,n,0,step))
                  break;
              }
          if (n == num_bands)
            return 0; // No need to explicitly write a marker here.
        }
    }

  // We are now committed to writing (or simulating) a marker segment.
  if ((guard > 7) || (guard < 0))
    { KDU_ERROR(e,116); e <<
        KDU_TXT("Illegal number of guard bits, ") << guard <<
        KDU_TXT(". Legal range is from 0 to 7.");
    }
  
  int component_bytes = (get_num_comps() <= 256)?1:2;
  
  if (comp_idx < 0)
    length = 4 + 1;
  else
    length = 4 + component_bytes + 1;
  length += num_bands * (2-reversible);
  if (out == NULL)
    return length;

  // Now for actually writing out the marker.
  if (tile_idx >= 0)
    { // Check for profile compatibility.
      kdu_params *siz = access_cluster(SIZ_params);
      assert(siz != NULL);
      int profile = 2; siz->get(Sprofile,0,0,profile);
      if (profile == 0)
        { KDU_WARNING(w,4); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  QCD/QCC marker segments may only appear in the "
            "main header of a Profile-0 code-stream.  You should set "
            "\"Sprofile\" to 1 or 2.  Problem detected in tile ")
            << tile_idx << ".";
        }
      else if ((profile >= Sprofile_CINEMA2K) &&
               (profile <= Sprofile_CINEMASS))
        { KDU_ERROR(e,0x2604100D); e <<
            KDU_TXT("Profile violation detected.  QCD/QCC marker segments may "
                    "only appear in the main header of a code-stream marked "
                    "with one of the Digital Cinema or BROADCAST profiles.");
        }
    }

  int acc_length = 0; // Count actual written bytes for consistency checking.
  int style = (guard << 5) + ((reversible)?0:(2-derived));
  if (comp_idx < 0)
    {
      acc_length += out->put(KDU_QCD);
      acc_length += out->put((kdu_uint16)(length-2));
      acc_length += out->put((kdu_byte) style);
    }
  else
    {
      acc_length += out->put(KDU_QCC);
      acc_length += out->put((kdu_uint16)(length-2));
      if (component_bytes == 1)
        acc_length += out->put((kdu_byte) comp_idx);
      else
        acc_length += out->put((kdu_uint16) comp_idx);
      acc_length += out->put((kdu_byte) style);
    }
  for (n=0; n < num_bands; n++)
    if (reversible)
      {
        int val=0;

        if (!get(Qabs_ranges,n,0,val))
          { KDU_ERROR_DEV(e,117); e <<
              KDU_TXT("Insufficient absolute ranging parameters "
              "available for writing QCD/QCC marker segment");
          }
        if ((val < 0) || (val > 31))
          { KDU_ERROR(e,118); e <<
              KDU_TXT("Absolute ranging parameters for reversibly "
              "compressed subbands must be non-negative, no larger than 31!");
          }
        acc_length += out->put((kdu_byte)(val<<3));
      }
    else
      {
        float val=0.0f;
        int eps, mu;

        if (!get(Qabs_steps,n,0,val))
          { KDU_ERROR_DEV(e,119); e <<
              KDU_TXT("Insufficient absolute quantization step size "
              "parameters available for writing QCD/QCC marker segment.");
          }
        step_to_eps_mu(val,eps,mu);
        acc_length += out->put((kdu_uint16)((eps<<11)+mu));
      }
  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                       qcd_params::check_marker_segment                    */
/*****************************************************************************/

bool
  qcd_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if (code == KDU_QCD)
    {
      c_idx = -1;
      return true;
    }
  if ((code == KDU_QCC) && (num_bytes >= 2))
    {
      c_idx = *(bytes++);
      if (num_comps > 256)
        c_idx = (c_idx<<8) + *(bytes++);
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                        qcd_params::read_marker_segment                    */
/*****************************************************************************/

bool
  qcd_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if (tpart_idx != 0)
    return false;
  bp = bytes;
  end = bp + num_bytes;
  if (comp_idx < 0)
    { // Need QCD for success
      if (code != KDU_QCD)
        return false;
    }
  else
    { // Need QCC for success
      int which_comp;

      if (code != KDU_QCC)
        return false;
      if (get_num_comps() <= 256)
        which_comp = *(bp++);
      else
        { which_comp = *(bp++); which_comp = (which_comp<<8) + *(bp++); }
      if (which_comp != comp_idx)
        return false;
    }

  if (tile_idx >= 0)
    { // Check for profile compatibility.
      kdu_params *siz = access_cluster(SIZ_params);
      assert(siz != NULL);
      int profile = 2; siz->get(Sprofile,0,0,profile);
      if (profile == 0)
        { KDU_WARNING(w,5); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  QCD/QCC marker segments may only appear in the "
            "main header of a Profile-0 code-stream.  You should set "
            "\"Sprofile\" to 1 or 2.  Problem detected in tile ")
            << tile_idx << ".";
        }
    }

  try {
      int style = kdu_read(bp,end,1);
      bool reversible=false, derived=false;
      int n;

      set(Qguard,0,0,(style>>5));
      style &= 31;
      if (style == 0)
        { reversible = true; derived = false; }
      else if (style == 1)
        { reversible = false; derived = true; }
      else if (style == 2)
        { reversible = false; derived = false; }
      else
        { KDU_ERROR(e,120); e <<
            KDU_TXT("Undefined style byte found in QCD/QCC marker "
            "segment!");
        }
      if (!reversible)
        set(Qderived,0,0,derived);
      if (reversible)
        for (n=0; bp < end; n++)
          set(Qabs_ranges,n,0,kdu_read(bp,end,1)>>3);
      else
        for (n=0; bp < (end-1); n++)
          {
            int val = kdu_read(bp,end,2);
            int mu, eps;
            float step;
          
            mu = val & ((1<<11)-1);
            eps = val >> 11;
            step = ((float) mu) / ((float)(1<<11)) + 1.0F;
            step = step / ((float)(((kdu_uint32) 1)<<eps));
            set(Qabs_steps,n,0,step);
          }
        if (n < 1)
          throw bp;
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,122); e <<
        KDU_TXT("Malformed QCD/QCC marker segment encountered. "
        "Marker segment is too small.");
    }

  if (bp != end)
    { KDU_ERROR(e,121); e <<
        KDU_TXT("Malformed QCD/QCC marker segment encountered. The final ")
        << (int)(end-bp) <<
        KDU_TXT(" bytes were not consumed!");
    }

  return true;
}

/*****************************************************************************/
/*                            qcd_params::finalize                           */
/*****************************************************************************/

void
  qcd_params::finalize(bool after_reading)
{
  if (after_reading)
    return;
  int n, decomp=0, val=0, guard_bits=0;
  float fval=0.0f;

  if (!get(Qguard,0,0,guard_bits))
    set(Qguard,0,0,guard_bits=1); // Default is 1 guard bit.

  kdu_params *cod = access_cluster(COD_params);
  assert(cod != NULL);
  cod = cod->access_relation(tile_idx,comp_idx,0,true);
  assert(cod != NULL);

  int reversible=0, num_levels=0, kernel_id=0;
  if (!(cod->get(Creversible,0,0,reversible) &&
        cod->get(Clevels,0,0,num_levels) &&
        cod->get(Ckernels,0,0,kernel_id)))
    abort();

  kdu_params *atk = NULL;
  if (kernel_id == Ckernels_ATK)
    {
      int atk_idx=0;
      if (!cod->get(Catk,0,0,atk_idx))
        assert(0);
      atk = access_cluster(ATK_params);
      if ((atk == NULL) ||
          ((atk = atk->access_relation(tile_idx,-1,atk_idx,true)) == NULL))
        assert(0);
    }

  kdu_int16 band_descriptors[49];

  int num_bands = 1;
  for (n=0; n < num_levels; n++)
    {
      cod->get(Cdecomp,n,0,decomp);
      num_bands +=
        cod_params::expand_decomp_bands(decomp,band_descriptors) - 1;
    }

  // Find out how many steps and/or ranges are available already
  int abs_steps=0, abs_ranges=0;
  while (get(Qabs_steps,abs_steps,0,fval,true,true,false))
    abs_steps++;
  while (get(Qabs_ranges,abs_ranges,0,val,true,true,false))
    abs_ranges++;

  // Now see if we need to generate some quantization parameters ourselves
  bool derived_from_LL=false;
  if (reversible)
    {
      if ((!get(Qderived,0,0,derived_from_LL)) || derived_from_LL)
        set(Qderived,0,0,derived_from_LL=false);
      if (abs_ranges == num_bands)
        return; // Use the existing information

      // Implement default policy for absolute ranges.
      int precision=0;
      kdu_params *siz = access_cluster(SIZ_params);
      if (!siz->get(Sprecision,
                    ((comp_idx<0)?0:comp_idx),0,precision))
        { KDU_ERROR(e,123); e <<
            KDU_TXT("Trying to finalize quantization parameter "
            "attributes without first providing any information about the "
            "image component bit-depths (i.e. \"Sprecision\").");
        }
      if (comp_idx < 0)
        { // See if any of the specific components have different precisions
          // to the present one.  If so, make sure that unique instances of
          // the `qcd_params' object exist for those components, so we can
          // install the correct absolute ranges.
          for (int c=0; c < num_comps; c++)
            if (!siz->compare(Sprecision,c,0,precision))
              access_relation(tile_idx,c); // May create a new unique object
        }

      /* The default ranging parameters are based on the folowing:
         1) To avoid overflow/underflow we need
            `epsilon'+G >= `precision'+log2(B)
            where `epsilon' is the ranging parameter, G is the number of
            guard bits and B is the worst case (BIBO) bit-depth expansion for
            the reversible DWT.  Values for B are calculated using the
            `kdu_kernels::get_bibo_gain' function.
         2) If there are 3 or more components, we need to allow an extra bit
            in the chrominance channels if the RCT is used.  Since a single
            set of ranging parameters may have to work for some
            tile-components which use the RCT and others which do not, it is
            safest to always allow this extra bit.
         3) The BIBO gain values are accurate only for moderate to high
            bit-depth (precision) imagery.  At lower precisions, it is safer
            to increase the ranging parameters. */
      if (num_comps >= 3)
        precision++; // Allows for the use of the RCT
      if (precision < 5)
        precision++; // Safer for low bit-depth data


      derive_absolute_ranges(this,cod,atk,num_levels,num_bands,
                             precision,guard_bits);
      set_derived(Qabs_ranges);
      return;
    }

  // Processing for irreversible step sizes is more complex.
  float ref_step=0.0f;
  if (get(Qstep,0,0,ref_step))
    { // Ignore any existing step sizes in this case.
      if (get(Qabs_steps,0,0,fval,false,false,false))
        { KDU_WARNING_DEV(w,6); w <<
            KDU_TXT("Some absolute step sizes which you have supplied will be "
            "ignored, since `Qstep' has been used or a default value for "
            "`Qstep' has been forced.  If you want to specify explicit "
            "absolute step sizes, you must not use `Qstep' anywhere in the "
            "inheritance path of the relevant tile-component.  In practice, "
            "this means that you must prevent a default `Qstep' attribute "
            "from being synthesized at a higher level in the inheritance "
            "path (e.g., at the global level) by using one of the "
            "available methods to explicitly specify quantization "
            "step sizes -- you can specify a full set, or you can specify "
            "a single value and use the efficient `Qderived' option.");
        }
      if (!get(Qderived,0,0,derived_from_LL))
        set(Qderived,0,0,derived_from_LL=false);
      bool weighted_quant = false;
      if (derived_from_LL ||
          !(get(Qweights,0,0,weighted_quant) && weighted_quant))
        derive_absolute_steps(this,cod,atk,num_levels,num_bands,ref_step,
                              derived_from_LL);
      else
        derive_weighted_steps(this,cod,atk,num_levels,num_bands,ref_step);
      set_derived(Qabs_steps);
    }
  else if (abs_steps >= num_bands)
    { // Use the available step sizes.
      if ((!get(Qderived,0,0,derived_from_LL)) || derived_from_LL)
        set(Qderived,0,0,derived_from_LL=false);
    }
  else if (abs_steps == 1)
    {
      if ((!get(Qderived,0,0,derived_from_LL)) || !derived_from_LL)
        set(Qderived,0,0,derived_from_LL=true);
    }
  else
    { // Must derive step sizes, using default reference step.
      if (!get(Qderived,0,0,derived_from_LL))
        set(Qderived,0,0,derived_from_LL=false);
      set(Qstep,0,0,ref_step=1.0F/256.0F);
      derive_absolute_steps(this,cod,atk,num_levels,num_bands,ref_step,
                            derived_from_LL);
      set_derived(Qabs_steps);
    }

  // At this point, the object has access to a set of valid step sizes.
  if (derived_from_LL)
    num_bands = 1;
  if (get(Qabs_steps,0,0,fval,false,false,true))
    { /* The attribute is not inherited.  Just make minor corrections to
         ensure that the values conform exactly to the epsilon-mu
         parametrization supported by the code-stream. */
      int eps, mu;

      for (n=0; n < num_bands; n++)
        {
          if (!get(Qabs_steps,n,0,fval))
            assert(0);
          step_to_eps_mu(fval,eps,mu);
          set(Qabs_steps,n,0,
              (1.0F+mu/((float)(1<<11)))/((float)((kdu_uint32)(1<<eps))));
        }
    }
  else
    { /* The step sizes which we have are inherited.  Make a local copy
         to ensure that nothing goes wrong. */
      int eps, mu;
      float *values = new float[num_bands];
      try { // Protect against possible memory leaks
        for (n=0; n < num_bands; n++)
          if (!get(Qabs_steps,n,0,values[n],true,true,false))
            assert(0);
        for (n=0; n < num_bands; n++)
          { 
            step_to_eps_mu(values[n],eps,mu);
            set(Qabs_steps,n,0,
                (1.0F+mu/((float)(1<<11)))/((float)((kdu_uint32)(1<<eps))));
          }
        set_derived(Qabs_steps);
      }
      catch (kdu_exception exc) {
        delete[] values;
        throw exc;
      }
      delete[] values;
    }
  
  // Finish by making any adjustments required by the `Qfix16' attribute
  int fix16_limit=0;
  if ((!derived_from_LL) && get(Qfix16,0,0,fix16_limit))
    optimize_absolute_steps_for_fix16(this,cod,atk,num_levels,num_bands,
                                      guard_bits,(fix16_limit==Qfix16_LIMIT));
}


/* ========================================================================= */
/*                              rgn_params members                           */
/* ========================================================================= */

/*****************************************************************************/
/*                          rgn_params::new_object                           */
/*****************************************************************************/

kdu_params *rgn_params::new_object()
{
  return new rgn_params(); 
}

/*****************************************************************************/
/*                       rgn_params::rgn_params (no args)                    */
/*****************************************************************************/

rgn_params::~rgn_params() {}
rgn_params::rgn_params()
  : kdu_params(RGN_params,true,true,false,true)
{
  define_attribute(Rshift,
                   "Region of interest up-shift value.  All subband samples "
                   "which are involved in the synthesis of any image sample "
                   "which belongs to the foreground region of an ROI mask "
                   "will be effectively shifted up (scaled by two the power "
                   "of this shift value) prior to quantization.  The "
                   "region geometry is specified independently and is not "
                   "explicitly signalled through the code-stream; instead, "
                   "this shift must be sufficiently large to enable the "
                   "decoder to separate the foreground and background "
                   "on the basis of the shifted sample amplitudes alone.  "
                   "You will receive an appropriate error message if the "
                   "shift value is too small.\n"
                   "\t\t[Default is 0]",
                   "I");
  define_attribute(Rlevels,
                   "Number of initial (highest frequency) DWT levels through "
                   "which to propagate geometric information concerning the "
                   "foreground region for ROI processing.  Additional "
                   "levels (i.e., lower frequency subbands) will be treated "
                   "as belonging entirely to the foreground region.\n"
                   "\t\t[Default is 4]",
                   "I");
  define_attribute(Rweight,
                   "Region of interest significance weight.  Although this "
                   "attribute may be used together with `Rshift', it is "
                   "common to use only one or the other.  All code-blocks "
                   "whose samples contribute in any way to the reconstruction "
                   "of the foreground region of an ROI mask will have their "
                   "distortion metrics scaled by the square of the supplied "
                   "weighting factor, for the purpose of rate allocation.  "
                   "This renders such blocks more important and assigns to "
                   "them relatively more bits, in a manner which "
                   "is closely related to the effect of the `Clevel_weights' "
                   "and `Cband_weights' attributes on the importance of "
                   "whole subbands.  Note that this region weighting "
                   "strategy is most effective when working with large "
                   "images and relatively small code-blocks (or precincts).\n"
                   "\t\t[Default is 1, i.e., no extra weighting]",
                   "F");
}

/*****************************************************************************/
/*                       rgn_params::copy_with_xforms                        */
/*****************************************************************************/

void
  rgn_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int shift=0;
  if (source->get(Rshift,0,0,shift,false))
    set(Rshift,0,0,shift);
}

/*****************************************************************************/
/*                       rgn_params::write_marker_segment                    */
/*****************************************************************************/

int
  rgn_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  rgn_params *ref = (rgn_params *) last_marked;

  if ((tpart_idx != 0) || (comp_idx < 0))
    return 0;

  int shift=0;
  if (!get(Rshift,0,0,shift))
    shift = 0;
  if (ref != NULL)
    {
      int ref_shift=0;

      if (!ref->get(Rshift,0,0,ref_shift))
        ref_shift = 0;
      if (shift == ref_shift)
        return 0;
    }
  else if (shift == 0)
    return 0;

  if ((shift > 255) || (shift < 0))
    { KDU_ERROR(e,124); e <<
        KDU_TXT("Illegal ROI up-shift, ") << shift <<
        KDU_TXT(". Legal range is from 0 to 255!");
    }
  kdu_params *siz = access_cluster(SIZ_params);
  int profile = 2;
  if (siz != NULL)
    siz->get(Sprofile,0,0,profile);
  if ((profile >= Sprofile_CINEMA2K) && (profile <= Sprofile_CINEMASS))
    { KDU_ERROR(e,0x2604100F); e <<
      KDU_TXT("Profile violation detected.  RGN marker segments are "
              "disallowed in code-streams marked with any of the Digital "
              "Cinema or BROADCAST profiles.");
    }

  int component_bytes = (get_num_comps() <= 256)?1:2;
  
  int length = 6+component_bytes;
  if (out == NULL)
    return length;

  int acc_length = 0;

  acc_length += out->put(KDU_RGN);
  acc_length += out->put((kdu_uint16)(length-2));
  if (component_bytes == 1)
    acc_length += out->put((kdu_byte) comp_idx);
  else
    acc_length += out->put((kdu_uint16) comp_idx);
  acc_length += out->put((kdu_byte) 0);
  acc_length += out->put((kdu_byte) shift);

  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                       rgn_params::check_marker_segment                    */
/*****************************************************************************/

bool
  rgn_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  if ((code != KDU_RGN) || (num_bytes < 2))
    return false;
  c_idx = *(bytes++);
  if (num_comps > 256)
    c_idx = (c_idx<<8) + *(bytes++);
  return true;
}

/*****************************************************************************/
/*                        rgn_params::read_marker_segment                    */
/*****************************************************************************/

bool
  rgn_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if ((tpart_idx != 0) || (code != KDU_RGN) || (comp_idx < 0))
    return false;
  bp = bytes;
  end = bp + num_bytes;

  int component_bytes = (get_num_comps() <= 256)?1:2;

  try {
    int which_comp = kdu_read(bp,end,component_bytes);
    if (which_comp != comp_idx)
      return false;
    
    int style = kdu_read(bp,end,1);
    if (style != 0)
      { KDU_ERROR(e,125); e <<
          KDU_TXT("Encountered non-Part1 RGN marker segment!");
      }
    set(Rshift,0,0,kdu_read(bp,end,1));
    if (bp != end)
      { KDU_ERROR(e,126); e <<
          KDU_TXT("Malformed RGN marker segment encountered. The final ")
          << (int)(end-bp) <<
          KDU_TXT(" bytes were not consumed!");
      }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,127); e <<
        KDU_TXT("Malformed RGN marker segment encountered. "
        "Marker segment is too small.");
    }

  return true;
}

/*****************************************************************************/
/*                               rgn_params::finalize                        */
/*****************************************************************************/

void
  rgn_params::finalize(bool after_reading)
{
  if (after_reading)
    return;

  int val=0;
  if (!get(Rlevels,0,0,val))
    set(Rlevels,0,0,4); // Default is 4 DWT levels of ROI mask propagation.
  if (get(Rshift,0,0,val) && (val > 37))
    { KDU_WARNING(w,8); w <<
        KDU_TXT("Up-shift values in the RGN marker segment should "
        "not need to exceed 37 under any circumstances.  The use of a larger "
        "value, ") << val <<
        KDU_TXT(" in this case, may cause problems.");
    }
}


/* ========================================================================= */
/*                                 poc_params                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          poc_params::new_object                           */
/*****************************************************************************/

kdu_params *poc_params::new_object()
{
  return new poc_params(); 
}

/*****************************************************************************/
/*                       poc_params::poc_params (no args)                    */
/*****************************************************************************/

poc_params::~poc_params() {}
poc_params::poc_params()
  : kdu_params(POC_params,true,false,true)
{
  define_attribute(Porder,
                   "Progression order change information.  The attribute may "
                   "be applied globally (main header), or in a tile-specific "
                   "manner (tile-part header).  In this latter case, multiple "
                   "instances of the attribute may be supplied for any "
                   "given tile, which will force the generation of multiple "
                   "tile-parts for the tile (one for each instance of the "
                   "`Porder' attribute).  As with all attributes, tile "
                   "specific forms are specified by appending a suffix of the "
                   "form \":T<tnum>\" to the attribute name, where <tnum> "
                   "stands for the tile number, starting from 0.  "
                   "Each instance of the attribute may contain one or more "
                   "progression records, each of which defines the order for "
                   "a collection of packets. Each record contains 6 fields. "
                   "The first two fields identify inclusive lower bounds for "
                   "the resolution level and image component indices, "
                   "respectively. The next three fields identify exclusive "
                   "upper bounds for the quality layer, resolution level and "
                   "image component indices, respectively. All indices are "
                   "zero-based, with resolution level 0 corresponding to the "
                   "LL_D subband. The final field in each record identifies "
                   "the progression order to be applied within the indicated "
                   "bounds. This order is applied only to those packets which "
                   "have not already been sequenced by previous records or "
                   "instances.",
                   "IIIII(LRCP=0,RLCP=1,RPCL=2,PCRL=3,CPRL=4)",MULTI_RECORD);
}

/*****************************************************************************/
/*                       poc_params::copy_with_xforms                        */
/*****************************************************************************/

void
  poc_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int r_min=0, c_min=0, layer_lim=0, r_lim=0, c_lim=0, order=0;
  if (source->get(Porder,0,0,r_min,false))
    {
      int n=0;
      while (source->get(Porder,n,0,r_min,false,false) &&
             source->get(Porder,n,1,c_min,false,false) &&
             source->get(Porder,n,2,layer_lim,false,false) &&
             source->get(Porder,n,3,r_lim,false,false) &&
             source->get(Porder,n,4,c_lim,false,false) &&
             source->get(Porder,n,5,order,false,false))
        {
          c_min -= skip_components;
          if (c_min < 0)
            c_min = 0;
          c_lim -= skip_components;
          if (c_lim < 1)
            { c_lim = 1; layer_lim = 0; }
          set(Porder,n,0,r_min);
          set(Porder,n,1,c_min);
          set(Porder,n,2,layer_lim);
          set(Porder,n,3,r_lim);
          set(Porder,n,4,c_lim);
          set(Porder,n,5,order);
          n++;
        }
    }
}

/*****************************************************************************/
/*                       poc_params::write_marker_segment                    */
/*****************************************************************************/

int
  poc_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  poc_params *ref = (poc_params *) last_marked;
  int length, num_records, n;
  int res_min=0, comp_min=0;
  int layer_above=0, res_above=0, comp_above=0, order=0;

  if (tpart_idx != inst_idx)
    return 0;
  for (n=0; get(Porder,n,0,res_min,false); n++);
  num_records = n;
  if (num_records == 0)
    return 0; // No order information available.
  if ((ref != NULL) && (ref->tile_idx != this->tile_idx))
    { // See if we can skip the marker altogether.
      assert((ref->tile_idx < 0) && (inst_idx == 0));
      kdu_params *next_instance = access_relation(tile_idx,comp_idx,1,true);
      if ((next_instance == NULL) || !next_instance->get(Porder,0,0,res_min))
        { /* This is the only instance; otherwise, we would definitely need
             explicit markers for this tile. */
          for (n=0; n < num_records; n++)
            {
              if (!(get(Porder,n,0,res_min) && get(Porder,n,1,comp_min) &&
                    get(Porder,n,2,layer_above) && get(Porder,n,3,res_above) &&
                    get(Porder,n,4,comp_above) && get(Porder,n,5,order)))
                { KDU_ERROR(e,128); e <<
                    KDU_TXT("Information required to write POC marker "
                    "segment is not currently complete!");
                }
              if (!(ref->compare(Porder,n,0,res_min) &&
                    ref->compare(Porder,n,1,comp_min) &&
                    ref->compare(Porder,n,2,layer_above) &&
                    ref->compare(Porder,n,3,res_above) &&
                    ref->compare(Porder,n,4,comp_above) &&
                    ref->compare(Porder,n,5,order)))
                break;
            }
          if (n < num_records)
            return 0;
        }
    }

  // We are now committed to writing the marker, or at least simulating it.

  int num_components=0, component_bytes, max_comp_above;
  kdu_params *siz = access_cluster(SIZ_params);
  if ((siz == NULL) || !siz->get(Scomponents,0,0,num_components))
    abort();
  if (num_components <= 256)
    { component_bytes = 1; max_comp_above = 256; }
  else
    { component_bytes = 2; max_comp_above = 16384; }

  int profile = 0;
  if (siz != NULL)
    siz->get(Sprofile,0,0,profile);
  if ((profile == Sprofile_CINEMA2K) || (profile == Sprofile_BROADCAST))
    { KDU_ERROR(e,0x2604100E); e <<
      KDU_TXT("Profile violation detected.  POC marker segments may not "
              "be included in 2K Digital Cinema (CINEMA2K) or BROADCAST "
              "code-streams.  You should either modify the `Sprofile' "
              "attribute or remove the `Porder' attribute.");
    }
  else if (profile == Sprofile_CINEMASS)
    { KDU_ERROR(e,0x21091303); e <<
      KDU_TXT("Profile violation detected.  POC marker segments may not "
              "be included in code-streams marked with the \"long term "
              "storage\" digital cinema profile.");
    }
  else if ((profile >= Sprofile_CINEMA4K) &&
           (profile <= Sprofile_CINEMA4S) && (tile_idx >= 0))
    { KDU_ERROR(e,0x11110831); e <<
      KDU_TXT("Profile violation detected.  POC marker segments may not "
              "be included in tile-part headers of Digital Cinema "
              "code-streams.  You should either remove the `Sprofile' "
              "attribute, or remove the tile-specific `Porder' attribute.");
    }

  length = 4 + num_records*(1 + component_bytes + 2 + 1 + component_bytes + 1);
  if (out == NULL)
    return length;

  int acc_length = 0;

  acc_length += out->put(KDU_POC);
  acc_length += out->put((kdu_uint16)(length-2));
  for (n=0; n < num_records; n++)
    {
      if (!(get(Porder,n,0,res_min) && get(Porder,n,1,comp_min) &&
            get(Porder,n,2,layer_above) && get(Porder,n,3,res_above) &&
            get(Porder,n,4,comp_above) && get(Porder,n,5,order)))
        { KDU_ERROR_DEV(e,129); e <<
            KDU_TXT("Information required to write POC marker "
            "segment is not currently complete!");
        }
      if ((res_min < 0) || (res_min >= 33))
        { KDU_ERROR(e,130); e <<
            KDU_TXT("Illegal lower bound, ") << res_min <<
            KDU_TXT(", for resolution level indices in progression order "
            "change attribute.  Legal range is from 0 to 32.");
        }
      if ((res_above <= res_min) || (res_above > 33))
        { KDU_ERROR(e,131); e <<
            KDU_TXT("Illegal upper bound (exclusive), ") << res_above <<
            KDU_TXT(", for resolution level indices in progression order "
            "change attribute.  Legal range is from the lower bound + 1 "
            "to 33.");
        }
      if ((comp_min < 0) || (comp_min >= max_comp_above))
        { KDU_ERROR(e,132); e <<
            KDU_TXT("Illegal lower bound, ") << comp_min <<
            KDU_TXT(", for component indices in progression order "
            "change attribute.  Legal range is from 0 to ")
            << max_comp_above-1 << ".";
        }
      if ((comp_above <= comp_min) || (comp_above > max_comp_above))
        { KDU_ERROR(e,133); e <<
            KDU_TXT("Illegal upper bound (exclusive), ")
            << comp_above <<
            KDU_TXT(", for component indices in progression order change "
            "attribute.  Legal range is from the lower bound + 1 to ")
            << max_comp_above << ".";
        }
      if ((layer_above < 0) || (layer_above >= (1<<16)))
        { KDU_ERROR(e,134); e <<
            KDU_TXT("Illegal upper bound (exclusive), ")
            << layer_above <<
            KDU_TXT(", for layer indices in progression order change "
            "attribute.  Legal range is from 0 to ") << (1<<16)-1 << ".";
        }

      if ((comp_above >= max_comp_above) && (component_bytes == 1))
        comp_above = 0; // Interpreted as 256

      acc_length += out->put((kdu_byte) res_min);
      if (component_bytes == 1)
        acc_length += out->put((kdu_byte) comp_min);
      else
        acc_length += out->put((kdu_uint16) comp_min);
      acc_length += out->put((kdu_uint16) layer_above);
      acc_length += out->put((kdu_byte) res_above);
      if (component_bytes == 1)
        acc_length += out->put((kdu_byte) comp_above);
      else
        acc_length += out->put((kdu_uint16) comp_above);
      acc_length += out->put((kdu_byte) order);
    }

  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                       poc_params::check_marker_segment                    */
/*****************************************************************************/

bool
  poc_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  c_idx = -1;
  return (code == KDU_POC);
}

/*****************************************************************************/
/*                        poc_params::read_marker_segment                    */
/*****************************************************************************/

bool
  poc_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;
  int num_records, n;

  if (code != KDU_POC)
    return false;
  bp = bytes;
  end = bp + num_bytes;

  int num_components=0, component_bytes;
  kdu_params *siz = access_cluster(SIZ_params);
  if ((siz == NULL) || !siz->get(Scomponents,0,0,num_components))
    abort();
  component_bytes = (num_components <= 256)?1:2;
  
  try {
    num_records = num_bytes / (1+component_bytes+2+1+component_bytes+1);
    if (num_records < 1)
      throw bp;
    for (n=0; n < num_records; n++)
      {
        set(Porder,n,0,kdu_read(bp,end,1));
        set(Porder,n,1,kdu_read(bp,end,component_bytes));
        set(Porder,n,2,kdu_read(bp,end,2));
        set(Porder,n,3,kdu_read(bp,end,1));
        int comp_above = kdu_read(bp,end,component_bytes);
        if ((comp_above == 0) && (component_bytes == 1))
          comp_above = 256;
        set(Porder,n,4,comp_above);
        set(Porder,n,5,kdu_read(bp,end,1));
      }
    if (bp != end)
      { KDU_ERROR(e,135); e <<
          KDU_TXT("Malformed POC marker segment encountered. The final ")
          << (int)(end-bp) <<
          KDU_TXT(" bytes were not consumed!");
      }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,136);  e <<
        KDU_TXT("Malformed POC marker segment encountered. "
        "Marker segment is too small.");
    }

  return true;
}


/* ========================================================================= */
/*                                crg_params                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          crg_params::new_object                           */
/*****************************************************************************/

kdu_params *crg_params::new_object()
{
  return new crg_params(); 
}

/*****************************************************************************/
/*                       crg_params::crg_params (no args)                    */
/*****************************************************************************/

crg_params::~crg_params() {}
crg_params::crg_params()
  : kdu_params(CRG_params,false,false,false)
{
  define_attribute(CRGoffset,
                   "Provides additional component registration offsets. "
                   "The offsets add to those implied by the canvas "
                   "coordinate system and should only be used when "
                   "canvas coordinates (notably `Ssize', `Soffset' and "
                   "`Ssampling') cannot be found, which adequately reflect "
                   "the relative displacement of the components. Each record "
                   "specifies offsets for one component, with the vertical "
                   "offset appearing first. Offsets must be in the range "
                   "0 (inclusive) to 1 (exclusive) and represent a fraction "
                   "of the relevant component sub-sampling factor (see "
                   "`Ssampling'). The last supplied record is repeated "
                   "as needed to recover offsets for all components. ",
                   "FF",MULTI_RECORD | CAN_EXTRAPOLATE);
}

/*****************************************************************************/
/*                        crg_params::copy_with_xforms                       */
/*****************************************************************************/

void
  crg_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  if (vflip || hflip)
    { // Can't deal with flipping.  Best to destroy any existing information.
      delete_unparsed_attribute(CRGoffset);
      return;
    }

  // Start by finding out how the canvas dimensions have been changed
  kdu_params *source_siz = source->access_cluster(SIZ_params);
  kdu_params *target_siz = this->access_cluster(SIZ_params);
  assert((source_siz != NULL) && (target_siz != NULL));
  kdu_coords source_canvas, source_origin, target_canvas, target_origin;
  if (!(source_siz->get(Ssize,0,0,source_canvas.y) &&
        source_siz->get(Ssize,0,1,source_canvas.x) &&
        source_siz->get(Sorigin,0,0,source_origin.y) &&
        source_siz->get(Sorigin,0,1,source_origin.x) &&
        target_siz->get(Ssize,0,0,target_canvas.y) &&
        target_siz->get(Ssize,0,1,target_canvas.x) &&
        target_siz->get(Sorigin,0,0,target_origin.y) &&
        target_siz->get(Sorigin,0,1,target_origin.x)))
    assert(0);
  source_canvas -= source_origin;
  target_canvas -= target_origin;
  if (transpose)
    source_canvas.transpose();
  int fact_x = source_canvas.x / target_canvas.x;
  int fact_y = source_canvas.y / target_canvas.y;
  assert((fact_x > 0) && (fact_y > 0)); // Actually should be powers of 2

  float y=0.0F, x=0.0F;
  int n = 0;
  while (source->get(CRGoffset,n,(transpose)?1:0,y,false,false) &&
         source->get(CRGoffset,n,(transpose)?0:1,x,false,false))
    {
      y = y / (float) fact_y;
      x = x / (float) fact_x;
      if (n >= skip_components)
        {
          set(CRGoffset,n-skip_components,0,y);
          set(CRGoffset,n-skip_components,1,x);
        }
      n++;
    }
  if ((n > 0) && (n <= skip_components))
    { // Copy last actual offsets into the first target component.
      set(CRGoffset,0,0,y);
      set(CRGoffset,0,1,x);
    }
}

/*****************************************************************************/
/*                       crg_params::write_marker_segment                    */
/*****************************************************************************/

int
  crg_params::write_marker_segment(kdu_output *out, kdu_params *last_marked,
                                   int tpart_idx)
{
  int length;
  float xoff=0.0F, yoff=0.0F;

  if ((tpart_idx != 0) || (comp_idx >= 0))
    return 0;
  if (!get(CRGoffset,0,0,yoff))
    return 0;
  assert(last_marked == NULL);

  int num_components=0, c;
  kdu_params *siz = access_cluster(SIZ_params);
  if ((siz == NULL) || !siz->get(Scomponents,0,0,num_components))
    abort();

  length = 4 + 4*num_components;
  if (out == NULL)
    return length;

  int acc_length = 0;

  acc_length += out->put(KDU_CRG);
  acc_length += out->put((kdu_uint16)(length-2));
  for (c=0; c < num_components; c++)
    {
      int x, y;

      if (!(get(CRGoffset,c,0,yoff) && get(CRGoffset,c,1,xoff)))
        { KDU_ERROR_DEV(e,137); e <<
            KDU_TXT("Component registration information incomplete!");
        }
      if ((xoff < 0.0F) || (xoff >= 1.0F) || (yoff < 0.0F) || (yoff >= 1.0F))
        { KDU_ERROR(e,138); e <<
            KDU_TXT("Illegal component registration offsets, {")
            << yoff << "," << xoff <<
            KDU_TXT("}.  Legal range is from 0.0 to 1.0 (exclusive).");
        }
      x = (int) floor(0.5F + xoff * (float)(1<<16));
      if (x >= (1<<16))
        x = (1<<16) - 1;
      y = (int) floor(0.5F + yoff * (float)(1<<16));
      if (y >= (1<<16))
        y = (1<<16) - 1;
      acc_length += out->put((kdu_uint16) x);
      acc_length += out->put((kdu_uint16) y);
    }

  assert(length == acc_length);
  return length;
}

/*****************************************************************************/
/*                       crg_params::check_marker_segment                    */
/*****************************************************************************/

bool
  crg_params::check_marker_segment(kdu_uint16 code, int num_bytes,
                                   kdu_byte bytes[], int &c_idx)
{
  c_idx = -1;
  return (code == KDU_CRG);
}

/*****************************************************************************/
/*                        crg_params::read_marker_segment                    */
/******************************************************************************/

bool
  crg_params::read_marker_segment(kdu_uint16 code, int num_bytes,
                                  kdu_byte bytes[], int tpart_idx)
{
  kdu_byte *bp, *end;

  if ((tpart_idx != 0) || (code != KDU_CRG) || (comp_idx >= 0))
    return false;
  bp = bytes;
  end = bp + num_bytes;

  int num_components=0, c;
  kdu_params *siz = access_cluster(SIZ_params);
  if ((siz == NULL) || !siz->get(Scomponents,0,0,num_components))
    abort();

  try {
    for (c=0; c < num_components; c++)
      {
        set(CRGoffset,c,1,kdu_read(bp,end,2)/((float)(1<<16)));
        set(CRGoffset,c,0,kdu_read(bp,end,2)/((float)(1<<16)));
      }
    if (bp != end)
      { KDU_ERROR(e,139);  e <<
          KDU_TXT("Malformed CRG marker segment encountered. The final ")
          << (int)(end-bp) <<
          KDU_TXT(" bytes were not consumed!");
      }
    } // End of try block.
  catch (kdu_byte *)
    { KDU_ERROR(e,140); e <<
        KDU_TXT("Malformed CRG marker segment encountered. "
        "Marker segment is too small.");
    }

  return true;
}


/* ========================================================================= */
/*                                 org_params                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          org_params::new_object                           */
/*****************************************************************************/

kdu_params *org_params::new_object()
{
  return new org_params(); 
}

/*****************************************************************************/
/*                       org_params::org_params (no args)                    */
/*****************************************************************************/

org_params::~org_params() {}
org_params::org_params()
  : kdu_params(ORG_params,true,false,true)
{
  define_attribute(ORGtparts,
     "Controls the division of each tile's packets into tile-parts.  The "
     "attribute consists of one or more of the flags, `ORGtparts_R', "
     "`ORGtparts_L' and `ORGtpargs_C'.  If the `ORGtparts_R' flag is "
     "supplied, tile-parts will be introduced as necessary to ensure "
     "that each tile-part consists of packets from only one resolution "
     "level.  If `ORGtparts_L' is supplied, tile-parts are introduced as "
     "necessary to ensure that each tile-part consists of packets from "
     "only one quality layer.  Similarly, if `ORGtparts_C' is supplied, "
     "each tile-part will consist of packets from only one component.  "
     "Note that the cost of extra tile-part headers will not be taken "
     "into account during rate control, so that the code-stream may end "
     "up being a little larger than you expect.  By default, tile-part "
     "boundaries are introduced only as required by the presence "
     "of multiple `Porder' attribute specifications (see `poc_params') "
     "and/or incremental codestream flushing, which can produce "
     "tile-part boundaries due to interruption in the set of available "
     "J2K packets during a flush cycle; these interruptions can be "
     "bounded via the `ORGtpart_interrupts' attribute.\n"
     "\t\t[By default, tile-part bondaries are introduced only as "
     "required by incremental flushing or the presence of multiple "
     "\"Porder\" attribute specifications.]",
     "[R=1|L=2|C=4]",ALL_COMPONENTS);
  define_attribute(ORGgen_plt,
                   "Requests the insertion of packet length information "
                   "in the header of all tile-parts associated with tiles "
                   "for which this attribute is turned on (has a value of "
                   "\"yes\").  The PLT marker segments written into the "
                   "relevant tile-part headers will hold the lengths of "
                   "those packets which belong to the same tile-part.  Note "
                   "that the cost of any PLT marker segments generated "
                   "as a result of this attribute being enabled will not be "
                   "taken into account during rate allocation.  This means "
                   "that the resulting code-streams will generally be a "
                   "little larger than one might expect; however, this is "
                   "probably a reasonable policy, since the PLT marker "
                   "segments may be removed without losing any information.  "
                   "Also note that the `ORGplt_parts' attribute may be used to "
                   "take control over the way in which PLT information is "
                   "partitioned into distinct PLT marker segments.",
                   "B",ALL_COMPONENTS);
  define_attribute(ORGplt_parts,
                   "Controls the division of packet length information into "
                   "PLT marker segments; this attribute has no impact unless "
                   "`ORGgen_plt' is true.  The attribute consists of one or "
                   " more of the flags `R', `L' and `C', separated by the "
                   "vertical bar character, `|'.  If the `R' flag is supplied, "
                   "new PLT marker segments will be started as required to "
                   "ensure that no PLT marker segment contains length "
                   "information for packets that belong to different "
                   "resolution levels.  Similarly, the `L' and `C' flags "
                   "cause packet length information to be partitioned into "
                   "distinct PLT marker segments at quality layer and "
                   "codestream image component boundaries, respectively.\n"
                   "\t\t[By default, packet length information is packed into "
                   "the smallest possible collection of PLT marker segments.]",
                   "[R=1|L=2|C=4]",ALL_COMPONENTS);
  define_attribute(ORGgen_tlm,
     "Requests the insertion of TLM (tile-part-length) marker "
     "segments in the main header, to facilitate random access "
     "to the code-stream.  This attribute takes a single integer-valued "
     "parameter, which identifies the maximum number of tile-parts "
     "which will be written to the code-stream for each tile.  The "
     "reason for including this parameter is that space for the "
     "TLM information must be reserved ahead of time; once the "
     "entire code-stream has been written the generation machinery "
     "goes back and overwrites this reserved space with actual TLM "
     "data.  If the actual number of tile-parts which are generated "
     "is less than the value supplied here, empty tile-parts will be "
     "inserted into the code-stream so as to use up all of the "
     "reserved TLM space.  For this reason, you should try to "
     "estimate the maximum number of tile-parts you will need as "
     "accurately as possible, noting that the actual value may be "
     "hard to determine ahead of time if incremental flushing "
     "features are to be employed.  An error will be generated at "
     "run-time if the number of declared maximum number of tile-parts "
     "turns out to be insufficient.  Fortunately, you can bound the "
     "impact of incremental flushing on the number of extra tile-parts "
     "that may be introduced beyond the control of the coding parameters "
     "themselves by specifying a value for the `ORGtpart_interrupts' "
     "attribute.  You should note that the `ORGgen_tlm' attribute may be "
     "ignored if the `kdu_compressed_target' object, to which generated "
     "data is written, does not support repositioning functionality.",
     "I",ALL_COMPONENTS);
  define_attribute(ORGtlm_style,
                   "This attribute can be used to control the format used "
                   "to record TLM (tile-part-length) marker segments; it is "
                   "relevant only in conjunction with \"ORGgen_tlm\".  The "
                   "standard defines 6 different formats for the TLM marker "
                   "segment, some of which are more compact than others.  "
                   "The main reason for providing this level of control is "
                   "that some applications/profiles may expect a specific "
                   "format to be used.  By default, each record in a TLM "
                   "marker segment is written with 6 bytes, 2 of which "
                   "identify the tile number, while the remaining 4 give "
                   "the length of the relevant tile-part.  This attribute "
                   "takes two fields: the first field specifies the number "
                   "of bytes to be used to record tile numbers (0, 1 or 2); "
                   "the second field specifies the number of bytes to be "
                   "used to record tile-part lengths (2 or 4).  The "
                   "values provided here might not be checked ahead of time, "
                   "which means that some combinations may be found to be "
                   "illegal at some point during the compression process.  "
                   "Also, the first field may be 0 (meaning \"implied\") "
                   "only if tiles are written in order and have exactly one "
                   "tile-part each.  This is usually the case if "
                   "\"ORGtparts\" is not used, but incremental flushing of "
                   "tiles which are generated in an unusual order may "
                   "violate this assumption -- this sort of thing can "
                   "happen if Kakadu's appearance transforms are used to "
                   "compress imagery which is presented in a transposed "
                   "or flipped order, for example.",
                   "(implied=0,byte=1,short=2)(short=2,long=4)",
                   ALL_COMPONENTS);
  define_attribute(ORGtpart_interrupts,
     "Can be used to bound the impact of incremental codestream flushing "
     "on the introduction of tile-parts into the generated codestream.  "
     "This attribute is irrelevant if you are not doing incremental "
     "flushing, or if you are flushing to a structured compressed data "
     "cache, rather than a sequential codestream.  Incremental flushing "
     "may result in some precincts of a tile being available before others "
     "so that a flush cycle needs to output some packets of the tile to "
     "a separate tile-part, leaving the rest to follow in a later "
     "tile-part.  These interruptions can be hard to predict, especially "
     "in multi-threaded implementations, which can result in the hard limit "
     "of 255 tile-parts per tile being exceeded or the number of tile-parts "
     "reserved by `ORGgen_tlm' for main header tile-part pointers being "
     "insufficient.  To avoid both difficulties, this attribute allows you "
     "to specify a bound on the maximum number of tile-part boundaries "
     "that can occur as a result of data interruptions created by "
     "incremental flushing.  You should factor this bound into the value "
     "you pass with any `ORGgen_tlm' parameter attribute.  If the bound "
     "is at risk of being exceeded, the incremental flushing of content "
     "for a tile is suspended until all precincts of the tile have been "
     "generated.  You should be aware that this may reduce the performance "
     "of incremental flushing, in which case you should consider enlarging "
     "the bound supplied via `ORGtpart_interrupts'.\n"
     "\t\t[Defaults to 200, which is large enough to accommodate most "
     "incremental flushing configurations and leaves enough remaining "
     "tile-parts to accommodate most `ORGtparts' configurations without "
     "violating the hard limit of 255 parts per tile.  However, if you "
     "are using `ORGgen_tlm' to generate tile-part pointer information, "
     "you really should choose a suitable small value for this parameter "
     "and add it to the minimum value that would be required "
     "by `ORGgen_tlm' in the absence of incremental flushing.]",
     "I",ALL_COMPONENTS);
}

/*****************************************************************************/
/*                       org_params::copy_with_xforms                        */
/*****************************************************************************/

void
  org_params::copy_with_xforms(kdu_params *source,
                               int skip_components, int discard_levels,
                               bool transpose, bool vflip, bool hflip)
{
  int flags=0, tnum_prec=0, tplen_prec=0;
  bool yes_no=false;
  if (source->get(ORGtparts,0,0,flags,false))
    set(ORGtparts,0,0,flags);
  if (source->get(ORGgen_plt,0,0,yes_no,false))
    set(ORGgen_plt,0,0,yes_no);
  if (source->get(ORGplt_parts,0,0,flags,false))
    set(ORGplt_parts,0,0,flags);
  if (source->get(ORGtlm_style,0,0,tnum_prec,false) &&
      source->get(ORGtlm_style,0,1,tplen_prec,false)) 
    { set(ORGtlm_style,0,0,tnum_prec); set(ORGtlm_style,0,1,tplen_prec); }
}
