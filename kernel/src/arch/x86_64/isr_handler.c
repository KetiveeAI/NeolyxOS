/*
 * NeolyxOS Interrupt Handler Dispatch
 * 
 * Main C handler called from assembly ISR stubs.
 * Routes interrupts to appropriate handlers.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "arch/x86_64/idt.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void pic_send_eoi(uint8_t irq);
extern void nxmouse_irq_handler(void *frame);

/* ============ Register State ============ */

/* Pushed by assembly stub */
typedef struct {
    uint64_t es;
    uint64_t ds;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;        /* Interrupt number */
    uint64_t err_code;      /* Error code (or 0) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) registers_t;

/* ============ Exception Names ============ */

static const char *exception_names[] = {
    "Division Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FP Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception"
};

/* ============ Helper ============ */

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_puts(&buf[i + 1]);
}

/* ============ Main Interrupt Handler ============ */

void isr_handler(registers_t *regs) {
    uint64_t int_no = regs->int_no;
    
    /* Exception (0-31) */
    if (int_no < 32) {
        serial_puts("\n!!! EXCEPTION: ");
        if (int_no < 20) {
            serial_puts(exception_names[int_no]);
        } else {
            serial_puts("Unknown");
        }
        serial_puts(" !!!\n");
        
        serial_puts("Vector: ");
        serial_dec(int_no);
        serial_puts("\n");
        
        serial_puts("Error Code: ");
        serial_hex64(regs->err_code);
        serial_puts("\n");
        
        serial_puts("RIP: ");
        serial_hex64(regs->rip);
        serial_puts("\n");
        
        serial_puts("CS: ");
        serial_hex64(regs->cs);
        serial_puts("\n");
        
        serial_puts("RFLAGS: ");
        serial_hex64(regs->rflags);
        serial_puts("\n");
        
        serial_puts("RSP: ");
        serial_hex64(regs->rsp);
        serial_puts("\n");
        
        /* Page fault: show CR2 */
        if (int_no == 14) {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            serial_puts("CR2 (fault addr): ");
            serial_hex64(cr2);
            serial_puts("\n");
        }
        
        serial_puts("\n*** SYSTEM HALTED ***\n");
        while (1) {
            __asm__ volatile("cli; hlt");
        }
    }
    
    /* Hardware IRQs (32-47) */
    if (int_no >= 32 && int_no < 48) {
        uint8_t irq = int_no - 32;
        
        /* Debug: count IRQs to verify interrupts work after CR3 switch */
        static uint32_t irq_total = 0;
        static uint32_t irq12_count = 0;
        irq_total++;
        if (irq == 12) irq12_count++;
        
        /* Log first 10 IRQs and periodically after */
        if (irq_total <= 10 || irq_total % 5000 == 0) {
            serial_puts("[ISR] IRQ#");
            serial_dec(irq);
            serial_puts(" (total=");
            serial_dec(irq_total);
            serial_puts(", mouse=");
            serial_dec(irq12_count);
            serial_puts(")\n");
        }
        
        switch (irq) {
            case 0:  /* Timer */
                pit_tick();
                break;
                
            case 1:  /* Keyboard */
                keyboard_irq();
                break;
                
            case 12: /* Mouse */
                nxmouse_irq_handler((void*)regs);
                break;
                
            default:
                /* Unhandled IRQ */
                break;
        }
        
        /* EOI sent exactly once by dispatcher (never by drivers) */
        pic_send_eoi(irq);
    }
}
