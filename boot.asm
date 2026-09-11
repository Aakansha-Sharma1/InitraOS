; InitraOS - Stage 1 Bootloader
;
; Loads LOAD_SECTORS sectors starting at LBA 1 to 0000:8000, then jumps there.
; LOAD_SECTORS is supplied by the build system (-DLOAD_SECTORS=n) so the
; bootloader always loads exactly as much as stage2+kernel actually need.
;
; Disk access:
;   Probes for INT 13h Extensions (EDD) with AH=41h. If present, reads with
;   AH=42h (LBA, no geometry arithmetic). If absent, queries the drive
;   geometry with AH=08h and falls back to CHS reads with AH=02h.
;
; Reports which path it took on both VGA and COM1 as "S1:LBA" or "S1:CHS",
; so CI can assert that the extended path is actually being exercised.

bits 16
org 0x7C00

%ifndef LOAD_SECTORS
%define LOAD_SECTORS 10             ; fallback for a bare "nasm boot.asm"
%endif

LOAD_OFF equ 0x8000                 ; stage2 lands here, kernel right after
COM1     equ 0x3F8

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti                             ; BIOS disk services need IRQs enabled

    mov [boot_drive], dl

    call serial_init

    mov si, loading_message
    call print_string

    ; ---- probe for INT 13h extensions (EDD) ----------------------
    ; AH=41h BX=55AAh. Success: CF clear, BX=AA55h, CX bit 0 set.
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_edd
    cmp bx, 0xAA55
    jne .no_edd
    test cl, 1                      ; bit 0 = extended read/write supported
    jz .no_edd

    mov byte [use_lba], 1
    mov si, mode_lba
    jmp .mode_done

.no_edd:
    call get_geometry
    mov si, mode_chs

.mode_done:
    call print_string

    ; Destination is tracked as a SEGMENT, not a 16-bit offset. A flat
    ; offset would wrap at 64K (sector 64) and start overwriting the
    ; interrupt vector table. Advancing the segment by 32 per sector
    ; (512/16) keeps the load linear across the whole conventional area.
    mov word [dest_seg], LOAD_OFF >> 4
    mov word [cur_lba], 1
    mov cx, LOAD_SECTORS

.load_loop:
    push cx
    call read_one_sector
    pop cx
    add word [dest_seg], 32
    inc word [cur_lba]
    loop .load_loop

    xor ax, ax                      ; hand stage2 a clean ES
    mov es, ax
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

; ---------------------------------------------------------------
; Ask the BIOS for the drive geometry (CHS fallback path only).
; AH=08h returns sectors/track in CL bits 0-5 and max head in DH.
; ES:DI is zeroed first to dodge buggy BIOSes. Keeps the floppy
; defaults if the call fails.
; ---------------------------------------------------------------
get_geometry:
    push es
    xor di, di
    mov es, di
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    pop es
    jc .keep_defaults

    and cx, 0x003F                  ; CL bits 0-5 = sectors per track
    jz .keep_defaults               ; a zero here would divide by zero
    mov [spt], cx

    mov al, dh
    xor ah, ah
    inc ax                          ; heads = max head + 1
    mov [heads], ax

.keep_defaults:
    ret

; ---------------------------------------------------------------
; Read one sector (LBA in [cur_lba]) to [dest_seg]:0000, 3 retries.
; Dispatches to the extended or the CHS path.
; ---------------------------------------------------------------
read_one_sector:
    mov di, 3

.retry:
    cmp byte [use_lba], 0
    je .chs

    ; ---- extended read (AH=42h) with a Disk Address Packet -------
    mov ax, [dest_seg]
    mov [dap_seg], ax
    mov ax, [cur_lba]
    mov [dap_lba], ax
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .done
    jmp .failed

.chs:
    ; ---- classic read (AH=02h), LBA -> CHS ----------------------
    mov ax, [cur_lba]
    xor dx, dx
    div word [spt]                  ; ax = lba/spt, dx = lba%spt
    mov cl, dl
    inc cl                          ; CHS sector is 1-based

    xor dx, dx
    div word [heads]                ; ax = cylinder, dx = head

    mov ch, al                      ; cylinder (low 8 bits)
    mov dh, dl                      ; head
    mov dl, [boot_drive]
    mov bx, [dest_seg]
    mov es, bx                      ; read to dest_seg:0000
    xor bx, bx
    mov ax, 0x0201                  ; AH=02 read, AL=1 sector
    int 0x13
    jnc .done

.failed:
    xor ah, ah                      ; reset disk controller
    mov dl, [boot_drive]
    int 0x13

    dec di
    jnz .retry
    jmp disk_error

.done:
    ret

disk_error:
    mov si, error_message
    call print_string

hang:
    hlt
    jmp hang

; ---------------------------------------------------------------
; COM1 at 38400 8N1, matching the kernel's serial settings.
; ---------------------------------------------------------------
serial_init:
    mov dx, COM1 + 1
    xor al, al
    out dx, al                      ; interrupts off
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al                      ; DLAB on
    mov dx, COM1
    mov al, 3
    out dx, al                      ; divisor lo = 3 -> 38400
    mov dx, COM1 + 1
    xor al, al
    out dx, al                      ; divisor hi
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al                      ; 8N1, DLAB off
    ret

serial_putc:                        ; al = char
    push ax
    push dx
    mov ah, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20                   ; transmitter holding register empty
    jz .wait
    mov dx, COM1
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; Writes to VGA via BIOS teletype and to COM1, so the same message is
; visible in a window and capturable by CI.
print_string:
.loop:
    lodsb
    test al, al
    jz .done
    push ax
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop ax
    call serial_putc
    jmp .loop
.done:
    ret

loading_message db 'InitraOS loading', 13, 10, 0
mode_lba        db 'S1:LBA', 13, 10, 0
mode_chs        db 'S1:CHS', 13, 10, 0
error_message   db 'S1:DISK ERROR', 13, 10, 0

; Disk Address Packet for AH=42h. Count and offset are constant; only
; the buffer segment and the LBA change per sector.
dap:
    db 0x10                         ; packet size
    db 0
    dw 1                            ; sectors to transfer
    dw 0                            ; buffer offset (always 0)
dap_seg:
    dw 0                            ; buffer segment
dap_lba:
    dd 0                            ; LBA bits 0-31
    dd 0                            ; LBA bits 32-63

boot_drive db 0
use_lba    db 0
spt        dw 18                    ; floppy defaults, replaced by AH=08h
heads      dw 2
dest_seg   dw 0
cur_lba    dw 0

times 510-($-$$) db 0
dw 0xAA55
