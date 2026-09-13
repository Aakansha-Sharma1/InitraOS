; InitraOS - Interrupt Descriptor Table
;
; IDT entries are initialized at runtime because the kernel is now
; assembled as ELF32 and the final ISR addresses are resolved by the linker.

idt_start:

    ; Vector 0 - Divide Error
    times 8 db 0

    ; Vectors 1..31 - unused CPU exceptions
    times 31 dq 0

    ; Vector 32 - PIT Timer (IRQ0, remapped)
    times 8 db 0

    ; Vectors 33..255 - unused
    times 223 dq 0

idt_end:

idt_descriptor:
    dw idt_end - idt_start - 1
    dd idt_start