; InitraOS - A20 gate control (16-bit real mode)
; The A20 line must be enabled BEFORE entering protected mode, otherwise
; every odd megabyte of the address space aliases onto the one below it.

; ---------------------------------------------------------------
; check_a20 - returns CF=0 if A20 is enabled, CF=1 if it is masked
; Compares 0000:0500 with FFFF:0510 (the same physical byte when
; A20 is off, two different bytes when it is on).
; ---------------------------------------------------------------
check_a20:
    pushf
    push ds
    push es
    push di
    push si
    push ax

    xor ax, ax
    mov es, ax
    mov di, 0x0500

    mov ax, 0xFFFF
    mov ds, ax
    mov si, 0x0510

    mov al, [es:di]                 ; save both original bytes
    push ax
    mov al, [ds:si]
    push ax

    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    cmp byte [es:di], 0xFF          ; did the high write bleed through?

    pop ax                          ; restore in reverse order
    mov [ds:si], al
    pop ax
    mov [es:di], al

    pop ax
    pop si
    pop di
    pop es
    pop ds
    popf

    je .disabled
    clc
    ret
.disabled:
    stc
    ret

; ---------------------------------------------------------------
; enable_a20 - try BIOS, then fast A20, then keyboard controller.
; Returns CF=0 on success, CF=1 if A20 could not be enabled.
; ---------------------------------------------------------------
enable_a20:
    call check_a20
    jnc .ok

    mov ax, 0x2401                  ; BIOS A20 enable
    int 0x15
    call check_a20
    jnc .ok

    in al, 0x92                     ; fast A20 (System Control Port A)
    test al, 2
    jnz .skip_fast
    or  al, 2
    and al, 0xFE                    ; never set bit 0 - that is fast reset
    out 0x92, al
.skip_fast:
    call check_a20
    jnc .ok

    call .kbc_wait_in               ; keyboard controller method
    mov al, 0xAD
    out 0x64, al
    call .kbc_wait_in
    mov al, 0xD0
    out 0x64, al
    call .kbc_wait_out
    in al, 0x60
    push ax
    call .kbc_wait_in
    mov al, 0xD1
    out 0x64, al
    call .kbc_wait_in
    pop ax
    or al, 2
    out 0x60, al
    call .kbc_wait_in
    mov al, 0xAE
    out 0x64, al
    call .kbc_wait_in

    call check_a20
    jnc .ok
    stc
    ret

.ok:
    clc
    ret

.kbc_wait_in:                       ; wait until input buffer is empty
    in al, 0x64
    test al, 2
    jnz .kbc_wait_in
    ret

.kbc_wait_out:                      ; wait until output buffer is full
    in al, 0x64
    test al, 1
    jz .kbc_wait_out
    ret
