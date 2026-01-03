/*
 * NeolyxOS Kernel Panic Implementation
 * 
 * Halts system with diagnostic info.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/panic.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Helpers ============ */

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(int val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    char buf[12];
    int i = 11;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_puts(&buf[i + 1]);
}

/* ============ Panic Implementation ============ */

void kernel_panic(const char *message, uint64_t code) {
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    serial_puts("\n");
    serial_puts("╔══════════════════════════════════════════════════════════════╗\n");
    serial_puts("║                    KERNEL PANIC                              ║\n");
    serial_puts("╚══════════════════════════════════════════════════════════════╝\n");
    serial_puts("\n");
    
    serial_puts("Message: ");
    serial_puts(message ? message : "(null)");
    serial_puts("\n");
    
    serial_puts("Code:    ");
    serial_hex64(code);
    serial_puts("\n");
    
    /* Get RIP from stack if possible */
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    serial_puts("RSP:     ");
    serial_hex64(rsp);
    serial_puts("\n");
    
    serial_puts("\n*** SYSTEM HALTED ***\n");
    serial_puts("Reboot required.\n");
    
    /* Halt forever */
    while (1) {
        __asm__ volatile("hlt");
    }
}

void kernel_assert_fail(const char *expr, const char *file, int line) {
    __asm__ volatile("cli");
    
    serial_puts("\n!!! ASSERTION FAILED !!!\n");
    serial_puts("Expression: ");
    serial_puts(expr);
    serial_puts("\nFile: ");
    serial_puts(file);
    serial_puts("\nLine: ");
    serial_dec(line);
    serial_puts("\n");
    
    kernel_panic("Assertion failed", (uint64_t)line);
}
