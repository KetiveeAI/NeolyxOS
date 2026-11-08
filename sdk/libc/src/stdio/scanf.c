/*
 * nxlibc - the scanf family.
 *
 * Supported: assignment suppression '*', field widths, length modifiers
 * hh h l ll j z t L, conversions d i u o x X a A e E f F g G c s [ p n %.
 * Numbers are collected as text (at most 400 characters per field) and
 * converted with strtoll/strtoull/strtod, so scanf and strto* always agree.
 * Wide-character forms (%lc, %ls, %l[) are not supported; they end the
 * scan as a matching failure.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "../internal.h"

#define FIELD_MAX 400

typedef struct {
    FILE       *f;          /* stream source, or NULL */
    const char *s;          /* string source */
    size_t      pos;
    long        count;      /* characters consumed (%n) */
} src_t;

static int src_get(src_t *in) {
    int c;
    if (in->f) {
        c = __nxc_stream_getc(in->f);
    } else {
        c = in->s[in->pos] ? (unsigned char)in->s[in->pos] : EOF;
        if (c != EOF) in->pos++;
    }
    if (c != EOF) in->count++;
    return c;
}

static void src_unget(src_t *in, int c) {
    if (c == EOF) return;
    in->count--;
    if (in->f) __nxc_stream_ungetc(c, in->f);
    else in->pos--;
}

typedef enum { L_NONE, L_HH, L_H, L_L, L_LL, L_J, L_Z, L_T, L_BIGL } len_t;

static void store_int(void *dst, len_t len, uintmax_t v) {
    switch (len) {
    case L_HH: *(unsigned char *)dst = (unsigned char)v; break;
    case L_H:  *(unsigned short *)dst = (unsigned short)v; break;
    case L_L:  *(unsigned long *)dst = (unsigned long)v; break;
    case L_LL: *(unsigned long long *)dst = (unsigned long long)v; break;
    case L_J:  *(uintmax_t *)dst = v; break;
    case L_Z:  *(size_t *)dst = (size_t)v; break;
    case L_T:  *(ptrdiff_t *)dst = (ptrdiff_t)v; break;
    default:   *(unsigned int *)dst = (unsigned int)v; break;
    }
}

/* Collects an integer field; returns length (0 = matching failure) */
static int collect_int(src_t *in, char *buf, int width, int base) {
    int n = 0;
    int c = src_get(in);
    if ((c == '+' || c == '-') && n < width) {
        buf[n++] = (char)c;
        c = src_get(in);
    }
    if ((base == 0 || base == 16) && c == '0' && n < width) {
        buf[n++] = '0';
        c = src_get(in);
        if ((c == 'x' || c == 'X') && n < width) {
            buf[n++] = (char)c;
            c = src_get(in);
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    }
    if (base == 0) base = 10;
    while (c != EOF && n < width && n < FIELD_MAX) {
        int ok = (base == 16) ? isxdigit(c) : (base == 8) ? (c >= '0' && c <= '7') : isdigit(c);
        if (!ok) break;
        buf[n++] = (char)c;
        c = src_get(in);
    }
    src_unget(in, c);
    buf[n] = '\0';
    /* need at least one digit: a lone sign or a bare "0x" is not a number */
    int digits = 0;
    for (int i = 0; i < n; i++) {
        if (isxdigit((unsigned char)buf[i]) && !(i == 1 && (buf[i] == 'x' || buf[i] == 'X'))) digits = 1;
    }
    return digits ? n : 0;
}

/* Collects a floating-point field; returns length (0 = matching failure) */
static int collect_float(src_t *in, char *buf, int width) {
    int n = 0;
    int c = src_get(in);
    if ((c == '+' || c == '-') && n < width) {
        buf[n++] = (char)c;
        c = src_get(in);
    }
    /* inf / infinity / nan */
    if (c == 'i' || c == 'I' || c == 'n' || c == 'N') {
        const char *word = (c == 'i' || c == 'I') ? "infinity" : "nan";
        int k = 0;
        while (word[k] && c != EOF && tolower(c) == word[k] && n < width && n < FIELD_MAX) {
            buf[n++] = (char)c;
            k++;
            c = src_get(in);
        }
        src_unget(in, c);
        buf[n] = '\0';
        if (word[0] == 'i') return (k == 3 || k == 8) ? n : 0;
        return (k == 3) ? n : 0;
    }
    int hex = 0, digits = 0;
    if (c == '0' && n < width) {
        buf[n++] = '0';
        digits = 1;
        c = src_get(in);
        if ((c == 'x' || c == 'X') && n < width) {
            buf[n++] = (char)c;
            hex = 1;
            digits = 0;
            c = src_get(in);
        }
    }
    int point = 0;
    while (c != EOF && n < width && n < FIELD_MAX) {
        if (hex ? isxdigit(c) : isdigit(c)) {
            digits = 1;
        } else if (c == '.' && !point) {
            point = 1;
        } else {
            break;
        }
        buf[n++] = (char)c;
        c = src_get(in);
    }
    if (digits && n < width && n < FIELD_MAX &&
        ((!hex && (c == 'e' || c == 'E')) || (hex && (c == 'p' || c == 'P')))) {
        buf[n++] = (char)c;
        c = src_get(in);
        if ((c == '+' || c == '-') && n < width) {
            buf[n++] = (char)c;
            c = src_get(in);
        }
        int expd = 0;
        while (c != EOF && isdigit(c) && n < width && n < FIELD_MAX) {
            buf[n++] = (char)c;
            expd = 1;
            c = src_get(in);
        }
        if (!expd) digits = 0;  /* "1e" is a matching failure in ISO C */
    }
    src_unget(in, c);
    buf[n] = '\0';
    return digits ? n : 0;
}

static int vscan(src_t *in, const char *fmt, va_list ap_in) {
    va_list ap;
    va_copy(ap, ap_in);
    int assigned = 0;
    int converted_any = 0;
    char field[FIELD_MAX + 1];
    const unsigned char *p = (const unsigned char *)fmt;
    int result;

    while (*p) {
        if (isspace(*p)) {
            int c;
            while ((c = src_get(in)) != EOF && isspace(c)) {
            }
            src_unget(in, c);
            p++;
            continue;
        }
        if (*p != '%' || p[1] == '%') {
            if (*p == '%') {
                p++;    /* "%%": skip white space, then match '%' */
                int c0;
                while ((c0 = src_get(in)) != EOF && isspace(c0)) {
                }
                src_unget(in, c0);
            }
            int c = src_get(in);
            if (c != *p) {
                src_unget(in, c);
                result = (c == EOF && !converted_any) ? EOF : assigned;
                goto done;
            }
            p++;
            continue;
        }
        p++;
        int suppress = 0;
        if (*p == '*') {
            suppress = 1;
            p++;
        }
        int width = 0;
        while (isdigit(*p)) {
            if (width < 100000) width = width * 10 + (*p - '0');
            p++;
        }
        len_t len = L_NONE;
        switch (*p) {
        case 'h': len = (p[1] == 'h') ? L_HH : L_H; p += (len == L_HH) ? 2 : 1; break;
        case 'l': len = (p[1] == 'l') ? L_LL : L_L; p += (len == L_LL) ? 2 : 1; break;
        case 'j': len = L_J; p++; break;
        case 'z': len = L_Z; p++; break;
        case 't': len = L_T; p++; break;
        case 'L': len = L_BIGL; p++; break;
        default: break;
        }
        int conv = *p;
        if (conv == '\0') {
            result = assigned;
            goto done;
        }
        p++;

        if (conv == 'n') {
            if (!suppress) store_int(va_arg(ap, void *), len, (uintmax_t)in->count);
            continue;
        }
        if (conv != 'c' && conv != '[') {
            int c;
            while ((c = src_get(in)) != EOF && isspace(c)) {
            }
            if (c == EOF) {
                result = converted_any ? assigned : EOF;
                goto done;
            }
            src_unget(in, c);
        }

        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
            int base = (conv == 'd' || conv == 'u') ? 10 : (conv == 'i') ? 0 : (conv == 'o') ? 8 : 16;
            int n = collect_int(in, field, width ? width : FIELD_MAX, base);
            if (n == 0) {
                result = assigned;
                goto done;
            }
            converted_any = 1;
            if (suppress) break;
            if (conv == 'p') {
                *va_arg(ap, void **) = (void *)(uintptr_t)strtoull(field, NULL, 16);
            } else if (conv == 'd' || conv == 'i') {
                long long v = strtoll(field, NULL, base);
                store_int(va_arg(ap, void *), len, (uintmax_t)v);
            } else {
                unsigned long long v = strtoull(field, NULL, base);
                store_int(va_arg(ap, void *), len, (uintmax_t)v);
            }
            assigned++;
            break;
        }
        case 'a': case 'A': case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
            int n = collect_float(in, field, width ? width : FIELD_MAX);
            if (n == 0) {
                result = assigned;
                goto done;
            }
            converted_any = 1;
            if (suppress) break;
            if (len == L_L) *va_arg(ap, double *) = strtod(field, NULL);
            else if (len == L_BIGL) *va_arg(ap, long double *) = strtold(field, NULL);
            else *va_arg(ap, float *) = strtof(field, NULL);
            assigned++;
            break;
        }
        case 'c': {
            if (len == L_L) {
                result = assigned;
                goto done;
            }
            int w = width ? width : 1;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int got = 0;
            for (; got < w; got++) {
                int c = src_get(in);
                if (c == EOF) break;
                if (dst) dst[got] = (char)c;
            }
            if (got < w) {
                result = (got == 0 && !converted_any) ? EOF : assigned;
                goto done;
            }
            converted_any = 1;
            if (!suppress) assigned++;
            break;
        }
        case 's':
        case '[': {
            if (len == L_L) {
                result = assigned;
                goto done;
            }
            /* scan set: 256 bits */
            uint64_t set[4] = { 0, 0, 0, 0 };
            if (conv == 's') {
                for (int ch = 0; ch < 256; ch++) {
                    if (!isspace(ch)) set[ch >> 6] |= 1ull << (ch & 63);
                }
            } else {
                int invert = 0;
                if (*p == '^') {
                    invert = 1;
                    p++;
                }
                const unsigned char *start = p;
                while (*p && (*p != ']' || p == start)) {
                    if (p[1] == '-' && p[2] && p[2] != ']' && p[2] >= *p) {
                        for (int ch = *p; ch <= p[2]; ch++) set[ch >> 6] |= 1ull << (ch & 63);
                        p += 3;
                    } else {
                        set[*p >> 6] |= 1ull << (*p & 63);
                        p++;
                    }
                }
                if (*p != ']') {
                    result = assigned;      /* unterminated scan set */
                    goto done;
                }
                p++;
                if (invert) {
                    for (int i = 0; i < 4; i++) set[i] = ~set[i];
                }
            }
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int w = width ? width : INT_MAX;
            int got = 0;
            int c = EOF;
            while (got < w) {
                c = src_get(in);
                if (c == EOF || !((set[c >> 6] >> (c & 63)) & 1)) break;
                if (dst) dst[got] = (char)c;
                got++;
                c = EOF;
            }
            src_unget(in, c);
            if (got == 0) {
                result = (c == EOF && !converted_any) ? EOF : assigned;
                goto done;
            }
            if (dst) dst[got] = '\0';
            converted_any = 1;
            if (!suppress) assigned++;
            break;
        }
        default:
            result = assigned;      /* unknown conversion */
            goto done;
        }
    }
    result = assigned;
done:
    va_end(ap);
    return result;
}

int vsscanf(const char *restrict s, const char *restrict fmt, va_list ap) {
    if (!s || !fmt) {
        errno = EINVAL;
        return EOF;
    }
    src_t in = { NULL, s, 0, 0 };
    return vscan(&in, fmt, ap);
}

int vfscanf(FILE *restrict f, const char *restrict fmt, va_list ap) {
    if (!f || !fmt) {
        errno = EINVAL;
        return EOF;
    }
    src_t in = { f, NULL, 0, 0 };
    return vscan(&in, fmt, ap);
}

int vscanf(const char *restrict fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

int sscanf(const char *restrict s, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

int fscanf(FILE *restrict f, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int scanf(const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return r;
}
