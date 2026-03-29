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

/* Pushed by assembly stub - ORDER MUST MATCH isr.S push/pop sequence!
 * First pushed register is at HIGHEST offset from RSP.
 * Assembly pushes: rax, rbx, rcx, rdx, rsi, rdi, rbp, r8-r15, ds, es
 * So in struct: es (at RSP), ds, r15, r14...r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
 */
typedef struct {
    /* Segment registers (pushed last, at RSP) */
    uint64_t es;
    uint64_t ds;
    /* General purpose (pushed in reverse order) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* ISR stub values */
    uint64_t int_no;        /* Interrupt number */
    uint64_t err_code;      /* Error code (or 0) */
    /* CPU-pushed interrupt frame */
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

/* Forward declaration */
extern void fault_route(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs);

extern void e1000_irq_handler(void);

void isr_handler(registers_t *regs) {
    uint64_t int_no = regs->int_no;
    
    /* Exception (0-31) */
    if (int_no < 32) {
        /* Determine origin: userspace (CS & 3) or kernel */
        int from_userspace = (regs->cs & 3) == 3;
        
        serial_puts("\n╔══════════════════════════════════════════════════════════╗\n");
        if (from_userspace) {
            serial_puts("║           USERSPACE EXCEPTION CAUGHT                     ║\n");
        } else {
            serial_puts("║             KERNEL EXCEPTION CAUGHT                      ║\n");
        }
        serial_puts("╚══════════════════════════════════════════════════════════╝\n\n");
        
        serial_puts("Exception: ");
        if (int_no < 20) {
            serial_puts(exception_names[int_no]);
        } else {
            serial_puts("Unknown");
        }
        serial_puts("\n");
        
        serial_puts("Vector: ");
        serial_dec(int_no);
        serial_puts("  Origin: ");
        serial_puts(from_userspace ? "USERSPACE (Ring 3)" : "KERNEL (Ring 0)");
        serial_puts("\n\n");
        
        serial_puts("Error Code: ");
        serial_hex64(regs->err_code);
        serial_puts("\n");
        
        serial_puts("RIP: ");
        serial_hex64(regs->rip);
        serial_puts("\n");
        
        serial_puts("CS:  ");
        serial_hex64(regs->cs);
        serial_puts("\n");
        
        serial_puts("RFLAGS: ");
        serial_hex64(regs->rflags);
        serial_puts("\n");
        
        serial_puts("RSP: ");
        serial_hex64(regs->rsp);
        serial_puts("\n");
        
        /* Page fault: show CR2 and fault details */
        if (int_no == 14) {
            uint64_t cr2, cr4;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
            
            serial_puts("CR2 (fault addr): ");
            serial_hex64(cr2);
            serial_puts("\n");
            
            serial_puts("CR4: ");
            serial_hex64(cr4);
            serial_puts("\n");
            
            /* Check SMEP and SMAP bits */
            serial_puts("SMEP (bit 20): ");
            serial_puts((cr4 & (1ULL << 20)) ? "ENABLED" : "disabled");
            serial_puts("  SMAP (bit 21): ");
            serial_puts((cr4 & (1ULL << 21)) ? "ENABLED" : "disabled");
            serial_puts("\n");
            
            /* Decode error code bits */
            serial_puts("PF Error bits: ");
            serial_puts((regs->err_code & 1) ? "P=1(protection) " : "P=0(not-present) ");
            serial_puts((regs->err_code & 2) ? "W/R=1(WRITE) " : "W/R=0(READ) ");
            serial_puts((regs->err_code & 4) ? "U/S=1(USER) " : "U/S=0(SUPER) ");
            serial_puts((regs->err_code & 8) ? "RSVD=1(RESERVED!) " : "");
            serial_puts((regs->err_code & 16) ? "I/D=1(IFETCH)" : "");
            serial_puts("\n");
            
            /* Check page alignment */
            serial_puts("Page aligned: ");
            serial_puts((cr2 & 0xFFF) == 0 ? "YES" : "NO (offset=");
            if ((cr2 & 0xFFF) != 0) {
                serial_hex64(cr2 & 0xFFF);
                serial_puts(")");
            }
            serial_puts("\n");
        }
        
        /* Route to fault handler for userspace crashes */
        if (from_userspace) {
            serial_puts("\n[FAULT_ROUTER] Routing userspace exception...\n");
            fault_route(int_no, regs->err_code, regs->rip, regs->cs);
            serial_puts("[FAULT_ROUTER] Userspace process terminated.\n");
            return;
        }
        
        /* Kernel-space exception — unrecoverable, halt. */
        serial_puts("\n*** KERNEL EXCEPTION — SYSTEM HALTED ***\n");
        serial_puts("Press RESET to restart.\n");
        while (1) {
            __asm__ volatile("cli; hlt");
        }
    }
    
    /* Hardware IRQs (32-47) */
    if (int_no >= 32 && int_no < 48) {
        uint8_t irq = int_no - 32;
        
        switch (irq) {
            case 0:  /* Timer */
                pit_tick();
                break;
                
            case 1:  /* Keyboard */
                keyboard_irq();
                break;
                
            case 11: /* Network (e1000 on QEMU) */
                e1000_irq_handler();
                break;
                
            case 12: /* Mouse */
                nxmouse_irq_handler((void*)regs);
                break;
                
            default:
                break;
        }
        
        /* EOI sent exactly once by dispatcher (never by drivers) */
        pic_send_eoi(irq);
    }
}
