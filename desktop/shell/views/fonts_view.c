/*
 * NeolyxOS - Font Manager (shell-hosted app view, see app_view.h)
 *
 * Left: glass sidebar with a search field and the installed families
 * (system and user, from the font registry). Right: the selected family,
 * its styles as chips, and three tabs:
 *   Specimen    editable sample line with a size slider, a waterfall of
 *               the pangram and the alphabet
 *   Characters  character map (glyph grid, paged) with a large preview and
 *               the face's metric lines for the selected character
 *   Info        name-table data, weight, glyph/character counts, kerning,
 *               file size, location and the license text
 * Install... opens a sheet that validates a .ttf path before installing
 * it through nxfont_install(); user fonts can be disabled and enabled.
 *
 * All layout is computed in content-local coordinates; render translates
 * the canvas to the content origin so the same rects serve hit testing.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "app_view.h"
#include "nxui.h"
#include "nxglass.h"
#include "nxframe.h"
#include "nxicons.h"
#include "nxfont_registry.h"
#include "nxsyscall.h"

/* ============ Layout constants ============ */

#define SIDEBAR_W        212.0f
#define PAD              22.0f
#define ROW_H            30.0f
#define CHIP_H           26.0f
#define TAB_W            264.0f
#define CELL             46.0f
#define GRID_COLS        7
#define GRID_ROWS        7
#define GRID_PAGE        (GRID_COLS * GRID_ROWS)
#define MAX_CHARS        8192
#define MAX_STYLES       12
#define MIN_PX           8.0f
#define MAX_PX           144.0f

#define SAMPLE_MAX       120
#define SEARCH_MAX       40

typedef enum { TAB_SPECIMEN = 0, TAB_CHARS = 1, TAB_INFO = 2 } fv_tab_t;
typedef enum { FOCUS_NONE = 0, FOCUS_SEARCH, FOCUS_SAMPLE, FOCUS_PATH } fv_focus_t;

/* ============ State ============ */

static struct {
    int        open;
    char       family[64];            /* selected family */
    uint32_t   style_id;              /* selected registry id */
    fv_tab_t   tab;
    fv_focus_t focus;
    char       search[SEARCH_MAX];
    char       sample[SAMPLE_MAX];
    float      size_px;
    int        dragging_slider;
    int32_t    mx, my;                /* last pointer position (local) */
    uint8_t    shift, caps;

    /* character map of style_id */
    uint32_t   chars_for;             /* registry id the list was built for */
    uint32_t   chars[MAX_CHARS];
    uint32_t   nchars;
    int        chars_truncated;
    uint32_t   page;
    uint32_t   sel_char;              /* index into chars */

    /* family list scroll (rows) */
    uint32_t   list_first;

    /* install sheet */
    int        sheet;
    char       path[NXFONT_PATH_MAX];
    char       message[NXFONT_REASON_MAX];
    int        message_ok;            /* 1 success, 0 error, -1 none */

    /* status line (actions) */
    char       status[NXFONT_REASON_MAX];
} g_fv;

static const char *k_default_sample = "Sphinx of black quartz, judge my vow";
static const char *k_pangram = "The quick brown fox jumps over the lazy dog";

/* ============ Small helpers ============ */

static uint32_t fv_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void fv_copy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void fv_cat(char *dst, uint32_t cap, const char *src) {
    uint32_t n = fv_len(dst);
    if (n < cap) fv_copy(dst + n, cap - n, src);
}

static void fv_cat_u32(char *dst, uint32_t cap, uint32_t v) {
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    char o[12];
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
    fv_cat(dst, cap, o);
}

static void fv_cat_hex(char *dst, uint32_t cap, uint32_t v, int digits) {
    static const char hx[] = "0123456789ABCDEF";
    char o[9];
    int n = 0;
    for (int i = 7; i >= 0; i--) {
        uint32_t d = (v >> (i * 4)) & 0xFu;
        if (d || n || i < digits) o[n++] = hx[d];
    }
    o[n] = 0;
    fv_cat(dst, cap, o);
}

static char fv_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int fv_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (fv_lower(*a) != fv_lower(*b)) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int fv_contains_ci(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return 1;
    for (const char *h = hay; h && *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && fv_lower(*a) == fv_lower(*b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

/* Encode one code point as UTF-8 (out >= 5 bytes). */
static void fv_utf8(uint32_t cp, char *out) {
    if (cp < 0x80u) { out[0] = (char)cp; out[1] = 0; }
    else if (cp < 0x800u) { out[0] = (char)(0xC0u | (cp >> 6)); out[1] = (char)(0x80u | (cp & 0x3Fu)); out[2] = 0; }
    else if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12)); out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu)); out[3] = 0;
    } else {
        out[0] = (char)(0xF0u | (cp >> 18)); out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); out[3] = (char)(0x80u | (cp & 0x3Fu)); out[4] = 0;
    }
}

static void fv_log(const char *a, const char *b) {
    char line[200];
    line[0] = 0;
    fv_cat(line, sizeof(line), "[FONTMGR] ");
    fv_cat(line, sizeof(line), a);
    if (b) fv_cat(line, sizeof(line), b);
    fv_cat(line, sizeof(line), "\n");
    nx_debug_print(line);
}

static nxc_text_opts_t fv_opts(nxc_align_t align, float max_w) {
    nxc_text_opts_t o;
    o.letter_spacing = 0.0f; o.align = align; o.max_width = max_w; o.no_kerning = 0;
    return o;
}

/* ============ Model ============ */

static const nxfont_entry_t *sel_entry(void) { return nxfont_get(g_fv.style_id); }

static const nxc_font_t *sel_face(void) {
    const nxc_font_t *f = nxfont_face(g_fv.style_id);
    return f ? f : nxui_font(NXUI_FONT_REGULAR);
}

static uint32_t family_styles(const char *family, uint32_t *ids) {
    return nxfont_family_styles(family, ids, MAX_STYLES);
}

/* Select a family; keeps the style weight when the family has it. */
static void select_family(const char *family) {
    if (!family) return;
    uint32_t ids[MAX_STYLES];
    uint32_t n = family_styles(family, ids);
    if (n == 0) return;
    uint32_t want = 400;
    const nxfont_entry_t *cur = sel_entry();
    if (cur) want = cur->info.weight;
    uint32_t best = ids[0];
    uint32_t best_d = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        const nxfont_entry_t *e = nxfont_get(ids[i]);
        if (!e || e->info.italic) continue;
        uint32_t d = e->info.weight > want ? e->info.weight - want : want - e->info.weight;
        if (d < best_d) { best_d = d; best = ids[i]; }
    }
    fv_copy(g_fv.family, sizeof(g_fv.family), family);
    g_fv.style_id = best;
    g_fv.status[0] = 0;
}

/* Build the character list of the selected face once per selection. */
static void ensure_chars(void) {
    if (g_fv.chars_for == g_fv.style_id && g_fv.style_id != 0) return;
    const nxc_font_t *f = nxfont_face(g_fv.style_id);
    g_fv.nchars = 0;
    g_fv.chars_truncated = 0;
    g_fv.page = 0;
    g_fv.sel_char = 0;
    g_fv.chars_for = g_fv.style_id;
    if (!f) return;
    uint32_t cp = nxc_font_next_char(f, 0x20u);
    while (cp != NXC_NO_CHAR) {
        if (g_fv.nchars >= MAX_CHARS) { g_fv.chars_truncated = 1; break; }
        g_fv.chars[g_fv.nchars++] = cp;
        cp = nxc_font_next_char(f, cp + 1u);
    }
    /* Start on 'A' when present. */
    for (uint32_t i = 0; i < g_fv.nchars; i++) {
        if (g_fv.chars[i] == 'A') { g_fv.sel_char = i; g_fv.page = i / GRID_PAGE; break; }
    }
}

/* ============ Layout (content-local) ============ */

typedef struct {
    float w, h;
    nxc_rect_t sidebar, search, list, install_btn;
    nxc_rect_t main;
    nxc_rect_t tabs, toggle_btn;
    float chips_y;
    nxc_rect_t body;
    /* specimen */
    nxc_rect_t sample_card, slider, size_label;
    /* characters */
    nxc_rect_t grid, detail, prev_btn, next_btn;
    /* sheet */
    nxc_rect_t sheet, sheet_field, sheet_cancel, sheet_validate, sheet_install;
} fv_layout_t;

/* Sample card height: tall enough for the wrapped sample at the current
 * size, between 112 px and 56% of the window. */
static float sample_card_h(float card_w, float win_h) {
    const nxc_font_t *f = sel_face();
    const char *txt = g_fv.sample[0] ? g_fv.sample : k_default_sample;
    float px = g_fv.size_px;
    float lh = (float)(int32_t)(px * 1.12f + 0.5f);
    float used = nxui_text_wrapped_font(0, f, px, nxc_rect(0, 0, card_w - 36, 4096), txt, 0, lh);
    float h = used + 24.0f;
    float max_h = (float)(int32_t)(win_h * 0.56f);
    if (h < 112.0f) h = 112.0f;
    if (h > max_h) h = max_h;
    return (float)(int32_t)h;
}

static fv_layout_t layout_for(const desktop_window_t *win) {
    fv_layout_t L;
    L.w = (float)win->width;
    L.h = (float)win->height;
    L.sidebar = nxc_rect(0, 0, SIDEBAR_W, L.h);
    L.search = nxc_rect(12, 12, SIDEBAR_W - 24, NXGLASS_FIELD_H);
    L.list = nxc_rect(8, 54, SIDEBAR_W - 16, L.h - 54 - 52);
    L.install_btn = nxc_rect(12, L.h - 42, SIDEBAR_W - 24, NXGLASS_BUTTON_H);
    L.main = nxc_rect(SIDEBAR_W, 0, L.w - SIDEBAR_W, L.h);
    float mx = SIDEBAR_W + PAD, mw = L.w - SIDEBAR_W - 2 * PAD;
    L.tabs = nxc_rect(mx + mw - TAB_W, 20, TAB_W, 28);
    L.toggle_btn = nxc_rect(mx + mw - 92, 66, 92, NXGLASS_BUTTON_H);
    L.chips_y = 66;
    L.body = nxc_rect(mx, 110, mw, L.h - 110 - 16);
    float ch = sample_card_h(mw, L.h);
    L.sample_card = nxc_rect(mx, L.body.y, mw, ch);
    L.slider = nxc_rect(mx + 46, L.body.y + ch + 12, mw - 46 - 64, 20);
    L.size_label = nxc_rect(mx + mw - 56, L.body.y + ch + 10, 56, 24);
    float gw = GRID_COLS * CELL;
    L.grid = nxc_rect(mx, L.body.y, gw, GRID_ROWS * CELL);
    L.detail = nxc_rect(mx + gw + 16, L.body.y, mw - gw - 16, GRID_ROWS * CELL + 38);
    L.prev_btn = nxc_rect(mx, L.body.y + GRID_ROWS * CELL + 10, 28, NXGLASS_BUTTON_H);
    L.next_btn = nxc_rect(mx + gw - 28, L.body.y + GRID_ROWS * CELL + 10, 28, NXGLASS_BUTTON_H);
    float sw = 420, sh = 236;
    L.sheet = nxc_rect(SIDEBAR_W + (L.w - SIDEBAR_W - sw) * 0.5f, 70, sw, sh);
    L.sheet_field = nxc_rect(L.sheet.x + 20, L.sheet.y + 96, sw - 40, NXGLASS_FIELD_H);
    L.sheet_cancel = nxc_rect(L.sheet.x + 20, L.sheet.y + sh - 46, 90, NXGLASS_BUTTON_H);
    L.sheet_install = nxc_rect(L.sheet.x + sw - 20 - 96, L.sheet.y + sh - 46, 96, NXGLASS_BUTTON_H);
    L.sheet_validate = nxc_rect(L.sheet_install.x - 10 - 96, L.sheet.y + sh - 46, 96, NXGLASS_BUTTON_H);
    return L;
}

/* Visible families after the search filter. */
static uint32_t visible_families(const char **out, uint32_t cap) {
    uint32_t n = 0;
    uint32_t total = nxfont_family_count();
    for (uint32_t i = 0; i < total && n < cap; i++) {
        const char *f = nxfont_family_at(i);
        if (f && fv_contains_ci(f, g_fv.search)) out[n++] = f;
    }
    return n;
}

/* Source of a family: system when any face is a system face. */
static int family_is_system(const char *family) {
    uint32_t ids[MAX_STYLES];
    uint32_t n = family_styles(family, ids);
    for (uint32_t i = 0; i < n; i++) {
        const nxfont_entry_t *e = nxfont_get(ids[i]);
        if (e && e->source == NXFONT_SOURCE_SYSTEM) return 1;
    }
    return 0;
}

static int family_enabled_count(const char *family) {
    uint32_t ids[MAX_STYLES];
    uint32_t n = family_styles(family, ids);
    int k = 0;
    for (uint32_t i = 0; i < n; i++) {
        const nxfont_entry_t *e = nxfont_get(ids[i]);
        if (e && e->enabled) k++;
    }
    return k;
}

/* Sidebar rows: a header row per source group followed by its families.
 * kind 0 = header (name = label), 1 = family. */
typedef struct { int kind; const char *name; } fv_row_t;

static uint32_t sidebar_rows(fv_row_t *rows, uint32_t cap) {
    const char *fams[NXFONT_MAX_FONTS];
    uint32_t nf = visible_families(fams, NXFONT_MAX_FONTS);
    uint32_t n = 0;
    for (int group = 0; group < 2; group++) {
        int header_done = 0;
        for (uint32_t i = 0; i < nf && n < cap; i++) {
            if (family_is_system(fams[i]) != (group == 0)) continue;
            if (!header_done && n < cap) {
                rows[n].kind = 0;
                rows[n].name = group == 0 ? "SYSTEM" : "USER";
                n++;
                header_done = 1;
            }
            if (n < cap) { rows[n].kind = 1; rows[n].name = fams[i]; n++; }
        }
        if (group == 1 && !header_done && n + 2 <= cap && !g_fv.search[0]) {
            rows[n].kind = 0; rows[n].name = "USER"; n++;
            rows[n].kind = 2; rows[n].name = "No user fonts installed"; n++;
        }
    }
    return n;
}

static float row_height(int kind) { return kind == 0 ? 26.0f : ROW_H; }

/* ============ Drawing: sidebar ============ */

static void draw_sidebar(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxframe_rail(c, L->sidebar);
    nxglass_field(c, L->search, g_fv.search, "Search fonts", 1,
                  g_fv.focus == FOCUS_SEARCH ? NXGLASS_FOCUSED : 0);

    fv_row_t rows[NXFONT_MAX_FONTS + 4];
    uint32_t n = sidebar_rows(rows, NXFONT_MAX_FONTS + 4);
    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)L->list.x, (int32_t)L->list.y, (int32_t)L->list.w, (int32_t)L->list.h));
    float y = L->list.y;
    for (uint32_t i = 0; i < n; i++) {
        float rh = row_height(rows[i].kind);
        if (y > L->list.y + L->list.h) break;
        if (rows[i].kind == 0) {
            nxframe_caps(c, L->list.x + 8, y + 18, rows[i].name, th->text_tertiary);
        } else if (rows[i].kind == 2) {
            nxui_text(c, NXUI_TEXT_FOOTNOTE, L->list.x + 8, y + 19, rows[i].name, th->text_tertiary, 0);
        } else {
            nxc_rect_t r = nxc_rect(L->list.x, y, L->list.w, rh - 2);
            int sel = fv_eq_ci(rows[i].name, g_fv.family);
            int hov = nxglass_hit(r, g_fv.mx, g_fv.my);
            uint32_t ids[MAX_STYLES];
            uint32_t ns = family_styles(rows[i].name, ids);
            int enabled = family_enabled_count(rows[i].name);
            char detail[16];
            detail[0] = 0;
            if (enabled == 0) fv_copy(detail, sizeof(detail), "Off");
            else fv_cat_u32(detail, sizeof(detail), ns);
            nxframe_rail_item(c, r, -1, 0, detail,
                              (sel ? NXFRAME_SELECTED : 0u) | (hov ? NXFRAME_HOVER : 0u));
            /* "Aa" in the family's own regular face. */
            const nxc_font_t *pf = nxfont_lookup(rows[i].name, 400, 0);
            if (!pf && ns > 0) pf = nxfont_face(ids[0]);
            nxc_color_t tc = enabled ? th->text_primary : th->text_tertiary;
            float base = (float)(int32_t)(r.y + r.h * 0.5f + 5.0f);
            if (pf) {
                nxc_text_opts_t o = fv_opts(NXC_ALIGN_CENTER, 0);
                nxc_draw_text(c, pf, 15.0f, r.x + 20, base, "Aa", -1,
                              sel ? th->text_primary : th->text_secondary, &o);
            }
            nxc_text_opts_t o = fv_opts(NXC_ALIGN_LEFT, r.w - 40 - 34);
            nxc_draw_text(c, nxui_font(NXUI_FONT_REGULAR), 13.0f, r.x + 40, base, rows[i].name, -1, tc, &o);
        }
        y += rh;
    }
    nxc_restore(c);
    int hov = nxglass_hit(L->install_btn, g_fv.mx, g_fv.my);
    nxglass_button(c, L->install_btn, "Install Font\xE2\x80\xA6", NXGLASS_BUTTON_DEFAULT,
                   hov ? NXGLASS_HOVER : 0);
}

/* ============ Drawing: header ============ */

static float draw_chip(nxc_canvas_t *c, float x, float y, const char *label, int selected, int hovered,
                       int dim) {
    const nxui_theme_t *th = nxui_theme();
    float w = nxui_text_width(NXUI_TEXT_SUBHEADLINE, label) + 24.0f;
    nxc_rect_t r = nxc_rect(x, y, w, CHIP_H);
    nxc_color_t fill = selected ? th->accent : (hovered ? th->control_fill_hover : th->control_fill);
    nxc_fill_rrect(c, r, CHIP_H * 0.5f, fill);
    if (!selected) nxc_stroke_rrect(c, r, CHIP_H * 0.5f, 1.0f, th->separator);
    nxc_color_t tc = selected ? th->text_on_accent : (dim ? th->text_tertiary : th->text_primary);
    nxui_text_in_rect(c, NXUI_TEXT_SUBHEADLINE, r, label, tc, NXC_ALIGN_CENTER);
    return w;
}

static void draw_header(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    float x = SIDEBAR_W + PAD;
    const nxfont_entry_t *e = sel_entry();
    nxc_text_opts_t o = fv_opts(NXC_ALIGN_LEFT, L->tabs.x - x - 12);
    nxui_text(c, NXUI_TEXT_TITLE1, x, 40, g_fv.family[0] ? g_fv.family : "Fonts", th->text_primary, &o);

    uint32_t ids[MAX_STYLES];
    uint32_t n = family_styles(g_fv.family, ids);
    char sub[96];
    sub[0] = 0;
    fv_cat_u32(sub, sizeof(sub), n);
    fv_cat(sub, sizeof(sub), n == 1 ? " style" : " styles");
    if (e) {
        fv_cat(sub, sizeof(sub), e->source == NXFONT_SOURCE_SYSTEM ? "  \xC2\xB7  System font" : "  \xC2\xB7  User font");
        if (e->info.monospace) fv_cat(sub, sizeof(sub), "  \xC2\xB7  Monospaced");
    }
    nxui_text(c, NXUI_TEXT_FOOTNOTE, x, 58, sub, th->text_secondary, 0);

    static const char *const tabs[3] = { "Specimen", "Characters", "Info" };
    int hov_tab = nxglass_segment_at(L->tabs, 3, g_fv.mx, g_fv.my);
    nxframe_tabs(c, L->tabs, tabs, 3, (int)g_fv.tab, hov_tab);

    /* Style chips */
    float cx = x;
    float max_x = L->toggle_btn.x - 10;
    for (uint32_t i = 0; i < n; i++) {
        const nxfont_entry_t *se = nxfont_get(ids[i]);
        if (!se) continue;
        float w = nxui_text_width(NXUI_TEXT_SUBHEADLINE, se->style) + 24.0f;
        if (cx + w > max_x) break;
        nxc_rect_t r = nxc_rect(cx, L->chips_y + 1, w, CHIP_H);
        cx += draw_chip(c, cx, L->chips_y + 1, se->style, ids[i] == g_fv.style_id,
                        nxglass_hit(r, g_fv.mx, g_fv.my), !se->enabled) + 8.0f;
    }

    /* Enable / Disable (user fonts) or a lock badge (system fonts). */
    if (e && e->locked) {
        float bw = nxui_text_width(NXUI_TEXT_CAPTION, "Built in") + 14.0f;
        nxglass_badge(c, L->toggle_btn.x + L->toggle_btn.w - bw, L->toggle_btn.y + 5, "Built in",
                      th->text_secondary);
    } else if (e) {
        int hov = nxglass_hit(L->toggle_btn, g_fv.mx, g_fv.my);
        nxglass_button(c, L->toggle_btn, e->enabled ? "Disable" : "Enable",
                       e->enabled ? NXGLASS_BUTTON_DEFAULT : NXGLASS_BUTTON_PRIMARY, hov ? NXGLASS_HOVER : 0);
    }
    nxglass_separator(c, x, L->body.y - 12, L->body.w);
}

/* ============ Drawing: specimen ============ */

static void draw_specimen(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    const nxc_font_t *f = sel_face();
    nxc_rect_t card = L->sample_card;
    nxframe_panel(c, card);
    if (g_fv.focus == FOCUS_SAMPLE)
        nxc_stroke_rrect(c, card, 12.0f, 1.5f, NXC_WITH_ALPHA(th->accent, 200));
    const char *txt = g_fv.sample[0] ? g_fv.sample : (g_fv.focus == FOCUS_SAMPLE ? "" : k_default_sample);
    nxc_font_metrics_t m;
    float px = g_fv.size_px;
    if (nxc_font_metrics(f, px, &m) != NXC_OK) { m.ascent = px * 0.8f; m.cap_height = px * 0.66f; m.descent = px * 0.2f; }
    float base = card.y + card.h * 0.5f + m.cap_height * 0.5f;
    if (base - m.ascent < card.y + 4) base = card.y + 4 + m.ascent;
    nxc_save(c);
    nxc_clip_rrect(c, card, 12.0f);
    nxc_text_opts_t o = fv_opts(NXC_ALIGN_LEFT, 0);
    float inner_w = card.w - 36;
    int one_line = nxc_text_width(f, px, txt, -1, &o) <= inner_w;
    if (one_line) {
        nxc_draw_text(c, f, px, card.x + 18, base, txt, -1, th->text_primary, &o);
    } else {
        /* Wrap long samples; the block is centered when it fits. */
        float lh = (float)(int32_t)(px * 1.12f + 0.5f);
        nxc_rect_t tr = nxc_rect(card.x + 18, card.y + 6, inner_w, card.h - 12);
        float used = nxui_text_wrapped_font(0, f, px, tr, txt, th->text_primary, lh);
        if (used < tr.h) tr.y += (float)(int32_t)((tr.h - used) * 0.5f);
        nxui_text_wrapped_font(c, f, px, tr, txt, th->text_primary, lh);
    }
    if (one_line && g_fv.focus == FOCUS_SAMPLE && ((int)(nxui_time() * 2.0f) & 1) == 0) {
        float cxp = card.x + 18 + nxc_text_width(f, px, txt, -1, &o) + 2;
        if (cxp < card.x + card.w - 6)
            nxc_fill_rect(c, nxc_rect(cxp, base - m.ascent * 0.9f, 2.0f, m.ascent * 0.9f + m.descent * 0.6f), th->accent);
    }
    nxc_restore(c);
    if (!g_fv.sample[0] && g_fv.focus != FOCUS_SAMPLE)
        nxui_text(c, NXUI_TEXT_CAPTION, card.x + card.w - 12, card.y + card.h - 10, "Click to type",
                  th->text_tertiary, &(nxc_text_opts_t){ 0.0f, NXC_ALIGN_RIGHT, 0.0f, 0 });

    /* Size slider */
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(card.x, L->slider.y - 2, 40, 24), "Size", th->text_secondary,
                      NXC_ALIGN_LEFT);
    float v = (g_fv.size_px - MIN_PX) / (MAX_PX - MIN_PX);
    nxglass_slider(c, L->slider, v, g_fv.dragging_slider ? NXGLASS_PRESSED : 0);
    char sz[16];
    sz[0] = 0;
    fv_cat_u32(sz, sizeof(sz), (uint32_t)(g_fv.size_px + 0.5f));
    fv_cat(sz, sizeof(sz), " px");
    nxui_text_in_rect(c, NXUI_TEXT_MONO, L->size_label, sz, th->text_primary, NXC_ALIGN_RIGHT);

    /* Waterfall */
    static const float sizes[] = { 12.0f, 16.0f, 22.0f, 30.0f };
    float y = L->slider.y + 40;
    float bottom = L->body.y + L->body.h;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        nxc_font_metrics_t wm;
        if (nxc_font_metrics(f, sizes[i], &wm) != NXC_OK) break;
        float lh = wm.ascent + wm.descent + 6.0f;
        if (y + lh > bottom - 30) break;
        float bl = (float)(int32_t)(y + wm.ascent + 0.5f);
        char lab[8];
        lab[0] = 0;
        fv_cat_u32(lab, sizeof(lab), (uint32_t)sizes[i]);
        nxui_text(c, NXUI_TEXT_MONO_SMALL, card.x, bl, lab, th->text_tertiary, 0);
        nxc_text_opts_t wo = fv_opts(NXC_ALIGN_LEFT, card.w - 34);
        nxc_draw_text(c, f, sizes[i], card.x + 34, bl, k_pangram, -1, th->text_primary, &wo);
        y += lh;
    }
    /* Alphabet and figures */
    if (y + 40 < bottom) {
        nxglass_separator(c, card.x, y + 4, card.w);
        nxc_text_opts_t ao = fv_opts(NXC_ALIGN_LEFT, card.w);
        nxc_draw_text(c, f, 15.0f, card.x, y + 26, "ABCDEFGHIJKLMNOPQRSTUVWXYZ  abcdefghijklmnopqrstuvwxyz", -1,
                      th->text_secondary, &ao);
        if (y + 48 < bottom)
            nxc_draw_text(c, f, 15.0f, card.x, y + 47,
                          "0123456789  !?&@#%*()[]{}  \xC3\x80\xC3\x89\xC3\x8E\xC3\x95\xC3\x9C \xC3\x9F \xC3\xA6\xC3\xB8 \xE2\x82\xAC\xC2\xA3\xC2\xA5",
                          -1, th->text_secondary, &ao);
    }
}

/* ============ Drawing: character map ============ */

static void draw_chars(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    const nxc_font_t *f = sel_face();
    ensure_chars();
    nxframe_panel(c, nxc_rect(L->grid.x - 1, L->grid.y - 1, L->grid.w + 2, L->grid.h + 2));
    uint32_t first = g_fv.page * GRID_PAGE;
    nxc_font_metrics_t m;
    if (nxc_font_metrics(f, 22.0f, &m) != NXC_OK) m.cap_height = 15.0f;
    for (int i = 0; i < GRID_PAGE; i++) {
        uint32_t idx = first + (uint32_t)i;
        float x = L->grid.x + (float)(i % GRID_COLS) * CELL;
        float y = L->grid.y + (float)(i / GRID_COLS) * CELL;
        nxc_rect_t cell = nxc_rect(x, y, CELL, CELL);
        if (i % GRID_COLS) nxc_fill_rect(c, nxc_rect(x, y + 6, 1, CELL - 12), th->separator);
        if (i >= GRID_COLS) nxc_fill_rect(c, nxc_rect(x + 6, y, CELL - 12, 1), th->separator);
        if (idx >= g_fv.nchars) continue;
        int sel = idx == g_fv.sel_char;
        int hov = nxglass_hit(cell, g_fv.mx, g_fv.my);
        if (sel) nxc_fill_rrect(c, nxc_rect(x + 3, y + 3, CELL - 6, CELL - 6), 8.0f, th->accent);
        else if (hov) nxc_fill_rrect(c, nxc_rect(x + 3, y + 3, CELL - 6, CELL - 6), 8.0f, th->control_fill_hover);
        char u[5];
        fv_utf8(g_fv.chars[idx], u);
        nxc_text_opts_t o = fv_opts(NXC_ALIGN_CENTER, 0);
        float base = (float)(int32_t)(y + CELL * 0.5f + m.cap_height * 0.5f + 0.5f);
        nxc_draw_text(c, f, 22.0f, x + CELL * 0.5f, base, u, -1, sel ? th->text_on_accent : th->text_primary, &o);
    }

    /* Pager */
    uint32_t pages = g_fv.nchars ? (g_fv.nchars + GRID_PAGE - 1) / GRID_PAGE : 1;
    nxglass_button(c, L->prev_btn, "\xE2\x80\xB9", NXGLASS_BUTTON_DEFAULT,
                   g_fv.page == 0 ? NXGLASS_DISABLED : (nxglass_hit(L->prev_btn, g_fv.mx, g_fv.my) ? NXGLASS_HOVER : 0));
    nxglass_button(c, L->next_btn, "\xE2\x80\xBA", NXGLASS_BUTTON_DEFAULT,
                   g_fv.page + 1 >= pages ? NXGLASS_DISABLED : (nxglass_hit(L->next_btn, g_fv.mx, g_fv.my) ? NXGLASS_HOVER : 0));
    char pg[64];
    pg[0] = 0;
    uint32_t last = first + GRID_PAGE < g_fv.nchars ? first + GRID_PAGE : g_fv.nchars;
    fv_cat_u32(pg, sizeof(pg), g_fv.nchars ? first + 1 : 0);
    fv_cat(pg, sizeof(pg), "\xE2\x80\x93");
    fv_cat_u32(pg, sizeof(pg), last);
    fv_cat(pg, sizeof(pg), " of ");
    fv_cat_u32(pg, sizeof(pg), g_fv.nchars);
    fv_cat(pg, sizeof(pg), g_fv.chars_truncated ? "+ characters" : " characters");
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE,
                      nxc_rect(L->prev_btn.x + 34, L->prev_btn.y, L->grid.w - 68, L->prev_btn.h), pg,
                      th->text_secondary, NXC_ALIGN_CENTER);

    /* Detail: big glyph on metric lines, code point. */
    nxc_rect_t d = L->detail;
    nxframe_panel(c, d);
    if (g_fv.nchars == 0) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, d, "No characters", th->text_tertiary, NXC_ALIGN_CENTER);
        return;
    }
    uint32_t cp = g_fv.chars[g_fv.sel_char < g_fv.nchars ? g_fv.sel_char : 0];
    float px = 96.0f;
    nxc_font_metrics_t bm;
    if (nxc_font_metrics(f, px, &bm) != NXC_OK) return;
    float base = (float)(int32_t)(d.y + 26 + bm.ascent);
    nxc_color_t guide = NXC_WITH_ALPHA(th->accent, 70);
    float gx = d.x + 12, gw = d.w - 24;
    nxc_fill_rect(c, nxc_rect(gx, base, gw, 1), NXC_WITH_ALPHA(th->accent, 140));
    nxc_fill_rect(c, nxc_rect(gx, (float)(int32_t)(base - bm.x_height), gw, 1), guide);
    nxc_fill_rect(c, nxc_rect(gx, (float)(int32_t)(base - bm.cap_height), gw, 1), guide);
    nxc_fill_rect(c, nxc_rect(gx, (float)(int32_t)(base + bm.descent), gw, 1), guide);
    char u[5];
    fv_utf8(cp, u);
    nxc_text_opts_t o = fv_opts(NXC_ALIGN_CENTER, 0);
    nxc_save(c);
    nxc_clip_rrect(c, d, 12.0f);
    nxc_draw_text(c, f, px, d.x + d.w * 0.5f, base, u, -1, th->text_primary, &o);
    nxc_restore(c);
    nxui_text(c, NXUI_TEXT_CAPTION, gx + gw, base - 3, "baseline", th->text_tertiary,
              &(nxc_text_opts_t){ 0.0f, NXC_ALIGN_RIGHT, 0.0f, 0 });
    char line[64];
    line[0] = 0;
    fv_cat(line, sizeof(line), "U+");
    fv_cat_hex(line, sizeof(line), cp, 4);
    float ty = base + bm.descent + 30;
    nxui_text(c, NXUI_TEXT_TITLE3, d.x + 16, ty, line, th->text_primary, 0);
    line[0] = 0;
    fv_cat(line, sizeof(line), "Decimal ");
    fv_cat_u32(line, sizeof(line), cp);
    nxui_text(c, NXUI_TEXT_MONO_SMALL, d.x + 16, ty + 20, line, th->text_secondary, 0);
    line[0] = 0;
    fv_cat(line, sizeof(line), "UTF-8   ");
    for (int i = 0; u[i]; i++) {
        fv_cat_hex(line, sizeof(line), (uint8_t)u[i], 2);
        fv_cat(line, sizeof(line), " ");
    }
    nxui_text(c, NXUI_TEXT_MONO_SMALL, d.x + 16, ty + 38, line, th->text_secondary, 0);

    /* The character at text sizes, on a hairline. */
    float sy = ty + 62;
    if (sy + 40 < d.y + d.h) {
        nxglass_separator(c, d.x + 12, sy, d.w - 24);
        static const float ks[] = { 13.0f, 20.0f, 32.0f };
        float sx = d.x + 16;
        float sb = (float)(int32_t)(sy + 46);
        for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++) {
            nxc_text_opts_t so = fv_opts(NXC_ALIGN_LEFT, 0);
            nxc_draw_text(c, f, ks[i], sx, sb, u, -1, th->text_primary, &so);
            sx += nxc_text_width(f, ks[i], u, -1, &so) + 14.0f;
            if (sx > d.x + d.w - 20) break;
        }
    }
}

/* ============ Drawing: info ============ */

static void fmt_size(char *out, uint32_t cap, uint32_t bytes) {
    out[0] = 0;
    if (bytes >= 1024u * 1024u) {
        uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10u + 524288u) / 1048576u);
        fv_cat_u32(out, cap, tenths / 10u);
        fv_cat(out, cap, ".");
        fv_cat_u32(out, cap, tenths % 10u);
        fv_cat(out, cap, " MB");
    } else {
        fv_cat_u32(out, cap, (bytes + 512u) / 1024u);
        fv_cat(out, cap, " KB");
    }
}

static const char *weight_name(uint32_t w) {
    if (w <= 150) return "Thin";
    if (w <= 250) return "Extra Light";
    if (w <= 350) return "Light";
    if (w <= 450) return "Regular";
    if (w <= 550) return "Medium";
    if (w <= 650) return "Semibold";
    if (w <= 750) return "Bold";
    if (w <= 850) return "Extra Bold";
    return "Black";
}

static void info_row(nxc_canvas_t *c, float x, float y, float kw, float vw, const char *k, const char *v) {
    const nxui_theme_t *th = nxui_theme();
    nxui_text(c, NXUI_TEXT_FOOTNOTE, x + kw, y, k, th->text_secondary,
              &(nxc_text_opts_t){ 0.0f, NXC_ALIGN_RIGHT, 0.0f, 0 });
    nxc_text_opts_t o = fv_opts(NXC_ALIGN_LEFT, vw);
    nxui_text(c, NXUI_TEXT_BODY, x + kw + 12, y, v && v[0] ? v : "\xE2\x80\x94", th->text_primary, &o);
}

static void draw_info(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    const nxfont_entry_t *e = sel_entry();
    if (!e) return;
    const nxc_font_info_t *in = &e->info;
    float x = L->body.x, w = L->body.w;
    float colw = (w - 16) * 0.5f;
    nxc_rect_t card = nxc_rect(x, L->body.y, w, 162);
    nxframe_panel(c, card);
    char buf[96];
    float kw = 74, vw = colw - kw - 24;
    float y = card.y + 26, dy = 22;
    float x1 = x + 8, x2 = x + 8 + colw + 8;
    info_row(c, x1, y, kw, vw, "Full name", in->full_name);
    info_row(c, x1, y + dy, kw, vw, "Family", in->family);
    info_row(c, x1, y + 2 * dy, kw, vw, "Style", in->style);
    buf[0] = 0;
    fv_cat_u32(buf, sizeof(buf), in->weight);
    fv_cat(buf, sizeof(buf), "  \xC2\xB7  ");
    fv_cat(buf, sizeof(buf), weight_name(in->weight));
    if (in->italic) fv_cat(buf, sizeof(buf), " Italic");
    info_row(c, x1, y + 3 * dy, kw, vw, "Weight", buf);
    /* "Version 3.052;hotconv ..." -> "3.052" */
    {
        const char *v = in->version;
        if (v[0] == 'V' && v[1] == 'e' && v[2] == 'r' && v[3] == 's' && v[4] == 'i' && v[5] == 'o' &&
            v[6] == 'n' && v[7] == ' ') v += 8;
        uint32_t k = 0;
        while (v[k] && v[k] != ';' && k + 1 < sizeof(buf)) { buf[k] = v[k]; k++; }
        buf[k] = 0;
        info_row(c, x1, y + 4 * dy, kw, vw, "Version", buf);
    }
    info_row(c, x1, y + 5 * dy, kw, vw, "Role", e->role);

    buf[0] = 0;
    fv_cat_u32(buf, sizeof(buf), in->num_glyphs);
    info_row(c, x2, y, kw, vw, "Glyphs", buf);
    buf[0] = 0;
    fv_cat_u32(buf, sizeof(buf), nxfont_char_count(e->id));
    info_row(c, x2, y + dy, kw, vw, "Characters", buf);
    info_row(c, x2, y + 2 * dy, kw, vw, "Kerning",
             in->kerning == 2 ? "GPOS pair kerning" : (in->kerning == 1 ? "kern table" : "None"));
    fmt_size(buf, sizeof(buf), in->file_size);
    fv_cat(buf, sizeof(buf), "  \xC2\xB7  TrueType");
    info_row(c, x2, y + 3 * dy, kw, vw, "File", buf);
    buf[0] = 0;
    fv_cat_u32(buf, sizeof(buf), in->units_per_em);
    fv_cat(buf, sizeof(buf), in->monospace ? "  \xC2\xB7  monospaced" : "  \xC2\xB7  proportional");
    info_row(c, x2, y + 4 * dy, kw, vw, "Units/em", buf);
    info_row(c, x2, y + 5 * dy, kw, vw, "Location", e->location);

    /* License (name 13) and copyright (name 0) */
    float ly = card.y + card.h + 14;
    nxui_text(c, NXUI_TEXT_SUBHEADLINE, x, ly + 10, "LICENSE", th->text_tertiary, 0);
    nxc_rect_t lr = nxc_rect(x, ly + 18, w, L->body.y + L->body.h - (ly + 18));
    const char *lic = in->license[0] ? in->license : "No license text in this font.";
    float used = nxui_text_wrapped(c, NXUI_TEXT_FOOTNOTE, nxc_rect(lr.x, lr.y, lr.w, lr.h - 22), lic,
                                   th->text_secondary, 16.0f);
    if (in->copyright[0] && used + 22 <= lr.h) {
        nxc_text_opts_t o = fv_opts(NXC_ALIGN_LEFT, w);
        nxui_text(c, NXUI_TEXT_CAPTION, x, lr.y + used + 14, in->copyright, th->text_tertiary, &o);
    }
}

/* ============ Drawing: install sheet ============ */

static void draw_sheet(nxc_canvas_t *c, const fv_layout_t *L) {
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, nxc_rect(0, 0, L->w, L->h), NXC_ARGB(90, 0, 0, 0));
    nxc_rect_t s = L->sheet;
    nxui_elevation(c, s, 14.0f, 3);
    nxc_fill_rrect(c, s, 14.0f, th->window_bg);
    nxframe_panel(c, s);
    nxui_text(c, NXUI_TEXT_TITLE2, s.x + 20, s.y + 34, "Install a Font", th->text_primary, 0);
    nxui_text_wrapped(c, NXUI_TEXT_FOOTNOTE, nxc_rect(s.x + 20, s.y + 44, s.w - 40, 40),
                      "Enter the absolute path of a TrueType (.ttf) file. The file is checked "
                      "completely before anything is installed.",
                      th->text_secondary, 16.0f);
    nxglass_field(c, L->sheet_field, g_fv.path, "/Users/me/Fonts/Example.ttf", 0,
                  g_fv.focus == FOCUS_PATH ? NXGLASS_FOCUSED : 0);
    if (g_fv.message[0] && g_fv.message_ok >= 0) {
        nxc_color_t col = g_fv.message_ok ? th->success : th->destructive;
        nxc_rect_t mr = nxc_rect(s.x + 20, L->sheet_field.y + 40, s.w - 40, 44);
        nxsym_draw(c, g_fv.message_ok ? NXSYM_CHECK : NXSYM_WARNING, mr.x + 8, mr.y + 9, 14.0f, col, 0.0f);
        nxui_text_wrapped(c, NXUI_TEXT_FOOTNOTE, nxc_rect(mr.x + 22, mr.y, mr.w - 22, mr.h), g_fv.message,
                          col, 16.0f);
    }
    int has_path = g_fv.path[0] != 0;
    nxglass_button(c, L->sheet_cancel, "Cancel", NXGLASS_BUTTON_DEFAULT,
                   nxglass_hit(L->sheet_cancel, g_fv.mx, g_fv.my) ? NXGLASS_HOVER : 0);
    nxglass_button(c, L->sheet_validate, "Validate", NXGLASS_BUTTON_DEFAULT,
                   !has_path ? NXGLASS_DISABLED : (nxglass_hit(L->sheet_validate, g_fv.mx, g_fv.my) ? NXGLASS_HOVER : 0));
    nxglass_button(c, L->sheet_install, "Install", NXGLASS_BUTTON_PRIMARY,
                   !has_path ? NXGLASS_DISABLED : (nxglass_hit(L->sheet_install, g_fv.mx, g_fv.my) ? NXGLASS_HOVER : 0));
}

/* ============ Actions ============ */

static void do_validate(void) {
    nxc_font_info_t info;
    char reason[NXFONT_REASON_MAX];
    fv_log("validate: ", g_fv.path);
    if (nxfont_validate_file(g_fv.path, &info, reason, sizeof(reason)) == NXC_OK) {
        g_fv.message[0] = 0;
        fv_cat(g_fv.message, sizeof(g_fv.message), "Valid font: ");
        fv_cat(g_fv.message, sizeof(g_fv.message), info.full_name[0] ? info.full_name : info.family);
        fv_cat(g_fv.message, sizeof(g_fv.message), "  \xC2\xB7  ");
        fv_cat_u32(g_fv.message, sizeof(g_fv.message), info.num_glyphs);
        fv_cat(g_fv.message, sizeof(g_fv.message), " glyphs");
        g_fv.message_ok = 1;
    } else {
        fv_copy(g_fv.message, sizeof(g_fv.message), reason);
        g_fv.message_ok = 0;
    }
}

static void do_install(void) {
    uint32_t id = 0;
    char msg[NXFONT_REASON_MAX];
    nxc_status_t st = nxfont_install(g_fv.path, &id, msg, sizeof(msg));
    fv_copy(g_fv.message, sizeof(g_fv.message), msg);
    g_fv.message_ok = st == NXC_OK;
    if (st == NXC_OK) {
        const nxfont_entry_t *e = nxfont_get(id);
        if (e) {
            fv_copy(g_fv.family, sizeof(g_fv.family), e->family);
            g_fv.style_id = id;
        }
        fv_copy(g_fv.status, sizeof(g_fv.status), msg);
        g_fv.sheet = 0;
        g_fv.focus = FOCUS_NONE;
        g_fv.tab = TAB_SPECIMEN;
    }
}

static void do_toggle(void) {
    const nxfont_entry_t *e = sel_entry();
    if (!e || e->locked) return;
    nxc_status_t st = nxfont_set_enabled(e->id, !e->enabled);
    if (st != NXC_OK) fv_log("enable/disable failed: ", nxc_status_str(st));
}

/* ============ App view callbacks ============ */

int fonts_view_open(uint32_t win_id) {
    (void)win_id;
    if (!g_fv.open) {
        g_fv.size_px = 44.0f;
        g_fv.tab = TAB_SPECIMEN;
        g_fv.message_ok = -1;
        g_fv.open = 1;
    }
    if (nxfont_count() == 0) {
        fv_log("no fonts registered", 0);
        return 0;      /* still opens and shows an empty state */
    }
    uint32_t probe = 0;
    if (!g_fv.family[0] || nxfont_family_styles(g_fv.family, &probe, 1) == 0 || !sel_entry()) {
        uint32_t reg = nxui_font_registry_id(NXUI_FONT_REGULAR);
        const nxfont_entry_t *re = reg ? nxfont_get(reg) : 0;
        if (re) {
            fv_copy(g_fv.family, sizeof(g_fv.family), re->family);
            g_fv.style_id = reg;
        } else {
            g_fv.style_id = 0;
            select_family(nxfont_family_at(0));
        }
    }
    fv_log("opened", 0);
    return 0;
}

void fonts_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    nxc_canvas_t *c = nxui_canvas();
    if (!win || !c) return;
    nxc_rect_t content = app_view_content_rect(win);
    fv_layout_t L = layout_for(win);
    desktop_set_window_place(win->id, g_fv.family[0] ? g_fv.family : NULL);
    nxc_save(c);
    nxc_translate(c, content.x, content.y);
    draw_sidebar(c, &L);
    if (nxfont_count() == 0 || !sel_entry()) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY, L.main, "No fonts are registered", nxui_theme()->text_tertiary,
                          NXC_ALIGN_CENTER);
    } else {
        draw_header(c, &L);
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)L.body.x - 4, (int32_t)L.body.y - 4,
                                   (int32_t)L.body.w + 8, (int32_t)L.body.h + 8));
        if (g_fv.tab == TAB_SPECIMEN) draw_specimen(c, &L);
        else if (g_fv.tab == TAB_CHARS) draw_chars(c, &L);
        else draw_info(c, &L);
        nxc_restore(c);
        if (g_fv.status[0] && !g_fv.sheet) {
            nxc_text_opts_t o = fv_opts(NXC_ALIGN_RIGHT, L.body.w);
            nxui_text(c, NXUI_TEXT_CAPTION, L.body.x + L.body.w, L.h - 8, g_fv.status,
                      nxui_theme()->text_secondary, &o);
        }
    }
    if (g_fv.sheet) draw_sheet(c, &L);
    nxc_restore(c);
}

/* Scancode set 1 to ASCII (0 = not printable). */
static char key_ascii(uint8_t sc, int shift) {
    static const char lo[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
    };
    static const char hi[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, 0, 0, ' '
    };
    if (sc >= sizeof(lo)) return 0;
    return shift ? hi[sc] : lo[sc];
}

static void edit_buffer(char *buf, uint32_t cap, uint8_t sc, char ch) {
    uint32_t n = fv_len(buf);
    if (sc == 0x0E) {                                   /* backspace: drop one UTF-8 char */
        while (n > 0) {
            n--;
            if (((uint8_t)buf[n] & 0xC0u) != 0x80u) break;
        }
        buf[n] = 0;
        return;
    }
    if (ch && n + 1 < cap) { buf[n] = ch; buf[n + 1] = 0; }
}

static void move_char_selection(int delta) {
    if (g_fv.nchars == 0) return;
    int64_t s = (int64_t)g_fv.sel_char + delta;
    if (s < 0) s = 0;
    if (s >= (int64_t)g_fv.nchars) s = (int64_t)g_fv.nchars - 1;
    g_fv.sel_char = (uint32_t)s;
    g_fv.page = g_fv.sel_char / GRID_PAGE;
}

void fonts_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)ctx;
    if (sc == 0x2A || sc == 0x36) { g_fv.shift = pressed ? 1 : 0; return; }
    if (!pressed) return;
    if (sc == 0x3A) { g_fv.caps ^= 1u; return; }
    int shift = g_fv.shift;
    char ch = key_ascii(sc, shift);
    if (ch >= 'a' && ch <= 'z' && g_fv.caps) ch = (char)(ch - 32);
    else if (ch >= 'A' && ch <= 'Z' && g_fv.caps) ch = (char)(ch + 32);

    if (sc == 0x01) {                                   /* Esc */
        if (g_fv.sheet) { g_fv.sheet = 0; g_fv.focus = FOCUS_NONE; }
        else g_fv.focus = FOCUS_NONE;
        return;
    }
    switch (g_fv.focus) {
        case FOCUS_SEARCH:
            if (sc == 0x1C) { g_fv.focus = FOCUS_NONE; return; }
            edit_buffer(g_fv.search, sizeof(g_fv.search), sc, ch);
            g_fv.list_first = 0;
            {
                const char *fams[NXFONT_MAX_FONTS];
                if (visible_families(fams, NXFONT_MAX_FONTS) > 0) select_family(fams[0]);
            }
            return;
        case FOCUS_SAMPLE:
            if (sc == 0x1C) { g_fv.focus = FOCUS_NONE; return; }
            edit_buffer(g_fv.sample, sizeof(g_fv.sample), sc, ch);
            return;
        case FOCUS_PATH:
            if (sc == 0x1C) { if (g_fv.path[0]) do_install(); return; }
            edit_buffer(g_fv.path, sizeof(g_fv.path), sc, ch);
            g_fv.message_ok = -1;
            return;
        default:
            break;
    }
    /* No text focus: navigation keys. */
    if (g_fv.tab == TAB_CHARS) {
        ensure_chars();
        if (sc == 0x4B) move_char_selection(-1);
        else if (sc == 0x4D) move_char_selection(1);
        else if (sc == 0x48) move_char_selection(-GRID_COLS);
        else if (sc == 0x50) move_char_selection(GRID_COLS);
        else if (sc == 0x49) move_char_selection(-GRID_PAGE);
        else if (sc == 0x51) move_char_selection(GRID_PAGE);
    }
    if (sc == 0x0F) g_fv.tab = (fv_tab_t)(((int)g_fv.tab + 1) % 3);   /* Tab cycles views */
}

static void click_sidebar(const fv_layout_t *L, int32_t x, int32_t y) {
    if (nxglass_hit(L->search, x, y)) { g_fv.focus = FOCUS_SEARCH; return; }
    if (nxglass_hit(L->install_btn, x, y)) {
        g_fv.sheet = 1;
        g_fv.focus = FOCUS_PATH;
        g_fv.message[0] = 0;
        g_fv.message_ok = -1;
        return;
    }
    if (!nxglass_hit(L->list, x, y)) return;
    fv_row_t rows[NXFONT_MAX_FONTS + 4];
    uint32_t n = sidebar_rows(rows, NXFONT_MAX_FONTS + 4);
    float ry = L->list.y;
    for (uint32_t i = 0; i < n; i++) {
        float rh = row_height(rows[i].kind);
        if ((float)y >= ry && (float)y < ry + rh) {
            if (rows[i].kind == 1) {
                select_family(rows[i].name);
                fv_log("family selected: ", rows[i].name);
            }
            return;
        }
        ry += rh;
    }
}

static void click_main(const fv_layout_t *L, int32_t x, int32_t y) {
    int seg = nxglass_segment_at(L->tabs, 3, x, y);
    if (seg >= 0) { g_fv.tab = (fv_tab_t)seg; return; }
    const nxfont_entry_t *e = sel_entry();
    if (e && !e->locked && nxglass_hit(L->toggle_btn, x, y)) { do_toggle(); return; }
    /* Style chips */
    uint32_t ids[MAX_STYLES];
    uint32_t n = family_styles(g_fv.family, ids);
    float cx = SIDEBAR_W + PAD;
    for (uint32_t i = 0; i < n; i++) {
        const nxfont_entry_t *se = nxfont_get(ids[i]);
        if (!se) continue;
        float w = nxui_text_width(NXUI_TEXT_SUBHEADLINE, se->style) + 24.0f;
        if (nxglass_hit(nxc_rect(cx, L->chips_y + 1, w, CHIP_H), x, y)) {
            g_fv.style_id = ids[i];
            return;
        }
        cx += w + 8.0f;
    }
    if (g_fv.tab == TAB_SPECIMEN) {
        if (nxglass_hit(L->sample_card, x, y)) { g_fv.focus = FOCUS_SAMPLE; return; }
        nxc_rect_t grab = nxc_rect(L->slider.x - 10, L->slider.y - 6, L->slider.w + 20, L->slider.h + 12);
        if (nxglass_hit(grab, x, y)) {
            g_fv.dragging_slider = 1;
            g_fv.size_px = MIN_PX + nxglass_slider_value_at(L->slider, x) * (MAX_PX - MIN_PX);
            return;
        }
    } else if (g_fv.tab == TAB_CHARS) {
        ensure_chars();
        uint32_t pages = g_fv.nchars ? (g_fv.nchars + GRID_PAGE - 1) / GRID_PAGE : 1;
        if (nxglass_hit(L->prev_btn, x, y) && g_fv.page > 0) {
            g_fv.page--;
            g_fv.sel_char = g_fv.page * GRID_PAGE;
            return;
        }
        if (nxglass_hit(L->next_btn, x, y) && g_fv.page + 1 < pages) {
            g_fv.page++;
            g_fv.sel_char = g_fv.page * GRID_PAGE;
            return;
        }
        if (nxglass_hit(L->grid, x, y)) {
            int col = (int)(((float)x - L->grid.x) / CELL), row = (int)(((float)y - L->grid.y) / CELL);
            if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS) {
                uint32_t idx = g_fv.page * GRID_PAGE + (uint32_t)(row * GRID_COLS + col);
                if (idx < g_fv.nchars) g_fv.sel_char = idx;
            }
            return;
        }
    }
    g_fv.focus = FOCUS_NONE;
}

static void click_sheet(const fv_layout_t *L, int32_t x, int32_t y) {
    if (nxglass_hit(L->sheet_field, x, y)) { g_fv.focus = FOCUS_PATH; return; }
    if (nxglass_hit(L->sheet_cancel, x, y)) { g_fv.sheet = 0; g_fv.focus = FOCUS_NONE; return; }
    if (g_fv.path[0] && nxglass_hit(L->sheet_validate, x, y)) { do_validate(); return; }
    if (g_fv.path[0] && nxglass_hit(L->sheet_install, x, y)) { do_install(); return; }
}

void fonts_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win) return;
    g_fv.mx = lx;
    g_fv.my = ly;
    fv_layout_t L = layout_for(win);
    int down = (b & 1u) && !(pb & 1u);
    int up = !(b & 1u) && (pb & 1u);
    if (g_fv.dragging_slider) {
        g_fv.size_px = MIN_PX + nxglass_slider_value_at(L.slider, lx) * (MAX_PX - MIN_PX);
        if (up || !(b & 1u)) g_fv.dragging_slider = 0;
        return;
    }
    if (!down) return;
    if (g_fv.sheet) { click_sheet(&L, lx, ly); return; }
    if ((float)lx < SIDEBAR_W) click_sidebar(&L, lx, ly);
    else click_main(&L, lx, ly);
}
