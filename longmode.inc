; InitraOS - Long Mode Transition Support
;
; Issue #77:
; Check whether the CPU supports x86-64 long mode.
;
; Issue #80:
; Enable the CPU state required before the later
; 64-bit GDT/far-jump transition.
;
; This file does NOT perform the final far jump.
; That belongs to the later transition issues.

bits 32


%define LONG_MODE_STATUS 0x4FF0

; Keep this file independent of include order.
; Issue #79 places the PML4 at 0x20000.
%ifndef PML4_TABLE_ADDRESS
    %define PML4_TABLE_ADDRESS 0x00020000
%endif


; =========================================================
; Check CPU long-mode support
; =========================================================
;
; Returns:
;   CF = 0 -> long mode supported
;   CF = 1 -> not supported
;
; Also writes:
;   0 -> unsupported
;   1 -> supported
;
; All general-purpose registers are preserved.
; =========================================================

check_long_mode:

    push eax
    push ebx
    push ecx
    push edx

    mov byte [LONG_MODE_STATUS], 0

    ; Get highest extended CPUID leaf.
    mov eax, 0x80000000
    cpuid

    cmp eax, 0x80000001
    jb .unsupported

    ; Extended processor features.
    mov eax, 0x80000001
    cpuid

    ; EDX bit 29 = Long Mode.
    test edx, (1 << 29)
    jz .unsupported

    mov byte [LONG_MODE_STATUS], 1

    pop edx
    pop ecx
    pop ebx
    pop eax

    clc
    ret


.unsupported:

    pop edx
    pop ecx
    pop ebx
    pop eax

    stc
    ret


; =========================================================
; Issue #80 - Enable long-mode prerequisites
; =========================================================
;
; Sequence:
;
;   1. Disable current paging
;   2. Enable PAE
;   3. Load PML4 into CR3
;   4. Enable EFER.LME
;   5. Re-enable paging
;
; No GDT reload.
; No far jump.
; No 64-bit code is executed here.
;
; Those belong to later transition issues.
; =========================================================

enable_long_mode:

    ; -----------------------------------------------------
    ; 1. Disable current 32-bit paging.
    ; -----------------------------------------------------

    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax


    ; -----------------------------------------------------
    ; 2. Enable Physical Address Extension.
    ; CR4.PAE = bit 5.
    ; -----------------------------------------------------

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax


    ; -----------------------------------------------------
    ; 3. Load the physical address of the PML4.
    ; -----------------------------------------------------

    mov eax, PML4_TABLE_ADDRESS
    mov cr3, eax


    ; -----------------------------------------------------
    ; 4. Enable Long Mode Enable in IA32_EFER.
    ;
    ; MSR 0xC0000080
    ; LME = bit 8
    ; -----------------------------------------------------

    mov ecx, 0xC0000080
    rdmsr

    or eax, (1 << 8)

    wrmsr


    ; -----------------------------------------------------
    ; 5. Re-enable paging.
    ;
    ; The CPU is now prepared for the later IA-32e
    ; transition, but execution remains under the current
    ; protected-mode code segment until the later far jump.
    ; -----------------------------------------------------

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ret