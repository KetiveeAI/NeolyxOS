/*
 * nxlibc - integer conversion: strtol family, ato*, strtoimax/strtoumax.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/*
 * Parses an integer as ISO C 7.22.1.4 describes. The magnitude is
 * accumulated as unsigned 64-bit and clamped at `limit`; *neg reports a
 * leading '-'. Returns the magnitude; sets *overflow when it exceeded limit.
 * *end is the first unparsed character (s itself when nothing was parsed).
 */
static uint64_t parse_magnitude(const char *s, char **end, int base, uint64_t limit,
                                int *neg, int *overflow) {
    const char *p = s;
    *neg = 0;
    *overflow = 0;
    if (end) *end = (char *)s;
    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        return 0;
    }
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') {
        *neg = (*p == '-');
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_value((unsigned char)p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    const char *digits = p;
    uint64_t v = 0;
    for (;; p++) {
        int d = digit_value((unsigned char)*p);
        if (d >= base) break;
        if (!*overflow) {
            if (v > (limit - (uint64_t)d) / (uint64_t)base) {
                *overflow = 1;
            } else {
                v = v * (uint64_t)base + (uint64_t)d;
            }
        }
    }
    if (p == digits) {
        /* No digits: a bare "0x" still parsed the 0 */
        return 0;
    }
    if (end) *end = (char *)p;
    return v;
}

long long strtoll(const char *restrict s, char **restrict end, int base) {
    int neg, ovf;
    uint64_t limit = (uint64_t)LLONG_MAX + 1u;    /* magnitude of LLONG_MIN */
    uint64_t m = parse_magnitude(s, end, base, limit, &neg, &ovf);
    if (ovf || (!neg && m > (uint64_t)LLONG_MAX)) {
        errno = ERANGE;
        return neg ? LLONG_MIN : LLONG_MAX;
    }
    if (neg) return (m == limit) ? LLONG_MIN : -(long long)m;
    return (long long)m;
}

long strtol(const char *restrict s, char **restrict end, int base) {
    return (long)strtoll(s, end, base);     /* long is 64-bit (LP64) */
}

unsigned long long strtoull(const char *restrict s, char **restrict end, int base) {
    int neg, ovf;
    uint64_t m = parse_magnitude(s, end, base, UINT64_MAX, &neg, &ovf);
    if (ovf) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    /* ISO C: a '-' sign negates the value in the unsigned type */
    return neg ? (unsigned long long)(0u - m) : (unsigned long long)m;
}

unsigned long strtoul(const char *restrict s, char **restrict end, int base) {
    return (unsigned long)strtoull(s, end, base);
}

intmax_t strtoimax(const char *restrict s, char **restrict end, int base) {
    return (intmax_t)strtoll(s, end, base);
}

uintmax_t strtoumax(const char *restrict s, char **restrict end, int base) {
    return (uintmax_t)strtoull(s, end, base);
}

/* ato*: like strto* in base 10; out-of-range input gives the clamped value */
int atoi(const char *s) {
    long long v = strtoll(s, NULL, 10);
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return (int)v;
}

long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }
double atof(const char *s) { return strtod(s, NULL); }
