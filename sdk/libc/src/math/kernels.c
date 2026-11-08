/*
 * nxlibc - extended-precision kernels shared by the math functions.
 *
 * Every kernel works in x87 extended precision (64-bit significand), so a
 * double result computed from it carries about 11 guard bits and is
 * rounded once at the end. The series are plain Taylor/atanh series on
 * small reduced arguments; their coefficients (1/k!) and constants come
 * from tools/gen_math_constants.py.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>
#include "../internal.h"
#include "constants.h"

/* 2^n as a long double, for n in [-16382, 16383] */
static long double pow2l(int n) {
    union { long double v; struct { uint64_t mant; uint16_t se; } b; } u;
    memset(&u, 0, sizeof(u));
    if (n > 16383) n = 16383;
    if (n < -16382) n = -16382;
    u.b.mant = 1ull << 63;
    u.b.se = (uint16_t)(n + 16383);
    return u.v;
}

static long double scale2l(long double x, int n) {
    /* two steps so that results near the ends of the range stay exact */
    if (n > 16000) {
        x *= pow2l(16000);
        n -= 16000;
    } else if (n < -16000) {
        x *= pow2l(-16000);
        n += 16000;
    }
    return x * pow2l(n);
}

static long double fabsl_(long double x) { return x < 0 ? -x : x; }

long double __nxc_sqrtl(long double x) NXC_HIDDEN;
long double __nxc_sqrtl(long double x) {
    __asm__("fsqrt" : "+t"(x));
    return x;
}

/* ============ exp ============ */

/* e^(x + xlo) where xlo is a small correction below the last bit of x */
static long double expl2(long double x, long double xlo) {
    if (x != x) return x;
    if (x > 11357.0L) return __builtin_infl();
    if (x < -11400.0L) return 0.0L;
    /* x = k ln2 + r, |r| <= ln2/2 */
    long double kf = x * NXC_LOG2E_L;
    long k = (long)(kf + (kf >= 0 ? 0.5L : -0.5L));
    long double r = (x - (long double)k * NXC_LN2_HI_L) - (long double)k * NXC_LN2_LO_L;
    r += xlo;
    /* e^r = sum r^i / i!, |r| < 0.35: 18 terms reach 2^-70 */
    long double sum = nxc_inv_fact[18];
    for (int i = 17; i >= 0; i--) sum = sum * r + nxc_inv_fact[i];
    return scale2l(sum, (int)k);
}

long double __nxc_expl(long double x) {
    return expl2(x, 0.0L);
}

/* e^x - 1 without cancellation for small x */
long double __nxc_expm1l(long double x) NXC_HIDDEN;
long double __nxc_expm1l(long double x) {
    if (fabsl_(x) < 0.35L) {
        long double sum = nxc_inv_fact[20];
        for (int i = 19; i >= 1; i--) sum = sum * x + nxc_inv_fact[i];
        return sum * x;
    }
    return __nxc_expl(x) - 1.0L;
}

/* ============ log ============ */

/* 2 * atanh(s) = ln((1+s)/(1-s)), |s| <= 0.18 */
static long double atanh2_series(long double s) {
    long double s2 = s * s;
    long double sum = 0.0L;
    for (int k = 31; k >= 1; k -= 2) sum = sum * s2 + 1.0L / (long double)k;
    return 2.0L * s * sum;
}

long double __nxc_logl(long double x) {
    union { long double v; struct { uint64_t mant; uint16_t se; } b; } u;
    u.v = x;
    int e = (int)(u.b.se & 0x7FFF) - 16383;
    if ((u.b.se & 0x7FFF) == 0) {
        /* extended subnormal: cannot come from a double, normalise anyway */
        u.v = x * pow2l(64);
        e = (int)(u.b.se & 0x7FFF) - 16383 - 64;
    }
    u.b.se = (uint16_t)((u.b.se & 0x8000) | 16383);     /* m in [1, 2) */
    long double m = u.v;
    if (m > 1.41421356237309504880L) {
        m *= 0.5L;
        e++;
    }
    long double s = (m - 1.0L) / (m + 1.0L);
    long double lm = atanh2_series(s);
    return ((long double)e * NXC_LN2_HI_L + lm) + (long double)e * NXC_LN2_LO_L;
}

/* ln(1 + x), x > -1, without cancellation for small x */
long double __nxc_log1pl(long double x) NXC_HIDDEN;
long double __nxc_log1pl(long double x) {
    if (fabsl_(x) < 0.25L) return atanh2_series(x / (2.0L + x));
    return __nxc_logl(1.0L + x);
}

/* ============ pow ============ */

/*
 * Error-free transformations in extended precision (64-bit significand):
 * split() cuts a value into two halves of at most 32 bits, so products of
 * halves are exact; two_prod and two_sum return the rounding error of a
 * product or a sum next to the rounded result.
 */
static void split(long double a, long double *hi, long double *lo) {
    long double t = a * 4294967297.0L;      /* 2^32 + 1 */
    *hi = t - (t - a);
    *lo = a - *hi;
}

static long double two_prod(long double a, long double b, long double *err) {
    long double p = a * b, ah, al, bh, bl;
    split(a, &ah, &al);
    split(b, &bh, &bl);
    *err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return p;
}

static long double two_sum(long double a, long double b, long double *err) {
    long double s = a + b;
    long double bb = s - a;
    *err = (a - (s - bb)) + (b - bb);
    return s;
}

/*
 * x^y for finite x > 0 (x != 1) and finite y, as e^(y ln x). ln x is
 * carried as hi + lo (about 120 bits) and the product y ln x is formed
 * without rounding error, so the only errors are those of the final
 * exponential: about 2^-62 relative, whatever the size of y ln x.
 */
long double __nxc_powl(double x, double y) NXC_HIDDEN;
long double __nxc_powl(double x, double y) {
    union { long double v; struct { uint64_t mant; uint16_t se; } b; } u;
    u.v = (long double)x;
    int e;
    if ((u.b.se & 0x7FFF) == 0) {
        u.v = (long double)x * pow2l(64);
        e = (int)(u.b.se & 0x7FFF) - 16383 - 64;
    } else {
        e = (int)(u.b.se & 0x7FFF) - 16383;
    }
    u.b.se = (uint16_t)((u.b.se & 0x8000) | 16383);
    long double m = u.v;                    /* 53 significant bits, [1, 2) */
    if (m > 1.41421356237309504880L) {
        m *= 0.5L;
        e++;
    }
    /* s = (m - 1) / (m + 1); numerator and denominator are exact */
    long double num = m - 1.0L, den = m + 1.0L;
    long double s = num / den;
    long double perr;
    long double p = two_prod(s, den, &perr);
    long double s_lo = ((num - p) - perr) / den;
    /* ln m = 2s + 2s^3 (1/3 + s^2/5 + ...) */
    long double s2 = s * s;
    long double q = 0.0L;
    for (int k = 31; k >= 3; k -= 2) q = q * s2 + 1.0L / (long double)k;
    long double lm_lo = 2.0L * s_lo + 2.0L * s * s2 * q;
    /* ln x = e ln2 + ln m, as hi + lo */
    long double l_err;
    long double l_hi = two_sum((long double)e * NXC_LN2_HI_L, 2.0L * s, &l_err);
    long double l_lo = l_err + lm_lo + (long double)e * NXC_LN2_LO_L;
    long double hi = l_hi + l_lo;
    long double lo = l_lo - (hi - l_hi);
    /* y ln x = z + z_lo */
    long double z_err;
    long double z = two_prod((long double)y, hi, &z_err);
    long double z_lo = z_err + (long double)y * lo;
    if (z > 11357.0L) return __builtin_infl();
    if (z < -11400.0L) return 0.0L;
    return expl2(z, z_lo);
}

/* ============ Trigonometric reduction ============ */

/* Bits [pos, pos+32) of 2/pi after the point (pos 0 is the first bit) */
static uint32_t two_over_pi_bits(int pos) {
    int k = pos / 32, r = pos % 32;
    uint32_t hi = (k < NXC_TWO_OVER_PI_WORDS) ? nxc_two_over_pi[k] : 0;
    if (r == 0) return hi;
    uint32_t lo = (k + 1 < NXC_TWO_OVER_PI_WORDS) ? nxc_two_over_pi[k + 1] : 0;
    return (hi << r) | (lo >> (32 - r));
}

/* 64 bits starting at bit pos of a little-endian 32-bit limb array */
static uint64_t limbs_get64(const uint32_t *l, int nl, int pos) {
    uint64_t v = 0;
    for (int b = 0; b < 64; b += 32) {
        int p = pos + b;
        int k = p / 32, r = p % 32;
        uint64_t lo = (k < nl) ? l[k] : 0;
        uint64_t hi = (k + 1 < nl) ? l[k + 1] : 0;
        uint32_t w = (uint32_t)(((hi << 32) | lo) >> r);
        v |= (uint64_t)w << b;
    }
    return v;
}

/*
 * Payne-Hanek reduction for |x| >= 2^20: y = x * 2/pi is computed modulo 4
 * from a 192-bit window of 2/pi, the only bits that matter for this x.
 */
static int rem_pio2_large(double ax, long double *r) {
    uint64_t bits;
    memcpy(&bits, &ax, sizeof(bits));
    int e = (int)((bits >> 52) & 0x7FF) - 1075;          /* ax = m * 2^e */
    uint64_t m = (bits & ((1ull << 52) - 1)) | (1ull << 52);

    int i0 = e - 1;                                      /* first bit that matters (1-based) */
    if (i0 < 1) i0 = 1;
    /* window P: bits i0 .. i0+191, as 6 little-endian limbs */
    uint32_t P[6];
    for (int j = 0; j < 6; j++) P[5 - j] = two_over_pi_bits(i0 - 1 + 32 * j);
    uint32_t M[2] = { (uint32_t)m, (uint32_t)(m >> 32) };
    uint32_t prod[8] = { 0 };
    for (int a = 0; a < 2; a++) {
        uint64_t carry = 0;
        for (int b = 0; b < 6; b++) {
            uint64_t t = (uint64_t)M[a] * P[b] + prod[a + b] + carry;
            prod[a + b] = (uint32_t)t;
            carry = t >> 32;
        }
        prod[a + 6] += (uint32_t)carry;
    }
    int s = i0 + 191 - e;                                /* fraction bits in prod */
    int q = (int)(limbs_get64(prod, 8, s) & 3);
    uint64_t fhi = limbs_get64(prod, 8, s - 64);
    uint64_t flo = limbs_get64(prod, 8, s - 128);
    int neg = 0;
    if (fhi >> 63) {
        /* fraction >= 1/2: use fraction - 1 and the next quadrant */
        fhi = ~fhi;
        flo = ~flo;
        if (++flo == 0) fhi++;
        neg = 1;
        q = (q + 1) & 3;
    }
    long double f;
    if (fhi) {
        int lz = __builtin_clzll(fhi);
        uint64_t mant = lz ? (fhi << lz) | (flo >> (64 - lz)) : fhi;
        f = scale2l((long double)mant, -64 - lz);
    } else {
        f = scale2l((long double)flo, -128);
    }
    *r = (neg ? -f : f) * NXC_PIO2_L;
    return q;
}

int __nxc_rem_pio2(double x, long double *r) {
    double ax = (x < 0) ? -x : x;
    int q;
    if (ax <= 0.785398163397448309615) {
        *r = x;
        return 0;
    }
    if (ax < 1048576.0) {
        /* Cody-Waite with pi/2 in three exact pieces */
        long double qf = (long double)ax * NXC_INVPIO2_L;
        long n = (long)(qf + 0.5L);
        long double nf = (long double)n;
        *r = (((long double)ax - nf * NXC_PIO2_P1_L) - nf * NXC_PIO2_P2_L) - nf * NXC_PIO2_P3_L;
        q = (int)(n & 3);
    } else {
        q = rem_pio2_large(ax, r);
    }
    if (x < 0) {
        *r = -*r;
        q = (4 - q) & 3;
    }
    return q;
}

/* sin r, |r| <= pi/4 + a little: terms to r^25 reach 2^-70 */
long double __nxc_sin_kernel(long double r) {
    long double r2 = r * r;
    long double sum = 0.0L;
    for (int k = 25; k >= 3; k -= 2) {
        long double c = nxc_inv_fact[k];
        if (((k - 1) / 2) & 1) c = -c;
        sum = (sum + c) * r2;
    }
    return r + r * sum;
}

/* cos r, |r| <= pi/4 + a little */
long double __nxc_cos_kernel(long double r) {
    long double r2 = r * r;
    long double sum = 0.0L;
    for (int k = 26; k >= 2; k -= 2) {
        long double c = nxc_inv_fact[k];
        if ((k / 2) & 1) c = -c;
        sum = (sum + c) * r2;
    }
    return 1.0L + sum;
}

/* ============ atan ============ */

long double __nxc_atan_kernel(long double x) {
    if (x != x) return x;
    int neg = x < 0;
    long double a = neg ? -x : x;
    int flip = 0;
    if (a > 1.0L) {
        if (a == __builtin_infl()) return neg ? -NXC_PIO2_L : NXC_PIO2_L;
        a = 1.0L / a;
        flip = 1;
    }
    /* atan(a) = 2 atan(a / (1 + sqrt(1 + a^2))), twice: |t| <= tan(pi/16) */
    long double t = a;
    for (int i = 0; i < 2; i++) t = t / (1.0L + __nxc_sqrtl(1.0L + t * t));
    long double t2 = t * t;
    long double sum = 0.0L;
    for (int k = 31; k >= 1; k -= 2) {
        long double c = 1.0L / (long double)k;
        if ((k / 2) & 1) c = -c;
        sum = sum * t2 + c;
    }
    long double v = 4.0L * t * sum;
    if (flip) v = NXC_PIO2_L - v;
    return neg ? -v : v;
}
