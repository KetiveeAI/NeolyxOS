/*
 * NeolyxOS - NXCanvas effects
 *
 * Gaussian blur of surfaces and masks, cached soft shadows of rounded
 * rects and frosted-glass backdrop materials.
 *
 * Blur
 *   A Gaussian of standard deviation sigma is approximated by three
 *   successive box filters per axis. Each box is an "extended" box: an
 *   integer box plus two fractional end taps, sized so the three passes
 *   add up to variance sigma^2 exactly. The blur therefore changes
 *   continuously as sigma animates instead of jumping between integer box
 *   widths. Every pass is a running sum (O(1) per pixel for any sigma) on
 *   premultiplied channels in 8.8 fixed point, clamping at the region
 *   edges. Rows and columns are staged in NXC_SCRATCH_LINE0/LINE1.
 *   Sigma is capped at FX_SIGMA_MAX (64 px).
 *
 * Shadows
 *   The blurred mask of the (spread) rounded rect is built once and kept
 *   in the shadow pool, keyed by quantized radius, sigma and extents, with
 *   LRU eviction. Along an axis that is long compared with radius + blur,
 *   the mask does not depend on the extent: only the near end is stored
 *   (a corner tile whose last row/column is the 1D edge profile) and the
 *   far end is its mirror, so a 1100x750 window shadow needs one small
 *   tile shared by every window with the same radius and sigma (nine-
 *   slice; the interior is a solid span). Masks for large sigma are built
 *   at 1/2..1/8 resolution and sampled bilinearly: the blurred mask is
 *   smooth at that scale, so this stays well under one 8-bit step while
 *   costing 4..64x less memory and build time. Fractional positions and
 *   offsets are handled by the bilinear sampling, not by the cache key.
 *
 * Backdrop
 *   Fills a rounded rect with the blurred backdrop (a caller-provided
 *   pre-blurred copy, or the target blurred live through working memory
 *   taken from the shadow pool), then applies saturation around
 *   luminance, the tint, deterministic grain and a 1px inner highlight.
 *   The rounded clip anti-aliases the outline.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"

/* Largest sigma honored by every effect; larger requests are clamped. The
 * three passes then reach ~190 px each way, beyond which a blur reads as
 * a flat average, and every running sum stays far inside 32 bits. */
#define FX_SIGMA_MAX        64.0f
/* Coordinates beyond this magnitude are rejected (as in nxc_clip_rect). */
#define FX_COORD_MAX        1.0e8f
/* Blocks (cached masks + working memory) tracked in the shadow pool. */
#define FX_SLOTS            32
/* Alignment of blocks inside the pool. */
#define FX_ALIGN            16u
/* Material limits. */
#define FX_SATURATION_MAX   8.0f
#define FX_NOISE_MAX        16u
/* Largest surface/mask side accepted (same limit as surfaces). */
#define FX_DIM_MAX          16384

/* ============ Small helpers ============ */

static inline int64_t fx_floor_d(double v) {
    int64_t t = (int64_t)v;
    return ((double)t > v) ? t - 1 : t;
}

static inline int64_t fx_ceil_d(double v) {
    int64_t t = (int64_t)v;
    return ((double)t < v) ? t + 1 : t;
}

static inline int fx_coord_ok(float v) {
    return nxc__finitef(v) && v >= -FX_COORD_MAX && v <= FX_COORD_MAX;
}

static int fx_surface_ok(const nxc_surface_t *s) {
    return s && s->pixels && s->width > 0 && s->height > 0 && s->stride >= s->width &&
           s->width <= FX_DIM_MAX && s->height <= FX_DIM_MAX;
}

static int fx_rect_ok(nxc_rect_t r) {
    return fx_coord_ok(r.x) && fx_coord_ok(r.y) && fx_coord_ok(r.w) && fx_coord_ok(r.h) &&
           r.w >= 0.0f && r.h >= 0.0f;
}

/* Intersect two integer rects; w/h are 0 when empty. */
static nxc_irect_t fx_intersect(nxc_irect_t a, nxc_irect_t b) {
    int64_t x0 = nxc__maxi(a.x, b.x), y0 = nxc__maxi(a.y, b.y);
    int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    int64_t x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) return nxc_irect((int32_t)x0, (int32_t)y0, 0, 0);
    return nxc_irect((int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0));
}

/* Both line buffers and their common capacity in uint32 elements. */
static int fx_lines(uint32_t **l0, uint32_t **l1, int64_t *cap) {
    uint64_t c0 = 0, c1 = 0;
    *l0 = (uint32_t *)nxc__scratch(NXC_SCRATCH_LINE0, &c0);
    *l1 = (uint32_t *)nxc__scratch(NXC_SCRATCH_LINE1, &c1);
    *cap = (int64_t)((c0 < c1 ? c0 : c1) / sizeof(uint32_t));
    return *l0 && *l1 && *cap > 0;
}

/* ============ Box approximation of a Gaussian ============ */

typedef struct {
    int32_t  r;         /* integer half width of each box */
    uint32_t aq;        /* weight of the two fractional end taps, in 1/256 */
    uint64_t recip;     /* round(2^32 / (256 * (2r + 1) + 2 * aq)) */
    int32_t  support;   /* pixels the three passes reach on each side */
    int32_t  small;     /* 1: sigma < 1, single pass with the 5 taps below */
    uint32_t g[3];      /* small mode: center, +-1, +-2 weights in 1/65536 */
} fx_box_t;

/* Below this sigma three boxes are a poor Gaussian; sample it directly. */
#define FX_SMALL_SIGMA 1.0f

/*
 * Derive the extended box shared by the three passes. Each pass must add
 * variance v = sigma^2 / 3. An integer box of half width r has variance
 * r(r + 1) / 3; end taps of weight a at +-(r + 1) raise it continuously
 * to that of the next integer box. Solving
 *     v (2r + 1 + 2a) = r(r + 1)(2r + 1) / 3 + 2a (r + 1)^2
 * for a gives the end-tap weight. Returns 0 for an identity kernel.
 */
static int fx_box_init(float sigma, fx_box_t *b) {
    b->r = 0;
    b->aq = 0;
    b->support = 0;
    b->small = 0;
    b->recip = ((uint64_t)1 << 32) / 256u;
    if (!(sigma > 0.0f)) return 0;
    if (sigma > FX_SIGMA_MAX) sigma = FX_SIGMA_MAX;
    if (sigma < FX_SMALL_SIGMA) {
        /* Direct 5-tap Gaussian. Each tap is the Gaussian integrated over
         * its pixel (16 midpoint samples), not sampled at the center, so a
         * blurred edge matches the continuous model even at sigma 0.5. */
        float inv = -0.5f / (sigma * sigma);
        float w[3];
        for (int32_t k = 0; k < 3; k++) {
            float acc = 0.0f;
            for (int32_t j = 0; j < 16; j++) {
                float x = (float)k - 0.5f + ((float)j + 0.5f) / 16.0f;
                acc += nxc__expf(x * x * inv);
            }
            w[k] = acc / 16.0f;
        }
        float norm = 65536.0f / (w[0] + 2.0f * w[1] + 2.0f * w[2]);
        b->g[1] = (uint32_t)(w[1] * norm + 0.5f);
        b->g[2] = (uint32_t)(w[2] * norm + 0.5f);
        b->g[0] = 65536u - 2u * b->g[1] - 2u * b->g[2];
        b->small = 1;
        b->support = 2;
        return 1;
    }
    float v = sigma * sigma * (1.0f / 3.0f);
    int32_t r = (int32_t)((nxc__sqrtf(1.0f + 12.0f * v) - 1.0f) * 0.5f);
    if (r < 0) r = 0;
    /* Settle float rounding: largest r with r(r + 1) / 3 <= v. */
    while (r > 0 && (float)r * (float)(r + 1) > 3.0f * v) r--;
    while ((float)(r + 1) * (float)(r + 2) <= 3.0f * v) r++;
    float fr = (float)r;
    float a = (2.0f * fr + 1.0f) * (fr * (fr + 1.0f) / 3.0f - v) /
              (2.0f * (v - (fr + 1.0f) * (fr + 1.0f)));
    int32_t aq = (int32_t)(a * 256.0f + 0.5f);
    if (aq < 0) aq = 0;
    if (aq >= 256) { r++; aq = 0; }
    if (r == 0 && aq == 0) return 0;
    uint64_t d = 256u * (2u * (uint64_t)r + 1u) + 2u * (uint64_t)aq;
    b->r = r;
    b->aq = (uint32_t)aq;
    b->recip = (((uint64_t)1 << 32) + d / 2u) / d;
    b->support = 3 * (r + (aq ? 1 : 0));
    return 1;
}

/*
 * One extended-box pass over n samples of nch interleaved channels, edge
 * clamped. Values are 8.8 fixed point (<= 65280). The window sum slides by
 * one add and one subtract per sample; the normalization is a reciprocal
 * multiply. Output is monotonic in the input, so premultiplied channels
 * never exceed alpha after rounding.
 */
static inline void fx_box_pass(const uint32_t *in, uint32_t *out, int32_t n, int32_t nch,
                               const fx_box_t *b) {
    const int32_t r = b->r, last = n - 1;
    const uint64_t aq = b->aq, recip = b->recip;
    uint32_t s[4] = { 0u, 0u, 0u, 0u };
    for (int32_t ch = 0; ch < nch; ch++) {
        uint32_t acc = in[ch] * (uint32_t)(r + 1);
        for (int32_t k = 1; k <= r; k++) acc += in[(int64_t)(k < last ? k : last) * nch + ch];
        s[ch] = acc;
    }
    for (int32_t i = 0; i < n; i++) {
        int32_t il = i - r - 1, ir = i + r + 1, io = i - r;
        const uint32_t *pl = in + (int64_t)(il < 0 ? 0 : il) * nch;
        const uint32_t *pr = in + (int64_t)(ir > last ? last : ir) * nch;
        const uint32_t *po = in + (int64_t)(io < 0 ? 0 : io) * nch;
        uint32_t *o = out + (int64_t)i * nch;
        for (int32_t ch = 0; ch < nch; ch++) {
            uint64_t num = (uint64_t)s[ch] * 256u + aq * (uint64_t)(pl[ch] + pr[ch]);
            o[ch] = (uint32_t)((num * recip + 0x80000000u) >> 32);
            s[ch] += pr[ch] - po[ch];          /* exact modulo 2^32, stays >= 0 */
        }
    }
}

/* Small-sigma pass: 5 Gaussian taps, edge clamped, 8.8 in and out. */
static void fx_small_pass(const uint32_t *in, uint32_t *out, int32_t n, int32_t nch,
                          const fx_box_t *b) {
    const int32_t last = n - 1;
    for (int32_t i = 0; i < n; i++) {
        int32_t i1 = i - 1 < 0 ? 0 : i - 1, i2 = i - 2 < 0 ? 0 : i - 2;
        int32_t j1 = i + 1 > last ? last : i + 1, j2 = i + 2 > last ? last : i + 2;
        for (int32_t ch = 0; ch < nch; ch++) {
            uint64_t acc = (uint64_t)in[(int64_t)i * nch + ch] * b->g[0]
                         + (uint64_t)(in[(int64_t)i1 * nch + ch] + in[(int64_t)j1 * nch + ch]) * b->g[1]
                         + (uint64_t)(in[(int64_t)i2 * nch + ch] + in[(int64_t)j2 * nch + ch]) * b->g[2];
            out[(int64_t)i * nch + ch] = (uint32_t)((acc + 32768u) >> 16);
        }
    }
}

/* Three passes over a single-channel line: l0 -> l1 -> l0 -> l1 (the
 * small-sigma kernel runs once and still ends in l1). */
static void fx_blur_line1(uint32_t *l0, uint32_t *l1, int32_t n, const fx_box_t *b) {
    if (b->small) { fx_small_pass(l0, l1, n, 1, b); return; }
    fx_box_pass(l0, l1, n, 1, b);
    fx_box_pass(l1, l0, n, 1, b);
    fx_box_pass(l0, l1, n, 1, b);
}

/* Three passes over a four-channel line: l0 -> l1 -> l0 -> l1. */
static void fx_blur_line4(uint32_t *l0, uint32_t *l1, int32_t n, const fx_box_t *b) {
    if (b->small) { fx_small_pass(l0, l1, n, 4, b); return; }
    fx_box_pass(l0, l1, n, 4, b);
    fx_box_pass(l1, l0, n, 4, b);
    fx_box_pass(l0, l1, n, 4, b);
}

/* 8.8 fixed point -> 0..255, rounded. */
static inline uint32_t fx_pack8(uint32_t v) {
    v = (v + 128u) >> 8;
    return v > 255u ? 255u : v;
}

/* Spread n pixels (every `step` pixels apart) into 4 fixed-point channels. */
static void fx_unpack32(const uint32_t *p, int64_t step, int32_t n, uint32_t *l) {
    for (int32_t i = 0; i < n; i++) {
        uint32_t v = p[(int64_t)i * step];
        uint32_t *o = l + (int64_t)i * 4;
        o[0] = (v & 0xFFu) << 8;
        o[1] = ((v >> 8) & 0xFFu) << 8;
        o[2] = ((v >> 16) & 0xFFu) << 8;
        o[3] = (v >> 24) << 8;
    }
}

/* Inverse of fx_unpack32; opaque targets keep alpha at 0xFF. */
static void fx_pack32(const uint32_t *l, uint32_t *p, int64_t step, int32_t n, int opaque) {
    for (int32_t i = 0; i < n; i++) {
        const uint32_t *v = l + (int64_t)i * 4;
        uint32_t a = opaque ? 255u : fx_pack8(v[3]);
        p[(int64_t)i * step] = (a << 24) | (fx_pack8(v[2]) << 16) | (fx_pack8(v[1]) << 8) |
                               fx_pack8(v[0]);
    }
}

/* Horizontal blur of the rows of rect `in` (clamped at its left and right
 * edges); only columns [ox0, ox1) of each row are written back. */
static void fx_blur_rows32(uint32_t *px, int32_t stride, nxc_irect_t in, int32_t ox0,
                           int32_t ox1, const fx_box_t *b, uint32_t *l0, uint32_t *l1,
                           int opaque) {
    for (int32_t y = in.y; y < in.y + in.h; y++) {
        uint32_t *row = px + (int64_t)y * stride + in.x;
        fx_unpack32(row, 1, in.w, l0);
        fx_blur_line4(l0, l1, in.w, b);
        fx_pack32(l1 + (int64_t)(ox0 - in.x) * 4, row + (ox0 - in.x), 1, ox1 - ox0, opaque);
    }
}

/* Vertical blur of the columns of rect `in` (clamped at its top and bottom
 * edges); only rows [oy0, oy1) of each column are written back. */
static void fx_blur_cols32(uint32_t *px, int32_t stride, nxc_irect_t in, int32_t oy0,
                           int32_t oy1, const fx_box_t *b, uint32_t *l0, uint32_t *l1,
                           int opaque) {
    for (int32_t x = in.x; x < in.x + in.w; x++) {
        uint32_t *col = px + (int64_t)in.y * stride + x;
        fx_unpack32(col, stride, in.h, l0);
        fx_blur_line4(l0, l1, in.h, b);
        fx_pack32(l1 + (int64_t)(oy0 - in.y) * 4, col + (int64_t)(oy0 - in.y) * stride,
                  stride, oy1 - oy0, opaque);
    }
}

/* Blur an 8-bit mask in place, rows then columns. The caller checked that
 * w and h fit the line buffers. */
static void fx_blur_mask_core(uint8_t *mask, int32_t w, int32_t h, int32_t stride,
                              const fx_box_t *b, uint32_t *l0, uint32_t *l1) {
    for (int32_t y = 0; y < h; y++) {
        uint8_t *row = mask + (int64_t)y * stride;
        for (int32_t i = 0; i < w; i++) l0[i] = (uint32_t)row[i] << 8;
        fx_blur_line1(l0, l1, w, b);
        for (int32_t i = 0; i < w; i++) row[i] = (uint8_t)fx_pack8(l1[i]);
    }
    for (int32_t x = 0; x < w; x++) {
        uint8_t *col = mask + x;
        for (int32_t i = 0; i < h; i++) l0[i] = (uint32_t)col[(int64_t)i * stride] << 8;
        fx_blur_line1(l0, l1, h, b);
        for (int32_t i = 0; i < h; i++) col[(int64_t)i * stride] = (uint8_t)fx_pack8(l1[i]);
    }
}

/* ============ Public blur API ============ */

nxc_status_t nxc_blur_surface(nxc_surface_t *s, nxc_irect_t region, float sigma) {
    static const char where[] = "nxc_blur_surface";
    if (!fx_surface_ok(s) || region.w < 0 || region.h < 0 || !nxc__finitef(sigma)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    fx_box_t b;
    if (!fx_box_init(sigma, &b)) return NXC_OK;            /* sigma <= 0: no-op */
    nxc_irect_t r = fx_intersect(region, nxc_irect(0, 0, s->width, s->height));
    if (r.w == 0) return NXC_OK;
    uint32_t *l0, *l1;
    int64_t cap;
    if (!fx_lines(&l0, &l1, &cap)) return nxc__fail(NXC_ERR_STATE, where);
    if ((int64_t)r.w * 4 > cap || (int64_t)r.h * 4 > cap) {
        return nxc__fail(NXC_ERR_OVERFLOW, where);
    }
    int opaque = (s->flags & NXC_SURFACE_OPAQUE) != 0;
    fx_blur_rows32(s->pixels, s->stride, r, r.x, r.x + r.w, &b, l0, l1, opaque);
    fx_blur_cols32(s->pixels, s->stride, r, r.y, r.y + r.h, &b, l0, l1, opaque);
    return NXC_OK;
}

nxc_status_t nxc_blur_mask(uint8_t *mask, int32_t w, int32_t h, int32_t stride, float sigma) {
    static const char where[] = "nxc_blur_mask";
    if (!mask || w <= 0 || h <= 0 || stride < w || w > FX_DIM_MAX || h > FX_DIM_MAX ||
        !nxc__finitef(sigma)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    fx_box_t b;
    if (!fx_box_init(sigma, &b)) return NXC_OK;
    uint32_t *l0, *l1;
    int64_t cap;
    if (!fx_lines(&l0, &l1, &cap)) return nxc__fail(NXC_ERR_STATE, where);
    if ((int64_t)w > cap || (int64_t)h > cap) return nxc__fail(NXC_ERR_OVERFLOW, where);
    fx_blur_mask_core(mask, w, h, stride, &b, l0, l1);
    return NXC_OK;
}

/* ============ Shadow pool: blocks with LRU eviction ============ */

typedef struct {
    int32_t  key[4];     /* radius*4, sigma*4, width*4 or -1 (long), height*4 or -1 */
    uint32_t off;        /* byte offset in the shadow pool */
    uint32_t bytes;      /* block size, FX_ALIGN multiple */
    uint32_t stamp;      /* last use, for LRU eviction */
    int32_t  tw, th;     /* mask size in texels */
    uint8_t  live;       /* block in use */
    uint8_t  temp;       /* working memory, not a cached mask */
} fx_slot_t;

static struct {
    uint32_t  gen;       /* nxc__generation() the slots belong to */
    uint32_t  clock;     /* LRU clock */
    fx_slot_t slot[FX_SLOTS];
} g_fx;

/* Forget every block when the engine was re-initialized (new pool). */
static void fx_cache_sync(void) {
    uint32_t gen = nxc__generation();
    if (g_fx.gen != gen) {
        nxc__zero(&g_fx, sizeof(g_fx));
        g_fx.gen = gen;
    }
}

/* Next LRU timestamp; renormalizes instead of wrapping. */
static uint32_t fx_tick(void) {
    if (g_fx.clock == 0xFFFFFFFFu) {
        for (int32_t i = 0; i < FX_SLOTS; i++) g_fx.slot[i].stamp = 0;
        g_fx.clock = 0;
    }
    return ++g_fx.clock;
}

/* Evict the least recently used cached mask. Returns 0 when none is left. */
static int fx_evict_lru(void) {
    int32_t best = -1;
    for (int32_t i = 0; i < FX_SLOTS; i++) {
        const fx_slot_t *e = &g_fx.slot[i];
        if (!e->live || e->temp) continue;
        if (best < 0 || e->stamp < g_fx.slot[best].stamp) best = i;
    }
    if (best < 0) return 0;
    g_fx.slot[best].live = 0;
    return 1;
}

/* Find a free range of `bytes` among the live blocks: the lowest one for
 * masks, the highest for working memory (keeps the two apart). */
static int fx_find_gap(uint32_t bytes, uint32_t pool_bytes, int from_top, uint32_t *off_out) {
    int32_t order[FX_SLOTS];
    int32_t n = 0;
    for (int32_t i = 0; i < FX_SLOTS; i++) {
        if (!g_fx.slot[i].live) continue;
        int32_t k = n++;
        while (k > 0 && g_fx.slot[order[k - 1]].off > g_fx.slot[i].off) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    uint32_t limit = pool_bytes & ~(FX_ALIGN - 1u);
    if (!from_top) {
        uint32_t pos = 0;
        for (int32_t k = 0; k < n; k++) {
            const fx_slot_t *e = &g_fx.slot[order[k]];
            if (e->off >= pos && e->off - pos >= bytes) { *off_out = pos; return 1; }
            if (e->off + e->bytes > pos) pos = e->off + e->bytes;
        }
        if (limit >= pos && limit - pos >= bytes) { *off_out = pos; return 1; }
        return 0;
    }
    uint32_t end = limit;
    for (int32_t k = n - 1; k >= 0; k--) {
        const fx_slot_t *e = &g_fx.slot[order[k]];
        uint32_t top = e->off + e->bytes;
        if (end >= top && end - top >= bytes) { *off_out = end - bytes; return 1; }
        if (e->off < end) end = e->off;
    }
    if (end >= bytes) { *off_out = end - bytes; return 1; }
    return 0;
}

/* Reserve a pool block, evicting least recently used masks as needed.
 * Returns the slot index, or -1 when the block cannot fit. */
static int32_t fx_pool_alloc(uint32_t bytes, int temp, uint32_t pool_bytes) {
    for (int32_t attempt = 0; attempt <= FX_SLOTS; attempt++) {
        int32_t free_i = -1;
        for (int32_t i = 0; i < FX_SLOTS; i++) {
            if (!g_fx.slot[i].live) { free_i = i; break; }
        }
        uint32_t off = 0;
        if (free_i >= 0 && fx_find_gap(bytes, pool_bytes, temp, &off)) {
            fx_slot_t *e = &g_fx.slot[free_i];
            nxc__zero(e, sizeof(*e));
            e->live = 1;
            e->temp = (uint8_t)(temp ? 1 : 0);
            e->off = off;
            e->bytes = bytes;
            e->stamp = fx_tick();
            return free_i;
        }
        if (!fx_evict_lru()) return -1;
    }
    return -1;
}

/* Cached mask with this key, or -1. */
static int32_t fx_cache_find(const int32_t key[4]) {
    for (int32_t i = 0; i < FX_SLOTS; i++) {
        const fx_slot_t *e = &g_fx.slot[i];
        if (e->live && !e->temp && e->key[0] == key[0] && e->key[1] == key[1] &&
            e->key[2] == key[2] && e->key[3] == key[3]) {
            return i;
        }
    }
    return -1;
}

/* ============ Shadow masks ============ */

/* Everything needed to build or find one shadow mask. All values derive
 * from the quantized key, so a cache hit reproduces the build exactly. */
typedef struct {
    int32_t  key[4];
    int32_t  ds;            /* device pixels per texel (1, 2, 4, 8) */
    int32_t  m;             /* margin around the shape, texels */
    int32_t  tw, th;        /* mask size, texels */
    int32_t  long_x, long_y;
    float    rt;            /* corner radius, texels */
    float    wt, ht;        /* shape extents, texels (short axes only) */
    fx_box_t box;           /* blur in texel units */
} fx_shadow_t;

/*
 * Quantize the shadow parameters (1/4 px) and lay out its mask. An axis
 * is "long" when its two ends cannot interact through the blur: then the
 * mask stores only the near end (corner radius + 2 margins + guard) and
 * the extent is left out of the key.
 */
static void fx_shadow_plan(float w, float h, float rad, float sigma, fx_shadow_t *p) {
    int32_t rq4 = (int32_t)(rad * 4.0f + 0.5f);
    int32_t sq4 = (int32_t)(sigma * 4.0f + 0.5f);
    float sq = (float)sq4 * 0.25f;
    p->ds = (sq >= 24.0f) ? 8 : (sq >= 12.0f) ? 4 : (sq >= 6.0f) ? 2 : 1;
    float st = sq / (float)p->ds;
    if (p->ds > 1) {
        /* Rasterizing into ds-sized texels (box, ds^2/12) and the bilinear
         * reconstruction (tent, ds^2/6) widen the result by ds^2/4 px^2 of
         * variance; take it out of the blur (st >= 3, so this stays > 0). */
        st = nxc__sqrtf(st * st - 0.25f);
    }
    fx_box_init(st, &p->box);
    p->m = p->box.support + 2;                 /* blur reach + AA + bilinear guard */
    p->rt = (float)rq4 * 0.25f / (float)p->ds;
    int32_t crt = (int32_t)nxc__ceilf(p->rt);
    float long_min = 2.0f * (float)((crt + p->m + 2) * p->ds);
    p->long_x = (w >= long_min);
    p->long_y = (h >= long_min);
    int32_t w4 = p->long_x ? -1 : nxc__maxi(1, (int32_t)(w * 4.0f + 0.5f));
    int32_t h4 = p->long_y ? -1 : nxc__maxi(1, (int32_t)(h * 4.0f + 0.5f));
    p->wt = p->long_x ? 0.0f : (float)w4 * 0.25f / (float)p->ds;
    p->ht = p->long_y ? 0.0f : (float)h4 * 0.25f / (float)p->ds;
    p->tw = p->long_x ? crt + 2 * p->m + 2 : (int32_t)nxc__ceilf(p->wt) + 2 * p->m + 1;
    p->th = p->long_y ? crt + 2 * p->m + 2 : (int32_t)nxc__ceilf(p->ht) + 2 * p->m + 1;
    p->key[0] = rq4;
    p->key[1] = sq4;
    p->key[2] = w4;
    p->key[3] = h4;
}

/*
 * Area coverage estimate of the pixel centered at (cx, cy) by the rounded
 * rect [x0, x1) x [y0, y1), corner radius rad: exact box overlap along the
 * straight edges (also exact for shapes thinner than a pixel), signed
 * distance coverage inside the corner zones.
 */
static float fx_rrect_cov(float cx, float cy, float x0, float y0, float x1, float y1,
                          float rad) {
    float ox = nxc__minf(cx + 0.5f, x1) - nxc__maxf(cx - 0.5f, x0);
    float oy = nxc__minf(cy + 0.5f, y1) - nxc__maxf(cy - 0.5f, y0);
    if (!(ox > 0.0f) || !(oy > 0.0f)) return 0.0f;
    float box = nxc__minf(ox, 1.0f) * nxc__minf(oy, 1.0f);
    if (!(rad > 0.0f)) return box;
    float hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
    rad = nxc__minf(rad, nxc__minf(hx, hy));
    float qx = nxc__absf(cx - (x0 + hx)) - (hx - rad);
    float qy = nxc__absf(cy - (y0 + hy)) - (hy - rad);
    if (qx <= 0.0f || qy <= 0.0f) return box;
    float cov = 0.5f - (nxc__sqrtf(qx * qx + qy * qy) - rad);
    if (cov <= 0.0f) return 0.0f;
    return nxc__minf(cov, box);
}

/*
 * Rasterize the shape into the mask (texel space, shape corner at (m, m))
 * and blur it. On a long axis the shape runs past the far mask edge, so
 * the edge clamping of the blur reproduces the infinite edge exactly.
 */
static void fx_shadow_build(uint8_t *mask, const fx_shadow_t *p, uint32_t *l0, uint32_t *l1) {
    float m = (float)p->m;
    float x0 = m, y0 = m;
    float x1 = p->long_x ? (float)(p->tw + p->m) + p->rt + 64.0f : m + p->wt;
    float y1 = p->long_y ? (float)(p->th + p->m) + p->rt + 64.0f : m + p->ht;
    for (int32_t j = 0; j < p->th; j++) {
        uint8_t *row = mask + (int64_t)j * p->tw;
        float cy = (float)j + 0.5f;
        for (int32_t i = 0; i < p->tw; i++) {
            float cov = fx_rrect_cov((float)i + 0.5f, cy, x0, y0, x1, y1, p->rt);
            row[i] = (uint8_t)(cov * 255.0f + 0.5f);
        }
    }
    if (p->box.r > 0 || p->box.aq > 0) {
        fx_blur_mask_core(mask, p->tw, p->th, p->tw, &p->box, l0, l1);
    }
}

/* Mapping of device pixels to mask texels along one axis. The texel
 * coordinate (texel centers at integers) is measured from the near edge,
 * and on long axes also from the far edge (mirrored); the smaller wins. */
typedef struct {
    double  tn0, tf0;   /* near / far texel coordinate of device pixel 0 */
    double  inv;        /* texels per device pixel (1 / ds) */
    int32_t lo, hi;     /* device pixels the shadow touches, [lo, hi) */
    int32_t pa, pb;     /* plateau [pa, pb): beyond the tile on both sides */
    int32_t tmax;       /* last texel index */
    int32_t is_long;
} fx_axis_t;

static void fx_axis_init(fx_axis_t *a, double e0, double e1, int32_t ds, int32_t m,
                         int32_t texels, int is_long) {
    double margin = (double)m * (double)ds;
    a->inv = 1.0 / (double)ds;
    a->tn0 = (0.5 - e0 + margin) * a->inv - 0.5;
    a->tf0 = (e1 - 0.5 + margin) * a->inv - 0.5;
    a->lo = (int32_t)fx_floor_d(e0 - margin);
    a->hi = (int32_t)fx_ceil_d(e1 + margin);
    a->tmax = texels - 1;
    a->is_long = is_long;
    a->pa = a->pb = a->hi;
    if (is_long) {
        int64_t pa = fx_ceil_d(((double)a->tmax - a->tn0) * (double)ds);
        int64_t pb = fx_floor_d((a->tf0 - (double)a->tmax) * (double)ds) + 1;
        if (pa < a->lo) pa = a->lo;
        if (pa > a->hi) pa = a->hi;
        if (pb < pa) pb = pa;
        if (pb > a->hi) pb = a->hi;
        a->pa = (int32_t)pa;
        a->pb = (int32_t)pb;
    }
}

/* Texel coordinate of device pixel p, 16.16 fixed point, clamped to the mask. */
static int64_t fx_axis_t16(const fx_axis_t *a, int32_t p) {
    double t = a->tn0 + (double)p * a->inv;
    if (a->is_long) {
        double tf = a->tf0 - (double)p * a->inv;
        if (tf < t) t = tf;
    }
    if (t < 0.0) t = 0.0;
    if (t > (double)a->tmax) t = (double)a->tmax;
    return (int64_t)(t * 65536.0);
}

/* Coverage of n device pixels from p0 along x: linear interpolation of the
 * vertically blended mask row br (texel values scaled by 256), stepping
 * the texel coordinate incrementally (ds is a power of two: exact). */
static void fx_eval_span(const fx_axis_t *a, const uint32_t *br, int32_t p0, int32_t n,
                         uint8_t *cov) {
    int64_t step = (int64_t)(a->inv * 65536.0);
    int64_t tn = (int64_t)((a->tn0 + (double)p0 * a->inv) * 65536.0);
    int64_t tf = (int64_t)((a->tf0 - (double)p0 * a->inv) * 65536.0);
    int64_t tmax = (int64_t)a->tmax << 16;
    for (int32_t i = 0; i < n; i++) {
        int64_t t = tn;
        if (a->is_long && tf < t) t = tf;
        if (t < 0) t = 0;
        else if (t > tmax) t = tmax;
        int32_t i0 = (int32_t)(t >> 16);
        uint32_t f = (uint32_t)(t >> 8) & 0xFFu;
        int32_t i1 = (i0 < a->tmax) ? i0 + 1 : i0;
        uint32_t v = br[i0] * (256u - f) + br[i1] * f;
        cov[i] = (uint8_t)((v + 32768u) >> 16);
        tn += step;
        tf -= step;
    }
}

/* Draw device pixels [p0, p1) of row py from the blended mask row. */
static void fx_shadow_ramp(nxc_canvas_t *c, const fx_axis_t *ax, const uint32_t *br,
                           int32_t py, int32_t p0, int32_t p1, uint32_t premul,
                           uint8_t *cov, int32_t cap) {
    while (p0 < p1) {
        int32_t n = nxc__mini(cap, p1 - p0);
        fx_eval_span(ax, br, p0, n, cov);
        nxc__span_cov_gamma(c, py, p0, cov, n, premul);
        p0 += n;
    }
}

/*
 * Assemble the shadow row by row: blend the two mask rows around the row's
 * texel coordinate (reused while it does not change, i.e. across the whole
 * vertical plateau), evaluate the left and right ramps per pixel and fill
 * the plateau between them with one constant-coverage span.
 */
static void fx_shadow_paint(nxc_canvas_t *c, const uint8_t *mask, const fx_shadow_t *p,
                            const fx_axis_t *ax, const fx_axis_t *ay, uint32_t premul,
                            uint32_t *br, uint8_t *cov, int32_t cov_cap) {
    nxc_irect_t cl = nxc__device_clip(c);
    int32_t y0 = nxc__maxi(ay->lo, cl.y), y1 = nxc__mini(ay->hi, cl.y + cl.h);
    int32_t x0 = nxc__maxi(ax->lo, cl.x), x1 = nxc__mini(ax->hi, cl.x + cl.w);
    if (cl.w == 0 || y0 >= y1 || x0 >= x1) return;
    int64_t prev = -1;
    for (int32_t py = y0; py < y1; py++) {
        int64_t t = fx_axis_t16(ay, py);
        int32_t j0 = (int32_t)(t >> 16);
        uint32_t fy = (uint32_t)(t >> 8) & 0xFFu;
        int64_t rowkey = ((int64_t)j0 << 8) | fy;
        if (rowkey != prev) {
            const uint8_t *m0 = mask + (int64_t)j0 * p->tw;
            const uint8_t *m1 = (j0 < ay->tmax) ? m0 + p->tw : m0;
            for (int32_t i = 0; i < p->tw; i++) {
                br[i] = (uint32_t)m0[i] * (256u - fy) + (uint32_t)m1[i] * fy;
            }
            prev = rowkey;
        }
        if (ax->is_long) {
            uint32_t pc = (br[ax->tmax] + 128u) >> 8;
            fx_shadow_ramp(c, ax, br, py, x0, nxc__mini(ax->pa, x1), premul, cov, cov_cap);
            if (pc) {
                nxc__span_solid_gamma(c, py, nxc__maxi(ax->pa, x0), nxc__mini(ax->pb, x1), premul, pc);
            }
            fx_shadow_ramp(c, ax, br, py, nxc__maxi(ax->pb, x0), x1, premul, cov, cov_cap);
        } else {
            fx_shadow_ramp(c, ax, br, py, x0, x1, premul, cov, cov_cap);
        }
    }
}

nxc_status_t nxc_draw_shadow_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                   float sigma, float dx, float dy, float spread,
                                   nxc_color_t color) {
    static const char where[] = "nxc_draw_shadow_rrect";
    if (!c || !fx_surface_ok(c->target) || !fx_rect_ok(r) || !fx_coord_ok(radius) ||
        radius < 0.0f || !nxc__finitef(sigma) || sigma < 0.0f || !fx_coord_ok(dx) ||
        !fx_coord_ok(dy) || !fx_coord_ok(spread)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    fx_cache_sync();
    uint32_t premul = nxc__premul(color);
    if (premul == 0u || c->st.alpha == 0u) return NXC_OK;
    if (sigma > FX_SIGMA_MAX) sigma = FX_SIGMA_MAX;

    /* Spread grows the shape (and a rounded corner's radius) before blurring. */
    float w = r.w + 2.0f * spread, h = r.h + 2.0f * spread;
    if (!(w > 0.0f) || !(h > 0.0f)) return NXC_OK;
    float rad = (radius > 0.0f) ? radius + spread : 0.0f;
    rad = nxc__clampf(rad, 0.0f, 0.5f * nxc__minf(w, h));

    fx_shadow_t p;
    fx_shadow_plan(w, h, rad, sigma, &p);

    /* Device-space shape edges; short axes use the quantized extent,
     * centered on the requested one. */
    double ex0 = (double)r.x + (double)c->st.tx + (double)dx - (double)spread;
    double ey0 = (double)r.y + (double)c->st.ty + (double)dy - (double)spread;
    double ew = p.long_x ? (double)w : (double)p.key[2] * 0.25;
    double eh = p.long_y ? (double)h : (double)p.key[3] * 0.25;
    ex0 += 0.5 * ((double)w - ew);
    ey0 += 0.5 * ((double)h - eh);
    if (!(ex0 > -1.0e9 && ex0 + ew < 1.0e9 && ey0 > -1.0e9 && ey0 + eh < 1.0e9)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    fx_axis_t ax, ay;
    fx_axis_init(&ax, ex0, ex0 + ew, p.ds, p.m, p.tw, p.long_x);
    fx_axis_init(&ay, ey0, ey0 + eh, p.ds, p.m, p.th, p.long_y);

    nxc_irect_t cl = nxc__device_clip(c);
    if (cl.w == 0 || ax.hi <= cl.x || ax.lo >= cl.x + cl.w || ay.hi <= cl.y ||
        ay.lo >= cl.y + cl.h) {
        return NXC_OK;                              /* entirely clipped: no build */
    }

    uint32_t *l0, *l1;
    int64_t cap;
    uint64_t cov_bytes = 0;
    uint8_t *cov = (uint8_t *)nxc__scratch(NXC_SCRATCH_COV, &cov_bytes);
    uint32_t pool_bytes = 0;
    uint8_t *pool = (uint8_t *)nxc__shadow_pool(&pool_bytes);
    if (!fx_lines(&l0, &l1, &cap) || !cov || cov_bytes == 0 || !pool) {
        return nxc__fail(NXC_ERR_STATE, where);
    }
    uint64_t bytes = ((uint64_t)p.tw * (uint64_t)p.th + (FX_ALIGN - 1u)) & ~(uint64_t)(FX_ALIGN - 1u);
    if ((int64_t)p.tw > cap || (int64_t)p.th > cap || bytes > pool_bytes) {
        nxc__log("[NXC] shadow: mask exceeds the shadow cache\n");
        return nxc__fail(NXC_ERR_NO_MEMORY, where);
    }

    int32_t si = fx_cache_find(p.key);
    if (si < 0) {
        si = fx_pool_alloc((uint32_t)bytes, 0, pool_bytes);
        if (si < 0) {
            nxc__log("[NXC] shadow: no room in the shadow cache\n");
            return nxc__fail(NXC_ERR_NO_MEMORY, where);
        }
        fx_slot_t *e = &g_fx.slot[si];
        for (int k = 0; k < 4; k++) e->key[k] = p.key[k];
        e->tw = p.tw;
        e->th = p.th;
        fx_shadow_build(pool + e->off, &p, l0, l1);
    }
    g_fx.slot[si].stamp = fx_tick();

    int32_t cov_cap = (int32_t)(cov_bytes > 0x40000000u ? 0x40000000u : cov_bytes);
    fx_shadow_paint(c, pool + g_fx.slot[si].off, &p, &ax, &ay, premul, l0, cov, cov_cap);
    return NXC_OK;
}

/* ============ Backdrop materials ============ */

typedef struct {
    int32_t  sat;          /* saturation * 256 (256 = unchanged) */
    uint32_t tint;         /* premultiplied tint (0 = none) */
    int32_t  noise;        /* grain amplitude 0..16 */
    uint32_t border;       /* premultiplied highlight (0 = none) */
    float    ix0, iy0, ix1, iy1, irad;   /* 1px-inset inner rounded rect, device */
    int      inner_empty;  /* shape thinner than 2px: all highlight */
} fx_mat_t;

/* Deterministic per-pixel hash (integer avalanche mix). */
static inline uint32_t fx_hash(int32_t x, int32_t y) {
    uint32_t h = (uint32_t)x * 0xA24BAED5u ^ ((uint32_t)y * 0x9FB21C65u + 0x6E3A1D27u);
    h ^= h >> 16;
    h *= 0xE2D0A3A7u;
    h ^= h >> 13;
    h *= 0x8C35F0B1u;
    h ^= h >> 16;
    return h;
}

/* Move one premultiplied channel away from luminance by s256/256. */
static inline uint32_t fx_saturate(int32_t ch, int32_t lum, int32_t s256, int32_t a) {
    int32_t v = lum * 256 + (ch - lum) * s256 + 128;
    if (v <= 0) return 0u;
    v >>= 8;
    return (uint32_t)(v > a ? a : v);
}

/* Saturation, tint and grain applied to one blurred backdrop pixel. */
static inline uint32_t fx_material_px(const fx_mat_t *mt, uint32_t p, int32_t x, int32_t y) {
    if (mt->sat != 256) {
        int32_t a = (int32_t)(p >> 24);
        int32_t r = (int32_t)((p >> 16) & 0xFFu), g = (int32_t)((p >> 8) & 0xFFu);
        int32_t b = (int32_t)(p & 0xFFu);
        int32_t lum = (54 * r + 183 * g + 19 * b + 128) >> 8;      /* Rec. 709 weights */
        p = ((uint32_t)a << 24) | (fx_saturate(r, lum, mt->sat, a) << 16) |
            (fx_saturate(g, lum, mt->sat, a) << 8) | fx_saturate(b, lum, mt->sat, a);
    }
    if (mt->tint) p = nxc__over(mt->tint, p);
    if (mt->noise) {
        int32_t a = (int32_t)(p >> 24);
        int32_t d = ((int32_t)(fx_hash(x, y) >> 24) - 128) * mt->noise * a / 32768;
        if (d != 0) {
            uint32_t out = p & 0xFF000000u;
            for (int shift = 0; shift < 24; shift += 8) {
                int32_t v = (int32_t)((p >> shift) & 0xFFu) + d;
                v = v < 0 ? 0 : (v > a ? a : v);
                out |= (uint32_t)v << shift;
            }
            p = out;
        }
    }
    return p;
}

/* Composite the 1px inner highlight over row segment out[0..n) (device
 * x0.., row y). Only pixels not fully inside the inset rrect get any. */
static void fx_border_row(const fx_mat_t *mt, uint32_t *out, int32_t x0, int32_t y, int32_t n) {
    if (mt->inner_empty) {
        for (int32_t i = 0; i < n; i++) out[i] = nxc__over(mt->border, out[i]);
        return;
    }
    float cy = (float)y + 0.5f;
    int32_t a1 = n, b0 = n;
    if ((float)y >= mt->iy0 + mt->irad && (float)(y + 1) <= mt->iy1 - mt->irad) {
        /* Straight band: only the two side edges can be uncovered. */
        double xl = (double)nxc__ceilf(mt->ix0) - (double)x0;
        double xr = (double)nxc__floorf(mt->ix1) - (double)x0;
        a1 = (int32_t)(xl < 0.0 ? 0.0 : (xl > (double)n ? (double)n : xl));
        b0 = (int32_t)(xr < (double)a1 ? (double)a1 : (xr > (double)n ? (double)n : xr));
    }
    for (int32_t i = 0; i < n; i++) {
        if (i == a1) { i = b0; if (i >= n) break; }
        float in = fx_rrect_cov((float)(x0 + i) + 0.5f, cy, mt->ix0, mt->iy0, mt->ix1,
                                mt->iy1, mt->irad);
        uint32_t k = (uint32_t)((1.0f - in) * 255.0f + 0.5f);
        if (k == 0u) continue;
        out[i] = nxc__over(k >= 255u ? mt->border : nxc__mul_px(mt->border, k), out[i]);
    }
}

nxc_status_t nxc_draw_backdrop(nxc_canvas_t *c, nxc_rect_t r, float radius,
                               const nxc_surface_t *blurred_source,
                               const nxc_material_t *m) {
    static const char where[] = "nxc_draw_backdrop";
    if (!c || !fx_surface_ok(c->target) || !m || !fx_rect_ok(r) || !fx_coord_ok(radius) ||
        radius < 0.0f || !nxc__finitef(m->saturation) || m->saturation < 0.0f ||
        m->saturation > FX_SATURATION_MAX || m->noise > FX_NOISE_MAX) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (blurred_source) {
        if (!fx_surface_ok(blurred_source) || blurred_source->width != c->target->width ||
            blurred_source->height != c->target->height) {
            return nxc__fail(NXC_ERR_INVALID_ARG, where);
        }
    } else if (!nxc__finitef(m->blur_sigma)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    double dx0 = (double)r.x + (double)c->st.tx, dy0 = (double)r.y + (double)c->st.ty;
    if (!(dx0 > -1.0e9 && dx0 + r.w < 1.0e9 && dy0 > -1.0e9 && dy0 + r.h < 1.0e9)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, where);
    }
    if (!nxc__ready()) return nxc__fail(NXC_ERR_STATE, where);
    if (!(r.w > 0.0f) || !(r.h > 0.0f)) return NXC_OK;
    fx_cache_sync();

    uint32_t *l0, *l1;
    int64_t cap;
    uint32_t pool_bytes = 0;
    uint8_t *pool = (uint8_t *)nxc__shadow_pool(&pool_bytes);
    if (!fx_lines(&l0, &l1, &cap) || !pool) return nxc__fail(NXC_ERR_STATE, where);

    /* Material constants and the inset rrect of the highlight. */
    fx_mat_t mt;
    nxc__zero(&mt, sizeof(mt));
    mt.sat = (int32_t)(m->saturation * 256.0f + 0.5f);
    mt.tint = nxc__premul(m->tint);
    mt.noise = (int32_t)m->noise;
    mt.border = nxc__premul(m->border);
    float rad = nxc__minf(radius, 0.5f * nxc__minf(r.w, r.h));
    mt.ix0 = (float)dx0 + 1.0f;
    mt.iy0 = (float)dy0 + 1.0f;
    mt.ix1 = (float)dx0 + r.w - 1.0f;
    mt.iy1 = (float)dy0 + r.h - 1.0f;
    mt.irad = nxc__maxf(rad - 1.0f, 0.0f);
    mt.inner_empty = !(mt.ix1 > mt.ix0) || !(mt.iy1 > mt.iy0);

    fx_box_t box;
    int live_blur = !blurred_source && fx_box_init(m->blur_sigma, &box);

    nxc_status_t st = nxc_save(c);                 /* records its own failure */
    if (st != NXC_OK) return st;
    st = nxc_clip_rrect(c, r, radius);
    if (st != NXC_OK) { (void)nxc_restore(c); return st; }
    nxc_irect_t v = nxc__device_clip(c);
    if (v.w == 0) return nxc_restore(c);

    nxc_surface_t *t = c->target;
    int32_t si = -1;
    uint32_t *work = 0;
    nxc_irect_t sr = v;
    if (live_blur) {
        /* Blur the region plus the blur's reach (for correct edges) in
         * working memory carved from the shadow pool. */
        sr = fx_intersect(nxc_irect(v.x - box.support, v.y - box.support,
                                    v.w + 2 * box.support, v.h + 2 * box.support),
                          nxc_irect(0, 0, t->width, t->height));
        uint64_t bytes = ((uint64_t)sr.w * (uint64_t)sr.h * 4u + (FX_ALIGN - 1u)) &
                         ~(uint64_t)(FX_ALIGN - 1u);
        if ((int64_t)sr.w * 4 > cap || (int64_t)sr.h * 4 > cap || bytes > pool_bytes ||
            (si = fx_pool_alloc((uint32_t)bytes, 1, pool_bytes)) < 0) {
            nxc__log("[NXC] backdrop: live blur region exceeds the shadow pool; "
                     "pass a pre-blurred source\n");
            (void)nxc_restore(c);
            return nxc__fail(NXC_ERR_OVERFLOW, where);
        }
        work = (uint32_t *)(void *)(pool + g_fx.slot[si].off);
        for (int32_t y = 0; y < sr.h; y++) {
            nxc__copy32(work + (int64_t)y * sr.w,
                        t->pixels + (int64_t)(sr.y + y) * t->stride + sr.x, sr.w);
        }
        int opaque = (t->flags & NXC_SURFACE_OPAQUE) != 0;
        fx_blur_rows32(work, sr.w, nxc_irect(0, 0, sr.w, sr.h), v.x - sr.x,
                       v.x - sr.x + v.w, &box, l0, l1, opaque);
        fx_blur_cols32(work, sr.w, nxc_irect(v.x - sr.x, 0, v.w, sr.h), v.y - sr.y,
                       v.y - sr.y + v.h, &box, l0, l1, opaque);
    }

    /* Shade each row into LINE0 and write it through the rounded clip. */
    int32_t chunk = (int32_t)(cap > 0x40000000 ? 0x40000000 : cap);
    for (int32_t y = v.y; y < v.y + v.h; y++) {
        const uint32_t *src;
        if (blurred_source) {
            src = blurred_source->pixels + (int64_t)y * blurred_source->stride + v.x;
        } else if (work) {
            src = work + (int64_t)(y - sr.y) * sr.w + (v.x - sr.x);
        } else {
            src = t->pixels + (int64_t)y * t->stride + v.x;
        }
        for (int32_t x = 0; x < v.w; x += chunk) {
            int32_t n = nxc__mini(chunk, v.w - x);
            for (int32_t i = 0; i < n; i++) {
                l0[i] = fx_material_px(&mt, src[x + i], v.x + x + i, y);
            }
            if (mt.border) fx_border_row(&mt, l0, v.x + x, y, n);
            if (n > 8) {
                /* Split so the interior takes the span blender's fast path. */
                nxc__span_pixels(c, y, v.x + x, l0, 0, 2, 255u);
                nxc__span_pixels(c, y, v.x + x + 2, l0 + 2, 0, n - 4, 255u);
                nxc__span_pixels(c, y, v.x + x + n - 2, l0 + n - 2, 0, 2, 255u);
            } else {
                nxc__span_pixels(c, y, v.x + x, l0, 0, n, 255u);
            }
        }
    }
    if (si >= 0) g_fx.slot[si].live = 0;            /* release working memory */
    return nxc_restore(c);
}
