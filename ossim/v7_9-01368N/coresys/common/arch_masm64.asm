COMMENT $
/*****************************************************************************/
// File: arch_masm64.asm [scope = CORESYS/COMMON]
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
   Provides CPUID testing code for 64-bit X86 platforms, coded for the 64-bit
version of the Microsoft Macro Assembler (MASM).  This is the only way to
perform CPUID support testing in Microsoft's .NET 2005 compiler.
******************************************************************************/
$

;=============================================================================
; MACROS
;=============================================================================


.code

;=============================================================================
; EXTERNALLY CALLABLE FUNCTIONS FOR REGULAR BLOCK DECODING
;=============================================================================

;*****************************************************************************
; PROC: x64_get_mmx_level
;*****************************************************************************
x64_get_mmx_level PROC USES rbx
      ; Result (integer) returned via EAX/RAX
      ; Registers used: RAX, RCX, RDX

  xor ecx, ecx
  mov eax, 1
  cpuid
  test edx, 00800000h
  JZ no_mmx_exists
    mov eax, edx
    and eax, 06000000h  
    cmp eax, 06000000h
    JNZ mmx_exists
      test ecx, 1
      JZ sse2_exists
        test ecx, 200h
        JZ sse3_exists
          mov ebx, ecx
		      and ebx, 00980000h
          cmp ebx, 00980000h ; Are SSE4.1, SSE4.2 and POPCNT flags all set? 
          JNZ ssse3_exists
ifndef KDU_NO_AVX
            and ecx, 18000000h  ; Mask off OSXSAVE and AVX feature flags
            cmp ecx, 18000000h  ; Check that both flags are present
            JNE sse41_exists
              xor ecx, ecx    ; Specify 0 for XFEATURE_ENABLED_MASK
        			xgetbv          ; Result returned in EDX:EAX
		  	      and eax, 06h
              cmp eax, 06h    ; Check OS saves both XMM and YMM state
		        	JE avx_exists   ; AVX is fully supported
endif
		      JMP sse41_exists
avx2_exists:
  mov eax, 7
  JMP done
avx_exists:
ifndef KDU_NO_AVX2
    xor ecx,ecx
    xor ebx,ebx
    mov eax, 7    ; Call cpuid with EAX=7 (tests above used EAX=1)
    cpuid
    test ebx, 020h  ; Check bit-5 of EBX (AVX2 bit)
    JNZ avx2_exists
endif
  mov eax, 6
  JMP done
sse41_exists:
  mov eax, 5
  JMP done
ssse3_exists:
  mov eax, 4
  JMP done
sse3_exists:
  mov eax, 3
  JMP done
sse2_exists:
  mov eax, 2
  JMP done
mmx_exists:
  mov eax, 1
  JMP done
no_mmx_exists:
  mov eax, 0
done:
  ret
;----------------------------------------------------------------------------- 
x64_get_mmx_level ENDP

;*****************************************************************************
; PROC: x64_get_cmov_exists
;*****************************************************************************
x64_get_cmov_exists PROC USES rbx
      ; Result (boolean) returned via EAX/RAX
      ; Registers used: RAX, RCX, RDX
  xor ecx, ecx
  mov eax, 1
  cpuid
  test edx, 00008000h
  mov eax, 0
  JZ @F
    mov eax, 1 ; CMOV exists
@@:
  ret
;----------------------------------------------------------------------------- 
x64_get_cmov_exists ENDP

;*****************************************************************************
; PROC: x64_get_bmi2_exists
;*****************************************************************************
x64_get_bmi2_exists PROC USES rbx
      ; Result (boolean) returned via EAX/RAX
      ; Registers used: RAX, RCX, RDX
  xor ecx, ecx
  xor ebx, ebx
  mov eax, 7
  cpuid
  and ebx, 00000108h
  cmp ebx, 00000108h
  JNE @F ; No BMI2 support
  xor ecx, ecx
  xor ebx, ebx
  mov eax, 80000001h
  cpuid
  test ecx, 32
  JZ @F ; No LZCNT support
  mov eax, 1 ; return true
  JMP bmi2_done
@@:
  mov eax, 0 ; return false
bmi2_done:
  ret
;----------------------------------------------------------------------------- 
x64_get_bmi2_exists ENDP


END
