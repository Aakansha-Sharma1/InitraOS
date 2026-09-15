; InitraOS - Long Mode Transition Preparation
;
; Issue #77:
; Prepare the 32-bit environment for a future x86-64
; long-mode transition.
;
; This file ONLY checks CPU support.
; It does NOT enable long mode.

bits 32

%define LONG_MODE_STATUS 0x4FF0

; ---------------------------------------------------------
; check_long_mode
;
; Returns:
;   CF = 0 -> CPU supports x86-64 long mode
;   CF = 1 -> CPU does not support x86-64 long mode
;
; Result is also stored at LONG_MODE_STATUS:
;   0 = unsupported
;   1 = supported
;
; All registers are preserved.
; ---------------------------------------------------------

check_long_mode:
    push eax
    push ebx
    push ecx
    push edx

    ; Default: long mode unsupported.
    mov byte [LONG_MODE_STATUS], 0

    ; Check whether extended CPUID leaves exist.
    mov eax, 0x80000000
    cpuid

    cmp eax, 0x80000001
    jb .unsupported

    ; Check Long Mode support.
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