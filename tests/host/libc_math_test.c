/*
 * nxlibc host test: accuracy of the math library.
 *
 * Unlike the other libc tests this is an ordinary host program: it uses
 * the development machine's long double math functions as the reference
 * (64-bit precision, far more accurate than the double results checked).
 * nxlibc's own math objects are linked in with every symbol renamed to
 * nx_* (objcopy --prefix-symbols), so both libraries live side by side.
 *
 * For every function: random arguments over its useful range, the error
 * in units in the last place (ulp) of the result type, and the ISO C
 * special cases (NaN, infinities, signed zeros, errno for domain and
 * range errors). nxlibc's target is "faithfully rounded": every result is
 * one of the two values next to the exact one, so the error is < 1 ulp.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

/* ---- the nxlibc side ---- */

static int nx_errno_value;
int *nx___nxc_errno_location(void);
int *nx___nxc_errno_location(void) { return &nx_errno_value; }
void *nx_memcpy(void *d, const void *s, size_t n);
void *nx_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
void *nx_memset(void *d, int c, size_t n);
void *nx_memset(void *d, int c, size_t n) { return memset(d, c, n); }

/* nxlibc errno numbers (sdk/libc/include/errno.h) */
#define NX_EDOM   33
#define NX_ERANGE 40

#define DECL1(fn) double nx_##fn(double); float nx_##fn##f(float);
#define DECL2(fn) double nx_##fn(double, double); float nx_##fn##f(float, float);
DECL1(sin) DECL1(cos) DECL1(tan) DECL1(asin) DECL1(acos) DECL1(atan)
DECL1(sinh) DECL1(cosh) DECL1(tanh)
DECL1(exp) DECL1(exp2) DECL1(expm1) DECL1(log) DECL1(log2) DECL1(log10) DECL1(log1p)
DECL1(cbrt) DECL1(sqrt) DECL1(floor) DECL1(ceil) DECL1(trunc) DECL1(round)
DECL2(atan2) DECL2(pow) DECL2(hypot) DECL2(fmod)
double nx_frexp(double, int *);
double nx_ldexp(double, int);
double nx_modf(double, double *);
long nx_lround(double);
void nx_sincos(double, double *, double *);

/* ---- random arguments ---- */

static uint64_t g_seed = 0x6A09E667F3BCC909ull;
static uint64_t rnd(void) {
    g_seed ^= g_seed >> 12;
    g_seed ^= g_seed << 25;
    g_seed ^= g_seed >> 27;
    return g_seed * 0x2545F4914F6CDD1Dull;
}

/* |x| = m * 2^e with e uniform in [elo, ehi]: covers every scale */
static double rnd_log(int elo, int ehi, int allow_neg) {
    uint64_t r = rnd();
    double m = 1.0 + (double)(r >> 12) * 0x1p-52;
    int e = elo + (int)(rnd() % (uint64_t)(ehi - elo + 1));
    double x = ldexp(m, e);
    if (allow_neg && (r & 1)) x = -x;
    return x;
}

static double rnd_lin(double lo, double hi) {
    return lo + (hi - lo) * ((double)(rnd() >> 11) * 0x1p-53);
}

/* ---- error measurement ---- */

static double ulp_err_d(double got, long double want) {
    if (isnan(want)) return isnan(got) ? 0 : 1e9;
    if (isinf(want)) {
        if (got == (double)want) return 0;
        /* the exact value may lie just beyond DBL_MAX */
        if (fabsl(want) < 0) return 1e9;
        return 1e9;
    }
    if (isinf(got)) {
        /* overflow: fine if the exact value rounds past DBL_MAX */
        long double lim = (long double)DBL_MAX + ldexpl(1.0L, 970);
        return (fabsl(want) >= lim && !!signbit(got) == !!signbit(want)) ? 0 : 1e9;
    }
    int e;
    frexpl(want == 0 ? 1e-300L : want, &e);
    int ue = e - 53;
    if (ue < -1074) ue = -1074;
    return (double)(fabsl((long double)got - want) / ldexpl(1.0L, ue));
}

static double ulp_err_f(float got, long double want) {
    if (isnan(want)) return isnan(got) ? 0 : 1e9;
    if (isinf(want)) return (got == (float)want) ? 0 : 1e9;
    if (isinf(got)) {
        long double lim = (long double)FLT_MAX + ldexpl(1.0L, 103);
        return (fabsl(want) >= lim && !!signbit(got) == !!signbit(want)) ? 0 : 1e9;
    }
    int e;
    frexpl(want == 0 ? 1e-40L : want, &e);
    int ue = e - 24;
    if (ue < -149) ue = -149;
    return (double)(fabsl((long double)got - want) / ldexpl(1.0L, ue));
}

static int g_fail, g_count;

typedef struct {
    const char *name;
    double max_err;
    double worst_x, worst_y;
    long n, exact;
} stat_t;

static void stat_add(stat_t *s, double err, double x, double y, int exact) {
    s->n++;
    if (exact) s->exact++;
    if (err > s->max_err) { s->max_err = err; s->worst_x = x; s->worst_y = y; }
}

static void stat_report(const stat_t *s, double limit) {
    g_count++;
    int bad = !(s->max_err < limit);
    if (bad) g_fail++;
    printf("  %-8s max %.3f ulp, %5.1f%% correctly rounded%s", s->name, s->max_err,
           100.0 * (double)s->exact / (double)(s->n ? s->n : 1), bad ? "   FAIL" : "");
    if (bad) printf(" at x=%a y=%a", s->worst_x, s->worst_y);
    printf("\n");
}

#define SAMPLES 100000

typedef double (*fn1_t)(double);
typedef long double (*ref1_t)(long double);
typedef float (*fn1f_t)(float);

static void run1(const char *name, fn1_t f, fn1f_t ff, ref1_t ref, double (*gen)(void)) {
    stat_t s = { name, 0, 0, 0, 0, 0 };
    char fname[16];
    snprintf(fname, sizeof fname, "%sf", name);
    stat_t sf = { fname, 0, 0, 0, 0, 0 };
    for (int i = 0; i < SAMPLES; i++) {
        double x = gen();
        long double w = ref((long double)x);
        double g = f(x);
        stat_add(&s, ulp_err_d(g, w), x, 0, g == (double)w);
        float xf = (float)x;
        if (!isfinite(xf)) continue;
        long double wf = ref((long double)xf);
        float gf = ff(xf);
        stat_add(&sf, ulp_err_f(gf, wf), xf, 0, gf == (float)wf);
    }
    stat_report(&s, 1.0);
    stat_report(&sf, 1.0);
}

typedef double (*fn2_t)(double, double);
typedef long double (*ref2_t)(long double, long double);
typedef float (*fn2f_t)(float, float);

static void run2(const char *name, fn2_t f, fn2f_t ff, ref2_t ref, void (*gen)(double *, double *)) {
    stat_t s = { name, 0, 0, 0, 0, 0 };
    char fname[16];
    snprintf(fname, sizeof fname, "%sf", name);
    stat_t sf = { fname, 0, 0, 0, 0, 0 };
    for (int i = 0; i < SAMPLES; i++) {
        double x, y;
        gen(&x, &y);
        long double w = ref((long double)x, (long double)y);
        double g = f(x, y);
        stat_add(&s, ulp_err_d(g, w), x, y, g == (double)w);
        float xf = (float)x, yf = (float)y;
        if (!isfinite(xf) || !isfinite(yf)) continue;
        long double wf = ref((long double)xf, (long double)yf);
        float gf = ff(xf, yf);
        stat_add(&sf, ulp_err_f(gf, wf), xf, yf, gf == (float)wf);
    }
    stat_report(&s, 1.0);
    stat_report(&sf, 1.0);
}

/* argument generators */
static double g_trig(void)  { return (rnd() & 3) ? rnd_lin(-10.0, 10.0) : rnd_log(-30, 1000, 1); }
static double g_unit(void)  { return rnd_lin(-1.0, 1.0); }
static double g_any(void)   { return rnd_log(-1074, 1023, 1); }
static double g_exp(void)   { return (rnd() & 1) ? rnd_lin(-745.0, 710.0) : rnd_lin(-2.0, 2.0); }
static double g_exp2(void)  { return rnd_lin(-1075.0, 1024.0); }
static double g_expm1(void) { return (rnd() & 1) ? rnd_log(-60, 9, 1) : rnd_lin(-40.0, 709.0); }
static double g_pos(void)   { return (rnd() & 3) ? rnd_log(-1074, 1023, 0) : rnd_lin(0.5, 2.0); }
static double g_log1p(void) { double x = rnd_log(-60, 1023, 1); return (x <= -1.0) ? -rnd_lin(0.0, 1.0) : x; }
static double g_hyp(void)   { return (rnd() & 1) ? rnd_lin(-30.0, 30.0) : rnd_log(-60, 9, 1); }

static void g2_atan2(double *x, double *y) { *x = g_any(); *y = g_any(); }
static void g2_hypot(double *x, double *y) { *x = g_any(); *y = (rnd() & 1) ? g_any() : *x * rnd_lin(0.1, 10); }
static void g2_pow(double *x, double *y) {
    switch (rnd() & 3) {
    case 0: *x = rnd_lin(0.0, 4.0); *y = rnd_lin(-300.0, 300.0); break;
    case 1: *x = rnd_log(-1074, 1023, 0); *y = rnd_lin(-1.0, 1.0); break;
    case 2: *x = -rnd_lin(0.0, 10.0); *y = (double)(int64_t)(rnd() % 600) - 300.0; break;
    default: *x = rnd_lin(0.9, 1.1); *y = rnd_log(-10, 20, 1); break;
    }
}

/* long double references that take the same arguments */
static long double r_exp2(long double x)  { return exp2l(x); }
static long double r_atan2(long double y, long double x) { return atan2l(y, x); }

/* ---- ISO C special cases ---- */

#define SPECIAL(expr, want, err) do { \
    nx_errno_value = 0; \
    double got_ = (expr); \
    double want_ = (want); \
    g_count++; \
    int ok_ = (isnan(want_) ? isnan(got_) : (got_ == want_ && !!signbit(got_) == !!signbit(want_))) \
              && nx_errno_value == (err); \
    if (!ok_) { \
        g_fail++; \
        printf("  FAIL %s = %a (errno %d), want %a (errno %d)\n", #expr, got_, nx_errno_value, \
               want_, (err)); \
    } \
} while (0)

static void test_special(void) {
    const double inf = INFINITY, nan_ = NAN;
    SPECIAL(nx_sqrt(-1.0), nan_, NX_EDOM);
    SPECIAL(nx_sqrt(-0.0), -0.0, 0);
    SPECIAL(nx_sqrt(inf), inf, 0);
    SPECIAL(nx_log(0.0), -inf, NX_ERANGE);
    SPECIAL(nx_log(-1.0), nan_, NX_EDOM);
    SPECIAL(nx_log(1.0), 0.0, 0);
    SPECIAL(nx_log(inf), inf, 0);
    SPECIAL(nx_log2(8.0), 3.0, 0);
    SPECIAL(nx_log10(1000.0), 3.0, 0);
    SPECIAL(nx_log10(1e23), 23.0, 0);
    SPECIAL(nx_log1p(-1.0), -inf, NX_ERANGE);
    SPECIAL(nx_log1p(-2.0), nan_, NX_EDOM);
    SPECIAL(nx_log1p(-0.0), -0.0, 0);
    SPECIAL(nx_exp(0.0), 1.0, 0);
    SPECIAL(nx_exp(1000.0), inf, NX_ERANGE);
    SPECIAL(nx_exp(-1000.0), 0.0, NX_ERANGE);
    SPECIAL(nx_exp(-inf), 0.0, 0);
    SPECIAL(nx_exp(inf), inf, 0);
    SPECIAL(nx_exp(nan_), nan_, 0);
    SPECIAL(nx_exp2(10.0), 1024.0, 0);
    SPECIAL(nx_exp2(-1074.0), 0x1p-1074, 0);
    SPECIAL(nx_expm1(-0.0), -0.0, 0);
    SPECIAL(nx_expm1(-inf), -1.0, 0);
    SPECIAL(nx_sin(-0.0), -0.0, 0);
    SPECIAL(nx_sin(inf), nan_, NX_EDOM);
    SPECIAL(nx_cos(-inf), nan_, NX_EDOM);
    SPECIAL(nx_tan(-0.0), -0.0, 0);
    SPECIAL(nx_asin(2.0), nan_, NX_EDOM);
    SPECIAL(nx_acos(1.0), 0.0, 0);
    SPECIAL(nx_atan(inf), 0x1.921fb54442d18p+0, 0);
    SPECIAL(nx_atan2(0.0, -0.0), 0x1.921fb54442d18p+1, 0);
    SPECIAL(nx_atan2(-0.0, -0.0), -0x1.921fb54442d18p+1, 0);
    SPECIAL(nx_atan2(-0.0, 0.0), -0.0, 0);
    SPECIAL(nx_atan2(inf, -inf), 0x1.2d97c7f3321d2p+1, 0);
    SPECIAL(nx_atan2(1.0, 0.0), 0x1.921fb54442d18p+0, 0);
    SPECIAL(nx_sinh(-0.0), -0.0, 0);
    SPECIAL(nx_sinh(1000.0), inf, NX_ERANGE);
    SPECIAL(nx_cosh(-1000.0), inf, NX_ERANGE);
    SPECIAL(nx_tanh(1000.0), 1.0, 0);
    SPECIAL(nx_tanh(-inf), -1.0, 0);
    SPECIAL(nx_pow(2.0, 10.0), 1024.0, 0);
    SPECIAL(nx_pow(-2.0, 3.0), -8.0, 0);
    SPECIAL(nx_pow(-2.0, 0.5), nan_, NX_EDOM);
    SPECIAL(nx_pow(0.0, -1.0), inf, NX_ERANGE);
    SPECIAL(nx_pow(-0.0, -3.0), -inf, NX_ERANGE);
    SPECIAL(nx_pow(-0.0, 3.0), -0.0, 0);
    SPECIAL(nx_pow(nan_, 0.0), 1.0, 0);
    SPECIAL(nx_pow(1.0, nan_), 1.0, 0);
    SPECIAL(nx_pow(-1.0, inf), 1.0, 0);
    SPECIAL(nx_pow(0.5, inf), 0.0, 0);
    SPECIAL(nx_pow(0.5, -inf), inf, 0);
    SPECIAL(nx_pow(-inf, -3.0), -0.0, 0);
    SPECIAL(nx_pow(-inf, 3.0), -inf, 0);
    SPECIAL(nx_pow(10.0, 400.0), inf, NX_ERANGE);
    SPECIAL(nx_pow(10.0, -400.0), 0.0, NX_ERANGE);
    SPECIAL(nx_pow(10.0, 22.0), 1e22, 0);
    SPECIAL(nx_hypot(inf, nan_), inf, 0);
    SPECIAL(nx_hypot(3.0, 4.0), 5.0, 0);
    SPECIAL(nx_hypot(DBL_MAX, DBL_MAX), inf, NX_ERANGE);
    SPECIAL(nx_cbrt(-27.0), -3.0, 0);
    SPECIAL(nx_cbrt(-0.0), -0.0, 0);
    SPECIAL(nx_fmod(5.5, 2.0), 1.5, 0);
    SPECIAL(nx_fmod(-5.5, 2.0), -1.5, 0);
    SPECIAL(nx_fmod(1.0, 0.0), nan_, NX_EDOM);
    SPECIAL(nx_fmod(inf, 1.0), nan_, NX_EDOM);
    SPECIAL(nx_fmod(1.0, inf), 1.0, 0);
    SPECIAL(nx_fmod(1e300, 3.0), fmod(1e300, 3.0), 0);
    SPECIAL(nx_floor(-0.5), -1.0, 0);
    SPECIAL(nx_floor(-0.0), -0.0, 0);
    SPECIAL(nx_ceil(-0.5), -0.0, 0);
    SPECIAL(nx_ceil(0x1p51 + 0.5), 0x1p51 + 1.0, 0);
    SPECIAL(nx_trunc(-2.7), -2.0, 0);
    SPECIAL(nx_round(2.5), 3.0, 0);
    SPECIAL(nx_round(-2.5), -3.0, 0);
    SPECIAL(nx_round(0.49999999999999994), 0.0, 0);
    SPECIAL(nx_floor(1e300), 1e300, 0);
    SPECIAL((double)nx_lround(-2.5), -3.0, 0);
    SPECIAL(nx_ldexp(1.0, 1024), inf, NX_ERANGE);
    SPECIAL(nx_ldexp(1.0, -1075), 0.0, NX_ERANGE);
    SPECIAL(nx_ldexp(1.5, -1074), 0x1p-1073, 0);  /* rounds to even, exact enough */
    {
        int e = 0;
        double m = nx_frexp(0x1p-1074, &e);
        g_count++;
        if (!(m == 0.5 && e == -1073)) { g_fail++; printf("  FAIL frexp(min subnormal)\n"); }
        double ip = 0;
        double fr = nx_modf(-3.25, &ip);
        g_count++;
        if (!(fr == -0.25 && ip == -3.0)) { g_fail++; printf("  FAIL modf(-3.25)\n"); }
        double s, c;
        nx_sincos(1.0, &s, &c);
        g_count++;
        if (s != nx_sin(1.0) || c != nx_cos(1.0)) { g_fail++; printf("  FAIL sincos\n"); }
    }
}

/* exact operations: every sample must match exactly */
static void test_exact(void) {
    long bad = 0;
    for (int i = 0; i < SAMPLES; i++) {
        double x = g_any(), y = rnd_log(-1074, 1023, 1);
        double ref_mod = (double)fmodl((long double)x, (long double)y);
        if (nx_fmod(x, y) != ref_mod) { if (bad++ < 3) printf("  FAIL fmod(%a, %a)\n", x, y); }
        double p = fabs(x);
        if (nx_sqrt(p) != sqrt(p)) { if (bad++ < 3) printf("  FAIL sqrt(%a)\n", p); }
        float pf = (float)p;
        if (isfinite(pf) && nx_sqrtf(pf) != sqrtf(pf)) { if (bad++ < 3) printf("  FAIL sqrtf(%a)\n", (double)pf); }
        double r = rnd_lin(-1e6, 1e6);
        if (nx_floor(r) != floor(r) || nx_ceil(r) != ceil(r) || nx_trunc(r) != trunc(r) ||
            nx_round(r) != round(r)) {
            if (bad++ < 3) printf("  FAIL rounding of %a\n", r);
        }
    }
    g_count++;
    if (bad) g_fail++;
    printf("  exact ops (fmod, sqrt, floor/ceil/trunc/round): %ld mismatches%s\n", bad, bad ? "   FAIL" : "");
}

int main(void) {
    printf("nxlibc math accuracy (%d samples per function; reference: host long double)\n", SAMPLES);
    run1("sin", nx_sin, nx_sinf, sinl, g_trig);
    run1("cos", nx_cos, nx_cosf, cosl, g_trig);
    run1("tan", nx_tan, nx_tanf, tanl, g_trig);
    run1("asin", nx_asin, nx_asinf, asinl, g_unit);
    run1("acos", nx_acos, nx_acosf, acosl, g_unit);
    run1("atan", nx_atan, nx_atanf, atanl, g_any);
    run1("sinh", nx_sinh, nx_sinhf, sinhl, g_hyp);
    run1("cosh", nx_cosh, nx_coshf, coshl, g_hyp);
    run1("tanh", nx_tanh, nx_tanhf, tanhl, g_hyp);
    run1("exp", nx_exp, nx_expf, expl, g_exp);
    run1("exp2", nx_exp2, nx_exp2f, r_exp2, g_exp2);
    run1("expm1", nx_expm1, nx_expm1f, expm1l, g_expm1);
    run1("log", nx_log, nx_logf, logl, g_pos);
    run1("log2", nx_log2, nx_log2f, log2l, g_pos);
    run1("log10", nx_log10, nx_log10f, log10l, g_pos);
    run1("log1p", nx_log1p, nx_log1pf, log1pl, g_log1p);
    run1("cbrt", nx_cbrt, nx_cbrtf, cbrtl, g_any);
    run2("atan2", nx_atan2, nx_atan2f, r_atan2, g2_atan2);
    run2("hypot", nx_hypot, nx_hypotf, hypotl, g2_hypot);
    run2("pow", nx_pow, nx_powf, powl, g2_pow);
    test_exact();
    test_special();
    printf("[libc_math] %d checks, %d failed: %s\n", g_count, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
