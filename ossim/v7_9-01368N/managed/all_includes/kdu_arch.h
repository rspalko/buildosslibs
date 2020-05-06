/*****************************************************************************/
// File: kdu_arch.h [scope = CORESYS/COMMON]
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
   Definitions and functions which provide information about the machine
architecture, including information about special instruction sets for
vector arithmetic (MMX, SSE, Altivec, Sparc-VIS, etc.).
   All external variables and functions defined here are implemented in
"kdu_arch.cpp".
******************************************************************************/

#ifndef KDU_ARCH_H
#define KDU_ARCH_H

#include "kdu_ubiquitous.h"

namespace kdu_core {

/* ========================================================================= */
/*                      SIMD Support Testing Variables                       */
/* ========================================================================= */

KDU_EXPORT extern
  int kdu_mmx_level;
  /* [SYNOPSIS]
     Indicates the level of MMX support offered by the architecture:
     [>>] 0 if the architecture does not support MMX instructions (e.g.,
          non-Intel processor);
     [>>] 1 if the architecture supports MMX instructions only;
     [>>] 2 if the architecture also supports SSE and SSE2;
     [>>] 3 if the architecture also supports SSE3;
     [>>] 4 if the architecture also supports SSSE3;
     [>>] 5 if the architecture also supports SSE4.1, SSE4.2 and POPCNT;
     [>>] 6 if the architecture also supports AVX;
     [>>] 7 if the architecture also supports AVX2 and FMA
  */

KDU_EXPORT extern
  int kdu_get_mmx_level();
  /* [SYNOPSIS]
       This function should return exactly the same value that is stored in
       the global read-only variable `kdu_mmx_level'.  The only reason for
       providing the function is that it can be used in the construction of
       static/global objects and the initialization of other static/global
       variables -- the initialization code for such objects and variables
       is invoked in an uncertain order, possibly before the value of
       `kdu_mmx_level' is known, so you should use this function to
       explicitly evaluate the level of support if needed in such
       initialization functions.
  */

KDU_EXPORT extern
  bool kdu_pentium_cmov_exists;
  /* [SYNOPSIS]
     Indicates whether the X86 CMOV (conditional move) instruction
     is known to exist.
  */

KDU_EXPORT extern
  bool kdu_x86_bmi2_exists;
  /* [SYNOPSIS]
     Indicates whether the X86 BMI instructions (BMI, BMI1, BMI2 and also
     LZCNT) are all known to exist.
  */

KDU_EXPORT extern
  int kdu_neon_level;
  /* [SYNOPSIS]
     Indicates whether ARM-NEON processor is found to be available and, if
     so, what features are available.  Currently, only the following values
     are defined:
     [>>] 0 if the architecture does not support NEON accelerations;
     [>>] 1 if the architecture supports 128-bit NEON instructions for
          8-, 16-, 32- and 64-bit integer and 32-bit float operations.
  */
KDU_EXPORT extern
  int kdu_get_neon_level();
  /* [SYNOPSIS]
       This function should return exactly the same value that is stored in
       the global read-only variable `kdu_neon_level'.  The only reason for
       providing the function is that it can be used in the construction of
       static/global objects and the initialization of other static/global
       variables -- the initialization code for such objects and variables
       is invoked in an uncertain order, possibly before the value of
       `kdu_neon_level' is known, so you should use this function to
       explicitly evaluate the level of support if needed in such
       initialization functions.
  */
KDU_EXPORT extern
  bool kdu_sparcvis_exists;
  /* [SYNOPSIS]
     True if the architecture is known to support the SPARC visual
     instruction set.
  */

KDU_EXPORT extern
  bool kdu_get_sparcvis_exists();
  /* [SYNOPSIS]
       This function should return exactly the same value that is stored in
       the global read-only variable `kdu_sparcvis_exists'.  The only reason
       for providing the function is that it can be used in the construction of
       static/global objects and the initialization of other static/global
       variables -- the initialization code for such objects and variables
       is invoked in an uncertain order, possibly before the value of
       `kdu_sparcvis_exists' is known, so you should use this function to
       explicitly evaluate the level of support if needed in such
       initialization functions.
  */

KDU_EXPORT extern
  bool kdu_altivec_exists;
  /* [SYNOPSIS]
     True if the architecture is known to support the Altivec instruction --
     i.e., the fast vector processor available on many G4/G5 PowerPC
     CPU's.
  */

KDU_EXPORT extern
  bool kdu_get_altivec_exists();
  /* [SYNOPSIS]
       This function should return exactly the same value that is stored in
       the global read-only variable `kdu_altivec_exists'.  The only reason
       ro providing the function is that it can be used in the construction of
       static/global objects and the initialization of other static/global
       variables -- the initialization code for such objects and variables
       is invoked in an uncertain order, possibly before the value of
       `kdu_altivec_exists' is known, so you should use this function to
       explicitly evaluate the level of support if needed in such
       initialization functions.
  */

#ifdef KDU_MAC_SPEEDUPS
#  if defined(__ppc__) || defined(__ppc64__)
#    ifndef KDU_ALTIVEC_GCC
#      define KDU_ALTIVEC_GCC
#    endif
#  endif
#  if defined(__i386__) || defined(__x86_64__)
#    ifndef KDU_X86_INTRINSICS
#      define KDU_X86_INTRINSICS
#    endif
#  endif
#endif // KDU_MAC_SPEEDUPS

#ifdef __ARM_NEON__
#  ifndef KDU_NO_NEON
#    ifndef KDU_NEON_INTRINSICS
#      define KDU_NEON_INTRINSICS
#    endif
#  endif
#endif

#ifdef KDU_NO_NEON
#  ifdef KDU_NEON_INTRINSICS
#    undef KDU_NEON_INTRINSICS
#  endif
#endif

#ifndef KDU_MIN_MMX_LEVEL
#  if ((defined KDU_MAC_SPEEDUPS) && !(defined KDU_NO_SSSE3))
#    define KDU_MIN_MMX_LEVEL 4
#  else
#    define KDU_MIN_MMX_LEVEL 2
#  endif
#endif // Default KDU_MIN_MMX_LEVEL

#if (defined KDU_PENTIUM_MSVC)
# undef KDU_PENTIUM_MSVC
# ifndef KDU_X86_INTRINSICS
#   define KDU_X86_INTRINSICS // All accelerators now use portable intrinsics
# endif
#endif // KDU_PENTIUM_MSVC

#if (defined KDU_PENTIUM_GCC)
# undef KDU_PENTIUM_GCC
# ifndef KDU_X86_INTRINSICS
#   define KDU_X86_INTRINSICS // All accelerators now use portable intrinsics
# endif
#endif // KDU_PENTIUM_GCC

#if (defined _WIN64) && !(defined KDU_NO_MMX64)
#  define KDU_NO_MMX64  // 64-bit processors always support at least SSE2
#endif // _WIN64 && !KDU_NO_MMX64

#ifdef KDU_NEON_INTRINSICS
#  ifdef _MSC_VER
#    define KD_ARM_PREFETCH(_addr) __prefetch(_addr)
#  else
#    define KD_ARM_PREFETCH(_addr) __builtin_prefetch(_addr)
#  endif
#endif


/* ========================================================================= */
/*                    Macros that `kdu_sample_allocator'                     */
/* ========================================================================= */

// The following macros play a critical role in ensuring safe use of SIMD
// accelerators in different architectures.  The fallback position, if none
// of the following are defined, is for sample buffers to be allocated with
// sufficient alignment only for 128-bit vector processing.
//    To avoid the risk that sample buffers are allocated by an application
// that is linked to a shared/dynamic library which was compiled with different
// values for these macros, we provide the `kdu_check_sample_alignment' macro
// that causes a run-time error to be issued if alignment inconsistency is
// detected between linked binaries.

#if (defined KDU_X86_INTRINSICS)
#  define KDU_SAMPLE_ALIGNMENT_AVX2
#  define KDU_SAMPLE_ALIGNMENT_AVX
#endif

/* Notes:
 1. The following quantities are all expected to be powers of 2.
 2. The `KDU_PREALIGN_BYTES' value must be no smaller than
    max{ 2*`KDU_ALIGN_SAMPLES16', 4*`KDU_ALIGN_SAMPLES32' }
 */

#if (defined KDU_SAMPLE_ALIGNMENT_MIC)

#  define KDU_OVERREAD_BYTES   256
#  define KDU_PREALIGN_BYTES    64
#  define KDU_ALIGN_SAMPLES16   16
#  define KDU_ALIGN_SAMPLES32   16

#elif (defined KDU_SAMPLE_ALIGNMENT_AVX2)

#  define KDU_OVERREAD_BYTES   128
#  define KDU_PREALIGN_BYTES    64
#  define KDU_ALIGN_SAMPLES16   16
#  define KDU_ALIGN_SAMPLES32    8

#elif (defined KDU_SAMPLE_ALIGNMENT_AVX)

#  define KDU_OVERREAD_BYTES   128
#  define KDU_PREALIGN_BYTES    64
#  define KDU_ALIGN_SAMPLES16    8
#  define KDU_ALIGN_SAMPLES32    8

#else

#  define KDU_OVERREAD_BYTES   128
#  define KDU_PREALIGN_BYTES    32
#  define KDU_ALIGN_SAMPLES16    8
#  define KDU_ALIGN_SAMPLES32    4

#endif

KDU_EXPORT extern bool
  kdu_core_sample_alignment_checker(int overread_bytes, int prealign_bytes,
                                    int align_samples16, int align_samples32,
                                    bool return_on_fail, bool strict);
  /* [SYNOPSIS]
       This function is provided to facilitate run-time verification that the
       alignment conditions under which the core system was compiled are
       compatible with those under which an application or other library was
       built.  The check needs to be made at run-time to properly handle
       shared/dynamic libraries, catching conditions in which the core system
       may be assuming different alignment conditions to those that prevail
       at the point when a dependent application is built.
       [//]
       Applications should not need to invoke this function directly, but
       might consider invoking the function via the
       `kdu_check_sample_alignment' macro, that is called with function
       semantics, taking no arguments.  This should be done whenever an
       application provides its own SIMD accelerator functions that rely upon
       some alignemnt conditions that are tested via the macros defined in
       this header file.  The macro invokes this function with `return_on_fail'
       false and `strict' also false, which means that an error will be
       generated through `kdu_error' if and only if the alignment conditions
       under which the core system was compiled are less strict than those
       advertised by the macros above.
       [//]
       The function is called automatically by the `kdu_sample_allocator'
       object when it actually allocates memory; in that case, the
       `return_on_fail' argument is also false, but `strict' is set to true.
       This protects an application against the possibility that it allocates
       sample buffers itself using alignment that is less strict than that
       expected by the core system.
     [ARG: return_on_fail]
       If true, the function returns false, rather than generating an error
       through `kdu_error', in the event that incompatibility is detected.
     [ARG: strict]
       If true, the function considers alignment to be incompatible if any
       of the first four arguments differ from the values of the corresponding
       macros, as seen by the caller.  If this argument is false, the function
       considers alignment to be compatible so long as the alignment
       requirements represented by the first four arguments are no stronger
       than the values of the corresponding macros at the time when the
       core system was compiled.  For example, with `strict' false, it is
       OK if the core system was compiled with a `KDU_ALIGN_SAMPLE16' value
       that was larger than that supplied via the `align_sample16' argument, 
       but it is not OK if `KDU_ALIGN_SAMPLE16' was smaller than the value
       passed for the `align_sample16' argument.
  */

#define kdu_check_sample_alignment() \
  kdu_core_sample_alignment_checker(KDU_OVERREAD_BYTES,KDU_PREALIGN_BYTES, \
                                    KDU_ALIGN_SAMPLES16,KDU_ALIGN_SAMPLES32, \
                                    false,true);

/* ========================================================================= */
/*                          Cache-Related Macros                             */
/* ========================================================================= */

#define KDU_MAX_L2_CACHE_LINE 64 // Assumed max L2 cache bytes (power of 2)
        /* The above value works for all known x86 and ARM family processors
           of which we are aware, but for other systems you might like to
           condition the definition of this macro on architecture-specific
           predefines.  Certainly some ARM processors use 32-byte cache
           lines, but there is no real value in reducing the macro below
           64.  There is an implicit assumption in Kakadu that L2 cache
           lines will be at least as large as L1 cache lines.  If this is
           not the case, you should ensure that `KDU_MAX_L2_CACHE_LINE'
           is also at least as large as the L1 cache line size. */

#define KDU_CODE_BUFFER_ALIGN KDU_MAX_L2_CACHE_LINE
     /* Note: code buffers are best allocated to occupy (and be aligned on)
        whole cache-line boundaries.  A common L1 cache line size for
        modern processors is 64 bytes.  The situation is less clear for
        the L2 cache, but Intel processors typically read/write to/from
        the L2 cache in multiples of 64 bytes and it is best to avoid
        sharing lines of this size between threads. */

#define KD_PRIVATE_CACHELINE_SEPARATOR(_name) \
  protected: kdu_byte _name[KDU_MAX_L2_CACHE_LINE]; private:
     /* Inserts a separator of at least one notional L2 cache line into
        the data members of a class/struct definition.  It is assumed that
        the separator is inserted within a "private" scoped part of the
        class/struct definition -- to avoid pedantic compiler warnings, the
        separator is defined protected and then the scope is restored to
        private. */

#define KD_PUBLIC_CACHELINE_SEPARATOR(_name) \
  protected: kdu_byte _name[KDU_MAX_L2_CACHE_LINE]; public:
     /* As above, but inserts the separator into a portion of the class/struct
        definition that is currently scoped as public. */


/* ========================================================================= */
/*                          Number of Processors                             */
/* ========================================================================= */

KDU_EXPORT extern int
  kdu_get_num_processors();
  /* [SYNOPSIS]
       This function returns the total number of logical processors which
       are available to the current process, or 0 if the value cannot be
       determined.
       [//]
       Note that on Windows systems that have multiple processor groups
       (typically very large systems with more than 64 logical CPUs), this
       function will return the number of logical CPUs in the processor
       group to which the current process belongs, unless individual
       threads have already been bound to different processor groups
       (e.g., via calls to `kdu_thread::set_cpu_affinity' with different
       `affinity_context' arguments), in which case the function will
       return 0.
       [//]
       For a variety of good reasons, POSIX refuses to
       standardize a consistent mechanism for discovering the number of
       system processors, although their are a variety of platform-specific
       methods around.  This function works on OSX and most Linux
       systems, but if the number of processors cannot be discovered, the
       function returns 0.
  */
  
} // namespace kdu_core

#endif // KDU_ARCH_H
