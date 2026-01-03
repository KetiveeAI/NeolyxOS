/*
 * NeolyxOS Interrupt Service Routines
 * 
 * Assembly stubs for interrupt handling.
 * These save registers, call C handlers, restore, and IRETQ.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

.section .text
.code64

/* ============ Macro for ISR without error code ============ */
.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    push $0             /* Dummy error code */
    push $\num          /* Interrupt number */
    jmp isr_common_stub
.endm

/* ============ Macro for ISR with error code ============ */
.macro ISR_ERRCODE num
.global isr\num
isr\num:
    push $\num          /* Interrupt number */
    jmp isr_common_stub
.endm

/* ============ Macro for IRQ ============ */
.macro IRQ num, irq_num
.global irq\num
irq\num:
    push $0             /* Dummy error code */
    push $\irq_num      /* IRQ number (32 + num) */
    jmp isr_common_stub
.endm

/* ============ Exception ISRs (0-31) ============ */

ISR_NOERRCODE 0     /* Division Error */
ISR_NOERRCODE 1     /* Debug */
ISR_NOERRCODE 2     /* NMI */
ISR_NOERRCODE 3     /* Breakpoint */
ISR_NOERRCODE 4     /* Overflow */
ISR_NOERRCODE 5     /* Bound Range */
ISR_NOERRCODE 6     /* Invalid Opcode */
ISR_NOERRCODE 7     /* Device Not Available */
ISR_ERRCODE   8     /* Double Fault */
ISR_NOERRCODE 9     /* Coprocessor Segment Overrun */
ISR_ERRCODE   10    /* Invalid TSS */
ISR_ERRCODE   11    /* Segment Not Present */
ISR_ERRCODE   12    /* Stack Fault */
ISR_ERRCODE   13    /* General Protection Fault */
ISR_ERRCODE   14    /* Page Fault */
ISR_NOERRCODE 15    /* Reserved */
ISR_NOERRCODE 16    /* x87 FP Exception */
ISR_ERRCODE   17    /* Alignment Check */
ISR_NOERRCODE 18    /* Machine Check */
ISR_NOERRCODE 19    /* SIMD FP Exception */
ISR_NOERRCODE 20    /* Virtualization */
ISR_ERRCODE   21    /* Control Protection */
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30    /* Security Exception */
ISR_NOERRCODE 31

/* ============ Hardware IRQs (32-47) ============ */

IRQ 0, 32           /* Timer */
IRQ 1, 33           /* Keyboard */
IRQ 2, 34           /* Cascade */
IRQ 3, 35           /* COM2 */
IRQ 4, 36           /* COM1 */
IRQ 5, 37           /* LPT2 */
IRQ 6, 38           /* Floppy */
IRQ 7, 39           /* LPT1 */
IRQ 8, 40           /* RTC */
IRQ 9, 41           /* Free */
IRQ 10, 42          /* Free */
IRQ 11, 43          /* Free */
IRQ 12, 44          /* PS/2 Mouse */
IRQ 13, 45          /* FPU */
IRQ 14, 46          /* Primary ATA */
IRQ 15, 47          /* Secondary ATA */

/* ============ Common Stub ============ */

.extern isr_handler

isr_common_stub:
    /* Save all general-purpose registers */
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    
    /* Save segment registers (if needed in future) */
    mov %ds, %ax
    push %rax
    mov %es, %ax
    push %rax
    
    /* Load kernel data segment */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    
    /* Call C handler with registers pointer */
    mov %rsp, %rdi      /* First argument: pointer to registers */
    call isr_handler
    
    /* Restore segment registers */
    pop %rax
    mov %ax, %es
    pop %rax
    mov %ax, %ds
    
    /* Restore general-purpose registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    
    /* Remove interrupt number and error code */
    add $16, %rsp
    
    /* Return from interrupt */
    iretq

/* ============ ISR Stub Table ============ */

.section .data
.global isr_stub_table
isr_stub_table:
    .quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .quad isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .quad irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    .quad irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
