/*
 * nxlibc - strtod, strtof, strtold: correctly rounded decimal and
 * hexadecimal conversion.
 *
 * Method (decimal input):
 *   1. Parse up to 800 significant digits D and a decimal exponent E, so
 *      the value is D * 10^E. Digits past 800 only matter as "something
 *      non-zero follows"; that is kept as a sticky extra digit. Halfway
 *      points between two doubles have fewer than 800 significant digits,
 *      so this never changes the rounding.
 *   2. Make a first guess with extended (80-bit) precision; it is within a
 *      couple of units in the last place (ulp).
 *   3. Compare D * 10^E exactly, with big integers, against the halfway
 *      points next to the guess and step the guess until the value lies
 *      between them. Exact ties round to even.
 * The result is the nearest representable value in every case, as ISO C
 * Annex F requires for round-to-nearest.
 *
 * strtold returns the correctly rounded double, widened: long double
 * conversions have double precision in this version.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include "../internal.h"

#define MAX_DIGITS  800
#define BIG_LIMBS   240         /* 7680 bits; the largest comparison needs ~5000 */

/* ============ Big unsigned integers (base 2^32, little-endian) ============ */

typedef struct {
    int      n;                 /* limbs in use; 0 means the value 0 */
    int      overflow;          /* an operation exceeded BIG_LIMBS */
    uint32_t d[BIG_LIMBS];
} big_t;

static void big_set_u64(big_t *b, uint64_t v) {
    b->overflow = 0;
    b->n = 0;
    while (v) {
        b->d[b->n++] = (uint32_t)v;
        v >>= 32;
    }
}

static void big_mul_add(big_t *b, uint32_t mul, uint32_t add) {
    uint64_t carry = add;
    for (int i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->d[i] * mul + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (b->n >= BIG_LIMBS) {
            b->overflow = 1;
            return;
        }
        b->d[b->n++] = (uint32_t)carry;
    }
}

static void big_mul_pow5(big_t *b, int k) {
    static const uint32_t pow5[14] = {
        1u, 5u, 25u, 125u, 625u, 3125u, 15625u, 78125u, 390625u, 1953125u,
        9765625u, 48828125u, 244140625u, 1220703125u
    };
    while (k > 0 && !b->overflow) {
        int step = (k > 13) ? 13 : k;
        big_mul_add(b, pow5[step], 0);
        k -= step;
    }
}

static void big_shl(big_t *b, int bits) {
    if (b->n == 0 || bits <= 0) return;
    int words = bits / 32;
    int rem = bits % 32;
    int newn = b->n + words + 1;
    if (newn > BIG_LIMBS) {
        b->overflow = 1;
        return;
    }
    b->d[newn - 1] = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        uint64_t v = (uint64_t)b->d[i] << rem;
        b->d[i + words + 1] |= (uint32_t)(v >> 32);
        b->d[i + words] = (uint32_t)v;
    }
    for (int i = 0; i < words; i++) b->d[i] = 0;
    b->n = newn;
    while (b->n > 0 && b->d[b->n - 1] == 0) b->n--;
}

static int big_cmp(const big_t *a, const big_t *b) {
    if (a->n != b->n) return (a->n > b->n) ? 1 : -1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return (a->d[i] > b->d[i]) ? 1 : -1;
    }
    return 0;
}

/* ============ Target formats ============ */

typedef struct {
    int      p;         /* significand bits */
    int      kmin;      /* exponent of the smallest subnormal: value = m * 2^kmin */
    int      kmax;      /* largest k with m < 2^p */
} fpfmt_t;

static const fpfmt_t FMT_DOUBLE = { 53, -1074, 971 };
static const fpfmt_t FMT_FLOAT  = { 24, -149, 104 };

/* A candidate value m * 2^k; m < 2^p, and m >= 2^(p-1) unless k == kmin */
typedef struct { uint64_t m; int k; int inf; } cand_t;

static void cand_up(cand_t *c, const fpfmt_t *f) {
    c->m++;
    if (c->m == (1ull << f->p)) {
        c->m >>= 1;
        c->k++;
        if (c->k > f->kmax) c->inf = 1;
    }
}

static void cand_down(cand_t *c, const fpfmt_t *f) {
    if (c->m == 0) return;
    c->m--;
    if (c->m < (1ull << (f->p - 1)) && c->k > f->kmin && c->m != 0) {
        c->m = (c->m << 1) | 1u;
        c->k--;
    }
}

/*
 * Compares D * 10^E with (2m+1) * 2^(k-1) (the halfway point above m*2^k
 * when up != 0) or with the halfway point below it. Returns -1, 0, 1, or
 * 2 when the big integers overflowed.
 */
static int cmp_half(const big_t *digits, int e10, const cand_t *c, const fpfmt_t *f, int up) {
    static big_t x, y;              /* large: keep them off the stack */
    uint64_t hm;
    int hq;
    if (up) {
        hm = 2 * c->m + 1;
        hq = c->k - 1;
    } else {
        int at_binade = (c->m == (1ull << (f->p - 1)) && c->k > f->kmin);
        if (at_binade) {            /* the value below has half the spacing */
            hm = 4 * c->m - 1;
            hq = c->k - 2;
        } else {
            hm = 2 * c->m - 1;
            hq = c->k - 1;
        }
    }
    x = *digits;
    big_set_u64(&y, hm);
    int x2 = 0, y2 = hq;
    if (e10 >= 0) {
        big_mul_pow5(&x, e10);
        x2 += e10;
    } else {
        big_mul_pow5(&y, -e10);
        y2 += -e10;
    }
    if (x2 > y2) big_shl(&x, x2 - y2);
    else big_shl(&y, y2 - x2);
    if (x.overflow || y.overflow) return 2;
    return big_cmp(&x, &y);
}

/* ============ Assembling results ============ */

static double cand_to_double(const cand_t *c) {
    if (c->inf) return __builtin_inf();
    uint64_t bits;
    if (c->m < (1ull << 52)) {
        bits = c->m;                                    /* subnormal or zero (k == kmin) */
    } else {
        uint64_t bexp = (uint64_t)(c->k + 1075);        /* m*2^k with m in [2^52,2^53) */
        bits = (bexp << 52) | (c->m & ((1ull << 52) - 1));
    }
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static float cand_to_float(const cand_t *c) {
    if (c->inf) return __builtin_inff();
    uint32_t bits;
    if (c->m < (1u << 23)) {
        bits = (uint32_t)c->m;
    } else {
        uint32_t bexp = (uint32_t)(c->k + 150);
        bits = (bexp << 23) | (uint32_t)(c->m & ((1u << 23) - 1));
    }
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

/* Nearest candidate to mant * 2^e2 (+ a sticky fraction below mant's last
 * bit), with round-half-even. mant != 0. */
static cand_t round_binary(uint64_t mant, int e2, int sticky, const fpfmt_t *f) {
    cand_t c = { 0, f->kmin, 0 };
    int top = 63 - __builtin_clzll(mant);           /* value in [2^(top+e2), 2^(top+e2+1)) */
    int k = top + e2 - (f->p - 1);                   /* exponent for a p-bit significand */
    if (k < f->kmin) k = f->kmin;
    int shift = k - e2;                              /* bits to drop from mant */
    uint64_t m;
    int round_bit = 0, rest = sticky;
    if (shift <= 0) {
        m = (-shift >= 64) ? 0 : mant << -shift;     /* exact (value fits) */
    } else if (shift > 64) {
        m = 0;
        rest |= 1;
    } else {
        m = (shift == 64) ? 0 : mant >> shift;
        uint64_t dropped = (shift == 64) ? mant : mant & ((1ull << shift) - 1);
        uint64_t half = 1ull << (shift - 1);
        round_bit = (dropped & half) != 0;
        rest |= (dropped & (half - 1)) != 0;
    }
    c.m = m;
    c.k = k;
    if (c.k > f->kmax) {
        c.inf = 1;
        return c;
    }
    if (round_bit && (rest || (c.m & 1))) cand_up(&c, f);
    return c;
}

/* ============ Parsing ============ */

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int match_word(const char *s, const char *word) {
    int i = 0;
    for (; word[i]; i++) {
        if (lower((unsigned char)s[i]) != word[i]) return 0;
    }
    return i;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Reads an exponent "e+12"/"p-3" at s; returns chars used (0 if none) */
static int parse_exp(const char *s, char letter, long *out) {
    if (lower((unsigned char)s[0]) != letter) return 0;
    int i = 1, neg = 0;
    if (s[i] == '+' || s[i] == '-') {
        neg = (s[i] == '-');
        i++;
    }
    if (s[i] < '0' || s[i] > '9') return 0;
    long v = 0;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
        if (v < 100000000) v = v * 10 + (s[i] - '0');   /* clamps absurd exponents */
    }
    *out = neg ? -v : v;
    return i;
}

typedef struct {
    int     kind;           /* 0 number, 1 inf, 2 nan, -1 nothing parsed */
    int     neg;
    /* hexadecimal: */
    int     hex;
    uint64_t hmant;
    long    hexp;
    int     hsticky;
    /* decimal: */
    char    dig[MAX_DIGITS + 1];
    int     nd;
    long    e10;            /* value = (digits as an integer) * 10^e10 */
} parsed_t;

static const char *parse_number(const char *s, parsed_t *r) {
    const char *p = s;
    r->kind = -1;
    r->neg = 0;
    r->hex = 0;
    r->nd = 0;
    r->e10 = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') {
        r->neg = (*p == '-');
        p++;
    }
    int n;
    if ((n = match_word(p, "infinity")) || (n = match_word(p, "inf"))) {
        r->kind = 1;
        return p + n;
    }
    if ((n = match_word(p, "nan"))) {
        r->kind = 2;
        p += n;
        if (*p == '(') {
            const char *q = p + 1;
            while (isalnum((unsigned char)*q) || *q == '_') q++;
            if (*q == ')') p = q + 1;
        }
        return p;
    }
    if (p[0] == '0' && lower((unsigned char)p[1]) == 'x' &&
        (hexval((unsigned char)p[2]) >= 0 || (p[2] == '.' && hexval((unsigned char)p[3]) >= 0))) {
        p += 2;
        r->hex = 1;
        r->hmant = 0;
        r->hexp = 0;
        r->hsticky = 0;
        int seen_point = 0, any = 0;
        for (;; p++) {
            if (*p == '.' && !seen_point) {
                seen_point = 1;
                continue;
            }
            int v = hexval((unsigned char)*p);
            if (v < 0) break;
            any = 1;
            if (r->hmant >> 60) {
                /* no room: remember a non-zero digit, keep counting places */
                if (v) r->hsticky = 1;
                if (!seen_point) r->hexp += 4;
            } else {
                r->hmant = (r->hmant << 4) | (uint64_t)v;
                if (seen_point) r->hexp -= 4;
            }
        }
        if (!any) return s;
        long e = 0;
        int used = parse_exp(p, 'p', &e);
        p += used;
        r->hexp += e;
        r->kind = 0;
        return p;
    }
    /* decimal */
    int seen_point = 0, any = 0, sticky = 0;
    long dropped_int = 0;       /* integer digits not stored */
    for (;; p++) {
        if (*p == '.' && !seen_point) {
            seen_point = 1;
            continue;
        }
        if (*p < '0' || *p > '9') break;
        any = 1;
        char d = *p;
        if (r->nd == 0 && d == '0') {
            if (seen_point) r->e10--;       /* leading zeros after the point */
            continue;
        }
        if (r->nd < MAX_DIGITS) {
            r->dig[r->nd++] = d;
            if (seen_point) r->e10--;
        } else {
            if (d != '0') sticky = 1;
            if (!seen_point) dropped_int++;
        }
    }
    if (!any) return s;
    long e = 0;
    p += parse_exp(p, 'e', &e);
    r->e10 += e + dropped_int;
    if (sticky) {
        /* A digit 1 past the kept ones stands for "more, non-zero" */
        r->dig[r->nd++] = '1';
        r->e10--;
    }
    r->kind = 0;
    return p;
}

/* Converts a parsed decimal number; sets errno on overflow/underflow */
static cand_t convert_decimal(const parsed_t *r, const fpfmt_t *f) {
    cand_t c = { 0, f->kmin, 0 };
    if (r->nd == 0) return c;                                   /* zero */
    long mag = r->nd + r->e10;              /* value in [10^(mag-1), 10^mag) */
    if (mag > 320) {
        c.inf = 1;
        errno = ERANGE;
        return c;
    }
    if (mag < -330) {
        errno = ERANGE;
        return c;                                               /* underflow to 0 */
    }

    /* First guess in extended precision from the leading 19 digits */
    uint64_t lead = 0;
    int nl = (r->nd < 19) ? r->nd : 19;
    for (int i = 0; i < nl; i++) lead = lead * 10 + (uint64_t)(r->dig[i] - '0');
    long scale = r->e10 + (r->nd - nl);
    long double guess = (long double)lead;
    long double p10 = 10.0L;
    long as = (scale < 0) ? -scale : scale;
    long double factor = 1.0L;
    while (as) {
        if (as & 1) factor *= p10;
        p10 *= p10;
        as >>= 1;
    }
    guess = (scale < 0) ? guess / factor : guess * factor;

    /* Split the guess into a 64-bit mantissa and exponent */
    union { long double v; struct { uint64_t mant; uint16_t se; } b; } u;
    u.v = guess;
    int bexp = (u.b.se & 0x7FFF);
    if (bexp == 0x7FFF || guess == 0.0L) {
        /* Outside even the extended range: start from the limits */
        if (guess == 0.0L) {
            c.m = 1;
            c.k = f->kmin;
        } else {
            c.m = (1ull << f->p) - 1;
            c.k = f->kmax;
        }
    } else {
        int e2 = bexp - 16383 - 63;
        c = round_binary(u.b.mant, e2, 0, f);
        if (c.inf) {
            c.inf = 0;
            c.m = (1ull << f->p) - 1;
            c.k = f->kmax;
        }
        if (c.m == 0) c.m = 1;
    }

    static big_t digits;
    big_set_u64(&digits, 0);
    for (int i = 0; i < r->nd; i += 9) {
        uint32_t chunk = 0, mul = 1;
        for (int j = i; j < r->nd && j < i + 9; j++) {
            chunk = chunk * 10 + (uint32_t)(r->dig[j] - '0');
            mul *= 10;
        }
        if (digits.n == 0) big_set_u64(&digits, chunk);
        else big_mul_add(&digits, mul, chunk);
    }

    for (int iter = 0; iter < 64; iter++) {
        int up = cmp_half(&digits, (int)r->e10, &c, f, 1);
        if (up == 2) break;                                     /* cannot happen in range */
        if (up > 0 || (up == 0 && (c.m & 1))) {
            if (c.m == (1ull << f->p) - 1 && c.k == f->kmax) {
                c.inf = 1;                                      /* above the largest finite */
                break;
            }
            cand_up(&c, f);
            if (up == 0) break;                                 /* tie went to the even value */
            continue;
        }
        if (up == 0) break;                                     /* tie, m even: stay */
        if (c.m == 0) break;
        int down = cmp_half(&digits, (int)r->e10, &c, f, 0);
        if (down == 2) break;
        if (down < 0 || (down == 0 && (c.m & 1))) {
            cand_down(&c, f);
            if (down == 0) break;
            continue;
        }
        break;                                                  /* between the halfway points */
    }
    /* A non-zero input that became 0 or infinity is a range error */
    if (c.inf || c.m == 0) errno = ERANGE;
    return c;
}

static cand_t convert(const parsed_t *r, const fpfmt_t *f) {
    if (r->hex) {
        if (r->hmant == 0) {
            cand_t z = { 0, f->kmin, 0 };
            return z;
        }
        long e = r->hexp;
        if (e > 20000) e = 20000;
        if (e < -20000) e = -20000;
        cand_t c = round_binary(r->hmant, (int)e, r->hsticky, f);
        if (c.inf || c.m == 0) errno = ERANGE;
        return c;
    }
    return convert_decimal(r, f);
}

static parsed_t g_parsed;   /* ~850 bytes; no threads, keep it off small stacks */

double strtod(const char *restrict s, char **restrict end) {
    const char *stop = parse_number(s, &g_parsed);
    if (end) *end = (char *)((g_parsed.kind < 0) ? s : stop);
    double v;
    switch (g_parsed.kind) {
    case 1: v = __builtin_inf(); break;
    case 2: v = __builtin_nan(""); break;
    case 0: {
        cand_t c = convert(&g_parsed, &FMT_DOUBLE);
        v = cand_to_double(&c);
        break;
    }
    default: return 0.0;
    }
    return g_parsed.neg ? -v : v;
}

float strtof(const char *restrict s, char **restrict end) {
    const char *stop = parse_number(s, &g_parsed);
    if (end) *end = (char *)((g_parsed.kind < 0) ? s : stop);
    float v;
    switch (g_parsed.kind) {
    case 1: v = __builtin_inff(); break;
    case 2: v = __builtin_nanf(""); break;
    case 0: {
        cand_t c = convert(&g_parsed, &FMT_FLOAT);
        v = cand_to_float(&c);
        break;
    }
    default: return 0.0f;
    }
    return g_parsed.neg ? -v : v;
}

long double strtold(const char *restrict s, char **restrict end) {
    return (long double)strtod(s, end);
}
