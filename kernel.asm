bits 32

global kernel_start
extern kernel_main
extern keyboard_handle

kernel_start:

    call serial_init

    mov esi, s_banner
    call serial_print

    mov edi, 0xB8000
    mov ecx, 80 * 25
    mov ax, 0x0720

clear_screen:
    mov word [edi], ax
    add edi, 2
    loop clear_screen


    ; -----------------------------------------
    ; CPUID Vendor
    ; -----------------------------------------

    mov eax, 0
    cpuid

    mov [cpuid_max_leaf], eax
    mov [cpu_vendor], ebx
    mov [cpu_vendor + 4], edx
    mov [cpu_vendor + 8], ecx
    mov byte [cpu_vendor + 12], 0


    ; -----------------------------------------
    ; CPUID Basic Information
    ; -----------------------------------------

    mov eax, 1
    cpuid

    mov [cpu_raw], eax

    mov ebx, eax
    shr ebx, 8
    and ebx, 0x0F
    mov [cpu_family], ebx

    mov ebx, eax
    shr ebx, 4
    and ebx, 0x0F
    mov [cpu_model], ebx


    ; -----------------------------------------
    ; Display System Information
    ; -----------------------------------------

    mov edi, 0xB8000
    mov esi, kernel_message
    call print_string

    mov edi, 0xB80A0
    mov esi, info_arch
    call print_string

    mov edi, 0xB8140
    mov esi, info_mode
    call print_string

    mov edi, 0xB81E0
    mov esi, info_kernel
    call print_string

    mov edi, 0xB8280
    mov esi, info_cpu
    call print_string

    mov esi, cpu_vendor
    call print_string

    mov edi, 0xB8320
    mov esi, info_max_leaf
    call print_string

    mov eax, [cpuid_max_leaf]
    call print_hex

    mov edi, 0xB83C0
    mov esi, info_raw
    call print_string

    mov eax, [cpu_raw]
    call print_hex

    mov edi, 0xB8460
    mov esi, info_family
    call print_string

    mov eax, [cpu_family]
    call print_hex

    mov edi, 0xB8500
    mov esi, info_model
    call print_string

    mov eax, [cpu_model]
    call print_hex

    ; mirror CPU identification to the serial port
    mov esi, s_cpu
    call serial_print
    mov esi, cpu_vendor
    call serial_print
    call serial_newline

    mov esi, s_cpuid
    call serial_print
    mov eax, [cpu_raw]
    call serial_print_hex
    call serial_newline


        ; -----------------------------------------
    ; Initialize IDT gates
    ; -----------------------------------------

    ; Vector 0 -> isr0
    mov eax, isr0
    mov word [idt_start + 0], ax
    mov word [idt_start + 2], 0x08
    mov byte [idt_start + 4], 0
    mov byte [idt_start + 5], 0x8E
    shr eax, 16
    mov word [idt_start + 6], ax

    ; Vector 32 -> isr_timer
    mov eax, isr_timer
    mov word [idt_start + 32 * 8 + 0], ax
    mov word [idt_start + 32 * 8 + 2], 0x08
    mov byte [idt_start + 32 * 8 + 4], 0
    mov byte [idt_start + 32 * 8 + 5], 0x8E
    shr eax, 16
    mov word [idt_start + 32 * 8 + 6], ax

; Vector 33 -> keyboard IRQ1
mov eax, isr_keyboard
mov word [idt_start + 33 * 8 + 0], ax
mov word [idt_start + 33 * 8 + 2], 0x08
mov byte [idt_start + 33 * 8 + 4], 0
mov byte [idt_start + 33 * 8 + 5], 0x8E
shr eax, 16
mov word [idt_start + 33 * 8 + 6], ax

    ; -----------------------------------------
    ; Load IDT
    ; -----------------------------------------

    lidt [idt_descriptor]

    mov esi, s_idt
    call serial_print


    ; -----------------------------------------
    ; Initialize PIT
    ; -----------------------------------------

    mov al, 0x36
    out 0x43, al

    mov ax, 1193
    out 0x40, al
    mov al, ah
    out 0x40, al


    ; -----------------------------------------
    ; Remap PIC
    ; -----------------------------------------

    mov al, 0x11
    out 0x20, al
    out 0xA0, al

    mov al, 0x20
    out 0x21, al

    mov al, 0x28
    out 0xA1, al

    mov al, 0x04
    out 0x21, al

    mov al, 0x02
    out 0xA1, al

    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    mov al, 0xFC
    out 0x21, al

    mov esi, s_pic
    call serial_print


    ; -----------------------------------------
    ; Test Interrupt 0
    ; -----------------------------------------

    int 0


    ; -----------------------------------------
    ; Confirm IRET returned here
    ; -----------------------------------------

    mov edi, 0xB8640
    mov esi, info_mode
    call print_string

    mov esi, s_iret
    call serial_print


    ; -----------------------------------------
    ; Boot complete - CI asserts on this marker
    ; -----------------------------------------

    mov esi, s_bootok
    call serial_print

; -----------------------------------------
; Enter C kernel
; -----------------------------------------

call kernel_main

    ; -----------------------------------------
    ; Halt kernel
    ; -----------------------------------------

kernel_halt:
    sti
    hlt
    jmp kernel_halt


; -----------------------------------------
; Interrupt 0 Handler
; -----------------------------------------

isr0:
    cli

    mov edi, 0xB85A0
    mov esi, interrupt_message
    call print_string

    mov esi, s_int0
    call serial_print

    iret


; -----------------------------------------
; PIT Timer ISR
; -----------------------------------------

isr_timer:
    pushad
    inc dword [timer_ticks]

    mov edi, 0xB8780
    mov esi, timer_message
    call print_string

    mov edi, 0xB87E0
    mov eax, [timer_ticks]
    call print_hex

    mov al, 0x20
    out 0x20, al
    
    popad
    iret

; -----------------------------------------
; Keyboard IRQ1 Handler
; -----------------------------------------

isr_keyboard:
    pushad

    ; Read keyboard scancode
    in al, 0x60
    movzx eax, al

    ; Pass scancode to C
    push eax
    call keyboard_handle
    add esp, 4

    ; Send EOI to master PIC
    mov al, 0x20
    out 0x20, al

    popad
    iret

; -----------------------------------------
; Print String
; -----------------------------------------

print_string:
    lodsb

    cmp al, 0
    je .done

    mov ah, 0x07
    mov word [edi], ax
    add edi, 2

    jmp print_string

.done:
    ret


; -----------------------------------------
; Print 32-bit Hexadecimal
; -----------------------------------------

print_hex:
    mov edx, eax
    mov ecx, 8

.hex_loop:

    rol edx, 4

    mov ebx, edx
    and ebx, 0x0F

    cmp bl, 9
    jbe .number

    add bl, 'A' - 10
    jmp .write

.number:
    add bl, '0'

.write:
    mov ah, 0x07
    mov al, bl

    mov word [edi], ax
    add edi, 2

    loop .hex_loop

    ret


; -----------------------------------------
; Messages
; -----------------------------------------

kernel_message db 'InitraOS System Information', 0

info_arch db 'Architecture: x86 (32-bit)', 0

info_mode db 'Mode: Protected Mode', 0

info_kernel db 'Kernel: InitraOS', 0

info_cpu db 'CPU: ', 0

info_max_leaf db 'CPUID Max Leaf: 0x', 0

info_raw db 'CPUID EAX: 0x', 0

info_family db 'CPU Family: 0x', 0

info_model db 'CPU Model: 0x', 0

interrupt_message db 'INTERRUPT 0 HANDLED!', 0

timer_message db 'TIMER TICKS: ', 0


; -----------------------------------------
; Serial log messages
; -----------------------------------------

s_banner db 13, 10, '[InitraOS] kernel entry, serial online', 13, 10, 0
s_cpu    db '[InitraOS] CPU vendor: ', 0
s_cpuid  db '[InitraOS] CPUID EAX: 0x', 0
s_idt    db '[InitraOS] IDT loaded', 13, 10, 0
s_pic    db '[InitraOS] PIC remapped, PIT armed', 13, 10, 0
s_int0   db '[InitraOS] INT0 handler reached', 13, 10, 0
s_iret   db '[InitraOS] IRET returned to kernel', 13, 10, 0
s_bootok db '[InitraOS] BOOT_OK', 13, 10, 0


; -----------------------------------------
; Variables
; -----------------------------------------

cpu_vendor times 13 db 0

cpuid_max_leaf dd 0

cpu_raw dd 0

cpu_family dd 0

cpu_model dd 0

timer_ticks dd 0


; -----------------------------------------
; Includes
; -----------------------------------------

%include "serial.inc"
%include "idt.inc"