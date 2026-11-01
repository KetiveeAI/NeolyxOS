/*
 * nxlibc host test: strtod / strtof / atof, correctly rounded.
 * Expected values are hexadecimal literals (exact) so the compiler's own
 * decimal conversion is not part of the test.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "libc_test.h"

static uint64_t bits_d(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static uint32_t bits_f(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

#define D(str, want) do { \
    errno = 0; \
    double got_ = strtod(str, NULL); \
    t_count++; \
    if (bits_d(got_) != bits_d(want)) { \
        t_fail++; \
        printf("  FAIL line %d: strtod(\"%s\") = %a, want %a\n", __LINE__, str, got_, (double)(want)); \
    } \
} while (0)

#define F(str, want) do { \
    float got_ = strtof(str, NULL); \
    t_count++; \
    if (bits_f(got_) != bits_f(want)) { \
        t_fail++; \
        printf("  FAIL line %d: strtof(\"%s\") = %a, want %a\n", __LINE__, str, (double)got_, (double)(want)); \
    } \
} while (0)

static void test_exact_cases(void) {
    D("0", 0.0);
    D("-0", -0.0);
    D("1", 1.0);
    D("-1.5", -1.5);
    D("0.1", 0x1.999999999999ap-4);
    D("0.2", 0x1.999999999999ap-3);
    D("0.3", 0x1.3333333333333p-2);
    D("3.141592653589793", 0x1.921fb54442d18p+1);
    D("1e23", 0x1.52d02c7e14af6p+76);
    D("8.98846567431158e307", 0x1.0000000000000p+1023);
    D("1.7976931348623157e308", 0x1.fffffffffffffp+1023);
    D("1.7976931348623158e308", 0x1.fffffffffffffp+1023);
    D("2.2250738585072011e-308", 0x0.fffffffffffffp-1022);
    D("2.2250738585072014e-308", 0x1p-1022);
    D("4.9406564584124654e-324", 0x0.0000000000001p-1022);
    D("2.4703282292062328e-324", 0x0.0000000000001p-1022);  /* just above half */
    D("2.4703282292062327e-324", 0.0);                       /* just below half */
    D("9007199254740993", 0x1p+53);                          /* tie: to even */
    D("9007199254740993.0000000000000000000000001", 0x1.0000000000001p+53);
    D("9007199254740995", 0x1.0000000000002p+53);            /* tie: to even (up) */
    D("0.1000000000000000055511151231257827021181583404541015625", 0x1.999999999999ap-4);
    D("123456789012345678901234567890", 0x1.8ee90ff6c373ep+96);
    D("1.00000000000000011102230246251565404236316680908203125", 0x1p+0);  /* exact tie */
    D("1.00000000000000011102230246251565404236316680908203126", 0x1.0000000000001p+0);
    D("  +42.", 42.0);
    D(".5", 0.5);
    D("5e-1", 0.5);
    D("5E+1", 50.0);
    D("1e-400", 0.0);
    D("0x1p-3", 0.125);
    D("-0X1.8P1", -3.0);
    D("0x.8", 0.5);
    D("0x1.fffffffffffff8p0", 2.0);                          /* hex tie to even */
    D("0x1.fffffffffffff7ffp0", 0x1.fffffffffffffp0);
    D("0x1p-1074", 0x0.0000000000001p-1022);
    D("0x1p-1075", 0.0);
    D("0x1.0000000000001p-1075", 0x0.0000000000001p-1022);
}

static void test_special(void) {
    char *end;
    double d = strtod("inf", &end);
    CHECK(isinf(d) && d > 0 && *end == 0);
    d = strtod("-Infinity!", &end);
    CHECK(isinf(d) && d < 0 && *end == '!');
    d = strtod("infx", &end);
    CHECK(isinf(d) && strcmp(end, "x") == 0);
    d = strtod("NaN", &end);
    CHECK(isnan(d) && *end == 0);
    d = strtod("nan(123)z", &end);
    CHECK(isnan(d) && *end == 'z');

    /* nothing to convert */
    const char *s = "  abc";
    d = strtod(s, &end);
    CHECK(d == 0.0 && end == s);
    s = "-.";
    d = strtod(s, &end);
    CHECK(d == 0.0 && end == s);
    /* partial forms */
    d = strtod("1e", &end);
    CHECK(d == 1.0 && *end == 'e');
    d = strtod("1e+", &end);
    CHECK(d == 1.0 && *end == 'e');
    d = strtod("0x", &end);
    CHECK(d == 0.0 && *end == 'x');
    d = strtod("0xg", &end);
    CHECK(d == 0.0 && strcmp(end, "xg") == 0);

    /* range errors */
    errno = 0;
    d = strtod("1e309", NULL);
    CHECK(isinf(d) && errno == ERANGE);
    errno = 0;
    d = strtod("-1.7976931348623159e308", NULL);
    CHECK(isinf(d) && d < 0 && errno == ERANGE);
    errno = 0;
    d = strtod("1e-400", NULL);
    CHECK(d == 0.0 && errno == ERANGE);
    errno = 0;
    d = strtod("0e-400", NULL);
    CHECK(d == 0.0 && errno == 0);
    errno = 0;
    d = strtod("1e-310", NULL);         /* subnormal but representable */
    CHECK(d > 0 && errno == 0);
    errno = 0;
    d = strtod("1e99999999999999999999", NULL);
    CHECK(isinf(d) && errno == ERANGE);
    errno = 0;
    d = strtod("0.0000000000000000000000000000001e-99999999999999999999", NULL);
    CHECK(d == 0.0 && errno == ERANGE);
    CHECK(atof("2.5xyz") == 2.5);

    /* very long input: 1 followed by 1000 zeros then 1, scaled down */
    static char big[1100];
    big[0] = '0'; big[1] = '.';
    memset(big + 2, '0', 1000);
    big[1002] = '1';
    strcpy(big + 1003, "e1001");
    d = strtod(big, &end);
    CHECK(d == 1.0 && *end == 0);
}

static void test_float(void) {
    F("0.1", 0x1.99999ap-4f);
    F("3.4028235e38", 0x1.fffffep+127f);
    F("3.4028235677973365e38", 0x1.fffffep+127f);  /* just below the overflow tie */
    F("3.4028235677973367e38", __builtin_inff());  /* just above it */
    F("1.17549435e-38", 0x1p-126f);
    F("1.4e-45", 0x1p-149f);
    F("7e-46", 0.0f);                    /* below half of the minimum */
    F("16777217", 0x1p+24f);             /* tie: to even */
    F("16777219", 0x1.000004p+24f);      /* tie: to even (up) */
    F("0x1.fffffep127", 0x1.fffffep+127f);
    errno = 0;
    float f = strtof("3.5e38", NULL);
    CHECK(isinf(f) && errno == ERANGE);
    long double ld = strtold("2.5", NULL);
    CHECK(ld == 2.5L);
}

/* xorshift64*: deterministic test values */
static uint64_t g_seed = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_seed ^= g_seed >> 12;
    g_seed ^= g_seed << 25;
    g_seed ^= g_seed >> 27;
    return g_seed * 0x2545F4914F6CDD1Dull;
}

static void test_round_trip(void) {
    char buf[64];
    int bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t u = rnd();
        double d;
        memcpy(&d, &u, 8);
        if (isnan(d) || isinf(d)) continue;
        snprintf(buf, sizeof buf, (i & 1) ? "%.17g" : "%.16e", d);
        double back = strtod(buf, NULL);
        if (bits_d(back) != u) {
            if (bad++ < 5) printf("  FAIL round trip %a -> \"%s\" -> %a\n", d, buf, back);
        }
        /* shortest-ish forms must parse to the nearest value too */
        float f = (float)d;
        if (!isinf(f) && !isnan(f)) {
            snprintf(buf, sizeof buf, "%.9g", (double)f);
            float fb = strtof(buf, NULL);
            if (bits_f(fb) != bits_f(f)) {
                if (bad++ < 5) printf("  FAIL float round trip %a -> \"%s\" -> %a\n", (double)f, buf, (double)fb);
            }
        }
    }
    t_count++;
    if (bad) { t_fail++; printf("  FAIL: %d round-trip errors\n", bad); }
}

int main(void) {
    test_exact_cases();
    test_special();
    test_float();
    test_round_trip();
    return t_finish("libc_strtod");
}
