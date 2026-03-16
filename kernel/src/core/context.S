/*
 * NeolyxOS Context Switch Assembly
 * 
 * Save/restore CPU context for process switching.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

.section .text
.code64

/* ============ Context Switch ============ */
/* 
 * void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx);
 * 
 * RDI = pointer to old context (save here)
 * RSI = pointer to new context (load from here)
 * 
 * cpu_context_t layout (offsets):
 *   0x00: r15, r14, r13, r12 (4 * 8 = 32 bytes)
 *   0x20: r11, r10, r9, r8   (4 * 8 = 32 bytes)
 *   0x40: rbp, rdi, rsi, rdx, rcx, rbx, rax (7 * 8 = 56 bytes)
 *   0x78: ds, es, fs, gs     (4 * 8 = 32 bytes)
 *   0x98: rip, cs, rflags, rsp, ss (5 * 8 = 40 bytes)
 */

.global context_switch
context_switch:
    /* If old context is NULL, skip save */
    test %rdi, %rdi
    jz .load_context
    
    /* Save general purpose registers */
    mov %r15, 0x00(%rdi)
    mov %r14, 0x08(%rdi)
    mov %r13, 0x10(%rdi)
    mov %r12, 0x18(%rdi)
    mov %r11, 0x20(%rdi)
    mov %r10, 0x28(%rdi)
    mov %r9,  0x30(%rdi)
    mov %r8,  0x38(%rdi)
    mov %rbp, 0x40(%rdi)
    /* RDI/RSI will be restored from new context */
    mov %rdx, 0x58(%rdi)
    mov %rcx, 0x60(%rdi)
    mov %rbx, 0x68(%rdi)
    mov %rax, 0x70(%rdi)
    
    /* Save segment registers */
    mov %ds, %ax
    movzx %ax, %rax
    mov %rax, 0x78(%rdi)
    mov %es, %ax
    movzx %ax, %rax
    mov %rax, 0x80(%rdi)
    /* FS and GS handled by kernel */
    
    /* Save return address as RIP */
    mov (%rsp), %rax
    mov %rax, 0x98(%rdi)
    
    /* Save CS (kernel) */
    movq $0x08, 0xA0(%rdi)
    
    /* Save RFLAGS */
    pushfq
    pop %rax
    mov %rax, 0xA8(%rdi)
    
    /* Save RSP (after return address) */
    lea 8(%rsp), %rax
    mov %rax, 0xB0(%rdi)
    
    /* Save SS (kernel) */
    movq $0x10, 0xB8(%rdi)

.load_context:
    /* Load new context from RSI */
    
    /* Load general purpose registers */
    mov 0x00(%rsi), %r15
    mov 0x08(%rsi), %r14
    mov 0x10(%rsi), %r13
    mov 0x18(%rsi), %r12
    mov 0x20(%rsi), %r11
    mov 0x28(%rsi), %r10
    mov 0x30(%rsi), %r9
    mov 0x38(%rsi), %r8
    mov 0x40(%rsi), %rbp
    mov 0x48(%rsi), %rdi
    /* RSI loaded last */
    mov 0x58(%rsi), %rdx
    mov 0x60(%rsi), %rcx
    mov 0x68(%rsi), %rbx
    mov 0x70(%rsi), %rax
    
    /* Load RSP */
    mov 0xB0(%rsi), %rsp
    
    /* Push return address for ret */
    push 0x98(%rsi)
    
    /* Load RSI last (we were using it) */
    mov 0x50(%rsi), %rsi
    
    /* Return to new context */
    ret

/* ============ Enter User Mode ============ */
/*
 * void enter_usermode(uint64_t entry, uint64_t stack);
 * 
 * RDI = user entry point
 * RSI = user stack pointer
 * 
 * Switches to ring 3 using IRETQ.
 */

.global enter_usermode
enter_usermode:
    /* Build interrupt frame on stack */
    
    /* SS (user data segment = 0x23) */
    pushq $0x23
    
    /* RSP (user stack) */
    push %rsi
    
    /* RFLAGS (IF enabled) */
    pushq $0x202
    
    /* CS (user code 64-bit segment = 0x2B) */
    pushq $0x2B
    
    /* RIP (entry point) */
    push %rdi
    
    /* Clear registers */
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rsi, %rsi
    xor %rdi, %rdi
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15
    
    /* Load user data segment */
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    
    /* Jump to user mode */
    iretq

/* ============ Switch Stack ============ */
/*
 * void switch_stack(uint64_t new_stack, void (*func)(void*), void *arg);
 * 
 * Switches to a new stack and calls function.
 * Used for initial thread setup.
 */

.global switch_stack
switch_stack:
    /* RDI = new stack, RSI = function, RDX = arg */
    mov %rdi, %rsp
    mov %rdx, %rdi      /* arg in RDI (first param) */
    call *%rsi
    
    /* If function returns, halt */
    cli
1:  hlt
    jmp 1b
