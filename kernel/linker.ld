ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
    . = 0x100000;
    
    /* Multiboot header must be first for QEMU -kernel */
    .multiboot : {
        *(.multiboot)
    }
    
    .text : {
        *(.text*)
    }
    
    .rodata : {
        *(.rodata*)
    }
    
    .data : {
        *(.data*)
    }
    
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.pdata*)
        *(.xdata*)
    }
}
