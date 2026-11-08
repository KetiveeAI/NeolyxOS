/*
 * nxlibc - the printf formatter and the printf family.
 *
 * Supported: flags - + space # 0; width and precision (numbers or *);
 * length modifiers hh h l ll j z t L; conversions d i u o x X c s p f F e E
 * g G a A %. %lc and %ls print wide characters 0-127 as ASCII and others
 * as '?' (no wide-character locale yet).
 *
 * %n is refused: it writes through a pointer chosen by the format string,
 * the classic way a format-string bug becomes code execution. A format
 * with %n (or any unknown conversion) makes the call fail with EINVAL and
 * logs the reason; the output up to that point is kept.
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
#include <stdlib.h>
#include <nx/libc.h>
#include "../internal.h"

/* ============ Sink ============ */

void __nxc_sink_put(nxc_sink_t *s, const char *data, size_t n) {
    if (n == 0) return;
    s->total += n;
    if (s->file) {
        if (!s->error && __nxc_stream_write(s->file, data, n) != n) s->error = 1;
        return;
    }
    if (s->cap == 0) return;
    size_t room = s->cap - 1 - s->len;      /* keep one byte for the NUL */
    size_t k = (n < room) ? n : room;
    if (k) {
        memcpy(s->buf + s->len, data, k);
        s->len += k;
    }
}

/* ============ Integers ============ */

typedef enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIGL } len_t;

static void put_int(nxc_sink_t *s, uintmax_t mag, int negative, int base, int upper, int flags,
                    int width, int prec, int is_signed) {
    char digits[72];
    int nd = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (mag) {
        digits[nd++] = set[mag % (unsigned)base];
        mag /= (unsigned)base;
    }
    /* precision: minimum digits; 0 with value 0 prints no digits */
    int zeros = (prec > nd) ? prec - nd : 0;
    if (prec < 0 && nd == 0) zeros = 1;
    char prefix[2];
    int plen = 0;
    if (is_signed) {
        if (negative) prefix[plen++] = '-';
        else if (flags & NXC_FMT_PLUS) prefix[plen++] = '+';
        else if (flags & NXC_FMT_SPACE) prefix[plen++] = ' ';
    }
    if (flags & NXC_FMT_ALT) {
        if (base == 8 && zeros == 0 && (nd == 0 || digits[nd - 1] != '0')) zeros = 1;
        if (base == 16 && nd > 0) {
            prefix[plen++] = '0';
            prefix[plen++] = upper ? 'X' : 'x';
        }
    }
    int body = plen + zeros + nd;
    int pad = (width > body) ? width - body : 0;
    int zero_pad = (flags & NXC_FMT_ZERO) && !(flags & NXC_FMT_LEFT) && prec < 0;
    if (!(flags & NXC_FMT_LEFT) && !zero_pad) __nxc_pad(s, ' ', pad);
    __nxc_sink_put(s, prefix, (size_t)plen);
    if (zero_pad) __nxc_pad(s, '0', pad);
    __nxc_pad(s, '0', zeros);
    while (nd > 0) __nxc_sink_put(s, &digits[--nd], 1);
    if (flags & NXC_FMT_LEFT) __nxc_pad(s, ' ', pad);
}

static void put_str(nxc_sink_t *s, const char *str, size_t len, int flags, int width) {
    int pad = (width > 0 && (size_t)width > len) ? width - (int)len : 0;
    if (!(flags & NXC_FMT_LEFT)) __nxc_pad(s, ' ', pad);
    __nxc_sink_put(s, str, len);
    if (flags & NXC_FMT_LEFT) __nxc_pad(s, ' ', pad);
}

static char wide_to_ascii(long wc) {
    return (wc >= 0 && wc < 128) ? (char)wc : '?';
}

static int read_int(const char **pp) {
    const char *p = *pp;
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        if (v <= INT_MAX) v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return (v > INT_MAX) ? INT_MAX : (int)v;
}

static int fail(const char *why) {
    __nxc_log("nxlibc: printf: ");
    __nxc_log(why);
    __nxc_log("\n");
    errno = EINVAL;
    return -1;
}

int __nxc_vformat(nxc_sink_t *s, const char *fmt, va_list ap_in) {
    va_list ap;
    va_copy(ap, ap_in);
    const char *p = fmt;
    int rc = 0;
    if (!fmt) {
        va_end(ap);
        return fail("NULL format");
    }
    while (*p) {
        if (*p != '%') {
            const char *q = p;
            while (*q && *q != '%') q++;
            __nxc_sink_put(s, p, (size_t)(q - p));
            p = q;
            continue;
        }
        p++;
        if (*p == '%') {
            __nxc_sink_put(s, "%", 1);
            p++;
            continue;
        }
        int flags = 0;
        for (;; p++) {
            if (*p == '-') flags |= NXC_FMT_LEFT;
            else if (*p == '+') flags |= NXC_FMT_PLUS;
            else if (*p == ' ') flags |= NXC_FMT_SPACE;
            else if (*p == '#') flags |= NXC_FMT_ALT;
            else if (*p == '0') flags |= NXC_FMT_ZERO;
            else break;
        }
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= NXC_FMT_LEFT;
                width = (width == INT_MIN) ? INT_MAX : -width;
            }
            p++;
        } else {
            width = read_int(&p);
        }
        int prec = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;    /* negative: as if omitted */
                p++;
            } else {
                prec = read_int(&p);
            }
        }
        len_t len = LEN_NONE;
        switch (*p) {
        case 'h':
            len = (p[1] == 'h') ? LEN_HH : LEN_H;
            p += (len == LEN_HH) ? 2 : 1;
            break;
        case 'l':
            len = (p[1] == 'l') ? LEN_LL : LEN_L;
            p += (len == LEN_LL) ? 2 : 1;
            break;
        case 'j': len = LEN_J; p++; break;
        case 'z': len = LEN_Z; p++; break;
        case 't': len = LEN_T; p++; break;
        case 'L': len = LEN_BIGL; p++; break;
        default: break;
        }
        char conv = *p;
        if (conv == '\0') {
            rc = fail("format ends inside a conversion");
            break;
        }
        p++;
        switch (conv) {
        case 'd':
        case 'i': {
            intmax_t v;
            switch (len) {
            case LEN_HH: v = (signed char)va_arg(ap, int); break;
            case LEN_H:  v = (short)va_arg(ap, int); break;
            case LEN_L:  v = va_arg(ap, long); break;
            case LEN_LL: v = va_arg(ap, long long); break;
            case LEN_J:  v = va_arg(ap, intmax_t); break;
            case LEN_Z:  v = (intmax_t)va_arg(ap, size_t); break;   /* signed size: long on LP64 */
            case LEN_T:  v = va_arg(ap, ptrdiff_t); break;
            default:     v = va_arg(ap, int); break;
            }
            uintmax_t mag = (v < 0) ? (uintmax_t)0 - (uintmax_t)v : (uintmax_t)v;
            put_int(s, mag, v < 0, 10, 0, flags, width, prec, 1);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            uintmax_t v;
            switch (len) {
            case LEN_HH: v = (unsigned char)va_arg(ap, unsigned int); break;
            case LEN_H:  v = (unsigned short)va_arg(ap, unsigned int); break;
            case LEN_L:  v = va_arg(ap, unsigned long); break;
            case LEN_LL: v = va_arg(ap, unsigned long long); break;
            case LEN_J:  v = va_arg(ap, uintmax_t); break;
            case LEN_Z:  v = va_arg(ap, size_t); break;
            case LEN_T:  v = (uintmax_t)va_arg(ap, ptrdiff_t); break;
            default:     v = va_arg(ap, unsigned int); break;
            }
            int base = (conv == 'u') ? 10 : (conv == 'o') ? 8 : 16;
            put_int(s, v, 0, base, conv == 'X', flags & ~(NXC_FMT_PLUS | NXC_FMT_SPACE),
                    width, prec, 0);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            if (v == 0) {
                put_str(s, "0x0", 3, flags, width);     /* '#' alone would print "0" */
                break;
            }
            put_int(s, v, 0, 16, 0, (flags & (NXC_FMT_LEFT | NXC_FMT_ZERO)) | NXC_FMT_ALT,
                    width, prec, 0);
            break;
        }
        case 'c': {
            char c;
            if (len == LEN_L) c = wide_to_ascii((long)va_arg(ap, unsigned int));
            else c = (char)(unsigned char)va_arg(ap, int);
            put_str(s, &c, 1, flags, width);
            break;
        }
        case 's': {
            if (len == LEN_L) {
                const int32_t *ws = va_arg(ap, const int32_t *);
                if (!ws) {
                    put_str(s, "(null)", (prec >= 0 && prec < 6) ? (size_t)prec : 6, flags, width);
                    break;
                }
                size_t n = 0;
                while (ws[n] && (prec < 0 || n < (size_t)prec)) n++;
                int pad = (width > 0 && (size_t)width > n) ? width - (int)n : 0;
                if (!(flags & NXC_FMT_LEFT)) __nxc_pad(s, ' ', pad);
                for (size_t i = 0; i < n; i++) {
                    char c = wide_to_ascii(ws[i]);
                    __nxc_sink_put(s, &c, 1);
                }
                if (flags & NXC_FMT_LEFT) __nxc_pad(s, ' ', pad);
                break;
            }
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            size_t n = (prec >= 0) ? strnlen(str, (size_t)prec) : strlen(str);
            put_str(s, str, n, flags, width);
            break;
        }
        case 'f': case 'F': case 'e': case 'E':
        case 'g': case 'G': case 'a': case 'A': {
            double v;
            if (len == LEN_BIGL) v = (double)va_arg(ap, long double);
            else v = va_arg(ap, double);
            if (__nxc_format_fp(s, v, conv, flags, width, prec) != 0) rc = -1;
            break;
        }
        case 'n':
            rc = fail("%n is not supported (format-string safety)");
            break;
        default:
            rc = fail("unknown conversion");
            break;
        }
        if (rc != 0) break;
    }
    va_end(ap);
    if (rc != 0) return -1;
    if (s->error) return -1;    /* errno was set by the failed write */
    if (s->total > INT_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    return (int)s->total;
}

/* ============ The printf family ============ */

int vsnprintf(char *restrict buf, size_t size, const char *restrict fmt, va_list ap) {
    nxc_sink_t s = { NULL, buf, size, 0, 0, 0 };
    if (size > 0 && !buf) {
        errno = EINVAL;
        return -1;
    }
    int r = __nxc_vformat(&s, fmt, ap);
    if (size > 0) buf[s.len] = '\0';
    return r;
}

int snprintf(char *restrict buf, size_t size, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *restrict buf, const char *restrict fmt, va_list ap) {
    /* The caller promises the buffer is big enough (ISO C); prefer snprintf */
    return vsnprintf(buf, (size_t)INT_MAX + 1, fmt, ap);
}

int sprintf(char *restrict buf, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *restrict f, const char *restrict fmt, va_list ap) {
    return __nxc_vfprintf_locked(f, fmt, ap);
}

int fprintf(FILE *restrict f, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int vprintf(const char *restrict fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

/* ============ Debug log ============ */

int nxc_vdebugf(const char *fmt, va_list ap) {
    char line[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(line, sizeof(line), fmt, ap2);
    va_end(ap2);
    if (n < 0) return n;
    if ((size_t)n < sizeof(line)) {
        __nxc_log(line);
        return n;
    }
    /* Longer than one line: format again into a heap buffer */
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) {
        __nxc_log(line);    /* the first 255 bytes are better than nothing */
        __nxc_log("\nnxlibc: debugf: message truncated (out of memory)\n");
        return n;
    }
    va_copy(ap2, ap);
    vsnprintf(big, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    __nxc_log(big);
    free(big);
    return n;
}

int nxc_debugf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = nxc_vdebugf(fmt, ap);
    va_end(ap);
    return r;
}
