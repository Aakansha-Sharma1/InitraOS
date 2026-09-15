bits 32

; InitraOS - 32-bit kernel low-level support

%define KERNEL_ORG 0x8800

; CPU privilege levels
%define KERNEL_RING 0
%define USER_RING   3

; GDT selectors owned by the kernel after kernel_start.
%define KERNEL_CODE_SELECTOR 0x08
%define KERNEL_DATA_SELECTOR 0x10
%define USER_CODE_SELECTOR   0x1B
%define USER_DATA_SELECTOR   0x23
%define TSS_SELECTOR         0x28


; =========================================================
; Kernel entry
; =========================================================

section .text

global kernel_start
global task_switch
global c_print_string
global c_serial_print
global c_serial_print_hex
global enter_user_mode
global user_mode_entry
global user_mode_code_start
global user_mode_code_end

extern scheduler_tick
extern kernel_main
extern keyboard_handle


kernel_start:

    ; ---------------------------------------------------------
    ; Install the kernel-owned GDT and TSS first.
    ; Stage 2's GDT is still valid, but the kernel needs a TSS
    ; descriptor so Ring 3 faults can safely enter Ring 0.
    ; ---------------------------------------------------------

    mov eax, tss_start

    mov [kernel_gdt_tss + 2], ax

    shr eax, 16

    mov [kernel_gdt_tss + 4], al
    mov [kernel_gdt_tss + 7], ah

    mov word [kernel_gdt_tss + 0], \
        tss_end - tss_start - 1

    mov byte [kernel_gdt_tss + 5], 0x89
    mov byte [kernel_gdt_tss + 6], 0x00

    mov eax, tss_kernel_stack_top

    mov [tss_start + 4], eax
    mov word [tss_start + 8], KERNEL_DATA_SELECTOR

    mov word [tss_start + 0x66], \
        tss_end - tss_start

    lgdt [kernel_gdt_descriptor]

    mov ax, KERNEL_DATA_SELECTOR

    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Reload CS from the new GDT.
    jmp KERNEL_CODE_SELECTOR:.kernel_gdt_ready


.kernel_gdt_ready:

    mov ax, TSS_SELECTOR
    ltr ax

    call serial_init

    mov esi, s_banner
    call serial_print


    ; ---------------------------------------------------------
    ; Clear VGA screen
    ; ---------------------------------------------------------

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


    ; -----------------------------------------
    ; Serial CPU information
    ; -----------------------------------------

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
    mov word [idt_start + 2], KERNEL_CODE_SELECTOR
    mov byte [idt_start + 4], 0
    mov byte [idt_start + 5], 0x8E

    shr eax, 16

    mov word [idt_start + 6], ax


    ; Vector 14 -> page fault

    mov eax, isr_page_fault

    mov word [idt_start + 14 * 8 + 0], ax
    mov word [idt_start + 14 * 8 + 2], KERNEL_CODE_SELECTOR
    mov byte [idt_start + 14 * 8 + 4], 0
    mov byte [idt_start + 14 * 8 + 5], 0x8E

    shr eax, 16

    mov word [idt_start + 14 * 8 + 6], ax


    ; Vector 32 -> Timer

    mov eax, isr_timer

    mov word [idt_start + 32 * 8 + 0], ax
    mov word [idt_start + 32 * 8 + 2], KERNEL_CODE_SELECTOR
    mov byte [idt_start + 32 * 8 + 4], 0
    mov byte [idt_start + 32 * 8 + 5], 0x8E

    shr eax, 16

    mov word [idt_start + 32 * 8 + 6], ax


    ; Vector 33 -> Keyboard IRQ1

    mov eax, isr_keyboard

    mov word [idt_start + 33 * 8 + 0], ax
    mov word [idt_start + 33 * 8 + 2], KERNEL_CODE_SELECTOR
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

    ; Enable IRQ0 and IRQ1 on master PIC.
    ; Keep slave IRQs masked.

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
    ; Boot complete
    ; -----------------------------------------

    mov esi, s_bootok
    call serial_print


    ; -----------------------------------------
    ; Verify E820 memory map detection
    ; -----------------------------------------

    mov esi, s_e820
    call serial_print

    mov eax, [0x5000]
    call serial_print_hex

    call serial_newline


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


; =========================================================
; enter_user_mode
;
; Switch from Ring 0 (kernel) to Ring 3 (user mode).
;
; C calling convention:
;
;   [esp + 4] = user entry address
;   [esp + 8] = user stack top
; =========================================================

enter_user_mode:

    mov eax, [esp + 4]
    mov edx, [esp + 8]

    cli

    mov bx, USER_DATA_SELECTOR

    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx

    ; Ring 3 IRET frame:
    ;
    ; SS
    ; ESP
    ; EFLAGS
    ; CS
    ; EIP

    push dword USER_DATA_SELECTOR
    push edx
    push dword 0x202
    push dword USER_CODE_SELECTOR
    push eax

    iret


; =========================================================
; Position-independent user-mode test image.
;
; The C kernel copies these bytes to 0x00100000.
;
; The code then writes to VGA and attempts to read
; kernel memory at 0x00008800.
; =========================================================

user_mode_code_start:

user_mode_entry:

    mov edi, 0xB8C80

    call .get_ip

.get_ip:

    pop esi

    add esi, user_mode_user_message - .get_ip


.user_print:

    lodsb

    test al, al
    jz .test_kernel_access

    mov ah, 0x07
    stosw

    jmp .user_print


.test_kernel_access:

    ; 0x8800 is inside the kernel image
    ; and is supervisor-only.

    mov eax, [0x00008800]


.user_halt:

    jmp .user_halt


user_mode_user_message db \
    'USER MODE WORKED! TESTING PROTECTION...', 0

user_mode_code_end:


; =========================================================
; task_switch
;
; C signature:
;
;     void task_switch(task_context_t *old_context,
;                      task_context_t *new_context);
;
; Context layout:
;
;   +0  eax
;   +4  ebx
;   +8  ecx
;   +12 edx
;   +16 esi
;   +20 edi
;   +24 ebp
;   +28 esp
;   +32 eip
;   +36 eflags
;   +40 privilege
; =========================================================

task_switch:

    pushf
    pushad

    ; Stack after pushf + pushad:
    ;
    ; [esp + 0]   EDI
    ; [esp + 4]   ESI
    ; [esp + 8]   EBP
    ; [esp + 12]  original ESP
    ; [esp + 16]  EBX
    ; [esp + 20]  EDX
    ; [esp + 24]  ECX
    ; [esp + 28]  EAX
    ; [esp + 32]  EFLAGS
    ; [esp + 36]  return EIP
    ; [esp + 40]  old_context
    ; [esp + 44]  new_context

    mov edx, [esp + 40]
    mov eax, [esp + 44]

    cli


    ; -----------------------------------------
    ; Save current context
    ; -----------------------------------------

    mov ecx, [esp + 28]
    mov [edx + 0], ecx

    mov ecx, [esp + 16]
    mov [edx + 4], ecx

    mov ecx, [esp + 24]
    mov [edx + 8], ecx

    mov ecx, [esp + 20]
    mov [edx + 12], ecx

    mov ecx, [esp + 4]
    mov [edx + 16], ecx

    mov ecx, [esp + 0]
    mov [edx + 20], ecx

    mov ecx, [esp + 8]
    mov [edx + 24], ecx

    ; Original ESP before pushf.
    mov ecx, [esp + 12]
    add ecx, 4

    mov [edx + 28], ecx

    mov ecx, [esp + 36]
    mov [edx + 32], ecx

    mov ecx, [esp + 32]
    mov [edx + 36], ecx


    ; -----------------------------------------
    ; Switch to new context
    ; -----------------------------------------

    mov ebp, eax

    mov esp, [ebp + 28]

    cmp dword [ebp + 40], USER_RING
    je task_switch_user


    ; -----------------------------------------
    ; Build Ring 0 IRET frame
    ; -----------------------------------------

    mov ax, KERNEL_DATA_SELECTOR

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword [ebp + 36]
    push dword KERNEL_CODE_SELECTOR
    push dword [ebp + 32]

    jmp task_switch_restore


task_switch_user:

    ; User data segments are valid at CPL 3.

    mov ax, USER_DATA_SELECTOR

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax


    ; -----------------------------------------
    ; Build Ring 3 IRET frame
    ; -----------------------------------------

    push dword USER_DATA_SELECTOR
    push dword [ebp + 28]
    push dword [ebp + 36]
    push dword USER_CODE_SELECTOR
    push dword [ebp + 32]


task_switch_restore:

    ; -----------------------------------------
    ; Restore general registers
    ; -----------------------------------------

    mov eax, [ebp + 0]
    mov ebx, [ebp + 4]
    mov ecx, [ebp + 8]
    mov edx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]

    mov ebp, [ebp + 24]

    iret


; =========================================================
; C VGA string wrapper
; =========================================================

c_print_string:

    push edi
    push esi

    mov esi, [esp + 12]

    ; Row 20.
    mov edi, 0xB8FA0

    call print_string

    pop esi
    pop edi

    ret


; =========================================================
; C serial string wrapper
;
; C signature:
;
;     void c_serial_print(const char *message);
; =========================================================

c_serial_print:

    push esi

    mov esi, [esp + 8]

    call serial_print

    pop esi

    ret


; =========================================================
; C serial hexadecimal wrapper
;
; C signature:
;
;     void c_serial_print_hex(unsigned int value);
; =========================================================

c_serial_print_hex:

    push eax

    mov eax, [esp + 8]

    call serial_print_hex

    pop eax

    ret


; =========================================================
; Interrupt 0 Handler
; =========================================================

isr0:

    cli

    mov edi, 0xB85A0
    mov esi, interrupt_message
    call print_string

    mov esi, s_int0
    call serial_print

    iret


; =========================================================
; Page Fault Handler
; =========================================================

isr_page_fault:

    cli

    pushad

    ; CPU pushes an error code before entering this handler.
    ; After pushad, [esp + 32] is that error code.

    mov eax, cr2
    mov [page_fault_address], eax

    mov edi, 0xB8D20
    mov esi, page_fault_message
    call print_string

    mov edi, 0xB8DA0
    mov esi, page_fault_address_label
    call print_string

    mov eax, [page_fault_address]
    call print_hex

    mov edi, 0xB8E40
    mov esi, page_fault_error_label
    call print_string

    mov eax, [esp + 32]
    call print_hex

    mov esi, s_page_fault
    call serial_print

    mov esi, s_fault_address
    call serial_print

    mov eax, [page_fault_address]
    call serial_print_hex

    call serial_newline

    mov esi, s_isolation_ok
    call serial_print

    mov edi, 0xB8EE0
    mov esi, memory_protection_message
    call print_string


.page_fault_halt:

    hlt
    jmp .page_fault_halt


; =========================================================
; PIT Timer ISR
; =========================================================

isr_timer:

    pushad

    inc dword [timer_ticks]

    mov edi, 0xB8780
    mov esi, timer_message
    call print_string

    mov edi, 0xB87E0

    mov eax, [timer_ticks]
    call print_hex

    ; Give scheduler a chance on every timer tick.
    call scheduler_tick

    ; End of timer interrupt.

    mov al, 0x20
    out 0x20, al

    popad

    iret


; =========================================================
; Keyboard IRQ1 Handler
; =========================================================

isr_keyboard:

    pushad

    ; Read keyboard scancode.

    in al, 0x60
    movzx eax, al

    ; Pass scancode to C.

    push eax
    call keyboard_handle
    add esp, 4

    ; Send EOI to master PIC.

    mov al, 0x20
    out 0x20, al

    popad

    iret


; =========================================================
; Print String
; =========================================================

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


; =========================================================
; Print 32-bit Hexadecimal
; =========================================================

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


; =========================================================
; Kernel-owned GDT
; =========================================================

section .data

align 8

kernel_gdt_start:

    ; Null descriptor
    dq 0x0000000000000000


    ; Kernel code:
    ; base 0, limit 4 GiB, DPL 0

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00


    ; Kernel data:
    ; base 0, limit 4 GiB, DPL 0

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00


    ; User code:
    ; base 0, limit 4 GiB, DPL 3

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0xFA
    db 0xCF
    db 0x00


    ; User data:
    ; base 0, limit 4 GiB, DPL 3

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0xF2
    db 0xCF
    db 0x00


kernel_gdt_tss:

    ; Patched at runtime with the TSS base/limit.
    dq 0


kernel_gdt_end:


kernel_gdt_descriptor:

    dw kernel_gdt_end - kernel_gdt_start - 1
    dd kernel_gdt_start


; =========================================================
; Kernel strings and variables
; =========================================================

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

page_fault_message \
    db 'MEMORY PROTECTION: USER KERNEL ACCESS BLOCKED', 0

page_fault_address_label \
    db 'FAULT ADDRESS: 0x', 0

page_fault_error_label \
    db 'ERROR CODE: 0x', 0

memory_protection_message \
    db 'PROCESS ISOLATION VALIDATED!', 0


; Serial boot markers

s_banner \
    db 13, 10, '[InitraOS] kernel entry, serial online', 13, 10, 0

s_cpu \
    db '[InitraOS] CPU vendor: ', 0

s_cpuid \
    db '[InitraOS] CPUID EAX: 0x', 0

s_idt \
    db '[InitraOS] IDT loaded', 13, 10, 0

s_pic \
    db '[InitraOS] PIC remapped, PIT armed', 13, 10, 0

s_int0 \
    db '[InitraOS] INT0 handler reached', 13, 10, 0

s_iret \
    db '[InitraOS] IRET returned to kernel', 13, 10, 0

s_bootok \
    db '[InitraOS] BOOT_OK', 13, 10, 0


; E820 validation marker

s_e820 \
    db '[InitraOS] E820 entries: ', 0


; Page fault / isolation markers

s_page_fault \
    db '[InitraOS] Page fault handled at 0x', 0

s_fault_address \
    db '[InitraOS] fault address follows: ', 0

s_isolation_ok \
    db '[InitraOS] PROCESS_ISOLATION_OK', 13, 10, 0


section .data

global cpu_vendor
global page_fault_address

cpu_vendor times 13 db 0

cpuid_max_leaf dd 0

cpu_raw dd 0

cpu_family dd 0

cpu_model dd 0

timer_ticks dd 0

page_fault_address dd 0


; =========================================================
; IDT
; 256 entries x 8 bytes.
; =========================================================

section .bss

align 8

idt_start:

    resb 256 * 8

idt_end:


section .data

idt_descriptor:

    dw idt_end - idt_start - 1
    dd idt_start


; =========================================================
; TSS and dedicated Ring 0 stack
; =========================================================

section .bss

align 16

tss_start:

    resb 104

tss_end:


alignb 16

tss_kernel_stack:

    resb 4096

tss_kernel_stack_top:


; =========================================================
; Includes
; =========================================================

section .text

%include "serial.inc"