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
%define KERNEL64_CODE_SELECTOR 0x30
%define KERNEL64_DATA_SELECTOR 0x38


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
global enable_long_mode
global syscall_entry

extern scheduler_tick
extern kernel_main
extern keyboard_handle
extern syscall_dispatcher
extern syscall_exit

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

    ; Vector 0x80 -> system call entry
    ;
    ; DPL 3 is required so Ring 3 code can execute
    ; INT 0x80 directly.

    mov eax, syscall_entry

    mov word [idt_start + 0x80 * 8 + 0], ax
    mov word [idt_start + 0x80 * 8 + 2], KERNEL_CODE_SELECTOR
    mov byte [idt_start + 0x80 * 8 + 4], 0
    mov byte [idt_start + 0x80 * 8 + 5], 0xEE

    shr eax, 16

    mov word [idt_start + 0x80 * 8 + 6], ax

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

    ; -----------------------------------------------------
    ; InitraOS native user program header.
    ;
    ; entry      = 0
    ; code_size  = user program code size
    ; data_size  = 0
    ; bss_size   = 0
    ; -----------------------------------------------------

    dd 0x49504F53
    dd 1
    dd 0
    dd user_mode_code_end - user_mode_entry
    dd 0
    dd 0

user_mode_entry:

    mov edi, 0xB8C80

    call .get_ip

.get_ip:
    pop esi

    add esi, user_mode_user_message - .get_ip


.user_print:

    lodsb

    test al, al

    jz .test_syscall

    mov ah, 0x07

    stosw

    jmp .user_print


.test_syscall:

    ; Mark that the loaded user program actually
    ; reached this point in Ring 3.
    ;
    ; USER_STACK_BASE is the existing user-writable
    ; stack page at 0x007FF000.
    mov dword [0x007FF000], 0x45584543

    ; Test GETPID.
    mov eax, 2
    int 0x80

    ; The first kernel task has PID 1.
    ; user_test is the next task, so its PID is 2.
    cmp eax, ebx
    jne .syscall_test_fail

.test_exit:

    ; Test SYSCALL_EXIT.
    ; syscall_entry detects syscall number 0
    ; and switches back to kernel_context.
    mov eax, 0

    int 0x80

.syscall_test_fail:

    ; GETPID returned an unexpected value.
    ; Do not exit the task, so the test cannot
    ; falsely report success.
    jmp .syscall_test_fail

.user_halt:

    jmp .user_halt


user_mode_user_message db \
    'USER MODE WORKED! TESTING SYSCALL...', 0

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
; System Call Entry
;
; User-space enters through:
;
;     INT 0x80
;
; At this stage the entry point only preserves the
; interrupted register state and returns to the caller.
;
; System call dispatch will be added separately.
; =========================================================

syscall_entry:

    pushad

    ; pushad layout:
    ;
    ; [esp + 28] = saved EAX = syscall number
    ; [esp + 24] = saved ECX = arg2
    ; [esp + 20] = saved EDX = arg3
    ; [esp + 16] = saved EBX = arg1
    ; [esp + 12] = saved original ESP
    ; [esp + 8]  = saved EBP
    ; [esp + 4]  = saved ESI = arg4
    ; [esp + 0]  = saved EDI = arg5

    ; Copy the complete syscall register state
    ; before pushing anything onto the stack.

    mov eax, [esp + 28]    ; syscall number
    mov ebx, [esp + 16]    ; arg1
    mov ecx, [esp + 24]    ; arg2
    mov edx, [esp + 20]    ; arg3
    mov esi, [esp + 4]     ; arg4
    mov edi, [esp + 0]     ; arg5

    ; Preserve syscall number across the C call.
    push eax

    ; syscall_dispatcher(
    ;     syscall_number,
    ;     arg1,
    ;     arg2,
    ;     arg3,
    ;     arg4,
    ;     arg5
    ; );
    ;
    ; cdecl arguments are pushed right-to-left.

    push edi               ; arg5
    push esi               ; arg4
    push edx               ; arg3
    push ecx               ; arg2
    push ebx               ; arg1
    push eax               ; syscall number

    call syscall_dispatcher

    ; Remove six dispatcher arguments.
    add esp, 24

    ; Recover original syscall number.
    pop edx

    ; SYSCALL_EXIT does not return to user mode.
    cmp edx, 0
    je syscall_exit_entry

    ; Store dispatcher result as saved EAX.
    mov [esp + 28], eax

    popad

    iret


syscall_exit_entry:

    call syscall_exit

    ; syscall_exit() switches away from the
    ; finished user task and does not return.
    jmp $

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
; Issue #80 - Enable long-mode CPU state
; =========================================================

enable_long_mode:

    ; Do not allow hardware interrupts during the transition.
    cli

    ; 1. Disable current 32-bit paging.
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax

    ; 2. Enable PAE.
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; 3. Load the physical address of the 64-bit PML4.
    ;    PML4 is built by paging64.inc at 0x00020000.
    mov eax, 0x00020000
    mov cr3, eax

    ; 4. Enable IA32_EFER.LME.
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Confirm prerequisites before enabling paging.
    mov esi, s_before_pg
    call serial_print
    call serial_newline

    ; 5. Re-enable paging.
    ;    EFER.LME=1 + CR4.PAE=1 + CR0.PG=1
    ;    puts the CPU into IA-32e mode.
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; #89: Initialize 64-bit IDT vector 0.
    mov eax, isr64_0

    mov word [idt64_start + 0], ax
    mov word [idt64_start + 2], KERNEL64_CODE_SELECTOR
    mov byte [idt64_start + 4], 0
    mov byte [idt64_start + 5], 0x8E

    shr eax, 16
    mov word [idt64_start + 6], ax

    xor eax, eax
    mov dword [idt64_start + 8], eax
    mov dword [idt64_start + 12], eax

    ; #90: Initialize 64-bit page-fault handler (vector 14).
    mov eax, isr64_page_fault

    mov word [idt64_start + 0xE0], ax
    mov word [idt64_start + 0xE2], KERNEL64_CODE_SELECTOR
    mov byte [idt64_start + 0xE4], 0
    mov byte [idt64_start + 0xE5], 0x8E

    shr eax, 16
    mov word [idt64_start + 0xE6], ax

    xor eax, eax
    mov dword [idt64_start + 0xE8], eax
    mov dword [idt64_start + 0xEC], eax

    ; #91: Initialize 64-bit PIT timer handler (IRQ0, vector 32).
    mov eax, isr64_timer

    mov word [idt64_start + 0x200], ax
    mov word [idt64_start + 0x202], KERNEL64_CODE_SELECTOR
    mov byte [idt64_start + 0x204], 0
    mov byte [idt64_start + 0x205], 0x8E

    shr eax, 16
    mov word [idt64_start + 0x206], ax

    xor eax, eax
    mov dword [idt64_start + 0x208], eax
    mov dword [idt64_start + 0x20C], eax

    ; #80 success marker.

    ; This runs while the current compatibility-mode code segment
    ; is still active.
    mov esi, s_long_mode_ok
    call serial_print
    call serial_newline

    ; #82:
    ; Load the 64-bit kernel code segment and enter the 64-bit
    ; kernel entry point.
    ;
    ; 0x30 = KERNEL64_CODE_SELECTOR
    ; kernel64_entry = 64-bit entry point
    jmp KERNEL64_CODE_SELECTOR:kernel64_entry

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

    ; 64-bit kernel code:
    ; base 0, limit 0, DPL 0
    ; L=1, D/B=0, present, executable, readable
    ; Selector: 0x30

    dq 0x00209A0000000000

; 64-bit kernel data:
; base 0, limit 0, DPL 0
; present, writable
; Selector: 0x38

dq 0x0020920000000000

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

s_long_mode_ok \
    db '[InitraOS] LONG_MODE_ENABLED_OK', 13, 10, 0

s_before_pg \
    db '[InitraOS] BEFORE_PG', 13, 10, 0

s_kernel64_entry \
    db '[InitraOS] KERNEL64_ENTRY_OK', 13, 10, 0

s_kernel64_int0 \
    db '[InitraOS] KERNEL64_INT0_OK', 13, 10, 0

s_kernel64_page_fault \
    db '[InitraOS] KERNEL64_PAGE_FAULT_OK', 13, 10, 0

s_kernel64_fault_address \
    db '[InitraOS] KERNEL64_FAULT_ADDRESS: 0x', 0

s_kernel64_timer \
    db '[InitraOS] KERNEL64_TIMER_IRQ_OK', 13, 10, 0

; =========================================================
; Issue #82 - 64-bit kernel entry point
; =========================================================
;
; This is the first instruction entry executed after the
; CPU performs the far jump into the 64-bit code segment.
;
; The actual far jump is performed by the transition code.
; This entry only establishes the 64-bit execution point.
; =========================================================
section .text64 progbits alloc exec
bits 64

global kernel64_entry

kernel64_entry:

    ; #85: establish the 64-bit kernel stack.
    mov rsp, kernel64_stack_top

    ; #85: establish the 64-bit kernel data segment.
    mov ax, KERNEL64_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; #89: load the 64-bit IDT.
    lidt [idt64_descriptor]

    ; #85: start the 64-bit kernel with clean general-purpose registers.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp

    ; #82: confirm that execution reached the 64-bit kernel entry.

    mov esi, s_kernel64_entry

.kernel64_serial_loop:

    lodsb
    test al, al
    jz .kernel64_c_entry

    call serial64_putc

    jmp .kernel64_serial_loop

.kernel64_c_entry:

    ; First 64-bit C kernel entry test.
    ;
    ; kernel64_stack_top is 16-byte aligned.
    ; System V x86-64 requires RSP to be 16-byte aligned
    ; before a CALL instruction.
    sub rsp, 8
    call kernel64_c_main
    add rsp, 8

    ; Continue with the existing 64-bit interrupt tests.

.kernel64_int0:

    ; #89: Verify 64-bit interrupt delivery and return.
    int 0

    ; #91: Enable hardware interrupts after 64-bit entry validation.
    sti

    ; Wait for one real timer IRQ before triggering
    ; the page fault. This makes the timer validation
    ; deterministic instead of depending on QEMU timing.
    mov ebx, dword [timer_ticks]

.kernel64_wait_timer:
    cmp dword [timer_ticks], ebx
    je .kernel64_wait_timer

    ; #90: Trigger a real page fault outside the identity-mapped range.
    mov rdi, 0x01000000
    mov byte [rdi], 0x00

.kernel64_vga:

    mov rdi, 0xB8000
    mov rax, 0x1F341F49
    mov [rdi], rax

.kernel64_halt:

    hlt
    jmp .kernel64_halt

; =========================================================
; #89 - 64-bit Interrupt 0 Handler
; =========================================================

isr64_0:

    mov rsi, s_kernel64_int0

.isr64_0_loop:

    lodsb
    test al, al
    jz .isr64_0_done

    call serial64_putc

    jmp .isr64_0_loop

.isr64_0_done:

    iretq

; =========================================================
; #91 - 64-bit PIT Timer IRQ0 Handler
; =========================================================

isr64_timer:

    inc dword [timer_ticks]

    mov rsi, s_kernel64_timer

.isr64_timer_loop:

    lodsb
    test al, al
    jz .isr64_timer_done

    call serial64_putc

    jmp .isr64_timer_loop

.isr64_timer_done:

    ; Send EOI to the master PIC.
    mov al, 0x20
    out 0x20, al

    iretq

; =========================================================
; #90 - 64-bit Page Fault Handler
; =========================================================

isr64_page_fault:

    mov rsi, s_kernel64_page_fault

.isr64_page_fault_loop:

    lodsb
    test al, al
    jz .isr64_page_fault_done

    call serial64_putc

    jmp .isr64_page_fault_loop

.isr64_page_fault_done:

    mov rax, cr2

    mov rsi, s_kernel64_fault_address

.isr64_fault_address_loop:

    lodsb
    test al, al
    jz .isr64_fault_address_done

    call serial64_putc

    jmp .isr64_fault_address_loop

.isr64_fault_address_done:

    ; Print the page-fault address from CR2.
    call serial64_print_hex
    call serial64_newline

.isr64_page_fault_halt:

    hlt
    jmp .isr64_page_fault_halt


; =========================================================
; #87 - 64-bit serial output
; =========================================================
;
; serial64_putc
; AL = character
;
; Sends one character to COM1 while preserving RAX/RDX.
; =========================================================
serial64_putc:

    push rax
    push rdx

.serial64_wait:

    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .serial64_wait

    pop rdx
    pop rax

    mov dx, COM1
    out dx, al

    ret

; =========================================================
; #90 - 64-bit serial hexadecimal output
; =========================================================
;
; serial64_print_hex
; RAX = 64-bit value
; Prints 16 hexadecimal digits to COM1.
; =========================================================

; =========================================================
; 64-bit serial newline
; =========================================================

serial64_newline:

    push rax

    mov al, 13
    call serial64_putc

    mov al, 10
    call serial64_putc

    pop rax

    ret

serial64_print_hex:

    push rax
    push rbx
    push rcx
    push rdx

    mov rbx, rax
    mov rcx, 16

.serial64_hex_loop:

    rol rbx, 4

    mov rax, rbx
    and al, 0x0F

    cmp al, 9
    jbe .serial64_hex_number

    add al, 'A' - 10
    jmp .serial64_hex_write

.serial64_hex_number:

    add al, '0'

.serial64_hex_write:

    call serial64_putc

    loop .serial64_hex_loop

    pop rdx
    pop rcx
    pop rbx
    pop rax

    ret

; =========================================================
; 64-bit C kernel entry
; =========================================================
;
; The machine code was compiled separately with GCC -m64.
; It is embedded here so the existing mixed ELF32/ELF64
; kernel link remains unchanged for this milestone.
;
kernel64_c_main:
    incbin "build/kernel64.c.bin"

bits 32

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
; 64-bit IDT
; 256 entries x 16 bytes.
; =========================================================

section .bss

align 16

idt64_start:

    resb 256 * 16

idt64_end:


section .data

align 8

idt64_descriptor:

    dw idt64_end - idt64_start - 1
    dq idt64_start


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
alignb 16

kernel64_stack:
    resb 4096

kernel64_stack_top:

; =========================================================
; Includes
; =========================================================

section .text

%include "serial.inc"