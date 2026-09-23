ENTRY(kernel_start)

SECTIONS
{
    . = 0x8800;

    .text :
    {
        *(.text)
        *(.text64)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    __bss_start = .;

    .bss :
    {
        *(.bss)
        *(COMMON)
    }

    __bss_end = .;

    __kernel_end = .;
}