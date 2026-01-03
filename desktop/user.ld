/* Userspace program linker script for NeolyxOS */
/* Links at 0x1000000 (16MB) - safely above kernel BSS which ends at ~4MB */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x1000000;  /* 16MB mark - above kernel BSS (ends at ~4MB) */
    
    .text : {
        *(.text.startup)
        *(.text .text.*)
    }
    
    .rodata : {
        *(.rodata .rodata.*)
    }
    
    .data : {
        *(.data .data.*)
    }
    
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
