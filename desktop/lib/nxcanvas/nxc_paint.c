/*
 * NeolyxOS - NXCanvas paints
 *
 * Solid colors and linear/radial gradients. A gradient is baked into a
 * 256-entry premultiplied ramp when prepared; shading then costs one
 * multiply-add per pixel. Optional ordered (Bayer 4x4) dithering picks
 * between neighbouring ramp entries so slow gradients across a large
 * area show no 8-bit banding.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

/* 4x4 Bayer matrix scaled to [0, 1) in 1/16 steps. */
static const float g_bayer4[4][4] = {
    {  0.0f / 16.0f,  8.0f / 16.0f,  2.0f / 16.0f, 10.0f / 16.0f },
    { 12.0f / 16.0f,  4.0f / 16.0f, 14.0f / 16.0f,  6.0f / 16.0f },
    {  3.0f / 16.0f, 11.0f / 16.0f,  1.0f / 16.0f,  9.0f / 16.0f },
    { 15.0f / 16.0f,  7.0f / 16.0f, 13.0f / 16.0f,  5.0f / 16.0f },
};

/* ============ Constructors ============ */

nxc_paint_t nxc_paint_solid(nxc_color_t color) {
    nxc_paint_t p;
    nxc__zero(&p, sizeof(p));
    p.type = NXC_PAINT_SOLID;
    p.color = color;
    return p;
}

nxc_paint_t nxc_paint_linear(float x0, float y0, float x1, float y1,
                             nxc_color_t c0, nxc_color_t c1) {
    nxc_paint_t p;
    nxc__zero(&p, sizeof(p));
    p.type = NXC_PAINT_LINEAR;
    p.p0.x = x0; p.p0.y = y0;
    p.p1.x = x1; p.p1.y = y1;
    p.stops[0].pos = 0.0f; p.stops[0].color = c0;
    p.stops[1].pos = 1.0f; p.stops[1].color = c1;
    p.nstops = 2;
    p.dither = 1;
    return p;
}

nxc_paint_t nxc_paint_radial(float cx, float cy, float radius,
                             nxc_color_t inner, nxc_color_t outer) {
    nxc_paint_t p;
    nxc__zero(&p, sizeof(p));
    p.type = NXC_PAINT_RADIAL;
    p.p0.x = cx; p.p0.y = cy;
    p.radius = radius;
    p.stops[0].pos = 0.0f; p.stops[0].color = inner;
    p.stops[1].pos = 1.0f; p.stops[1].color = outer;
    p.nstops = 2;
    p.dither = 1;
    return p;
}

nxc_status_t nxc_paint_add_stop(nxc_paint_t *p, float pos, nxc_color_t color) {
    if (!p || !nxc__finitef(pos) || p->nstops < 0 || p->nstops > NXC_MAX_STOPS) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_paint_add_stop");
    }
    if (p->nstops == NXC_MAX_STOPS) return nxc__fail(NXC_ERR_OVERFLOW, "nxc_paint_add_stop");
    pos = nxc__clampf(pos, 0.0f, 1.0f);
    int32_t i = p->nstops;
    /* Insertion keeps stops sorted; equal positions keep insertion order
     * so a hard color edge can be expressed with two stops at one pos. */
    while (i > 0 && p->stops[i - 1].pos > pos) {
        p->stops[i] = p->stops[i - 1];
        i--;
    }
    p->stops[i].pos = pos;
    p->stops[i].color = color;
    p->nstops++;
    return NXC_OK;
}

/* ============ Preparation ============ */

/* Premultiplied interpolation of two straight-alpha colors (t in 0..1).
 * Interpolating premultiplied values avoids dark fringes when one end is
 * transparent. */
static uint32_t lerp_premul(nxc_color_t a, nxc_color_t b, float t) {
    uint32_t pa = nxc__premul(a), pb = nxc__premul(b);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float ca = (float)((pa >> shift) & 0xFFu);
        float cb = (float)((pb >> shift) & 0xFFu);
        float v = ca + (cb - ca) * t + 0.5f;
        uint32_t iv = (v <= 0.0f) ? 0u : (v >= 255.0f ? 255u : (uint32_t)v);
        out |= iv << shift;
    }
    /* Keep channels <= alpha (premultiplied invariant) despite rounding. */
    uint32_t al = out >> 24;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t ch = (out >> shift) & 0xFFu;
        if (ch > al) out = (out & ~(0xFFu << shift)) | (al << shift);
    }
    return out;
}

static nxc_status_t build_lut(const nxc_paint_t *p, uint32_t lut[256]) {
    if (p->nstops < 2 || p->nstops > NXC_MAX_STOPS) return NXC_ERR_INVALID_ARG;
    for (int32_t i = 0; i < p->nstops; i++) {
        if (!nxc__finitef(p->stops[i].pos)) return NXC_ERR_INVALID_ARG;
        if (i > 0 && p->stops[i].pos < p->stops[i - 1].pos) return NXC_ERR_INVALID_ARG;
    }
    int32_t s = 0;
    for (int32_t i = 0; i < 256; i++) {
        float t = (float)i / 255.0f;
        while (s < p->nstops - 2 && t > p->stops[s + 1].pos) s++;
        const nxc_stop_t *a = &p->stops[s];
        const nxc_stop_t *b = &p->stops[s + 1];
        float u;
        if (t <= a->pos) u = 0.0f;
        else if (t >= b->pos) u = 1.0f;
        else u = (t - a->pos) / (b->pos - a->pos);
        if (t < p->stops[0].pos) { a = b = &p->stops[0]; u = 0.0f; }
        if (t > p->stops[p->nstops - 1].pos) { a = b = &p->stops[p->nstops - 1]; u = 0.0f; }
        lut[i] = lerp_premul(a->color, b->color, u);
    }
    return NXC_OK;
}

nxc_status_t nxc__paint_prepare(const nxc_paint_t *p, float tx, float ty,
                                nxc__paint_ctx_t *out) {
    if (!p || !out || !nxc__finitef(tx) || !nxc__finitef(ty)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__paint_prepare");
    }
    nxc__zero(out, sizeof(*out));
    out->type = p->type;
    out->dither = p->dither ? 1 : 0;
    switch (p->type) {
        case NXC_PAINT_SOLID:
            out->solid = nxc__premul(p->color);
            return NXC_OK;
        case NXC_PAINT_LINEAR: {
            if (!nxc__finitef(p->p0.x) || !nxc__finitef(p->p0.y) ||
                !nxc__finitef(p->p1.x) || !nxc__finitef(p->p1.y)) {
                return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__paint_prepare");
            }
            float dx = p->p1.x - p->p0.x, dy = p->p1.y - p->p0.y;
            float len2 = dx * dx + dy * dy;
            out->ax = p->p0.x + tx;
            out->ay = p->p0.y + ty;
            out->bx = dx;
            out->by = dy;
            /* Degenerate gradient: every pixel takes the last stop. */
            out->inv_len2 = (len2 > 1e-12f) ? 1.0f / len2 : 0.0f;
            break;
        }
        case NXC_PAINT_RADIAL:
            if (!nxc__finitef(p->p0.x) || !nxc__finitef(p->p0.y) ||
                !nxc__finitef(p->radius) || p->radius < 0.0f) {
                return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__paint_prepare");
            }
            out->ax = p->p0.x + tx;
            out->ay = p->p0.y + ty;
            out->inv_radius = (p->radius > 1e-6f) ? 1.0f / p->radius : 0.0f;
            break;
        default:
            return nxc__fail(NXC_ERR_INVALID_ARG, "nxc__paint_prepare");
    }
    nxc_status_t st = build_lut(p, out->lut);
    if (st != NXC_OK) return nxc__fail(st, "nxc__paint_prepare");
    return NXC_OK;
}

/* ============ Shading ============ */

/* Map gradient parameter t to a ramp entry, dithering the fractional part. */
static inline uint32_t ramp(const nxc__paint_ctx_t *pc, float t, int32_t x, int32_t y) {
    float f = nxc__clampf(t, 0.0f, 1.0f) * 255.0f;
    if (pc->dither) f += g_bayer4[y & 3][x & 3] - 0.46875f;   /* centered offset */
    int32_t i = (int32_t)(f + 0.5f);
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    return pc->lut[i];
}

void nxc__paint_shade(const void *ctx, int32_t x, int32_t y, int32_t n, uint32_t *out) {
    const nxc__paint_ctx_t *pc = (const nxc__paint_ctx_t *)ctx;
    if (!pc || !out || n <= 0) return;
    float py = (float)y + 0.5f;
    switch (pc->type) {
        case NXC_PAINT_SOLID:
            nxc__fill32(out, pc->solid, n);
            return;
        case NXC_PAINT_LINEAR: {
            if (pc->inv_len2 == 0.0f) {
                nxc__fill32(out, pc->lut[255], n);
                return;
            }
            /* t = dot(p - a, b) / |b|^2, stepping by b.x/|b|^2 per pixel. */
            float t = (((float)x + 0.5f - pc->ax) * pc->bx + (py - pc->ay) * pc->by) * pc->inv_len2;
            float dt = pc->bx * pc->inv_len2;
            for (int32_t i = 0; i < n; i++) {
                out[i] = ramp(pc, t, x + i, y);
                t += dt;
            }
            return;
        }
        case NXC_PAINT_RADIAL: {
            float dy = (py - pc->ay) * pc->inv_radius;
            float dy2 = dy * dy;
            float dx = ((float)x + 0.5f - pc->ax) * pc->inv_radius;
            float step = pc->inv_radius;
            for (int32_t i = 0; i < n; i++) {
                float t = (pc->inv_radius == 0.0f) ? 1.0f : nxc__sqrtf(dx * dx + dy2);
                out[i] = ramp(pc, t, x + i, y);
                dx += step;
            }
            return;
        }
        default:
            nxc__fill32(out, 0u, n);
            return;
    }
}
