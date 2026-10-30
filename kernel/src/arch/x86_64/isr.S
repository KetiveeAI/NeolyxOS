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
/*
 * Kernel stack layout while isr_handler() runs (higher addresses on top).
 * RBX holds the registers_t pointer for the whole C call; it is callee-saved
 * and the interrupted RBX is already stored in the frame (RBX+0x78).
 *
 *   RBX+0xB8  ss        \
 *   RBX+0xB0  rsp        |
 *   RBX+0xA8  rflags     |  pushed by the CPU
 *   RBX+0xA0  cs         |
 *   RBX+0x98  rip       /
 *   RBX+0x90  err_code     (CPU or stub dummy 0)
 *   RBX+0x88  int_no       (stub)
 *   RBX+0x80  rax ... RBX+0x10 r15   (15 GPRs, see registers_t)
 *   RBX+0x08  ds
 *   RBX+0x00  es        <- RBX = registers_t * passed to isr_handler (RDI)
 *   [padding]           0 bytes in practice (see below), at most 15
 *   RSP+0x000 .. RSP+0x1FF  FXSAVE64 area, 512 bytes, 16-byte aligned
 *
 * In 64-bit mode the CPU aligns RSP to 16 bytes before pushing the
 * 40-byte interrupt frame, and the stub adds 152 bytes, so registers_t
 * (0xC0 bytes) starts 16-byte aligned and the AND below is a no-op. It is
 * kept so a misaligned entry can never make FXSAVE64 raise #GP.
 *
 * FPU/SSE state: the kernel is compiled with SSE enabled, so any C code
 * reached from here may clobber XMM registers and MXCSR. The interrupted
 * context's x87/MMX/SSE state is saved before the C call and restored
 * before IRETQ. The save area lives on the stack of the entry it belongs
 * to, so nested interrupts and stack-switching context switches each find
 * their own area. No task is resumed through this return path with a frame
 * built in C today (the preemptive scheduler hook in pit_tick() is
 * disabled); a future scheduler that builds such frames must place a valid
 * FXSAVE image (e.g. nx_fpu_default_state in arch.S) below registers_t.
 * CR4.OSXSAVE is clear, so AVX/YMM state is not enabled and FXSAVE covers
 * all user-visible FP state.
 *
 * Interrupt gates leave DF as the interrupted context had it, so CLD runs
 * before any C code (SysV ABI requires DF=0); IRETQ restores the original.
 */

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
    
    /* The SysV ABI requires DF=0 on entry to C; interrupt gates do not
     * clear it and the interrupted context may have set it. IRETQ restores
     * the interrupted RFLAGS (including DF). */
    cld
    
    /* RBX = registers_t pointer (callee-saved, survives the C call) */
    mov %rsp, %rbx
    
    /* Save interrupted FPU/SSE state in a 512-byte 16-byte-aligned area */
    sub $512, %rsp
    and $-16, %rsp
    fxsave64 (%rsp)
    
    /* Run kernel C code with a known FP environment, independent of the
     * interrupted context: x87 reset (exceptions masked, none pending) and
     * MXCSR = 0x1F80 (SIMD exceptions masked, round to nearest, no FTZ/DAZ).
     * The saved state, including any pending x87 exception, is restored
     * before IRETQ. */
    fninit
    ldmxcsr nx_fpu_default_mxcsr(%rip)
    
    /* Call C handler with registers pointer (RSP is 16-byte aligned here) */
    mov %rbx, %rdi      /* First argument: pointer to registers */
    call isr_handler
    
    /* Restore interrupted FPU/SSE state, then drop the save area */
    fxrstor64 (%rsp)
    mov %rbx, %rsp
    
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
