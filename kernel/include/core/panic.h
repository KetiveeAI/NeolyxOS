/*
 * NeolyxOS Kernel Panic
 * 
 * Fatal error handling - halts system with diagnostic info.
 * Always fail loud - silent failures are nightmares.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_PANIC_H
#define NEOLYX_PANIC_H

#include <stdint.h>

/**
 * Kernel panic - unrecoverable error.
 * 
 * @param message Error message
 * @param code    Error code or additional info
 * 
 * This function never returns.
 */
void kernel_panic(const char *message, uint64_t code) __attribute__((noreturn));

/**
 * Assertion failure.
 * 
 * @param expr Expression that failed
 * @param file Source file
 * @param line Line number
 */
void kernel_assert_fail(const char *expr, const char *file, int line) __attribute__((noreturn));

/* Assertion macro */
#define KERNEL_ASSERT(expr) \
    do { if (!(expr)) kernel_assert_fail(#expr, __FILE__, __LINE__); } while (0)

#endif /* NEOLYX_PANIC_H */
