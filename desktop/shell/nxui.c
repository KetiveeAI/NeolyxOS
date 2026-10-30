/*
 * NeolyxOS - NXUI: desktop shell visual layer
 *
 * Owns the frame canvas and its double-buffered backing surface, the
 * system fonts (embedded by nxui_assets.S), the Vajra dark/light theme,
 * the generated wallpaper and its pre-blurred copy for frosted glass,
 * the userspace cursor, and small motion helpers. See nxui.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/nxui.h"
#include "../include/nxsyscall.h"
#include "../include/nxfont_registry.h"

/* ============ Embedded font data (nxui_assets.S) ============ */

extern const uint8_t nxui_font_regular[], nxui_font_regular_end[];
extern const uint8_t nxui_font_medium[],  nxui_font_medium_end[];
extern const uint8_t nxui_font_bold[],    nxui_font_bold_end[];
extern const uint8_t nxui_font_display[], nxui_font_display_end[];
extern const uint8_t nxui_font_mono[],    nxui_font_mono_end[];
extern const uint8_t nxui_font_italic[],  nxui_font_italic_end[];

/* ============ State ============ */

typedef struct {
    int             ready;
    fb_info_t       fb;
    nxc_surface_t   back;           /* render target, presented by flip */
    nxc_canvas_t    canvas;
    nxc_surface_t   wallpaper;
    nxc_surface_t   wallpaper_blur;
    nxc_surface_t   cursor;         /* pre-rendered arrow with shadow */
    nxc_font_t     *fonts[NXUI_FONT_COUNT];
    uint32_t        font_reg_ids[NXUI_FONT_COUNT];
    nxc_font_metrics_t metrics[NXUI_TEXT_COUNT];
    nxui_appearance_t appearance;
    nxui_theme_t    theme;
    uint64_t        t0_ms;
    uint64_t        last_frame_ms;
    float           dt;
    uint32_t        frame;
} nxui_state_t;

static nxui_state_t g_ui;

/* Cursor bitmap geometry: the arrow tip sits at this offset inside it. */
#define CURSOR_SIZE   32
#define CURSOR_HOT_X  4.0f
#define CURSOR_HOT_Y  3.0f

/* ============ Small helpers ============ */

static void ui_log(const char *msg) { nx_debug_print(msg); }

static void *ui_alloc(uint64_t size) {
    /* nx_malloc returns 16-byte aligned blocks from the pre-mapped heap. */
    return nx_alloc(size);
}

static void ui_log_status(const char *what, nxc_status_t st) {
    ui_log("[NXUI] ");
    ui_log(what);
    ui_log(": ");
    ui_log(nxc_status_str(st));
    ui_log("\n");
}

/* Deterministic 32-bit hash for procedural art. */
static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float hash_unit(uint32_t x) { return (float)(hash32(x) & 0xFFFFFFu) / 16777215.0f; }

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* ============ Theme ============ */

/* Vajra palette (docs/architecture/desktop_theme.html) */
#define VAJRA_SAFFRON   NXC_RGB(0xE8, 0x66, 0x0A)
#define VAJRA_AMBER     NXC_RGB(0xD9, 0x77, 0x06)
#define VAJRA_VIOLET    NXC_RGB(0x6D, 0x28, 0xD9)
#define VAJRA_SKY       NXC_RGB(0x0E, 0xA5, 0xE9)
#define VAJRA_NAVY      NXC_RGB(0x07, 0x0B, 0x14)
#define VAJRA_SURFACE   NXC_RGB(0x0D, 0x14, 0x24)
#define VAJRA_ELEVATED  NXC_RGB(0x14, 0x20, 0x38)
#define VAJRA_TEXT      NXC_RGB(0xF0, 0xF4, 0xFF)
#define VAJRA_MUTED     NXC_RGB(0x8C, 0xA0, 0xC0)

static nxc_material_t material(float saturation, nxc_color_t tint, uint8_t noise,
                               nxc_color_t border) {
    nxc_material_t m;
    m.blur_sigma = 24.0f;
    m.saturation = saturation;
    m.tint = tint;
    m.noise = noise;
    m.border = border;
    return m;
}

static void theme_build(nxui_theme_t *t, nxui_appearance_t ap) {
    t->appearance = ap;
    t->radius_window = 12.0f;
    t->radius_popover = 14.0f;
    t->radius_dock = 22.0f;
    t->radius_widget = 20.0f;
    t->radius_control = 8.0f;
    t->menubar_height = 28.0f;

    t->accent = VAJRA_SAFFRON;
    t->accent_pressed = NXC_RGB(0xC9, 0x55, 0x05);
    t->destructive = NXC_RGB(0xF0, 0x44, 0x38);
    t->success = NXC_RGB(0x2F, 0xC2, 0x6B);
    t->warning = VAJRA_AMBER;
    t->text_on_accent = NXC_RGB(0xFF, 0xFF, 0xFF);
    t->traffic_close = NXC_RGB(0xFF, 0x5F, 0x57);
    t->traffic_minimize = NXC_RGB(0xFE, 0xBC, 0x2E);
    t->traffic_zoom = NXC_RGB(0x28, 0xC8, 0x40);

    if (ap == NXUI_APPEARANCE_DARK) {
        t->text_primary   = NXC_WITH_ALPHA(VAJRA_TEXT, 240);
        t->text_secondary = NXC_WITH_ALPHA(VAJRA_MUTED, 235);
        t->text_tertiary  = NXC_WITH_ALPHA(VAJRA_MUTED, 140);
        t->separator      = NXC_ARGB(36, 0xFF, 0xFF, 0xFF);
        t->highlight      = NXC_ARGB(46, 0xFF, 0xFF, 0xFF);
        t->control_fill         = NXC_ARGB(26, 0xFF, 0xFF, 0xFF);
        t->control_fill_hover   = NXC_ARGB(40, 0xFF, 0xFF, 0xFF);
        t->control_fill_pressed = NXC_ARGB(56, 0xFF, 0xFF, 0xFF);
        t->control_fill_on      = VAJRA_SAFFRON;
        t->selection            = NXC_WITH_ALPHA(VAJRA_SAFFRON, 200);

        t->mat_menubar = material(1.5f, NXC_WITH_ALPHA(VAJRA_NAVY, 150), 6, NXC_ARGB(0, 0, 0, 0));
        t->mat_dock    = material(1.6f, NXC_WITH_ALPHA(VAJRA_SURFACE, 140), 6, NXC_ARGB(40, 0xFF, 0xFF, 0xFF));
        t->mat_popover = material(1.6f, NXC_WITH_ALPHA(VAJRA_SURFACE, 175), 6, NXC_ARGB(36, 0xFF, 0xFF, 0xFF));
        t->mat_hud     = material(1.5f, NXC_WITH_ALPHA(VAJRA_SURFACE, 165), 6, NXC_ARGB(32, 0xFF, 0xFF, 0xFF));
        t->mat_widget  = material(1.5f, NXC_WITH_ALPHA(VAJRA_ELEVATED, 120), 6, NXC_ARGB(34, 0xFF, 0xFF, 0xFF));
        t->mat_sidebar = material(1.4f, NXC_WITH_ALPHA(VAJRA_SURFACE, 205), 4, NXC_ARGB(0, 0, 0, 0));

        t->window_bg               = NXC_RGB(0x16, 0x1B, 0x27);
        t->window_titlebar         = NXC_RGB(0x1D, 0x23, 0x31);
        t->window_titlebar_inactive = NXC_RGB(0x19, 0x1E, 0x2A);
        t->window_border           = NXC_ARGB(52, 0xFF, 0xFF, 0xFF);
        t->traffic_inactive        = NXC_ARGB(60, 0xFF, 0xFF, 0xFF);
        t->shadow                  = NXC_RGB(0x00, 0x02, 0x08);
    } else {
        t->text_primary   = NXC_ARGB(230, 0x10, 0x14, 0x1E);
        t->text_secondary = NXC_ARGB(150, 0x10, 0x14, 0x1E);
        t->text_tertiary  = NXC_ARGB(95, 0x10, 0x14, 0x1E);
        t->separator      = NXC_ARGB(30, 0x00, 0x00, 0x00);
        t->highlight      = NXC_ARGB(150, 0xFF, 0xFF, 0xFF);
        t->control_fill         = NXC_ARGB(18, 0x00, 0x00, 0x00);
        t->control_fill_hover   = NXC_ARGB(30, 0x00, 0x00, 0x00);
        t->control_fill_pressed = NXC_ARGB(44, 0x00, 0x00, 0x00);
        t->control_fill_on      = VAJRA_SAFFRON;
        t->selection            = NXC_WITH_ALPHA(VAJRA_SAFFRON, 210);

        t->mat_menubar = material(1.6f, NXC_ARGB(150, 0xF6, 0xF7, 0xFA), 5, NXC_ARGB(0, 0, 0, 0));
        t->mat_dock    = material(1.7f, NXC_ARGB(120, 0xF6, 0xF7, 0xFA), 5, NXC_ARGB(120, 0xFF, 0xFF, 0xFF));
        t->mat_popover = material(1.7f, NXC_ARGB(170, 0xF6, 0xF7, 0xFA), 5, NXC_ARGB(140, 0xFF, 0xFF, 0xFF));
        t->mat_hud     = material(1.6f, NXC_ARGB(160, 0xF6, 0xF7, 0xFA), 5, NXC_ARGB(120, 0xFF, 0xFF, 0xFF));
        t->mat_widget  = material(1.6f, NXC_ARGB(110, 0xFF, 0xFF, 0xFF), 5, NXC_ARGB(140, 0xFF, 0xFF, 0xFF));
        t->mat_sidebar = material(1.4f, NXC_ARGB(200, 0xEE, 0xF0, 0xF4), 4, NXC_ARGB(0, 0, 0, 0));

        t->window_bg               = NXC_RGB(0xFB, 0xFB, 0xFD);
        t->window_titlebar         = NXC_RGB(0xF1, 0xF2, 0xF5);
        t->window_titlebar_inactive = NXC_RGB(0xF6, 0xF6, 0xF8);
        t->window_border           = NXC_ARGB(40, 0x00, 0x00, 0x00);
        t->traffic_inactive        = NXC_ARGB(50, 0x00, 0x00, 0x00);
        t->shadow                  = NXC_RGB(0x0A, 0x10, 0x20);
    }
}

const nxui_theme_t *nxui_theme(void) { return &g_ui.theme; }
nxui_appearance_t nxui_appearance(void) { return g_ui.appearance; }

void nxui_set_appearance(nxui_appearance_t appearance) {
    if (appearance != NXUI_APPEARANCE_DARK && appearance != NXUI_APPEARANCE_LIGHT) return;
    g_ui.appearance = appearance;
    theme_build(&g_ui.theme, appearance);
    ui_log(appearance == NXUI_APPEARANCE_DARK ? "[NXUI] appearance: dark\n"
                                              : "[NXUI] appearance: light\n");
}

/* ============ Typography ============ */

static const nxui_type_t g_type_scale[NXUI_TEXT_COUNT] = {
    [NXUI_TEXT_CAPTION]       = { NXUI_FONT_REGULAR,  11.0f,  0.10f },
    [NXUI_TEXT_FOOTNOTE]      = { NXUI_FONT_REGULAR,  12.0f,  0.05f },
    [NXUI_TEXT_BODY]          = { NXUI_FONT_REGULAR,  13.0f,  0.00f },
    [NXUI_TEXT_BODY_MEDIUM]   = { NXUI_FONT_MEDIUM,   13.0f,  0.00f },
    [NXUI_TEXT_MENU]          = { NXUI_FONT_MEDIUM,   13.5f,  0.00f },
    [NXUI_TEXT_MENU_BOLD]     = { NXUI_FONT_BOLD,     13.5f,  0.00f },
    [NXUI_TEXT_HEADLINE]      = { NXUI_FONT_BOLD,     13.0f,  0.00f },
    [NXUI_TEXT_TITLE3]        = { NXUI_FONT_MEDIUM,   15.0f, -0.10f },
    [NXUI_TEXT_TITLE2]        = { NXUI_FONT_BOLD,     17.0f, -0.20f },
    [NXUI_TEXT_TITLE1]        = { NXUI_FONT_BOLD,     22.0f, -0.30f },
    [NXUI_TEXT_LARGE_TITLE]   = { NXUI_FONT_BOLD,     28.0f, -0.45f },
    [NXUI_TEXT_CLOCK]         = { NXUI_FONT_DISPLAY,  56.0f, -1.20f },
    [NXUI_TEXT_MONO]          = { NXUI_FONT_MONO,     12.5f,  0.00f },
    [NXUI_TEXT_SUBHEADLINE]   = { NXUI_FONT_SEMIBOLD, 12.0f,  0.05f },
    [NXUI_TEXT_CALLOUT]       = { NXUI_FONT_REGULAR,  15.0f, -0.05f },
    [NXUI_TEXT_BODY_SEMIBOLD] = { NXUI_FONT_SEMIBOLD, 13.0f,  0.00f },
    [NXUI_TEXT_BODY_ITALIC]   = { NXUI_FONT_ITALIC,   13.0f,  0.00f },
    [NXUI_TEXT_DISPLAY]       = { NXUI_FONT_DISPLAY,  40.0f, -0.80f },
    [NXUI_TEXT_MONO_SMALL]    = { NXUI_FONT_MONO,     11.0f,  0.00f },
};

const nxc_font_t *nxui_font(nxui_font_id_t id) {
    if (id < 0 || id >= NXUI_FONT_COUNT) id = NXUI_FONT_REGULAR;
    if (g_ui.fonts[id]) return g_ui.fonts[id];
    return g_ui.fonts[NXUI_FONT_REGULAR];   /* fallback for missing faces */
}

uint32_t nxui_font_registry_id(nxui_font_id_t id) {
    if (id < 0 || id >= NXUI_FONT_COUNT) return 0;
    return g_ui.font_reg_ids[id];
}

nxui_type_t nxui_type(nxui_text_style_t style) {
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    return g_type_scale[style];
}

nxc_status_t nxui_text(nxc_canvas_t *c, nxui_text_style_t style, float x, float baseline_y,
                       const char *utf8, nxc_color_t color, const nxc_text_opts_t *opts) {
    if (!c || !utf8) return NXC_ERR_INVALID_ARG;
    nxui_type_t t = nxui_type(style);
    const nxc_font_t *f = nxui_font(t.font);
    if (!f) return NXC_ERR_STATE;
    nxc_text_opts_t o;
    if (opts) {
        o = *opts;
    } else {
        o.letter_spacing = 0.0f; o.align = NXC_ALIGN_LEFT; o.max_width = 0.0f; o.no_kerning = 0;
    }
    o.letter_spacing += t.tracking;
    return nxc_draw_text(c, f, t.size, x, baseline_y, utf8, -1, color, &o);
}

float nxui_text_width(nxui_text_style_t style, const char *utf8) {
    if (!utf8) return 0.0f;
    nxui_type_t t = nxui_type(style);
    const nxc_font_t *f = nxui_font(t.font);
    if (!f) return 0.0f;
    nxc_text_opts_t o;
    o.letter_spacing = t.tracking; o.align = NXC_ALIGN_LEFT; o.max_width = 0.0f; o.no_kerning = 0;
    return nxc_text_width(f, t.size, utf8, -1, &o);
}

float nxui_text_ascent(nxui_text_style_t style) {
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    return g_ui.metrics[style].ascent;
}

float nxui_text_descent(nxui_text_style_t style) {
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    return g_ui.metrics[style].descent;
}

float nxui_text_line_height(nxui_text_style_t style) {
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    const nxc_font_metrics_t *m = &g_ui.metrics[style];
    float lh = m->ascent + m->descent + m->line_gap;
    if (!(lh > 0.0f)) lh = nxui_type(style).size * 1.3f;
    return (float)(int32_t)(lh + 0.5f);
}

nxc_status_t nxui_text_in_rect(nxc_canvas_t *c, nxui_text_style_t style, nxc_rect_t r,
                               const char *utf8, nxc_color_t color, nxc_align_t align) {
    if (!c || !utf8) return NXC_ERR_INVALID_ARG;
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    float cap = g_ui.metrics[style].cap_height;
    if (!(cap > 0.0f)) cap = nxui_type(style).size * 0.66f;
    float baseline = r.y + 0.5f * (r.h + cap);
    baseline = (float)(int32_t)(baseline + 0.5f);      /* crisp baseline */
    float x = r.x;
    if (align == NXC_ALIGN_CENTER) x = r.x + 0.5f * r.w;
    else if (align == NXC_ALIGN_RIGHT) x = r.x + r.w;
    nxc_text_opts_t o;
    o.letter_spacing = 0.0f; o.align = align; o.max_width = r.w; o.no_kerning = 0;
    return nxui_text(c, style, x, baseline, utf8, color, &o);
}

static inline int utf8_cont(char ch) { return ((uint8_t)ch & 0xC0u) == 0x80u; }

/* Longest prefix of s[0..n) in bytes (ending on a UTF-8 boundary) whose
 * width fits maxw; at least one character so wrapping always advances. */
static int32_t wrap_fit(const nxc_font_t *f, float px, const nxc_text_opts_t *o,
                        const char *s, int32_t n, float maxw) {
    int32_t first = 1;
    while (first < n && utf8_cont(s[first])) first++;
    int32_t lo = first, hi = n, best = first;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        while (mid < hi && utf8_cont(s[mid])) mid++;
        if (nxc_text_width(f, px, s, mid, o) <= maxw) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    while (best > first && best < n && utf8_cont(s[best])) best--;
    return best;
}

/*
 * Shared word-wrap engine. Lines are laid out top-down from r.y with the
 * baseline centered on the cap height inside each line box; drawing stops
 * at the bottom of r, ending the last line with an ellipsis when text is
 * left over. Returns the height used.
 */
static float wrap_draw(nxc_canvas_t *c, const nxc_font_t *f, float px, float tracking,
                       float cap_height, nxc_rect_t r, const char *utf8, nxc_color_t color,
                       float lh) {
    if (!f || !utf8 || !(px > 0.0f) || !(r.w > 0.0f) || !(lh > 0.0f)) return 0.0f;
    nxc_text_opts_t o;
    o.letter_spacing = tracking; o.align = NXC_ALIGN_LEFT; o.max_width = 0.0f; o.no_kerning = 0;
    float y = r.y;
    const char *s = utf8;
    uint32_t guard = 0;
    while (*s && guard++ < 4096) {
        int32_t n = 0;                               /* this hard line */
        while (s[n] && s[n] != '\n' && n < 8192) n++;
        int32_t take = n, next = n;
        if (n > 0 && nxc_text_width(f, px, s, n, &o) > r.w) {
            int32_t fit = wrap_fit(f, px, &o, s, n, r.w);
            int32_t sp = fit;
            while (sp > 0 && s[sp] != ' ') sp--;
            if (s[fit] == ' ') { take = fit; next = fit + 1; }
            else if (sp > 0) { take = sp; next = sp + 1; }
            else { take = fit; next = fit; }           /* one long word: split it */
        }
        const char *rest = s + next;
        if (next == n && *rest == '\n') rest++;
        while (*rest == ' ') rest++;
        int last_line = y + 2.0f * lh > r.y + r.h + 0.5f;
        float baseline = (float)(int32_t)(y + 0.5f * (lh + cap_height) + 0.5f);
        if (c) {
            if (last_line && *rest) {
                /* Ellipsis: draw what fits of the visible part plus U+2026. */
                float ew = nxc_text_width(f, px, "\xE2\x80\xA6", -1, &o);
                int32_t keep = take;
                if (nxc_text_width(f, px, s, keep, &o) + ew > r.w)
                    keep = wrap_fit(f, px, &o, s, keep, r.w - ew);
                while (keep > 0 && s[keep - 1] == ' ') keep--;
                nxc_draw_text(c, f, px, r.x, baseline, s, keep, color, &o);
                float w = keep > 0 ? nxc_text_width(f, px, s, keep, &o) : 0.0f;
                nxc_draw_text(c, f, px, r.x + w, baseline, "\xE2\x80\xA6", -1, color, &o);
            } else if (take > 0) {
                nxc_draw_text(c, f, px, r.x, baseline, s, take, color, &o);
            }
        }
        y += lh;
        if (last_line) break;
        s = rest;
    }
    return y - r.y;
}

float nxui_text_wrapped(nxc_canvas_t *c, nxui_text_style_t style, nxc_rect_t r,
                        const char *utf8, nxc_color_t color, float line_height) {
    if (style < 0 || style >= NXUI_TEXT_COUNT) style = NXUI_TEXT_BODY;
    nxui_type_t t = nxui_type(style);
    float cap = g_ui.metrics[style].cap_height;
    if (!(cap > 0.0f)) cap = t.size * 0.66f;
    float lh = line_height > 0.0f ? line_height : nxui_text_line_height(style);
    return wrap_draw(c, nxui_font(t.font), t.size, t.tracking, cap, r, utf8, color, lh);
}

float nxui_text_wrapped_font(nxc_canvas_t *c, const nxc_font_t *font, float px, nxc_rect_t r,
                             const char *utf8, nxc_color_t color, float line_height) {
    nxc_font_metrics_t m;
    if (!font || !(px > 0.0f) || nxc_font_metrics(font, px, &m) != NXC_OK) return 0.0f;
    float lh = line_height > 0.0f ? line_height
                                  : (float)(int32_t)(m.ascent + m.descent + m.line_gap + 0.5f);
    return wrap_draw(c, font, px, 0.0f, m.cap_height, r, utf8, color, lh);
}

/* ============ Materials & elevation ============ */

nxc_status_t nxui_material(nxc_canvas_t *c, nxc_rect_t r, float radius, const nxc_material_t *m) {
    if (!c || !m) return NXC_ERR_INVALID_ARG;
    const nxc_surface_t *src = g_ui.wallpaper_blur.pixels ? &g_ui.wallpaper_blur : 0;
    return nxc_draw_backdrop(c, r, radius, src, m);
}

nxc_status_t nxui_elevation(nxc_canvas_t *c, nxc_rect_t r, float radius, int32_t level) {
    if (!c) return NXC_ERR_INVALID_ARG;
    nxc_color_t sc = g_ui.theme.shadow;
    float light = (g_ui.appearance == NXUI_APPEARANCE_LIGHT) ? 0.55f : 1.0f;
    nxc_status_t st = NXC_OK;
    switch (level) {
        case 0:
            return NXC_OK;
        case 1:   /* control: tight contact shadow */
            st = nxc_draw_shadow_rrect(c, r, radius, 1.5f, 0.0f, 1.0f, 0.0f,
                                       nxc_color_fade(NXC_WITH_ALPHA(sc, 110), light));
            break;
        case 2:   /* popover / dock: soft ambient + contact */
            st = nxc_draw_shadow_rrect(c, r, radius, 18.0f, 0.0f, 10.0f, 0.0f,
                                       nxc_color_fade(NXC_WITH_ALPHA(sc, 120), light));
            if (st == NXC_OK)
                st = nxc_draw_shadow_rrect(c, r, radius, 2.0f, 0.0f, 1.0f, 0.0f,
                                           nxc_color_fade(NXC_WITH_ALPHA(sc, 90), light));
            break;
        case 3:   /* inactive window */
            st = nxc_draw_shadow_rrect(c, r, radius, 16.0f, 0.0f, 8.0f, 0.0f,
                                       nxc_color_fade(NXC_WITH_ALPHA(sc, 110), light));
            break;
        default:  /* 4: active window, deep and wide */
            st = nxc_draw_shadow_rrect(c, r, radius, 30.0f, 0.0f, 22.0f, 2.0f,
                                       nxc_color_fade(NXC_WITH_ALPHA(sc, 150), light));
            if (st == NXC_OK)
                st = nxc_draw_shadow_rrect(c, r, radius, 3.0f, 0.0f, 1.5f, 0.0f,
                                           nxc_color_fade(NXC_WITH_ALPHA(sc, 110), light));
            break;
    }
    return st;
}

/* ============ Panel cache ============ */

/* Shadow reach beyond the panel rect for each elevation level (sigma*3 +
 * offset + spread, rounded up), matching nxui_elevation. */
static int32_t panel_margin(int32_t level) {
    switch (level) {
        case 0: return 2;              /* room for the 1px outer edge of glass */
        case 1: return 8;
        case 2: return 66;
        case 3: return 58;
        default: return 116;
    }
}

typedef struct {
    int32_t        used;
    int32_t        x, y, w, h;        /* panel rect (whole pixels) */
    int32_t        ox, oy, sw, sh;    /* slot view: origin and size on screen */
    float          radius;
    nxc_material_t mat;
    int32_t        elevation;
    nxui_appearance_t appearance;
    nxui_panel_decor_fn decor;        /* optional overlay baked into the slot */
    uint32_t       decor_key;
    nxc_irect_t    solid;             /* opaque interior in slot coords (w 0 = none) */
    nxc_irect_t    vis;               /* bounds of visible pixels (alpha > 1) in slot coords */
    nxc_surface_t  surf;              /* transparent, at least sw x sh */
    int32_t        cap_w, cap_h;      /* allocated size */
    uint32_t       last_use;
} nxui_panel_slot_t;

static nxui_panel_slot_t g_panels[NXUI_PANEL_CACHE_SLOTS];
static uint32_t g_panel_tick;
static uint32_t g_panel_hits, g_panel_misses;

static int material_eq(const nxc_material_t *a, const nxc_material_t *b) {
    return a->blur_sigma == b->blur_sigma && a->saturation == b->saturation &&
           a->tint == b->tint && a->noise == b->noise && a->border == b->border;
}

/* Render the panel (shadow + material) into the slot surface. The slot
 * view covers the panel plus its shadow margin, clipped to the screen, so
 * the blurred wallpaper can be viewed through a window of the same size
 * at the same origin (the backdrop requires matching sizes). */
static nxc_status_t panel_render(nxui_panel_slot_t *sl) {
    const nxc_surface_t *wb = nxui_wallpaper_blurred();
    if (!wb) return NXC_ERR_STATE;
    nxc_surface_clear(&sl->surf, NXC_ARGB(0, 0, 0, 0));
    nxc_surface_t view = sl->surf;
    view.width = sl->sw; view.height = sl->sh;
    nxc_canvas_t c;
    nxc_status_t st = nxc_canvas_init(&c, &view);
    if (st != NXC_OK) return st;
    nxc_rect_t r = nxc_rect((float)(sl->x - sl->ox), (float)(sl->y - sl->oy), (float)sl->w, (float)sl->h);
    st = nxui_elevation(&c, r, sl->radius, sl->elevation);
    if (st != NXC_OK) return st;
    nxc_surface_t src;
    st = nxc_surface_wrap(&src, wb->pixels + (int64_t)sl->oy * wb->stride + sl->ox, sl->sw, sl->sh,
                          wb->stride, NXC_SURFACE_OPAQUE);
    if (st != NXC_OK) return st;
    st = nxc_draw_backdrop(&c, r, sl->radius, &src, &sl->mat);
    if (st != NXC_OK || !sl->decor) return st;
    /* Decoration (rim light, sheen, inner glow) is drawn with the slot
     * view's origin, so it lands exactly where the uncached path puts it. */
    return sl->decor(&c, r, sl->radius, sl->decor_key);
}

/* Analyze a freshly rendered slot once:
 *  - vis: bounds of pixels with alpha > 1 (the far shadow tail below
 *    1/255 contributes nothing visible), so blits skip empty margin;
 *  - solid: the interior inset past the corner radius, verified fully
 *    opaque pixel by pixel, which the blit copies instead of blending. */
static void panel_find_solid(nxui_panel_slot_t *sl) {
    int32_t vx0 = sl->sw, vy0 = sl->sh, vx1 = 0, vy1 = 0;
    for (int32_t y = 0; y < sl->sh; y++) {
        const uint32_t *row = sl->surf.pixels + (int64_t)y * sl->surf.stride;
        int32_t a = -1, b = -1;
        for (int32_t x = 0; x < sl->sw; x++) {
            if ((row[x] >> 24) > 1u) { if (a < 0) a = x; b = x; }
        }
        if (a < 0) continue;
        if (a < vx0) vx0 = a;
        if (b + 1 > vx1) vx1 = b + 1;
        if (y < vy0) vy0 = y;
        vy1 = y + 1;
    }
    sl->vis = (vx1 > vx0 && vy1 > vy0) ? nxc_irect(vx0, vy0, vx1 - vx0, vy1 - vy0) : nxc_irect(0, 0, 0, 0);

    sl->solid = nxc_irect(0, 0, 0, 0);
    int32_t inset = (int32_t)(sl->radius + 0.999f) + 1;
    int32_t x0 = sl->x - sl->ox + 1, x1 = sl->x - sl->ox + sl->w - 1;
    int32_t y0 = sl->y - sl->oy + inset, y1 = sl->y - sl->oy + sl->h - inset;
    if (x1 - x0 < 16 || y1 - y0 < 4) return;
    for (int32_t y = y0; y < y1; y++) {
        const uint32_t *row = sl->surf.pixels + (int64_t)y * sl->surf.stride;
        for (int32_t x = x0; x < x1; x++) {
            if ((row[x] >> 24) != 0xFFu) return;      /* not opaque: blend everything */
        }
    }
    sl->solid = nxc_irect(x0, y0, x1 - x0, y1 - y0);
}

static void panel_blit_part(nxc_canvas_t *c, const nxui_panel_slot_t *sl, int32_t sx, int32_t sy,
                            int32_t w, int32_t h) {
    /* Restrict to the visible bounds. */
    int32_t x0 = sx > sl->vis.x ? sx : sl->vis.x;
    int32_t y0 = sy > sl->vis.y ? sy : sl->vis.y;
    int32_t x1 = sx + w < sl->vis.x + sl->vis.w ? sx + w : sl->vis.x + sl->vis.w;
    int32_t y1 = sy + h < sl->vis.y + sl->vis.h ? sy + h : sl->vis.y + sl->vis.h;
    if (x1 <= x0 || y1 <= y0) return;
    nxc_draw_surface(c, &sl->surf, nxc_irect(x0, y0, x1 - x0, y1 - y0),
                     nxc_rect((float)(sl->ox + x0), (float)(sl->oy + y0), (float)(x1 - x0), (float)(y1 - y0)),
                     255, NXC_FILTER_NEAREST);
}

/* Blit a cached panel. With full opacity, an integer translation and no
 * rounded clip, the opaque interior is copied row by row (respecting the
 * rect clip) and only the rim, corners and shadow are blended. */
static nxc_status_t panel_blit(nxc_canvas_t *c, const nxui_panel_slot_t *sl) {
    const nxc_irect_t in = sl->solid;
    float tx = c->st.tx, ty = c->st.ty;
    int fast = in.w > 0 && c->st.alpha == 255 && c->st.rclip_radius == 0.0f && c->target &&
               c->target->pixels && tx == (float)(int32_t)tx && ty == (float)(int32_t)ty &&
               tx > -16384.0f && tx < 16384.0f && ty > -16384.0f && ty < 16384.0f;
    if (!fast) {
        if (sl->vis.w <= 0) return NXC_OK;
        return nxc_draw_surface(c, &sl->surf, sl->vis,
                                nxc_rect((float)(sl->ox + sl->vis.x), (float)(sl->oy + sl->vis.y),
                                         (float)sl->vis.w, (float)sl->vis.h),
                                255, NXC_FILTER_NEAREST);
    }
    /* Blended parts: above, below, left of and right of the interior. */
    panel_blit_part(c, sl, 0, 0, sl->sw, in.y);
    panel_blit_part(c, sl, 0, in.y + in.h, sl->sw, sl->sh - (in.y + in.h));
    panel_blit_part(c, sl, 0, in.y, in.x, in.h);
    panel_blit_part(c, sl, in.x + in.w, in.y, sl->sw - (in.x + in.w), in.h);
    /* Copied part, clipped to the canvas clip in device space. */
    int32_t dx0 = sl->ox + in.x + (int32_t)tx, dy0 = sl->oy + in.y + (int32_t)ty;
    nxc_irect_t cl = c->st.clip;
    int32_t ax0 = dx0 > cl.x ? dx0 : cl.x;
    int32_t ay0 = dy0 > cl.y ? dy0 : cl.y;
    int32_t ax1 = dx0 + in.w < cl.x + cl.w ? dx0 + in.w : cl.x + cl.w;
    int32_t ay1 = dy0 + in.h < cl.y + cl.h ? dy0 + in.h : cl.y + cl.h;
    if (ax0 < 0) ax0 = 0;
    if (ay0 < 0) ay0 = 0;
    if (ax1 > c->target->width) ax1 = c->target->width;
    if (ay1 > c->target->height) ay1 = c->target->height;
    for (int32_t y = ay0; y < ay1; y++) {
        const uint32_t *src = sl->surf.pixels + (int64_t)(y - dy0 + in.y) * sl->surf.stride + (ax0 - dx0 + in.x);
        uint32_t *dst = c->target->pixels + (int64_t)y * c->target->stride + ax0;
        for (int32_t x = 0; x < ax1 - ax0; x++) dst[x] = src[x];
    }
    return NXC_OK;
}

/* Uncached fallback: shadow, material, then the decoration in place. */
static nxc_status_t panel_draw_direct(nxc_canvas_t *c, nxc_rect_t r, float radius,
                                      const nxc_material_t *m, int32_t elevation,
                                      nxui_panel_decor_fn decor, uint32_t decor_key) {
    nxc_status_t st = nxui_elevation(c, r, radius, elevation);
    if (st != NXC_OK) return st;
    st = nxui_material(c, r, radius, m);
    if (st != NXC_OK || !decor) return st;
    return decor(c, r, radius, decor_key);
}

nxc_status_t nxui_panel(nxc_canvas_t *c, nxc_rect_t r, float radius, const nxc_material_t *m,
                        int32_t elevation) {
    return nxui_panel_ex(c, r, radius, m, elevation, 0, 0);
}

void nxui_panel_stats(uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = g_panel_hits;
    if (misses) *misses = g_panel_misses;
}

nxc_status_t nxui_panel_ex(nxc_canvas_t *c, nxc_rect_t r, float radius, const nxc_material_t *m,
                           int32_t elevation, nxui_panel_decor_fn decor, uint32_t decor_key) {
    if (!c || !m || !g_ui.ready) return NXC_ERR_INVALID_ARG;
    if (!(radius >= 0.0f) || radius > 4096.0f) return NXC_ERR_INVALID_ARG;
    if (!(r.w > 0.0f) || !(r.h > 0.0f) || r.w > 8192.0f || r.h > 8192.0f) return NXC_ERR_INVALID_ARG;
    int32_t x = (int32_t)(r.x + 0.5f), y = (int32_t)(r.y + 0.5f);
    int32_t w = (int32_t)(r.w + 0.5f), h = (int32_t)(r.h + 0.5f);
    if (w <= 0 || h <= 0) return NXC_OK;
    if (elevation < 0) elevation = 0;
    if (elevation > 4) elevation = 4;
    const nxc_surface_t *wb = nxui_wallpaper_blurred();
    nxc_rect_t ir = nxc_rect((float)x, (float)y, (float)w, (float)h);
    if (!wb || x < 0 || y < 0 || x + w > wb->width || y + h > wb->height) {
        /* partly off screen: uncached */
        return panel_draw_direct(c, ir, radius, m, elevation, decor, decor_key);
    }
    int32_t margin = panel_margin(elevation);
    int32_t ox = x - margin < 0 ? 0 : x - margin;
    int32_t oy = y - margin < 0 ? 0 : y - margin;
    int32_t ex = x + w + margin > wb->width ? wb->width : x + w + margin;
    int32_t ey = y + h + margin > wb->height ? wb->height : y + h + margin;
    int32_t sw = ex - ox, sh = ey - oy;

    g_panel_tick++;
    nxui_panel_slot_t *hit = 0, *victim = 0;
    for (int32_t i = 0; i < NXUI_PANEL_CACHE_SLOTS; i++) {
        nxui_panel_slot_t *sl = &g_panels[i];
        if (sl->used && sl->x == x && sl->y == y && sl->w == w && sl->h == h && sl->radius == radius &&
            sl->elevation == elevation && sl->appearance == g_ui.appearance && material_eq(&sl->mat, m) &&
            sl->decor == decor && sl->decor_key == decor_key) {
            hit = sl;
            break;
        }
        if (!victim || !sl->used || (victim->used && sl->last_use < victim->last_use)) victim = sl;
    }
    if (!hit) {
        nxui_panel_slot_t *sl = victim;
        if (!sl->surf.pixels || sl->cap_w < sw || sl->cap_h < sh) {
            /* Allocate with slack so small size changes reuse the buffer
             * (the heap is a bump allocator: every allocation is for good). */
            int32_t cw = (sw + 63) & ~63, ch = (sh + 63) & ~63;
            if (cw < sl->cap_w) cw = sl->cap_w;
            if (ch < sl->cap_h) ch = sl->cap_h;
            nxc_surface_t ns;
            if (nxc_surface_create(&ns, cw, ch, 0) != NXC_OK) {
                ui_log("[NXUI] panel cache: slot allocation failed, drawing uncached\n");
                return panel_draw_direct(c, ir, radius, m, elevation, decor, decor_key);
            }
            sl->surf = ns;
            sl->cap_w = cw; sl->cap_h = ch;
        }
        sl->used = 1;
        sl->x = x; sl->y = y; sl->w = w; sl->h = h;
        sl->ox = ox; sl->oy = oy; sl->sw = sw; sl->sh = sh;
        sl->radius = radius; sl->mat = *m;
        sl->elevation = elevation; sl->appearance = g_ui.appearance;
        sl->decor = decor; sl->decor_key = decor_key;
        g_panel_misses++;
        nxc_status_t st = panel_render(sl);
        if (st != NXC_OK) { sl->used = 0; return st; }
        panel_find_solid(sl);
        hit = sl;
    } else {
        g_panel_hits++;
    }
    hit->last_use = g_panel_tick;
    return panel_blit(c, hit);
}

const nxc_surface_t *nxui_wallpaper(void) { return g_ui.wallpaper.pixels ? &g_ui.wallpaper : 0; }
const nxc_surface_t *nxui_wallpaper_blurred(void) {
    return g_ui.wallpaper_blur.pixels ? &g_ui.wallpaper_blur : 0;
}

/* ============ Wallpaper: "Vajra Facets" ============ */

/*
 * A procedural, resolution-independent take on the NeolyxOS low-poly
 * wallpaper: a deep navy field, saffron and violet aurora glows, and a
 * jittered triangle mesh whose facets catch light from the glows.
 */
#define FACET_COLS 9
#define FACET_ROWS 6

static nxc_status_t paint_glow(nxc_canvas_t *c, float w, float h, float cx, float cy,
                               float radius, nxc_color_t color, uint8_t alpha) {
    nxc_paint_t p = nxc_paint_radial(cx, cy, radius, NXC_WITH_ALPHA(color, alpha),
                                     NXC_WITH_ALPHA(color, 0));
    p.nstops = 0;
    nxc_paint_add_stop(&p, 0.0f, NXC_WITH_ALPHA(color, alpha));
    nxc_paint_add_stop(&p, 0.45f, NXC_WITH_ALPHA(color, (uint8_t)(alpha / 2)));
    nxc_paint_add_stop(&p, 1.0f, NXC_WITH_ALPHA(color, 0));
    return nxc_fill_rect_paint(c, nxc_rect(0, 0, w, h), &p);
}

/* Light a facet: how much each glow reaches its centroid (0..1). */
static float glow_falloff(float x, float y, float gx, float gy, float radius) {
    float dx = (x - gx) / radius, dy = (y - gy) / radius;
    float d2 = dx * dx + dy * dy;
    float v = 1.0f - d2;
    return v > 0.0f ? v * v : 0.0f;
}

static nxc_status_t wallpaper_generate(nxc_surface_t *s) {
    nxc_canvas_t c;
    nxc_status_t st = nxc_canvas_init(&c, s);
    if (st != NXC_OK) return st;
    float w = (float)s->width, h = (float)s->height;

    /* Base field */
    nxc_paint_t base = nxc_paint_linear(0, 0, w * 0.35f, h, NXC_RGB(0x0A, 0x0F, 0x1E),
                                        NXC_RGB(0x05, 0x08, 0x12));
    st = nxc_fill_rect_paint(&c, nxc_rect(0, 0, w, h), &base);
    if (st != NXC_OK) return st;

    /* Aurora glows (positions relative to screen size) */
    const float g1x = w * 0.18f, g1y = h * 0.92f, g1r = w * 0.62f;   /* saffron */
    const float g2x = w * 0.86f, g2y = h * 0.10f, g2r = w * 0.70f;   /* violet  */
    const float g3x = w * 0.55f, g3y = h * 0.55f, g3r = w * 0.45f;   /* sky     */
    paint_glow(&c, w, h, g2x, g2y, g2r, VAJRA_VIOLET, 120);
    paint_glow(&c, w, h, g1x, g1y, g1r, VAJRA_SAFFRON, 105);
    paint_glow(&c, w, h, g3x, g3y, g3r, VAJRA_SKY, 34);

    /* Jittered mesh points */
    static nxc_point_t pts[(FACET_ROWS + 1) * (FACET_COLS + 1)];
    for (int32_t r = 0; r <= FACET_ROWS; r++) {
        for (int32_t q = 0; q <= FACET_COLS; q++) {
            float fx = (float)q / (float)FACET_COLS, fy = (float)r / (float)FACET_ROWS;
            float jx = (q == 0 || q == FACET_COLS) ? 0.0f : (hash_unit((uint32_t)(r * 131 + q * 7 + 1)) - 0.5f) * 0.8f;
            float jy = (r == 0 || r == FACET_ROWS) ? 0.0f : (hash_unit((uint32_t)(r * 17 + q * 977 + 3)) - 0.5f) * 0.8f;
            pts[r * (FACET_COLS + 1) + q].x = (fx + jx / (float)FACET_COLS) * w;
            pts[r * (FACET_COLS + 1) + q].y = (fy + jy / (float)FACET_ROWS) * h;
        }
    }

    nxc_point_t pbuf[8];
    uint8_t vbuf[8];
    nxc_path_t tri;
    nxc_stroke_t edge;
    edge.width = 1.0f;
    edge.cap = NXC_CAP_ROUND;
    edge.join = NXC_JOIN_ROUND;
    edge.miter_limit = 4.0f;

    for (int32_t r = 0; r < FACET_ROWS; r++) {
        for (int32_t q = 0; q < FACET_COLS; q++) {
            const nxc_point_t *a = &pts[r * (FACET_COLS + 1) + q];
            const nxc_point_t *b = &pts[r * (FACET_COLS + 1) + q + 1];
            const nxc_point_t *cc = &pts[(r + 1) * (FACET_COLS + 1) + q];
            const nxc_point_t *d = &pts[(r + 1) * (FACET_COLS + 1) + q + 1];
            /* Alternate the diagonal for a less regular mesh. */
            int flip = (int)(hash32((uint32_t)(r * 53 + q * 29)) & 1u);
            const nxc_point_t *t0[3] = { a, b, flip ? d : cc };
            const nxc_point_t *t1[3] = { flip ? a : b, d, cc };
            const nxc_point_t **tris[2] = { t0, t1 };
            for (int k = 0; k < 2; k++) {
                const nxc_point_t **v = tris[k];
                float mx = (v[0]->x + v[1]->x + v[2]->x) / 3.0f;
                float my = (v[0]->y + v[1]->y + v[2]->y) / 3.0f;
                float l1 = glow_falloff(mx, my, g1x, g1y, g1r);
                float l2 = glow_falloff(mx, my, g2x, g2y, g2r);
                float l3 = glow_falloff(mx, my, g3x, g3y, g3r);
                float rnd = hash_unit((uint32_t)(r * 1009 + q * 31 + k * 7 + 11));
                /* Facet shading: light facets toward glows, darken others. */
                nxc_path_init(&tri, pbuf, 8, vbuf, 8);
                nxc_path_move_to(&tri, v[0]->x, v[0]->y);
                nxc_path_line_to(&tri, v[1]->x, v[1]->y);
                nxc_path_line_to(&tri, v[2]->x, v[2]->y);
                nxc_path_close(&tri);
                if (rnd > 0.5f) {
                    uint8_t a8 = (uint8_t)(6.0f + 22.0f * (rnd - 0.5f) * 2.0f * (0.35f + l1 + l2 + l3));
                    nxc_color_t tint = (l1 > l2) ? NXC_RGB(0xFF, 0xB0, 0x70) : NXC_RGB(0xC8, 0xB8, 0xFF);
                    nxc_fill_path(&c, &tri, NXC_FILL_NONZERO, NXC_WITH_ALPHA(tint, a8));
                } else {
                    uint8_t a8 = (uint8_t)(10.0f + 26.0f * (0.5f - rnd) * 2.0f);
                    nxc_fill_path(&c, &tri, NXC_FILL_NONZERO, NXC_ARGB(a8, 0, 0, 0x08));
                }
                /* Edges glow faintly, more near the aurora. */
                float el = clamp01(0.10f + 0.9f * (l1 + l2));
                uint8_t ea = (uint8_t)(10.0f + 40.0f * el);
                nxc_color_t ec = (l1 > l2) ? NXC_RGB(0xFF, 0xC0, 0x90) : NXC_RGB(0xD8, 0xCC, 0xFF);
                nxc_stroke_path(&c, &tri, &edge, NXC_WITH_ALPHA(ec, ea));
            }
        }
    }

    /* Vignette keeps attention on the center and the UI readable. */
    nxc_paint_t vig = nxc_paint_radial(w * 0.5f, h * 0.45f, w * 0.78f, NXC_ARGB(0, 0, 0, 0),
                                       NXC_ARGB(150, 0, 0, 0));
    vig.nstops = 0;
    nxc_paint_add_stop(&vig, 0.0f, NXC_ARGB(0, 0, 0, 0));
    nxc_paint_add_stop(&vig, 0.6f, NXC_ARGB(30, 0, 0, 0));
    nxc_paint_add_stop(&vig, 1.0f, NXC_ARGB(150, 0, 0, 0));
    return nxc_fill_rect_paint(&c, nxc_rect(0, 0, w, h), &vig);
}

/* ============ Cursor ============ */

/* Arrow outline in cursor-local units, tip at (0, 0). */
static nxc_status_t cursor_path(nxc_path_t *p, float ox, float oy) {
    static const float pts[][2] = {
        { 0.0f, 0.0f }, { 0.0f, 17.2f }, { 4.3f, 13.3f }, { 7.1f, 19.8f },
        { 10.0f, 18.6f }, { 7.2f, 12.3f }, { 12.6f, 12.3f },
    };
    nxc_status_t st = nxc_path_move_to(p, ox + pts[0][0], oy + pts[0][1]);
    for (uint32_t i = 1; i < sizeof(pts) / sizeof(pts[0]) && st == NXC_OK; i++) {
        st = nxc_path_line_to(p, ox + pts[i][0], oy + pts[i][1]);
    }
    if (st == NXC_OK) st = nxc_path_close(p);
    return st;
}

static nxc_status_t cursor_build(void) {
    nxc_status_t st = nxc_surface_create(&g_ui.cursor, CURSOR_SIZE, CURSOR_SIZE, 0);
    if (st != NXC_OK) return st;
    nxc_canvas_t c;
    st = nxc_canvas_init(&c, &g_ui.cursor);
    if (st != NXC_OK) return st;
    nxc_point_t pbuf[16];
    uint8_t vbuf[16];
    nxc_path_t p;
    nxc_path_init(&p, pbuf, 16, vbuf, 16);

    /* Soft shadow: dark arrow offset down, blurred. */
    cursor_path(&p, CURSOR_HOT_X + 0.4f, CURSOR_HOT_Y + 1.4f);
    nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_ARGB(120, 0, 0, 0));
    st = nxc_blur_surface(&g_ui.cursor, nxc_irect(0, 0, CURSOR_SIZE, CURSOR_SIZE), 1.4f);
    if (st != NXC_OK) return st;

    /* Body and outline */
    nxc_path_reset(&p);
    cursor_path(&p, CURSOR_HOT_X, CURSOR_HOT_Y);
    nxc_fill_path(&c, &p, NXC_FILL_NONZERO, NXC_RGB(0xFF, 0xFF, 0xFF));
    nxc_stroke_t s;
    s.width = 1.25f; s.cap = NXC_CAP_ROUND; s.join = NXC_JOIN_ROUND; s.miter_limit = 4.0f;
    return nxc_stroke_path(&c, &p, &s, NXC_RGB(0x08, 0x08, 0x0C));
}

/* ============ Motion ============ */

float nxui_time(void) {
    uint64_t now = nx_gettime();
    return (float)(now - g_ui.t0_ms) * 0.001f;
}

float nxui_frame_dt(void) { return g_ui.dt; }

void nxui_spring_step(nxui_spring_t *s, float target, float stiffness, float damping, float dt) {
    if (!s || !(dt > 0.0f)) return;
    if (dt > 0.05f) dt = 0.05f;
    /* Semi-implicit Euler in 4 sub-steps keeps stiff springs stable. */
    float h = dt * 0.25f;
    for (int i = 0; i < 4; i++) {
        float a = stiffness * (target - s->value) - damping * s->velocity;
        s->velocity += a * h;
        s->value += s->velocity * h;
    }
}

float nxui_approach(float value, float target, float tau, float dt) {
    if (!(tau > 0.0f)) return target;
    /* First-order response; dt/(tau+dt) is the implicit-Euler step of
     * dv/dt = (target - v)/tau and never overshoots. */
    if (!(dt > 0.0f)) return value;
    float k = dt / (tau + dt);
    return value + (target - value) * k;
}

float nxui_ease_out_cubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float nxui_ease_in_out_cubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * 0.5f;
}

/* ============ Lifecycle ============ */

nxc_canvas_t *nxui_canvas(void) { return g_ui.ready ? &g_ui.canvas : 0; }
int32_t nxui_width(void) { return (int32_t)g_ui.fb.width; }
int32_t nxui_height(void) { return (int32_t)g_ui.fb.height; }

static nxc_status_t load_font(nxui_font_id_t id, const uint8_t *start, const uint8_t *end,
                              const char *name) {
    uint64_t size = (uint64_t)(end - start);
    if (end <= start || size > 0x7FFFFFFFu) {
        ui_log("[NXUI] font asset missing: ");
        ui_log(name);
        ui_log("\n");
        return NXC_ERR_NOT_FOUND;
    }
    nxc_status_t st = nxc_font_load(&g_ui.fonts[id], start, (uint32_t)size);
    if (st != NXC_OK) {
        g_ui.fonts[id] = 0;
        ui_log_status(name, st);
        return st;
    }
    ui_log("[NXUI] font loaded: ");
    ui_log(name);
    ui_log("\n");
    return NXC_OK;
}

/*
 * Fallback chains and registry entries for the loaded system faces.
 * Sans faces fall back to Mono (box drawing, technical symbols), Mono to
 * Regular. Failures are logged; text still renders without them.
 */
static void fonts_register(void) {
    static const struct {
        nxui_font_id_t id;
        const char *family, *style, *role;
    } k_faces[] = {
        { NXUI_FONT_REGULAR, "NX Sans",      "Regular",  "text"     },
        { NXUI_FONT_MEDIUM,  "NX Sans",      "Medium",   "ui"       },
        { NXUI_FONT_DISPLAY, "NX Sans",      "Semibold", "display"  },
        { NXUI_FONT_BOLD,    "NX Sans",      "Bold",     "emphasis" },
        { NXUI_FONT_ITALIC,  "NX Sans",      "Italic",   "emphasis" },
        { NXUI_FONT_MONO,    "NX Sans Mono", "Regular",  "mono"     },
    };
    nxc_font_t *mono = g_ui.fonts[NXUI_FONT_MONO];
    nxc_font_t *regular = g_ui.fonts[NXUI_FONT_REGULAR];
    for (int32_t i = 0; i < NXUI_FONT_COUNT; i++) {
        nxc_font_t *f = g_ui.fonts[i];
        if (!f || i == NXUI_FONT_SEMIBOLD) continue;      /* alias of DISPLAY */
        const nxc_font_t *fb = (f == mono) ? regular : mono;
        if (fb && fb != f && nxc_font_set_fallbacks(f, &fb, 1) != NXC_OK) {
            ui_log("[NXUI] font fallback chain not set\n");
        }
    }
    if (nxfont_registry_init() != NXC_OK) {
        ui_log("[NXUI] font registry init failed\n");
        return;
    }
    for (uint32_t k = 0; k < sizeof(k_faces) / sizeof(k_faces[0]); k++) {
        const nxc_font_t *f = g_ui.fonts[k_faces[k].id];
        if (!f) continue;
        uint32_t rid = 0;
        if (nxfont_registry_add_system(f, k_faces[k].family, k_faces[k].style, k_faces[k].role,
                                       &rid) != NXC_OK) {
            ui_log("[NXUI] font registry: system face not registered\n");
            continue;
        }
        g_ui.font_reg_ids[k_faces[k].id] = rid;
    }
    g_ui.font_reg_ids[NXUI_FONT_SEMIBOLD] = g_ui.font_reg_ids[NXUI_FONT_DISPLAY];
    uint32_t loaded = 0;
    if (nxfont_registry_load_user(0, &loaded) == NXC_OK && loaded > 0) {
        ui_log("[NXUI] user fonts loaded\n");
    }
}

nxc_status_t nxui_init(void) {
    if (g_ui.ready) return NXC_OK;
    ui_log("[NXUI] init\n");

    if (nx_fb_info(&g_ui.fb) != 0 || g_ui.fb.width == 0 || g_ui.fb.height == 0 ||
        g_ui.fb.width > 8192 || g_ui.fb.height > 8192 || g_ui.fb.bpp != 32) {
        ui_log("[NXUI] fb_info failed or unsupported format\n");
        return NXC_ERR_UNSUPPORTED;
    }
    if (!nx_fb_map()) {
        ui_log("[NXUI] fb_map failed\n");
        return NXC_ERR_STATE;
    }

    nxc_env_t env;
    env.alloc = ui_alloc;
    env.free = 0;                       /* heap is a bump allocator */
    env.log = ui_log;
    env.max_width = (int32_t)g_ui.fb.width;
    env.max_height = (int32_t)g_ui.fb.height;
    env.glyph_cache_bytes = 3u * 1024u * 1024u;
    env.shadow_cache_bytes = 4u * 1024u * 1024u;
    nxc_status_t st = nxc_init(&env);
    if (st != NXC_OK) { ui_log_status("nxc_init", st); return st; }

    /* Backbuffer: linear width*4 stride, presented by the kernel flip,
     * which converts to the hardware pitch. */
    st = nxc_surface_create(&g_ui.back, (int32_t)g_ui.fb.width, (int32_t)g_ui.fb.height,
                            NXC_SURFACE_OPAQUE);
    if (st != NXC_OK) { ui_log_status("backbuffer", st); return st; }
    st = nxc_canvas_init(&g_ui.canvas, &g_ui.back);
    if (st != NXC_OK) { ui_log_status("canvas", st); return st; }

    /* Fonts: Regular is required, the rest fall back to it. */
    st = load_font(NXUI_FONT_REGULAR, nxui_font_regular, nxui_font_regular_end, "NXSans Text");
    if (st != NXC_OK) return st;
    load_font(NXUI_FONT_MEDIUM,  nxui_font_medium,  nxui_font_medium_end,  "NXSans UI");
    load_font(NXUI_FONT_BOLD,    nxui_font_bold,    nxui_font_bold_end,    "NXSans Bold");
    load_font(NXUI_FONT_DISPLAY, nxui_font_display, nxui_font_display_end, "NXSans Display");
    load_font(NXUI_FONT_MONO,    nxui_font_mono,    nxui_font_mono_end,    "NXSans Mono");
    load_font(NXUI_FONT_ITALIC,  nxui_font_italic,  nxui_font_italic_end,  "NXSans Italic");
    g_ui.fonts[NXUI_FONT_SEMIBOLD] = g_ui.fonts[NXUI_FONT_DISPLAY];   /* same 600 face */
    fonts_register();
    for (int32_t i = 0; i < NXUI_TEXT_COUNT; i++) {
        nxui_type_t t = g_type_scale[i];
        if (nxc_font_metrics(nxui_font(t.font), t.size, &g_ui.metrics[i]) != NXC_OK) {
            nxc_font_metrics_t zero = { 0 };
            g_ui.metrics[i] = zero;
        }
    }

    g_ui.appearance = NXUI_APPEARANCE_DARK;
    theme_build(&g_ui.theme, g_ui.appearance);

    /* Wallpaper + blurred copy for materials */
    st = nxc_surface_create(&g_ui.wallpaper, (int32_t)g_ui.fb.width, (int32_t)g_ui.fb.height,
                            NXC_SURFACE_OPAQUE);
    if (st == NXC_OK) st = wallpaper_generate(&g_ui.wallpaper);
    if (st != NXC_OK) { ui_log_status("wallpaper", st); return st; }
    st = nxc_surface_create(&g_ui.wallpaper_blur, (int32_t)g_ui.fb.width, (int32_t)g_ui.fb.height,
                            NXC_SURFACE_OPAQUE);
    if (st == NXC_OK)
        st = nxc_surface_copy(&g_ui.wallpaper_blur, 0, 0, &g_ui.wallpaper,
                              nxc_irect(0, 0, g_ui.wallpaper.width, g_ui.wallpaper.height));
    if (st == NXC_OK)
        st = nxc_blur_surface(&g_ui.wallpaper_blur,
                              nxc_irect(0, 0, g_ui.wallpaper.width, g_ui.wallpaper.height), 26.0f);
    if (st != NXC_OK) {
        /* Materials degrade to live blur; not fatal. */
        ui_log_status("wallpaper blur", st);
        nxc_surface_destroy(&g_ui.wallpaper_blur);
    }

    st = cursor_build();
    if (st != NXC_OK) ui_log_status("cursor", st);

    st = nxicons_init(6u * 1024u * 1024u);
    if (st != NXC_OK) ui_log_status("icons", st);

    /* The kernel draws its own fallback cursor after every flip; hide it
     * since the shell now draws an anti-aliased one into the frame. */
    nx_cursor_set(0, 0, 0);

    g_ui.t0_ms = nx_gettime();
    g_ui.last_frame_ms = g_ui.t0_ms;
    g_ui.dt = 1.0f / 60.0f;
    g_ui.ready = 1;
    ui_log("[NXUI] ready\n");
    return NXC_OK;
}

void nxui_frame_begin(void) {
    if (!g_ui.ready) return;
    uint64_t now = nx_gettime();
    float dt = (float)(now - g_ui.last_frame_ms) * 0.001f;
    if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
    if (dt > 0.1f) dt = 0.1f;
    g_ui.dt = dt;
    g_ui.last_frame_ms = now;
    g_ui.frame++;

    /* Fresh canvas state each frame (drops any unbalanced saves). */
    if (g_ui.canvas.depth != 0) ui_log("[NXUI] warning: unbalanced nxc_save in last frame\n");
    nxc_canvas_init(&g_ui.canvas, &g_ui.back);
    nxc_surface_copy(&g_ui.back, 0, 0, &g_ui.wallpaper,
                     nxc_irect(0, 0, g_ui.wallpaper.width, g_ui.wallpaper.height));
}

nxc_status_t nxui_frame_end(int32_t cursor_x, int32_t cursor_y) {
    if (!g_ui.ready) return NXC_ERR_STATE;
    nxc_canvas_init(&g_ui.canvas, &g_ui.back);
    if (g_ui.cursor.pixels) {
        nxc_draw_surface(&g_ui.canvas, &g_ui.cursor, nxc_irect(0, 0, CURSOR_SIZE, CURSOR_SIZE),
                         nxc_rect((float)cursor_x - CURSOR_HOT_X, (float)cursor_y - CURSOR_HOT_Y,
                                  CURSOR_SIZE, CURSOR_SIZE),
                         255, NXC_FILTER_NEAREST);
    }
    if (nx_fb_flip(g_ui.back.pixels) != 0) {
        static int warned = 0;
        if (!warned) { ui_log("[NXUI] fb_flip failed\n"); warned = 1; }
        return NXC_ERR_STATE;
    }
    return NXC_OK;
}
