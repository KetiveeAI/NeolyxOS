/*
 * NeolyxOS - NXCanvas text
 *
 * UTF-8 decoding, line layout (advances, kerning, tracking, alignment,
 * ellipsis truncation), a glyph coverage cache and text drawing.
 *
 * Glyph cache
 *   Lives in the engine's glyph pool. The pool holds a hash table of
 *   entries followed by a ring arena of 8-bit coverage bitmaps. Bitmaps
 *   are rendered once per (font, size/8 px, glyph, x phase/4 px) and
 *   evicted when the ring wraps over them; the table is open addressed
 *   with a short probe window so lookups stay O(1). Glyphs too large for
 *   the ring (big display text) are rasterized straight to the canvas.
 *
 * Crispness
 *   Baselines are snapped to whole pixels and glyph x positions keep a
 *   quarter-pixel phase, so UI text is sharp while spacing stays even.
 *   A gentle contrast curve on coverage keeps light text on dark
 *   backgrounds from looking thin and dark text on light from bolding.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_font_internal.h"

/* ============ Limits ============ */

#define NXT_MAX_BYTES        8192u    /* longest string accepted */
#define NXT_MAX_GLYPHS       1024u    /* glyphs laid out per call */
#define NXT_PHASES           4u       /* horizontal subpixel positions */
#define NXT_SIZE_Q           8.0f     /* size quantization (1/8 px) */
#define NXT_MIN_PX           1.0f
#define NXT_MAX_PX           NXC_FONT_MAX_PX
#define NXT_PROBES           8u       /* hash probe window */
#define NXT_MAX_CACHED_DIM   160      /* bitmaps up to this w/h are cached */
#define NXT_PATH_PTS         16384u   /* outline scratch path capacity */
#define NXT_PATH_VERBS       8192u
#define NXT_ENTRY_BYTES      sizeof(nxt_entry_t)

#define NXT_REPLACEMENT      0xFFFDu
#define NXT_ELLIPSIS         0x2026u

/* ============ Cache structures ============ */

typedef struct {
    uint32_t font_id;      /* 0 = empty slot */
    uint32_t glyph;
    uint16_t size_q;       /* px * 8 */
    uint8_t  phase;        /* 0..3 */
    uint8_t  empty;        /* 1: glyph has no ink (w == h == 0) */
    int16_t  bx, by;       /* bitmap origin relative to pen (phase removed) */
    uint16_t w, h;
    uint32_t off;          /* arena offset of the bitmap */
    uint32_t stamp;        /* allocation stamp, for ring validity */
} nxt_entry_t;

typedef struct {
    uint32_t     generation;
    nxt_entry_t *entries;
    uint32_t     nentries;     /* power of two */
    uint8_t     *arena;
    uint32_t     arena_bytes;
    uint32_t     head;         /* next free arena byte */
    uint32_t     stamp;        /* increments per allocation */
    uint32_t     hits, misses, evictions;
    nxc_path_t   path;         /* outline scratch (allocated once) */
    int          path_ready;
    uint8_t      lut_light[256];   /* coverage curve for light text */
    uint8_t      lut_dark[256];    /* coverage curve for dark text */
    int          lut_ready;
} nxt_cache_t;

static nxt_cache_t g_text;

/* ============ UTF-8 ============ */

/*
 * Decode one code point from s[0..n), advancing *i. Overlong forms,
 * surrogates, values above U+10FFFF and truncated sequences yield
 * U+FFFD and consume one byte (so garbage never swallows valid text).
 */
static uint32_t utf8_next(const uint8_t *s, uint32_t n, uint32_t *i) {
    uint32_t p = *i;
    uint32_t b0 = s[p];
    if (b0 < 0x80u) { *i = p + 1; return b0; }
    uint32_t need, cp, min;
    if ((b0 & 0xE0u) == 0xC0u)      { need = 1; cp = b0 & 0x1Fu; min = 0x80u; }
    else if ((b0 & 0xF0u) == 0xE0u) { need = 2; cp = b0 & 0x0Fu; min = 0x800u; }
    else if ((b0 & 0xF8u) == 0xF0u) { need = 3; cp = b0 & 0x07u; min = 0x10000u; }
    else { *i = p + 1; return NXT_REPLACEMENT; }
    for (uint32_t k = 1; k <= need; k++) {   /* also catches a truncated tail */
        if (p + k >= n) { *i = p + 1; return NXT_REPLACEMENT; }
        uint32_t b = s[p + k];
        if ((b & 0xC0u) != 0x80u) { *i = p + 1; return NXT_REPLACEMENT; }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        *i = p + 1;
        return NXT_REPLACEMENT;
    }
    *i = p + need + 1;
    return cp;
}

static uint32_t text_len(const char *utf8, int32_t len) {
    if (len >= 0) return (uint32_t)len > NXT_MAX_BYTES ? NXT_MAX_BYTES : (uint32_t)len;
    uint32_t n = 0;
    while (n < NXT_MAX_BYTES && utf8[n]) n++;
    return n;
}

/* ============ Cache management ============ */

static void lut_build(void) {
    /* Light text: cov' = cov^0.82 (fuller stems); dark text: cov^1.18. */
    for (uint32_t i = 0; i < 256; i++) {
        float c = (float)i / 255.0f;
        float l = nxc__powf(c, 0.82f) * 255.0f + 0.5f;
        float d = nxc__powf(c, 1.18f) * 255.0f + 0.5f;
        g_text.lut_light[i] = (uint8_t)(l > 255.0f ? 255.0f : l);
        g_text.lut_dark[i] = (uint8_t)(d > 255.0f ? 255.0f : d);
    }
    g_text.lut_light[0] = 0;
    g_text.lut_dark[0] = 0;
    g_text.lut_light[255] = 255;
    g_text.lut_dark[255] = 255;
    g_text.lut_ready = 1;
}

/* (Re)build the cache layout over the pool when the engine was re-inited. */
static int cache_ready(void) {
    if (!nxc__ready()) return 0;
    if (!g_text.lut_ready) lut_build();
    if (g_text.generation == nxc__generation() && g_text.entries) return 1;
    uint32_t bytes = 0;
    uint8_t *pool = (uint8_t *)nxc__glyph_pool(&bytes);
    if (!pool || bytes < 64u * 1024u) {
        (void)nxc__fail(NXC_ERR_NO_MEMORY, "nxc_text cache");
        return 0;
    }
    /* 1/8 of the pool for the table (rounded down to a power of two),
     * the rest for bitmaps. */
    uint32_t want = (bytes / 8u) / (uint32_t)NXT_ENTRY_BYTES;
    uint32_t n = 1;
    while (n * 2u <= want) n *= 2u;
    if (n < 64u) n = 64u;
    uint32_t table_bytes = (n * (uint32_t)NXT_ENTRY_BYTES + 15u) & ~15u;
    g_text.entries = (nxt_entry_t *)pool;
    g_text.nentries = n;
    nxc__zero(g_text.entries, table_bytes);
    g_text.arena = pool + table_bytes;
    g_text.arena_bytes = bytes - table_bytes;
    g_text.head = 0;
    g_text.stamp = 1;
    g_text.hits = g_text.misses = g_text.evictions = 0;
    g_text.generation = nxc__generation();
    /* The scratch path is tied to the engine's allocator lifetime too. */
    g_text.path_ready = 0;
    return 1;
}

static int path_ready(void) {
    if (g_text.path_ready) return 1;
    if (nxc_path_init_alloc(&g_text.path, NXT_PATH_PTS, NXT_PATH_VERBS) != NXC_OK) return 0;
    g_text.path_ready = 1;
    return 1;
}

static inline uint32_t key_hash(uint32_t font_id, uint32_t glyph, uint32_t size_q, uint32_t phase) {
    uint32_t h = font_id * 0x9E3779B1u;
    h ^= glyph + 0x7F4A7C15u + (h << 6) + (h >> 2);
    h ^= (size_q << 2 | phase) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0xC2B2AE35u;
    h ^= h >> 13;
    return h;
}

static nxt_entry_t *cache_find(uint32_t font_id, uint32_t glyph, uint32_t size_q, uint32_t phase) {
    uint32_t mask = g_text.nentries - 1u;
    uint32_t h = key_hash(font_id, glyph, size_q, phase);
    for (uint32_t k = 0; k < NXT_PROBES; k++) {
        nxt_entry_t *e = &g_text.entries[(h + k) & mask];
        if (e->font_id == 0) return 0;
        if (e->font_id == font_id && e->glyph == glyph && e->size_q == size_q && e->phase == phase) {
            return e;
        }
    }
    return 0;
}

/* Allocate `bytes` in the ring, evicting entries that overlap the range. */
static uint32_t arena_alloc(uint32_t bytes) {
    bytes = (bytes + 15u) & ~15u;
    if (bytes == 0) bytes = 16;
    if (bytes > g_text.arena_bytes) return 0xFFFFFFFFu;
    if (g_text.head + bytes > g_text.arena_bytes) g_text.head = 0;
    uint32_t start = g_text.head, end = start + bytes;
    for (uint32_t i = 0; i < g_text.nentries; i++) {
        nxt_entry_t *e = &g_text.entries[i];
        if (e->font_id == 0 || e->empty) continue;
        uint32_t s = e->off, t = e->off + (uint32_t)e->w * e->h;
        if (s < end && t > start) {
            e->font_id = 0;
            g_text.evictions++;
        }
    }
    g_text.head = end;
    return start;
}

/* Pick the slot for a new key: an empty one in the probe window, else the
 * oldest in the window. Rebuilding tombstones is unnecessary because
 * eviction only ever clears slots. */
static nxt_entry_t *cache_slot(uint32_t font_id, uint32_t glyph, uint32_t size_q, uint32_t phase) {
    uint32_t mask = g_text.nentries - 1u;
    uint32_t h = key_hash(font_id, glyph, size_q, phase);
    nxt_entry_t *oldest = 0;
    for (uint32_t k = 0; k < NXT_PROBES; k++) {
        nxt_entry_t *e = &g_text.entries[(h + k) & mask];
        if (e->font_id == 0) return e;
        if (!oldest || (int32_t)(e->stamp - oldest->stamp) < 0) oldest = e;
    }
    return oldest;
}

/* ============ Glyph rasterization ============ */

typedef struct {
    uint8_t *mask;
    int32_t  w, h;
} mask_sink_t;

static void mask_sink(void *ctx, int32_t y, int32_t x0, const uint8_t *cov, int32_t n) {
    mask_sink_t *m = (mask_sink_t *)ctx;
    if (y < 0 || y >= m->h) return;
    int32_t a = x0 < 0 ? 0 : x0;
    int32_t b = x0 + n > m->w ? m->w : x0 + n;
    if (b <= a) return;
    uint8_t *row = m->mask + (int64_t)y * m->w;
    const uint8_t *src = cov + (a - x0);
    for (int32_t x = a; x < b; x++) row[x] = *src++;
}

typedef struct {
    nxc_canvas_t *c;
    uint32_t      premul;
    const uint8_t *lut;
} canvas_sink_t;

/* Direct-to-canvas sink for uncached glyphs, with the contrast curve. */
static void canvas_sink(void *ctx, int32_t y, int32_t x0, const uint8_t *cov, int32_t n) {
    canvas_sink_t *cs = (canvas_sink_t *)ctx;
    uint64_t cap = 0;
    uint8_t *tmp = (uint8_t *)nxc__scratch(NXC_SCRATCH_COLOR, &cap);   /* raster owns COV */
    if (!tmp) return;
    while (n > 0) {
        int32_t len = (int32_t)(cap < (uint64_t)n ? cap : (uint64_t)n);
        for (int32_t i = 0; i < len; i++) tmp[i] = cs->lut[cov[i]];
        nxc__span_cov(cs->c, y, x0, tmp, len, cs->premul);
        x0 += len; cov += len; n -= len;
    }
}

/* Build the outline of `glyph` at px size with the pen at (x, y). */
static nxc_status_t glyph_outline(const nxc_font_t *f, uint32_t glyph, float px, float x, float y) {
    if (!path_ready()) return NXC_ERR_NO_MEMORY;
    nxc_path_reset(&g_text.path);
    float scale = px / nxc__font_units_per_em(f);
    nxc_status_t st = nxc__font_glyph_outline(f, glyph, &g_text.path, scale, x, y);
    if (st == NXC_ERR_UNSUPPORTED) st = NXC_OK;    /* partial composite: draw what we have */
    return st;
}

/* Render a glyph into the cache; returns the entry or NULL (not cacheable). */
static nxt_entry_t *cache_render(const nxc_font_t *f, uint32_t glyph, float px, uint32_t size_q,
                                 uint32_t phase) {
    uint32_t font_id = nxc__font_id(f);
    float fx = (float)phase / (float)NXT_PHASES;
    if (glyph_outline(f, glyph, px, fx, 0.0f) != NXC_OK) return 0;
    nxc_rect_t b;
    if (nxc_path_bounds(&g_text.path, &b) != NXC_OK) return 0;
    nxt_entry_t *e = cache_slot(font_id, glyph, size_q, phase);
    if (!e) return 0;
    if (g_text.path.nverbs == 0 || b.w <= 0.0f || b.h <= 0.0f) {
        e->font_id = font_id; e->glyph = glyph; e->size_q = (uint16_t)size_q;
        e->phase = (uint8_t)phase; e->empty = 1; e->bx = e->by = 0; e->w = e->h = 0;
        e->off = 0; e->stamp = g_text.stamp++;
        return e;
    }
    int32_t x0 = (int32_t)nxc__floorf(b.x) - 1, y0 = (int32_t)nxc__floorf(b.y) - 1;
    int32_t x1 = (int32_t)nxc__ceilf(b.x + b.w) + 1, y1 = (int32_t)nxc__ceilf(b.y + b.h) + 1;
    int32_t w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0 || w > NXT_MAX_CACHED_DIM || h > NXT_MAX_CACHED_DIM) return 0;
    uint32_t off = arena_alloc((uint32_t)w * (uint32_t)h);
    if (off == 0xFFFFFFFFu) return 0;
    /* The slot may have just been evicted by arena_alloc; re-pick it. */
    e = cache_slot(font_id, glyph, size_q, phase);
    if (!e) return 0;
    mask_sink_t ms;
    ms.mask = g_text.arena + off;
    ms.w = w; ms.h = h;
    nxc__zero(ms.mask, (uint64_t)w * (uint64_t)h);
    nxc_status_t st = nxc__raster_path(&g_text.path, 1.0f, (float)-x0, (float)-y0, NXC_FILL_NONZERO,
                                       nxc_irect(0, 0, w, h), mask_sink, &ms);
    if (st != NXC_OK) return 0;
    e->font_id = font_id; e->glyph = glyph; e->size_q = (uint16_t)size_q;
    e->phase = (uint8_t)phase; e->empty = 0;
    e->bx = (int16_t)x0; e->by = (int16_t)y0;
    e->w = (uint16_t)w; e->h = (uint16_t)h;
    e->off = off; e->stamp = g_text.stamp++;
    g_text.misses++;
    return e;
}

/* ============ Layout ============ */

typedef struct {
    uint32_t glyph;
    float    x;          /* pen x relative to the line start */
} nxt_pos_t;

typedef struct {
    nxt_pos_t pos[NXT_MAX_GLYPHS];
    uint32_t  count;
    float     width;     /* advance width of the laid out glyphs */
    int       truncated;
} nxt_line_t;

static int opts_valid(const nxc_text_opts_t *o) {
    if (!o) return 1;
    return nxc__finitef(o->letter_spacing) && nxc__finitef(o->max_width) &&
           o->max_width >= 0.0f && nxc__absf(o->letter_spacing) <= 1024.0f &&
           (o->align == NXC_ALIGN_LEFT || o->align == NXC_ALIGN_CENTER || o->align == NXC_ALIGN_RIGHT);
}

/* Lay out up to NXT_MAX_GLYPHS glyphs. Width excludes trailing tracking. */
static void layout(const nxc_font_t *f, float px, const uint8_t *s, uint32_t n,
                   const nxc_text_opts_t *o, nxt_line_t *ln) {
    float scale = px / nxc__font_units_per_em(f);
    float tracking = o ? o->letter_spacing : 0.0f;
    int kern = !(o && o->no_kerning) && nxc__font_has_kerning(f);
    uint32_t i = 0, prev = 0;
    float pen = 0.0f;
    ln->count = 0;
    ln->truncated = 0;
    while (i < n && ln->count < NXT_MAX_GLYPHS) {
        uint32_t cp = utf8_next(s, n, &i);
        if (cp == '\n' || cp == '\r') cp = ' ';
        uint32_t g = nxc__font_glyph_index(f, cp);
        if (ln->count > 0) {
            if (kern) pen += (float)nxc__font_kerning(f, prev, g) * scale;
            pen += tracking;
        }
        ln->pos[ln->count].glyph = g;
        ln->pos[ln->count].x = pen;
        ln->count++;
        pen += (float)nxc__font_advance(f, g) * scale;
        prev = g;
    }
    if (i < n) ln->truncated = 1;
    ln->width = pen;
}

/* Truncate to max_width with an ellipsis (U+2026 or "..."). */
static void truncate_line(const nxc_font_t *f, float px, const nxc_text_opts_t *o, nxt_line_t *ln) {
    float maxw = o->max_width;
    if (ln->width <= maxw && !ln->truncated) return;
    float scale = px / nxc__font_units_per_em(f);
    float tracking = o->letter_spacing;
    uint32_t ell = nxc__font_glyph_index(f, NXT_ELLIPSIS);
    uint32_t dots[3];
    uint32_t ndots;
    float ellw;
    if (ell != 0) {
        dots[0] = ell; ndots = 1;
        ellw = (float)nxc__font_advance(f, ell) * scale;
    } else {
        uint32_t dot = nxc__font_glyph_index(f, '.');
        dots[0] = dots[1] = dots[2] = dot; ndots = 3;
        ellw = 3.0f * (float)nxc__font_advance(f, dot) * scale + 2.0f * tracking;
    }
    /* Longest prefix whose width plus the ellipsis fits. */
    uint32_t keep = ln->count;
    while (keep > 0) {
        float w = ln->pos[keep - 1].x + (float)nxc__font_advance(f, ln->pos[keep - 1].glyph) * scale;
        if (w + tracking + ellw <= maxw) break;
        keep--;
    }
    float pen = 0.0f;
    if (keep > 0) {
        pen = ln->pos[keep - 1].x + (float)nxc__font_advance(f, ln->pos[keep - 1].glyph) * scale + tracking;
    }
    ln->count = keep;
    for (uint32_t k = 0; k < ndots && ln->count < NXT_MAX_GLYPHS; k++) {
        if (k > 0) pen += tracking;
        ln->pos[ln->count].glyph = dots[k];
        ln->pos[ln->count].x = pen;
        ln->count++;
        pen += (float)nxc__font_advance(f, dots[k]) * scale;
    }
    ln->width = pen;
    ln->truncated = 1;
}

static int text_args_ok(const nxc_font_t *f, float px, const char *utf8, const nxc_text_opts_t *o) {
    return nxc__font_valid(f) && utf8 && nxc__finitef(px) && px >= NXT_MIN_PX && px <= NXT_MAX_PX &&
           opts_valid(o) && nxc__font_units_per_em(f) > 0.0f;
}

float nxc_text_width(const nxc_font_t *font, float px_size, const char *utf8, int32_t len,
                     const nxc_text_opts_t *opts) {
    if (!text_args_ok(font, px_size, utf8, opts)) {
        (void)nxc__fail(NXC_ERR_INVALID_ARG, "nxc_text_width");
        return 0.0f;
    }
    static nxt_line_t ln;
    uint32_t n = text_len(utf8, len);
    layout(font, px_size, (const uint8_t *)utf8, n, opts, &ln);
    if (opts && opts->max_width > 0.0f) truncate_line(font, px_size, opts, &ln);
    return ln.width;
}

/* ============ Drawing ============ */

static float align_offset(const nxc_text_opts_t *o, float width) {
    if (!o) return 0.0f;
    if (o->align == NXC_ALIGN_CENTER) return -0.5f * width;
    if (o->align == NXC_ALIGN_RIGHT) return -width;
    return 0.0f;
}

/* Perceived luminance 0..255 of a straight-alpha color. */
static uint32_t luminance(nxc_color_t c) {
    uint32_t r = (c >> 16) & 0xFFu, g = (c >> 8) & 0xFFu, b = c & 0xFFu;
    return (r * 54u + g * 183u + b * 19u) >> 8;
}

nxc_status_t nxc_draw_text(nxc_canvas_t *c, const nxc_font_t *font, float px_size,
                           float x, float baseline_y, const char *utf8, int32_t len,
                           nxc_color_t color, const nxc_text_opts_t *opts) {
    if (!c || !text_args_ok(font, px_size, utf8, opts) || !nxc__finitef(x) ||
        !nxc__finitef(baseline_y) || nxc__absf(x) > 1.0e6f || nxc__absf(baseline_y) > 1.0e6f) {
        return nxc__fail(NXC_ERR_INVALID_ARG, "nxc_draw_text");
    }
    if (!cache_ready()) return nxc__fail(NXC_ERR_STATE, "nxc_draw_text");
    uint32_t premul = nxc__premul(color);
    if (premul == 0) return NXC_OK;

    static nxt_line_t ln;
    uint32_t n = text_len(utf8, len);
    layout(font, px_size, (const uint8_t *)utf8, n, opts, &ln);
    if (opts && opts->max_width > 0.0f) truncate_line(font, px_size, opts, &ln);
    if (ln.count == 0) return NXC_OK;

    /* Device origin: snapped baseline, line start with alignment. */
    float ox = x + c->st.tx + align_offset(opts, ln.width);
    int32_t by = (int32_t)nxc__floorf(baseline_y + c->st.ty + 0.5f);
    const uint8_t *lut = luminance(color) >= 128u ? g_text.lut_light : g_text.lut_dark;

    nxc_irect_t clip = nxc__device_clip(c);
    if (clip.w == 0 || clip.h == 0) return NXC_OK;
    /* Whole line above/below the clip: nothing to do (generous bound). */
    if (by + (int32_t)px_size * 2 < clip.y || by - (int32_t)px_size * 2 > clip.y + clip.h) return NXC_OK;

    uint32_t size_q = (uint32_t)(px_size * NXT_SIZE_Q + 0.5f);
    if (size_q == 0) size_q = 1;
    uint32_t font_id = nxc__font_id(font);
    uint64_t cap = 0;
    uint8_t *tmp = (uint8_t *)nxc__scratch(NXC_SCRATCH_COV, &cap);
    if (!tmp) return nxc__fail(NXC_ERR_STATE, "nxc_draw_text");
    nxc_status_t worst = NXC_OK;

    for (uint32_t k = 0; k < ln.count; k++) {
        float gx = ox + ln.pos[k].x;
        float fl = nxc__floorf(gx);
        int32_t ix = (int32_t)fl;
        uint32_t phase = (uint32_t)((gx - fl) * (float)NXT_PHASES + 0.5f);
        if (phase >= NXT_PHASES) { phase = 0; ix++; }
        /* Horizontal reject before touching the cache. */
        if (ix > clip.x + clip.w + (int32_t)px_size * 2 || ix + (int32_t)px_size * 3 < clip.x) continue;

        nxt_entry_t *e = cache_find(font_id, ln.pos[k].glyph, size_q, phase);
        if (e) {
            g_text.hits++;
        } else {
            e = cache_render(font, ln.pos[k].glyph, px_size, size_q, phase);
        }
        if (e) {
            if (e->empty) continue;
            /* Bitmaps were rendered with the raster using scratch COV; it is free again now. */
            const uint8_t *bm = g_text.arena + e->off;
            int32_t dx = ix + e->bx, dy = by + e->by;
            int32_t w = e->w;
            if (dy + e->h <= clip.y || dy >= clip.y + clip.h) continue;
            for (int32_t row = 0; row < e->h; row++) {
                int32_t yy = dy + row;
                if (yy < clip.y) continue;
                if (yy >= clip.y + clip.h) break;
                const uint8_t *src = bm + (int64_t)row * w;
                int32_t len_px = w < (int32_t)cap ? w : (int32_t)cap;
                int any = 0;
                for (int32_t i = 0; i < len_px; i++) { tmp[i] = lut[src[i]]; any |= tmp[i]; }
                if (any) nxc__span_cov(c, yy, dx, tmp, len_px, premul);
            }
            continue;
        }
        /* Not cacheable (huge or capacity): rasterize to the canvas. */
        if (glyph_outline(font, ln.pos[k].glyph, px_size,
                          (float)ix + (float)phase / (float)NXT_PHASES, (float)by) != NXC_OK) {
            worst = NXC_ERR_OVERFLOW;
            continue;
        }
        canvas_sink_t cs;
        cs.c = c; cs.premul = premul; cs.lut = lut;
        nxc_status_t st = nxc__raster_path(&g_text.path, 1.0f, 0.0f, 0.0f, NXC_FILL_NONZERO,
                                           clip, canvas_sink, &cs);
        if (st != NXC_OK) worst = st;
    }
    if (worst != NXC_OK) return nxc__fail(worst, "nxc_draw_text");
    return NXC_OK;
}
