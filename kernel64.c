/*
 * InitraOS - minimal 64-bit C kernel entry test.
 *
 * This file is intentionally independent from the existing
 * 32-bit kernel.c. It is compiled as freestanding x86-64 code
 * and embedded into the existing 64-bit kernel section.
 */

#include <stdint.h>

#define COM1 ((uint16_t)0x3F8)

static void serial64_c_putc(
    uint8_t value
);

/*
 * Keep the C entry point first in .text.
 *
 * The assembly bootstrap embeds the complete .text section
 * and calls its first byte as kernel64_c_main.
 */
void kernel64_main(void)
{
    serial64_c_putc('[');
    serial64_c_putc('I');
    serial64_c_putc('n');
    serial64_c_putc('i');
    serial64_c_putc('t');
    serial64_c_putc('r');
    serial64_c_putc('a');
    serial64_c_putc('O');
    serial64_c_putc('S');
    serial64_c_putc(']');
    serial64_c_putc(' ');
    serial64_c_putc('K');
    serial64_c_putc('E');
    serial64_c_putc('R');
    serial64_c_putc('N');
    serial64_c_putc('E');
    serial64_c_putc('L');
    serial64_c_putc('6');
    serial64_c_putc('4');
    serial64_c_putc('_');
    serial64_c_putc('C');
    serial64_c_putc('_');
    serial64_c_putc('O');
    serial64_c_putc('K');
    serial64_c_putc('\r');
    serial64_c_putc('\n');
}

static inline uint8_t io_inb(
    uint16_t port
)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void io_outb(
    uint16_t port,
    uint8_t value
)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void serial64_c_putc(
    uint8_t value
)
{
    while ((io_inb(COM1 + 5) & 0x20) == 0)
    {
    }

    io_outb(COM1, value);
}