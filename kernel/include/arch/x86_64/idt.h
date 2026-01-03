/*
 * NeolyxOS Interrupt Descriptor Table
 * 
 * Sets up the x86_64 IDT for exception and interrupt handling.
 * This is required for keyboard IRQ, timer, and page faults.
 * 
 * Design:
 *   - 256 interrupt vectors
 *   - Exceptions (0-31)
 *   - Hardware IRQs (32-47 after PIC remap)
 *   - User-defined (48-255)
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_IDT_H
#define NEOLYX_IDT_H

#include <stdint.h>

/* ============ IDT Constants ============ */

#define IDT_ENTRIES         256

/* Interrupt vector numbers */
#define INT_DIVIDE_ERROR    0
#define INT_DEBUG           1
#define INT_NMI             2
#define INT_BREAKPOINT      3
#define INT_OVERFLOW        4
#define INT_BOUND_RANGE     5
#define INT_INVALID_OPCODE  6
#define INT_DEVICE_NA       7
#define INT_DOUBLE_FAULT    8
#define INT_INVALID_TSS     10
#define INT_SEGMENT_NP      11
#define INT_STACK_FAULT     12
#define INT_GPF             13
#define INT_PAGE_FAULT      14
#define INT_X87_FP          16
#define INT_ALIGNMENT       17
#define INT_MACHINE_CHECK   18
#define INT_SIMD_FP         19

/* Hardware IRQs (after PIC remap to 32+) */
#define IRQ_BASE            32
#define IRQ_TIMER           (IRQ_BASE + 0)
#define IRQ_KEYBOARD        (IRQ_BASE + 1)
#define IRQ_CASCADE         (IRQ_BASE + 2)
#define IRQ_COM2            (IRQ_BASE + 3)
#define IRQ_COM1            (IRQ_BASE + 4)
#define IRQ_LPT2            (IRQ_BASE + 5)
#define IRQ_FLOPPY          (IRQ_BASE + 6)
#define IRQ_LPT1            (IRQ_BASE + 7)
#define IRQ_RTC             (IRQ_BASE + 8)
#define IRQ_MOUSE           (IRQ_BASE + 12)
#define IRQ_FPU             (IRQ_BASE + 13)
#define IRQ_ATA_PRIMARY     (IRQ_BASE + 14)
#define IRQ_ATA_SECONDARY   (IRQ_BASE + 15)

/* Gate types */
#define IDT_GATE_INTERRUPT  0x8E  /* P=1, DPL=0, Type=Interrupt Gate */
#define IDT_GATE_TRAP       0x8F  /* P=1, DPL=0, Type=Trap Gate */
#define IDT_GATE_USER       0xEE  /* P=1, DPL=3, Type=Interrupt Gate (user-callable) */

/* ============ IDT Structures ============ */

/* IDT Entry (64-bit mode) */
typedef struct {
    uint16_t offset_low;    /* Handler address bits 0-15 */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table (bits 0-2), reserved (bits 3-7) */
    uint8_t  type_attr;     /* Gate type and attributes */
    uint16_t offset_mid;    /* Handler address bits 16-31 */
    uint32_t offset_high;   /* Handler address bits 32-63 */
    uint32_t reserved;      /* Must be zero */
} __attribute__((packed)) idt_entry_t;

/* IDT Pointer (for LIDT instruction) */
typedef struct {
    uint16_t limit;         /* Size of IDT - 1 */
    uint64_t base;          /* Base address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* Interrupt Frame (pushed by CPU on interrupt) */
typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame_t;

/* Interrupt Frame with Error Code */
typedef struct {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame_error_t;

/* ============ IDT API ============ */

/**
 * Initialize the IDT and load it.
 * Sets up all 256 entries and remaps PIC.
 */
void idt_init(void);

/**
 * Set an IDT entry.
 * 
 * @param vector    Interrupt vector number (0-255)
 * @param handler   Handler function address
 * @param selector  Code segment selector (usually 0x08)
 * @param type_attr Gate type and attributes
 */
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t type_attr);

/**
 * Register an interrupt handler.
 * 
 * @param vector  Interrupt vector number
 * @param handler Handler function
 */
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

/**
 * Enable interrupts (STI).
 */
void interrupts_enable(void);

/**
 * Disable interrupts (CLI).
 */
void interrupts_disable(void);

/**
 * Check if interrupts are enabled.
 * @return 1 if enabled, 0 if disabled
 */
int interrupts_enabled(void);

/* ============ PIC Control ============ */

/**
 * Initialize the 8259 PIC and remap IRQs to 32-47.
 */
void pic_init(void);

/**
 * Send End-Of-Interrupt signal to PIC.
 * @param irq IRQ number (0-15)
 */
void pic_send_eoi(uint8_t irq);

/**
 * Mask (disable) an IRQ.
 * @param irq IRQ number (0-15)
 */
void pic_mask_irq(uint8_t irq);

/**
 * Unmask (enable) an IRQ.
 * @param irq IRQ number (0-15)
 */
void pic_unmask_irq(uint8_t irq);

#endif /* NEOLYX_IDT_H */
