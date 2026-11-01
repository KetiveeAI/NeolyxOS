/*
 * NeolyxOS - NXCanvas core
 *
 * Engine state, diagnostics, scratch memory, surfaces, canvas state and
 * span blending. Every pixel the engine writes passes through the
 * nxc__span_* functions in this file, so clipping, rounded clipping and
 * global alpha are enforced in one place.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_internal.h"
#include "nxc_srgb_lut.h"

/* ============ Engine state ============ */

typedef struct {
    int         ready;
    uint32_t    generation;
    nxc_env_t   env;
    uint32_t    error_count;
    uint32_t    logged_codes;          /* bit per status code already logged */
    void       *scratch[NXC_SCRATCH_COUNT];
    uint64_t    scratch_bytes[NXC_SCRATCH_COUNT];
    void       *glyph_pool;
    uint32_t    glyph_pool_bytes;
    void       *shadow_pool;
    uint32_t    shadow_pool_bytes;
} nxc_engine_t;

static nxc_engine_t g_nxc;

int nxc__ready(void) { return g_nxc.ready; }
uint32_t nxc__generation(void) { return g_nxc.generation; }
int32_t nxc__max_width(void) { return g_nxc.env.max_width; }
int32_t nxc__max_height(void) { return g_nxc.env.max_height; }
uint32_t nxc_error_count(void) { return g_nxc.error_count; }

void *nxc__alloc(uint64_t size) {
    if (!g_nxc.env.alloc || size == 0) return 0;
    void *p = g_nxc.env.alloc(size);
    if (((uintptr_t)p & 15u) != 0) {
        /* Contract violation by the allocator: refuse rather than fault later. */
        nxc__log("[NXC] allocator returned unaligned memory");
        return 0;
    }
    return p;
}

void nxc__free(void *ptr) {
    if (ptr && g_nxc.env.free) g_nxc.env.free(ptr);
}

void nxc__log(const char *msg) {
    if (msg && g_nxc.env.log) g_nxc.env.log(msg);
}

const char *nxc_status_str(nxc_status_t status) {
    switch (status) {
        case NXC_OK:               return "ok";
        case NXC_ERR_INVALID_ARG:  return "invalid argument";
        case NXC_ERR_NO_MEMORY:    return "out of memory";
        case NXC_ERR_BAD_FONT:     return "bad font";
        case NXC_ERR_OVERFLOW:     return "capacity overflow";
        case NXC_ERR_NOT_FOUND:    return "not found";
        case NXC_ERR_STATE:        return "invalid state";
        case NXC_ERR_UNSUPPORTED:  return "unsupported";
        default:                   return "unknown error";
    }
}

/* Append src to dst[pos..cap) and return the new position. */
static uint32_t log_append(char *dst, uint32_t pos, uint32_t cap, const char *src) {
    while (src && *src && pos + 1 < cap) dst[pos++] = *src++;
    dst[pos] = '\0';
    return pos;
}

nxc_status_t nxc__fail(nxc_status_t status, const char *where) {
    if (status == NXC_OK) return status;
    g_nxc.error_count++;
    uint32_t code = (uint32_t)(-status);
    uint32_t bit = (code < 32u) ? (1u << code) : (1u << 31);
    if (!(g_nxc.logged_codes & bit)) {
        g_nxc.logged_codes |= bit;
        char line[160];
        uint32_t n = 0;
        n = log_append(line, n, sizeof(line), "[NXC] error: ");
        n = log_append(line, n, sizeof(line), nxc_status_str(status));
        n = log_append(line, n, sizeof(line), " in ");
        n = log_append(line, n, sizeof(line), where ? where : "?");
        (void)log_append(line, n, sizeof(line), "\n");
        nxc__log(line);
    }
    return status;
}

/* ============ Memory helpers ============ */

void nxc__fill32(uint32_t *dst, uint32_t value, int32_t count) {
    if (!dst || count <= 0) return;
    while (count >= 4) {
        dst[0] = value; dst[1] = value; dst[2] = value; dst[3] = value;
        dst += 4; count -= 4;
    }
    while (count-- > 0) *dst++ = value;
}

void nxc__copy32(uint32_t *dst, const uint32_t *src, int32_t count) {
    if (!dst || !src || count <= 0 || dst == src) return;
    if (dst > src && dst < src + count) {
        /* Overlapping, copy backwards. */
        for (int32_t i = count - 1; i >= 0; i--) dst[i] = src[i];
        return;
    }
    while (count >= 4) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        dst += 4; src += 4; count -= 4;
    }
    while (count-- > 0) *dst++ = *src++;
}

void nxc__zero(void *dst, uint64_t bytes) {
    if (!dst) return;
    uint8_t *p = (uint8_t *)dst;
    while (bytes && ((uintptr_t)p & 7u)) { *p++ = 0; bytes--; }
    uint64_t *q = (uint64_t *)p;
    while (bytes >= 8) { *q++ = 0; bytes -= 8; }
    p = (uint8_t *)q;
    while (bytes--) *p++ = 0;
}

/* ============ Init / shutdown ============ */

static void release_all(void) {
    for (int i = 0; i < NXC_SCRATCH_COUNT; i++) {
        nxc__free(g_nxc.scratch[i]);
        g_nxc.scratch[i] = 0;
        g_nxc.scratch_bytes[i] = 0;
    }
    nxc__free(g_nxc.glyph_pool);
    nxc__free(g_nxc.shadow_pool);
    g_nxc.glyph_pool = 0;
    g_nxc.shadow_pool = 0;
    g_nxc.glyph_pool_bytes = 0;
    g_nxc.shadow_pool_bytes = 0;
}

nxc_status_t nxc_init(const nxc_env_t *env) {
    if (!env || !env->alloc) return NXC_ERR_INVALID_ARG;
    if (env->max_width <= 0 || env->max_height <= 0 ||
        env->max_width > 16384 || env->max_height > 16384) {
        return NXC_ERR_INVALID_ARG;
    }
    if (g_nxc.ready) nxc_shutdown();

    uint32_t gen = g_nxc.generation;
    nxc__zero(&g_nxc, sizeof(g_nxc));
    g_nxc.generation = gen;
    g_nxc.env = *env;
    if (g_nxc.env.glyph_cache_bytes == 0)  g_nxc.env.glyph_cache_bytes = NXC_DEFAULT_GLYPH_CACHE_BYTES;
    if (g_nxc.env.shadow_cache_bytes == 0) g_nxc.env.shadow_cache_bytes = NXC_DEFAULT_SHADOW_CACHE_BYTES;

    uint64_t mw = (uint64_t)env->max_width;
    uint64_t mh = (uint64_t)env->max_height;
    uint64_t line = (mw > mh ? mw : mh);
    uint64_t sizes[NXC_SCRATCH_COUNT];
    sizes[NXC_SCRATCH_ACCUM] = (mw + 4u) * (NXC_BAND_ROWS + 1u) * sizeof(float);
    sizes[NXC_SCRATCH_COV]   = (mw + 64u);
    sizes[NXC_SCRATCH_COLOR] = (mw + 64u) * sizeof(uint32_t);
    sizes[NXC_SCRATCH_LINE0] = (line * 4u + 64u) * sizeof(uint32_t);
    sizes[NXC_SCRATCH_LINE1] = (line * 4u + 64u) * sizeof(uint32_t);

    for (int i = 0; i < NXC_SCRATCH_COUNT; i++) {
        uint64_t bytes = (sizes[i] + 15u) & ~(uint64_t)15u;
        g_nxc.scratch[i] = nxc__alloc(bytes);
        if (!g_nxc.scratch[i]) {
            nxc__log("[NXC] init: scratch allocation failed\n");
            release_all();
            return NXC_ERR_NO_MEMORY;
        }
        nxc__zero(g_nxc.scratch[i], bytes);
        g_nxc.scratch_bytes[i] = bytes;
    }

    g_nxc.glyph_pool = nxc__alloc(g_nxc.env.glyph_cache_bytes);
    g_nxc.shadow_pool = nxc__alloc(g_nxc.env.shadow_cache_bytes);
    if (!g_nxc.glyph_pool || !g_nxc.shadow_pool) {
        nxc__log("[NXC] init: cache allocation failed\n");
        release_all();
        return NXC_ERR_NO_MEMORY;
    }
    g_nxc.glyph_pool_bytes = g_nxc.env.glyph_cache_bytes;
    g_nxc.shadow_pool_bytes = g_nxc.env.shadow_cache_bytes;

    g_nxc.generation++;
    g_nxc.ready = 1;
    nxc__log("[NXC] engine initialized\n");
    return NXC_OK;
}

void nxc_shutdown(void) {
    if (!g_nxc.ready) return;
    release_all();
    g_nxc.ready = 0;
    nxc__log("[NXC] engine shut down\n");
}

void *nxc__scratch(nxc_scratch_id_t id, uint64_t *capacity_bytes) {
    if (id < 0 || id >= NXC_SCRATCH_COUNT || !g_nxc.ready) {
        if (capacity_bytes) *capacity_bytes = 0;
        return 0;
    }
    if (capacity_bytes) *capacity_bytes = g_nxc.scratch_bytes[id];
    return g_nxc.scratch[id];
}

void *nxc__glyph_pool(uint32_t *bytes) {
    if (bytes) *bytes = g_nxc.ready ? g_nxc.glyph_pool_bytes : 0;
    return g_nxc.ready ? g_nxc.glyph_pool : 0;
}

void *nxc__shadow_pool(uint32_t *bytes) {
    if (bytes) *bytes = g_nxc.ready ? g_nxc.shadow_pool_bytes : 0;
    return g_nxc.ready ? g_nxc.shadow_pool : 0;
}

/* ============ Colors ============ */

nxc_color_t nxc_color_lerp(nxc_color_t a, nxc_color_t b, float t) {
    if (!(t > 0.0f)) return a;          /* also catches NaN */
    if (t >= 1.0f) return b;
    uint32_t k = (uint32_t)(t * 256.0f + 0.5f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int32_t ca = (int32_t)((a >> shift) & 0xFFu);
        int32_t cb = (int32_t)((b >> shift) & 0xFFu);
        int32_t v = ca + (((cb - ca) * (int32_t)k) >> 8);
        out |= ((uint32_t)v & 0xFFu) << shift;
    }
    return out;
}

nxc_color_t nxc_color_fade(nxc_color_t c, float k) {
    if (!(k > 0.0f)) return c & 0x00FFFFFFu;
    if (k >= 1.0f) return c;
    uint32_t a = (uint32_t)((float)(c >> 24) * k + 0.5f);
    return (c & 0x00FFFFFFu) | (a << 24);
}

/* ============ Surfaces ============ */

nxc_status_t nxc_surface_wrap(nxc_surface_t *s, uint32_t *pixels,
                              int32_t width, int32_t height,
                              int32_t stride, uint32_t flags) {
    if (!s || !pixels || width <= 0 || height <= 0 || stride < width ||
        width > 16384 || height > 16384) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_surface_wrap");
    }
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->stride = stride;
    s->flags = flags & ~NXC_SURFACE_OWNED;
    return NXC_OK;
}

nxc_status_t nxc_surface_create(nxc_surface_t *s, int32_t width,
                                int32_t height, uint32_t flags) {
    if (!s || width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_surface_create");
    }
    if (!g_nxc.ready) return nxc__fail(NXC_ERR_STATE, "nxc_surface_create");
    uint64_t bytes = (uint64_t)width * (uint64_t)height * 4u;
    uint32_t *px = (uint32_t *)nxc__alloc(bytes);
    if (!px) return nxc__fail(NXC_ERR_NO_MEMORY, "nxc_surface_create");
    nxc__zero(px, bytes);
    s->pixels = px;
    s->width = width;
    s->height = height;
    s->stride = width;
    s->flags = (flags & NXC_SURFACE_OPAQUE) | NXC_SURFACE_OWNED;
    if (s->flags & NXC_SURFACE_OPAQUE) {
        for (int32_t y = 0; y < height; y++) nxc__fill32(px + (uint64_t)y * width, 0xFF000000u, width);
    }
    return NXC_OK;
}

void nxc_surface_destroy(nxc_surface_t *s) {
    if (!s) return;
    if ((s->flags & NXC_SURFACE_OWNED) && s->pixels) nxc__free(s->pixels);
    s->pixels = 0;
    s->width = s->height = s->stride = 0;
    s->flags = 0;
}

static int surface_valid(const nxc_surface_t *s) {
    return s && s->pixels && s->width > 0 && s->height > 0 && s->stride >= s->width;
}

nxc_status_t nxc_surface_clear(nxc_surface_t *s, nxc_color_t color) {
    if (!surface_valid(s)) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_surface_clear");
    uint32_t v = nxc__premul(color);
    if (s->flags & NXC_SURFACE_OPAQUE) v = nxc__over(v, 0xFF000000u);
    for (int32_t y = 0; y < s->height; y++) {
        nxc__fill32(s->pixels + (uint64_t)y * (uint64_t)s->stride, v, s->width);
    }
    return NXC_OK;
}

/* Intersect two integer rects; result w/h are 0 when empty. */
static nxc_irect_t irect_intersect(nxc_irect_t a, nxc_irect_t b) {
    int32_t x0 = nxc__maxi(a.x, b.x);
    int32_t y0 = nxc__maxi(a.y, b.y);
    int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    int64_t x1 = ax1 < bx1 ? ax1 : bx1;
    int64_t y1 = ay1 < by1 ? ay1 : by1;
    nxc_irect_t r;
    r.x = x0; r.y = y0;
    r.w = (x1 > x0) ? (int32_t)(x1 - x0) : 0;
    r.h = (y1 > y0) ? (int32_t)(y1 - y0) : 0;
    if (r.w == 0 || r.h == 0) { r.w = 0; r.h = 0; }
    return r;
}

nxc_status_t nxc_surface_copy(nxc_surface_t *dst, int32_t dx, int32_t dy,
                              const nxc_surface_t *src, nxc_irect_t src_rect) {
    if (!surface_valid(dst) || !surface_valid(src) || src_rect.w < 0 || src_rect.h < 0) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_surface_copy");
    }
    nxc_irect_t sb = nxc_irect(0, 0, src->width, src->height);
    nxc_irect_t sr = irect_intersect(src_rect, sb);
    /* Shift destination by however much the source rect was clipped. */
    int64_t ox = (int64_t)dx + (sr.x - src_rect.x);
    int64_t oy = (int64_t)dy + (sr.y - src_rect.y);
    if (ox < -16384 || ox > 16384 || oy < -16384 || oy > 16384) return NXC_OK;
    nxc_irect_t dr = irect_intersect(nxc_irect((int32_t)ox, (int32_t)oy, sr.w, sr.h),
                                     nxc_irect(0, 0, dst->width, dst->height));
    if (dr.w == 0) return NXC_OK;
    int32_t sx = sr.x + (dr.x - (int32_t)ox);
    int32_t sy = sr.y + (dr.y - (int32_t)oy);
    for (int32_t row = 0; row < dr.h; row++) {
        uint32_t *d = dst->pixels + (uint64_t)(dr.y + row) * (uint64_t)dst->stride + dr.x;
        const uint32_t *s = src->pixels + (uint64_t)(sy + row) * (uint64_t)src->stride + sx;
        nxc__copy32(d, s, dr.w);
        if ((dst->flags & NXC_SURFACE_OPAQUE) && !(src->flags & NXC_SURFACE_OPAQUE)) {
            for (int32_t i = 0; i < dr.w; i++) d[i] = nxc__over(d[i], 0xFF000000u);
        }
    }
    return NXC_OK;
}

/* ============ Canvas state ============ */

nxc_status_t nxc_canvas_init(nxc_canvas_t *c, nxc_surface_t *target) {
    if (!c || !surface_valid(target)) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_canvas_init");
    nxc__zero(c, sizeof(*c));
    c->target = target;
    c->st.clip = nxc_irect(0, 0, target->width, target->height);
    c->st.rclip_radius = 0.0f;
    c->st.tx = 0.0f;
    c->st.ty = 0.0f;
    c->st.alpha = 255;
    c->depth = 0;
    return NXC_OK;
}

nxc_status_t nxc_save(nxc_canvas_t *c) {
    if (!c) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_save");
    if (c->depth >= NXC_STATE_STACK_MAX) return nxc__fail(NXC_ERR_OVERFLOW, "nxc_save");
    c->stack[c->depth++] = c->st;
    return NXC_OK;
}

nxc_status_t nxc_restore(nxc_canvas_t *c) {
    if (!c) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_restore");
    if (c->depth <= 0) return nxc__fail(NXC_ERR_STATE, "nxc_restore");
    c->st = c->stack[--c->depth];
    return NXC_OK;
}

nxc_status_t nxc_clip_rect(nxc_canvas_t *c, nxc_irect_t r) {
    if (!c || r.w < 0 || r.h < 0) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_clip_rect");
    float fx = (float)r.x + c->st.tx;
    float fy = (float)r.y + c->st.ty;
    if (!nxc__finitef(fx) || !nxc__finitef(fy) ||
        fx < -1.0e8f || fx > 1.0e8f || fy < -1.0e8f || fy > 1.0e8f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_clip_rect");
    }
    nxc_irect_t d = nxc_irect((int32_t)nxc__floorf(fx), (int32_t)nxc__floorf(fy), r.w, r.h);
    c->st.clip = irect_intersect(c->st.clip, d);
    return NXC_OK;
}

nxc_status_t nxc_clip_rrect(nxc_canvas_t *c, nxc_rect_t r, float radius) {
    if (!c || !nxc__finitef(r.x) || !nxc__finitef(r.y) || !nxc__finitef(r.w) ||
        !nxc__finitef(r.h) || !nxc__finitef(radius) || r.w < 0.0f || r.h < 0.0f ||
        radius < 0.0f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_clip_rrect");
    }
    nxc_rect_t d = nxc_rect(r.x + c->st.tx, r.y + c->st.ty, r.w, r.h);
    if (c->st.rclip_radius > 0.0f) {
        /* Keep the intersection of the rounded bounds, newest radius. */
        float x0 = nxc__maxf(d.x, c->st.rclip.x);
        float y0 = nxc__maxf(d.y, c->st.rclip.y);
        float x1 = nxc__minf(d.x + d.w, c->st.rclip.x + c->st.rclip.w);
        float y1 = nxc__minf(d.y + d.h, c->st.rclip.y + c->st.rclip.h);
        d = nxc_rect(x0, y0, nxc__maxf(0.0f, x1 - x0), nxc__maxf(0.0f, y1 - y0));
    }
    float maxr = 0.5f * nxc__minf(d.w, d.h);
    c->st.rclip = d;
    c->st.rclip_radius = nxc__maxf(nxc__minf(radius, maxr), 0.0001f);
    /* Also tighten the rect clip to the pixels the rounded rect touches. */
    int32_t ix0 = (int32_t)nxc__floorf(d.x), iy0 = (int32_t)nxc__floorf(d.y);
    int32_t ix1 = (int32_t)nxc__ceilf(d.x + d.w), iy1 = (int32_t)nxc__ceilf(d.y + d.h);
    c->st.clip = irect_intersect(c->st.clip, nxc_irect(ix0, iy0, ix1 - ix0, iy1 - iy0));
    return NXC_OK;
}

nxc_status_t nxc_translate(nxc_canvas_t *c, float dx, float dy) {
    if (!c || !nxc__finitef(dx) || !nxc__finitef(dy)) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_translate");
    }
    c->st.tx += dx;
    c->st.ty += dy;
    return NXC_OK;
}

nxc_status_t nxc_set_alpha(nxc_canvas_t *c, uint8_t a) {
    if (!c) return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_set_alpha");
    c->st.alpha = (uint8_t)nxc__div255((uint32_t)c->st.alpha * a);
    return NXC_OK;
}

nxc_irect_t nxc__device_clip(const nxc_canvas_t *c) {
    if (!c || !surface_valid(c->target)) return nxc_irect(0, 0, 0, 0);
    nxc_irect_t r = irect_intersect(c->st.clip,
                                    nxc_irect(0, 0, c->target->width, c->target->height));
    return r;
}

/* ============ Rounded clip coverage ============ */

/*
 * Coverage (0..255) of pixel (x, y) by the canvas rounded clip, from the
 * signed distance of the pixel center to the rounded rect. Pixels well
 * inside return 255 without a square root.
 */
static uint32_t rclip_coverage(const nxc_state_t *st, int32_t x, int32_t y) {
    const nxc_rect_t *r = &st->rclip;
    float rad = st->rclip_radius;
    float px = (float)x + 0.5f, py = (float)y + 0.5f;
    float hx = 0.5f * r->w, hy = 0.5f * r->h;
    float qx = nxc__absf(px - (r->x + hx)) - (hx - rad);
    float qy = nxc__absf(py - (r->y + hy)) - (hy - rad);
    float sd;
    if (qx > 0.0f && qy > 0.0f) {
        sd = nxc__sqrtf(qx * qx + qy * qy) - rad;
    } else {
        sd = nxc__maxf(qx, qy) - rad;
    }
    float cov = 0.5f - sd;
    if (cov >= 1.0f) return 255u;
    if (cov <= 0.0f) return 0u;
    return (uint32_t)(cov * 255.0f + 0.5f);
}

/* True when every pixel center of row y in [x0, x1) is at least one pixel
 * inside the rounded clip, so per-pixel evaluation can be skipped. */
static int rclip_row_interior(const nxc_state_t *st, int32_t y, int32_t x0, int32_t x1) {
    const nxc_rect_t *r = &st->rclip;
    float rad = st->rclip_radius;
    float py = (float)y + 0.5f;
    if (py < r->y + rad + 1.0f || py > r->y + r->h - rad - 1.0f) return 0;
    return ((float)x0 + 0.5f >= r->x + 1.0f) && ((float)x1 - 0.5f <= r->x + r->w - 1.0f);
}

/* ============ Span blending ============ */

/* Clip [x0, x0+n) of row y to the device clip. Returns the number of
 * pixels kept and the offset of the first kept pixel. */
static int32_t clip_span(const nxc_canvas_t *c, int32_t y, int32_t x0, int32_t n,
                         int32_t *first_out) {
    nxc_irect_t cl = nxc__device_clip(c);
    if (cl.w == 0 || y < cl.y || y >= cl.y + cl.h || n <= 0) return 0;
    int64_t a = x0, b = (int64_t)x0 + n;
    if (a < cl.x) a = cl.x;
    if (b > (int64_t)cl.x + cl.w) b = (int64_t)cl.x + cl.w;
    if (b <= a) return 0;
    *first_out = (int32_t)(a - x0);
    return (int32_t)(b - a);
}

static inline uint32_t blend_px(uint32_t dst, uint32_t src_premul, uint32_t k, int opaque) {
    uint32_t s = (k >= 255u) ? src_premul : nxc__mul_px(src_premul, k);
    uint32_t out = nxc__over(s, dst);
    return opaque ? (out | 0xFF000000u) : out;
}

/*
 * R2: linear-light edges. Anti-aliased coverage says how much of a pixel a
 * shape covers, which is a fraction of *light*, so the edge pixel is mixed
 * between "untouched" and "fully covered" in linear light, not in the
 * gamma-encoded sRGB values. Mixing in sRGB makes edges look too dark on
 * light backgrounds and too thin/bright on dark ones (light text-like
 * fringes on colored fills). Alpha (paint and global alpha) still
 * composites in sRGB, as before, so fully covered pixels are unchanged.
 *
 * Only used where dst is opaque: the lerp is defined on display colors,
 * and a premultiplied translucent pixel has no single display color.
 */
static inline uint32_t lerp_linear(uint32_t dst, uint32_t full, uint32_t cov) {
    uint32_t inv = 255u - cov;
    uint32_t out = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        uint32_t ld = nxc__srgb_to_lin16[(dst >> sh) & 0xFFu];
        uint32_t lf = nxc__srgb_to_lin16[(full >> sh) & 0xFFu];
        uint32_t l = (lf * cov + ld * inv + 127u) / 255u;          /* 0..65535 */
        out |= (uint32_t)nxc__lin12_to_srgb[l >> 4] << sh;
    }
    return out;
}

/*
 * Blend premultiplied `premul` with alpha `ga` (global alpha or extra
 * coverage that is not geometric, 0..255) and geometric edge coverage
 * `geo` (1..255). Falls back to the plain sRGB blend for full coverage
 * (identical to blend_px) and for translucent destinations.
 */
static inline uint32_t blend_edge(uint32_t dst, uint32_t premul, uint32_t ga, uint32_t geo,
                                  int opaque) {
    uint32_t k = (ga >= 255u) ? geo : nxc__div255(geo * ga);
    if (geo >= 255u || (dst >> 24) != 255u) return blend_px(dst, premul, k, opaque);
    uint32_t s = (ga >= 255u) ? premul : nxc__mul_px(premul, ga);
    if (s == 0) return dst;
    uint32_t full = nxc__over(s, dst) | 0xFF000000u;      /* dst is opaque */
    return lerp_linear(dst, full, geo);
}

static void span_solid_impl(nxc_canvas_t *c, int32_t y, int32_t x0, int32_t x1,
                            uint32_t premul, uint32_t coverage, int linear) {
    if (!c || x1 <= x0 || coverage == 0 || premul == 0) return;
    int32_t first = 0;
    int32_t n = clip_span(c, y, x0, x1 - x0, &first);
    if (n <= 0) return;
    int32_t xs = x0 + first;
    uint32_t k = nxc__div255((coverage > 255u ? 255u : coverage) * c->st.alpha);
    if (k == 0) return;
    nxc_surface_t *t = c->target;
    int opaque = (t->flags & NXC_SURFACE_OPAQUE) != 0;
    uint32_t *row = t->pixels + (uint64_t)y * (uint64_t)t->stride;

    if (c->st.rclip_radius > 0.0f && !rclip_row_interior(&c->st, y, xs, xs + n)) {
        uint32_t cv = coverage > 255u ? 255u : coverage;
        for (int32_t i = 0; i < n; i++) {
            uint32_t rk = rclip_coverage(&c->st, xs + i, y);
            if (rk == 0) continue;
            if (linear) {
                uint32_t geo = nxc__div255(rk * cv);
                if (geo) row[xs + i] = blend_edge(row[xs + i], premul, c->st.alpha, geo, opaque);
            } else {
                row[xs + i] = blend_px(row[xs + i], premul, nxc__div255(rk * k), opaque);
            }
        }
        return;
    }
    if (linear && coverage < 255u) {
        for (int32_t i = 0; i < n; i++) {
            row[xs + i] = blend_edge(row[xs + i], premul, c->st.alpha, coverage, opaque);
        }
        return;
    }
    if (k == 255u && (premul >> 24) == 255u) {
        nxc__fill32(row + xs, premul, n);
        return;
    }
    uint32_t s = (k == 255u) ? premul : nxc__mul_px(premul, k);
    uint32_t inv = 255u - (s >> 24);
    for (int32_t i = 0; i < n; i++) {
        uint32_t out = s + nxc__mul_px(row[xs + i], inv);
        row[xs + i] = opaque ? (out | 0xFF000000u) : out;
    }
}

void nxc__span_solid(nxc_canvas_t *c, int32_t y, int32_t x0, int32_t x1,
                     uint32_t premul, uint32_t coverage) {
    span_solid_impl(c, y, x0, x1, premul, coverage, 1);
}

void nxc__span_solid_gamma(nxc_canvas_t *c, int32_t y, int32_t x0, int32_t x1,
                           uint32_t premul, uint32_t coverage) {
    span_solid_impl(c, y, x0, x1, premul, coverage, 0);
}

static void span_cov_impl(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                          int32_t n, uint32_t premul, int linear) {
    if (!c || !cov || n <= 0 || premul == 0) return;
    int32_t first = 0;
    int32_t m = clip_span(c, y, x0, n, &first);
    if (m <= 0) return;
    int32_t xs = x0 + first;
    const uint8_t *cv = cov + first;
    uint32_t ga = c->st.alpha;
    nxc_surface_t *t = c->target;
    int opaque = (t->flags & NXC_SURFACE_OPAQUE) != 0;
    uint32_t *row = t->pixels + (uint64_t)y * (uint64_t)t->stride;
    int rclip = c->st.rclip_radius > 0.0f && !rclip_row_interior(&c->st, y, xs, xs + m);
    int full = ((premul >> 24) == 255u) && ga == 255u;

    for (int32_t i = 0; i < m; i++) {
        uint32_t k = cv[i];
        if (k == 0) continue;
        if (linear) {
            /* geometric coverage stays apart from the global alpha */
            if (rclip) k = nxc__div255(k * rclip_coverage(&c->st, xs + i, y));
            if (k == 0) continue;
            if (k == 255u && full) row[xs + i] = premul;
            else row[xs + i] = blend_edge(row[xs + i], premul, ga, k, opaque);
            continue;
        }
        if (ga != 255u) k = nxc__div255(k * ga);
        if (rclip) k = nxc__div255(k * rclip_coverage(&c->st, xs + i, y));
        if (k == 0) continue;
        if (k == 255u && full) {
            row[xs + i] = premul;
        } else {
            row[xs + i] = blend_px(row[xs + i], premul, k, opaque);
        }
    }
}

void nxc__span_cov(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                   int32_t n, uint32_t premul) {
    span_cov_impl(c, y, x0, cov, n, premul, 1);
}

void nxc__span_cov_gamma(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                         int32_t n, uint32_t premul) {
    span_cov_impl(c, y, x0, cov, n, premul, 0);
}

void nxc__span_pixels(nxc_canvas_t *c, int32_t y, int32_t x0, const uint32_t *src,
                      const uint8_t *cov, int32_t n, uint32_t k) {
    if (!c || !src || n <= 0 || k == 0) return;
    int32_t first = 0;
    int32_t m = clip_span(c, y, x0, n, &first);
    if (m <= 0) return;
    int32_t xs = x0 + first;
    const uint32_t *sp = src + first;
    const uint8_t *cv = cov ? cov + first : 0;
    uint32_t base = nxc__div255((k > 255u ? 255u : k) * c->st.alpha);
    if (base == 0) return;
    nxc_surface_t *t = c->target;
    int opaque = (t->flags & NXC_SURFACE_OPAQUE) != 0;
    uint32_t *row = t->pixels + (uint64_t)y * (uint64_t)t->stride;
    int rclip = c->st.rclip_radius > 0.0f && !rclip_row_interior(&c->st, y, xs, xs + m);

    for (int32_t i = 0; i < m; i++) {
        uint32_t s = sp[i];
        if (s == 0) continue;
        if (cv || rclip) {
            /* R2: shape edges (shader coverage, rounded clip) in linear light */
            uint32_t geo = cv ? cv[i] : 255u;
            if (geo == 0) continue;
            if (rclip) geo = nxc__div255(geo * rclip_coverage(&c->st, xs + i, y));
            if (geo == 0) continue;
            if (geo < 255u) {
                row[xs + i] = blend_edge(row[xs + i], s, base, geo, opaque);
                continue;
            }
        }
        uint32_t kk = base;
        if (kk == 255u && (s >> 24) == 255u) {
            row[xs + i] = s;
        } else {
            row[xs + i] = blend_px(row[xs + i], s, kk, opaque);
        }
    }
}

void nxc__span_shade(nxc_canvas_t *c, int32_t y, int32_t x0, const uint8_t *cov,
                     int32_t n, nxc__shade_fn fn, const void *ctx) {
    if (!c || !fn || n <= 0) return;
    int32_t first = 0;
    int32_t m = clip_span(c, y, x0, n, &first);
    if (m <= 0) return;
    uint64_t cap = 0;
    uint32_t *buf = (uint32_t *)nxc__scratch(NXC_SCRATCH_COLOR, &cap);
    if (!buf) { (void)nxc__fail(NXC_ERR_STATE, "nxc__span_shade"); return; }
    int32_t chunk_max = (int32_t)(cap / sizeof(uint32_t));
    int32_t xs = x0 + first;
    const uint8_t *cv = cov ? cov + first : 0;
    while (m > 0) {
        int32_t len = m < chunk_max ? m : chunk_max;
        fn(ctx, xs, y, len, buf);
        nxc__span_pixels(c, y, xs, buf, cv, len, 255u);
        xs += len;
        if (cv) cv += len;
        m -= len;
    }
}

/* ============ Masks ============ */

nxc_status_t nxc_draw_mask(nxc_canvas_t *c, const uint8_t *mask, int32_t w, int32_t h,
                           int32_t stride, int32_t x, int32_t y, nxc_color_t color) {
    if (!c || !mask || w <= 0 || h <= 0 || stride < w) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_draw_mask");
    }
    float fx = (float)x + c->st.tx, fy = (float)y + c->st.ty;
    if (!nxc__finitef(fx) || !nxc__finitef(fy) || fx < -1.0e8f || fx > 1.0e8f ||
        fy < -1.0e8f || fy > 1.0e8f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_draw_mask");
    }
    int32_t dx = (int32_t)nxc__floorf(fx + 0.5f);
    int32_t dy = (int32_t)nxc__floorf(fy + 0.5f);
    uint32_t premul = nxc__premul(color);
    if (premul == 0) return NXC_OK;
    nxc_irect_t cl = nxc__device_clip(c);
    for (int32_t row = 0; row < h; row++) {
        int32_t yy = dy + row;
        if (yy < cl.y) continue;
        if (yy >= cl.y + cl.h) break;
        nxc__span_cov_gamma(c, yy, dx, mask + (uint64_t)row * (uint64_t)stride, w, premul);
    }
    return NXC_OK;
}
