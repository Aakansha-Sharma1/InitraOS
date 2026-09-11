bits 16
org 0x7C00

start:
    cli
    
    mov [boot_drive], dl

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, loading_message
    call print_string

    ; Load Stage 2 from disk
    mov ah, 0x02        ; BIOS read sectors
    mov al, 10           ; Read 10 sectors
    mov ch, 0           ; Cylinder 0
    mov cl, 2           ; Sector 2
    mov dh, 0           ; Head 0
    mov dl, [boot_drive]

    mov bx, 0x8000      ; Load Stage 2 at 0000:8000

    int 0x13
    jc disk_error

    jmp 0x0000:0x8000

disk_error:
    mov si, error_message
    call print_string

hang:
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

loading_message db 'INITRA OS - Loading Stage 2...', 13, 10, 0
error_message   db 'Disk read error!', 13, 10, 0

boot_drive db 0
times 510-($-$$) db 0
dw 0xAA55