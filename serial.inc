; InitraOS - COM1 serial driver (32-bit protected mode)
;
; Serial output is the primary debugging channel for OS development: QEMU can
; redirect it to a file or stdout, so boot output becomes machine-readable and
; CI can assert on it. Build this before you need it.

COM1 equ 0x3F8

; ---------------------------------------------------------------
; serial_init - 38400 baud, 8N1, FIFO enabled
; ---------------------------------------------------------------
serial_init:
    push eax
    push edx

    mov dx, COM1 + 1
    xor al, al
    out dx, al                      ; disable serial interrupts

    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al                      ; enable DLAB (set baud divisor)

    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al                      ; divisor low  = 3 -> 38400 baud

    mov dx, COM1 + 1
    xor al, al
    out dx, al                      ; divisor high = 0

    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al                      ; 8 bits, no parity, 1 stop bit

    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al                      ; enable FIFO, clear, 14-byte threshold

    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al                      ; RTS/DSR set

    pop edx
    pop eax
    ret

; ---------------------------------------------------------------
; serial_putc - AL = character
; ---------------------------------------------------------------
serial_putc:
    push eax
    push edx
    mov ah, al                      ; stash the character

    mov dx, COM1 + 5
.wait:
    in al, dx
    test al, 0x20                   ; transmitter holding register empty?
    jz .wait

    mov al, ah
    mov dx, COM1
    out dx, al

    pop edx
    pop eax
    ret

; ---------------------------------------------------------------
; serial_print - ESI = pointer to NUL-terminated string
; ---------------------------------------------------------------
serial_print:
    push eax
    push esi
.loop:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .loop
.done:
    pop esi
    pop eax
    ret

; ---------------------------------------------------------------
; serial_print_hex - EAX = value, prints 8 hex digits
; ---------------------------------------------------------------
serial_print_hex:
    push eax
    push ebx
    push ecx
    push edx

    mov edx, eax
    mov ecx, 8
.hex_loop:
    rol edx, 4
    mov ebx, edx
    and ebx, 0x0F

    cmp bl, 9
    jbe .digit
    add bl, 'A' - 10
    jmp .emit
.digit:
    add bl, '0'
.emit:
    mov al, bl
    call serial_putc
    loop .hex_loop

    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; ---------------------------------------------------------------
; serial_newline
; ---------------------------------------------------------------
serial_newline:
    push eax
    mov al, 13
    call serial_putc
    mov al, 10
    call serial_putc
    pop eax
    ret
