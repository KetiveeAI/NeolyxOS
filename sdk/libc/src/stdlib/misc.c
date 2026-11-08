/*
 * nxlibc - integer helpers, pseudo-random numbers, environment, system().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "../internal.h"

/* abs(INT_MIN) is undefined in ISO C; here it returns INT_MIN unchanged
 * (two's complement) instead of trapping. */
int abs(int x) { return (x < 0) ? (int)(0u - (unsigned int)x) : x; }
long labs(long x) { return (x < 0) ? (long)(0ul - (unsigned long)x) : x; }
long long llabs(long long x) { return (x < 0) ? (long long)(0ull - (unsigned long long)x) : x; }
intmax_t imaxabs(intmax_t x) { return (x < 0) ? (intmax_t)(0u - (uintmax_t)x) : x; }

div_t div(int num, int den) {
    div_t r = { num / den, num % den };
    return r;
}
ldiv_t ldiv(long num, long den) {
    ldiv_t r = { num / den, num % den };
    return r;
}
lldiv_t lldiv(long long num, long long den) {
    lldiv_t r = { num / den, num % den };
    return r;
}
imaxdiv_t imaxdiv(intmax_t num, intmax_t den) {
    imaxdiv_t r = { num / den, num % den };
    return r;
}

/*
 * rand(): a 64-bit linear congruential generator (multiplier and increment
 * from Knuth's MMIX), returning the high 31 bits, which have the longest
 * period. Deterministic: srand(s) always replays the same sequence; the
 * initial seed is 1, as ISO C requires. Not for cryptography (stream S6
 * provides kernel randomness).
 */
static uint64_t g_rand_state = 1;

void srand(unsigned int seed) {
    g_rand_state = seed;
}

int rand(void) {
    g_rand_state = g_rand_state * 6364136223846793005ull + 1442695040888963407ull;
    return (int)(g_rand_state >> 33);
}

/* ============ Environment ============ */

char **__nxc_environ;

char *getenv(const char *name) {
    if (!name || !*name || strchr(name, '=') || !__nxc_environ) return NULL;
    size_t len = strlen(name);
    for (char **e = __nxc_environ; *e; e++) {
        if (strncmp(*e, name, len) == 0 && (*e)[len] == '=') return *e + len + 1;
    }
    return NULL;
}

/* No command processor exists in this version (the userspace shell is
 * stream S5): system(NULL) reports that, any command fails with ENOSYS. */
int system(const char *command) {
    if (!command) return 0;
    errno = ENOSYS;
    return -1;
}
