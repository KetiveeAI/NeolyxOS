/*
 * NeolyxOS - NXCanvas math
 *
 * Self-contained elementary functions so the engine does not depend on a
 * libm. Each uses range reduction plus a short polynomial; maximum error
 * is around 1e-6 relative over the documented domain, which is far below
 * what is visible in 8-bit rendering.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

typedef union { float f; uint32_t u; } nxc_fbits_t;

/* Round to nearest integer (ties away from zero) for |v| < 2^23. */
static float round_nearest(float v) {
    return (v >= 0.0f) ? nxc__floorf(v + 0.5f) : -nxc__floorf(-v + 0.5f);
}

/* sin(x) for x in [-pi/2, pi/2] via the Taylor series to x^11. */
static float sin_poly(float x) {
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f +
           x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}

float nxc__sinf(float x) {
    if (!nxc__finitef(x)) return 0.0f;
    /* Reduce to [-pi, pi] with a two-part 2*pi to keep precision. */
    const float two_pi_hi = 6.28318548202514648f;
    const float two_pi_lo = -1.7484555e-7f;
    if (x > 1.0e6f || x < -1.0e6f) return 0.0f;    /* outside useful range */
    float k = round_nearest(x * (1.0f / 6.28318530717958648f));
    x = (x - k * two_pi_hi) - k * two_pi_lo;
    /* Fold into [-pi/2, pi/2] using sin(pi - x) = sin(x). */
    const float half_pi = 1.57079632679489662f;
    const float pi = 3.14159265358979323846f;
    if (x > half_pi) x = pi - x;
    else if (x < -half_pi) x = -pi - x;
    /* Exact at the quarter turns so cos(0) == 1 and sin(pi/2) == 1 bit
     * for bit (shape builders compare generated points for equality). */
    if (x == half_pi) return 1.0f;
    if (x == -half_pi) return -1.0f;
    return sin_poly(x);
}

float nxc__cosf(float x) {
    return nxc__sinf(x + 1.57079632679489662f);
}

/* atan(x) for |x| <= 1. Uses atan(x) = pi/6 + atan((x*sqrt3 - 1)/(x + sqrt3))
 * to bring the argument under tan(pi/12) before the series. */
static float atan_unit(float x) {
    int neg = x < 0.0f;
    if (neg) x = -x;
    float offset = 0.0f;
    if (x > 0.26794919243f) {                 /* tan(pi/12) */
        const float sqrt3 = 1.73205080757f;
        x = (x * sqrt3 - 1.0f) / (x + sqrt3);
        offset = 0.52359877560f;              /* pi/6 */
    }
    float x2 = x * x;
    float r = x * (1.0f + x2 * (-1.0f / 3.0f + x2 * (1.0f / 5.0f + x2 * (-1.0f / 7.0f +
              x2 * (1.0f / 9.0f + x2 * (-1.0f / 11.0f))))));
    r += offset;
    return neg ? -r : r;
}

float nxc__atan2f(float y, float x) {
    if (!nxc__finitef(x) || !nxc__finitef(y)) return 0.0f;
    const float pi = 3.14159265358979323846f;
    const float half_pi = 1.57079632679489662f;
    if (x == 0.0f) {
        if (y > 0.0f) return half_pi;
        if (y < 0.0f) return -half_pi;
        return 0.0f;
    }
    float ax = nxc__absf(x), ay = nxc__absf(y);
    float a;
    if (ay <= ax) a = atan_unit(ay / ax);
    else a = half_pi - atan_unit(ax / ay);
    if (x < 0.0f) a = pi - a;
    return (y < 0.0f) ? -a : a;
}

float nxc__expf(float x) {
    if (x != x) return 0.0f;
    if (x > 88.0f) return 3.4028234e38f;
    if (x < -87.0f) return 0.0f;
    /* ln2 split in two parts so k*ln2 is subtracted without rounding loss. */
    const float ln2_hi = 0.693145751953125f;
    const float ln2_lo = 1.428606765330187e-6f;
    float k = round_nearest(x * 1.44269504089f);
    float r = (x - k * ln2_hi) - k * ln2_lo;
    /* exp(r) for |r| <= ln2/2, Taylor to r^7. */
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6.0f + r * (1.0f / 24.0f +
              r * (1.0f / 120.0f + r * (1.0f / 720.0f + r * (1.0f / 5040.0f)))))));
    int32_t ki = (int32_t)k;
    nxc_fbits_t scale;
    scale.u = (uint32_t)(ki + 127) << 23;      /* 2^k, k in [-126, 127] */
    return p * scale.f;
}

float nxc__logf(float x) {
    if (!(x > 0.0f) || !nxc__finitef(x)) return -3.4028234e38f;
    nxc_fbits_t b;
    b.f = x;
    int32_t e = (int32_t)((b.u >> 23) & 0xFFu) - 127;
    if (e == -127) {                           /* subnormal: renormalize */
        b.f = x * 8388608.0f;                  /* 2^23 */
        e = (int32_t)((b.u >> 23) & 0xFFu) - 127 - 23;
    }
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u;   /* mantissa in [1, 2) */
    float m = b.f;
    if (m > 1.41421356237f) { m *= 0.5f; e += 1; }
    /* log(m) = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.1716 */
    float s = (m - 1.0f) / (m + 1.0f);
    float s2 = s * s;
    float lm = 2.0f * s * (1.0f + s2 * (1.0f / 3.0f + s2 * (1.0f / 5.0f +
               s2 * (1.0f / 7.0f + s2 * (1.0f / 9.0f)))));
    return lm + (float)e * 0.69314718056f;
}

float nxc__powf(float x, float y) {
    if (y == 0.0f) return 1.0f;
    if (!(x > 0.0f)) return 0.0f;
    if (x == 1.0f) return 1.0f;
    return nxc__expf(y * nxc__logf(x));
}
