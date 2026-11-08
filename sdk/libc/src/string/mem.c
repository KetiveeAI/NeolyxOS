/*
 * nxlibc - memory functions of <string.h>.
 *
 * memcpy, memmove and memset use the x86 string instructions (REP MOVSB /
 * STOSB), which current CPUs run at full cache bandwidth. This file must be
 * compiled with -fno-builtin -fno-tree-loop-distribute-patterns so the
 * compiler never turns a loop here into a call to the function itself.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdint.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) {
        /* No harmful overlap: forward copy */
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
        return dst;
    }
    /* dst overlaps the end of src: copy backwards with the direction flag */
    d += n - 1;
    s += n - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"((unsigned char)c) : "memory");
    return ret;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    /* Compare 8 bytes at a time while they are equal */
    while (n >= 8) {
        uint64_t wx, wy;
        __builtin_memcpy(&wx, x, 8);
        __builtin_memcpy(&wy, y, 8);
        if (wx != wy) break;
        x += 8;
        y += 8;
        n -= 8;
    }
    for (; n > 0; n--, x++, y++) {
        if (*x != *y) return (int)*x - (int)*y;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;
    for (; n > 0; n--, p++) {
        if (*p == ch) return (void *)p;
    }
    return NULL;
}

void *memccpy(void *restrict dst, const void *restrict src, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char ch = (unsigned char)c;
    for (; n > 0; n--) {
        unsigned char v = *s++;
        *d++ = v;
        if (v == ch) return d;
    }
    return NULL;
}
