/*
 * NeolyxOS - NXFrame: the Neolyx app layout kit (see nxframe.h)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxframe.h"
#include "nxicons.h"

/* ============ Helpers ============ */

/* A rect is drawable when its size is positive, finite and sane. */
static int rect_ok(nxc_rect_t r) {
    return r.w > 0.0f && r.h > 0.0f && r.w < 16384.0f && r.h < 16384.0f &&
           r.x > -16384.0f && r.x < 16384.0f && r.y > -16384.0f && r.y < 16384.0f;
}

static int is_light(void) { return nxui_appearance() == NXUI_APPEARANCE_LIGHT; }

#define UPPER_MAX 64

/* ASCII upper-case copy (labels are short UI strings; longer ones are cut). */
static void upper_copy(char *dst, const char *src) {
    int i = 0;
    if (src)
        for (; src[i] && i < UPPER_MAX - 1; i++) {
            char ch = src[i];
            dst[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
        }
    dst[i] = '\0';
}

/* Draw an uppercase, tracked caption at a baseline; returns its width. */
static float caption_caps(nxc_canvas_t *c, float x, float baseline, const char *text, nxc_color_t color) {
    char up[UPPER_MAX];
    upper_copy(up, text);
    const nxui_type_t ty = nxui_type(NXUI_TEXT_CAPTION);
    const nxc_font_t *f = nxui_font(NXUI_FONT_SEMIBOLD);
    if (!f) f = nxui_font(ty.font);
    if (!f) return 0.0f;
    nxc_text_opts_t o = { 0.9f, NXC_ALIGN_LEFT, 0.0f, 0 };
    float w = nxc_text_width(f, ty.size, up, -1, &o);
    if (c) nxc_draw_text(c, f, ty.size, x, baseline, up, -1, color, &o);
    return w;
}

/* ============ Colors ============ */

nxc_color_t nxframe_rail_tone(void) {
    const nxui_theme_t *th = nxui_theme();
    /* Dark: a step deeper than the body. Light: a cool step greyer. */
    return is_light() ? nxc_color_lerp(th->window_bg, NXC_RGB(0xDD, 0xE1, 0xEA), 0.45f)
                      : nxc_color_lerp(th->window_bg, NXC_RGB(0x05, 0x07, 0x0C), 0.38f);
}

/* ============ Window chrome ============ */

nxc_status_t nxframe_crest(nxc_canvas_t *c, nxc_rect_t r) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    /* Saffron fading to a warmer tone along the edge: one line, two stops. */
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x + r.w, r.y, th->accent,
                                     nxc_color_lerp(th->accent, NXC_RGB(0xE8, 0x4A, 0x5F), 0.55f));
    return nxc_fill_rect_paint(c, nxc_rect(r.x, r.y, r.w, NXFRAME_CREST_H), &p);
}

nxc_status_t nxframe_title(nxc_canvas_t *c, nxc_rect_t tb, const char *app,
                           const char *place, float right_reserve, int focused) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(tb) || !app) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    float x = tb.x + 12.0f;
    float limit = tb.x + tb.w - right_reserve - 8.0f;
    if (limit - x < 40.0f) return NXC_OK;

    /* App glyph */
    float gs = 14.0f;
    nxapp_icon_draw(c, nxapp_icon_from_name(app), x, tb.y + (tb.h - gs) * 0.5f, gs, focused ? 255 : 150);
    x += gs + 8.0f;

    nxc_color_t pc = focused ? th->text_primary : th->text_tertiary;
    nxc_color_t sc = focused ? th->text_secondary : th->text_tertiary;
    float aw = nxui_text_width(NXUI_TEXT_BODY_SEMIBOLD, app);
    if (aw > limit - x) aw = limit - x;
    nxui_text_in_rect(c, NXUI_TEXT_BODY_SEMIBOLD, nxc_rect(x, tb.y, aw, tb.h), app, pc, NXC_ALIGN_LEFT);
    x += aw;

    if (place && place[0] && limit - x > 30.0f) {
        float sw = nxui_text_width(NXUI_TEXT_BODY, "/");
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(x + 7.0f, tb.y, sw, tb.h), "/",
                          NXC_WITH_ALPHA(th->accent, focused ? 230 : 120), NXC_ALIGN_LEFT);
        x += sw + 14.0f;
        if (limit - x > 12.0f)
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(x, tb.y, limit - x, tb.h), place, sc, NXC_ALIGN_LEFT);
    }
    return NXC_OK;
}

/* ============ Rail ============ */

nxc_status_t nxframe_rail(nxc_canvas_t *c, nxc_rect_t r) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    nxc_status_t st = nxc_fill_rect(c, r, nxframe_rail_tone());
    if (st != NXC_OK) return st;
    return nxc_fill_rect(c, nxc_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), nxui_theme()->separator);
}

nxc_status_t nxframe_rail_group(nxc_canvas_t *c, float x, float y, float w, const char *title) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!title || !(w > 0.0f) || w > 16384.0f) return NXC_OK;
    caption_caps(c, x, y + NXFRAME_RAIL_GROUP_H - 9.0f, title, nxui_theme()->text_tertiary);
    return NXC_OK;
}

nxc_status_t nxframe_rail_item(nxc_canvas_t *c, nxc_rect_t r, int sym, const char *label,
                               const char *detail, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    int sel = (state & NXFRAME_SELECTED) != 0;
    int dis = (state & NXFRAME_DISABLED) != 0;
    int light = is_light();

    if (sel) {
        nxc_fill_rrect(c, r, 5.0f, NXC_WITH_ALPHA(th->accent, light ? 34 : 44));
        /* The accent bar: the selection mark of every Neolyx rail. */
        float bh = r.h - 12.0f;
        if (bh < 6.0f) bh = 6.0f;
        nxc_fill_rrect(c, nxc_rect(r.x, r.y + (r.h - bh) * 0.5f, 3.0f, bh), 1.5f, th->accent);
    } else if (state & NXFRAME_PRESSED) {
        nxc_fill_rrect(c, r, 5.0f, light ? NXC_ARGB(24, 0, 0, 0) : NXC_ARGB(30, 0xFF, 0xFF, 0xFF));
    } else if (state & NXFRAME_HOVER) {
        nxc_fill_rrect(c, r, 5.0f, light ? NXC_ARGB(14, 0, 0, 0) : NXC_ARGB(16, 0xFF, 0xFF, 0xFF));
    }

    nxc_color_t tc = dis ? th->text_tertiary : th->text_primary;
    nxc_color_t gc = dis ? th->text_tertiary : sel ? th->accent : th->text_secondary;
    float tx = r.x + 12.0f;
    if (sym >= 0 && sym < (int)NXSYM_COUNT) {
        nxsym_draw(c, (nxsym_t)sym, r.x + NXFRAME_GLYPH_CX, r.y + r.h * 0.5f, 15.0f, gc, 1.0f);
        tx = r.x + 38.0f;
    } else if (sym == NXFRAME_SYM_SLOT) {
        tx = r.x + 38.0f;
    }
    float dw = (detail && detail[0]) ? nxui_text_width(NXUI_TEXT_FOOTNOTE, detail) + 12.0f : 0.0f;
    if (dw > r.w * 0.4f) dw = r.w * 0.4f;
    if (label && r.x + r.w - tx - dw > 0.0f)
        nxui_text_in_rect(c, sel ? NXUI_TEXT_BODY_SEMIBOLD : NXUI_TEXT_BODY,
                          nxc_rect(tx, r.y, r.x + r.w - tx - dw, r.h), label, tc, NXC_ALIGN_LEFT);
    if (dw > 0.0f)
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + r.w - dw, r.y, dw - 8.0f, r.h), detail,
                          th->text_tertiary, NXC_ALIGN_RIGHT);
    return NXC_OK;
}

/* ============ Page ============ */

float nxframe_page_title(nxc_canvas_t *c, float x, float y, float w,
                         const char *title, const char *subtitle) {
    if (!title || !(w > 0.0f) || w > 16384.0f) return 0.0f;
    const nxui_theme_t *th = nxui_theme();
    float asc = nxui_text_ascent(NXUI_TEXT_TITLE1);
    float th_h = asc + nxui_text_descent(NXUI_TEXT_TITLE1);
    float h = th_h + 8.0f + 3.0f;   /* title, gap, tick */
    if (c) {
        nxui_text(c, NXUI_TEXT_TITLE1, x, y + asc, title, th->text_primary, NULL);
        nxc_fill_rrect(c, nxc_rect(x, y + th_h + 8.0f, 22.0f, 3.0f), 1.5f, th->accent);
    }
    if (subtitle && subtitle[0]) {
        nxc_rect_t sr = nxc_rect(x, y + h + 10.0f, w, 400.0f);
        float sh = nxui_text_wrapped(c, NXUI_TEXT_FOOTNOTE, sr, subtitle, th->text_secondary, 0.0f);
        h += 10.0f + sh;
    }
    return h;
}

nxc_status_t nxframe_section(nxc_canvas_t *c, float x, float y, float w, const char *title) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!title || !(w > 0.0f) || w > 16384.0f) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    float base = y + NXFRAME_SECTION_H - 12.0f;
    float tw = caption_caps(c, x, base, title, th->text_secondary);
    float rx = x + tw + 10.0f;
    if (x + w - rx > 4.0f)
        nxc_fill_rect(c, nxc_rect(rx, base - 4.0f, x + w - rx, 1.0f), th->separator);
    return NXC_OK;
}

void nxframe_rule(nxc_canvas_t *c, float x, float y, float w) {
    if (!c || !(w > 0.0f) || w > 16384.0f) return;
    const nxui_theme_t *th = nxui_theme();
    nxc_fill_rect(c, nxc_rect(x, y, w, 1.0f), NXC_WITH_ALPHA(th->separator, (th->separator >> 24) * 2 / 3));
}

nxc_status_t nxframe_ledger_row(nxc_canvas_t *c, nxc_rect_t r, const char *label, const char *value) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    float lw = r.w * NXFRAME_LABEL_COL;
    if (label)
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(r.x, r.y, lw - 12.0f, r.h), label,
                          th->text_secondary, NXC_ALIGN_LEFT);
    if (value)
        nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(r.x + lw, r.y, r.w - lw, r.h), value,
                          th->text_primary, NXC_ALIGN_LEFT);
    return NXC_OK;
}

nxc_status_t nxframe_row_wash(nxc_canvas_t *c, nxc_rect_t r, uint32_t state) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    int light = is_light();
    if (state & NXFRAME_PRESSED)
        return nxc_fill_rrect(c, r, 4.0f, light ? NXC_ARGB(20, 0, 0, 0) : NXC_ARGB(24, 0xFF, 0xFF, 0xFF));
    if (state & NXFRAME_HOVER)
        return nxc_fill_rrect(c, r, 4.0f, light ? NXC_ARGB(10, 0, 0, 0) : NXC_ARGB(12, 0xFF, 0xFF, 0xFF));
    return NXC_OK;
}

float nxframe_caps(nxc_canvas_t *c, float x, float baseline, const char *text, nxc_color_t color) {
    if (!text) return 0.0f;
    return caption_caps(c, x, baseline, text, color);
}

/* ============ Status strip ============ */

nxc_status_t nxframe_strip(nxc_canvas_t *c, nxc_rect_t r, const char *text) {
    if (!c) return NXC_ERR_INVALID_ARG;
    if (!rect_ok(r)) return NXC_OK;
    const nxui_theme_t *th = nxui_theme();
    nxc_status_t st = nxc_fill_rect(c, r, nxframe_rail_tone());
    if (st != NXC_OK) return st;
    nxc_fill_rect(c, nxc_rect(r.x, r.y, r.w, 1.0f), th->separator);
    if (text && r.w > 40.0f) {
        nxc_fill_circle(c, r.x + 16.0f, r.y + r.h * 0.5f, 3.0f, th->accent);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + 26.0f, r.y, r.w - 36.0f, r.h), text,
                          th->text_secondary, NXC_ALIGN_LEFT);
    }
    return NXC_OK;
}
