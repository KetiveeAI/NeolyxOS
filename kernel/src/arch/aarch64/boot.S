/*
 * NeolyxOS ARM64 Boot Stub
 * 
 * Entry point for AArch64 kernel boot
 * 
 * Boot sequence:
 *   1. Firmware/bootloader passes control at EL2 or EL1
 *   2. Set up EL1 if coming from EL2
 *   3. Initialize stack
 *   4. Set up exception vectors
 *   5. Enable MMU
 *   6. Jump to C kernel_main
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

.section .text.boot
.global _start
.global _arm64_start

/* ============ Boot Entry Point ============ */

_start:
_arm64_start:
    /* Disable interrupts */
    msr     daifset, #0xf
    
    /* Get current exception level */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    and     x0, x0, #3
    
    /* If EL3, drop to EL2 */
    cmp     x0, #3
    beq     from_el3
    
    /* If EL2, drop to EL1 */
    cmp     x0, #2
    beq     from_el2
    
    /* Already at EL1, continue */
    b       at_el1

/* ============ Drop from EL3 to EL2 ============ */

from_el3:
    /* Configure EL2 to be AArch64 */
    mov     x0, #(1 << 10)      /* RW bit = AArch64 for EL2 */
    orr     x0, x0, #(1 << 0)   /* NS bit = non-secure */
    msr     scr_el3, x0
    
    /* Set SPSR for return to EL2 */
    mov     x0, #0x3c9          /* EL2h, all interrupts masked */
    msr     spsr_el3, x0
    
    /* Set return address */
    adr     x0, from_el2
    msr     elr_el3, x0
    
    eret

/* ============ Drop from EL2 to EL1 ============ */

from_el2:
    /* Disable EL2 traps */
    mov     x0, xzr
    msr     hcr_el2, x0
    
    /* Configure EL1 to be AArch64 */
    mov     x0, #(1 << 31)      /* RW bit = AArch64 for EL1 */
    orr     x0, x0, #(1 << 29)  /* Disable hypervisor call trapping */
    msr     hcr_el2, x0
    
    /* Set SCTLR_EL1 to safe defaults */
    mov     x0, xzr
    orr     x0, x0, #(1 << 29)  /* LSMAOE */
    orr     x0, x0, #(1 << 28)  /* nTLSMD */
    msr     sctlr_el1, x0
    
    /* Set SPSR for return to EL1 */
    mov     x0, #0x3c5          /* EL1h, all interrupts masked */
    msr     spsr_el2, x0
    
    /* Set return address */
    adr     x0, at_el1
    msr     elr_el2, x0
    
    eret

/* ============ At EL1 - Main Boot ============ */

at_el1:
    /* Get CPU ID */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    
    /* Only CPU 0 continues boot; others wait */
    cbnz    x0, cpu_spin
    
    /* Set up stack (16KB kernel stack) */
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0
    
    /* Clear BSS */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
    
bss_loop:
    cmp     x0, x1
    bge     bss_done
    str     xzr, [x0], #8
    b       bss_loop
bss_done:

    /* Set up exception vectors */
    adrp    x0, arm64_vectors
    add     x0, x0, :lo12:arm64_vectors
    msr     vbar_el1, x0
    isb
    
    /* Initialize MMU and caches */
    bl      arm64_mmu_early_init
    
    /* Jump to C kernel entry */
    bl      kernel_main_arm64
    
    /* If kernel returns, halt */
halt:
    wfi
    b       halt

/* ============ Secondary CPU Spin ============ */

cpu_spin:
    wfe
    b       cpu_spin

/* ============ Exception Vector Table ============ */

.balign 0x800
.global arm64_vectors
arm64_vectors:

/* Current EL with SP_EL0 */
.balign 0x80
vector_sync_el1_sp0:
    b       sync_exception_handler

.balign 0x80
vector_irq_el1_sp0:
    b       irq_handler

.balign 0x80
vector_fiq_el1_sp0:
    b       fiq_handler

.balign 0x80
vector_serror_el1_sp0:
    b       serror_handler

/* Current EL with SP_ELx */
.balign 0x80
vector_sync_el1_spx:
    b       sync_exception_handler

.balign 0x80
vector_irq_el1_spx:
    b       irq_handler

.balign 0x80
vector_fiq_el1_spx:
    b       fiq_handler

.balign 0x80
vector_serror_el1_spx:
    b       serror_handler

/* Lower EL using AArch64 */
.balign 0x80
vector_sync_el0_64:
    b       sync_el0_handler

.balign 0x80
vector_irq_el0_64:
    b       irq_el0_handler

.balign 0x80
vector_fiq_el0_64:
    b       fiq_handler

.balign 0x80
vector_serror_el0_64:
    b       serror_handler

/* Lower EL using AArch32 - not supported */
.balign 0x80
    b       halt
.balign 0x80
    b       halt
.balign 0x80
    b       halt
.balign 0x80
    b       halt

/* ============ Context Save/Restore Macros ============ */

.macro save_context
    sub     sp, sp, #272           /* Space for 34 regs */
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x30, x0, [sp, #240]
    str     x1, [sp, #256]
.endm

.macro restore_context
    ldr     x1, [sp, #256]
    ldp     x30, x0, [sp, #240]
    msr     spsr_el1, x1
    msr     elr_el1, x0
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #272
.endm

/* ============ Exception Handlers ============ */

sync_exception_handler:
    save_context
    mov     x0, sp              /* Pass context pointer */
    mrs     x1, esr_el1         /* Exception syndrome */
    mrs     x2, far_el1         /* Fault address */
    bl      arm64_sync_handler
    restore_context
    eret

irq_handler:
    save_context
    mov     x0, sp
    bl      arm64_irq_handler
    restore_context
    eret

fiq_handler:
    save_context
    mov     x0, sp
    bl      arm64_fiq_handler
    restore_context
    eret

serror_handler:
    save_context
    mov     x0, sp
    bl      arm64_serror_handler
    restore_context
    eret

sync_el0_handler:
    save_context
    mov     x0, sp
    mrs     x1, esr_el1
    bl      arm64_syscall_handler
    restore_context
    eret

irq_el0_handler:
    save_context
    mov     x0, sp
    bl      arm64_irq_handler
    restore_context
    eret

/* ============ Context Switch ============ */

.global arm64_context_switch
arm64_context_switch:
    /* x0 = old context, x1 = new context */
    
    /* Save callee-saved registers to old context */
    stp     x19, x20, [x0, #152]
    stp     x21, x22, [x0, #168]
    stp     x23, x24, [x0, #184]
    stp     x25, x26, [x0, #200]
    stp     x27, x28, [x0, #216]
    stp     x29, x30, [x0, #232]
    mov     x2, sp
    str     x2, [x0, #248]          /* SP */
    
    /* Restore from new context */
    ldp     x19, x20, [x1, #152]
    ldp     x21, x22, [x1, #168]
    ldp     x23, x24, [x1, #184]
    ldp     x25, x26, [x1, #200]
    ldp     x27, x28, [x1, #216]
    ldp     x29, x30, [x1, #232]
    ldr     x2, [x1, #248]
    mov     sp, x2
    
    ret

/* ============ Stack ============ */

.section .bss
.balign 16
__stack_bottom:
    .space  16384               /* 16KB kernel stack */
.global __stack_top
__stack_top:

.section .bss
.global __bss_start
__bss_start:

.section .bss
.global __bss_end
__bss_end:
