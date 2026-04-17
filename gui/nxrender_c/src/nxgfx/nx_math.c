/*
 * NeolyxOS - NXGFX Vector Engine Math
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx_path.h"

/* Fast float square root override if needed. Using standard for now via linking unless redefined. */
extern float sqrtf(float);
extern float cosf(float);
extern float sinf(float);

float nx_vec2_mag_sq(nx_vec2_t v) {
    return v.x * v.x + v.y * v.y;
}

float nx_vec2_mag(nx_vec2_t v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

nx_vec2_t nx_vec2_normalize(nx_vec2_t v) {
    float mag = nx_vec2_mag(v);
    if (mag > 0.00001f) {
        return (nx_vec2_t){ v.x / mag, v.y / mag };
    }
    return (nx_vec2_t){0, 0};
}

float nx_vec2_dot(nx_vec2_t a, nx_vec2_t b) {
    return a.x * b.x + a.y * b.y;
}

float nx_vec2_cross(nx_vec2_t a, nx_vec2_t b) {
    return a.x * b.y - a.y * b.x;
}

float nx_dist_sq(nx_vec2_t a, nx_vec2_t b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

nx_bounds_t nx_bounds_merge(nx_bounds_t a, nx_bounds_t b) {
    return (nx_bounds_t){
        .min = { a.min.x < b.min.x ? a.min.x : b.min.x, a.min.y < b.min.y ? a.min.y : b.min.y },
        .max = { a.max.x > b.max.x ? a.max.x : b.max.x, a.max.y > b.max.y ? a.max.y : b.max.y }
    };
}

/* Base identity matrix
   | 1 0 0 |
   | 0 1 0 |
*/
nx_transform_t nx_transform_identity(void) {
    return (nx_transform_t){ .m = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f} };
}

/* Multiply matrices A * B */
nx_transform_t nx_transform_multiply(nx_transform_t a, nx_transform_t b) {
    nx_transform_t out;
    out.m[0] = a.m[0] * b.m[0] + a.m[2] * b.m[1];
    out.m[1] = a.m[1] * b.m[0] + a.m[3] * b.m[1];
    out.m[2] = a.m[0] * b.m[2] + a.m[2] * b.m[3];
    out.m[3] = a.m[1] * b.m[2] + a.m[3] * b.m[3];
    out.m[4] = a.m[0] * b.m[4] + a.m[2] * b.m[5] + a.m[4];
    out.m[5] = a.m[1] * b.m[4] + a.m[3] * b.m[5] + a.m[5];
    return out;
}

nx_transform_t nx_transform_translate(nx_transform_t t, float tx, float ty) {
    nx_transform_t trans = nx_transform_identity();
    trans.m[4] = tx; trans.m[5] = ty;
    return nx_transform_multiply(t, trans);
}

nx_transform_t nx_transform_scale(nx_transform_t t, float sx, float sy) {
    nx_transform_t scale = nx_transform_identity();
    scale.m[0] = sx; scale.m[3] = sy;
    return nx_transform_multiply(t, scale);
}

nx_transform_t nx_transform_rotate(nx_transform_t t, float radians) {
    nx_transform_t rot = nx_transform_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    rot.m[0] = c; rot.m[1] = s;
    rot.m[2] = -s; rot.m[3] = c;
    return nx_transform_multiply(t, rot);
}

nx_vec2_t nx_transform_point(nx_transform_t t, nx_vec2_t p) {
    return (nx_vec2_t){
        p.x * t.m[0] + p.y * t.m[2] + t.m[4],
        p.x * t.m[1] + p.y * t.m[3] + t.m[5]
    };
}
