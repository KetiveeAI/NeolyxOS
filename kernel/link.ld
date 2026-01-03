/* kernel/link.ld - 64-bit kernel linker script */
ENTRY(kernel_main)

SECTIONS
{
    /* Load kernel at 1MB (0x100000) for 64-bit */
    . = 0x100000;
    
    .text : {
        *(.text)
        *(.text.*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    
    /* Discard other sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}