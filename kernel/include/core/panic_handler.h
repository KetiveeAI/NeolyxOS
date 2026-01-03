/*
 * NeolyxOS Panic Handler
 * 
 * Crash recovery, stack guards, and kernel assertions.
 * Prevents system crashes and aids debugging.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_PANIC_HANDLER_H
#define NEOLYX_PANIC_HANDLER_H

#include <stdint.h>

/* ============ Panic Types ============ */

typedef enum {
    PANIC_UNKNOWN = 0,
    PANIC_ASSERT,           /* Assertion failure */
    PANIC_NULL_PTR,         /* Null pointer dereference */
    PANIC_STACK_OVERFLOW,   /* Stack overflow detected */
    PANIC_HEAP_CORRUPT,     /* Heap corruption */
    PANIC_DOUBLE_FREE,      /* Double free detected */
    PANIC_DIV_ZERO,         /* Division by zero */
    PANIC_INVALID_OPCODE,   /* Invalid instruction */
    PANIC_PAGE_FAULT,       /* Unhandled page fault */
    PANIC_GPF,              /* General protection fault */
    PANIC_OUT_OF_MEMORY,    /* OOM condition */
} panic_type_t;

/* ============ Stack Frame ============ */

typedef struct {
    uint64_t rip;   /* Instruction pointer */
    uint64_t rbp;   /* Base pointer */
    uint64_t rsp;   /* Stack pointer */
    uint64_t rax;   /* Registers */
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
} panic_frame_t;

/* ============ Crash Handler Callback ============ */

typedef void (*crash_handler_t)(panic_type_t type, const char *msg, panic_frame_t *frame);

/* ============ Panic API ============ */

/**
 * panic_init - Initialize panic handler
 * 
 * Returns: 0 on success
 */
int panic_init(void);

/**
 * kernel_panic_ex - Trigger kernel panic with full details
 * 
 * @type: Type of panic
 * @file: Source file
 * @line: Line number
 * @msg: Error message
 * 
 * Does not return.
 */
void kernel_panic_ex(panic_type_t type, const char *file, int line, const char *msg)
    __attribute__((noreturn));

/**
 * kernel_panic - Simple kernel panic (legacy interface)
 * 
 * @msg: Error message
 * 
 * Does not return.
 */
void kernel_panic(const char *msg)
    __attribute__((noreturn));

/**
 * kernel_assert - Assert condition with panic on failure
 * 
 * @cond: Condition to check
 * @file: Source file
 * @line: Line number
 * @expr: Expression string
 */
void kernel_assert(int cond, const char *file, int line, const char *expr);

/**
 * register_crash_handler - Register crash handler callback
 * 
 * @handler: Handler function
 * 
 * Returns: 0 on success
 */
int register_crash_handler(crash_handler_t handler);

/* ============ Stack Guard ============ */

/**
 * stack_guard_init - Initialize stack canary
 * 
 * @stack_top: Top of stack
 * @size: Stack size
 */
void stack_guard_init(void *stack_top, uint64_t size);

/**
 * stack_guard_check - Check stack canary
 * 
 * Returns: 0 if intact, -1 if corrupted (and panics)
 */
int stack_guard_check(void);

/* ============ Debug Macros ============ */

#define PANIC(msg) \
    kernel_panic_ex(PANIC_UNKNOWN, __FILE__, __LINE__, msg)

#define PANIC_TYPE(type, msg) \
    kernel_panic_ex(type, __FILE__, __LINE__, msg)

#define ASSERT(cond) \
    kernel_assert(!!(cond), __FILE__, __LINE__, #cond)

#define ASSERT_NOT_NULL(ptr) \
    do { if (!(ptr)) kernel_panic_ex(PANIC_NULL_PTR, __FILE__, __LINE__, #ptr); } while(0)

#define ASSERT_BOUNDS(idx, max) \
    do { if ((idx) >= (max)) kernel_panic_ex(PANIC_ASSERT, __FILE__, __LINE__, "bounds"); } while(0)

/* ============ Safe Division ============ */

static inline int64_t safe_div(int64_t a, int64_t b, const char *file, int line) {
    if (b == 0) {
        kernel_panic_ex(PANIC_DIV_ZERO, file, line, "Division by zero");
    }
    return a / b;
}

#define SAFE_DIV(a, b) safe_div((a), (b), __FILE__, __LINE__)

#endif /* NEOLYX_PANIC_HANDLER_H */
