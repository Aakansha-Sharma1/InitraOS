bits 16
org 0x8000

start:
    mov bx, 0

    mov ax, 0x0003
    int 0x10

    mov si, message
    call print_string

    mov si, prompt
    call print_string

wait_key:
    mov ah, 0x00
    int 0x16

    cmp al, 13
    je new_line

    cmp al, 8
    je backspace

    cmp bx, 63
    jae wait_key

    mov [buffer + bx], al
    inc bx

    mov ah, 0x0E
    int 0x10

    jmp wait_key

backspace:
    cmp bx, 0
    je wait_key

    dec bx

    mov al, 8
    mov ah, 0x0E
    int 0x10

    mov al, ' '
    int 0x10

    mov al, 8
    int 0x10

    jmp wait_key

new_line:
    mov byte [buffer + bx], 0

    mov al, 13
    mov ah, 0x0E
    int 0x10

    mov al, 10
    int 0x10

    mov si, buffer

    ; Check for "help"
    cmp byte [si], 'h'
    jne check_clear
    cmp byte [si+1], 'e'
    jne check_clear
    cmp byte [si+2], 'l'
    jne check_clear
    cmp byte [si+3], 'p'
    jne check_clear
    cmp byte [si+4], 0
    jne check_clear

show_help:
    mov si, help_message
    call print_string
    jmp command_done

check_clear:
    mov si, buffer

    cmp byte [si], 'c'
    jne check_version
    cmp byte [si+1], 'l'
    jne check_version
    cmp byte [si+2], 'e'
    jne check_version
    cmp byte [si+3], 'a'
    jne check_version
    cmp byte [si+4], 'r'
    jne check_version
    cmp byte [si+5], 0
    jne check_version

show_clear:
    mov ax, 0x0003
    int 0x10
    jmp command_done

check_version:
    mov si, buffer

    cmp byte [si], 'v'
    jne check_echo
    cmp byte [si+1], 'e'
    jne check_echo
    cmp byte [si+2], 'r'
    jne check_echo
    cmp byte [si+3], 's'
    jne check_echo
    cmp byte [si+4], 'i'
    jne check_echo
    cmp byte [si+5], 'o'
    jne check_echo
    cmp byte [si+6], 'n'
    jne check_echo
    cmp byte [si+7], 0
    jne check_echo

show_version:
    mov si, version_message
    call print_string
    jmp command_done

check_echo:
    mov si, buffer

    cmp byte [si], 'e'
    jne check_sysinfo
    cmp byte [si+1], 'c'
    jne check_sysinfo
    cmp byte [si+2], 'h'
    jne check_sysinfo
    cmp byte [si+3], 'o'
    jne check_sysinfo
    cmp byte [si+4], ' '
    jne check_sysinfo

show_echo:
    add si, 5
    call print_string
    mov si, newline_message
    call print_string
    jmp command_done

check_sysinfo:
    mov si, buffer

    cmp byte [si], 's'
    jne check_pmode
    cmp byte [si+1], 'y'
    jne check_pmode
    cmp byte [si+2], 's'
    jne check_pmode
    cmp byte [si+3], 'i'
    jne check_pmode
    cmp byte [si+4], 'n'
    jne check_pmode
    cmp byte [si+5], 'f'
    jne check_pmode
    cmp byte [si+6], 'o'
    jne check_pmode
    cmp byte [si+7], 0
    jne check_pmode

show_sysinfo:
    mov si, sysinfo_message
    call print_string
    jmp command_done

check_pmode:
    mov si, buffer

    cmp byte [si], 'p'
    jne unknown_command
    cmp byte [si+1], 'm'
    jne unknown_command
    cmp byte [si+2], 'o'
    jne unknown_command
    cmp byte [si+3], 'd'
    jne unknown_command
    cmp byte [si+4], 'e'
    jne unknown_command
    cmp byte [si+5], 0
    jne unknown_command

    jmp enter_protected_mode

unknown_command:
    mov si, unknown_message
    call print_string

command_done:
    mov si, prompt
    call print_string

    mov bx, 0
    jmp wait_key

print_string:
    lodsb
    cmp al, 0
    je .done

    mov ah, 0x0E
    int 0x10
    jmp print_string

.done:
    ret


; -------------------------------------------------
; Enter 32-bit Protected Mode
; -------------------------------------------------

enter_protected_mode:
    cli

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode_start


bits 32

protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov esp, 0x90000

    ; Clear VGA text memory
    mov edi, 0xB8000
    mov ecx, 80 * 25
    mov ax, 0x0720

clear_screen:
    mov word [edi], ax
    add edi, 2
    loop clear_screen

    ; Display Protected Mode message
    mov edi, 0xB8000
    mov esi, pmode_message

print_pmode:
    lodsb
    cmp al, 0
    je pmode_halt

    mov ah, 0x07
    mov word [edi], ax
    add edi, 2

    jmp print_pmode

pmode_halt:
    jmp 0x08:0x8800


bits 16

message db 'INITRA OS - Stage 2 loaded!', 13, 10, 0
prompt db 'INITRA> ', 0

help_message db 'Commands: help, clear, version, echo, sysinfo, pmode', 13, 10, 0
version_message db 'InitraOS v0.1', 13, 10, 0
unknown_message db 'Unknown command', 13, 10, 0
newline_message db 13, 10, 0

sysinfo_message db 'InitraOS v0.1', 13, 10
                db 'Architecture: x86', 13, 10
                db 'Mode: 16-bit', 13, 10
                db 'Shell: Initra Shell', 13, 10, 0

pmode_message db 'INITRA OS - 32-bit Protected Mode: OK', 0

buffer times 64 db 0

%include "gdt.inc"

times 2048-($-$$) db 0