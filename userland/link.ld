/*
 * Vesper OS – User-space linker script
 *
 * User ELF binaries are loaded at virtual address 0x01000000
 * (USER_LOAD_BASE defined in kernel/elf.h).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x01000000;

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata*)
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
}
