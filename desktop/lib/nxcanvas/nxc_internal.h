/*
 * NeolyxOS - NXCanvas internal interfaces
 *
 * Shared between the engine's translation units only. Nothing outside
 * desktop/lib/nxcanvas/ may include this header.
 *
 * Module ownership (each file implements the functions listed for it):
 *   nxc_core.c     engine state, errors, scratch, surfaces, canvas state,
 *                  span blending (all pixel writes go through nxc__span_*),
 *                  nxc_draw_mask, color helpers
 *   nxc_math.c     nxc__sinf/cosf/atan2f/expf/logf/powf
 *   nxc_raster.c   paths, flattening, banded coverage rasterizer, stroker,
 *                  nxc_fill_path*, nxc_stroke_path
 *   nxc_shapes.c   analytic rect/rrect/circle/line fills and strokes
 *   nxc_paint.c    paint constructors and the gradient shader
 *   nxc_image.c    scaled surface/RGBA8 drawing
 *   nxc_effects.c  blur, shadow cache, backdrop materials
 *   nxc_font.c     TrueType parsing, cmap, glyph outlines, kerning
 *   nxc_text.c     glyph cache, UTF-8, text measure/draw
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXC_INTERNAL_H
#define NXC_INTERNAL_H

#include "../../include/nxcanvas.h"

/* ============ Engine state & diagnostics ============ */

/* 1 once nxc_init succeeded and until nxc_shutdown. */
int nxc__ready(void);
/* Incremented on every successful nxc_init; modules with lazily built
 * caches compare it to detect a re-init and rebuild. */
uint32_t nxc__generation(void);

/* Allocate/free through the env (16-byte aligned, NULL on failure).
 * Drawing paths must not call these; only init/load/create functions. */
void *nxc__alloc(uint64_t size);
void  nxc__free(void *ptr);

/* Log a line through the env hook (no-op without one). */
void nxc__log(const char *msg);
/* Record an error, log it the first time each code is seen from `where`,
 * and return `status` unchanged so callers can `return nxc__fail(...)`. */
nxc_status_t nxc__fail(nxc_status_t status, const char *where);

/* Max surface dimensions given at init. */
int32_t nxc__max_width(void);
int32_t nxc__max_height(void);

/* ============ Scratch memory (allocated once at init) ============ */

/* Rows per rasterization band. The rasterizer processes tall paths in
 * bands so its accumulation buffer stays bounded. */
#define NXC_BAND_ROWS 32

typedef enum {
    NXC_SCRATCH_ACCUM = 0,   /* float[(max_width + 4) * (NXC_BAND_ROWS + 1)]  raster  */
    NXC_SCRATCH_COV   = 1,   /* uint8_t[max_width + 64]   coverage row          */
    NXC_SCRATCH_COLOR = 2,   /* uint32_t[max_width + 64]  shader output row     */
    NXC_SCRATCH_LINE0 = 3,   /* uint32_t[max(max_w,max_h) * 4 + 64]  blur/image */
    NXC_SCRATCH_LINE1 = 4,   /* uint32_t[max(max_w,max_h) * 4 + 64]  blur/image */
    NXC_SCRATCH_COUNT
} nxc_scratch_id_t;

/* Returns the buffer and its capacity in bytes (never NULL when ready).
 * Buffers are not preserved across public API calls; a module must not
 * hold one while calling into another module that may use the same id
 * (ACCUM: raster only; COV/COLOR: span callers; LINE0/1: effects/image). */
void *nxc__scratch(nxc_scratch_id_t id, uint64_t *capacity_bytes);

/* Cache pools sized by env (owned by nxc_text.c / nxc_effects.c). */
void *nxc__glyph_pool(uint32_t *bytes);
void *nxc__shadow_pool(uint32_t *bytes);

/* ============ Math helpers ============ */

static inline float nxc__minf(float a, float b) { return a < b ? a : b; }
static inline float nxc__maxf(float a, float b) { return a > b ? a : b; }
static inline float nxc__clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float nxc__absf(float v) { return v < 0.0f ? -v : v; }
static inline int32_t nxc__mini(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t nxc__maxi(int32_t a, int32_t b) { return a > b ? a : b; }
/* NaN and infinity checks without libm. */
static inline int nxc__finitef(float v) { return (v == v) && (v - v == 0.0f); }

static inline float nxc__floorf(float v) {
    if (!(v > -8388608.0f && v < 8388608.0f)) return v;   /* |v| >= 2^23 or NaN */
    float t = (float)(int32_t)v;
    return (t > v) ? t - 1.0f : t;
}
static inline float nxc__ceilf(float v) {
    if (!(v > -8388608.0f && v < 8388608.0f)) return v;
    float t = (float)(int32_t)v;
    return (t < v) ? t + 1.0f : t;
}
static inline float nxc__sqrtf(float v) {
    if (!(v > 0.0f)) return 0.0f;
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(v));
    return r;
}

/* Implemented in nxc_math.c (accurate to ~1e-6 over their domains). */
float nxc__sinf(float x);
float nxc__cosf(float x);
float nxc__atan2f(float y, float x);
float nxc__expf(float x);
float nxc__logf(float x);     /* x > 0 */
float nxc__powf(float x, float y);   /* x >= 0 */

#define NXC_PI 3.14159265358979323846f

/* ============ Pixel helpers (premultiplied ARGB32) ============ */

/* Round(x / 255) for x in [0, 65535]. */
static inline uint32_t nxc__div255(uint32_t x) {
    x += 128u;
    return (x + (x >> 8)) >> 8;
}

/* Multiply all four channels of c by a/255 (a in 0..255). */
static inline uint32_t nxc__mul_px(uint32_t c, uint32_t a) {
    uint32_t rb = (c & 0x00FF00FFu) * a + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    uint32_t ag = ((c >> 8) & 0x00FF00FFu) * a + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
    return rb | ag;
}

/* Source-over of premultiplied src onto premultiplied dst. */
static inline uint32_t nxc__over(uint32_t src, uint32_t dst) {
    return src + nxc__mul_px(dst, 255u - (src >> 24));
}

/* Straight-alpha color -> premultiplied pixel. */
static inline uint32_t nxc__premul(nxc_color_t c) {
    uint32_t a = c >> 24;
    if (a == 255u) return c;
    if (a == 0u) return 0u;
    return (nxc__mul_px(c | 0xFF000000u, a) & 0x00FFFFFFu) | (a << 24);
}

/* Fast 32-bit fill/copy (the libc stubs are byte-at-a-time). */
void nxc__fill32(uint32_t *dst, uint32_t value, int32_t count);
void nxc__copy32(uint32_t *dst, const uint32_t *src, int32_t count);
void nxc__zero(void *dst, uint64_t bytes);

/* ============ Span blending (the only way modules write pixels) ============ */
/*
 * All coordinates are DEVICE space (translation already applied). Each
 * function clips to the canvas clip rect, applies the rounded clip and the
 * global alpha, and blends src-over into c->target. Coverage is 0..255.
 *
 * R2: coverage is geometric (how much of the pixel a shape covers), so on
 * opaque pixels nxc__span_solid, nxc__span_cov, nxc__span_pixels and
 * nxc__span_shade mix partly covered pixels in linear light. Alpha (paint
 * and global alpha) still composites in sRGB, so fully covered pixels are
 * unchanged. The _gamma variants keep the plain sRGB blend for coverage
 * that is not geometry: text (which has its own per-polarity correction),
 * blurred shadows and caller masks.
 */

/* Constant color, constant coverage over [x0, x1). */
void nxc__span_solid(nxc_canvas_t *c, int32_t y, int32_t x0, int32_t x1,
                     uint32_t premul, uint32_t coverage);
/* Constant color, per-pixel coverage cov[0..n) starting at x0. */
void nxc__span_cov(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                   int32_t n, uint32_t premul);
/* As nxc__span_solid / nxc__span_cov, blending coverage in sRGB (pre-R2
 * behavior, bit for bit). For text, shadows and masks. */
void nxc__span_solid_gamma(nxc_canvas_t *c, int32_t y, int32_t x0, int32_t x1,
                           uint32_t premul, uint32_t coverage);
void nxc__span_cov_gamma(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                         int32_t n, uint32_t premul);
/* Per-pixel premultiplied colors src[0..n) with optional per-pixel
 * coverage (NULL = 255) and a constant extra coverage k (0..255). */
void nxc__span_pixels(nxc_canvas_t *c, int32_t y, int32_t x0, const uint32_t *src,
                      const uint8_t *cov, int32_t n, uint32_t k);

/* Shader: writes n premultiplied colors for device pixels (x..x+n-1, y). */
typedef void (*nxc__shade_fn)(const void *ctx, int32_t x, int32_t y,
                              int32_t n, uint32_t *out);
/* Per-pixel shader with optional per-pixel coverage (NULL = 255).
 * Uses NXC_SCRATCH_COLOR internally. */
void nxc__span_shade(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                     int32_t n, nxc__shade_fn fn, const void *ctx);

/* Effective device clip of the canvas (rect clip intersected with target
 * bounds and the rounded clip's bounds). Empty when w or h is 0. */
nxc_irect_t nxc__device_clip(const nxc_canvas_t *c);

/* ============ Raster (nxc_raster.c) ============ */

/* Receives one row of coverage: cov[0..n) for device pixels x0..x0+n-1. */
typedef void (*nxc__cov_sink_fn)(void *ctx, int32_t y, int32_t x0,
                                 const uint8_t *cov, int32_t n);

/* Rasterize a path with exact-area anti-aliasing. Points are offset by
 * (tx, ty) then scaled by `scale` about the origin before rasterizing:
 * device = (p.x * scale + tx, p.y * scale + ty). Rows/columns outside
 * `clip` are skipped. Uses NXC_SCRATCH_ACCUM and NXC_SCRATCH_COV. */
nxc_status_t nxc__raster_path(const nxc_path_t *p, float scale, float tx, float ty,
                              nxc_fill_rule_t rule, nxc_irect_t clip,
                              nxc__cov_sink_fn sink, void *sink_ctx);

/* ============ Paint (nxc_paint.c) ============ */

typedef struct {
    nxc_paint_type_t type;
    uint32_t solid;                    /* premultiplied */
    float    ax, ay, bx, by;           /* device-space gradient geometry */
    float    inv_len2;                 /* linear: 1/|p1-p0|^2 */
    float    inv_radius;               /* radial */
    uint32_t lut[256];                 /* premultiplied gradient ramp */
    int32_t  dither;
} nxc__paint_ctx_t;

/* Prepare a paint for device space (adds translation tx, ty). */
nxc_status_t nxc__paint_prepare(const nxc_paint_t *p, float tx, float ty,
                                nxc__paint_ctx_t *out);
/* nxc__shade_fn for a prepared paint. */
void nxc__paint_shade(const void *ctx, int32_t x, int32_t y, int32_t n, uint32_t *out);

#endif /* NXC_INTERNAL_H */
