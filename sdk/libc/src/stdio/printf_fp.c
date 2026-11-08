/*
 * nxlibc - floating-point conversions for printf: %f %F %e %E %g %G %a %A.
 *
 * Decimal conversions print the exact value of the double. A double is
 * m * 2^e with integer m; its exact decimal expansion is m * 2^e (e >= 0)
 * or m * 5^-e / 10^-e (e < 0), which is computed with a big integer in
 * base 10^9 (at most 767 significant digits). The digits are then rounded
 * to the requested precision with round-half-to-even on the exact value, so
 * the output never depends on intermediate floating-point rounding and is
 * the same on every machine.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "../internal.h"

#define MAX_SIG   800       /* > 767 significant digits of any double */
#define LIMBS     96        /* base 1e9: 96 * 9 = 864 digits */

typedef struct {
    char digits[MAX_SIG + 2];
    int  len;               /* significant digits in `digits`, no trailing zeros */
    int  dp;                /* value = 0.digits * 10^dp; 0 when the value is 0 */
} dec_t;

/* ============ Exact decimal expansion ============ */

static void dec_from_double(double v, dec_t *out) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    uint64_t frac = bits & ((1ull << 52) - 1);
    int bexp = (int)((bits >> 52) & 0x7FF);
    uint64_t m;
    int e2;
    if (bexp == 0) {
        m = frac;
        e2 = -1074;
    } else {
        m = frac | (1ull << 52);
        e2 = bexp - 1075;
    }
    out->len = 0;
    out->dp = 0;
    if (m == 0) return;

    /* strip factors of two from m to keep the numbers small */
    while ((m & 1) == 0) {
        m >>= 1;
        e2++;
    }

    uint32_t limb[LIMBS];
    int n = 0;
    while (m) {
        limb[n++] = (uint32_t)(m % 1000000000u);
        m /= 1000000000u;
    }
    int frac_digits = 0;
    if (e2 >= 0) {
        int k = e2;
        while (k > 0) {
            int step = (k > 29) ? 29 : k;
            uint64_t carry = 0;
            for (int i = 0; i < n; i++) {
                uint64_t t = ((uint64_t)limb[i] << step) + carry;
                limb[i] = (uint32_t)(t % 1000000000u);
                carry = t / 1000000000u;
            }
            while (carry) {
                limb[n++] = (uint32_t)(carry % 1000000000u);
                carry /= 1000000000u;
            }
            k -= step;
        }
    } else {
        int k = -e2;
        frac_digits = k;
        while (k > 0) {
            int step = (k > 13) ? 13 : k;
            uint32_t mul = 1;
            for (int i = 0; i < step; i++) mul *= 5;
            uint64_t carry = 0;
            for (int i = 0; i < n; i++) {
                uint64_t t = (uint64_t)limb[i] * mul + carry;
                limb[i] = (uint32_t)(t % 1000000000u);
                carry = t / 1000000000u;
            }
            while (carry) {
                limb[n++] = (uint32_t)(carry % 1000000000u);
                carry /= 1000000000u;
            }
            k -= step;
        }
    }

    /* limbs to a digit string, most significant first */
    char *d = out->digits;
    int len = 0;
    uint32_t top = limb[n - 1];
    char tmp[10];
    int t = 0;
    do {
        tmp[t++] = (char)('0' + top % 10);
        top /= 10;
    } while (top);
    while (t > 0) d[len++] = tmp[--t];
    for (int i = n - 2; i >= 0; i--) {
        uint32_t x = limb[i];
        for (int j = 8; j >= 0; j--) {
            d[len + j] = (char)('0' + x % 10);
            x /= 10;
        }
        len += 9;
    }
    out->dp = len - frac_digits;
    while (len > 0 && d[len - 1] == '0') len--;
    out->len = len;
}

/*
 * Keeps the first n significant digits (n may be <= 0), rounding half to
 * even on the exact value. Afterwards len <= max(n, 0) or the value became
 * a single "1" one place higher.
 */
static void dec_round(dec_t *x, int n) {
    if (x->len == 0 || n >= x->len) return;
    int up;
    if (n < 0) {
        up = 0;
    } else {
        char next = x->digits[n];
        int rest = 0;
        for (int i = n + 1; i < x->len; i++) {
            if (x->digits[i] != '0') {
                rest = 1;
                break;
            }
        }
        int last_odd = (n > 0) ? ((x->digits[n - 1] - '0') & 1) : 0;
        up = (next > '5') || (next == '5' && (rest || last_odd));
    }
    if (n <= 0) {
        if (up) {
            x->digits[0] = '1';
            x->len = 1;
            x->dp += 1;
        } else {
            x->len = 0;
            x->dp = 0;
        }
        return;
    }
    x->len = n;
    if (up) {
        int i = n - 1;
        while (i >= 0 && x->digits[i] == '9') {
            x->digits[i] = '0';
            i--;
        }
        if (i < 0) {
            x->digits[0] = '1';
            x->len = 1;
            x->dp += 1;
        } else {
            x->digits[i]++;
        }
    }
    while (x->len > 0 && x->digits[x->len - 1] == '0') x->len--;
    if (x->len == 0) x->dp = 0;
}

/* digit at position i of the significant digits ('0' past the end) */
static char dec_digit(const dec_t *x, int i) {
    return (i >= 0 && i < x->len) ? x->digits[i] : '0';
}

/* ============ Emitting ============ */

void __nxc_pad(nxc_sink_t *s, char c, int n) {
    char block[32];
    if (n <= 0) return;
    memset(block, c, sizeof(block));
    while (n > 0) {
        int k = (n > (int)sizeof(block)) ? (int)sizeof(block) : n;
        __nxc_sink_put(s, block, (size_t)k);
        n -= k;
    }
}

/* Emits digits [from, from+count) of x */
static void put_digits(nxc_sink_t *s, const dec_t *x, int from, int count) {
    while (count > 0) {
        if (from >= x->len || from < 0) {
            /* a run of zeros */
            int zeros = (from < 0) ? -from : count;
            if (zeros > count) zeros = count;
            __nxc_pad(s, '0', zeros);
            from += zeros;
            count -= zeros;
            continue;
        }
        int avail = x->len - from;
        int k = (avail < count) ? avail : count;
        __nxc_sink_put(s, x->digits + from, (size_t)k);
        from += k;
        count -= k;
    }
}

static int exp_digits(int e) {
    int a = (e < 0) ? -e : e;
    int n = 1;
    while (a >= 10) {
        a /= 10;
        n++;
    }
    return (n < 2) ? 2 : n;
}

static void put_exponent(nxc_sink_t *s, char letter, int e) {
    char buf[8];
    int n = 0;
    int a = (e < 0) ? -e : e;
    do {
        buf[n++] = (char)('0' + a % 10);
        a /= 10;
    } while (a);
    if (n < 2) buf[n++] = '0';
    char head[2] = { letter, (char)((e < 0) ? '-' : '+') };
    __nxc_sink_put(s, head, 2);
    while (n > 0) __nxc_sink_put(s, &buf[--n], 1);
}

/* Pads around a body of body_len bytes, given the sign character (0 none) */
static void put_padded_start(nxc_sink_t *s, char sign, int prefix_len, int body_len, int width, int flags,
                             const char *prefix) {
    int total = (sign ? 1 : 0) + prefix_len + body_len;
    int pad = (width > total) ? width - total : 0;
    if (!(flags & NXC_FMT_LEFT) && !(flags & NXC_FMT_ZERO)) __nxc_pad(s, ' ', pad);
    if (sign) __nxc_sink_put(s, &sign, 1);
    if (prefix_len) __nxc_sink_put(s, prefix, (size_t)prefix_len);
    if (!(flags & NXC_FMT_LEFT) && (flags & NXC_FMT_ZERO)) __nxc_pad(s, '0', pad);
}

static void put_padded_end(nxc_sink_t *s, char sign, int prefix_len, int body_len, int width, int flags) {
    int total = (sign ? 1 : 0) + prefix_len + body_len;
    if ((flags & NXC_FMT_LEFT) && width > total) __nxc_pad(s, ' ', width - total);
}

/* ============ Conversions ============ */

static void fmt_hex(nxc_sink_t *s, double v, int upper, int flags, int width, int prec, char sign) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    uint64_t frac = bits & ((1ull << 52) - 1);
    int bexp = (int)((bits >> 52) & 0x7FF);
    uint64_t mant;      /* 1.xxx as 53 bits, or 0 */
    int e;
    if (bexp == 0 && frac == 0) {
        mant = 0;
        e = 0;
    } else if (bexp == 0) {
        /* subnormal: normalise to 1.xxx */
        int shift = __builtin_clzll(frac) - 11;
        mant = frac << shift;
        e = -1022 - shift;
    } else {
        mant = frac | (1ull << 52);
        e = bexp - 1023;
    }
    /* 13 hex digits of fraction after the leading 1 */
    int nd = 13;
    uint64_t f13 = mant & ((1ull << 52) - 1);
    int lead = (mant != 0) ? 1 : 0;
    if (prec >= 0 && prec < 13) {
        int drop = 4 * (13 - prec);
        uint64_t keep = f13 >> drop;
        uint64_t rem = f13 & ((1ull << drop) - 1);
        uint64_t half = 1ull << (drop - 1);
        if (rem > half || (rem == half && (keep & 1))) {
            keep++;
            if (keep >> (4 * prec)) {   /* carried into the leading digit */
                keep = 0;
                lead++;             /* 1 -> 2: still a valid hex digit */
            }
        }
        f13 = keep;
        nd = prec;
    } else if (prec < 0) {
        while (nd > 0 && (f13 & 0xF) == 0) {
            f13 >>= 4;
            nd--;
        }
    }
    int extra_zeros = (prec > 13) ? prec - 13 : 0;
    const char *hexd = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char body[40];
    int bl = 0;
    body[bl++] = hexd[lead];
    if (nd > 0 || extra_zeros > 0 || (flags & NXC_FMT_ALT)) body[bl++] = '.';
    for (int i = nd - 1; i >= 0; i--) body[bl++] = hexd[(f13 >> (4 * i)) & 0xF];
    int elen = 2;
    {
        int a = (e < 0) ? -e : e;
        int nd2 = 1;
        while (a >= 10) {
            a /= 10;
            nd2++;
        }
        elen += nd2;
    }
    const char *prefix = upper ? "0X" : "0x";
    int body_len = bl + extra_zeros + elen;
    put_padded_start(s, sign, 2, body_len, width, flags, prefix);
    __nxc_sink_put(s, body, (size_t)bl);
    __nxc_pad(s, '0', extra_zeros);
    {
        char eb[8];
        int n = 0;
        int a = (e < 0) ? -e : e;
        do {
            eb[n++] = (char)('0' + a % 10);
            a /= 10;
        } while (a);
        char head[2] = { upper ? 'P' : 'p', (char)((e < 0) ? '-' : '+') };
        __nxc_sink_put(s, head, 2);
        while (n > 0) __nxc_sink_put(s, &eb[--n], 1);
    }
    put_padded_end(s, sign, 2, body_len, width, flags);
}

/* %f with fraction digits `frac` (already rounded to them) */
static void emit_fixed(nxc_sink_t *s, const dec_t *x, int frac, int point, char sign, int width, int flags) {
    int intlen = (x->dp > 0) ? x->dp : 1;
    int body = intlen + (point ? 1 : 0) + frac;
    put_padded_start(s, sign, 0, body, width, flags, "");
    if (x->dp > 0) put_digits(s, x, 0, x->dp);
    else __nxc_sink_put(s, "0", 1);
    if (point) __nxc_sink_put(s, ".", 1);
    /* fraction digit i is significant digit dp + i */
    put_digits(s, x, x->dp, frac);
    put_padded_end(s, sign, 0, body, width, flags);
}

/* %e with `frac` digits after the point */
static void emit_exp(nxc_sink_t *s, const dec_t *x, int frac, int point, char letter, char sign,
                     int width, int flags) {
    int e = (x->len == 0) ? 0 : x->dp - 1;
    int body = 1 + (point ? 1 : 0) + frac + 2 + exp_digits(e);    /* d.ddd e+XX */
    put_padded_start(s, sign, 0, body, width, flags, "");
    char first = dec_digit(x, 0);
    __nxc_sink_put(s, &first, 1);
    if (point) __nxc_sink_put(s, ".", 1);
    put_digits(s, x, 1, frac);
    put_exponent(s, letter, e);
    put_padded_end(s, sign, 0, body, width, flags);
}

int __nxc_format_fp(nxc_sink_t *s, double v, int spec, int flags, int width, int prec) {
    int upper = (spec == 'F' || spec == 'E' || spec == 'G' || spec == 'A');
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    int neg = (int)(bits >> 63);
    char sign = neg ? '-' : (flags & NXC_FMT_PLUS) ? '+' : (flags & NXC_FMT_SPACE) ? ' ' : 0;
    if (neg) v = -v;

    if (v != v || v == __builtin_inf()) {
        const char *word = (v != v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        int f2 = flags & ~NXC_FMT_ZERO;     /* never zero-pad inf/nan */
        put_padded_start(s, sign, 0, 3, width, f2, "");
        __nxc_sink_put(s, word, 3);
        put_padded_end(s, sign, 0, 3, width, f2);
        return 0;
    }
    if (flags & NXC_FMT_LEFT) flags &= ~NXC_FMT_ZERO;

    int lspec = spec | 0x20;    /* lower case */
    if (lspec == 'a') {
        fmt_hex(s, v, upper, flags, width, prec, sign);
        return 0;
    }

    dec_t x;
    dec_from_double(v, &x);
    if (prec < 0) prec = 6;
    if (prec > 100000) {
        errno = EOVERFLOW;
        return -1;
    }
    int alt = (flags & NXC_FMT_ALT) != 0;

    if (lspec == 'f') {
        dec_round(&x, x.dp + prec);
        emit_fixed(s, &x, prec, prec > 0 || alt, sign, width, flags);
        return 0;
    }
    if (lspec == 'e') {
        dec_round(&x, prec + 1);
        emit_exp(s, &x, prec, prec > 0 || alt, upper ? 'E' : 'e', sign, width, flags);
        return 0;
    }
    /* %g */
    int P = (prec == 0) ? 1 : prec;
    dec_round(&x, P);
    int X = (x.len == 0) ? 0 : x.dp - 1;
    if (P > X && X >= -4) {
        int frac = P - 1 - X;
        if (!alt) {
            int nonzero = x.len - x.dp;     /* fraction digits that are not trailing zeros */
            if (nonzero < 0) nonzero = 0;
            if (frac > nonzero) frac = nonzero;
        }
        emit_fixed(s, &x, frac, frac > 0 || alt, sign, width, flags);
    } else {
        int frac = P - 1;
        if (!alt) {
            int nonzero = x.len - 1;
            if (nonzero < 0) nonzero = 0;
            if (frac > nonzero) frac = nonzero;
        }
        emit_exp(s, &x, frac, frac > 0 || alt, upper ? 'E' : 'e', sign, width, flags);
    }
    return 0;
}
