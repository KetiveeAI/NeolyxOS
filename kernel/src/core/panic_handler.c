/*
 * NeolyxOS Panic Handler Implementation
 * 
 * Crash recovery, state dump, and stack guards.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/core/panic_handler.h"
#include "../include/core/security.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t get_current_pid(void);

/* Halt system (from stubs or arch) */
static void halt_cpu(void) {
    __asm__ volatile("cli");  /* Disable interrupts */
    while (1) {
        __asm__ volatile("hlt");  /* Halt CPU */
    }
}

/* ============ Static State ============ */

static uint8_t g_panic_initialized = 0;
static crash_handler_t g_crash_handler = NULL;

/* Stack guard canary */
#define STACK_CANARY_MAGIC 0xDEADBEEFCAFEBABEULL
static uint64_t *g_stack_canary_ptr = NULL;

/* Panic names for debugging */
static const char *g_panic_names[] = {
    "UNKNOWN",
    "ASSERTION_FAILURE",
    "NULL_POINTER",
    "STACK_OVERFLOW",
    "HEAP_CORRUPTION",
    "DOUBLE_FREE",
    "DIVISION_BY_ZERO",
    "INVALID_OPCODE",
    "PAGE_FAULT",
    "GPF",
    "OUT_OF_MEMORY",
};

/* ============ Utility Functions ============ */

static void print_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void print_dec(int val) {
    if (val < 0) {
        serial_putc('-');
        val = -val;
    }
    char buf[12];
    int i = 0;
    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ Panic Handler ============ */

int panic_init(void) {
    if (g_panic_initialized) return 0;
    
    serial_puts("[PANIC] Panic handler initialized\n");
    g_panic_initialized = 1;
    return 0;
}

__attribute__((noreturn))
void kernel_panic_ex(panic_type_t type, const char *file, int line, const char *msg) {
    /* Disable interrupts immediately */
    __asm__ volatile("cli");
    
    /* Log to audit system if available */
    security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "KERNEL PANIC");
    
    /* Print panic banner */
    serial_puts("\n");
    serial_puts("╔══════════════════════════════════════════════════════════╗\n");
    serial_puts("║                    KERNEL PANIC                          ║\n");
    serial_puts("╚══════════════════════════════════════════════════════════╝\n");
    serial_puts("\n");
    
    /* Print panic type */
    serial_puts("Type: ");
    if (type <= PANIC_OUT_OF_MEMORY) {
        serial_puts(g_panic_names[type]);
    } else {
        serial_puts("UNKNOWN");
    }
    serial_puts("\n");
    
    /* Print location */
    serial_puts("Location: ");
    if (file) {
        serial_puts(file);
        serial_puts(":");
        print_dec(line);
    } else {
        serial_puts("(unknown)");
    }
    serial_puts("\n");
    
    /* Print message */
    serial_puts("Message: ");
    if (msg) {
        serial_puts(msg);
    } else {
        serial_puts("(no message)");
    }
    serial_puts("\n\n");
    
    /* Print current PID */
    serial_puts("PID: ");
    print_dec(get_current_pid());
    serial_puts("\n");
    
    /* Get and print stack pointer */
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    serial_puts("RSP: ");
    print_hex64(rsp);
    serial_puts("\n");
    
    /* Call registered crash handler if any */
    if (g_crash_handler) {
        panic_frame_t frame = {0};
        frame.rsp = rsp;
        g_crash_handler(type, msg, &frame);
    }
    
    serial_puts("\n");
    serial_puts("System halted. Please restart manually.\n");
    serial_puts("Press RESET or power cycle the system.\n");
    
    /* Halt forever */
    halt_cpu();
    
    /* Never reached */
    __builtin_unreachable();
}

/* Simple legacy wrapper for compatibility */
__attribute__((noreturn))
void kernel_panic(const char *msg) {
    kernel_panic_ex(PANIC_UNKNOWN, NULL, 0, msg);
}

void kernel_assert(int cond, const char *file, int line, const char *expr) {
    if (!cond) {
        serial_puts("\n[ASSERT] Assertion failed: ");
        if (expr) serial_puts(expr);
        serial_puts("\n");
        
        kernel_panic_ex(PANIC_ASSERT, file, line, expr);
    }
}

int register_crash_handler(crash_handler_t handler) {
    g_crash_handler = handler;
    serial_puts("[PANIC] Crash handler registered\n");
    return 0;
}

/* ============ Stack Guard ============ */

void stack_guard_init(void *stack_top, uint64_t size) {
    (void)size;
    
    if (!stack_top) return;
    
    /* Place canary at bottom of stack */
    g_stack_canary_ptr = (uint64_t *)stack_top;
    *g_stack_canary_ptr = STACK_CANARY_MAGIC;
    
    serial_puts("[PANIC] Stack guard initialized at ");
    print_hex64((uint64_t)stack_top);
    serial_puts("\n");
}

int stack_guard_check(void) {
    if (!g_stack_canary_ptr) {
        return 0;  /* Not initialized */
    }
    
    if (*g_stack_canary_ptr != STACK_CANARY_MAGIC) {
        kernel_panic_ex(PANIC_STACK_OVERFLOW, __FILE__, __LINE__, 
                    "Stack canary corrupted");
    }
    
    return 0;
}
