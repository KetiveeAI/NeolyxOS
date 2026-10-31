/*
 * NeolyxOS - NXGlass: the shared glassmorphism layer (see nxglass.h)
 *
 * Every glass surface is: soft shadow (nxui_elevation) + backdrop (the
 * pre-blurred wallpaper, saturated, tinted, grained) + light treatment
 * (diagonal sheen from the top-left, soft inner glow, a 1px rim whose
 * gradient is brightest at the top-left, and an outer definition edge).
 * Static panels go through the NXUI panel cache with the light treatment
 * baked in, so they cost one blit per frame; only live panels re-shade.
 *
 * Design tokens and usage: docs/desktop/glass.md. The Glass area owns
 * this file; callers depend only on nxglass.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxglass.h"
#include "nxicons.h"
#include "nxsyscall.h"

/* ============ Small helpers ============ */

static void glass_log(const char *msg) { nx_debug_print(msg); }

static float g_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float g_minf(float a, float b) { return a < b ? a : b; }
static float g_maxf(float a, float b) { return a > b ? a : b; }

/* A rect is drawable when its size is positive, finite and sane. */
static int rect_ok(nxc_rect_t r) {
    return r.w > 0.0f && r.h > 0.0f && r.w < 16384.0f && r.h < 16384.0f &&
           r.x > -16384.0f && r.x < 16384.0f && r.y > -16384.0f && r.y < 16384.0f;
}

static nxc_rect_t inset(nxc_rect_t r, float d) {
    return nxc_rect(r.x + d, r.y + d, r.w - 2.0f * d, r.h - 2.0f * d);
}

/* ============ Tokens and levels ============ */

/* Per-level recipe: backdrop tint and the light treatment (alphas). */
typedef struct {
    nxc_color_t tint;          /* blended over the saturated blur */
    float       saturation;
    uint8_t     noise;         /* grain against banding */
    uint8_t     rim_top;       /* rim alpha at the top-left */
    uint8_t     rim_bottom;    /* rim alpha at the bottom-right */
    uint8_t     sheen;         /* diagonal light from the top-left */
    uint8_t     glow;          /* inner glow next to the rim */
    uint8_t     edge;          /* outer definition hairline */
} glass_spec_t;

/* Dark: navy glass that lets the aurora through, white light. */
static const glass_spec_t g_spec_dark[NXGLASS_LEVEL_COUNT] = {
    [NXGLASS_ULTRATHIN] = { NXC_ARGB(84, 0x0D, 0x14, 0x24), 1.80f, 6, 92, 14, 18, 12, 70 },
    [NXGLASS_THIN]      = { NXC_ARGB(120, 0x0D, 0x14, 0x24), 1.70f, 6, 84, 13, 15, 10, 72 },
    [NXGLASS_REGULAR]   = { NXC_ARGB(160, 0x0D, 0x14, 0x24), 1.60f, 6, 76, 12, 13,  9, 76 },
    [NXGLASS_THICK]     = { NXC_ARGB(202, 0x10, 0x18, 0x2A), 1.45f, 5, 62, 10,  9,  7, 80 },
    [NXGLASS_CHROME]    = { NXC_ARGB(150, 0x07, 0x0B, 0x14), 1.50f, 6, 40,  8,  6,  0, 60 },
};

/* Light: milky glass, white rim on top, a faint dark rim at the bottom. */
static const glass_spec_t g_spec_light[NXGLASS_LEVEL_COUNT] = {
    [NXGLASS_ULTRATHIN] = { NXC_ARGB(150, 0xF4, 0xF6, 0xFA), 1.85f, 5, 235, 30, 64, 46, 24 },
    [NXGLASS_THIN]      = { NXC_ARGB(174, 0xF4, 0xF6, 0xFA), 1.80f, 5, 225, 28, 56, 42, 26 },
    [NXGLASS_REGULAR]   = { NXC_ARGB(194, 0xF5, 0xF6, 0xF9), 1.75f, 5, 215, 26, 48, 38, 28 },
    [NXGLASS_THICK]     = { NXC_ARGB(214, 0xF1, 0xF3, 0xF7), 1.55f, 4, 200, 24, 36, 30, 30 },
    [NXGLASS_CHROME]    = { NXC_ARGB(186, 0xF6, 0xF7, 0xFA), 1.60f, 5, 170, 18, 24,  0, 22 },
};

typedef struct {
    int               built;
    nxui_appearance_t appearance;
    nxc_color_t       accent;
    nxglass_tokens_t  tok;
    nxc_material_t    mat[NXGLASS_LEVEL_COUNT];
} glass_state_t;

static glass_state_t g_glass;

static const glass_spec_t *glass_spec(nxglass_level_t level) {
    if ((int)level < 0 || level >= NXGLASS_LEVEL_COUNT) level = NXGLASS_REGULAR;
    return nxui_appearance() == NXUI_APPEARANCE_LIGHT ? &g_spec_light[level] : &g_spec_dark[level];
}

static void tokens_build(void) {
    const nxui_theme_t *th = nxui_theme();
    nxui_appearance_t ap = nxui_appearance();
    nxglass_tokens_t *t = &g_glass.tok;

    t->radius_small = 6.0f;
    t->radius_control = th->radius_control > 0.0f ? th->radius_control : 8.0f;
    t->radius_card = 12.0f;
    t->radius_panel = th->radius_popover > 0.0f ? th->radius_popover : 14.0f;
    t->radius_sheet = 18.0f;
    t->spacing = 8.0f;

    t->accent = th->accent;
    t->accent_hover = nxc_color_lerp(th->accent, NXC_RGB(0xFF, 0xFF, 0xFF), 0.12f);
    t->accent_pressed = th->accent_pressed;
    t->accent_soft = NXC_WITH_ALPHA(th->accent, ap == NXUI_APPEARANCE_LIGHT ? 46 : 64);
    t->focus_ring = NXC_WITH_ALPHA(th->accent, 125);
    t->on_accent = NXC_RGB(0xFF, 0xFF, 0xFF);
    t->success = th->success;
    t->destructive = th->destructive;
    t->info = NXC_RGB(0x0E, 0xA5, 0xE9);

    if (ap == NXUI_APPEARANCE_LIGHT) {
        t->label            = NXC_ARGB(232, 0x10, 0x14, 0x1E);
        t->label_secondary  = NXC_ARGB(158, 0x10, 0x14, 0x1E);
        t->label_tertiary   = NXC_ARGB(105, 0x10, 0x14, 0x1E);
        t->label_quaternary = NXC_ARGB(62, 0x10, 0x14, 0x1E);
        t->fill         = NXC_ARGB(16, 0x00, 0x00, 0x10);
        t->fill_hover   = NXC_ARGB(26, 0x00, 0x00, 0x10);
        t->fill_pressed = NXC_ARGB(40, 0x00, 0x00, 0x10);
        t->fill_inset   = NXC_ARGB(150, 0xFF, 0xFF, 0xFF);
        t->raised       = NXC_ARGB(255, 0xFF, 0xFF, 0xFF);
        t->rim_top      = NXC_ARGB(225, 0xFF, 0xFF, 0xFF);
        t->rim_bottom   = NXC_ARGB(26, 0x00, 0x00, 0x10);
        t->edge         = NXC_ARGB(30, 0x00, 0x00, 0x10);
        t->separator    = NXC_ARGB(28, 0x00, 0x00, 0x10);
        t->warning      = NXC_RGB(0xC2, 0x6A, 0x05);
    } else {
        t->label            = NXC_ARGB(242, 0xF0, 0xF4, 0xFF);
        t->label_secondary  = NXC_ARGB(168, 0xE2, 0xE9, 0xF8);
        t->label_tertiary   = NXC_ARGB(108, 0xE2, 0xE9, 0xF8);
        t->label_quaternary = NXC_ARGB(66, 0xE2, 0xE9, 0xF8);
        t->fill         = NXC_ARGB(24, 0xFF, 0xFF, 0xFF);
        t->fill_hover   = NXC_ARGB(36, 0xFF, 0xFF, 0xFF);
        t->fill_pressed = NXC_ARGB(50, 0xFF, 0xFF, 0xFF);
        t->fill_inset   = NXC_ARGB(70, 0x00, 0x02, 0x0A);
        t->raised       = NXC_ARGB(52, 0xFF, 0xFF, 0xFF);
        t->rim_top      = NXC_ARGB(70, 0xFF, 0xFF, 0xFF);
        t->rim_bottom   = NXC_ARGB(10, 0xFF, 0xFF, 0xFF);
        t->edge         = NXC_ARGB(80, 0x00, 0x02, 0x08);
        t->separator    = NXC_ARGB(30, 0xFF, 0xFF, 0xFF);
        t->warning      = NXC_RGB(0xF5, 0xA5, 0x24);
    }

    for (int i = 0; i < NXGLASS_LEVEL_COUNT; i++) {
        const glass_spec_t *s = glass_spec((nxglass_level_t)i);
        nxc_material_t *m = &g_glass.mat[i];
        m->blur_sigma = 24.0f;           /* only used for live blur (no pre-blurred source) */
        m->saturation = s->saturation;
        m->tint = s->tint;
        m->noise = s->noise;
        m->border = NXC_ARGB(0, 0, 0, 0); /* the rim is drawn by glass_decor */
    }
    g_glass.appearance = ap;
    g_glass.accent = th->accent;
    g_glass.built = 1;
}

static const nxglass_tokens_t *tok(void) {
    if (!g_glass.built || g_glass.appearance != nxui_appearance() ||
        g_glass.accent != nxui_theme()->accent) {
        tokens_build();
    }
    return &g_glass.tok;
}

const nxglass_tokens_t *nxglass_tokens(void) { return tok(); }

const nxc_material_t *nxglass_material(nxglass_level_t level) {
    (void)tok();
    if ((int)level < 0 || level >= NXGLASS_LEVEL_COUNT) level = NXGLASS_REGULAR;
    return &g_glass.mat[level];
}

/* ============ Light treatment ============ */

/* Fill the 1px ring between r and r inset by `width` with a paint. */
static nxc_status_t ring_paint(nxc_canvas_t *c, nxc_rect_t r, float radius, float width,
                               const nxc_paint_t *p) {
    if (!(r.w > 2.0f * width) || !(r.h > 2.0f * width)) return NXC_OK;
    nxc_point_t pts[96];
    uint8_t verbs[64];
    nxc_path_t path;
    nxc_status_t st = nxc_path_init(&path, pts, 96, verbs, 64);
    if (st != NXC_OK) return st;
    float rad = g_minf(radius, 0.5f * g_minf(r.w, r.h));
    nxc_path_add_rrect(&path, r, rad);
    nxc_path_add_rrect(&path, inset(r, width), g_maxf(rad - width, 0.0f));
    if (path.error != NXC_OK) return path.error;
    return nxc_fill_path_paint(c, &path, NXC_FILL_EVENODD, p);
}

/* Rim: brightest at the top-left, fading to rim_bottom at the bottom-right. */
static nxc_status_t rim_gradient(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                 nxc_color_t top, nxc_color_t bottom) {
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x + r.w * 0.30f, r.y + r.h, top, bottom);
    p.nstops = 0;
    nxc_paint_add_stop(&p, 0.0f, top);
    nxc_paint_add_stop(&p, 0.28f, nxc_color_lerp(top, bottom, 0.62f));
    nxc_paint_add_stop(&p, 1.0f, bottom);
    return ring_paint(c, r, radius, 1.0f, &p);
}

/* Per-frame rim for small controls: the same top-to-bottom light as
 * rim_gradient, as three clipped analytic strokes (much cheaper than a
 * gradient-filled ring path; bands are invisible at control sizes). */
static nxc_status_t rim_bands(nxc_canvas_t *c, nxc_rect_t r, float radius,
                              nxc_color_t top, nxc_color_t bottom) {
    if (!(r.w > 2.0f) || !(r.h > 2.0f)) return NXC_OK;
    const float cut[4] = { 0.0f, 0.34f, 0.66f, 1.0f };
    const nxc_color_t col[3] = { top, nxc_color_lerp(top, bottom, 0.62f), bottom };
    nxc_status_t st = NXC_OK;
    for (int i = 0; i < 3 && st == NXC_OK; i++) {
        if ((col[i] >> 24) == 0) continue;
        float y0 = r.y + r.h * cut[i], y1 = r.y + r.h * cut[i + 1];
        int32_t iy0 = (int32_t)(y0 + 0.5f), iy1 = (int32_t)(y1 + 0.5f);
        if (i == 0) iy0 = (int32_t)r.y - 1;
        if (i == 2) iy1 = (int32_t)(r.y + r.h) + 2;
        if (iy1 <= iy0) continue;
        st = nxc_save(c);
        if (st != NXC_OK) return st;
        nxc_clip_rect(c, nxc_irect((int32_t)r.x - 1, iy0, (int32_t)(r.w + 0.5f) + 3, iy1 - iy0));
        st = nxc_stroke_rrect(c, r, radius, 1.0f, col[i]);
        nxc_restore(c);
    }
    return st;
}

/* Bottom-color for a rim in the current appearance: light mode darkens. */
static nxc_color_t rim_bottom_color(uint8_t alpha) {
    return nxui_appearance() == NXUI_APPEARANCE_LIGHT ? NXC_ARGB(alpha, 0x00, 0x00, 0x10)
                                                      : NXC_ARGB(alpha, 0xFF, 0xFF, 0xFF);
}

/* Decoration for a glass level; key = level | flags << 8. Pure function of
 * (r, radius, key, appearance), so the panel cache can bake it. */
#define GLASS_DECOR_NO_EDGE 0x100u

static nxc_status_t glass_decor(nxc_canvas_t *c, nxc_rect_t r, float radius, uint32_t key) {
    if (!c || !rect_ok(r)) return NXC_ERR_INVALID_ARG;
    nxglass_level_t level = (nxglass_level_t)(key & 0xFFu);
    const glass_spec_t *s = glass_spec(level);
    float rad = g_minf(radius, 0.5f * g_minf(r.w, r.h));
    nxc_status_t st = NXC_OK;

    /* Sheen: a soft diagonal wash of light from the top-left corner,
     * limited in reach so large panels are not washed out. */
    if (s->sheen) {
        float ex = g_minf(r.w, 360.0f) * 0.62f, ey = g_minf(r.h, 360.0f) * 0.85f;
        nxc_color_t w = NXC_ARGB(s->sheen, 0xFF, 0xFF, 0xFF);
        nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x + ex, r.y + ey, w, NXC_WITH_ALPHA(w, 0));
        p.nstops = 0;
        nxc_paint_add_stop(&p, 0.0f, w);
        nxc_paint_add_stop(&p, 0.45f, NXC_WITH_ALPHA(w, s->sheen / 3));
        nxc_paint_add_stop(&p, 1.0f, NXC_WITH_ALPHA(w, 0));
        st = nxc_fill_rrect_paint(c, r, rad, &p);
        if (st != NXC_OK) return st;
    }

    /* Inner glow: three hairlines stepping inward, fading out. */
    if (s->glow && r.w > 12.0f && r.h > 12.0f) {
        for (int i = 0; i < 3; i++) {
            float d = 1.0f + (float)i;
            uint8_t a = (uint8_t)((s->glow * (3 - i)) / 3);
            st = nxc_stroke_rrect(c, inset(r, d), g_maxf(rad - d, 0.0f), 1.0f,
                                  NXC_ARGB(a, 0xFF, 0xFF, 0xFF));
            if (st != NXC_OK) return st;
        }
    }

    /* Rim light, brighter at the top-left. */
    st = rim_gradient(c, r, rad, NXC_ARGB(s->rim_top, 0xFF, 0xFF, 0xFF), rim_bottom_color(s->rim_bottom));
    if (st != NXC_OK) return st;

    /* Outer edge: separates the glass from busy or bright backdrops. */
    if (s->edge && !(key & GLASS_DECOR_NO_EDGE)) {
        nxc_color_t e = NXC_ARGB(s->edge, 0x00, 0x02, 0x08);
        st = nxc_stroke_rrect(c, inset(r, -1.0f), rad + 1.0f, 1.0f, e);
    }
    return st;
}

static nxc_status_t glass_draw(nxc_canvas_t *c, nxc_rect_t r, float radius, nxglass_level_t level,
                               int32_t elevation, int live, const nxc_material_t *m, uint32_t flags) {
    if (!c || !m) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r) || !(radius >= 0.0f)) return r.w <= 0.0f || r.h <= 0.0f ? NXC_OK : NXC_ERR_INVALID_ARG;
    uint32_t key = ((uint32_t)level & 0xFFu) | flags;
    if (!live) {
        nxc_status_t st = nxui_panel_ex(c, r, radius, m, elevation, glass_decor, key);
        if (st == NXC_OK) return NXC_OK;
        /* Cache refused (e.g. out of memory): fall through to direct drawing. */
    }
    if (elevation > 0) {
        nxc_status_t st = nxui_elevation(c, r, radius, elevation);
        if (st != NXC_OK) return st;
    }
    nxc_status_t st = nxui_material(c, r, radius, m);
    if (st != NXC_OK) return st;
    return glass_decor(c, r, radius, key);
}

nxc_status_t nxglass_panel(nxc_canvas_t *c, nxc_rect_t r, float radius,
                           nxglass_level_t level, int32_t elevation, int live) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (r.w <= 0.0f || r.h <= 0.0f) return NXC_OK;
    if ((int)level < 0 || level >= NXGLASS_LEVEL_COUNT) level = NXGLASS_REGULAR;
    return glass_draw(c, r, radius, level, elevation, live, nxglass_material(level), 0);
}

nxc_status_t nxglass_panel_tinted(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                  nxglass_level_t level, int32_t elevation, int live,
                                  nxc_color_t tint) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (r.w <= 0.0f || r.h <= 0.0f) return NXC_OK;
    if ((int)level < 0 || level >= NXGLASS_LEVEL_COUNT) level = NXGLASS_REGULAR;
    nxc_material_t m = *nxglass_material(level);
    uint8_t a = (uint8_t)(m.tint >> 24);
    float k = nxui_appearance() == NXUI_APPEARANCE_LIGHT ? 0.22f : 0.30f;
    m.tint = NXC_WITH_ALPHA(nxc_color_lerp(NXC_WITH_ALPHA(m.tint, 255), NXC_WITH_ALPHA(tint, 255), k), a);
    return glass_draw(c, r, radius, level, elevation, live, &m, 0);
}

nxc_status_t nxglass_rim(nxc_canvas_t *c, nxc_rect_t r, float radius, nxglass_level_t level) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    if ((int)level < 0 || level >= NXGLASS_LEVEL_COUNT) level = NXGLASS_REGULAR;
    return glass_decor(c, r, radius, (uint32_t)level);
}

nxc_status_t nxglass_scrim(nxc_canvas_t *c, nxc_rect_t r, float amount) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    amount = g_clampf(amount, 0.0f, 1.0f);
    uint8_t a = (uint8_t)(amount * (nxui_appearance() == NXUI_APPEARANCE_LIGHT ? 90.0f : 125.0f));
    if (a == 0) return NXC_OK;
    return nxc_fill_rect(c, r, NXC_ARGB(a, 0x00, 0x02, 0x0A));
}

nxc_status_t nxglass_card(nxc_canvas_t *c, nxc_rect_t r, float radius) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    nxc_status_t st = nxc_fill_rrect(c, r, radius, light ? NXC_ARGB(150, 0xFF, 0xFF, 0xFF)
                                                         : NXC_ARGB(16, 0xFF, 0xFF, 0xFF));
    if (st != NXC_OK) return st;
    if (light) st = nxc_stroke_rrect(c, r, radius, 1.0f, t->separator);
    if (st != NXC_OK) return st;
    return rim_bands(c, r, radius, light ? NXC_ARGB(200, 0xFF, 0xFF, 0xFF) : NXC_ARGB(34, 0xFF, 0xFF, 0xFF),
                     light ? NXC_ARGB(10, 0, 0, 0x10) : NXC_ARGB(8, 0xFF, 0xFF, 0xFF));
}

nxc_status_t nxglass_sidebar(nxc_canvas_t *c, nxc_rect_t r) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const glass_spec_t *s = glass_spec(NXGLASS_THICK);
    nxc_color_t base = NXC_WITH_ALPHA(s->tint, 255);
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    /* Vertical gradient: slightly lighter at the top, like the panel sheen. */
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h,
                                     NXC_WITH_ALPHA(nxc_color_lerp(base, NXC_RGB(0xFF, 0xFF, 0xFF), light ? 0.25f : 0.05f), 150),
                                     NXC_WITH_ALPHA(base, 170));
    nxc_status_t st = nxc_fill_rect_paint(c, r, &p);
    if (st != NXC_OK) return st;
    return nxc_fill_rect(c, nxc_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), tok()->separator);
}

void nxglass_separator(nxc_canvas_t *c, float x, float y, float w) {
    if (!c || !(w > 0.0f) || w > 16384.0f) return;
    nxc_fill_rect(c, nxc_rect(x, y, w, 1.0f), tok()->separator);
}

/* ============ Controls ============ */

int nxglass_hit(nxc_rect_t r, int32_t x, int32_t y) {
    return (float)x >= r.x && (float)x < r.x + r.w && (float)y >= r.y && (float)y < r.y + r.h;
}

/* Raised control: optional contact shadow (light), fill, rim. */
static void control_rim(nxc_canvas_t *c, nxc_rect_t r, float radius, uint8_t top_a) {
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    rim_bands(c, r, radius, NXC_ARGB(top_a, 0xFF, 0xFF, 0xFF),
              light ? NXC_ARGB(30, 0, 0, 0x10) : NXC_ARGB(6, 0xFF, 0xFF, 0xFF));
}

static void focus_ring(nxc_canvas_t *c, nxc_rect_t r, float radius) {
    nxc_stroke_rrect(c, inset(r, -3.0f), radius + 3.0f, 3.0f, tok()->focus_ring);
}

/* Vertical gradient fill of a color: lighter at the top. */
static void fill_gloss(nxc_canvas_t *c, nxc_rect_t r, float radius, nxc_color_t color, float lift) {
    nxc_color_t top = NXC_WITH_ALPHA(nxc_color_lerp(color, NXC_RGB(0xFF, 0xFF, 0xFF), lift), color >> 24);
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h, top, color);
    nxc_fill_rrect_paint(c, r, radius, &p);
}

nxc_status_t nxglass_button(nxc_canvas_t *c, nxc_rect_t r, const char *label,
                            nxglass_button_kind_t kind, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    float rad = g_minf(t->radius_control, r.h * 0.5f);
    int pressed = (state & NXGLASS_PRESSED) != 0, hover = (state & NXGLASS_HOVER) != 0;
    nxc_color_t text = t->label;

    nxc_save(c);
    if (state & NXGLASS_DISABLED) { nxc_set_alpha(c, 105); pressed = hover = 0; }
    switch (kind) {
        case NXGLASS_BUTTON_PRIMARY:
        case NXGLASS_BUTTON_DESTRUCTIVE: {
            nxc_color_t base = kind == NXGLASS_BUTTON_PRIMARY ? t->accent : t->destructive;
            if (pressed) base = nxc_color_lerp(base, NXC_RGB(0, 0, 0), 0.14f);
            else if (hover) base = nxc_color_lerp(base, NXC_RGB(0xFF, 0xFF, 0xFF), 0.08f);
            nxui_elevation(c, r, rad, 1);
            fill_gloss(c, r, rad, base, pressed ? 0.04f : 0.16f);
            control_rim(c, r, rad, pressed ? 40 : 90);
            text = t->on_accent;
            break;
        }
        case NXGLASS_BUTTON_PLAIN:
            if (pressed || hover) nxc_fill_rrect(c, r, rad, pressed ? t->fill_pressed : t->fill_hover);
            text = pressed ? t->accent_pressed : t->accent;
            break;
        default:
            if (light) {
                /* Raised white key with a contact shadow. */
                nxui_elevation(c, r, rad, 1);
                nxc_fill_rrect(c, r, rad, pressed ? NXC_RGB(0xE6, 0xE8, 0xEE)
                                         : hover ? NXC_RGB(0xF7, 0xF8, 0xFA) : t->raised);
                nxc_stroke_rrect(c, r, rad, 1.0f, NXC_ARGB(22, 0, 0, 0x10));
            } else {
                nxc_fill_rrect(c, r, rad, pressed ? t->fill_pressed : hover ? t->fill_hover : t->fill);
                control_rim(c, r, rad, pressed ? 30 : 62);
            }
            break;
    }
    if (state & NXGLASS_FOCUSED) focus_ring(c, r, rad);
    if (label && label[0]) nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, inset(r, 4.0f), label, text, NXC_ALIGN_CENTER);
    nxc_restore(c);
    return NXC_OK;
}

/* Knob: white disc/pill with a soft shadow and a hairline. */
static void draw_knob(nxc_canvas_t *c, nxc_rect_t k) {
    float rad = 0.5f * g_minf(k.w, k.h);
    nxc_draw_shadow_rrect(c, k, rad, 1.6f, 0.0f, 1.0f, 0.0f, NXC_ARGB(70, 0, 0, 0x08));
    nxc_draw_shadow_rrect(c, k, rad, 0.6f, 0.0f, 0.3f, 0.0f, NXC_ARGB(40, 0, 0, 0x08));
    nxc_paint_t p = nxc_paint_linear(k.x, k.y, k.x, k.y + k.h, NXC_RGB(0xFF, 0xFF, 0xFF), NXC_RGB(0xF1, 0xF2, 0xF6));
    nxc_fill_rrect_paint(c, k, rad, &p);
    nxc_stroke_rrect(c, k, rad, 0.75f, NXC_ARGB(26, 0, 0, 0x10));
}

nxc_rect_t nxglass_toggle_rect(float x, float y) {
    return nxc_rect(x, y, NXGLASS_TOGGLE_W, NXGLASS_TOGGLE_H);
}

nxc_status_t nxglass_toggle(nxc_canvas_t *c, float x, float y, float on_t, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxglass_tokens_t *t = tok();
    if (!(on_t >= 0.0f)) on_t = 0.0f;     /* also catches NaN */
    if (on_t > 1.0f) on_t = 1.0f;
    nxc_rect_t r = nxglass_toggle_rect(x, y);
    float e = nxui_ease_in_out_cubic(on_t);
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    /* Track: inset glass when off, accent when on (cross-faded). */
    nxc_fill_rrect(c, r, r.h * 0.5f, (state & NXGLASS_HOVER) ? t->fill_hover : t->fill_pressed);
    if (e > 0.0f) {
        nxc_save(c);
        nxc_set_alpha(c, (uint8_t)(255.0f * e + 0.5f));
        fill_gloss(c, r, r.h * 0.5f, t->accent, 0.12f);
        nxc_restore(c);
    }
    rim_bands(c, r, r.h * 0.5f, NXC_ARGB(0, 0, 0, 0), NXC_ARGB(40, 0xFF, 0xFF, 0xFF));
    /* Knob slides; pressing stretches it toward the travel direction. */
    float kd = r.h - 4.0f;
    float stretch = (state & NXGLASS_PRESSED) ? 5.0f : 0.0f;
    float kw = kd + stretch;
    float kx = r.x + 2.0f + e * (r.w - 4.0f - kw);
    draw_knob(c, nxc_rect(kx, r.y + 2.0f, kw, kd));
    if (state & NXGLASS_FOCUSED) focus_ring(c, r, r.h * 0.5f);
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_slider(nxc_canvas_t *c, nxc_rect_t r, float value, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    float kr = (state & NXGLASS_PRESSED) ? 9.5f : 8.5f;
    float cy = r.y + r.h * 0.5f;
    /* Keep the knob inside r. */
    float x0 = r.x + kr, x1 = r.x + r.w - kr;
    if (x1 < x0) x1 = x0;
    float kx = x0 + (x1 - x0) * value;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    nxc_rect_t track = nxc_rect(r.x, cy - 2.5f, r.w, 5.0f);
    nxc_fill_rrect(c, track, 2.5f, t->fill_pressed);
    if (kx - r.x > 0.0f) fill_gloss(c, nxc_rect(r.x, cy - 2.5f, kx - r.x, 5.0f), 2.5f, t->accent, 0.15f);
    if (state & (NXGLASS_HOVER | NXGLASS_PRESSED))
        nxc_fill_circle(c, kx, cy, kr + 5.0f, NXC_WITH_ALPHA(t->accent, 46));
    draw_knob(c, nxc_rect(kx - kr, cy - kr, 2.0f * kr, 2.0f * kr));
    if (state & NXGLASS_FOCUSED) nxc_stroke_circle(c, kx, cy, kr + 3.0f, 2.5f, t->focus_ring);
    nxc_restore(c);
    return NXC_OK;
}

float nxglass_slider_value_at(nxc_rect_t r, int32_t x) {
    /* Matches the knob travel of nxglass_slider (8.5 px inset each side). */
    float x0 = r.x + 8.5f, x1 = r.x + r.w - 8.5f;
    if (!(x1 > x0)) return 0.0f;
    float v = ((float)x - x0) / (x1 - x0);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

nxc_status_t nxglass_segmented(nxc_canvas_t *c, nxc_rect_t r, const char *const *labels,
                               int count, int selected, int hovered) {
    if (!c || !labels || count <= 0 || count > 64) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    float rad = g_minf(t->radius_control, r.h * 0.5f);
    nxc_fill_rrect(c, r, rad, t->fill);
    float sw = r.w / (float)count;
    for (int i = 0; i < count; i++) {
        nxc_rect_t s = nxc_rect(r.x + sw * (float)i, r.y, sw, r.h);
        nxc_rect_t k = inset(s, 2.0f);
        if (i == selected) {
            if (light) nxui_elevation(c, k, rad - 2.0f, 1);
            nxc_fill_rrect(c, k, rad - 2.0f, t->raised);
            control_rim(c, k, rad - 2.0f, light ? 0 : 60);
        } else if (i == hovered) {
            nxc_fill_rrect(c, k, rad - 2.0f, t->fill_hover);
        }
        /* Dividers only between two unselected segments. */
        if (i > 0 && i != selected && i - 1 != selected)
            nxc_fill_rect(c, nxc_rect(s.x, s.y + 7.0f, 1.0f, s.h - 14.0f), t->separator);
        if (labels[i])
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, inset(s, 4.0f), labels[i],
                              i == selected ? t->label : t->label_secondary, NXC_ALIGN_CENTER);
    }
    return NXC_OK;
}

int nxglass_segment_at(nxc_rect_t r, int count, int32_t x, int32_t y) {
    if (count <= 0 || !(r.w > 0.0f) || !nxglass_hit(r, x, y)) return -1;
    int i = (int)(((float)x - r.x) / (r.w / (float)count));
    return i < 0 ? 0 : (i >= count ? count - 1 : i);
}

nxc_status_t nxglass_row(nxc_canvas_t *c, nxc_rect_t r, int icon, const char *title,
                         const char *detail, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int sel = (state & NXGLASS_SELECTED) != 0;
    float rad = t->radius_control - 2.0f;
    if (sel) {
        fill_gloss(c, r, rad, NXC_WITH_ALPHA(t->accent, 232), 0.08f);
    } else if (state & NXGLASS_PRESSED) {
        nxc_fill_rrect(c, r, rad, t->fill_pressed);
    } else if (state & NXGLASS_HOVER) {
        nxc_fill_rrect(c, r, rad, t->fill_hover);
    }
    nxc_color_t tc = (state & NXGLASS_DISABLED) ? t->label_quaternary : sel ? t->on_accent : t->label;
    float tx = r.x + 10.0f;
    if (icon >= 0 && icon < (int)NXSYM_COUNT) {
        nxsym_draw(c, (nxsym_t)icon, r.x + 20.0f, r.y + r.h * 0.5f, 16.0f,
                   sel ? t->on_accent : (state & NXGLASS_DISABLED) ? t->label_quaternary : t->accent, 1.0f);
        tx = r.x + 36.0f;
    }
    float dw = (detail && detail[0]) ? nxui_text_width(NXUI_TEXT_BODY, detail) + 14.0f : 0.0f;
    if (dw > r.w * 0.5f) dw = r.w * 0.5f;
    if (title && r.x + r.w - tx - dw > 0.0f)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(tx, r.y, r.x + r.w - tx - dw, r.h), title, tc, NXC_ALIGN_LEFT);
    if (dw > 0.0f)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(r.x + r.w - dw, r.y, dw - 10.0f, r.h), detail,
                          sel ? NXC_WITH_ALPHA(t->on_accent, 200) : t->label_secondary, NXC_ALIGN_RIGHT);
    return NXC_OK;
}

nxc_status_t nxglass_field(nxc_canvas_t *c, nxc_rect_t r, const char *text,
                           const char *placeholder, int search, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    float rad = search ? r.h * 0.5f : t->radius_control - 1.0f;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    nxc_fill_rrect(c, r, rad, t->fill_inset);
    /* Inset rim: darker at the top, catching light at the bottom. */
    rim_bands(c, r, rad, light ? NXC_ARGB(40, 0, 0, 0x10) : NXC_ARGB(70, 0, 0, 0x08),
              light ? NXC_ARGB(14, 0, 0, 0x10) : NXC_ARGB(26, 0xFF, 0xFF, 0xFF));
    if (state & NXGLASS_FOCUSED) {
        focus_ring(c, r, rad);
        nxc_stroke_rrect(c, r, rad, 1.0f, NXC_WITH_ALPHA(t->accent, 220));
    }
    float tx = r.x + 10.0f;
    if (search) {
        nxsym_draw(c, NXSYM_SEARCH, r.x + 17.0f, r.y + r.h * 0.5f, 14.0f, t->label_tertiary, 0.0f);
        tx = r.x + 30.0f;
    }
    int empty = !text || !text[0];
    nxc_rect_t tr = nxc_rect(tx, r.y, r.x + r.w - tx - 10.0f, r.h);
    if (tr.w > 0.0f)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, tr, empty ? (placeholder ? placeholder : "") : text,
                          empty ? t->label_tertiary : t->label, NXC_ALIGN_LEFT);
    if ((state & NXGLASS_FOCUSED) && ((int)(nxui_time() * 2.0f) & 1) == 0) {
        float cx = tx + (empty ? 0.0f : nxui_text_width(NXUI_TEXT_BODY, text)) + 1.0f;
        if (cx > r.x + r.w - 8.0f) cx = r.x + r.w - 8.0f;
        nxc_fill_rrect(c, nxc_rect(cx, r.y + 7.0f, 2.0f, r.h - 14.0f), 1.0f, t->accent);
    }
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_progress(nxc_canvas_t *c, nxc_rect_t r, float value) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    float rad = r.h * 0.5f;
    nxc_fill_rrect(c, r, rad, t->fill_pressed);
    if (value < 0.0f) {
        /* Indeterminate: a comet sweeping left to right, 1.2 s period. */
        float tt = nxui_time() / 1.2f;
        float p = nxui_ease_in_out_cubic(tt - (float)(int32_t)tt);
        float seg = g_maxf(r.w * 0.32f, r.h * 2.0f);
        float x = r.x - seg + (r.w + seg) * p;
        nxc_save(c);
        nxc_clip_rrect(c, r, rad);
        nxc_paint_t pp = nxc_paint_linear(x, 0.0f, x + seg, 0.0f, NXC_WITH_ALPHA(t->accent, 0), t->accent);
        nxc_fill_rrect_paint(c, nxc_rect(x, r.y, seg, r.h), rad, &pp);
        nxc_restore(c);
    } else {
        if (!(value <= 1.0f)) value = 1.0f;
        float fw = r.w * value;
        if (fw > 0.0f) {
            if (fw < r.h) fw = r.h;
            fill_gloss(c, nxc_rect(r.x, r.y, fw, r.h), rad, t->accent, 0.18f);
        }
    }
    return NXC_OK;
}

nxc_status_t nxglass_badge(nxc_canvas_t *c, float x, float y, const char *label, nxc_color_t color) {
    if (!c || !label) return NXC_ERR_INVALID_ARG;
    float w = nxui_text_width(NXUI_TEXT_CAPTION, label) + 16.0f;
    if (!(w > 0.0f) || w > 4096.0f) return NXC_ERR_INVALID_ARG;
    nxc_rect_t r = nxc_rect(x, y, w, 18.0f);
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    nxc_fill_rrect(c, r, 9.0f, NXC_WITH_ALPHA(color, light ? 40 : 46));
    nxc_stroke_rrect(c, r, 9.0f, 1.0f, NXC_WITH_ALPHA(color, light ? 80 : 72));
    nxc_color_t tc = light ? nxc_color_lerp(NXC_WITH_ALPHA(color, 255), NXC_RGB(0, 0, 0), 0.38f)
                           : nxc_color_lerp(NXC_WITH_ALPHA(color, 255), NXC_RGB(0xFF, 0xFF, 0xFF), 0.25f);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, r, label, tc, NXC_ALIGN_CENTER);
    return NXC_OK;
}

/* ============ Checkbox, radio, stepper ============ */

nxc_status_t nxglass_checkbox(nxc_canvas_t *c, float x, float y, const char *label,
                              float checked_t, uint32_t state, nxc_rect_t *hit) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxglass_tokens_t *t = tok();
    if (!(checked_t >= 0.0f)) checked_t = 0.0f;
    if (checked_t > 1.0f) checked_t = 1.0f;
    nxc_rect_t b = nxc_rect(x, y, NXGLASS_CHECK_SIZE, NXGLASS_CHECK_SIZE);
    float rad = 4.5f;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    nxc_fill_rrect(c, b, rad, (state & NXGLASS_PRESSED) ? t->fill_pressed : t->fill_inset);
    nxc_stroke_rrect(c, b, rad, 1.0f, nxui_appearance() == NXUI_APPEARANCE_LIGHT ? NXC_ARGB(60, 0, 0, 0x10)
                                                                                 : NXC_ARGB(70, 0xFF, 0xFF, 0xFF));
    if (checked_t > 0.0f) {
        nxc_save(c);
        nxc_set_alpha(c, (uint8_t)(255.0f * checked_t + 0.5f));
        fill_gloss(c, b, rad, (state & NXGLASS_PRESSED) ? t->accent_pressed : t->accent, 0.16f);
        control_rim(c, b, rad, 70);
        /* Check mark grows from its short leg. */
        float s = 0.6f + 0.4f * checked_t;
        float cx = b.x + 8.0f, cy = b.y + 8.0f;
        nxc_point_t pts[8];
        uint8_t verbs[8];
        nxc_path_t p;
        nxc_path_init(&p, pts, 8, verbs, 8);
        nxc_path_move_to(&p, cx + (-4.0f) * s, cy + (0.3f) * s);
        nxc_path_line_to(&p, cx + (-1.2f) * s, cy + (3.0f) * s);
        nxc_path_line_to(&p, cx + (4.2f) * s, cy + (-3.4f) * s);
        nxc_stroke_t st = { 2.0f, NXC_CAP_ROUND, NXC_JOIN_ROUND, 4.0f };
        if (p.error == NXC_OK) nxc_stroke_path(c, &p, &st, t->on_accent);
        nxc_restore(c);
    }
    if (state & NXGLASS_FOCUSED) focus_ring(c, b, rad);
    float lw = 0.0f;
    if (label && label[0]) {
        lw = nxui_text_width(NXUI_TEXT_BODY, label) + 8.0f;
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(b.x + b.w + 8.0f, b.y - 2.0f, lw, b.h + 4.0f), label,
                          (state & NXGLASS_DISABLED) ? t->label_tertiary : t->label, NXC_ALIGN_LEFT);
    }
    nxc_restore(c);
    if (hit) *hit = nxc_rect(b.x - 2.0f, b.y - 4.0f, b.w + 4.0f + (lw > 0.0f ? lw + 8.0f : 0.0f), b.h + 8.0f);
    return NXC_OK;
}

nxc_status_t nxglass_radio(nxc_canvas_t *c, float x, float y, const char *label,
                           float selected_t, uint32_t state, nxc_rect_t *hit) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxglass_tokens_t *t = tok();
    if (!(selected_t >= 0.0f)) selected_t = 0.0f;
    if (selected_t > 1.0f) selected_t = 1.0f;
    nxc_rect_t b = nxc_rect(x, y, NXGLASS_CHECK_SIZE, NXGLASS_CHECK_SIZE);
    float cx = b.x + 8.0f, cy = b.y + 8.0f;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    nxc_fill_circle(c, cx, cy, 8.0f, (state & NXGLASS_PRESSED) ? t->fill_pressed : t->fill_inset);
    nxc_stroke_circle(c, cx, cy, 8.0f, 1.0f, nxui_appearance() == NXUI_APPEARANCE_LIGHT ? NXC_ARGB(60, 0, 0, 0x10)
                                                                                       : NXC_ARGB(70, 0xFF, 0xFF, 0xFF));
    if (selected_t > 0.0f) {
        nxc_save(c);
        nxc_set_alpha(c, (uint8_t)(255.0f * selected_t + 0.5f));
        fill_gloss(c, b, 8.0f, t->accent, 0.16f);
        control_rim(c, b, 8.0f, 70);
        nxc_fill_circle(c, cx, cy, 3.4f * selected_t, t->on_accent);
        nxc_restore(c);
    }
    if (state & NXGLASS_FOCUSED) nxc_stroke_circle(c, cx, cy, 11.0f, 3.0f, t->focus_ring);
    float lw = 0.0f;
    if (label && label[0]) {
        lw = nxui_text_width(NXUI_TEXT_BODY, label) + 8.0f;
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(b.x + b.w + 8.0f, b.y - 2.0f, lw, b.h + 4.0f), label,
                          (state & NXGLASS_DISABLED) ? t->label_tertiary : t->label, NXC_ALIGN_LEFT);
    }
    nxc_restore(c);
    if (hit) *hit = nxc_rect(b.x - 2.0f, b.y - 4.0f, b.w + 4.0f + (lw > 0.0f ? lw + 8.0f : 0.0f), b.h + 8.0f);
    return NXC_OK;
}

nxc_status_t nxglass_stepper(nxc_canvas_t *c, float x, float y, int hovered, int pressed,
                             uint32_t minus_state, uint32_t plus_state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    nxc_rect_t r = nxc_rect(x, y, NXGLASS_STEPPER_W, NXGLASS_STEPPER_H);
    float rad = 7.0f;
    if (light) {
        nxui_elevation(c, r, rad, 1);
        nxc_fill_rrect(c, r, rad, t->raised);
        nxc_stroke_rrect(c, r, rad, 1.0f, NXC_ARGB(22, 0, 0, 0x10));
    } else {
        nxc_fill_rrect(c, r, rad, t->fill);
        control_rim(c, r, rad, 60);
    }
    float hw = r.w * 0.5f;
    for (int i = 0; i < 2; i++) {
        nxc_rect_t h = nxc_rect(r.x + hw * (float)i, r.y, hw, r.h);
        uint32_t st = i == 0 ? minus_state : plus_state;
        int dis = (st & NXGLASS_DISABLED) != 0;
        if (!dis && (pressed == i || hovered == i)) {
            nxc_save(c);
            nxc_clip_rect(c, nxc_irect((int32_t)h.x, (int32_t)h.y, (int32_t)(h.w + 0.5f), (int32_t)h.h));
            nxc_fill_rrect(c, r, rad, pressed == i ? t->fill_pressed : t->fill_hover);
            nxc_restore(c);
        }
        nxc_color_t g = dis ? t->label_quaternary : t->label;
        float cx = h.x + hw * 0.5f, cy = h.y + h.h * 0.5f;
        nxc_draw_line(c, cx - 4.5f, cy, cx + 4.5f, cy, 1.6f, g);
        if (i == 1) nxc_draw_line(c, cx, cy - 4.5f, cx, cy + 4.5f, 1.6f, g);
    }
    nxc_fill_rect(c, nxc_rect(r.x + hw - 0.5f, r.y + 5.0f, 1.0f, r.h - 10.0f), t->separator);
    return NXC_OK;
}

int nxglass_stepper_part_at(float x, float y, int32_t px, int32_t py) {
    nxc_rect_t r = nxc_rect(x, y, NXGLASS_STEPPER_W, NXGLASS_STEPPER_H);
    if (!nxglass_hit(r, px, py)) return -1;
    return (float)px < x + NXGLASS_STEPPER_W * 0.5f ? 0 : 1;
}

nxc_status_t nxglass_section_header(nxc_canvas_t *c, float x, float y, float w, const char *title) {
    if (!c || !title) return NXC_ERR_INVALID_ARG;
    if (!(w > 0.0f) || w > 16384.0f) return NXC_OK;
    char up[64];
    int n = 0;
    while (title[n] && n < 63) {
        char ch = title[n];
        up[n] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
        n++;
    }
    up[n] = '\0';
    nxc_text_opts_t o;
    o.letter_spacing = 0.6f; o.align = NXC_ALIGN_LEFT; o.max_width = w; o.no_kerning = 0;
    return nxui_text(c, NXUI_TEXT_CAPTION, x, y + 14.0f, up, tok()->label_secondary, &o);
}

nxc_status_t nxglass_icon_button(nxc_canvas_t *c, nxc_rect_t r, int sym, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    float rad = 0.5f * g_minf(r.w, r.h);
    int sel = (state & NXGLASS_SELECTED) != 0;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    if (sel) {
        fill_gloss(c, r, rad, (state & NXGLASS_PRESSED) ? t->accent_pressed : t->accent, 0.16f);
        control_rim(c, r, rad, 80);
    } else {
        if (light) {
            nxui_elevation(c, r, rad, 1);
            nxc_fill_rrect(c, r, rad, (state & NXGLASS_PRESSED) ? NXC_RGB(0xE6, 0xE8, 0xEE)
                                     : (state & NXGLASS_HOVER) ? NXC_RGB(0xF7, 0xF8, 0xFA) : t->raised);
            nxc_stroke_rrect(c, r, rad, 1.0f, NXC_ARGB(22, 0, 0, 0x10));
        } else {
            nxc_color_t f = (state & NXGLASS_PRESSED) ? t->fill_pressed : (state & NXGLASS_HOVER) ? t->fill_hover : t->fill;
            nxc_fill_rrect(c, r, rad, f);
            control_rim(c, r, rad, 52);
        }
    }
    if (state & NXGLASS_FOCUSED) focus_ring(c, r, rad);
    if (sym >= 0 && sym < (int)NXSYM_COUNT)
        nxsym_draw(c, (nxsym_t)sym, r.x + r.w * 0.5f, r.y + r.h * 0.5f, g_minf(r.w, r.h) * 0.54f,
                   sel ? t->on_accent : t->label, 1.0f);
    nxc_restore(c);
    return NXC_OK;
}

/* ============ Scrollbar ============ */

nxc_rect_t nxglass_scrollbar_thumb(nxc_rect_t r, float content, float view, float offset) {
    nxc_rect_t none = nxc_rect(0.0f, 0.0f, 0.0f, 0.0f);
    if (!rect_ok(r) || !(content > view) || !(view > 0.0f)) return none;
    int vert = r.h >= r.w;
    float len = vert ? r.h : r.w;
    float tl = g_maxf(len * view / content, g_minf(28.0f, len));
    float maxo = content - view;
    float o = g_clampf(offset, 0.0f, maxo);
    float pos = (len - tl) * (o / maxo);
    return vert ? nxc_rect(r.x, r.y + pos, r.w, tl) : nxc_rect(r.x + pos, r.y, tl, r.h);
}

nxc_status_t nxglass_scrollbar(nxc_canvas_t *c, nxc_rect_t r, float content, float view,
                               float offset, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    nxc_rect_t th = nxglass_scrollbar_thumb(r, content, view, offset);
    if (!(th.w > 0.0f)) return NXC_OK;
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    int vert = r.h >= r.w;
    int active = (state & (NXGLASS_HOVER | NXGLASS_PRESSED)) != 0;
    if (active) {
        nxc_fill_rrect(c, r, 0.5f * g_minf(r.w, r.h), light ? NXC_ARGB(14, 0, 0, 0x10) : NXC_ARGB(16, 0xFF, 0xFF, 0xFF));
    }
    /* Thin when idle, full breadth when hovered or dragged. */
    float breadth = active ? (vert ? r.w : r.h) - 2.0f : 5.0f;
    nxc_rect_t k = vert ? nxc_rect(r.x + r.w - 1.0f - breadth, th.y + 1.0f, breadth, th.h - 2.0f)
                        : nxc_rect(th.x + 1.0f, r.y + r.h - 1.0f - breadth, th.w - 2.0f, breadth);
    nxc_color_t col = light ? NXC_ARGB(active ? 130 : 95, 0x10, 0x14, 0x1E)
                            : NXC_ARGB(active ? 150 : 105, 0xE6, 0xEC, 0xF8);
    return nxc_fill_rrect(c, k, 0.5f * breadth, col);
}

float nxglass_scrollbar_offset_at(nxc_rect_t r, float content, float view, int32_t px, int32_t py) {
    nxc_rect_t th = nxglass_scrollbar_thumb(r, content, view, 0.0f);
    if (!(th.w > 0.0f)) return 0.0f;
    int vert = r.h >= r.w;
    float len = vert ? r.h : r.w, tl = vert ? th.h : th.w;
    if (!(len > tl)) return 0.0f;
    float p = (vert ? (float)py - r.y : (float)px - r.x) - tl * 0.5f;
    return g_clampf(p / (len - tl), 0.0f, 1.0f) * (content - view);
}

/* ============ Module slider and tile (Control Center style) ============ */

nxc_status_t nxglass_slider_large(nxc_canvas_t *c, nxc_rect_t r, float value, int sym,
                                  float sym_value, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    float rad = r.h * 0.5f;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 105);
    nxc_fill_rrect(c, r, rad, light ? NXC_ARGB(26, 0, 0, 0x10) : t->fill_pressed);
    rim_bands(c, r, rad, light ? NXC_ARGB(30, 0, 0, 0x10) : NXC_ARGB(40, 0, 0, 0x08),
              light ? NXC_ARGB(120, 0xFF, 0xFF, 0xFF) : NXC_ARGB(22, 0xFF, 0xFF, 0xFF));
    /* Fill always covers the symbol well (one track height). */
    float fw = r.h + (r.w - r.h) * value;
    nxc_rect_t f = nxc_rect(r.x, r.y, fw, r.h);
    nxc_paint_t p = nxc_paint_linear(f.x, f.y, f.x, f.y + f.h, NXC_RGB(0xFF, 0xFF, 0xFF),
                                     light ? NXC_RGB(0xF4, 0xF5, 0xF8) : NXC_RGB(0xE6, 0xE9, 0xF0));
    nxc_fill_rrect_paint(c, f, rad, &p);
    /* Knob at the fill end: a slightly raised disc. */
    float kx = f.x + f.w - rad;
    nxc_draw_shadow_rrect(c, nxc_rect(kx - rad + 1.0f, r.y + 1.0f, 2.0f * rad - 2.0f, 2.0f * rad - 2.0f),
                          rad - 1.0f, 2.0f, 0.0f, 0.5f, 0.0f, NXC_ARGB((state & NXGLASS_PRESSED) ? 90 : 60, 0, 0, 0x08));
    nxc_fill_circle(c, kx, r.y + rad, rad - 1.0f, NXC_RGB(0xFF, 0xFF, 0xFF));
    if (sym >= 0 && sym < (int)NXSYM_COUNT)
        nxsym_draw(c, (nxsym_t)sym, r.x + rad + 1.0f, r.y + rad, r.h * 0.62f, NXC_RGB(0x5A, 0x62, 0x72), sym_value);
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_tile(nxc_canvas_t *c, nxc_rect_t r, int sym, const char *title,
                          const char *subtitle, nxc_color_t on_color, int chevron,
                          uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    int on = (state & NXGLASS_SELECTED) != 0;
    float rad = t->radius_card;
    nxc_save(c);
    if (state & NXGLASS_DISABLED) nxc_set_alpha(c, 120);
    nxc_color_t bg = (state & NXGLASS_PRESSED) ? t->fill_pressed : (state & NXGLASS_HOVER) ? t->fill_hover : t->fill;
    if (light) bg = (state & NXGLASS_PRESSED) ? NXC_ARGB(120, 0xFF, 0xFF, 0xFF)
                  : (state & NXGLASS_HOVER) ? NXC_ARGB(200, 0xFF, 0xFF, 0xFF) : NXC_ARGB(165, 0xFF, 0xFF, 0xFF);
    nxc_fill_rrect(c, r, rad, bg);
    control_rim(c, r, rad, light ? 230 : 46);

    float wr = g_minf(15.0f, r.h * 0.5f - 6.0f);
    float wx = r.x + 10.0f + wr, cy = r.y + r.h * 0.5f;
    if (on) {
        nxc_paint_t p = nxc_paint_linear(wx, cy - wr, wx, cy + wr,
                                         nxc_color_lerp(on_color, NXC_RGB(0xFF, 0xFF, 0xFF), 0.18f), on_color);
        nxc_fill_circle_paint(c, wx, cy, wr, &p);
        nxc_stroke_circle(c, wx, cy, wr, 1.0f, NXC_ARGB(60, 0xFF, 0xFF, 0xFF));
    } else {
        nxc_fill_circle(c, wx, cy, wr, light ? NXC_ARGB(22, 0, 0, 0x10) : NXC_ARGB(34, 0xFF, 0xFF, 0xFF));
    }
    if (sym >= 0 && sym < (int)NXSYM_COUNT)
        nxsym_draw(c, (nxsym_t)sym, wx, cy, wr * 1.1f, on ? t->on_accent : t->label_secondary, 1.0f);
    float tx = wx + wr + 10.0f, tw = r.x + r.w - tx - (chevron ? 22.0f : 8.0f);
    if (tw > 0.0f && title) {
        if (subtitle && subtitle[0]) {
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(tx, cy - 17.0f, tw, 18.0f), title, t->label, NXC_ALIGN_LEFT);
            nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(tx, cy + 1.0f, tw, 16.0f), subtitle, t->label_secondary, NXC_ALIGN_LEFT);
        } else {
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(tx, r.y, tw, r.h), title, t->label, NXC_ALIGN_LEFT);
        }
    }
    if (chevron) nxsym_draw(c, NXSYM_CHEVRON_RIGHT, r.x + r.w - 13.0f, cy, 11.0f, t->label_tertiary, 0.0f);
    nxc_restore(c);
    return NXC_OK;
}

nxc_status_t nxglass_hud(nxc_canvas_t *c, nxc_rect_t r, int sym, float sym_value,
                         const char *title, const char *subtitle, float level,
                         nxc_color_t sym_color, uint8_t opacity) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r) || opacity == 0) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    nxc_save(c);
    nxc_set_alpha(c, opacity);
    nxc_status_t st = nxglass_panel(c, r, t->radius_sheet, NXGLASS_ULTRATHIN, 2, 0);
    float cy = r.y + r.h * 0.5f;
    float lx = r.x + 18.0f;
    if (sym >= 0 && sym < (int)NXSYM_COUNT) {
        nxsym_draw(c, (nxsym_t)sym, r.x + 32.0f, cy, 24.0f, (sym_color >> 24) ? sym_color : t->label, sym_value);
        lx = r.x + 58.0f;
    }
    float tw = r.x + r.w - 18.0f - lx;
    int has_level = level >= 0.0f;
    int has_title = title && title[0];
    int has_sub = subtitle && subtitle[0];
    if (tw > 0.0f) {
        float ty = has_level ? r.y + 12.0f : (has_sub ? cy - 18.0f : cy - 10.0f);
        if (has_title)
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(lx, ty, tw, 20.0f), title, t->label, NXC_ALIGN_LEFT);
        if (has_sub && !has_level)
            nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(lx, ty + 20.0f, tw, 16.0f), subtitle, t->label_secondary, NXC_ALIGN_LEFT);
        if (has_level) {
            float lv = level > 1.0f ? 1.0f : level;
            float by = has_title ? r.y + r.h - 20.0f : cy - 3.0f;
            nxc_rect_t bar = nxc_rect(lx, by, tw, 6.0f);
            nxc_fill_rrect(c, bar, 3.0f, t->fill_pressed);
            if (lv > 0.0f) {
                float fw = g_maxf(bar.w * lv, bar.h);
                nxc_paint_t p = nxc_paint_linear(bar.x, 0.0f, bar.x + bar.w, 0.0f, NXC_WITH_ALPHA(t->label, 210), t->label);
                nxc_fill_rrect_paint(c, nxc_rect(bar.x, bar.y, fw, bar.h), 3.0f, &p);
            }
            if (has_sub)
                nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(lx, r.y + 12.0f, tw, 20.0f), subtitle, t->label_secondary, NXC_ALIGN_RIGHT);
        }
    }
    nxc_restore(c);
    return st;
}

/* ============ Popover, sheet, dialog, tooltip ============ */

/* Color of the glass at screen point (x, y): saturated blurred wallpaper
 * under the level's tint. Used for small shapes the backdrop pass cannot
 * fill (popover arrows); the blur is so wide that a solid color matches. */
static nxc_color_t glass_color_at(nxc_canvas_t *c, float x, float y, nxglass_level_t level) {
    const nxc_surface_t *wb = nxui_wallpaper_blurred();
    const glass_spec_t *s = glass_spec(level);
    nxc_color_t base = NXC_RGB(0x20, 0x24, 0x30);
    if (wb && wb->pixels) {
        int32_t px = (int32_t)(x + c->st.tx), py = (int32_t)(y + c->st.ty);
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if (px >= wb->width) px = wb->width - 1;
        if (py >= wb->height) py = wb->height - 1;
        if (px >= 0 && py >= 0) base = wb->pixels[(int64_t)py * wb->stride + px] | 0xFF000000u;
    }
    /* Saturation around Rec.601 luma, as the backdrop shader does. */
    int32_t r = (int32_t)((base >> 16) & 0xFF), g = (int32_t)((base >> 8) & 0xFF), b = (int32_t)(base & 0xFF);
    int32_t l = (r * 77 + g * 150 + b * 29) >> 8;
    float k = s->saturation;
    r = l + (int32_t)((float)(r - l) * k);
    g = l + (int32_t)((float)(g - l) * k);
    b = l + (int32_t)((float)(b - l) * k);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    nxc_color_t sat = NXC_RGB((uint32_t)r, (uint32_t)g, (uint32_t)b);
    return nxc_color_lerp(sat, NXC_WITH_ALPHA(s->tint, 255), (float)(s->tint >> 24) / 255.0f);
}

nxc_status_t nxglass_popover(nxc_canvas_t *c, nxc_rect_t r, nxglass_arrow_t arrow,
                             float arrow_pos, int live) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxglass_tokens_t *t = tok();
    float rad = t->radius_panel;
    nxc_status_t st = nxglass_panel(c, r, rad, NXGLASS_REGULAR, 2, live);
    if (st != NXC_OK || arrow == NXGLASS_ARROW_NONE) return st;

    const float S = NXGLASS_ARROW_SIZE, W = NXGLASS_ARROW_SIZE * 1.35f;
    int horiz = arrow == NXGLASS_ARROW_TOP || arrow == NXGLASS_ARROW_BOTTOM;
    float lo = (horiz ? r.x : r.y) + rad + W, hi = (horiz ? r.x + r.w : r.y + r.h) - rad - W;
    if (hi < lo) { float m = 0.5f * (lo + hi); lo = hi = m; }
    float a = g_clampf(arrow_pos, lo, hi);
    /* Tip, two base corners (base pushed 1.5 px inside to cover the rim). */
    nxc_point_t tip, b0, b1;
    switch (arrow) {
        case NXGLASS_ARROW_TOP:
            tip = (nxc_point_t){ a, r.y - S }; b0 = (nxc_point_t){ a - W, r.y + 1.5f }; b1 = (nxc_point_t){ a + W, r.y + 1.5f }; break;
        case NXGLASS_ARROW_BOTTOM:
            tip = (nxc_point_t){ a, r.y + r.h + S }; b0 = (nxc_point_t){ a - W, r.y + r.h - 1.5f }; b1 = (nxc_point_t){ a + W, r.y + r.h - 1.5f }; break;
        case NXGLASS_ARROW_LEFT:
            tip = (nxc_point_t){ r.x - S, a }; b0 = (nxc_point_t){ r.x + 1.5f, a - W }; b1 = (nxc_point_t){ r.x + 1.5f, a + W }; break;
        default:
            tip = (nxc_point_t){ r.x + r.w + S, a }; b0 = (nxc_point_t){ r.x + r.w - 1.5f, a - W }; b1 = (nxc_point_t){ r.x + r.w - 1.5f, a + W }; break;
    }
    /* Fill with the panel's own composited color just inside the base (it
     * includes sheen and tint exactly); estimate it if that is off-target. */
    float sx = 0.5f * (b0.x + b1.x), sy = 0.5f * (b0.y + b1.y);
    float ix = arrow == NXGLASS_ARROW_LEFT ? 4.0f : arrow == NXGLASS_ARROW_RIGHT ? -4.0f : 0.0f;
    float iy = arrow == NXGLASS_ARROW_TOP ? 4.0f : arrow == NXGLASS_ARROW_BOTTOM ? -4.0f : 0.0f;
    nxc_color_t fill = glass_color_at(c, sx, sy, NXGLASS_REGULAR);
    {
        const nxc_surface_t *tg = c->target;
        int32_t px = (int32_t)(sx + ix + c->st.tx), py = (int32_t)(sy + iy + c->st.ty);
        if (tg && tg->pixels && (tg->flags & NXC_SURFACE_OPAQUE) && px >= 2 && py >= 2 && px + 2 < tg->width && py + 2 < tg->height) {
            /* Average a 5x5 block to cancel the grain. */
            uint32_t ar = 0, ag = 0, ab = 0;
            for (int32_t dy = -2; dy <= 2; dy++)
                for (int32_t dx = -2; dx <= 2; dx++) {
                    uint32_t v = tg->pixels[(int64_t)(py + dy) * tg->stride + px + dx];
                    ar += (v >> 16) & 0xFFu; ag += (v >> 8) & 0xFFu; ab += v & 0xFFu;
                }
            fill = NXC_RGB(ar / 25u, ag / 25u, ab / 25u);
        }
    }
    /* Soft rounded tip: quadratic curves into the tip. */
    nxc_point_t pts[16];
    uint8_t verbs[16];
    nxc_path_t p;
    nxc_path_init(&p, pts, 16, verbs, 16);
    float tx0 = tip.x + (b0.x - tip.x) * 0.18f, ty0 = tip.y + (b0.y - tip.y) * 0.18f;
    float tx1 = tip.x + (b1.x - tip.x) * 0.18f, ty1 = tip.y + (b1.y - tip.y) * 0.18f;
    nxc_path_move_to(&p, b0.x, b0.y);
    nxc_path_line_to(&p, tx0, ty0);
    nxc_path_quad_to(&p, tip.x, tip.y, tx1, ty1);
    nxc_path_line_to(&p, b1.x, b1.y);
    nxc_path_close(&p);
    if (p.error != NXC_OK) return p.error;
    st = nxc_fill_path(c, &p, NXC_FILL_NONZERO, fill);
    /* Rim along the two outer edges, matching the panel's rim. */
    const glass_spec_t *s = glass_spec(NXGLASS_REGULAR);
    nxc_color_t rim = arrow == NXGLASS_ARROW_BOTTOM ? rim_bottom_color(s->rim_bottom)
                                                    : NXC_ARGB(s->rim_top, 0xFF, 0xFF, 0xFF);
    nxc_path_reset(&p);
    nxc_path_move_to(&p, b0.x, b0.y);
    nxc_path_line_to(&p, tx0, ty0);
    nxc_path_quad_to(&p, tip.x, tip.y, tx1, ty1);
    nxc_path_line_to(&p, b1.x, b1.y);
    nxc_stroke_t sk = { 1.0f, NXC_CAP_BUTT, NXC_JOIN_ROUND, 4.0f };
    if (st == NXC_OK && p.error == NXC_OK) st = nxc_stroke_path(c, &p, &sk, rim);
    return st;
}

nxc_status_t nxglass_sheet(nxc_canvas_t *c, nxc_rect_t r, int live) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    float rad = tok()->radius_sheet;
    /* Extend upward by the radius and clip it off: square top corners. */
    nxc_rect_t ext = nxc_rect(r.x, r.y - rad, r.w, r.h + rad);
    const float m = 120.0f;   /* room for the elevation-4 shadow */
    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)(r.x - m), (int32_t)r.y, (int32_t)(r.w + 2.0f * m), (int32_t)(r.h + m)));
    nxc_status_t st = glass_draw(c, ext, rad, NXGLASS_THICK, 4, live, nxglass_material(NXGLASS_THICK), 0);
    nxc_restore(c);
    return st;
}

/* Word-wrap `text` into lines no wider than max_w in `style`; draws each
 * line centered in [x, x+max_w] when c is non-NULL. Returns line count. */
static int wrap_text(nxc_canvas_t *c, nxui_text_style_t style, const char *text, float x, float y,
                     float max_w, float line_h, nxc_color_t color, int max_lines) {
    if (!text || !text[0] || !(max_w > 0.0f) || max_lines <= 0) return 0;
    char line[160];
    int lines = 0, i = 0;
    while (text[i] && lines < max_lines) {
        while (text[i] == ' ') i++;
        if (!text[i]) break;
        int len = 0, last_fit = 0, j = i;
        /* Grow word by word while the line fits. */
        while (text[j] && len < (int)sizeof(line) - 1) {
            int k = j;
            while (text[k] && text[k] != ' ' && len + (k - j) < (int)sizeof(line) - 1) k++;
            int nl = len;
            for (int q = j; q < k; q++) line[nl++] = text[q];
            line[nl] = '\0';
            if (last_fit > 0 && nxui_text_width(style, line) > max_w) break;
            len = nl;
            last_fit = len;
            j = k;
            if (text[j] == ' ' && len < (int)sizeof(line) - 1) { line[len++] = ' '; j++; }
            if (!text[j]) break;
        }
        while (len > 0 && line[len - 1] == ' ') len--;
        line[len] = '\0';
        if (len == 0) break;
        if (c) nxui_text_in_rect(c, style, nxc_rect(x, y + (float)lines * line_h, max_w, line_h), line, color, NXC_ALIGN_CENTER);
        lines++;
        i = j;
    }
    return lines;
}

#define DIALOG_W     320.0f
#define DIALOG_PAD   20.0f
#define DIALOG_BTN_H 28.0f
#define DIALOG_LINE  17.0f

static nxc_rect_t dialog_button_rect(nxc_rect_t r, int count, int i) {
    float gap = 8.0f;
    float bw = (r.w - 2.0f * DIALOG_PAD - (float)(count - 1) * gap) / (float)count;
    float x = r.x + r.w - DIALOG_PAD - (float)(i + 1) * bw - (float)i * gap;
    return nxc_rect(x, r.y + r.h - DIALOG_PAD - DIALOG_BTN_H, bw, DIALOG_BTN_H);
}

nxc_rect_t nxglass_dialog_rect(float center_x, float center_y, const char *message) {
    int lines = wrap_text(0, NXUI_TEXT_FOOTNOTE, message, 0.0f, 0.0f, DIALOG_W - 2.0f * DIALOG_PAD,
                          DIALOG_LINE, 0, 6);
    float h = DIALOG_PAD + 52.0f + 24.0f + (float)lines * DIALOG_LINE + 18.0f + DIALOG_BTN_H + DIALOG_PAD;
    return nxc_rect(center_x - DIALOG_W * 0.5f, center_y - h * 0.5f, DIALOG_W, h);
}

nxc_status_t nxglass_dialog(nxc_canvas_t *c, nxc_rect_t r, int icon, const char *title,
                            const char *message, const char *const *buttons, int count,
                            int default_button, int hovered, int pressed) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    if (count < 0 || count > NXGLASS_DIALOG_MAX_BUTTONS || (count > 0 && !buttons)) return NXC_ERR_INVALID_ARG;
    const nxglass_tokens_t *t = tok();
    nxc_status_t st = nxglass_panel(c, r, t->radius_sheet, NXGLASS_THICK, 4, 0);
    if (st != NXC_OK) return st;
    float y = r.y + DIALOG_PAD;
    if (icon >= 0 && icon < (int)NXSYM_COUNT) {
        nxc_color_t ic = icon == NXSYM_WARNING ? t->warning : t->accent;
        nxc_fill_circle(c, r.x + r.w * 0.5f, y + 22.0f, 24.0f, NXC_WITH_ALPHA(ic, 40));
        nxsym_draw(c, (nxsym_t)icon, r.x + r.w * 0.5f, y + 22.0f, 28.0f, ic, 1.0f);
        y += 52.0f;
    }
    if (title)
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(r.x + DIALOG_PAD, y, r.w - 2.0f * DIALOG_PAD, 22.0f), title,
                          t->label, NXC_ALIGN_CENTER);
    y += 24.0f;
    wrap_text(c, NXUI_TEXT_FOOTNOTE, message, r.x + DIALOG_PAD, y, r.w - 2.0f * DIALOG_PAD, DIALOG_LINE,
              t->label_secondary, 6);
    for (int i = 0; i < count; i++) {
        uint32_t s = 0;
        if (i == hovered) s |= NXGLASS_HOVER;
        if (i == pressed) s |= NXGLASS_PRESSED;
        nxglass_button(c, dialog_button_rect(r, count, i), buttons[i],
                       i == default_button ? NXGLASS_BUTTON_PRIMARY : NXGLASS_BUTTON_DEFAULT, s);
    }
    return NXC_OK;
}

int nxglass_dialog_button_at(nxc_rect_t r, const char *const *buttons, int count,
                             int32_t x, int32_t y) {
    if (!buttons || count <= 0 || count > NXGLASS_DIALOG_MAX_BUTTONS) return -1;
    for (int i = 0; i < count; i++)
        if (nxglass_hit(dialog_button_rect(r, count, i), x, y)) return i;
    return -1;
}

nxc_status_t nxglass_tooltip(nxc_canvas_t *c, float anchor_x, float anchor_y, const char *text) {
    if (!c || !text) return NXC_ERR_INVALID_ARG;
    if (!text[0]) return NXC_OK;
    float tw = nxui_text_width(NXUI_TEXT_FOOTNOTE, text);
    if (!(tw >= 0.0f)) return NXC_ERR_INVALID_ARG;
    float sw = (float)nxui_width(), sh = (float)nxui_height();
    float w = g_minf(tw + 18.0f, 360.0f), h = 24.0f;
    float x = (float)(int32_t)(anchor_x - w * 0.5f);
    float y = (float)(int32_t)(anchor_y + 10.0f);
    if (x + w > sw - 6.0f) x = sw - 6.0f - w;
    if (x < 6.0f) x = 6.0f;
    if (y + h > sh - 6.0f) y = anchor_y - 10.0f - h;
    nxc_rect_t r = nxc_rect(x, y, w, h);
    nxc_status_t st = nxglass_panel(c, r, 7.0f, NXGLASS_THIN, 1, 0);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, inset(r, 6.0f), text, tok()->label, NXC_ALIGN_CENTER);
    return st;
}

/* ============ Window chrome ============ */

nxc_status_t nxglass_titlebar(nxc_canvas_t *c, nxc_rect_t r, float radius, const char *title,
                              int focused, int hovered_controls) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r) || !(radius >= 0.0f)) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    const nxglass_tokens_t *t = tok();
    int light = nxui_appearance() == NXUI_APPEARANCE_LIGHT;
    nxc_rect_t ext = nxc_rect(r.x, r.y, r.w, r.h + radius);
    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)r.x, (int32_t)r.y, (int32_t)(r.w + 0.5f), (int32_t)(r.h + 0.5f)));
    nxc_color_t base = focused ? th->window_titlebar : th->window_titlebar_inactive;
    nxc_color_t top = nxc_color_lerp(base, NXC_RGB(0xFF, 0xFF, 0xFF), focused ? (light ? 0.5f : 0.05f) : 0.0f);
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h, top, base);
    nxc_fill_rrect_paint(c, ext, radius, &p);
    /* Light catching the top edge and the rounded corners. */
    nxc_stroke_rrect(c, ext, radius, 1.0f, light ? NXC_ARGB(200, 0xFF, 0xFF, 0xFF) : NXC_ARGB(40, 0xFF, 0xFF, 0xFF));
    nxc_restore(c);
    nxc_fill_rect(c, nxc_rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f), t->separator);

    if (r.w > 60.0f) {
        nxglass_winctl_draw(c, r, focused || hovered_controls, -1, -1);
        float m = NXGLASS_WINCTL_GROUP_W + NXGLASS_WINCTL_MARGIN + 8.0f;
        if (title && r.w > 2.0f * m + 40.0f)
            nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(r.x + m, r.y, r.w - 2.0f * m, r.h), title,
                              focused ? t->label : t->label_tertiary, NXC_ALIGN_CENTER);
    }
    return NXC_OK;
}

int nxglass_titlebar_button_at(nxc_rect_t r, int32_t x, int32_t y) {
    return nxglass_winctl_at(r, x, y);
}

/* ---- Window controls ---- */

/* Left-to-right slot of each control index (0 close, 1 minimize, 2 zoom). */
static const int k_winctl_slot[3] = { 0, 2, 1 };

nxc_rect_t nxglass_winctl_rect(nxc_rect_t tb, int idx) {
    if (idx < 0 || idx > 2 || !rect_ok(tb)) return nxc_rect(0.0f, 0.0f, 0.0f, 0.0f);
    const float s = NXGLASS_WINCTL_SIZE;
    float x0 = tb.x + tb.w - NXGLASS_WINCTL_MARGIN - NXGLASS_WINCTL_GROUP_W;
    float x = x0 + (float)k_winctl_slot[idx] * (s + NXGLASS_WINCTL_GAP);
    float y = tb.y + (tb.h - s) * 0.5f;
    return nxc_rect((float)(int)(x + 0.5f), (float)(int)(y + 0.5f), s, s);
}

int nxglass_winctl_at(nxc_rect_t tb, int32_t x, int32_t y) {
    if (!rect_ok(tb) || tb.w <= NXGLASS_WINCTL_GROUP_W + 2.0f * NXGLASS_WINCTL_MARGIN) return -1;
    for (int i = 0; i < 3; i++) {
        nxc_rect_t b = nxglass_winctl_rect(tb, i);
        /* 2 px of slack around each button (half the gap), full bar height */
        float hx = NXGLASS_WINCTL_GAP * 0.5f;
        if ((float)x >= b.x - hx && (float)x < b.x + b.w + hx &&
            (float)y >= tb.y && (float)y < tb.y + tb.h) return i;
    }
    return -1;
}

/* Glyphs are drawn on the 20.57 px design grid of the owner's SVGs
 * (close.svg, expand.svg, minimize.svg) and scaled to the button. */
static void winctl_glyph(nxc_canvas_t *c, int idx, nxc_rect_t b, nxc_color_t ink) {
    const float k = b.w / 20.57f;
#define GX(v) (b.x + (v) * k)
#define GY(v) (b.y + (v) * k)
    if (idx == 0) {
        /* Close: two quarter discs, top-left and bottom-right */
        float r = 7.6f * k;
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)GX(2.38f), (int32_t)GY(2.85f),
                                   (int32_t)(8.5f * k + 1.0f), (int32_t)(7.64f * k + 0.5f)));
        nxc_fill_circle(c, GX(10.87f), GY(10.49f), r, ink);
        nxc_restore(c);
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)GX(8.87f), (int32_t)GY(10.49f),
                                   (int32_t)(8.5f * k + 1.0f), (int32_t)(7.64f * k + 1.0f)));
        nxc_fill_circle(c, GX(8.87f), GY(10.49f), r, ink);
        nxc_restore(c);
    } else if (idx == 2) {
        /* Zoom: two rounded squares on the diagonal */
        float rr = 2.6f * k;
        nxc_fill_rrect(c, nxc_rect(GX(2.36f), GY(2.95f), 8.23f * k, 7.55f * k), rr, ink);
        nxc_fill_rrect(c, nxc_rect(GX(9.76f), GY(10.5f), 8.23f * k, 7.55f * k), rr, ink);
    } else {
        /* Minimize: a bar in the lower right with a rounded outer corner */
        float rr = 6.0f * k;
        nxc_rect_t bar = nxc_rect(GX(8.25f), GY(11.2f), 10.29f * k, 6.72f * k);
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)bar.x, (int32_t)bar.y, (int32_t)(bar.w + 0.5f), (int32_t)(bar.h + 0.5f)));
        nxc_fill_rrect(c, nxc_rect(bar.x - rr, bar.y - rr, bar.w + rr, bar.h + rr), rr, ink);
        nxc_restore(c);
    }
#undef GX
#undef GY
}

nxc_status_t nxglass_winctl_draw(nxc_canvas_t *c, nxc_rect_t tb, int lit, int pressed, int hover) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(tb) || tb.w <= NXGLASS_WINCTL_GROUP_W + 2.0f * NXGLASS_WINCTL_MARGIN) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    /* Owner palette: close #FF0016..#FF2138, zoom #A18EF7..#6956C5, minimize #4B7A9A */
    static const uint32_t top[3] = { 0xFFFF0016u, 0xFF4B7A9Au, 0xFFA18EF7u };
    static const uint32_t bot[3] = { 0xFFFF2138u, 0xFF4B7A9Au, 0xFF6956C5u };
    for (int i = 0; i < 3; i++) {
        nxc_rect_t b = nxglass_winctl_rect(tb, i);
        float rad = b.w * 0.38f;
        if (lit) {
            nxc_color_t c0 = (nxc_color_t)top[i], c1 = (nxc_color_t)bot[i];
            if (pressed == i) {
                c0 = nxc_color_lerp(c0, NXC_RGB(0, 0, 0), 0.22f);
                c1 = nxc_color_lerp(c1, NXC_RGB(0, 0, 0), 0.22f);
            } else if (hover == i) {
                c0 = nxc_color_lerp(c0, NXC_RGB(0xFF, 0xFF, 0xFF), 0.12f);
                c1 = nxc_color_lerp(c1, NXC_RGB(0xFF, 0xFF, 0xFF), 0.12f);
            }
            nxc_paint_t p = nxc_paint_linear(b.x, b.y, b.x, b.y + b.h, c0, c1);
            nxc_fill_rrect_paint(c, b, rad, &p);
            winctl_glyph(c, i, b, NXC_RGB(0xFF, 0xFF, 0xFF));
        } else {
            nxc_fill_rrect(c, b, rad, th->traffic_inactive);
            winctl_glyph(c, i, b, NXC_WITH_ALPHA(th->text_tertiary, 150));
        }
    }
    return NXC_OK;
}

/* ============ Preview gallery ============ */

static int g_gallery_on;

void nxglass_gallery_toggle(void) {
    g_gallery_on = !g_gallery_on;
    glass_log(g_gallery_on ? "[NXGLASS] gallery shown\n" : "[NXGLASS] gallery hidden\n");
}

int nxglass_gallery_visible(void) { return g_gallery_on; }

void nxglass_gallery_light(void) { nxui_set_appearance(NXUI_APPEARANCE_LIGHT); }
void nxglass_gallery_dark(void) { nxui_set_appearance(NXUI_APPEARANCE_DARK); }

static void gallery_label(nxc_canvas_t *c, float x, float y, const char *s) {
    nxui_text(c, NXUI_TEXT_CAPTION, x, y, s, tok()->label_secondary, 0);
}

static void gallery_levels(nxc_canvas_t *c, float x, float y) {
    static const char *const names[NXGLASS_LEVEL_COUNT] = { "Ultrathin", "Thin", "Regular", "Thick", "Chrome" };
    static const char *const uses[NXGLASS_LEVEL_COUNT] = {
        "HUDs over content", "Menus, popovers, dock", "Panels, sheets, Control Center",
        "Sidebars, dialogs, setup cards", "Menu bar, title bars" };
    const nxglass_tokens_t *t = tok();
    for (int i = 0; i < NXGLASS_LEVEL_COUNT; i++) {
        nxc_rect_t r = nxc_rect(x, y + (float)i * 70.0f, 290.0f, 58.0f);
        nxglass_panel(c, r, t->radius_panel, (nxglass_level_t)i, 2, 0);
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(r.x + 16.0f, r.y + 10.0f, 200.0f, 18.0f), names[i], t->label, NXC_ALIGN_LEFT);
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(r.x + 16.0f, r.y + 30.0f, 260.0f, 16.0f), uses[i], t->label_secondary, NXC_ALIGN_LEFT);
    }
    nxc_rect_t a = nxc_rect(x, y + 5.0f * 70.0f, 290.0f, 58.0f);
    nxglass_panel_tinted(c, a, t->radius_panel, NXGLASS_REGULAR, 2, 0, t->accent);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(a.x + 16.0f, a.y + 10.0f, 200.0f, 18.0f), "Accent-tinted", t->label, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(a.x + 16.0f, a.y + 30.0f, 260.0f, 16.0f), "nxglass_panel_tinted(..., accent)", t->label_secondary, NXC_ALIGN_LEFT);
    nxglass_hud(c, nxc_rect(x, y + 6.0f * 70.0f + 4.0f, 290.0f, 64.0f), NXSYM_VOLUME, 0.7f, "Volume", "70%", 0.7f, 0, 255);
}

static void gallery_controls(nxc_canvas_t *c, nxc_rect_t r) {
    const nxglass_tokens_t *t = tok();
    nxglass_panel(c, r, t->radius_panel, NXGLASS_REGULAR, 2, 0);
    float x = r.x + 20.0f, y = r.y + 16.0f, w = r.w - 40.0f;
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(x, y, w, 22.0f), "Controls", t->label, NXC_ALIGN_LEFT);
    y += 30.0f;

    nxglass_section_header(c, x, y, w, "Buttons");
    y += 22.0f;
    float bw = (w - 24.0f) / 4.0f;
    nxglass_button(c, nxc_rect(x, y, bw, NXGLASS_BUTTON_H), "Default", NXGLASS_BUTTON_DEFAULT, 0);
    nxglass_button(c, nxc_rect(x + (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Primary", NXGLASS_BUTTON_PRIMARY, 0);
    nxglass_button(c, nxc_rect(x + 2.0f * (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Delete", NXGLASS_BUTTON_DESTRUCTIVE, 0);
    nxglass_button(c, nxc_rect(x + 3.0f * (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Plain", NXGLASS_BUTTON_PLAIN, NXGLASS_HOVER);
    y += 38.0f;
    nxglass_button(c, nxc_rect(x, y, bw, NXGLASS_BUTTON_H), "Hover", NXGLASS_BUTTON_DEFAULT, NXGLASS_HOVER);
    nxglass_button(c, nxc_rect(x + (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Pressed", NXGLASS_BUTTON_PRIMARY, NXGLASS_PRESSED);
    nxglass_button(c, nxc_rect(x + 2.0f * (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Disabled", NXGLASS_BUTTON_DEFAULT, NXGLASS_DISABLED);
    nxglass_button(c, nxc_rect(x + 3.0f * (bw + 8.0f), y, bw, NXGLASS_BUTTON_H), "Focused", NXGLASS_BUTTON_DEFAULT, NXGLASS_FOCUSED);
    y += 44.0f;

    nxglass_section_header(c, x, y, w, "Switches and choices");
    y += 24.0f;
    nxglass_toggle(c, x, y, 0.0f, 0);
    nxglass_toggle(c, x + 50.0f, y, 1.0f, 0);
    nxglass_toggle(c, x + 100.0f, y, 0.55f, NXGLASS_PRESSED);
    nxglass_toggle(c, x + 150.0f, y, 1.0f, NXGLASS_DISABLED);
    nxglass_stepper(c, x + w - NXGLASS_STEPPER_W, y - 1.0f, 1, -1, NXGLASS_DISABLED, 0);
    {
        const char *const seg[3] = { "Light", "Dark", "Auto" };
        nxglass_segmented(c, nxc_rect(x + 200.0f, y - 3.0f, w - 200.0f - NXGLASS_STEPPER_W - 12.0f, 28.0f), seg, 3,
                          nxui_appearance() == NXUI_APPEARANCE_LIGHT ? 0 : 1, 2);
    }
    y += 36.0f;
    nxglass_checkbox(c, x, y, "Checked", 1.0f, 0, 0);
    nxglass_checkbox(c, x + 108.0f, y, "Off", 0.0f, 0, 0);
    nxglass_radio(c, x + 186.0f, y, "Selected", 1.0f, 0, 0);
    nxglass_radio(c, x + 290.0f, y, "Other", 0.0f, NXGLASS_HOVER, 0);
    y += 34.0f;

    nxglass_section_header(c, x, y, w, "Values");
    y += 22.0f;
    nxglass_slider(c, nxc_rect(x, y, w * 0.5f - 8.0f, 22.0f), 0.62f, 0);
    nxglass_slider(c, nxc_rect(x + w * 0.5f + 8.0f, y, w * 0.5f - 8.0f, 22.0f), 0.3f, NXGLASS_HOVER);
    y += 32.0f;
    nxglass_slider_large(c, nxc_rect(x, y, w, 28.0f), 0.7f, NXSYM_VOLUME, 0.7f, 0);
    y += 40.0f;
    nxglass_progress(c, nxc_rect(x, y, w * 0.5f - 8.0f, 6.0f), 0.45f);
    nxglass_progress(c, nxc_rect(x + w * 0.5f + 8.0f, y, w * 0.5f - 8.0f, 6.0f), -1.0f);
    y += 20.0f;

    nxglass_section_header(c, x, y, w, "Text and status");
    y += 22.0f;
    nxglass_field(c, nxc_rect(x, y, w * 0.5f - 8.0f, NXGLASS_FIELD_H), 0, "Name", 0, 0);
    nxglass_field(c, nxc_rect(x + w * 0.5f + 8.0f, y, w * 0.5f - 8.0f, NXGLASS_FIELD_H), "neolyx", 0, 0, NXGLASS_FOCUSED);
    y += 40.0f;
    nxglass_field(c, nxc_rect(x, y, w, NXGLASS_FIELD_H), 0, "Search settings", 1, 0);
    y += 42.0f;
    float bx = x;
    nxglass_badge(c, bx, y, "Connected", t->success); bx += 86.0f;
    nxglass_badge(c, bx, y, "Running", t->info); bx += 72.0f;
    nxglass_badge(c, bx, y, "Warning", t->warning); bx += 72.0f;
    nxglass_badge(c, bx, y, "Failed", t->destructive);
    float ix = x + w - 4.0f * (NXGLASS_ICON_BUTTON + 8.0f) + 8.0f;
    nxglass_icon_button(c, nxc_rect(ix, y - 5.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON), NXSYM_GEAR, 0);
    nxglass_icon_button(c, nxc_rect(ix + 36.0f, y - 5.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON), NXSYM_PLUS, NXGLASS_HOVER);
    nxglass_icon_button(c, nxc_rect(ix + 72.0f, y - 5.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON), NXSYM_WIFI, NXGLASS_SELECTED);
    nxglass_icon_button(c, nxc_rect(ix + 108.0f, y - 5.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON), NXSYM_TRASH, NXGLASS_DISABLED);
}

static void gallery_window(nxc_canvas_t *c, nxc_rect_t r) {
    const nxui_theme_t *th = nxui_theme();
    const nxglass_tokens_t *t = tok();
    const float tb = 28.0f, rad = th->radius_window;
    nxui_elevation(c, r, rad, 4);
    nxc_fill_rrect(c, r, rad, th->window_bg);
    nxc_save(c);
    nxc_clip_rrect(c, r, rad);
    nxc_rect_t side = nxc_rect(r.x, r.y + tb, 150.0f, r.h - tb);
    nxglass_sidebar(c, side);
    float y = side.y + 8.0f;
    nxglass_section_header(c, side.x + 12.0f, y, side.w - 20.0f, "System");
    y += 22.0f;
    nxglass_row(c, nxc_rect(side.x + 6.0f, y, side.w - 12.0f, 30.0f), NXSYM_WIFI, "Network", 0, NXGLASS_SELECTED);
    nxglass_row(c, nxc_rect(side.x + 6.0f, y + 32.0f, side.w - 12.0f, 30.0f), NXSYM_BLUETOOTH, "Bluetooth", 0, NXGLASS_HOVER);
    nxglass_row(c, nxc_rect(side.x + 6.0f, y + 64.0f, side.w - 12.0f, 30.0f), NXSYM_SUN, "Display", 0, 0);
    nxglass_row(c, nxc_rect(side.x + 6.0f, y + 96.0f, side.w - 12.0f, 30.0f), NXSYM_LOCK, "Privacy", 0, NXGLASS_DISABLED);

    float cx = side.x + side.w + 16.0f, cw = r.x + r.w - cx - 22.0f;
    nxui_text_in_rect(c, NXUI_TEXT_TITLE2, nxc_rect(cx, r.y + tb + 10.0f, cw, 26.0f), "Network", t->label, NXC_ALIGN_LEFT);
    nxc_rect_t card = nxc_rect(cx, r.y + tb + 44.0f, cw, 3.0f * NXGLASS_ROW_H + 8.0f);
    nxglass_card(c, card, t->radius_card);
    nxglass_row(c, nxc_rect(card.x + 4.0f, card.y + 4.0f, card.w - 8.0f, NXGLASS_ROW_H), NXSYM_WIFI, "Wi-Fi", "Vajra-5G", 0);
    nxglass_separator(c, card.x + 40.0f, card.y + 4.0f + NXGLASS_ROW_H, card.w - 52.0f);
    nxglass_row(c, nxc_rect(card.x + 4.0f, card.y + 4.0f + NXGLASS_ROW_H, card.w - 8.0f, NXGLASS_ROW_H), NXSYM_ETHERNET, "Ethernet", "Connected", NXGLASS_HOVER);
    nxglass_separator(c, card.x + 40.0f, card.y + 4.0f + 2.0f * NXGLASS_ROW_H, card.w - 52.0f);
    nxglass_row(c, nxc_rect(card.x + 4.0f, card.y + 4.0f + 2.0f * NXGLASS_ROW_H, card.w - 8.0f - 50.0f, NXGLASS_ROW_H), NXSYM_SHIELD, "Firewall", 0, 0);
    nxglass_toggle(c, card.x + card.w - 12.0f - NXGLASS_TOGGLE_W, card.y + 4.0f + 2.0f * NXGLASS_ROW_H + 7.0f, 1.0f, 0);
    nxglass_scrollbar(c, nxc_rect(r.x + r.w - 12.0f, r.y + tb + 4.0f, NXGLASS_SCROLLBAR_W, r.h - tb - 8.0f), 900.0f, 300.0f, 120.0f, 0);
    nxc_restore(c);
    nxglass_titlebar(c, nxc_rect(r.x, r.y, r.w, tb), rad, "System Settings", 1, 1);
    nxc_stroke_rrect(c, r, rad, 1.0f, th->window_border);
}

void nxglass_gallery_render(nxc_canvas_t *c) {
    if (!c || !g_gallery_on) return;
    const nxglass_tokens_t *t = tok();
    float sw = (float)nxui_width(), sh = (float)nxui_height();
    if (!(sw >= 1100.0f) || !(sh >= 700.0f)) return;    /* gallery is laid out for 1280x800+ */
    nxc_save(c);
    nxglass_scrim(c, nxc_rect(0.0f, 0.0f, sw, sh), 0.8f);
    float ox = (float)(int32_t)((sw - 1220.0f) * 0.5f), oy = 34.0f;
    /* Header strip on chrome glass (legible in both appearances). */
    nxc_rect_t hdr = nxc_rect(ox, oy, 1220.0f, 34.0f);
    nxglass_panel(c, hdr, 10.0f, NXGLASS_CHROME, 1, 0);
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(hdr.x + 14.0f, hdr.y, 120.0f, hdr.h), "NXGlass", t->label, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(hdr.x + 92.0f, hdr.y, 400.0f, hdr.h),
                      nxui_appearance() == NXUI_APPEARANCE_LIGHT ? "Light appearance  \xC2\xB7  levels, controls, chrome"
                                                                 : "Dark appearance  \xC2\xB7  levels, controls, chrome",
                      t->label_secondary, NXC_ALIGN_LEFT);
    oy += 46.0f;
    gallery_levels(c, ox, oy);
    gallery_controls(c, nxc_rect(ox + 312.0f, oy, 470.0f, 524.0f));

    nxc_rect_t win = nxc_rect(ox + 804.0f, oy, 416.0f, 236.0f);
    gallery_window(c, win);

    /* Popover with arrow pointing at an icon button, and a tooltip. */
    nxc_rect_t ib = nxc_rect(ox + 820.0f, oy + 256.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON);
    nxglass_icon_button(c, ib, NXSYM_BELL, NXGLASS_PRESSED);
    nxc_rect_t pop = nxc_rect(ox + 804.0f, ib.y + ib.h + 14.0f, 200.0f, 110.0f);
    nxglass_popover(c, pop, NXGLASS_ARROW_TOP, ib.x + ib.w * 0.5f, 0);
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(pop.x + 14.0f, pop.y + 10.0f, pop.w - 28.0f, 20.0f), "Popover", t->label, NXC_ALIGN_LEFT);
    nxglass_row(c, nxc_rect(pop.x + 6.0f, pop.y + 36.0f, pop.w - 12.0f, 30.0f), NXSYM_MOON, "Do Not Disturb", 0, NXGLASS_HOVER);
    nxglass_row(c, nxc_rect(pop.x + 6.0f, pop.y + 68.0f, pop.w - 12.0f, 30.0f), NXSYM_GEAR, "Settings\xE2\x80\xA6", 0, 0);
    nxc_rect_t ib2 = nxc_rect(ox + 1110.0f, oy + 256.0f, NXGLASS_ICON_BUTTON, NXGLASS_ICON_BUTTON);
    nxglass_icon_button(c, ib2, NXSYM_INFO, NXGLASS_HOVER);
    nxglass_tooltip(c, ib2.x + ib2.w * 0.5f, ib2.y + ib2.h, "Tooltip on thin glass");

    const char *const btns[2] = { "Restart", "Later" };
    const char *msg = "Updates are ready. Restart now to finish installing NeolyxOS 1.2.";
    nxc_rect_t dr = nxglass_dialog_rect(ox + 1060.0f, 0.0f, msg);
    dr.y = pop.y + pop.h + 20.0f;
    nxglass_dialog(c, dr, NXSYM_INFO, "Restart to update?", msg, btns, 2, 0, 1, -1);

    /* Panel cache counters: hits should climb, misses stay flat. */
    uint32_t hits = 0, misses = 0;
    nxui_panel_stats(&hits, &misses);
    char buf[64];
    int n = 0;
    const char *pre = "panel cache  hits ";
    while (*pre && n < 40) buf[n++] = *pre++;
    char num[12]; int k = 0; uint32_t v = hits;
    do { num[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 11);
    while (k > 0 && n < 50) buf[n++] = num[--k];
    pre = "  misses ";
    while (*pre && n < 58) buf[n++] = *pre++;
    v = misses;
    do { num[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 11);
    while (k > 0 && n < 63) buf[n++] = num[--k];
    buf[n] = '\0';
    gallery_label(c, hdr.x + hdr.w - 200.0f, hdr.y + 21.0f, buf);
    nxc_restore(c);
}
