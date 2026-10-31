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
    
    _kernel_end = .;

    /* 0x1000000 (16 MB) is USER_WINDOW_BASE in include/mm/addrspace.h:
     * every process maps its own memory there, so the kernel image must end
     * below it. Fail the link instead of booting a kernel that overlaps it. */
    ASSERT(_kernel_end <= 0x1000000, "kernel image reaches the 16 MB user window (USER_WINDOW_BASE)")
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.pdata*)
        *(.xdata*)
    }
}
