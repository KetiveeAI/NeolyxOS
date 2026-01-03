/* EFI Bootloader Linker Script */
ENTRY(efi_entry)
OUTPUT_FORMAT(binary)

SECTIONS {
    . = 0;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
