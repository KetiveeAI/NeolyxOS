/*
 * nxlibc - string functions of <string.h>.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include "../internal.h"

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

char *strcpy(char *restrict dst, const char *restrict src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

char *strncpy(char *restrict dst, const char *restrict src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *restrict dst, const char *restrict src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *restrict dst, const char *restrict src, size_t n) {
    char *d = dst + strlen(dst);
    size_t i = 0;
    for (; i < n && src[i]; i++) d[i] = src[i];
    d[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (*x && *x == *y) {
        x++;
        y++;
    }
    return (int)*x - (int)*y;
}

int strncmp(const char *a, const char *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (; n > 0; n--, x++, y++) {
        if (*x != *y) return (int)*x - (int)*y;
        if (*x == '\0') return 0;
    }
    return 0;
}

int strcoll(const char *a, const char *b) {
    return strcmp(a, b);
}

size_t strxfrm(char *restrict dst, const char *restrict src, size_t n) {
    /* "C" locale: the transformed string is the string itself */
    size_t len = strlen(src);
    if (n > 0) {
        size_t c = (len < n - 1) ? len : n - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return len;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (;; s++) {
        if (*s == ch) return (char *)s;
        if (*s == '\0') return NULL;
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    for (;; s++) {
        if (*s == ch) last = s;
        if (*s == '\0') return (char *)last;
    }
}

/* 256-bit set of the bytes in a NUL-terminated string */
typedef struct { uint64_t w[4]; } byteset_t;

static void set_build(byteset_t *set, const char *chars) {
    set->w[0] = set->w[1] = set->w[2] = set->w[3] = 0;
    for (const unsigned char *p = (const unsigned char *)chars; *p; p++) {
        set->w[*p >> 6] |= 1ULL << (*p & 63);
    }
}

static int set_has(const byteset_t *set, unsigned char c) {
    return (int)((set->w[c >> 6] >> (c & 63)) & 1);
}

size_t strspn(const char *s, const char *accept) {
    byteset_t set;
    set_build(&set, accept);
    size_t n = 0;
    while (s[n] && set_has(&set, (unsigned char)s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    byteset_t set;
    set_build(&set, reject);
    size_t n = 0;
    while (s[n] && !set_has(&set, (unsigned char)s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *accept) {
    s += strcspn(s, accept);
    return *s ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char *)haystack;
    char first = needle[0];
    for (const char *h = haystack; (h = strchr(h, first)) != NULL; h++) {
        if (strncmp(h, needle, nlen) == 0) return (char *)h;
    }
    return NULL;
}

char *strtok_r(char *restrict s, const char *restrict delim, char **restrict save) {
    if (!save) return NULL;
    if (!s) s = *save;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (*s == '\0') {
        *save = NULL;
        return NULL;
    }
    char *end = s + strcspn(s, delim);
    if (*end) {
        *end = '\0';
        *save = end + 1;
    } else {
        *save = NULL;
    }
    return s;
}

char *strtok(char *restrict s, const char *restrict delim) {
    static char *state;
    return strtok_r(s, delim, &state);
}

char *strdup(const char *s) {
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

size_t __nxc_strlcpy(char *dst, const char *src, size_t cap) {
    size_t len = strlen(src);
    if (cap > 0) {
        size_t c = (len < cap - 1) ? len : cap - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return len;
}
