/*
 * nxlibc host test: printf family (formatting, truncation, errors).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "libc_test.h"

static char g_buf[2048];

#define E(want, ...) do { \
    int n_ = snprintf(g_buf, sizeof g_buf, __VA_ARGS__); \
    t_count++; \
    if (strcmp(g_buf, want) != 0 || n_ != (int)strlen(want)) { \
        t_fail++; \
        printf("  FAIL line %d: %s -> \"%s\" (%d), want \"%s\"\n", __LINE__, #__VA_ARGS__, g_buf, n_, want); \
    } \
} while (0);

static void test_vectors(void) {
#include "libc_printf_expected.h"
}

/* ISO C leaves the leading hex digit of a subnormal unspecified; nxlibc
 * normalizes every finite nonzero value (leading digit 1) */
static void test_hex_subnormal(void) {
    snprintf(g_buf, sizeof g_buf, "%a", 4.9406564584124654e-324);
    CHECK_STR(g_buf, "0x1p-1074");
    snprintf(g_buf, sizeof g_buf, "%a", 2.2250738585072009e-308);
    CHECK_STR(g_buf, "0x1.ffffffffffffep-1023");
}

static void test_truncation(void) {
    char b[8];
    memset(b, 'X', sizeof b);
    CHECK_INT(snprintf(b, 5, "%s", "abcdefgh"), 8);
    CHECK_STR(b, "abcd");
    CHECK(b[5] == 'X');
    CHECK_INT(snprintf(NULL, 0, "%d", 12345), 5);
    CHECK_INT(snprintf(b, 1, "abc"), 3);
    CHECK_STR(b, "");
    CHECK_INT(snprintf(b, sizeof b, "%.3f", 1.0), 5);
    CHECK_STR(b, "1.000");
    char big[64];
    CHECK_INT(sprintf(big, "%s-%d", "x", 9), 3);
    CHECK_STR(big, "x-9");
}

static void test_errors(void) {
    char b[32];
    int n = 0;
    errno = 0;
    CHECK_INT(snprintf(b, sizeof b, "ab%n", &n), -1);
    CHECK_INT(errno, EINVAL);
    CHECK_INT(n, 0);
    errno = 0;
    CHECK_INT(snprintf(b, sizeof b, "%y"), -1);
    CHECK_INT(errno, EINVAL);
    CHECK_STR(snprintf(b, sizeof b, "%p", (void *)0) > 0 ? b : "", "0x0");
    snprintf(b, sizeof b, "%p", (void *)0x1234);
    CHECK_STR(b, "0x1234");
    snprintf(b, sizeof b, "%s", (char *)0);
    CHECK_STR(b, "(null)");
    errno = 0;
    CHECK_INT(snprintf(b, sizeof b, "%.200000f", 1.0), -1);
    CHECK_INT(errno, EOVERFLOW);
}

static void test_long_output(void) {
    /* longer than any internal buffer */
    static char big[5000];
    int n = snprintf(big, sizeof big, "%4000d|", 1);
    CHECK_INT(n, 4001);
    CHECK(big[3998] == ' ' && big[3999] == '1' && big[4000] == '|' && big[4001] == 0);
    n = snprintf(big, sizeof big, "%.400f", 1e-300);
    CHECK_INT(n, 402);
    CHECK(big[0] == '0' && big[1] == '.' && big[301] == '1');
}

int main(void) {
    test_vectors();
    test_hex_subnormal();
    test_truncation();
    test_errors();
    test_long_output();
    return t_finish("libc_printf");
}
