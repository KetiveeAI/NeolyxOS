/*
 * NeolyxOS Interrupt Descriptor Table Implementation
 * 
 * Sets up the IDT for x86_64 with PIC remapping.
 * Clean, production-quality code.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "arch/x86_64/idt.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* ============ IDT State ============ */

/* The actual IDT */
static idt_entry_t idt[IDT_ENTRIES];

/* IDT pointer for LIDT instruction */
static idt_ptr_t idt_ptr;

/* Registered handlers (C functions) */
static interrupt_handler_t handlers[IDT_ENTRIES];

/* ============ PIC Ports ============ */

#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

#define ICW1_INIT       0x10
#define ICW1_ICW4       0x01
#define ICW4_8086       0x01

/* Exception names for debug output - used in verbose mode */
static const char *exception_names[] __attribute__((unused)) = {
    "Division Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FP Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

/* ============ Helper Functions ============ */

static void serial_hex64(uint64_t val) __attribute__((unused));
static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ PIC Implementation ============ */

void pic_init(void) {
    /* Save masks - reserved for future mask restore capability */
    uint8_t mask1 __attribute__((unused)) = inb(PIC1_DATA);
    uint8_t mask2 __attribute__((unused)) = inb(PIC2_DATA);
    
    /* ICW1: Start initialization sequence */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    /* ICW2: Remap IRQs to 32-47 */
    outb(PIC1_DATA, IRQ_BASE);      /* IRQ 0-7 → 32-39 */
    io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8);  /* IRQ 8-15 → 40-47 */
    io_wait();
    
    /* ICW3: Tell PICs about each other */
    outb(PIC1_DATA, 0x04);  /* IRQ2 has slave */
    io_wait();
    outb(PIC2_DATA, 0x02);  /* Slave identity */
    io_wait();
    
    /* ICW4: 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    /* Restore saved masks (mask all initially) */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    serial_puts("[IDT] PIC remapped to IRQ 32-47\n");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t mask = inb(port);
    outb(port, mask | (1 << irq));
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t mask = inb(port);
    outb(port, mask & ~(1 << irq));
}

/* ============ IDT Implementation ============ */

void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

/* ============ Assembly stub table (defined in isr.S) ============ */

extern uint64_t isr_stub_table[];

/* ============ IDT Initialization ============ */

void idt_init(void) {
    serial_puts("[IDT] Initializing interrupt descriptor table...\n");
    
    /* Initialize PIC first (remap IRQs to 32-47) */
    pic_init();
    
    /* Set up exception handlers (0-31) using assembly stubs */
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, isr_stub_table[i], 0x08, IDT_GATE_INTERRUPT);
        handlers[i] = 0;
    }
    
    /* Set up IRQ handlers (32-47) using assembly stubs */
    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, isr_stub_table[32 + i], 0x08, IDT_GATE_INTERRUPT);
        handlers[32 + i] = 0;
    }
    
    /* Clear remaining entries */
    for (int i = 48; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0x08, IDT_GATE_INTERRUPT);
        handlers[i] = 0;
    }
    
    /* Set up IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    
    /* Load IDT */
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    
    /* Unmask essential IRQs */
    pic_unmask_irq(0);  /* Timer (IRQ0 -> INT 32) */
    pic_unmask_irq(1);  /* Keyboard (IRQ1 -> INT 33) */
    pic_unmask_irq(2);  /* Cascade for slave PIC (required for IRQ8-15) */
    pic_unmask_irq(12); /* Mouse (IRQ12 -> INT 44) - also unmasked by mouse driver */
    
    serial_puts("[IDT] IDT loaded with ");
    serial_putc('4');
    serial_putc('8');
    serial_puts(" handlers\n");
}

void interrupts_enable(void) {
    __asm__ volatile("sti");
}

void interrupts_disable(void) {
    __asm__ volatile("cli");
}

int interrupts_enabled(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    return (flags & 0x200) ? 1 : 0;
}