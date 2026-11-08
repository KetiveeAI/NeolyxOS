/*
 * nxlibc host tests: tiny check framework. Uses only nxlibc itself
 * (printf to stdout), so it runs the same on the host shim and on
 * NeolyxOS.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_TEST_H
#define NXLIBC_TEST_H

#include <stdio.h>
#include <string.h>

static int t_fail;
static int t_count;

#define CHECK(cond) do { \
    t_count++; \
    if (!(cond)) { t_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(got, want) do { \
    const char *g_ = (got), *w_ = (want); \
    t_count++; \
    if (!g_ || strcmp(g_, w_) != 0) { \
        t_fail++; \
        printf("  FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, g_ ? g_ : "(null)", w_); \
    } \
} while (0)

#define CHECK_INT(got, want) do { \
    long long g_ = (long long)(got), w_ = (long long)(want); \
    t_count++; \
    if (g_ != w_) { \
        t_fail++; \
        printf("  FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #got, g_, w_); \
    } \
} while (0)

static inline int t_finish(const char *name) {
    printf("[%s] %d checks, %d failed: %s\n", name, t_count, t_fail, t_fail ? "FAIL" : "PASS");
    fflush(stdout);
    return t_fail ? 1 : 0;
}

#endif
