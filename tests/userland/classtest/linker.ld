/* NeolyxOS user program linker script
 *
 * Links at 0x1000000 (16MB), the base of the per-process user window
 * (kernel/include/mm/addrspace.h). Every process has a private address
 * space, so this program and the desktop use the same addresses without
 * touching each other's memory.
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x1000000;

    .text : ALIGN(4096) {
        *(.text.startup)
        *(.text .text.*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096) {
        *(.data .data.*)
    }

    .bss : ALIGN(16) {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
