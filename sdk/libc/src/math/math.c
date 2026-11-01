/*
 * nxlibc - <math.h> functions.
 *
 * Each function is computed once in extended precision (an "ld_" helper
 * taking a double and returning a long double) and rounded once, to double
 * for the plain function and to float for the "f" function, so the float
 * forms are never rounded twice.
 *
 * Errors follow math_errhandling == MATH_ERRNO: EDOM for domain errors
 * (result NaN), ERANGE when a finite argument gives an infinite result or
 * a non-zero result that rounds to zero.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "../internal.h"
#include "constants.h"

long double __nxc_sqrtl(long double x) NXC_HIDDEN;
long double __nxc_expm1l(long double x) NXC_HIDDEN;
long double __nxc_log1pl(long double x) NXC_HIDDEN;

#define LD_INF  __builtin_infl()
#define LD_NAN  __builtin_nanl("")

static int is_nan(long double x) { return x != x; }
static int is_inf(long double x) { return x == LD_INF || x == -LD_INF; }
static long double ld_abs(long double x) { return x < 0 ? -x : (x == 0 ? 0.0L : x); }

static long double domain_error(void) {
    errno = EDOM;
    return LD_NAN;
}

/* Rounds to double; reports overflow and total underflow of finite values */
static double ret_d(long double v, double arg) {
    double d = (double)v;
    if (!is_nan(v) && !is_inf((long double)arg)) {
        if (is_inf((long double)d) && !is_inf(v)) errno = ERANGE;
        else if (is_inf(v) && !is_nan((long double)arg)) errno = ERANGE;
        else if (d == 0.0 && v != 0.0L) errno = ERANGE;
    }
    return d;
}

static float ret_f(long double v, float arg) {
    float f = (float)v;
    if (!is_nan(v) && !is_inf((long double)arg)) {
        if (is_inf((long double)f) && !is_inf(v)) errno = ERANGE;
        else if (is_inf(v) && !is_nan((long double)arg)) errno = ERANGE;
        else if (f == 0.0f && v != 0.0L) errno = ERANGE;
    }
    return f;
}

/* ============ Trigonometric ============ */

static long double ld_sin(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return domain_error();
    if (x == 0.0) return x;             /* keeps the sign of zero */
    long double r;
    switch (__nxc_rem_pio2(x, &r)) {
    case 0: return __nxc_sin_kernel(r);
    case 1: return __nxc_cos_kernel(r);
    case 2: return -__nxc_sin_kernel(r);
    default: return -__nxc_cos_kernel(r);
    }
}

static long double ld_cos(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return domain_error();
    long double r;
    switch (__nxc_rem_pio2(x, &r)) {
    case 0: return __nxc_cos_kernel(r);
    case 1: return -__nxc_sin_kernel(r);
    case 2: return -__nxc_cos_kernel(r);
    default: return __nxc_sin_kernel(r);
    }
}

static long double ld_tan(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return domain_error();
    if (x == 0.0) return x;
    long double r;
    int q = __nxc_rem_pio2(x, &r);
    long double s = __nxc_sin_kernel(r), c = __nxc_cos_kernel(r);
    return (q & 1) ? -c / s : s / c;
}

static long double ld_atan(double x) {
    if (x == 0.0) return x;
    return __nxc_atan_kernel(x);
}

static long double ld_asin(double x) {
    if (is_nan(x)) return x;
    long double a = ld_abs(x);
    if (a > 1.0L) return domain_error();
    if (x == 0.0) return x;
    if (a == 1.0L) return x > 0 ? NXC_PIO2_L : -NXC_PIO2_L;
    long double xl = x;
    return __nxc_atan_kernel(xl / __nxc_sqrtl((1.0L - xl) * (1.0L + xl)));
}

static long double ld_acos(double x) {
    if (is_nan(x)) return x;
    long double xl = x;
    if (ld_abs(xl) > 1.0L) return domain_error();
    if (xl == -1.0L) return NXC_PI_L;
    /* acos x = 2 atan(sqrt((1-x)/(1+x))) */
    return 2.0L * __nxc_atan_kernel(__nxc_sqrtl((1.0L - xl) / (1.0L + xl)));
}

static long double ld_atan2(double y, double x) {
    if (is_nan(x) || is_nan(y)) return (long double)x + (long double)y;
    int ys = signbit(y) != 0, xs = signbit(x) != 0;
    if (y == 0.0) {
        if (xs) return ys ? -NXC_PI_L : NXC_PI_L;      /* x < 0 or x == -0 */
        return y;                                        /* +-0 */
    }
    if (x == 0.0) return ys ? -NXC_PIO2_L : NXC_PIO2_L;
    if (is_inf(x)) {
        if (is_inf(y)) {
            long double v = xs ? 3.0L * NXC_PIO4_L : NXC_PIO4_L;
            return ys ? -v : v;
        }
        if (xs) return ys ? -NXC_PI_L : NXC_PI_L;
        return ys ? -0.0L : 0.0L;
    }
    if (is_inf(y)) return ys ? -NXC_PIO2_L : NXC_PIO2_L;
    long double ax = ld_abs(x), ay = ld_abs(y);
    long double v;
    if (ay <= ax) {
        v = __nxc_atan_kernel(ay / ax);
    } else {
        v = NXC_PIO2_L - __nxc_atan_kernel(ax / ay);
    }
    if (xs) v = NXC_PI_L - v;
    return ys ? -v : v;
}

double sin(double x) { return ret_d(ld_sin(x), x); }
double cos(double x) { return ret_d(ld_cos(x), x); }
double tan(double x) { return ret_d(ld_tan(x), x); }
double asin(double x) { return ret_d(ld_asin(x), x); }
double acos(double x) { return ret_d(ld_acos(x), x); }
double atan(double x) { return ret_d(ld_atan(x), x); }
double atan2(double y, double x) { return ret_d(ld_atan2(y, x), x); }
float sinf(float x) { return ret_f(ld_sin(x), x); }
float cosf(float x) { return ret_f(ld_cos(x), x); }
float tanf(float x) { return ret_f(ld_tan(x), x); }
float asinf(float x) { return ret_f(ld_asin(x), x); }
float acosf(float x) { return ret_f(ld_acos(x), x); }
float atanf(float x) { return ret_f(ld_atan(x), x); }
float atan2f(float y, float x) { return ret_f(ld_atan2(y, x), x); }

void sincos(double x, double *s, double *c) {
    if (s) *s = sin(x);
    if (c) *c = cos(x);
}

void sincosf(float x, float *s, float *c) {
    if (s) *s = sinf(x);
    if (c) *c = cosf(x);
}

/* ============ Exponential and logarithmic ============ */

static long double ld_exp(double x) {
    if (is_nan(x)) return x;
    if (x == -__builtin_inf()) return 0.0L;
    return __nxc_expl(x);
}

static long double ld_exp2(double x) {
    if (is_nan(x)) return x;
    if (x == -__builtin_inf()) return 0.0L;
    if (x > 20000.0) return LD_INF;
    if (x < -20000.0) return 0.0L;
    /* split off the integer part: 2^x = 2^n * e^(f ln2), exact for integer x */
    long double xl = x;
    long n = (long)xl;
    long double f = xl - (long double)n;
    long double v = __nxc_expl(f * NXC_LN2_L);
    long double p = 1.0L;
    long double b = (n >= 0) ? 2.0L : 0.5L;
    unsigned long a = (n >= 0) ? (unsigned long)n : 0ul - (unsigned long)n;
    while (a) {
        if (a & 1) p *= b;
        b *= b;
        a >>= 1;
    }
    return v * p;
}

static long double ld_expm1(double x) {
    if (is_nan(x)) return x;
    if (x == -__builtin_inf()) return -1.0L;
    if (x == 0.0) return x;
    return __nxc_expm1l(x);
}

static long double ld_log(double x) {
    if (is_nan(x)) return x;
    if (x < 0) return domain_error();
    if (x == 0.0) {
        errno = ERANGE;             /* pole error */
        return -LD_INF;
    }
    if (is_inf(x)) return x;
    return __nxc_logl(x);
}

static long double ld_log1p(double x) {
    if (is_nan(x)) return x;
    if (x < -1.0) return domain_error();
    if (x == -1.0) {
        errno = ERANGE;
        return -LD_INF;
    }
    if (is_inf(x)) return x;
    if (x == 0.0) return x;
    return __nxc_log1pl(x);
}

static long double ld_log2(double x) {
    long double v = ld_log(x);
    if (is_nan(v) || is_inf(v)) return v;
    return v * NXC_LOG2E_L;
}

static long double ld_log10(double x) {
    long double v = ld_log(x);
    if (is_nan(v) || is_inf(v)) return v;
    return v * NXC_LOG10E_L;
}

double exp(double x) { return ret_d(ld_exp(x), x); }
double exp2(double x) { return ret_d(ld_exp2(x), x); }
double expm1(double x) { return ret_d(ld_expm1(x), x); }
double log(double x) { return ret_d(ld_log(x), x); }
double log1p(double x) { return ret_d(ld_log1p(x), x); }
double log2(double x) { return ret_d(ld_log2(x), x); }
double log10(double x) { return ret_d(ld_log10(x), x); }
float expf(float x) { return ret_f(ld_exp(x), x); }
float exp2f(float x) { return ret_f(ld_exp2(x), x); }
float expm1f(float x) { return ret_f(ld_expm1(x), x); }
float logf(float x) { return ret_f(ld_log(x), x); }
float log1pf(float x) { return ret_f(ld_log1p(x), x); }
float log2f(float x) { return ret_f(ld_log2(x), x); }
float log10f(float x) { return ret_f(ld_log10(x), x); }

/* ============ Hyperbolic ============ */

static long double ld_sinh(double x) {
    if (is_nan(x) || is_inf(x) || x == 0.0) return x;
    long double a = ld_abs(x);
    long double v;
    if (a < 1.0L) {
        long double e = __nxc_expm1l(a);
        v = 0.5L * (e + e / (e + 1.0L));    /* (e^a - e^-a)/2 without cancellation */
    } else {
        long double e = __nxc_expl(a);
        v = 0.5L * (e - 1.0L / e);
    }
    return (x < 0) ? -v : v;
}

static long double ld_cosh(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return LD_INF;
    long double e = __nxc_expl(ld_abs(x));
    return 0.5L * (e + 1.0L / e);
}

static long double ld_tanh(double x) {
    if (is_nan(x) || x == 0.0) return x;
    long double a = ld_abs(x);
    long double v;
    if (a > 23.0L) {
        v = 1.0L;
    } else {
        long double e = __nxc_expm1l(2.0L * a);
        v = e / (e + 2.0L);
    }
    return (x < 0) ? -v : v;
}

double sinh(double x) { return ret_d(ld_sinh(x), x); }
double cosh(double x) { return ret_d(ld_cosh(x), x); }
double tanh(double x) { return ret_d(ld_tanh(x), x); }
float sinhf(float x) { return ret_f(ld_sinh(x), x); }
float coshf(float x) { return ret_f(ld_cosh(x), x); }
float tanhf(float x) { return ret_f(ld_tanh(x), x); }

/* ============ Power ============ */

static int is_integer(double y) {
    if (is_inf(y) || is_nan(y)) return 0;
    if (ld_abs(y) >= 4503599627370496.0L) return 1;     /* >= 2^52: all integers */
    return (double)(long long)y == y;
}

static int is_odd_integer(double y) {
    if (!is_integer(y) || ld_abs(y) >= 9007199254740992.0L) return 0;  /* >= 2^53: even */
    return ((long long)y & 1) != 0;
}

static long double ld_pow(double x, double y) {
    if (y == 0.0) return 1.0L;                      /* even for NaN x */
    if (x == 1.0) return 1.0L;                      /* even for NaN y */
    if (is_nan(x) || is_nan(y)) return (long double)x + (long double)y;
    int y_odd = is_odd_integer(y);
    if (x == 0.0) {
        if (y < 0) {
            errno = ERANGE;                         /* pole error */
            return (y_odd && signbit(x)) ? -LD_INF : LD_INF;
        }
        return (y_odd && signbit(x)) ? -0.0L : 0.0L;
    }
    if (is_inf(y)) {
        long double ax = ld_abs(x);
        if (ax == 1.0L) return 1.0L;
        if ((ax < 1.0L) == (y < 0)) return LD_INF;
        return 0.0L;
    }
    if (is_inf(x)) {
        if (x > 0) return (y > 0) ? LD_INF : 0.0L;
        if (y > 0) return y_odd ? -LD_INF : LD_INF;
        return y_odd ? -0.0L : 0.0L;
    }
    int negate = 0;
    long double ax = x;
    if (x < 0) {
        if (!is_integer(y)) return domain_error();
        negate = y_odd;
        ax = -ax;
    }
    long double v;
    if (is_integer(y) && ld_abs(y) <= 64.0L) {
        /* small integer powers by squaring: exact when the result is */
        long double b = ax, p = 1.0L;
        unsigned n = (unsigned)(long long)ld_abs(y);
        while (n) {
            if (n & 1) p *= b;
            b *= b;
            n >>= 1;
        }
        v = (y < 0) ? 1.0L / p : p;
    } else {
        v = __nxc_powl((double)ax, y);
    }
    return negate ? -v : v;
}

/* pow of finite, non-zero x and finite y is never exactly 0 or infinite,
 * so either result is a range error */
static int pow_range_checked(double x, double y) {
    return x != 0.0 && !is_nan(x) && !is_nan(y) && !is_inf(x) && !is_inf(y);
}

double pow(double x, double y) {
    double d = (double)ld_pow(x, y);
    if (pow_range_checked(x, y) && (is_inf(d) || d == 0.0)) errno = ERANGE;
    return d;
}

float powf(float x, float y) {
    float f = (float)ld_pow(x, y);
    if (pow_range_checked(x, y) && (is_inf(f) || f == 0.0f)) errno = ERANGE;
    return f;
}

static long double ld_cbrt(double x) {
    if (is_nan(x) || is_inf(x) || x == 0.0) return x;
    long double a = ld_abs(x);
    long double y = __nxc_expl(__nxc_logl(a) / 3.0L);
    y -= (y * y * y - a) / (3.0L * y * y);         /* one Newton step */
    return (x < 0) ? -y : y;
}

static long double ld_hypot(double x, double y) {
    if (is_inf(x) || is_inf(y)) return LD_INF;      /* even with a NaN */
    if (is_nan(x) || is_nan(y)) return (long double)x + (long double)y;
    long double a = x, b = y;
    return __nxc_sqrtl(a * a + b * b);              /* no overflow in extended range */
}

double cbrt(double x) { return ret_d(ld_cbrt(x), x); }
/* the argument ret_d checks: infinite only when an input is */
static double hypot_arg(double x, double y) { return (is_inf(x) || is_inf(y)) ? __builtin_inf() : 1.0; }

double hypot(double x, double y) { return ret_d(ld_hypot(x, y), hypot_arg(x, y)); }
float cbrtf(float x) { return ret_f(ld_cbrt(x), x); }
float hypotf(float x, float y) { return ret_f(ld_hypot(x, y), (float)hypot_arg(x, y)); }

double sqrt(double x) {
    if (x < 0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x));
    return x;
}

float sqrtf(float x) {
    if (x < 0) {
        errno = EDOM;
        return __builtin_nanf("");
    }
    __asm__("sqrtss %1, %0" : "=x"(x) : "x"(x));
    return x;
}

double fabs(double x) {
    uint64_t b;
    memcpy(&b, &x, 8);
    b &= ~(1ull << 63);
    memcpy(&x, &b, 8);
    return x;
}

float fabsf(float x) {
    uint32_t b;
    memcpy(&b, &x, 4);
    b &= ~(1u << 31);
    memcpy(&x, &b, 4);
    return x;
}

/* ============ Nearest integer ============ */

double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;  /* >= 2^52, inf or NaN */
    double t = (double)(long long)x;
    return copysign(t, x);
}

double floor(double x) {
    double t = trunc(x);
    return (t > x) ? t - 1.0 : t;
}

double ceil(double x) {
    double t = trunc(x);
    if (t < x) t += 1.0;
    return copysign(t, x);                          /* ceil(-0.5) is -0 */
}

double round(double x) {
    double t = trunc(x);
    double diff = x - t;                            /* exact */
    if (diff >= 0.5) t += 1.0;
    else if (diff <= -0.5) t -= 1.0;
    return copysign(t, x);
}

static long round_to_long(double r) {
    if (r != r || r >= 9223372036854775808.0 || r < -9223372036854775808.0) {
        errno = EDOM;
        return (r < 0) ? LONG_MIN : LONG_MAX;
    }
    return (long)r;
}

long lround(double x) { return round_to_long(round(x)); }
long long llround(double x) { return (long long)round_to_long(round(x)); }

float truncf(float x) { return (float)trunc(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
long lroundf(float x) { return lround(x); }
long long llroundf(float x) { return llround(x); }

/* ============ Remainder and manipulation ============ */

double fmod(double x, double y) {
    if (x != x || y != y) return x + y;
    if (is_inf(x) || y == 0.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (is_inf(y) || x == 0.0) return x;
    long double a = x, b = y;
    /* FPREM gives the exact remainder (same sign as x), a few exponent
     * bits per step; repeat while C2 says it is partial */
    __asm__("1:\n\tfprem\n\tfnstsw %%ax\n\ttestw $0x400, %%ax\n\tjnz 1b"
            : "+t"(a) : "u"(b) : "ax", "cc");
    return (double)a;
}

float fmodf(float x, float y) { return (float)fmod(x, y); }

double copysign(double x, double y) {
    uint64_t bx, by;
    memcpy(&bx, &x, 8);
    memcpy(&by, &y, 8);
    bx = (bx & ~(1ull << 63)) | (by & (1ull << 63));
    memcpy(&x, &bx, 8);
    return x;
}

float copysignf(float x, float y) {
    uint32_t bx, by;
    memcpy(&bx, &x, 4);
    memcpy(&by, &y, 4);
    bx = (bx & ~(1u << 31)) | (by & (1u << 31));
    memcpy(&x, &bx, 4);
    return x;
}

double nan(const char *tag) { (void)tag; return __builtin_nan(""); }
float nanf(const char *tag) { (void)tag; return __builtin_nanf(""); }

double fmax(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == y) return signbit(x) ? y : x;          /* fmax(-0, +0) = +0 */
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == y) return signbit(x) ? x : y;
    return (x < y) ? x : y;
}

double fdim(double x, double y) {
    if (x != x || y != y) return x + y;
    return (x > y) ? x - y : 0.0;
}

float fmaxf(float x, float y) { return (float)fmax(x, y); }
float fminf(float x, float y) { return (float)fmin(x, y); }
float fdimf(float x, float y) { return (float)fdim(x, y); }

double frexp(double x, int *e) {
    int dummy;
    if (!e) e = &dummy;
    *e = 0;
    if (x == 0.0 || x != x || is_inf(x)) return x;
    uint64_t b;
    memcpy(&b, &x, 8);
    int bexp = (int)((b >> 52) & 0x7FF);
    if (bexp == 0) {
        /* subnormal: scale into the normal range first */
        x *= 18014398509481984.0;   /* 2^54 */
        memcpy(&b, &x, 8);
        bexp = (int)((b >> 52) & 0x7FF) - 54;
    }
    *e = bexp - 1022;
    b = (b & ~(0x7FFull << 52)) | (1022ull << 52);  /* [0.5, 1) */
    double m;
    memcpy(&m, &b, 8);
    return m;
}

float frexpf(float x, int *e) { return (float)frexp(x, e); }

double scalbn(double x, int n) {
    if (x == 0.0 || x != x || is_inf(x)) return x;
    if (n > 20000) n = 20000;
    if (n < -20000) n = -20000;
    long double v = x;
    /* exact in extended precision; one rounding to double */
    long double p = 1.0L, b = (n >= 0) ? 2.0L : 0.5L;
    unsigned a = (n >= 0) ? (unsigned)n : (unsigned)(-n);
    while (a) {
        if (a & 1) p *= b;
        b *= b;
        a >>= 1;
    }
    return ret_d(v * p, x);
}

double ldexp(double x, int n) { return scalbn(x, n); }
float scalbnf(float x, int n) { return ret_f((long double)scalbn(x, n), x); }
float ldexpf(float x, int n) { return scalbnf(x, n); }

double modf(double x, double *ip) {
    double t = trunc(x);
    if (ip) *ip = t;
    if (is_inf(x)) return copysign(0.0, x);
    return copysign(x - t, x);
}

float modff(float x, float *ip) {
    double t;
    double f = modf(x, &t);
    if (ip) *ip = (float)t;
    return (float)f;
}
