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

    .bss :
    {
        *(.bss)
        *(COMMON)
    }

    __kernel_end = .;
}