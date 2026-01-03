# kernel/src/arch/x86_64/startup_uefi.S
# UEFI 64-bit Kernel Entry Point
# NeolyxOS - Copyright (c) 2025 KetiveeAI
#
# Called directly by UEFI bootloader in 64-bit long mode
# First argument (RDI): pointer to NeolyxBootInfo structure

.section .text
.code64

.global _start
.extern kernel_main

# Entry point - called by UEFI bootloader
# Already in 64-bit mode, paging enabled, interrupts disabled
_start:
    # Disable interrupts (should already be disabled)
    cli
    
    # Debug: write "UEFI" to serial port
    mov $0x3F8, %dx
    mov $'U', %al
    out %al, (%dx)
    mov $'E', %al
    out %al, (%dx)
    mov $'F', %al
    out %al, (%dx)
    mov $'I', %al
    out %al, (%dx)
    mov $'\r', %al
    out %al, (%dx)
    mov $'\n', %al
    out %al, (%dx)
    
    # Save boot_info pointer (RDI = first arg per System V AMD64 ABI)
    mov %rdi, %r12
    
    # Set up our own stack (UEFI stack may be too small)
    lea stack_top(%rip), %rsp
    
    # Align stack to 16 bytes
    and $~0xF, %rsp
    
    # Clear direction flag
    cld
    
    # Debug: write "64" to serial
    mov $0x3F8, %dx
    mov $'6', %al
    out %al, (%dx)
    mov $'4', %al
    out %al, (%dx)
    mov $'\r', %al
    out %al, (%dx)
    mov $'\n', %al
    out %al, (%dx)
    
    # Restore boot_info pointer to RDI for kernel_main call
    mov %r12, %rdi
    
    # Call kernel_main(boot_info)
    call kernel_main
    
    # If kernel returns, halt
halt_loop:
    cli
    hlt
    jmp halt_loop

# Stack (16KB)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
