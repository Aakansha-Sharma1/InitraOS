; InitraOS - Stage 1 Bootloader
; Loads LOAD_SECTORS sectors starting at LBA 1 to 0000:8000, then jumps there.
; LOAD_SECTORS is supplied by the build system (-DLOAD_SECTORS=n) so the
; bootloader always loads exactly as much as stage2+kernel actually need.

bits 16
org 0x7C00

%ifndef LOAD_SECTORS
%define LOAD_SECTORS 10             ; fallback for a bare "nasm boot.asm"
%endif

SECTORS_PER_TRACK equ 18            ; 1.44MB floppy geometry
HEADS             equ 2
LOAD_OFF          equ 0x8000        ; stage2 lands here, kernel right after

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti                             ; BIOS disk services need IRQs enabled

    mov [boot_drive], dl

    mov si, loading_message
    call print_string

    mov word [dest_off], LOAD_OFF
    mov word [cur_lba], 1
    mov cx, LOAD_SECTORS

.load_loop:
    push cx
    call read_one_sector
    pop cx
    add word [dest_off], 512
    inc word [cur_lba]
    loop .load_loop

    mov dl, [boot_drive]
    jmp 0x0000:0x8000

; ---------------------------------------------------------------
; Read one sector (LBA in [cur_lba]) to 0000:[dest_off], 3 retries
; ---------------------------------------------------------------
read_one_sector:
    mov di, 3

.retry:
    mov ax, [cur_lba]
    xor dx, dx
    mov bx, SECTORS_PER_TRACK
    div bx                          ; ax = lba/SPT, dx = lba%SPT
    mov cl, dl
    inc cl                          ; CHS sector is 1-based

    xor dx, dx
    mov bx, HEADS
    div bx                          ; ax = cylinder, dx = head

    mov ch, al                      ; cylinder (low 8 bits)
    mov dh, dl                      ; head
    mov dl, [boot_drive]
    mov bx, [dest_off]
    mov ax, 0x0201                  ; AH=02 read, AL=1 sector
    int 0x13
    jnc .done

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

print_string:
    lodsb
    cmp al, 0
    je .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

loading_message db 'INITRA OS - Loading...', 13, 10, 0
error_message   db 'Disk read error!', 13, 10, 0

boot_drive db 0
dest_off   dw 0
cur_lba    dw 0

times 510-($-$$) db 0
dw 0xAA55
