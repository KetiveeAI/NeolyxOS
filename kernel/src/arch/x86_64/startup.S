# kernel/src/arch/x86_64/startup.S
# NeolyxOS 64-bit Kernel Entry Point
# 
# This code runs AFTER the NeolyxOS UEFI bootloader has:
#   - Set up 64-bit long mode
#   - Configured valid segment descriptors
#   - Exited boot services
#   - Placed boot_info pointer in RDI register
#
# Copyright (c) 2025 KetiveeAI

.section .text
.code64

.global _start
.extern kernel_main

# Entry point - UEFI bootloader jumps here in 64-bit mode
# RDI contains pointer to NeolyxBootInfo structure
_start:
    # Disable interrupts
    cli
    
    # Enable SSE/FPU (required for vector graphics code)
    # Clear CR0.EM (bit 2), set CR0.MP (bit 1)
    mov %cr0, %rax
    and $0xFFFFFFFFFFFFFFFB, %rax   # Clear EM (bit 2)
    or $0x2, %rax                    # Set MP (bit 1)
    mov %rax, %cr0
    
    # Set CR4.OSFXSR (bit 9) and CR4.OSXMMEXCPT (bit 10)
    mov %cr4, %rax
    or $0x600, %rax                  # Set bits 9 and 10
    mov %rax, %cr4
    
    # Save boot_info pointer (RDI) - will be destroyed by BSS clear
    mov %rdi, %r15
    
    # Debug: write "K64" to serial port
    mov $0x3F8, %dx
    mov $'K', %al
    out %al, (%dx)
    mov $'6', %al
    out %al, (%dx)
    mov $'4', %al
    out %al, (%dx)
    mov $'\r', %al
    out %al, (%dx)
    mov $'\n', %al
    out %al, (%dx)
    
    # Set up our own stack (16KB)
    lea stack_top(%rip), %rsp
    
    # Debug: write "STK" to serial
    mov $0x3F8, %dx
    mov $'S', %al
    out %al, (%dx)
    mov $'T', %al
    out %al, (%dx)
    mov $'K', %al
    out %al, (%dx)
    mov $'\r', %al
    out %al, (%dx)
    mov $'\n', %al
    out %al, (%dx)
    
    # Clear BSS section
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    shr $3, %rcx          # Divide by 8 (qwords)
    xor %rax, %rax
    cld
    rep stosq
    
    # Debug: write "GO!" to serial
    mov $0x3F8, %dx
    mov $'G', %al
    out %al, (%dx)
    mov $'O', %al
    out %al, (%dx)
    mov $'!', %al
    out %al, (%dx)
    mov $'\r', %al
    out %al, (%dx)
    mov $'\n', %al
    out %al, (%dx)
    
    # Restore boot_info pointer to RDI for kernel_main(boot_info)
    mov %r15, %rdi
    
    # Call kernel_main(boot_info)
    call kernel_main
    
    # If kernel_main returns, halt
halt_loop:
    cli
    hlt
    jmp halt_loop

# Stack (16KB, aligned to 16 bytes)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
