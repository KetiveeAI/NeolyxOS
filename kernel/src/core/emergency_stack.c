/*
 * NeolyxOS Emergency Fault Stack
 * 
 * Pre-allocated, identity-mapped stack for last-resort fault handling.
 * Used when primary stack is corrupted.
 * 
 * Features:
 *   - 8KB pre-allocated at compile time
 *   - No dynamic memory allocation
 *   - No logging except serial output
 *   - Identity mapped in page tables
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Emergency stack: 8KB pre-allocated, aligned to 16 bytes */
__attribute__((aligned(16))) static uint8_t g_emergency_stack[8192];

/* Emergency stack pointer (top of stack) */
static uint64_t g_emergency_rsp = 0;

/* Flag: are we on emergency stack? */
static volatile int g_on_emergency_stack = 0;

/* Initialize emergency stack */
void emergency_stack_init(void) {
    /* Stack grows downward, so RSP starts at top */
    g_emergency_rsp = (uint64_t)&g_emergency_stack[8192 - 16];
    
    serial_puts("[EMERG_STACK] Pre-allocated 8KB emergency stack at ");
    
    /* Print address using serial_putc */
    uint64_t addr = (uint64_t)g_emergency_stack;
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(addr >> i) & 0xF]);
    }
    serial_puts("\n");
    
    serial_puts("[EMERG_STACK] Ready for stack corruption recovery\n");
}

/* Get emergency stack pointer for TSS */
uint64_t emergency_stack_get_rsp(void) {
    return g_emergency_rsp;
}

/* Get emergency stack base (for page mapping) */
uint64_t emergency_stack_get_base(void) {
    return (uint64_t)g_emergency_stack;
}

/* Get emergency stack size */
uint64_t emergency_stack_get_size(void) {
    return 8192;
}

/* Called when switching to emergency stack */
void emergency_stack_enter(void) {
    g_on_emergency_stack = 1;
    serial_puts("[EMERG_STACK] SWITCHED TO EMERGENCY STACK\n");
}

/* Check if on emergency stack */
int emergency_stack_active(void) {
    return g_on_emergency_stack;
}
