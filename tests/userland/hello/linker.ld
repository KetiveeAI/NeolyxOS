# NeolyxOS Hello World Linker Script
# Produces a standalone ELF executable for userspace

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* User programs load at 0x400000 (standard Linux convention) */
SECTIONS
{
    . = 0x400000;
    
    .text : ALIGN(4096)
    {
        *(.text)
        *(.text.*)
    }
    
    .rodata : ALIGN(4096)
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : ALIGN(4096)
    {
        *(.data)
        *(.data.*)
    }
    
    .bss : ALIGN(4096)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    
    /* Discard unneeded sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
