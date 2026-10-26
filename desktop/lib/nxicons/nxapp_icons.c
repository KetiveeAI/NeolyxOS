/*
 * NeolyxOS - NXIcons: application icons
 *
 * Each icon is a continuous-curvature tile (squircle) with a vertical
 * two-tone gradient, a soft inner top highlight and a darker bottom
 * edge, carrying an original glyph drawn in the same 24-unit frame as
 * the system symbols. Icons are rendered once per integer size into
 * cached surfaces and composited from the cache; fractional sizes scale
 * the next larger cached size down with smooth filtering.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../../include/nxicons.h"
#include "nxicons_internal.h"

/* ============ Cache ============ */

#define APP_CACHE_MAX   64
#define APP_MIN_SIZE    8
#define APP_MAX_SIZE    512

typedef struct {
    nxc_surface_t surf;
    int32_t       id;       /* -1 = empty */
    int32_t       size;
    uint32_t      last_use;
} app_cache_entry_t;

static struct {
    int                 ready;
    uint32_t            budget;      /* bytes allowed for cached surfaces */
    uint32_t            used;
    uint32_t            tick;
    app_cache_entry_t   entries[APP_CACHE_MAX];
} g_app;

/* ============ Palette ============ */

typedef struct {
    nxc_color_t top, bottom;   /* tile gradient */
    nxc_color_t glyph;         /* main glyph color */
    nxc_color_t glyph2;        /* secondary glyph color */
} app_style_t;

#define C(r, g, b) NXC_RGB(r, g, b)

static const app_style_t g_styles[NXAPP_COUNT] = {
    [NXAPP_FILES]      = { C(0x5A, 0xC8, 0xFF), C(0x1E, 0x7F, 0xE6), C(0xFF, 0xFF, 0xFF), C(0xDA, 0xF0, 0xFF) },
    [NXAPP_BROWSER]    = { C(0x3E, 0x9C, 0xFF), C(0x19, 0x4C, 0xC9), C(0xFF, 0xFF, 0xFF), C(0xFF, 0x5A, 0x4E) },
    [NXAPP_TERMINAL]   = { C(0x30, 0x35, 0x44), C(0x11, 0x14, 0x1E), C(0xE8, 0xEC, 0xF5), C(0x5A, 0xE0, 0x8A) },
    [NXAPP_SETTINGS]   = { C(0x9A, 0xA1, 0xAE), C(0x5C, 0x63, 0x70), C(0xF5, 0xF6, 0xF8), C(0x2B, 0x2F, 0x38) },
    [NXAPP_CALCULATOR] = { C(0xFF, 0xA6, 0x3B), C(0xE8, 0x66, 0x0A), C(0xFF, 0xFF, 0xFF), C(0x3B, 0x2A, 0x1A) },
    [NXAPP_CALENDAR]   = { C(0xFF, 0xFF, 0xFF), C(0xEE, 0xEF, 0xF3), C(0xF0, 0x44, 0x38), C(0x2B, 0x2F, 0x38) },
    [NXAPP_CLOCK]      = { C(0x2A, 0x2E, 0x39), C(0x0D, 0x10, 0x18), C(0xFF, 0xFF, 0xFF), C(0xFF, 0x52, 0x3B) },
    [NXAPP_NOTES]      = { C(0xFF, 0xFF, 0xFF), C(0xF5, 0xF1, 0xE4), C(0xFF, 0xD2, 0x3B), C(0xB3, 0xA9, 0x8C) },
    [NXAPP_MUSIC]      = { C(0xFF, 0x6B, 0x8F), C(0xE0, 0x1F, 0x4C), C(0xFF, 0xFF, 0xFF), C(0xFF, 0xD0, 0xDC) },
    [NXAPP_PHOTOS]     = { C(0xFF, 0xFF, 0xFF), C(0xF0, 0xF1, 0xF5), C(0xFF, 0xB8, 0x2E), C(0x3B, 0x9C, 0xFF) },
    [NXAPP_MAIL]       = { C(0x58, 0xB8, 0xFF), C(0x1B, 0x68, 0xE0), C(0xFF, 0xFF, 0xFF), C(0xD8, 0xEC, 0xFF) },
    [NXAPP_MESSAGES]   = { C(0x5F, 0xE3, 0x7A), C(0x1E, 0xB4, 0x4F), C(0xFF, 0xFF, 0xFF), C(0xFF, 0xFF, 0xFF) },
    [NXAPP_FONTS]      = { C(0xB0, 0x7A, 0xFF), C(0x6D, 0x28, 0xD9), C(0xFF, 0xFF, 0xFF), C(0xEB, 0xDC, 0xFF) },
    [NXAPP_MONITOR]    = { C(0x2A, 0x33, 0x44), C(0x0F, 0x14, 0x20), C(0x3C, 0xE2, 0xB4), C(0x3E, 0x9C, 0xFF) },
    [NXAPP_STORE]      = { C(0x4C, 0xA8, 0xFF), C(0x19, 0x5B, 0xD6), C(0xFF, 0xFF, 0xFF), C(0xFF, 0xFF, 0xFF) },
    [NXAPP_TRASH]      = { C(0xB6, 0xBC, 0xC7), C(0x7C, 0x83, 0x90), C(0xFF, 0xFF, 0xFF), C(0xFF, 0xFF, 0xFF) },
    [NXAPP_LAUNCHER]   = { C(0x3A, 0x40, 0x4F), C(0x1C, 0x20, 0x2A), C(0xFF, 0xFF, 0xFF), C(0xFF, 0xFF, 0xFF) },
    [NXAPP_GENERIC]    = { C(0x8E, 0x95, 0xA3), C(0x55, 0x5C, 0x6A), C(0xFF, 0xFF, 0xFF), C(0xD8, 0xDC, 0xE4) },
};

static const char *const g_app_names[NXAPP_COUNT] = {
    "files", "browser", "terminal", "settings", "calculator", "calendar", "clock", "notes",
    "music", "photos", "mail", "messages", "fonts", "monitor", "store", "trash", "launcher",
    "generic",
};

/* ============ Glyph frame ============ */

typedef struct {
    nxc_canvas_t *c;
    float cx, cy, k;          /* 24-unit frame over the tile */
    nxc_status_t st;
    nxc_point_t pbuf[64];
    uint8_t vbuf[64];
    nxc_path_t path;
} app_ctx_t;

static void note(app_ctx_t *a, nxc_status_t st) { if (a->st == NXC_OK && st != NXC_OK) a->st = st; }
static inline float X(const app_ctx_t *a, float u) { return a->cx + u * a->k; }
static inline float Y(const app_ctx_t *a, float v) { return a->cy + v * a->k; }
static void pbegin(app_ctx_t *a) { nxc_path_init(&a->path, a->pbuf, 64, a->vbuf, 64); }
static void pm(app_ctx_t *a, float x, float y) { nxc_path_move_to(&a->path, X(a, x), Y(a, y)); }
static void pl(app_ctx_t *a, float x, float y) { nxc_path_line_to(&a->path, X(a, x), Y(a, y)); }
static void pc(app_ctx_t *a, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    nxc_path_cubic_to(&a->path, X(a, c1x), Y(a, c1y), X(a, c2x), Y(a, c2y), X(a, x), Y(a, y));
}
static void pz(app_ctx_t *a) { nxc_path_close(&a->path); }
static void pfill(app_ctx_t *a, nxc_color_t col) { note(a, nxc_fill_path(a->c, &a->path, NXC_FILL_NONZERO, col)); }
static void pstroke(app_ctx_t *a, float w, nxc_color_t col) {
    nxc_stroke_t s;
    s.width = w * a->k; if (s.width < 1.0f) s.width = 1.0f;
    s.cap = NXC_CAP_ROUND; s.join = NXC_JOIN_ROUND; s.miter_limit = 4.0f;
    note(a, nxc_stroke_path(a->c, &a->path, &s, col));
}
static void rr(app_ctx_t *a, float x, float y, float w, float h, float r, nxc_color_t col) {
    note(a, nxc_fill_rrect(a->c, nxc_rect(X(a, x), Y(a, y), w * a->k, h * a->k), r * a->k, col));
}
static void disc(app_ctx_t *a, float x, float y, float r, nxc_color_t col) {
    note(a, nxc_fill_circle(a->c, X(a, x), Y(a, y), r * a->k, col));
}
static void ringg(app_ctx_t *a, float x, float y, float r, float w, nxc_color_t col) {
    float sw = w * a->k; if (sw < 1.0f) sw = 1.0f;
    note(a, nxc_stroke_circle(a->c, X(a, x), Y(a, y), r * a->k, sw, col));
}
static void ln(app_ctx_t *a, float x0, float y0, float x1, float y1, float w, nxc_color_t col) {
    float sw = w * a->k; if (sw < 1.0f) sw = 1.0f;
    note(a, nxc_draw_line(a->c, X(a, x0), Y(a, y0), X(a, x1), Y(a, y1), sw, col));
}
static void sym(app_ctx_t *a, nxsym_t s, float x, float y, float units, nxc_color_t col, float value) {
    note(a, nxsym_draw(a->c, s, X(a, x), Y(a, y), units * a->k, col, value));
}

/* ============ Glyphs (24-unit frame centered on the tile) ============ */

static void glyph_files(app_ctx_t *a, const app_style_t *st) {
    /* Folder: back panel, lighter front flap. */
    pbegin(a);
    pm(a, -9.5f, 7.0f); pl(a, -9.5f, -5.5f);
    pc(a, -9.5f, -6.6f, -8.6f, -7.5f, -7.5f, -7.5f);
    pl(a, -2.5f, -7.5f); pl(a, 0.0f, -5.0f); pl(a, 7.5f, -5.0f);
    pc(a, 8.6f, -5.0f, 9.5f, -4.1f, 9.5f, -3.0f);
    pl(a, 9.5f, 7.0f);
    pc(a, 9.5f, 8.1f, 8.6f, 9.0f, 7.5f, 9.0f);
    pl(a, -7.5f, 9.0f);
    pc(a, -8.6f, 9.0f, -9.5f, 8.1f, -9.5f, 7.0f);
    pz(a);
    pfill(a, NXC_WITH_ALPHA(st->glyph2, 230));
    rr(a, -9.5f, -1.5f, 19.0f, 10.5f, 2.0f, st->glyph);
}

static void glyph_browser(app_ctx_t *a, const app_style_t *st) {
    ringg(a, 0.0f, 0.0f, 9.0f, 1.8f, st->glyph);
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * NXC_PI / 4.0f;
        float c = nxc_cosf_pub(ang), s = nxc_sinf_pub(ang);
        ln(a, c * 7.2f, s * 7.2f, c * 6.0f, s * 6.0f, 1.0f, NXC_WITH_ALPHA(st->glyph, 170));
    }
    /* Needle: red north, white south. */
    pbegin(a);
    pm(a, 4.5f, -4.5f); pl(a, 1.4f, 1.4f); pl(a, -1.4f, -1.4f); pz(a);
    pfill(a, st->glyph2);
    pbegin(a);
    pm(a, -4.5f, 4.5f); pl(a, -1.4f, -1.4f); pl(a, 1.4f, 1.4f); pz(a);
    pfill(a, st->glyph);
}

static void glyph_terminal(app_ctx_t *a, const app_style_t *st) {
    pbegin(a);
    pm(a, -7.5f, -4.5f); pl(a, -2.5f, 0.0f); pl(a, -7.5f, 4.5f);
    pstroke(a, 2.2f, st->glyph2);
    ln(a, 0.0f, 5.0f, 7.5f, 5.0f, 2.2f, st->glyph);
}

static void glyph_settings(app_ctx_t *a, const app_style_t *st) {
    sym(a, NXSYM_GEAR, 0.0f, 0.0f, 20.0f, st->glyph, 0.0f);
    disc(a, 0.0f, 0.0f, 2.2f, NXC_WITH_ALPHA(st->glyph2, 140));
}

static void glyph_calculator(app_ctx_t *a, const app_style_t *st) {
    /* Display + 3x3 keys with an accent column. */
    rr(a, -7.5f, -8.5f, 15.0f, 4.0f, 1.0f, NXC_WITH_ALPHA(st->glyph2, 190));
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            nxc_color_t col = (c == 2) ? NXC_WITH_ALPHA(st->glyph2, 190) : st->glyph;
            rr(a, -7.5f + 5.5f * (float)c, -2.5f + 4.0f * (float)r, 4.0f, 3.0f, 0.9f, col);
        }
}

static void glyph_calendar(app_ctx_t *a, const app_style_t *st) {
    /* Red header band with two rings, a grid of day dots below. */
    rr(a, -12.0f, -12.0f, 24.0f, 7.5f, 0.0f, st->glyph);
    disc(a, -5.0f, -8.25f, 1.0f, NXC_WITH_ALPHA(st->glyph2, 60));
    disc(a, 5.0f, -8.25f, 1.0f, NXC_WITH_ALPHA(st->glyph2, 60));
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            int today = (r == 1 && c == 2);
            float x = -7.5f + 5.0f * (float)c, y = -1.0f + 4.2f * (float)r;
            if (today) disc(a, x, y, 1.9f, st->glyph);
            else rr(a, x - 1.2f, y - 1.2f, 2.4f, 2.4f, 0.8f, NXC_WITH_ALPHA(st->glyph2, 110));
        }
}

static void glyph_clock(app_ctx_t *a, const app_style_t *st) {
    disc(a, 0.0f, 0.0f, 9.6f, st->glyph);
    for (int i = 0; i < 12; i++) {
        float ang = (float)i * NXC_PI / 6.0f;
        float c = nxc_cosf_pub(ang), s = nxc_sinf_pub(ang);
        float len = (i % 3 == 0) ? 1.8f : 1.0f;
        ln(a, c * 8.6f, s * 8.6f, c * (8.6f - len), s * (8.6f - len), 0.8f, 0xFF2B2F38u);
    }
    ln(a, 0.0f, 0.0f, 0.0f, -5.2f, 1.4f, 0xFF2B2F38u);
    ln(a, 0.0f, 0.0f, 4.0f, 2.4f, 1.4f, 0xFF2B2F38u);
    ln(a, 0.0f, 1.5f, -2.6f, -5.6f, 0.6f, st->glyph2);
    disc(a, 0.0f, 0.0f, 0.9f, st->glyph2);
}

static void glyph_notes(app_ctx_t *a, const app_style_t *st) {
    rr(a, -12.0f, -12.0f, 24.0f, 6.0f, 0.0f, st->glyph);
    for (int i = 0; i < 3; i++) {
        ln(a, -7.5f, -1.5f + 4.0f * (float)i, (i == 2) ? 2.5f : 7.5f, -1.5f + 4.0f * (float)i, 1.3f,
           NXC_WITH_ALPHA(st->glyph2, 200));
    }
}

static void glyph_music(app_ctx_t *a, const app_style_t *st) {
    /* Beamed pair of eighth notes. */
    disc(a, -4.5f, 6.0f, 3.0f, st->glyph);
    disc(a, 5.0f, 4.0f, 3.0f, st->glyph);
    pbegin(a);
    pm(a, -2.0f, 6.0f); pl(a, -2.0f, -7.0f); pl(a, 7.5f, -9.5f); pl(a, 7.5f, 4.0f);
    pstroke(a, 1.8f, st->glyph);
    pbegin(a);
    pm(a, -2.0f, -7.0f); pl(a, 7.5f, -9.5f); pl(a, 7.5f, -6.3f); pl(a, -2.0f, -3.8f); pz(a);
    pfill(a, st->glyph);
}

static void glyph_photos(app_ctx_t *a, const app_style_t *st) {
    (void)st;
    static const nxc_color_t petals[8] = {
        C(0xFF, 0x5A, 0x4E), C(0xFF, 0x9F, 0x2E), C(0xFF, 0xD6, 0x3B), C(0x5F, 0xD8, 0x6A),
        C(0x2E, 0xC8, 0xC8), C(0x3B, 0x9C, 0xFF), C(0x8B, 0x5C, 0xF6), C(0xF0, 0x5A, 0xB8),
    };
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * NXC_PI / 4.0f - NXC_PI / 2.0f;
        float cx = nxc_cosf_pub(ang) * 4.3f, cy = nxc_sinf_pub(ang) * 4.3f;
        /* Petal: ellipse elongated along the radius, built from an arc path. */
        pbegin(a);
        float ex = 5.4f, ey = 3.1f;
        float c = nxc_cosf_pub(ang), s = nxc_sinf_pub(ang);
        /* rotate unit ellipse points by ang */
        const float kk = 0.5522847498f;
        float px[4] = { ex, 0.0f, -ex, 0.0f }, py[4] = { 0.0f, ey, 0.0f, -ey };
        float hx[4] = { ex, -ex * kk, -ex, ex * kk }, hy[4] = { ey * kk, ey, -ey * kk, -ey };
        /* control handles: for point i, outgoing handle offsets */
        float ox[4] = { 0.0f, -ex * kk, 0.0f, ex * kk }, oy[4] = { ey * kk, 0.0f, -ey * kk, 0.0f };
        (void)hx; (void)hy;
        for (int q = 0; q < 4; q++) {
            int n = (q + 1) % 4;
            float p0x = px[q], p0y = py[q], p3x = px[n], p3y = py[n];
            float c1x = p0x + ox[q], c1y = p0y + oy[q];
            float c2x = p3x - ox[n], c2y = p3y - oy[n];
            float rp0x = c * p0x - s * p0y + cx, rp0y = s * p0x + c * p0y + cy;
            float rc1x = c * c1x - s * c1y + cx, rc1y = s * c1x + c * c1y + cy;
            float rc2x = c * c2x - s * c2y + cx, rc2y = s * c2x + c * c2y + cy;
            float rp3x = c * p3x - s * p3y + cx, rp3y = s * p3x + c * p3y + cy;
            if (q == 0) pm(a, rp0x, rp0y);
            pc(a, rc1x, rc1y, rc2x, rc2y, rp3x, rp3y);
        }
        pz(a);
        pfill(a, NXC_WITH_ALPHA(petals[i], 215));
    }
}

static void glyph_mail(app_ctx_t *a, const app_style_t *st) {
    rr(a, -9.5f, -6.5f, 19.0f, 13.0f, 2.2f, st->glyph);
    pbegin(a);
    pm(a, -9.0f, -5.0f); pl(a, 0.0f, 1.8f); pl(a, 9.0f, -5.0f);
    pstroke(a, 1.6f, NXC_WITH_ALPHA(st->bottom, 255));
}

static void glyph_messages(app_ctx_t *a, const app_style_t *st) {
    pbegin(a);
    pm(a, -9.5f, -2.0f);
    pc(a, -9.5f, -7.0f, -5.5f, -9.0f, 0.0f, -9.0f);
    pc(a, 5.5f, -9.0f, 9.5f, -7.0f, 9.5f, -2.0f);
    pc(a, 9.5f, 3.0f, 5.5f, 5.0f, 0.0f, 5.0f);
    pc(a, -2.0f, 5.0f, -3.5f, 4.7f, -5.0f, 4.2f);
    pl(a, -8.5f, 7.5f); pl(a, -7.0f, 3.0f);
    pc(a, -8.6f, 1.8f, -9.5f, 0.0f, -9.5f, -2.0f);
    pz(a);
    pfill(a, st->glyph);
}

static void glyph_fonts(app_ctx_t *a, const app_style_t *st) {
    /* A bold "A" cut from strokes, with a serif-less crossbar. */
    pbegin(a);
    pm(a, -8.0f, 9.0f); pl(a, 0.0f, -9.5f); pl(a, 8.0f, 9.0f);
    pstroke(a, 3.2f, st->glyph);
    ln(a, -4.0f, 3.0f, 4.0f, 3.0f, 2.6f, st->glyph);
    disc(a, 0.0f, -9.5f, 1.6f, st->glyph);
}

static void glyph_monitor(app_ctx_t *a, const app_style_t *st) {
    const float hs[5] = { 5.0f, 9.0f, 7.0f, 13.0f, 10.5f };
    for (int i = 0; i < 5; i++) {
        float x = -10.0f + 4.2f * (float)i;
        nxc_color_t col = nxc_color_lerp(st->glyph, st->glyph2, (float)i / 4.0f);
        rr(a, x, 9.0f - hs[i], 3.0f, hs[i], 1.0f, col);
    }
}

static void glyph_store(app_ctx_t *a, const app_style_t *st) {
    rr(a, -8.0f, -3.0f, 16.0f, 12.5f, 2.2f, st->glyph);
    pbegin(a);
    pm(a, -4.5f, -3.0f); pl(a, -4.5f, -5.5f);
    pc(a, -4.5f, -8.5f, -2.5f, -10.0f, 0.0f, -10.0f);
    pc(a, 2.5f, -10.0f, 4.5f, -8.5f, 4.5f, -5.5f);
    pl(a, 4.5f, -3.0f);
    pstroke(a, 1.8f, st->glyph);
    disc(a, -4.5f, -1.0f, 1.0f, NXC_WITH_ALPHA(st->bottom, 255));
    disc(a, 4.5f, -1.0f, 1.0f, NXC_WITH_ALPHA(st->bottom, 255));
}

static void glyph_trash(app_ctx_t *a, const app_style_t *st) {
    sym(a, NXSYM_TRASH, 0.0f, 0.5f, 19.0f, st->glyph, 0.0f);
}

static void glyph_launcher(app_ctx_t *a, const app_style_t *st) {
    (void)st;
    static const nxc_color_t cols[9] = {
        C(0xFF, 0x5A, 0x4E), C(0xFF, 0x9F, 0x2E), C(0xFF, 0xD6, 0x3B),
        C(0x5F, 0xD8, 0x6A), C(0x2E, 0xC8, 0xC8), C(0x3B, 0x9C, 0xFF),
        C(0x8B, 0x5C, 0xF6), C(0xF0, 0x5A, 0xB8), C(0xE8, 0x66, 0x0A),
    };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            rr(a, -9.0f + 6.5f * (float)c, -9.0f + 6.5f * (float)r, 5.0f, 5.0f, 1.6f, cols[r * 3 + c]);
        }
}

static void glyph_generic(app_ctx_t *a, const app_style_t *st) {
    rr(a, -9.0f, -7.0f, 18.0f, 14.0f, 2.0f, st->glyph);
    rr(a, -9.0f, -7.0f, 18.0f, 3.6f, 2.0f, NXC_WITH_ALPHA(st->bottom, 255));
    rr(a, -9.0f, -5.0f, 18.0f, 1.6f, 0.0f, NXC_WITH_ALPHA(st->bottom, 255));
    disc(a, -6.8f, -5.2f, 0.8f, st->glyph2);
    disc(a, -4.4f, -5.2f, 0.8f, st->glyph2);
}

/* ============ Tile ============ */

static nxc_status_t render_icon(nxc_canvas_t *c, nxapp_icon_t id, float x, float y, float size) {
    const app_style_t *st = &g_styles[id];
    nxc_rect_t r = nxc_rect(x, y, size, size);
    float radius = size * 0.225f;
    nxc_point_t pbuf[64];
    uint8_t vbuf[64];
    nxc_path_t tile;
    nxc_path_init(&tile, pbuf, 64, vbuf, 64);
    nxc_status_t s = nxc_path_add_squircle(&tile, r, radius);
    if (s != NXC_OK) return s;

    /* Tile body: vertical gradient. */
    nxc_paint_t grad = nxc_paint_linear(x, y, x, y + size, st->top, st->bottom);
    s = nxc_fill_path_paint(c, &tile, NXC_FILL_NONZERO, &grad);
    if (s != NXC_OK) return s;

    /* Glyph, clipped to the tile (the calendar/notes bands bleed to edges). */
    nxc_save(c);
    nxc_clip_rrect(c, r, radius * 1.1f);
    app_ctx_t a;
    a.c = c; a.cx = x + size * 0.5f; a.cy = y + size * 0.5f; a.k = size / 24.0f; a.st = NXC_OK;
    switch (id) {
        case NXAPP_FILES:      glyph_files(&a, st); break;
        case NXAPP_BROWSER:    glyph_browser(&a, st); break;
        case NXAPP_TERMINAL:   glyph_terminal(&a, st); break;
        case NXAPP_SETTINGS:   glyph_settings(&a, st); break;
        case NXAPP_CALCULATOR: glyph_calculator(&a, st); break;
        case NXAPP_CALENDAR:   glyph_calendar(&a, st); break;
        case NXAPP_CLOCK:      glyph_clock(&a, st); break;
        case NXAPP_NOTES:      glyph_notes(&a, st); break;
        case NXAPP_MUSIC:      glyph_music(&a, st); break;
        case NXAPP_PHOTOS:     glyph_photos(&a, st); break;
        case NXAPP_MAIL:       glyph_mail(&a, st); break;
        case NXAPP_MESSAGES:   glyph_messages(&a, st); break;
        case NXAPP_FONTS:      glyph_fonts(&a, st); break;
        case NXAPP_MONITOR:    glyph_monitor(&a, st); break;
        case NXAPP_STORE:      glyph_store(&a, st); break;
        case NXAPP_TRASH:      glyph_trash(&a, st); break;
        case NXAPP_LAUNCHER:   glyph_launcher(&a, st); break;
        default:               glyph_generic(&a, st); break;
    }
    nxc_restore(c);
    if (a.st != NXC_OK) return a.st;

    /* Lighting: soft highlight across the top third, darker bottom rim,
     * and a hairline edge so the tile reads on any background. */
    nxc_paint_t hi = nxc_paint_linear(x, y, x, y + size * 0.55f, NXC_ARGB(70, 255, 255, 255),
                                      NXC_ARGB(0, 255, 255, 255));
    nxc_save(c);
    nxc_clip_rrect(c, r, radius * 1.1f);
    s = nxc_fill_rect_paint(c, nxc_rect(x, y, size, size * 0.55f), &hi);
    nxc_paint_t lo = nxc_paint_linear(x, y + size * 0.7f, x, y + size, NXC_ARGB(0, 0, 0, 0),
                                      NXC_ARGB(60, 0, 0, 0));
    if (s == NXC_OK) s = nxc_fill_rect_paint(c, nxc_rect(x, y + size * 0.7f, size, size * 0.3f), &lo);
    nxc_restore(c);
    if (s != NXC_OK) return s;
    nxc_stroke_t edge;
    edge.width = size >= 48.0f ? 1.0f : 0.75f;
    edge.cap = NXC_CAP_BUTT; edge.join = NXC_JOIN_ROUND; edge.miter_limit = 4.0f;
    return nxc_stroke_path(c, &tile, &edge, NXC_ARGB(46, 0, 0, 0));
}

/* Uncached draw with a global alpha. */
static nxc_status_t render_icon_alpha(nxc_canvas_t *c, nxapp_icon_t id, float x, float y, float size,
                                      uint8_t alpha) {
    nxc_status_t s = nxc_save(c);
    if (s != NXC_OK) return s;
    nxc_set_alpha(c, alpha);
    s = render_icon(c, id, x, y, size);
    nxc_restore(c);
    return s;
}

/* ============ Cache ============ */

nxc_status_t nxicons_init(uint32_t cache_bytes) {
    for (int i = 0; i < APP_CACHE_MAX; i++) {
        g_app.entries[i].id = -1;
        g_app.entries[i].size = 0;
        g_app.entries[i].surf.pixels = 0;
    }
    g_app.budget = cache_bytes ? cache_bytes : (4u << 20);
    g_app.used = 0;
    g_app.tick = 0;
    g_app.ready = 1;
    return NXC_OK;
}

static app_cache_entry_t *cache_lookup(int32_t id, int32_t size) {
    for (int i = 0; i < APP_CACHE_MAX; i++) {
        app_cache_entry_t *e = &g_app.entries[i];
        if (e->id == id && e->size == size && e->surf.pixels) {
            e->last_use = ++g_app.tick;
            return e;
        }
    }
    return 0;
}

/* Render an icon into a new cache entry, evicting the least recently
 * used entries when over budget. Returns NULL when caching is impossible. */
static app_cache_entry_t *cache_fill(int32_t id, int32_t size) {
    uint64_t bytes = (uint64_t)size * (uint64_t)size * 4u;
    if (bytes > g_app.budget) return 0;
    /* Pick a slot: an empty one, else the LRU. */
    app_cache_entry_t *slot = 0;
    for (int i = 0; i < APP_CACHE_MAX; i++) {
        app_cache_entry_t *e = &g_app.entries[i];
        if (!e->surf.pixels) { slot = e; break; }
        if (!slot || e->last_use < slot->last_use) slot = e;
    }
    if (!slot) return 0;
    /* Free the slot and anything else needed to fit the budget. The engine
     * allocator may be a bump allocator, in which case destroy() is a
     * no-op and `used` just bounds the number of distinct renders. */
    if (slot->surf.pixels) {
        g_app.used -= (uint32_t)((uint64_t)slot->size * slot->size * 4u);
        nxc_surface_destroy(&slot->surf);
        slot->id = -1;
    }
    while (g_app.used + bytes > g_app.budget) {
        app_cache_entry_t *lru = 0;
        for (int i = 0; i < APP_CACHE_MAX; i++) {
            app_cache_entry_t *e = &g_app.entries[i];
            if (e->surf.pixels && (!lru || e->last_use < lru->last_use)) lru = e;
        }
        if (!lru) break;
        g_app.used -= (uint32_t)((uint64_t)lru->size * lru->size * 4u);
        nxc_surface_destroy(&lru->surf);
        lru->id = -1;
    }
    if (nxc_surface_create(&slot->surf, size, size, 0) != NXC_OK) return 0;
    nxc_canvas_t cc;
    if (nxc_canvas_init(&cc, &slot->surf) != NXC_OK ||
        render_icon(&cc, (nxapp_icon_t)id, 0.0f, 0.0f, (float)size) != NXC_OK) {
        nxc_surface_destroy(&slot->surf);
        slot->surf.pixels = 0;
        return 0;
    }
    slot->id = id;
    slot->size = size;
    slot->last_use = ++g_app.tick;
    g_app.used += (uint32_t)bytes;
    return slot;
}

nxc_status_t nxapp_icon_draw(nxc_canvas_t *c, nxapp_icon_t id, float x, float y, float size,
                             uint8_t alpha) {
    if (!c || (int)id < 0 || id >= NXAPP_COUNT || !nxi__finite(x) || !nxi__finite(y) ||
        !nxi__finite(size) || size < (float)APP_MIN_SIZE || size > (float)APP_MAX_SIZE) {
        return NXC_ERR_INVALID_ARG;
    }
    if (alpha == 0) return NXC_OK;
    if (!g_app.ready) return render_icon_alpha(c, id, x, y, size, alpha);
    /* Cache key: the integer size at or above the request (scale down only). */
    int32_t key = (int32_t)size;
    if ((float)key < size) key++;
    /* Dock magnification produces many fractional sizes; snap the cached
     * size to a multiple of 4 above 40 px to keep the cache small. */
    if (key > 40) key = (key + 3) & ~3;
    app_cache_entry_t *e = cache_lookup(id, key);
    if (!e) e = cache_fill(id, key);
    if (!e) return render_icon_alpha(c, id, x, y, size, alpha);
    nxc_filter_t f = ((float)key == size) ? NXC_FILTER_NEAREST : NXC_FILTER_SMOOTH;
    return nxc_draw_surface(c, &e->surf, nxc_irect(0, 0, key, key), nxc_rect(x, y, size, size), alpha, f);
}

nxapp_icon_t nxapp_icon_from_name(const char *app_name) {
    if (!app_name) return NXAPP_GENERIC;
    /* Case-insensitive match on the leading word (e.g. "Files", "System Settings"). */
    static const struct { const char *key; nxapp_icon_t id; } map[] = {
        { "files", NXAPP_FILES }, { "file manager", NXAPP_FILES }, { "finder", NXAPP_FILES },
        { "browser", NXAPP_BROWSER }, { "zepra", NXAPP_BROWSER }, { "web", NXAPP_BROWSER },
        { "terminal", NXAPP_TERMINAL }, { "console", NXAPP_TERMINAL },
        { "settings", NXAPP_SETTINGS }, { "system settings", NXAPP_SETTINGS }, { "preferences", NXAPP_SETTINGS },
        { "calculator", NXAPP_CALCULATOR }, { "calendar", NXAPP_CALENDAR }, { "clock", NXAPP_CLOCK },
        { "notes", NXAPP_NOTES }, { "music", NXAPP_MUSIC }, { "photos", NXAPP_PHOTOS },
        { "mail", NXAPP_MAIL }, { "messages", NXAPP_MESSAGES }, { "chat", NXAPP_MESSAGES },
        { "fonts", NXAPP_FONTS }, { "font manager", NXAPP_FONTS }, { "fontmanager", NXAPP_FONTS },
        { "monitor", NXAPP_MONITOR }, { "activity", NXAPP_MONITOR }, { "store", NXAPP_STORE },
        { "trash", NXAPP_TRASH }, { "launcher", NXAPP_LAUNCHER }, { "apps", NXAPP_LAUNCHER },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        const char *k = map[i].key, *n = app_name;
        int match = 1;
        while (*k) {
            char a = *n, b = *k;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) { match = 0; break; }
            n++; k++;
        }
        if (match && (*n == 0 || *n == ' ' || *n == '.')) return map[i].id;
    }
    return NXAPP_GENERIC;
}

const char *nxapp_icon_name(nxapp_icon_t id) {
    if ((int)id < 0 || id >= NXAPP_COUNT) return "invalid";
    return g_app_names[id];
}
