/*
 * nxlibc - linker script for NeolyxOS C programs.
 *
 * Programs are static ELF64 executables linked at 0x1000000 (16 MB), the
 * base of every process's private user window (kernel/include/mm/
 * addrspace.h). The kernel puts the heap right after .bss and the stack at
 * the top of the window.
 *
 * .init_array (constructors) is kept so __attribute__((constructor))
 * functions run before main.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x1000000;

    .text : ALIGN(4096) {
        *(.text.startup .text.startup.*)
        *(.text .text.*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    }

    .init_array : ALIGN(8) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
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
        *(.fini_array*)
    }
}
