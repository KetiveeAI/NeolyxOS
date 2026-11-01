/*
 * nxlibc - <math.h>
 *
 * A subset of ISO C11 <math.h>: the functions apps and the desktop use,
 * in double and float forms. The float forms compute in double and round
 * once. Transcendental functions are computed with extended (x87) precision
 * internally; results are within 1 ulp of the exact value across the
 * ranges tests/host/libc_math_test.c checks.
 *
 * Domain errors return NaN and set errno to EDOM; overflow returns
 * +-HUGE_VAL and sets errno to ERANGE (math_errhandling is MATH_ERRNO).
 * Floating-point exceptions are not raised on purpose (no <fenv.h> yet).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_MATH_H
#define NXLIBC_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float  float_t;
typedef double double_t;

#define HUGE_VAL    (__builtin_huge_val())
#define HUGE_VALF   (__builtin_huge_valf())
#define HUGE_VALL   (__builtin_huge_vall())
#define INFINITY    (__builtin_inff())
#define NAN         (__builtin_nanf(""))

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define MATH_ERRNO      1
#define MATH_ERREXCEPT  2
#define math_errhandling MATH_ERRNO

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isfinite(x)   __builtin_isfinite(x)
#define isinf(x)      __builtin_isinf_sign(x)
#define isnan(x)      __builtin_isnan(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)

#define isgreater(x, y)      __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y)         __builtin_isless(x, y)
#define islessequal(x, y)    __builtin_islessequal(x, y)
#define islessgreater(x, y)  __builtin_islessgreater(x, y)
#define isunordered(x, y)    __builtin_isunordered(x, y)

/* Mathematical constants (common extension, exact to double precision) */
#define M_E         2.71828182845904523536
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.434294481903251827651
#define M_LN2       0.693147180559945309417
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.785398163397448309616
#define M_1_PI      0.318309886183790671538
#define M_2_PI      0.636619772367581343076
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.707106781186547524401

/* Trigonometric */
double acos(double x);   float acosf(float x);
double asin(double x);   float asinf(float x);
double atan(double x);   float atanf(float x);
double atan2(double y, double x); float atan2f(float y, float x);
double cos(double x);    float cosf(float x);
double sin(double x);    float sinf(float x);
double tan(double x);    float tanf(float x);

/* Hyperbolic */
double cosh(double x);   float coshf(float x);
double sinh(double x);   float sinhf(float x);
double tanh(double x);   float tanhf(float x);

/* Exponential and logarithmic */
double exp(double x);    float expf(float x);
double exp2(double x);   float exp2f(float x);
double expm1(double x);  float expm1f(float x);
double log(double x);    float logf(float x);
double log10(double x);  float log10f(float x);
double log2(double x);   float log2f(float x);
double log1p(double x);  float log1pf(float x);
double frexp(double x, int *exp);       float frexpf(float x, int *exp);
double ldexp(double x, int exp);        float ldexpf(float x, int exp);
double scalbn(double x, int n);         float scalbnf(float x, int n);
double modf(double x, double *ipart);   float modff(float x, float *ipart);

/* Power and absolute value */
double cbrt(double x);   float cbrtf(float x);
double fabs(double x);   float fabsf(float x);
double hypot(double x, double y);       float hypotf(float x, float y);
double pow(double x, double y);         float powf(float x, float y);
double sqrt(double x);   float sqrtf(float x);

/* Nearest integer */
double ceil(double x);   float ceilf(float x);
double floor(double x);  float floorf(float x);
double trunc(double x);  float truncf(float x);
double round(double x);  float roundf(float x);
long   lround(double x); long lroundf(float x);
long long llround(double x); long long llroundf(float x);

/* Remainder and manipulation */
double fmod(double x, double y);        float fmodf(float x, float y);
double copysign(double x, double y);    float copysignf(float x, float y);
double nan(const char *tag);            float nanf(const char *tag);
double fmax(double x, double y);        float fmaxf(float x, float y);
double fmin(double x, double y);        float fminf(float x, float y);
double fdim(double x, double y);        float fdimf(float x, float y);

/* Used by desktop code: sin and cos at once */
void sincos(double x, double *s, double *c);
void sincosf(float x, float *s, float *c);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_MATH_H */
