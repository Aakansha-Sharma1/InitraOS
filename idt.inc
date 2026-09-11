; InitraOS - Interrupt Descriptor Table

idt_start:

; Interrupt 0 - Divide Error
dw isr0
dw 0x08
db 0
db 0x8E
dw 0

; Interrupt 32 - PIT Timer (IRQ0)
times 31 dq 0

dw isr_timer
dw 0x08
db 0
db 0x8E
dw 0

; Remaining entries
times 223 dq 0

idt_end:

idt_descriptor:
    dw idt_end - idt_start - 1
    dd idt_start