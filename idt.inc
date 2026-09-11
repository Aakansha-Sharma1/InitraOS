; InitraOS - Interrupt Descriptor Table
;
; An IDT gate splits the 32-bit handler address across two 16-bit fields.
; The previous version hardcoded the high word to zero, which only worked
; because the kernel happened to sit below 0x10000. This macro does it
; properly so handlers stay correct wherever the kernel is linked.

; In 'bin' output a label is section-relative, not a scalar, so NASM refuses
; to apply & and >> to it directly. (%1 - $$) is a scalar offset; adding the
; ORG back gives the absolute address in a form the assembler will fold.
%ifndef KERNEL_ORG
%error "KERNEL_ORG must be defined before including idt.inc"
%endif

%define ABS_ADDR(x) ((x) - $$ + KERNEL_ORG)

; IDT_GATE handler, selector, type_attr
%macro IDT_GATE 3
    dw ABS_ADDR(%1) & 0xFFFF            ; offset 15..0
    dw %2                               ; segment selector
    db 0                                ; reserved
    db %3                               ; type and attributes
    dw (ABS_ADDR(%1) >> 16) & 0xFFFF    ; offset 31..16
%endmacro

idt_start:

    ; Vector 0 - Divide Error
    IDT_GATE isr0, 0x08, 0x8E

    ; Vectors 1..31 - unused CPU exceptions (filled in Phase 3)
    times 31 dq 0

    ; Vector 32 - PIT Timer (IRQ0, remapped)
    IDT_GATE isr_timer, 0x08, 0x8E

    ; Vectors 33..255 - unused
    times 223 dq 0

idt_end:

idt_descriptor:
    dw idt_end - idt_start - 1
    dd idt_start
