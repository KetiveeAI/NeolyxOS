/*
 * NeolyxOS REOX SDK - minimal memory/string primitives for freestanding apps
 *
 * desktop/lib/libc_stubs.c supplies malloc/free/calloc/realloc/strlen/strcpy/
 * strncpy/strcmp and a few math helpers.  nxrender widgets additionally need
 * the symbols below.  All functions validate their arguments: a NULL pointer
 * with a non-zero length is treated as a no-op rather than a fault, so a bad
 * caller cannot take the whole process down through this file.
 *
 * Built with -fno-builtin -fno-tree-loop-distribute-patterns so GCC does not
 * turn these loops back into calls to themselves.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stddef.h>
#include <stdint.h>
#include "nxsyscall.h"

extern void* malloc(size_t size);
extern size_t strlen(const char* s);

void* memset(void* dst, int value, size_t n) {
    if (!dst) return dst;
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    while (n--) *d++ = v;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    if (!dst || !src) return dst;
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    if (!dst || !src || dst == src) return dst;
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

char* strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

int strncmp(const char* a, const char* b, size_t n) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
    }
    return 0;
}

char* strchr(const char* s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

/* GCC may emit this for stack protection even with -fno-stack-protector in
 * code pulled from prebuilt objects; fail loudly instead of returning. */
void __stack_chk_fail(void) {
    nx_debug_print("[REOX] stack smashing detected; halting\n");
    for (;;) {
        __asm__ volatile("pause");
    }
}
