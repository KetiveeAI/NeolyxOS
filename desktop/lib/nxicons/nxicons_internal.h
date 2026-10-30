/*
 * NeolyxOS - NXIcons internal helpers
 *
 * Shared by nxicons.c (symbols) and nxapp_icons.c (application icons).
 * The icon library sits above the engine and uses only nxcanvas.h, so it
 * carries its own tiny math: sine/cosine by range reduction plus a short
 * polynomial, atan2 and a hardware square root. Accuracy ~1e-6, plenty
 * for icon geometry.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXICONS_INTERNAL_H
#define NXICONS_INTERNAL_H

#include <stdint.h>
#include "../../include/nxcanvas.h"

#ifndef NXC_PI
#define NXC_PI 3.14159265358979323846f
#endif

static inline int nxi__finite(float v) { return (v == v) && (v - v == 0.0f); }

static inline float nxc_sqrtf_pub(float v) {
    if (!(v > 0.0f)) return 0.0f;
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(v));
    return r;
}

static inline float nxi__floor(float v) {
    float t = (float)(int32_t)v;
    return (t > v) ? t - 1.0f : t;
}

static inline float nxc_sinf_pub(float x) {
    if (!nxi__finite(x)) return 0.0f;
    const float two_pi = 6.28318530717958648f;
    float k = nxi__floor(x / two_pi + 0.5f);
    x -= k * two_pi;                               /* [-pi, pi] */
    const float half_pi = 1.57079632679489662f, pi = 3.14159265358979323846f;
    if (x > half_pi) x = pi - x;
    else if (x < -half_pi) x = -pi - x;
    if (x == half_pi) return 1.0f;
    if (x == -half_pi) return -1.0f;
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f +
           x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}

static inline float nxc_cosf_pub(float x) { return nxc_sinf_pub(x + 1.57079632679489662f); }

static inline float nxi__atan_unit(float x) {
    int neg = x < 0.0f;
    if (neg) x = -x;
    float offset = 0.0f;
    if (x > 0.26794919243f) {
        const float s3 = 1.73205080757f;
        x = (x * s3 - 1.0f) / (x + s3);
        offset = 0.52359877560f;
    }
    float x2 = x * x;
    float r = x * (1.0f + x2 * (-1.0f / 3.0f + x2 * (1.0f / 5.0f + x2 * (-1.0f / 7.0f +
              x2 * (1.0f / 9.0f + x2 * (-1.0f / 11.0f)))))) + offset;
    return neg ? -r : r;
}

static inline float nxc_atan2f_pub(float y, float x) {
    const float pi = 3.14159265358979323846f, half_pi = 1.57079632679489662f;
    if (x == 0.0f) return y > 0.0f ? half_pi : (y < 0.0f ? -half_pi : 0.0f);
    float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y;
    float a = (ay <= ax) ? nxi__atan_unit(ay / ax) : half_pi - nxi__atan_unit(ax / ay);
    if (x < 0.0f) a = pi - a;
    return y < 0.0f ? -a : a;
}

#endif /* NXICONS_INTERNAL_H */
