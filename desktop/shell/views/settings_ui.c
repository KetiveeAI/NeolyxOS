/*
 * NeolyxOS - System Settings: immediate-mode pane layout ("sui")
 *
 * See settings_internal.h. One build function per pane runs in a
 * MEASURE, DRAW or EVENT pass over the same deterministic layout. This
 * file owns scrolling of the detail area (drag-scroll, scrollbar drag,
 * keyboard), control interaction state and the small string helpers the
 * Settings files share (the desktop build is freestanding: no libc).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"

extern void serial_puts(const char *s);

/* ============ Logging ============ */

void settings_log(const char *msg) {
    if (!msg) return;
    serial_puts("[SETTINGS] ");
    serial_puts(msg);
    serial_puts("\n");
}

void settings_log2(const char *msg, const char *value) {
    if (!msg) return;
    serial_puts("[SETTINGS] ");
    serial_puts(msg);
    if (value) serial_puts(value);
    serial_puts("\n");
}

/* ============ String helpers ============ */

int sx_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n] && n < 0x7FFFFFFF) n++;
    return n;
}

void sx_strcpy(char *dst, const char *src, int cap) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

void sx_strcat(char *dst, const char *src, int cap) {
    if (!dst || !src || cap <= 0) return;
    int n = sx_strlen(dst);
    if (n >= cap) { dst[cap - 1] = '\0'; return; }
    sx_strcpy(dst + n, src, cap - n);
}

int sx_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char sx_lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

int sx_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && sx_lower(*a) == sx_lower(*b)) { a++; b++; }
    return sx_lower(*a) == sx_lower(*b);
}

int sx_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    if (!needle[0]) return 1;
    for (const char *h = hay; *h; h++) {
        const char *p = h, *q = needle;
        while (*p && *q && sx_lower(*p) == sx_lower(*q)) { p++; q++; }
        if (!*q) return 1;
    }
    return 0;
}

char *sx_itoa(int64_t v, char *dst, int cap) {
    if (!dst || cap <= 0) return dst;
    char tmp[24];
    int n = 0, neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u && n < 22);
    if (neg) tmp[n++] = '-';
    int i = 0;
    while (n > 0 && i < cap - 1) dst[i++] = tmp[--n];
    dst[i] = '\0';
    return dst;
}

void sx_join3(char *dst, int cap, const char *a, const char *b, const char *c) {
    if (!dst || cap <= 0) return;
    dst[0] = '\0';
    if (a) sx_strcat(dst, a, cap);
    if (b) sx_strcat(dst, b, cap);
    if (c) sx_strcat(dst, c, cap);
}

/* ============ Icon tiles ============ */

void settings_draw_tile(nxc_canvas_t *c, const settings_tile_t *t, float cx, float cy, float size) {
    if (!c || !t || size < 4.0f) return;
    nxc_rect_t r = nxc_rect(cx - size * 0.5f, cy - size * 0.5f, size, size);
    float rad = size * 0.25f;
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h,
                                     nxc_color_lerp(t->color, NXC_RGB(0xFF, 0xFF, 0xFF), 0.18f),
                                     t->color);
    nxc_fill_rrect_paint(c, r, rad, &p);
    nxc_stroke_rrect(c, r, rad, 0.75f, NXC_ARGB(40, 0, 0, 0));
    nxc_color_t white = NXC_RGB(0xFF, 0xFF, 0xFF);
    switch (t->kind) {
        case STILE_SYM:
            if (t->sym >= 0 && t->sym < (int)NXSYM_COUNT)
                nxsym_draw(c, (nxsym_t)t->sym, cx, cy, size * 0.62f, white, 1.0f);
            break;
        case STILE_TEXT:
            if (t->text)
                nxui_text_in_rect(c, size >= 30.0f ? NXUI_TEXT_TITLE3 : NXUI_TEXT_CAPTION,
                                  r, t->text, white, NXC_ALIGN_CENTER);
            break;
        case STILE_MOUSE: {
            float w = size * 0.34f, h = size * 0.56f;
            nxc_rect_t m = nxc_rect(cx - w * 0.5f, cy - h * 0.5f, w, h);
            float lw = size >= 30.0f ? 2.0f : 1.4f;
            nxc_stroke_rrect(c, m, w * 0.5f, lw, white);
            nxc_draw_line(c, cx, m.y + h * 0.14f, cx, m.y + h * 0.36f, lw, white);
            break;
        }
        case STILE_UPDATE: {
            float lw = size >= 30.0f ? 2.0f : 1.4f;
            nxc_stroke_circle(c, cx, cy, size * 0.30f, lw, white);
            nxc_draw_line(c, cx, cy - size * 0.15f, cx, cy + size * 0.13f, lw, white);
            nxc_draw_line(c, cx - size * 0.11f, cy + size * 0.02f, cx, cy + size * 0.13f, lw, white);
            nxc_draw_line(c, cx + size * 0.11f, cy + size * 0.02f, cx, cy + size * 0.13f, lw, white);
            break;
        }
        default:
            break;
    }
}

/* ============ Text wrapping ============ */

float sui_wrap_text(nxc_canvas_t *c, nxui_text_style_t style, float x, float y, float w,
                    const char *text, nxc_color_t color, int draw) {
    if (!text || !text[0] || w <= 10.0f) return 0.0f;
    float asc = nxui_text_ascent(style), desc = nxui_text_descent(style);
    float lh = asc + desc + 4.0f;
    char line[256];
    const int cap = (int)sizeof(line);
    float used = 0.0f;
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        /* Longest run of whole words starting at p that fits in w. */
        int best = 0, i = 0;
        for (;;) {
            int j = i;
            while (p[j] == ' ') j++;
            while (p[j] && p[j] != ' ') j++;
            if (j >= cap - 1) break;
            for (int k = 0; k < j; k++) line[k] = p[k];
            line[j] = '\0';
            if (best > 0 && nxui_text_width(style, line) > w) break;
            best = j;
            if (!p[j]) break;
            i = j;
        }
        if (best == 0) {              /* a single over-long word: hard cut */
            while (p[best] && best < cap - 1) best++;
        }
        for (int k = 0; k < best; k++) line[k] = p[k];
        line[best] = '\0';
        if (draw && c) nxui_text(c, style, x, y + used + asc, line, color, NULL);
        used += lh;
        p += best;
    }
    return used;
}

/* ============ sui state ============ */

#define SUI_PAD_X      28.0f
#define SUI_PAD_TOP    18.0f
#define SUI_PAD_BOTTOM 28.0f
#define SUI_MAX_W      640.0f
#define SUI_ROW_H      40.0f
#define SUI_ROW_H2     54.0f
#define SUI_INSET      14.0f
#define SUI_CARD_GAP   20.0f
#define SUI_MAX_CTL    96

static struct {
    float      scroll;
    float      content_h;
    nxc_rect_t area;
    int        have_area;
    float      card_h[SUI_MAX_CARDS];
    /* pointer */
    int32_t    mx, my;
    int        hover_valid;
    int        pressed;       /* control id pressed, -1 none */
    int        active;        /* slider being dragged, -1 none */
    int        down;          /* button held inside the pane */
    int32_t    press_x, press_y;
    float      scroll_start;
    int        scrolling;     /* drag-scroll in progress */
    int        sb_drag;       /* scrollbar thumb drag */
    float      sb_grab;       /* pointer offset within the thumb */
    float      sb_seen;
    float      scroll_prev;       /* nxui_time() of the last scroll activity */
    /* toggle animation */
    float      anim[SUI_MAX_CTL];
    uint8_t    anim_set[SUI_MAX_CTL];
    /* deferred navigation from a nav row */
    const char *nav;
    sui_build_fn last_fn;
} S = { .pressed = -1, .active = -1 };

float sui_scroll_offset(void) { return S.scroll; }

void sui_pointer_left(void) { if (!S.down) S.hover_valid = 0; }

void sui_pane_reset(void) {
    S.scroll = 0.0f;
    S.content_h = 0.0f;
    S.pressed = -1;
    S.active = -1;
    S.down = 0;
    S.scrolling = 0;
    S.sb_drag = 0;
    S.hover_valid = 0;
    S.nav = 0;
    S.sb_seen = -10.0f;
    S.scroll_prev = 0.0f;
    for (int i = 0; i < SUI_MAX_CTL; i++) S.anim_set[i] = 0;
    for (int i = 0; i < SUI_MAX_CARDS; i++) S.card_h[i] = 0.0f;
}

static float sui_max_scroll(void) {
    float m = S.content_h - S.area.h;
    return m > 0.0f ? m : 0.0f;
}

static void sui_clamp_scroll(void) {
    float m = sui_max_scroll();
    if (S.scroll != S.scroll_prev) { S.sb_seen = nxui_time(); S.scroll_prev = S.scroll; }
    if (S.scroll > m) S.scroll = m;
    if (S.scroll < 0.0f) S.scroll = 0.0f;
}

static void sui_begin(sui_t *ui, sui_pass_t pass, nxc_canvas_t *c, nxc_rect_t area) {
    ui->pass = pass;
    ui->c = c;
    ui->area = area;
    float w = area.w - 2.0f * SUI_PAD_X;
    if (w > SUI_MAX_W) w = SUI_MAX_W;
    if (w < 120.0f) w = 120.0f;
    ui->w = w;
    ui->x = area.x + (area.w - w) * 0.5f;
    ui->top = area.y + SUI_PAD_TOP - S.scroll;
    ui->y = ui->top;
    ui->ctl = 0;
    ui->card = 0;
    ui->in_card = 0;
    ui->row_in_card = 0;
    ui->card_y = 0.0f;
    ui->mx = S.mx;
    ui->my = S.my;
    ui->press = ui->release = ui->held = 0;
}

static int sui_hover(const sui_t *ui, nxc_rect_t r) {
    if (!S.hover_valid || S.scrolling || S.sb_drag) return 0;
    if (!nxglass_hit(ui->area, ui->mx, ui->my)) return 0;
    return nxglass_hit(r, ui->mx, ui->my);
}

/* Click semantics: arm on press, fire on release inside the same control,
 * cancelled when the press turns into a drag-scroll. */
static int sui_clickable(sui_t *ui, int id, nxc_rect_t r) {
    if (ui->pass != SUI_EVENT) return 0;
    if (ui->press && nxglass_hit(r, ui->mx, ui->my) && nxglass_hit(ui->area, ui->mx, ui->my)) {
        S.pressed = id;
        return 0;
    }
    if (ui->release && S.pressed == id) {
        S.pressed = -1;
        return nxglass_hit(r, ui->mx, ui->my);
    }
    return 0;
}

static uint32_t sui_state(const sui_t *ui, int id, nxc_rect_t r) {
    uint32_t st = 0;
    if (sui_hover(ui, r)) st |= NXGLASS_HOVER;
    if (S.pressed == id && S.down && nxglass_hit(r, ui->mx, ui->my)) st |= NXGLASS_PRESSED;
    return st;
}

/* ============ Layout ============ */

void sui_space(sui_t *ui, float h) { if (ui && h > 0.0f) ui->y += h; }

void sui_header(sui_t *ui, const char *pane_id, const char *title, const char *subtitle) {
    if (!ui || !title) return;
    const nxui_theme_t *th = nxui_theme();
    float tile = 44.0f;
    float tx = ui->x + tile + 14.0f;
    float tw = ui->w - tile - 14.0f;
    float sub_h = subtitle ? sui_wrap_text(ui->c, NXUI_TEXT_FOOTNOTE, tx, 0.0f, tw, subtitle, 0, 0) : 0.0f;
    float title_h = nxui_text_ascent(NXUI_TEXT_TITLE1) + nxui_text_descent(NXUI_TEXT_TITLE1);
    float block = title_h + (sub_h > 0.0f ? sub_h + 4.0f : 0.0f);
    float h = block > tile ? block : tile;
    if (ui->pass == SUI_DRAW) {
        settings_draw_tile(ui->c, settings_tile_for(pane_id), ui->x + tile * 0.5f, ui->y + h * 0.5f, tile);
        float ty = ui->y + (h - block) * 0.5f;
        nxui_text(ui->c, NXUI_TEXT_TITLE1, tx, ty + nxui_text_ascent(NXUI_TEXT_TITLE1), title,
                  th->text_primary, NULL);
        if (subtitle)
            sui_wrap_text(ui->c, NXUI_TEXT_FOOTNOTE, tx, ty + title_h + 4.0f, tw, subtitle,
                          th->text_secondary, 1);
    }
    ui->y += h + 22.0f;
}

void sui_section(sui_t *ui, const char *title) {
    if (!ui || !title) return;
    if (ui->pass == SUI_DRAW)
        nxui_text_in_rect(ui->c, NXUI_TEXT_HEADLINE, nxc_rect(ui->x + 4.0f, ui->y, ui->w - 8.0f, 18.0f),
                          title, nxui_theme()->text_primary, NXC_ALIGN_LEFT);
    ui->y += 26.0f;
}

void sui_card_begin(sui_t *ui) {
    if (!ui || ui->in_card) return;
    ui->in_card = 1;
    ui->row_in_card = 0;
    ui->card_y = ui->y;
    if (ui->pass == SUI_DRAW && ui->card < SUI_MAX_CARDS && S.card_h[ui->card] > 0.0f)
        nxglass_card(ui->c, nxc_rect(ui->x, ui->y, ui->w, S.card_h[ui->card]), 10.0f);
}

void sui_card_end(sui_t *ui) {
    if (!ui || !ui->in_card) return;
    if (ui->pass == SUI_MEASURE && ui->card < SUI_MAX_CARDS)
        S.card_h[ui->card] = ui->y - ui->card_y;
    ui->card++;
    ui->in_card = 0;
    ui->y += SUI_CARD_GAP;
}

void sui_note(sui_t *ui, const char *text) {
    if (!ui || !text) return;
    ui->y -= SUI_CARD_GAP - 6.0f;
    float h = sui_wrap_text(ui->c, NXUI_TEXT_FOOTNOTE, ui->x + 6.0f, ui->y, ui->w - 12.0f, text,
                            nxui_theme()->text_secondary, ui->pass == SUI_DRAW);
    ui->y += h + SUI_CARD_GAP - 2.0f;
}

/* Allocate a row inside the current card (draws the separator above it). */
static nxc_rect_t sui_row(sui_t *ui, float h) {
    nxc_rect_t r = nxc_rect(ui->x, ui->y, ui->w, h);
    if (ui->pass == SUI_DRAW && ui->in_card && ui->row_in_card > 0)
        nxglass_separator(ui->c, ui->x + SUI_INSET, ui->y, ui->w - 2.0f * SUI_INSET);
    ui->row_in_card++;
    ui->y += h;
    return r;
}

static void sui_label(sui_t *ui, nxc_rect_t r, const char *label, const char *detail, float right_w) {
    if (ui->pass != SUI_DRAW || !label) return;
    const nxui_theme_t *th = nxui_theme();
    float lw = r.w - 2.0f * SUI_INSET - right_w;
    if (lw < 40.0f) lw = 40.0f;
    if (detail) {
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(r.x + SUI_INSET, r.y + 8.0f, lw, 18.0f),
                          label, th->text_primary, NXC_ALIGN_LEFT);
        nxui_text_in_rect(ui->c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + SUI_INSET, r.y + 27.0f, lw, 16.0f),
                          detail, th->text_secondary, NXC_ALIGN_LEFT);
    } else {
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(r.x + SUI_INSET, r.y, lw, r.h),
                          label, th->text_primary, NXC_ALIGN_LEFT);
    }
}

nxc_rect_t sui_block(sui_t *ui, float h) {
    if (!ui) return nxc_rect(0, 0, 0, 0);
    nxc_rect_t r = sui_row(ui, h);
    return nxc_rect(r.x + SUI_INSET, r.y, r.w - 2.0f * SUI_INSET, r.h);
}

/* ============ Rows ============ */

void sui_info(sui_t *ui, const char *label, const char *value) {
    if (!ui) return;
    nxc_rect_t r = sui_row(ui, SUI_ROW_H);
    float vw = value ? nxui_text_width(NXUI_TEXT_BODY, value) + 8.0f : 0.0f;
    if (vw > r.w * 0.62f) vw = r.w * 0.62f;
    sui_label(ui, r, label, NULL, vw);
    if (ui->pass == SUI_DRAW && value)
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(r.x + r.w - SUI_INSET - vw, r.y, vw, r.h),
                          value, nxui_theme()->text_secondary, NXC_ALIGN_RIGHT);
}

void sui_status(sui_t *ui, const char *label, const char *badge, nxc_color_t color) {
    if (!ui) return;
    nxc_rect_t r = sui_row(ui, SUI_ROW_H);
    float bw = badge ? nxui_text_width(NXUI_TEXT_CAPTION, badge) + 14.0f : 0.0f;
    sui_label(ui, r, label, NULL, bw + 8.0f);
    if (ui->pass == SUI_DRAW && badge)
        nxglass_badge(ui->c, r.x + r.w - SUI_INSET - bw, r.y + (r.h - 18.0f) * 0.5f, badge, color);
}

int sui_toggle(sui_t *ui, const char *label, const char *detail, int *value) {
    if (!ui || !value) return 0;
    int id = ui->ctl++;
    nxc_rect_t r = sui_row(ui, detail ? SUI_ROW_H2 : SUI_ROW_H);
    float tx = r.x + r.w - SUI_INSET - NXGLASS_TOGGLE_W;
    float ty = r.y + (r.h - NXGLASS_TOGGLE_H) * 0.5f;
    nxc_rect_t hit = nxc_rect(tx - 6.0f, r.y, NXGLASS_TOGGLE_W + 12.0f, r.h);
    sui_label(ui, r, label, detail, NXGLASS_TOGGLE_W + 12.0f);
    if (ui->pass == SUI_DRAW) {
        float target = *value ? 1.0f : 0.0f;
        float t = target;
        if (id < SUI_MAX_CTL) {
            if (!S.anim_set[id]) { S.anim[id] = target; S.anim_set[id] = 1; }
            S.anim[id] = nxui_approach(S.anim[id], target, 0.05f, nxui_frame_dt());
            if (S.anim[id] - target < 0.01f && target - S.anim[id] < 0.01f) S.anim[id] = target;
            t = S.anim[id];
        }
        nxglass_toggle(ui->c, tx, ty, t, sui_state(ui, id, hit));
    }
    if (sui_clickable(ui, id, hit)) {
        *value = !*value;
        return 1;
    }
    return 0;
}

int sui_slider(sui_t *ui, const char *label, float *value, const char *lo, const char *hi) {
    if (!ui || !value) return 0;
    int id = ui->ctl++;
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = sui_row(ui, SUI_ROW_H2);
    float lw = r.w * 0.34f;
    if (lw > 190.0f) lw = 190.0f;
    float lo_w = lo ? nxui_text_width(NXUI_TEXT_FOOTNOTE, lo) + 10.0f : 0.0f;
    float hi_w = hi ? nxui_text_width(NXUI_TEXT_FOOTNOTE, hi) + 10.0f : 0.0f;
    float sx = r.x + SUI_INSET + lw + lo_w;
    float sw = r.w - 2.0f * SUI_INSET - lw - lo_w - hi_w - 8.0f;
    if (sw < 60.0f) sw = 60.0f;
    nxc_rect_t sr = nxc_rect(sx + 8.0f, r.y + (r.h - 20.0f) * 0.5f, sw - 8.0f, 20.0f);
    if (ui->pass == SUI_DRAW) {
        if (label)
            nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(r.x + SUI_INSET, r.y, lw - 8.0f, r.h),
                              label, th->text_primary, NXC_ALIGN_LEFT);
        if (lo)
            nxui_text_in_rect(ui->c, NXUI_TEXT_FOOTNOTE, nxc_rect(sx - lo_w, r.y, lo_w, r.h), lo,
                              th->text_secondary, NXC_ALIGN_RIGHT);
        if (hi)
            nxui_text_in_rect(ui->c, NXUI_TEXT_FOOTNOTE, nxc_rect(sr.x + sr.w + 8.0f, r.y, hi_w, r.h), hi,
                              th->text_secondary, NXC_ALIGN_LEFT);
        uint32_t st = (S.active == id) ? NXGLASS_PRESSED : 0;
        nxglass_slider(ui->c, sr, *value, st);
    }
    if (ui->pass != SUI_EVENT) return 0;
    nxc_rect_t grab = nxc_rect(sr.x - 10.0f, r.y, sr.w + 20.0f, r.h);
    if (ui->press && nxglass_hit(grab, ui->mx, ui->my) && nxglass_hit(ui->area, ui->mx, ui->my)) {
        S.active = id;
        float v = nxglass_slider_value_at(sr, ui->mx);
        int changed = v != *value;
        *value = v;
        return changed ? 1 : 0;
    }
    if (S.active == id && ui->held) {
        float v = nxglass_slider_value_at(sr, ui->mx);
        int changed = v != *value;
        *value = v;
        return changed ? 1 : 0;
    }
    if (S.active == id && ui->release) {
        S.active = -1;
        return 2;
    }
    return 0;
}

int sui_segmented(sui_t *ui, const char *label, const char *const *labels, int count, int *selected) {
    if (!ui || !labels || count <= 0 || !selected) return 0;
    int id = ui->ctl++;
    nxc_rect_t r = sui_row(ui, SUI_ROW_H);
    float want = 0.0f;
    for (int i = 0; i < count; i++) {
        float tw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, labels[i] ? labels[i] : "") + 26.0f;
        if (tw > want) want = tw;
    }
    want *= (float)count;
    float maxw = label ? r.w * 0.58f : r.w - 2.0f * SUI_INSET;
    if (want > maxw) want = maxw;
    if (want < 120.0f) want = 120.0f;
    nxc_rect_t sr = label ? nxc_rect(r.x + r.w - SUI_INSET - want, r.y + 7.0f, want, 26.0f)
                          : nxc_rect(r.x + SUI_INSET, r.y + 7.0f, r.w - 2.0f * SUI_INSET, 26.0f);
    sui_label(ui, r, label, NULL, want + 8.0f);
    if (ui->pass == SUI_DRAW) {
        int hov = sui_hover(ui, sr) ? nxglass_segment_at(sr, count, ui->mx, ui->my) : -1;
        nxglass_segmented(ui->c, sr, labels, count, *selected, hov);
    }
    if (ui->pass == SUI_EVENT && ui->press && nxglass_hit(ui->area, ui->mx, ui->my)) {
        int s = nxglass_segment_at(sr, count, ui->mx, ui->my);
        if (s >= 0) {
            S.pressed = id;
            if (s != *selected) { *selected = s; return 1; }
        }
    }
    return 0;
}

int sui_button_row(sui_t *ui, const char *label, const char *detail, const char *button,
                   nxglass_button_kind_t kind, int enabled) {
    if (!ui || !button) return 0;
    int id = ui->ctl++;
    nxc_rect_t r = sui_row(ui, detail ? SUI_ROW_H2 : SUI_ROW_H);
    float bw = nxui_text_width(NXUI_TEXT_BODY_MEDIUM, button) + 28.0f;
    if (bw < 84.0f) bw = 84.0f;
    nxc_rect_t br = nxc_rect(r.x + r.w - SUI_INSET - bw, r.y + (r.h - 26.0f) * 0.5f, bw, 26.0f);
    sui_label(ui, r, label, detail, bw + 10.0f);
    if (ui->pass == SUI_DRAW) {
        uint32_t st = enabled ? sui_state(ui, id, br) : NXGLASS_DISABLED;
        nxglass_button(ui->c, br, button, kind, st);
    }
    if (!enabled) return 0;
    return sui_clickable(ui, id, br);
}

int sui_nav_row(sui_t *ui, const char *pane_id, const char *label, const char *detail) {
    if (!ui || !label) return 0;
    int id = ui->ctl++;
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = sui_row(ui, SUI_ROW_H);
    if (ui->pass == SUI_DRAW) {
        uint32_t st = sui_state(ui, id, r);
        if (st & (NXGLASS_HOVER | NXGLASS_PRESSED)) {
            nxc_rect_t hr = nxc_rect(r.x + 4.0f, r.y + 3.0f, r.w - 8.0f, r.h - 6.0f);
            nxc_fill_rrect(ui->c, hr, 7.0f, (st & NXGLASS_PRESSED) ? th->control_fill_pressed
                                                                  : th->control_fill_hover);
        }
        float lx = r.x + SUI_INSET;
        if (pane_id) {
            settings_draw_tile(ui->c, settings_tile_for(pane_id), lx + 11.0f, r.y + r.h * 0.5f, 22.0f);
            lx += 32.0f;
        }
        float dw = detail ? nxui_text_width(NXUI_TEXT_BODY, detail) + 8.0f : 0.0f;
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(lx, r.y, r.x + r.w - lx - dw - 36.0f, r.h),
                          label, th->text_primary, NXC_ALIGN_LEFT);
        if (detail)
            nxui_text_in_rect(ui->c, NXUI_TEXT_BODY, nxc_rect(r.x + r.w - 32.0f - dw, r.y, dw, r.h),
                              detail, th->text_secondary, NXC_ALIGN_RIGHT);
        nxsym_draw(ui->c, NXSYM_CHEVRON_RIGHT, r.x + r.w - SUI_INSET - 5.0f, r.y + r.h * 0.5f, 10.0f,
                   th->text_tertiary, 0.0f);
    }
    if (sui_clickable(ui, id, r)) {
        if (pane_id) S.nav = pane_id;
        return 1;
    }
    return 0;
}

int sui_swatches(sui_t *ui, const char *label, const nxc_color_t *colors, const char *const *names,
                 int count, int *selected) {
    if (!ui || !colors || count <= 0 || !selected) return 0;
    int id = ui->ctl++;
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = sui_row(ui, SUI_ROW_H2 + 6.0f);
    float d = 20.0f, gap = 10.0f;
    float total = (float)count * d + (float)(count - 1) * gap;
    float x0 = r.x + r.w - SUI_INSET - total;
    float cy = r.y + 22.0f;
    const char *sel_name = (names && *selected >= 0 && *selected < count) ? names[*selected] : NULL;
    sui_label(ui, r, label, NULL, total + 8.0f);
    if (ui->pass == SUI_DRAW) {
        for (int i = 0; i < count; i++) {
            float cx = x0 + (float)i * (d + gap) + d * 0.5f;
            nxc_rect_t hr = nxc_rect(cx - d * 0.5f, cy - d * 0.5f, d, d);
            if (sui_hover(ui, hr) && i != *selected)
                nxc_fill_circle(ui->c, cx, cy, d * 0.5f + 3.0f, th->control_fill_hover);
            nxc_fill_circle(ui->c, cx, cy, d * 0.5f, colors[i]);
            nxc_stroke_circle(ui->c, cx, cy, d * 0.5f, 0.75f, NXC_ARGB(50, 0, 0, 0));
            if (i == *selected) {
                nxc_fill_circle(ui->c, cx, cy, 3.5f, NXC_RGB(0xFF, 0xFF, 0xFF));
                nxc_stroke_circle(ui->c, cx, cy, d * 0.5f + 3.0f, 1.5f, NXC_WITH_ALPHA(colors[i], 200));
            }
        }
        if (sel_name)
            nxui_text_in_rect(ui->c, NXUI_TEXT_CAPTION, nxc_rect(x0 - 20.0f, cy + 14.0f, total + 40.0f, 14.0f),
                              sel_name, th->text_secondary, NXC_ALIGN_CENTER);
    }
    if (ui->pass == SUI_EVENT && ui->press && nxglass_hit(ui->area, ui->mx, ui->my)) {
        for (int i = 0; i < count; i++) {
            float cx = x0 + (float)i * (d + gap) + d * 0.5f;
            if (nxglass_hit(nxc_rect(cx - d * 0.5f - 4.0f, cy - d * 0.5f - 4.0f, d + 8.0f, d + 8.0f),
                            ui->mx, ui->my)) {
                S.pressed = id;
                if (i != *selected) { *selected = i; return 1; }
                return 0;
            }
        }
    }
    return 0;
}

int sui_hotspot(sui_t *ui, nxc_rect_t r, uint32_t *state) {
    if (!ui) return 0;
    int id = ui->ctl++;
    if (state) *state = (ui->pass == SUI_DRAW) ? sui_state(ui, id, r) : 0;
    return sui_clickable(ui, id, r);
}

void sui_meter(sui_t *ui, const char *label, const char *caption, float value) {
    if (!ui) return;
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = sui_row(ui, 64.0f);
    if (ui->pass != SUI_DRAW) return;
    float cw = caption ? nxui_text_width(NXUI_TEXT_FOOTNOTE, caption) + 8.0f : 0.0f;
    if (label)
        nxui_text_in_rect(ui->c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(r.x + SUI_INSET, r.y + 8.0f, r.w - 2.0f * SUI_INSET - cw, 20.0f),
                          label, th->text_primary, NXC_ALIGN_LEFT);
    if (caption)
        nxui_text_in_rect(ui->c, NXUI_TEXT_FOOTNOTE, nxc_rect(r.x + r.w - SUI_INSET - cw, r.y + 8.0f, cw, 20.0f),
                          caption, th->text_secondary, NXC_ALIGN_RIGHT);
    nxc_rect_t bar = nxc_rect(r.x + SUI_INSET, r.y + 36.0f, r.w - 2.0f * SUI_INSET, 12.0f);
    if (value < 0.0f) {
        nxc_fill_rrect(ui->c, bar, 6.0f, th->control_fill);
        nxc_stroke_rrect(ui->c, bar, 6.0f, 1.0f, th->separator);
    } else {
        nxglass_progress(ui->c, bar, value > 1.0f ? 1.0f : value);
    }
}

/* ============ Pane glue ============ */

#define SUI_SB_W 10.0f

static int sui_thumb(nxc_rect_t *thumb) {
    float ms = sui_max_scroll();
    if (ms <= 0.0f || S.area.h <= 40.0f) return 0;
    float track_h = S.area.h - 12.0f;
    float th = track_h * (S.area.h / S.content_h);
    if (th < 30.0f) th = 30.0f;
    float ty = S.area.y + 6.0f + (track_h - th) * (S.scroll / ms);
    *thumb = nxc_rect(S.area.x + S.area.w - SUI_SB_W - 2.0f, ty, SUI_SB_W, th);
    return 1;
}

static void sui_run(sui_build_fn fn, sui_pass_t pass, nxc_canvas_t *c, sui_t *ui) {
    sui_begin(ui, pass, c, S.area);
    fn(ui);
    if (ui->in_card) sui_card_end(ui);   /* tolerate a missing card_end */
}

void sui_pane_render(sui_build_fn fn, nxc_canvas_t *c, nxc_rect_t area) {
    if (!fn || !c || area.w < 40.0f || area.h < 40.0f) return;
    if (S.last_fn != fn) {      /* pane switched without on_show */
        sui_pane_reset();
        S.last_fn = fn;
    }
    S.area = area;
    S.have_area = 1;
    sui_t ui;
    sui_run(fn, SUI_MEASURE, c, &ui);
    S.content_h = (ui.y - ui.top) + SUI_PAD_TOP + SUI_PAD_BOTTOM;
    sui_clamp_scroll();

    nxc_save(c);
    nxc_clip_rect(c, nxc_irect((int32_t)area.x, (int32_t)area.y, (int32_t)area.w, (int32_t)area.h));
    sui_run(fn, SUI_DRAW, c, &ui);

    /* Overlay scrollbar: appears while scrolling or when the pointer is
     * near it, then fades out. */
    nxc_rect_t thumb;
    if (sui_thumb(&thumb)) {
        const nxui_theme_t *th = nxui_theme();
        int near = S.hover_valid && nxglass_hit(S.area, S.mx, S.my) && S.mx >= (int32_t)(thumb.x - 12.0f);
        if (near || S.sb_drag) S.sb_seen = nxui_time();
        float age = nxui_time() - S.sb_seen;
        float a = age < 0.9f ? 1.0f : (age < 1.3f ? (1.3f - age) / 0.4f : 0.0f);
        if (a > 0.0f) {
            int hot = S.sb_drag || near;
            nxc_rect_t k = hot ? thumb : nxc_rect(thumb.x + 3.0f, thumb.y, thumb.w - 4.0f, thumb.h);
            nxc_fill_rrect(c, k, k.w * 0.5f, NXC_WITH_ALPHA(th->text_secondary, (uint8_t)((hot ? 150.0f : 100.0f) * a)));
        }
    }
    nxc_restore(c);
}

int sui_pane_mouse(sui_build_fn fn, int32_t lx, int32_t ly, uint8_t b, uint8_t pb) {
    if (!fn || !S.have_area) return 0;
    if (S.last_fn != fn) { sui_pane_reset(); S.last_fn = fn; }
    int32_t ax = (int32_t)S.area.x + lx, ay = (int32_t)S.area.y + ly;
    S.mx = ax; S.my = ay;
    S.hover_valid = 1;
    uint8_t press = (b & 1) && !(pb & 1);
    uint8_t release = !(b & 1) && (pb & 1);
    uint8_t held = (b & 1) && (pb & 1);
    int inside = nxglass_hit(S.area, ax, ay);

    if (press) {
        if (!inside) return 0;
        S.down = 1;
        S.press_x = ax; S.press_y = ay;
        S.scrolling = 0;
        S.scroll_start = S.scroll;
        nxc_rect_t thumb;
        if (sui_thumb(&thumb) && ax >= (int32_t)(thumb.x - 4.0f)) {
            if (nxglass_hit(nxc_rect(thumb.x - 4.0f, thumb.y, thumb.w + 8.0f, thumb.h), ax, ay)) {
                S.sb_grab = (float)ay - thumb.y;
            } else {
                /* Click in the track: jump so the thumb centers on the pointer. */
                S.sb_grab = thumb.h * 0.5f;
            }
            S.sb_drag = 1;
        }
    }
    if (S.sb_drag) {
        if (held || press) {
            float ms = sui_max_scroll();
            float track_h = S.area.h - 12.0f;
            nxc_rect_t thumb;
            if (ms > 0.0f && sui_thumb(&thumb) && track_h - thumb.h > 1.0f) {
                float t = ((float)ay - S.sb_grab - (S.area.y + 6.0f)) / (track_h - thumb.h);
                S.scroll = t * ms;
                sui_clamp_scroll();
            }
        }
        if (release) { S.sb_drag = 0; S.down = 0; }
        return 1;
    }
    if (held && S.active < 0 && S.down) {
        int32_t dy = ay - S.press_y;
        if (!S.scrolling && (dy > 5 || dy < -5) && sui_max_scroll() > 0.0f) {
            S.scrolling = 1;
            S.pressed = -1;
        }
        if (S.scrolling) {
            S.scroll = S.scroll_start - (float)dy;
            sui_clamp_scroll();
            return 1;
        }
    }
    if (release && S.scrolling) {
        S.scrolling = 0;
        S.down = 0;
        S.pressed = -1;
        return 1;
    }

    sui_t ui;
    sui_begin(&ui, SUI_EVENT, 0, S.area);
    ui.c = nxui_canvas();     /* for text measurement only; nothing is drawn */
    ui.mx = ax; ui.my = ay;
    ui.press = press;
    ui.release = release;
    ui.held = held;
    fn(&ui);
    if (release) {
        S.pressed = -1;
        S.active = -1;
        S.down = 0;
    }
    if (S.nav) {
        const char *target = S.nav;
        S.nav = 0;
        settings_navigate(target);   /* resets this state via on_show */
    }
    return inside || press || release || held;
}

int sui_pane_key(sui_build_fn fn, uint8_t sc, uint8_t pressed, char ascii) {
    (void)fn; (void)ascii;
    if (!pressed || !S.have_area) return 0;
    float page = S.area.h - 60.0f;
    if (page < 40.0f) page = 40.0f;
    switch (sc) {
        case 0x48: S.scroll -= 48.0f; break;          /* Up */
        case 0x50: S.scroll += 48.0f; break;          /* Down */
        case 0x49: S.scroll -= page; break;           /* Page Up */
        case 0x51: case 0x39: S.scroll += page; break;/* Page Down, Space */
        case 0x47: S.scroll = 0.0f; break;            /* Home */
        case 0x4F: S.scroll = sui_max_scroll(); break;/* End */
        default: return 0;
    }
    sui_clamp_scroll();
    return 1;
}
