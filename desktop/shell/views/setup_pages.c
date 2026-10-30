/*
 * NeolyxOS - Setup Assistant: pages
 *
 * Layout, drawing and page logic for every page of the first-run flow.
 * Pages draw in absolute coordinates relative to the card rect they are
 * given (setup_assistant.c translates the canvas for transitions) and
 * register their clickable rects with setup_hit_add().
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "setup_internal.h"
#include "nxsyscall.h"

/* ============ Small drawing helpers ============ */

#define CONTENT_TOP     206.0f
#define FOOTER_SPACE    84.0f

static nxc_rect_t content_rect(nxc_rect_t card) {
    return nxc_rect(card.x + 96.0f, card.y + CONTENT_TOP, card.w - 192.0f,
                    card.h - CONTENT_TOP - FOOTER_SPACE);
}

/* Greedy word wrap into r; returns the y below the last line. */
static float draw_wrapped(nxc_canvas_t *c, nxui_text_style_t style, nxc_rect_t r, const char *text,
                          nxc_color_t color, nxc_align_t align) {
    if (!c || !text) return r.y;
    nxui_type_t ty = nxui_type(style);
    float lh = ty.size * 1.42f;
    float y = r.y + nxui_text_ascent(style);
    char line[200];
    const char *p = text;
    while (*p) {
        size_t best = 0, n = 0;
        const char *q = p;
        /* Grow word by word while the line fits. */
        while (q[n]) {
            size_t m = n;
            while (q[m] && q[m] != ' ' && q[m] != '\n') m++;
            if (m >= sizeof(line)) break;
            for (size_t i = 0; i < m; i++) line[i] = q[i];
            line[m] = '\0';
            if (best > 0 && nxui_text_width(style, line) > r.w) break;
            best = m;
            if (q[m] == '\n' || !q[m]) break;
            n = m + 1;
        }
        if (best == 0) {           /* single word wider than the rect */
            while (q[best] && q[best] != ' ' && q[best] != '\n' && best < sizeof(line) - 1) best++;
        }
        for (size_t i = 0; i < best; i++) line[i] = q[i];
        line[best] = '\0';
        float x = align == NXC_ALIGN_CENTER ? r.x + (r.w - nxui_text_width(style, line)) * 0.5f
                : align == NXC_ALIGN_RIGHT ? r.x + r.w - nxui_text_width(style, line) : r.x;
        nxui_text(c, style, x, y, line, color, 0);
        p = q + best;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
        y += lh;
    }
    return y - nxui_text_ascent(style) + 4.0f;
}

typedef enum { ICON_SYM = 0, ICON_GLOBE, ICON_ACCESS, ICON_KETIVEE, ICON_CHECK } icon_kind_t;

/* App-style icon tile at the top of a page. */
static void draw_icon_tile(nxc_canvas_t *c, float cx, float cy, icon_kind_t kind, int sym, nxc_color_t base) {
    const float s = 60.0f;
    nxc_rect_t r = nxc_rect(cx - s * 0.5f, cy - s * 0.5f, s, s);
    nxc_draw_shadow_rrect(c, r, 17.0f, 8.0f, 0.0f, 3.0f, 0.0f, NXC_ARGB(70, 0, 0, 0));
    nxc_paint_t p = nxc_paint_linear(r.x, r.y, r.x, r.y + r.h,
                                     nxc_color_lerp(base, NXC_RGB(0xFF, 0xFF, 0xFF), 0.22f),
                                     nxc_color_lerp(base, NXC_RGB(0x00, 0x00, 0x00), 0.12f));
    nxc_fill_rrect_paint(c, r, 17.0f, &p);
    nxc_stroke_rrect(c, r, 17.0f, 1.0f, NXC_ARGB(60, 0xFF, 0xFF, 0xFF));
    nxc_color_t w = NXC_RGB(0xFF, 0xFF, 0xFF);
    switch (kind) {
        case ICON_GLOBE:
            nxc_stroke_circle(c, cx, cy, 15.0f, 2.2f, w);
            nxc_draw_line(c, cx - 15.0f, cy, cx + 15.0f, cy, 2.0f, w);
            nxc_draw_line(c, cx, cy - 15.0f, cx, cy + 15.0f, 2.0f, w);
            {
                nxc_path_t path;
                if (nxc_path_init_alloc(&path, 64, 16) == NXC_OK) {
                    nxc_path_add_ellipse(&path, cx, cy, 7.0f, 15.0f);
                    nxc_stroke_t sk = { 2.0f, NXC_CAP_ROUND, NXC_JOIN_ROUND, 4.0f };
                    nxc_stroke_path(c, &path, &sk, w);
                    nxc_path_destroy(&path);
                }
            }
            break;
        case ICON_ACCESS:
            nxc_fill_circle(c, cx, cy - 11.0f, 4.0f, w);
            nxc_draw_line(c, cx - 12.0f, cy - 4.0f, cx + 12.0f, cy - 4.0f, 3.0f, w);
            nxc_draw_line(c, cx, cy - 4.0f, cx, cy + 5.0f, 3.0f, w);
            nxc_draw_line(c, cx, cy + 5.0f, cx - 7.0f, cy + 15.0f, 3.0f, w);
            nxc_draw_line(c, cx, cy + 5.0f, cx + 7.0f, cy + 15.0f, 3.0f, w);
            break;
        case ICON_KETIVEE:
            nxui_text_in_rect(c, NXUI_TEXT_LARGE_TITLE, r, "K", w, NXC_ALIGN_CENTER);
            break;
        case ICON_CHECK:
            nxsym_draw(c, NXSYM_CHECK, cx, cy, 30.0f, w, 0.0f);
            break;
        default:
            nxsym_draw(c, (nxsym_t)sym, cx, cy, 28.0f, w, 0.0f);
            break;
    }
}

/* Icon, title and subtitle; returns nothing - content starts at CONTENT_TOP. */
static void draw_header(nxc_canvas_t *c, nxc_rect_t card, icon_kind_t kind, int sym,
                        nxc_color_t base, const char *title, const char *subtitle) {
    const nxui_theme_t *th = nxui_theme();
    float cx = card.x + card.w * 0.5f;
    draw_icon_tile(c, cx, card.y + 88.0f, kind, sym, base);
    nxc_text_opts_t o = { 0 };
    o.align = NXC_ALIGN_CENTER;
    o.letter_spacing = nxui_type(NXUI_TEXT_LARGE_TITLE).tracking;
    nxui_text(c, NXUI_TEXT_LARGE_TITLE, cx, card.y + 152.0f, title, th->text_primary, &o);
    if (subtitle)
        draw_wrapped(c, NXUI_TEXT_BODY, nxc_rect(card.x + 120.0f, card.y + 164.0f, card.w - 240.0f, 40.0f),
                     subtitle, th->text_secondary, NXC_ALIGN_CENTER);
}

static void focus_ring(nxc_canvas_t *c, nxc_rect_t r, float radius) {
    const nxui_theme_t *th = nxui_theme();
    nxc_stroke_rrect(c, nxc_rect(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f), radius + 3.0f, 2.0f,
                     NXC_WITH_ALPHA(th->accent, 150));
}

/* A selectable list inside a grouped card. */
static void draw_list(nxc_canvas_t *c, nxc_rect_t r, int slot, const setup_choice_t *items, int count,
                      int sel, float row_h, int show_detail) {
    const nxui_theme_t *th = nxui_theme();
    nxglass_card(c, r, 12.0f);
    if (setup_focus_ring_visible(slot)) focus_ring(c, r, 12.0f);
    for (int i = 0; i < count; i++) {
        nxc_rect_t row = nxc_rect(r.x + 4.0f, r.y + 4.0f + row_h * (float)i, r.w - 8.0f, row_h);
        if (row.y + row.h > r.y + r.h) break;
        uint32_t st = (i == sel ? NXGLASS_SELECTED : 0u) | (setup_hover(row) ? NXGLASS_HOVER : 0u);
        const char *detail = show_detail && items[i].detail ? items[i].detail : 0;
        nxc_rect_t rr = row;
        if (i == sel) rr.w -= 26.0f;
        nxglass_row(c, rr, NXSYM_COUNT, items[i].name, detail, st & ~NXGLASS_SELECTED);
        if (i == sel) {
            nxc_fill_rrect(c, row, th->radius_control, NXC_WITH_ALPHA(th->accent, 36));
            nxsym_draw(c, NXSYM_CHECK, row.x + row.w - 16.0f, row.y + row.h * 0.5f, 14.0f, th->accent, 0.0f);
        }
        setup_hit_add(row, SETUP_HIT_SLOT, slot, i);
    }
}

/* Toggle animation per slot of the current page. */
static float g_toggle_t[SETUP_MAX_SLOTS];

static void draw_toggle_rows(nxc_canvas_t *c, nxc_rect_t r, int slot_base, const char *const *titles,
                             const char *const *subs, uint8_t *flags, int count) {
    const nxui_theme_t *th = nxui_theme();
    const float rh = 52.0f;
    nxc_rect_t box = nxc_rect(r.x, r.y, r.w, rh * (float)count);
    nxglass_card(c, box, 12.0f);
    float dt = nxui_frame_dt();
    for (int i = 0; i < count; i++) {
        int slot = slot_base + i;
        nxc_rect_t row = nxc_rect(box.x, box.y + rh * (float)i, box.w, rh);
        if (i > 0) nxglass_separator(c, row.x + 14.0f, row.y, row.w - 28.0f);
        if (setup_hover(row)) nxc_fill_rrect(c, nxc_rect(row.x + 3.0f, row.y + 3.0f, row.w - 6.0f, row.h - 6.0f),
                                             9.0f, NXC_WITH_ALPHA(th->control_fill_hover, 90));
        nxui_text(c, NXUI_TEXT_BODY_MEDIUM, row.x + 16.0f, row.y + 22.0f, titles[i], th->text_primary, 0);
        nxui_text(c, NXUI_TEXT_FOOTNOTE, row.x + 16.0f, row.y + 39.0f, subs[i], th->text_secondary, 0);
        float target = flags[i] ? 1.0f : 0.0f;
        if (slot >= 0 && slot < SETUP_MAX_SLOTS) {
            g_toggle_t[slot] = setup_reduce_motion() ? target : nxui_approach(g_toggle_t[slot], target, 0.05f, dt);
            float tx = row.x + row.w - 16.0f - NXGLASS_TOGGLE_W;
            float ty = row.y + (rh - NXGLASS_TOGGLE_H) * 0.5f;
            nxglass_toggle(c, tx, ty, g_toggle_t[slot], 0u);
            if (setup_focus_ring_visible(slot))
                focus_ring(c, nxc_rect(tx, ty, NXGLASS_TOGGLE_W, NXGLASS_TOGGLE_H), NXGLASS_TOGGLE_H * 0.5f);
        }
        setup_hit_add(row, SETUP_HIT_SLOT, slot, 0);
    }
}

/* Masked rendering for secret fields: one bullet per character. */
static const char *field_display(const setup_field_t *f, char *tmp, size_t cap) {
    if (!f->secret) return f->buf;
    size_t n = 0;
    for (uint8_t i = 0; i < f->len && n + 4 < cap; i++) {
        tmp[n++] = (char)0xE2; tmp[n++] = (char)0x80; tmp[n++] = (char)0xA2;   /* U+2022 */
    }
    tmp[n] = '\0';
    return tmp;
}

static void draw_field(nxc_canvas_t *c, nxc_rect_t r, int slot, const setup_field_t *f,
                       const char *placeholder, int error) {
    const nxui_theme_t *th = nxui_theme();
    char masked[SETUP_FIELD_CAP * 3 + 4];
    uint32_t st = setup_focus_slot() == slot ? NXGLASS_FOCUSED : 0u;
    nxglass_field(c, r, field_display(f, masked, sizeof(masked)), placeholder, 0, st);
    if (error) nxc_stroke_rrect(c, r, th->radius_control, 1.5f, NXC_WITH_ALPHA(th->destructive, 220));
    setup_secure_wipe(masked, sizeof(masked));
    setup_hit_add(r, SETUP_HIT_SLOT, slot, 0);
}

static void draw_message(nxc_canvas_t *c, float x, float y, const char *err, const char *help) {
    const nxui_theme_t *th = nxui_theme();
    if (err) {
        nxsym_draw(c, NXSYM_WARNING, x + 6.0f, y - 4.0f, 11.0f, th->destructive, 0.0f);
        nxui_text(c, NXUI_TEXT_CAPTION, x + 16.0f, y, err, th->destructive, 0);
    } else if (help) {
        nxui_text(c, NXUI_TEXT_CAPTION, x, y, help, th->text_tertiary, 0);
    }
}

/* ============ Welcome ============ */

static void render_welcome(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    float cx = card.x + card.w * 0.5f;
    float cy = card.y + card.h * 0.40f;

    /* Soft accent glow behind the greeting. */
    nxc_paint_t glow = nxc_paint_radial(cx, cy - 16.0f, 230.0f, NXC_WITH_ALPHA(th->accent, 70),
                                        NXC_WITH_ALPHA(th->accent, 0));
    glow.dither = 1;
    nxc_fill_circle_paint(c, cx, cy - 16.0f, 230.0f, &glow);

    const float period = 2.6f;
    float t = nxui_time();
    int idx = (int)(t / period) % g_setup_hello_count;
    float ph = t - (float)(int)(t / period) * period;
    float a = 1.0f, rise = 0.0f;
    if (!setup_reduce_motion()) {
        if (ph < 0.45f) { float k = nxui_ease_out_cubic(ph / 0.45f); a = k; rise = (1.0f - k) * 14.0f; }
        else if (ph > period - 0.4f) { float k = (period - ph) / 0.4f; a = k < 0.0f ? 0.0f : k; rise = -(1.0f - a) * 8.0f; }
    }
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(a * 255.0f));
    nxc_text_opts_t o = { 0 };
    o.align = NXC_ALIGN_CENTER;
    o.letter_spacing = -1.5f;
    nxc_draw_text(c, nxui_font(NXUI_FONT_DISPLAY), 84.0f, cx, cy + 20.0f + rise,
                  g_setup_hello[idx], -1, th->text_primary, &o);
    nxc_restore(c);

    nxc_text_opts_t o2 = { 0 };
    o2.align = NXC_ALIGN_CENTER;
    nxui_text(c, NXUI_TEXT_TITLE2, cx, cy + 72.0f, "Welcome to NeolyxOS", th->text_secondary, &o2);

    draw_wrapped(c, NXUI_TEXT_FOOTNOTE, nxc_rect(card.x + 140.0f, card.y + card.h - 150.0f, card.w - 280.0f, 40.0f),
                 "Setup takes a few minutes. Use Tab, Enter and Esc to move through it with the keyboard.",
                 th->text_tertiary, NXC_ALIGN_CENTER);
}

/* ============ Language ============ */

static void render_language(nxc_canvas_t *c, nxc_rect_t card) {
    draw_header(c, card, ICON_GLOBE, 0, NXC_RGB(0x0A, 0x84, 0xFF), "Choose your language",
                "Used for system text, dates and the apps that support it.");
    nxc_rect_t r = content_rect(card);
    float rh = 34.0f;
    float h = rh * (float)g_setup_language_count + 8.0f;
    if (h > r.h) h = r.h;
    nxc_rect_t lr = nxc_rect(r.x + 40.0f, r.y, r.w - 80.0f, h);
    draw_list(c, lr, 0, g_setup_languages, g_setup_language_count, g_setup.language, rh, 0);
}

/* ============ Region & keyboard ============ */

static void render_region(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_KEYBOARD, NXC_RGB(0x5E, 0x5C, 0xE6), "Region and keyboard",
                "Choose where you are and how your keyboard is laid out.");
    nxc_rect_t r = content_rect(card);
    float gap = 20.0f, colw = (r.w - gap) * 0.5f, rh = 34.0f;
    nxui_text(c, NXUI_TEXT_FOOTNOTE, r.x + 4.0f, r.y + 10.0f, "Region", th->text_secondary, 0);
    nxui_text(c, NXUI_TEXT_FOOTNOTE, r.x + colw + gap + 4.0f, r.y + 10.0f, "Keyboard layout", th->text_secondary, 0);
    float h1 = rh * (float)g_setup_region_count + 8.0f;
    float h2 = rh * (float)g_setup_keyboard_count + 8.0f;
    float maxh = r.h - 22.0f;
    if (h1 > maxh) h1 = maxh;
    if (h2 > maxh) h2 = maxh;
    draw_list(c, nxc_rect(r.x, r.y + 20.0f, colw, h1), 0, g_setup_regions, g_setup_region_count,
              g_setup.region, rh, 0);
    draw_list(c, nxc_rect(r.x + colw + gap, r.y + 20.0f, colw, h2), 1, g_setup_keyboards,
              g_setup_keyboard_count, g_setup.keyboard, rh, 1);
}

/* ============ Accessibility ============ */

static const char *const k_ax_titles[SETUP_AX_COUNT] = {
    "Larger text", "Bold text", "Reduce motion", "Increase contrast", "Reduce transparency" };
static const char *const k_ax_subs[SETUP_AX_COUNT] = {
    "Make text and controls easier to read.",
    "Draw interface text in a heavier weight.",
    "Use fades instead of sliding and spring animations.",
    "Strengthen borders and separate colors more clearly.",
    "Replace frosted glass with solid backgrounds." };

static void render_accessibility(nxc_canvas_t *c, nxc_rect_t card) {
    draw_header(c, card, ICON_ACCESS, 0, NXC_RGB(0x0A, 0x84, 0xFF), "Accessibility",
                "Turn on anything that makes NeolyxOS easier to use. You can change these later in Settings.");
    nxc_rect_t r = content_rect(card);
    draw_toggle_rows(c, nxc_rect(r.x, r.y + 4.0f, r.w, r.h), 0, k_ax_titles, k_ax_subs, g_setup.ax, SETUP_AX_COUNT);
}

/* ============ Network ============ */

static int net_needs_password(void) {
    int s = g_setup.net_sel;
    return s >= 0 && s < g_setup.net_count && g_setup.nets[s].secured && !g_setup.nets[s].wired &&
           g_setup.net_joined != s;
}

static void render_network(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_WIFI, NXC_RGB(0x0A, 0x84, 0xFF), "Connect to a network",
                "Connect now to get updates and finish signing in to your Ketivee ID, or set up later.");
    nxc_rect_t r = content_rect(card);
    float cx = r.x + r.w * 0.5f;

    if (g_setup.net_count <= 0) {
        nxc_rect_t box = nxc_rect(r.x + 40.0f, r.y + 10.0f, r.w - 80.0f, 150.0f);
        nxglass_card(c, box, 12.0f);
        nxsym_draw(c, NXSYM_WIFI_OFF, cx, box.y + 46.0f, 30.0f, th->text_tertiary, 0.0f);
        nxc_text_opts_t o = { 0 };
        o.align = NXC_ALIGN_CENTER;
        nxui_text(c, NXUI_TEXT_HEADLINE, cx, box.y + 92.0f,
                  g_setup.net_count == 0 ? "No networks found" : "Networking isn't available yet",
                  th->text_primary, &o);
        nxui_text(c, NXUI_TEXT_FOOTNOTE, cx, box.y + 114.0f,
                  "You can connect later in Settings > Network.", th->text_secondary, &o);
        return;
    }

    const float rh = 40.0f;
    float lh = rh * (float)g_setup.net_count + 8.0f;
    nxc_rect_t lr = nxc_rect(r.x + 40.0f, r.y, r.w - 80.0f, lh);
    nxglass_card(c, lr, 12.0f);
    if (setup_focus_ring_visible(0)) focus_ring(c, lr, 12.0f);
    for (int i = 0; i < g_setup.net_count; i++) {
        const setup_net_entry_t *n = &g_setup.nets[i];
        nxc_rect_t row = nxc_rect(lr.x + 4.0f, lr.y + 4.0f + rh * (float)i, lr.w - 8.0f, rh);
        if (i == g_setup.net_sel) nxc_fill_rrect(c, row, th->radius_control, NXC_WITH_ALPHA(th->accent, 36));
        else if (setup_hover(row)) nxc_fill_rrect(c, row, th->radius_control, th->control_fill_hover);
        nxsym_draw(c, n->wired ? NXSYM_ETHERNET : NXSYM_WIFI, row.x + 20.0f, row.y + rh * 0.5f, 16.0f,
                   i == g_setup.net_sel ? th->accent : th->text_secondary, n->wired ? 0.0f : (float)n->bars / 3.0f);
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(row.x + 40.0f, row.y, row.w - 160.0f, rh), n->ssid,
                          th->text_primary, NXC_ALIGN_LEFT);
        float rx = row.x + row.w - 14.0f;
        if (i == g_setup.net_joined) {
            nxsym_draw(c, NXSYM_CHECK, rx - 2.0f, row.y + rh * 0.5f, 14.0f, th->success, 0.0f);
            nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(rx - 110.0f, row.y, 92.0f, rh), "Connected",
                              th->success, NXC_ALIGN_RIGHT);
        } else if (n->secured) {
            nxsym_draw(c, NXSYM_LOCK, rx - 2.0f, row.y + rh * 0.5f, 12.0f, th->text_tertiary, 0.0f);
        }
        setup_hit_add(row, SETUP_HIT_SLOT, 0, i);
    }

    float y = lr.y + lh + 16.0f;
    if (net_needs_password()) {
        nxui_text(c, NXUI_TEXT_FOOTNOTE, lr.x + 2.0f, y + 10.0f, "Network password", th->text_secondary, 0);
        draw_field(c, nxc_rect(lr.x, y + 18.0f, lr.w, NXGLASS_FIELD_H), 1, &g_setup.net_password,
                   "Password", g_setup.net_error != 0);
        y += 18.0f + NXGLASS_FIELD_H + 16.0f;
    }
    if (g_setup.net_error) draw_message(c, lr.x, y + 4.0f, g_setup.net_error, 0);
}

/* ============ Account ============ */

typedef struct { const char *full, *account, *password, *verify, *hint; } account_errors_t;

static account_errors_t account_errors(void) {
    account_errors_t e;
    e.full = setup_check_full_name(g_setup.full_name.buf);
    e.account = setup_check_account(g_setup.account.buf);
    e.password = setup_check_password(g_setup.password.buf, g_setup.password.len,
                                      g_setup.account.buf, g_setup.full_name.buf);
    e.verify = setup_check_verify(&g_setup.password, &g_setup.verify);
    e.hint = setup_check_hint(g_setup.hint.buf, &g_setup.password);
    return e;
}

/* Show a field's error once the user has left it, or after Continue. */
static int show_err(int slot, const setup_field_t *f) {
    if (g_setup.account_attempted) return 1;
    return f->touched && setup_focus_slot() != slot;
}

static void draw_strength(nxc_canvas_t *c, float right, float y) {
    const nxui_theme_t *th = nxui_theme();
    int s = setup_password_strength(g_setup.password.buf, g_setup.password.len,
                                    g_setup.account.buf, g_setup.full_name.buf);
    static const char *const labels[5] = { "Too short", "Weak", "Fair", "Good", "Strong" };
    nxc_color_t col = s <= 1 ? th->destructive : s == 2 ? th->warning : th->success;
    float lw = nxui_text_width(NXUI_TEXT_CAPTION, labels[s]);
    float segw = 22.0f, gap = 3.0f;
    float x0 = right - lw - 8.0f - 4.0f * segw - 3.0f * gap;
    for (int i = 0; i < 4; i++)
        nxc_fill_rrect(c, nxc_rect(x0 + (segw + gap) * (float)i, y - 6.0f, segw, 4.0f), 2.0f,
                       i < s ? col : th->control_fill_hover);
    nxui_text(c, NXUI_TEXT_CAPTION, right - lw, y, labels[s], col, 0);
}

static void render_account(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_USER, NXC_RGB(0x8E, 0x8E, 0x93), "Create your account",
                "This is the administrator account for this computer.");
    nxc_rect_t r = content_rect(card);
    account_errors_t e = account_errors();

    const float labw = 128.0f, gap = 14.0f, pitch = 52.0f;
    float fw = r.w - labw - gap - 20.0f;
    if (fw > 340.0f) fw = 340.0f;
    float fx = r.x + (r.w - (labw + gap + fw)) * 0.5f + labw + gap;
    float lx = fx - gap - labw;

    struct { const char *label, *ph, *err, *help; setup_field_t *f; } rows[5] = {
        { "Full name",    "Required",     e.full,     0, &g_setup.full_name },
        { "Account name", "Required",     e.account,  "Used for your home folder and to sign in.", &g_setup.account },
        { "Password",     "Required",     e.password, "Use 8 or more characters with a mix of types.", &g_setup.password },
        { "Verify",       "Required",     e.verify,   0, &g_setup.verify },
        { "Hint",         "Optional",     e.hint,     "Shown after several failed sign-in attempts.", &g_setup.hint },
    };
    for (int i = 0; i < 5; i++) {
        float y = r.y + pitch * (float)i;
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(lx, y, labw, NXGLASS_FIELD_H), rows[i].label,
                          th->text_secondary, NXC_ALIGN_RIGHT);
        int se = show_err(i, rows[i].f) && rows[i].err != 0;
        draw_field(c, nxc_rect(fx, y, fw, NXGLASS_FIELD_H), i, rows[i].f, rows[i].ph, se);
        float my = y + NXGLASS_FIELD_H + 14.0f;
        draw_message(c, fx + 2.0f, my, se ? rows[i].err : 0,
                     (setup_focus_slot() == i || !rows[i].f->len) ? rows[i].help : 0);
        if (i == 2 && g_setup.password.len > 0) draw_strength(c, fx + fw, my);
    }
}

/* ============ Ketivee ID ============ */

static void render_ketivee(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_KETIVEE, 0, NXC_RGB(0xE8, 0x66, 0x0A), "Sign in with your Ketivee ID",
                "One ID for Ketivee apps, purchases and sync across your devices. This step is optional.");
    nxc_rect_t r = content_rect(card);
    float cx = r.x + r.w * 0.5f;
    static const char *const seg[2] = { "Sign In", "Create Ketivee ID" };
    nxc_rect_t sr = nxc_rect(cx - 160.0f, r.y, 320.0f, 30.0f);
    nxglass_segmented(c, sr, seg, 2, g_setup.ketivee_mode, -1);
    if (setup_focus_ring_visible(0)) focus_ring(c, sr, th->radius_control);
    for (int i = 0; i < 2; i++)
        setup_hit_add(nxc_rect(sr.x + sr.w * 0.5f * (float)i, sr.y, sr.w * 0.5f, sr.h), SETUP_HIT_SLOT, 0, i);

    float fw = 340.0f, fx = cx - fw * 0.5f, y = r.y + 70.0f;
    const char *ee = setup_check_email(g_setup.kid_email.buf);
    const char *pe = g_setup.kid_password.len == 0 ? "Enter your password."
                   : (g_setup.ketivee_mode == 1 && g_setup.kid_password.len < 8) ? "Use at least 8 characters." : 0;
    int any = g_setup.kid_email.len || g_setup.kid_password.len;
    int se_e = ee && any && (g_setup.ketivee_attempted || (g_setup.kid_email.touched && setup_focus_slot() != 1));
    int se_p = pe && any && g_setup.ketivee_attempted;

    nxui_text(c, NXUI_TEXT_FOOTNOTE, fx + 2.0f, y - 6.0f, "E-mail", th->text_secondary, 0);
    draw_field(c, nxc_rect(fx, y, fw, NXGLASS_FIELD_H), 1, &g_setup.kid_email, "name@example.com", se_e);
    draw_message(c, fx + 2.0f, y + NXGLASS_FIELD_H + 14.0f, se_e ? ee : 0, 0);
    y += 66.0f;
    nxui_text(c, NXUI_TEXT_FOOTNOTE, fx + 2.0f, y - 6.0f, "Password", th->text_secondary, 0);
    draw_field(c, nxc_rect(fx, y, fw, NXGLASS_FIELD_H), 2, &g_setup.kid_password,
               g_setup.ketivee_mode ? "Create a password" : "Password", se_p);
    draw_message(c, fx + 2.0f, y + NXGLASS_FIELD_H + 14.0f, se_p ? pe : 0, 0);

    draw_wrapped(c, NXUI_TEXT_CAPTION, nxc_rect(fx - 20.0f, y + 54.0f, fw + 40.0f, 40.0f),
                 "Nothing is sent during setup. Sign-in finishes once this computer is online, "
                 "and your password is not stored.", th->text_tertiary, NXC_ALIGN_CENTER);
}

/* ============ Privacy ============ */

static const char *const k_priv_titles[SETUP_PRIV_COUNT] = {
    "Share analytics with Ketivee", "Share crash reports", "Personalized suggestions", "Location services" };
static const char *const k_priv_subs[SETUP_PRIV_COUNT] = {
    "Anonymous usage statistics that help improve NeolyxOS.",
    "Send a diagnostic report when an app or the system stops.",
    "Use on-device activity to suggest apps and settings.",
    "Let apps you approve use this computer's location." };

static void render_privacy(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_SHIELD, NXC_RGB(0x30, 0xB0, 0x50), "Privacy and analytics",
                "NeolyxOS collects nothing unless you allow it. Everything here starts off.");
    nxc_rect_t r = content_rect(card);
    draw_toggle_rows(c, nxc_rect(r.x, r.y + 4.0f, r.w, r.h), 0, k_priv_titles, k_priv_subs,
                     g_setup.privacy, SETUP_PRIV_COUNT);
    nxc_text_opts_t o = { 0 };
    o.align = NXC_ALIGN_CENTER;
    nxui_text(c, NXUI_TEXT_CAPTION, r.x + r.w * 0.5f, r.y + 4.0f + 52.0f * SETUP_PRIV_COUNT + 24.0f,
              "You can review these at any time in Settings > Privacy.", th->text_tertiary, &o);
}

/* ============ Appearance ============ */

static void draw_mini_desktop(nxc_canvas_t *c, nxc_rect_t r, int dark, nxc_color_t accent) {
    nxc_paint_t bg = nxc_paint_linear(r.x, r.y, r.x + r.w, r.y + r.h,
                                      dark ? NXC_RGB(0x1B, 0x22, 0x3A) : NXC_RGB(0xC9, 0xD8, 0xF0),
                                      dark ? NXC_RGB(0x3A, 0x22, 0x2E) : NXC_RGB(0xF2, 0xD9, 0xC8));
    nxc_fill_rect_paint(c, r, &bg);
    nxc_fill_rect(c, nxc_rect(r.x, r.y, r.w, 8.0f), dark ? NXC_ARGB(150, 0x10, 0x12, 0x18) : NXC_ARGB(150, 0xFF, 0xFF, 0xFF));
    nxc_rect_t w = nxc_rect(r.x + r.w * 0.16f, r.y + 18.0f, r.w * 0.62f, r.h * 0.52f);
    nxc_fill_rrect(c, w, 5.0f, dark ? NXC_RGB(0x2A, 0x2E, 0x38) : NXC_RGB(0xFF, 0xFF, 0xFF));
    nxc_fill_circle(c, w.x + 7.0f, w.y + 6.0f, 2.0f, NXC_RGB(0xFF, 0x5F, 0x57));
    nxc_fill_circle(c, w.x + 13.0f, w.y + 6.0f, 2.0f, NXC_RGB(0xFE, 0xBC, 0x2E));
    nxc_fill_circle(c, w.x + 19.0f, w.y + 6.0f, 2.0f, NXC_RGB(0x28, 0xC8, 0x40));
    nxc_color_t line = dark ? NXC_ARGB(90, 0xFF, 0xFF, 0xFF) : NXC_ARGB(60, 0, 0, 0);
    nxc_fill_rrect(c, nxc_rect(w.x + 8.0f, w.y + 16.0f, w.w * 0.55f, 4.0f), 2.0f, line);
    nxc_fill_rrect(c, nxc_rect(w.x + 8.0f, w.y + 25.0f, w.w * 0.40f, 4.0f), 2.0f, line);
    nxc_fill_rrect(c, nxc_rect(w.x + w.w - 30.0f, w.y + w.h - 14.0f, 22.0f, 8.0f), 4.0f, accent);
    nxc_rect_t dock = nxc_rect(r.x + r.w * 0.3f, r.y + r.h - 13.0f, r.w * 0.4f, 8.0f);
    nxc_fill_rrect(c, dock, 4.0f, dark ? NXC_ARGB(140, 0x30, 0x34, 0x40) : NXC_ARGB(160, 0xFF, 0xFF, 0xFF));
}

static void render_appearance(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_BRIGHTNESS, NXC_RGB(0x5E, 0x5C, 0xE6), "Choose your look",
                "Pick an appearance and an accent color. You can change them later in Settings.");
    nxc_rect_t r = content_rect(card);
    nxc_color_t accent = g_setup_accents[g_setup.accent].color;
    static const char *const names[SETUP_APPEAR_COUNT] = { "Light", "Dark", "Auto" };
    const float tw = 150.0f, tht = 100.0f, gap = 22.0f;
    float x0 = r.x + (r.w - (3.0f * tw + 2.0f * gap)) * 0.5f;
    for (int i = 0; i < SETUP_APPEAR_COUNT; i++) {
        nxc_rect_t t = nxc_rect(x0 + (tw + gap) * (float)i, r.y + 4.0f, tw, tht);
        nxc_draw_shadow_rrect(c, t, 12.0f, 6.0f, 0.0f, 2.0f, 0.0f, NXC_ARGB(60, 0, 0, 0));
        nxc_save(c);
        nxc_clip_rrect(c, t, 12.0f);
        if (i == SETUP_APPEAR_AUTO) {
            nxc_save(c);
            nxc_clip_rect(c, nxc_irect((int32_t)t.x, (int32_t)t.y, (int32_t)(t.w * 0.5f), (int32_t)t.h + 1));
            draw_mini_desktop(c, t, 0, accent);
            nxc_restore(c);
            nxc_save(c);
            nxc_clip_rect(c, nxc_irect((int32_t)(t.x + t.w * 0.5f), (int32_t)t.y, (int32_t)(t.w * 0.5f) + 1, (int32_t)t.h + 1));
            draw_mini_desktop(c, t, 1, accent);
            nxc_restore(c);
        } else {
            draw_mini_desktop(c, t, i == SETUP_APPEAR_DARK, accent);
        }
        nxc_restore(c);
        int sel = g_setup.appearance == i;
        nxc_stroke_rrect(c, t, 12.0f, 1.0f, th->separator);
        if (sel) nxc_stroke_rrect(c, nxc_rect(t.x - 4.0f, t.y - 4.0f, t.w + 8.0f, t.h + 8.0f), 15.0f, 2.5f, th->accent);
        else if (setup_hover(t)) nxc_stroke_rrect(c, nxc_rect(t.x - 4.0f, t.y - 4.0f, t.w + 8.0f, t.h + 8.0f), 15.0f, 2.0f, th->control_fill_hover);
        nxc_text_opts_t o = { 0 };
        o.align = NXC_ALIGN_CENTER;
        nxui_text(c, sel ? NXUI_TEXT_HEADLINE : NXUI_TEXT_BODY, t.x + t.w * 0.5f, t.y + t.h + 24.0f, names[i],
                  sel ? th->text_primary : th->text_secondary, &o);
        setup_hit_add(nxc_rect(t.x, t.y, t.w, t.h + 32.0f), SETUP_HIT_SLOT, 0, i);
    }
    if (setup_focus_ring_visible(0))
        focus_ring(c, nxc_rect(x0 - 8.0f, r.y - 4.0f, 3.0f * tw + 2.0f * gap + 16.0f, tht + 42.0f), 14.0f);

    float ay = r.y + tht + 72.0f;
    nxc_text_opts_t oc = { 0 };
    oc.align = NXC_ALIGN_CENTER;
    nxui_text(c, NXUI_TEXT_FOOTNOTE, r.x + r.w * 0.5f, ay, "Accent color", th->text_secondary, &oc);
    const float sr = 11.0f, sg = 14.0f;
    float sw = (float)g_setup_accent_count * (2.0f * sr) + (float)(g_setup_accent_count - 1) * sg;
    float sx = r.x + (r.w - sw) * 0.5f + sr;
    float sy = ay + 24.0f;
    for (int i = 0; i < g_setup_accent_count; i++) {
        float cxi = sx + (2.0f * sr + sg) * (float)i;
        nxc_fill_circle(c, cxi, sy, sr, g_setup_accents[i].color);
        nxc_stroke_circle(c, cxi, sy, sr, 1.0f, NXC_ARGB(40, 0, 0, 0));
        if (i == g_setup.accent) {
            nxc_stroke_circle(c, cxi, sy, sr + 4.0f, 2.0f, g_setup_accents[i].color);
            nxc_fill_circle(c, cxi, sy, 3.5f, NXC_RGB(0xFF, 0xFF, 0xFF));
        }
        setup_hit_add(nxc_rect(cxi - sr - 4.0f, sy - sr - 4.0f, 2.0f * sr + 8.0f, 2.0f * sr + 8.0f), SETUP_HIT_SLOT, 1, i);
    }
    if (setup_focus_ring_visible(1))
        focus_ring(c, nxc_rect(sx - sr - 8.0f, sy - sr - 8.0f, sw + 16.0f, 2.0f * sr + 16.0f), sr + 8.0f);
    nxui_text(c, NXUI_TEXT_CAPTION, r.x + r.w * 0.5f, sy + 34.0f, g_setup_accents[g_setup.accent].name,
              th->text_tertiary, &oc);
}

static void apply_appearance(void) {
    nxui_appearance_t a = NXUI_APPEARANCE_DARK;
    if (g_setup.appearance == SETUP_APPEAR_LIGHT) a = NXUI_APPEARANCE_LIGHT;
    else if (g_setup.appearance == SETUP_APPEAR_AUTO) {
        /* Auto: light during the day (07:00-19:00 local RTC), dark otherwise. */
        rtc_time_t now;
        if (nx_rtc_get(&now) == 0 && now.hour >= 7 && now.hour < 19) a = NXUI_APPEARANCE_LIGHT;
    }
    nxui_set_appearance(a);
    setup_log2("[SETUP] appearance applied: ",
               g_setup.appearance == SETUP_APPEAR_AUTO ? "auto" : a == NXUI_APPEARANCE_LIGHT ? "light" : "dark");
}

/* ============ Setting up ============ */

#define SETUP_TASKS 4
enum { TASK_PENDING = 0, TASK_RUNNING, TASK_OK, TASK_WARN, TASK_FAIL };

static struct {
    float t;
    int   next;                   /* next task to run */
    int   state[SETUP_TASKS];
    float done_at;
} g_run;

static const char *const k_task_names[SETUP_TASKS] = {
    "Creating your account", "Saving your preferences", "Applying your appearance", "Finishing up" };

static void run_task(int i) {
    int rc = SETUP_OK;
    switch (i) {
        case 0:
            rc = setup_accounts_create(g_setup.account.buf, g_setup.full_name.buf,
                                       g_setup.password.buf, g_setup.password.len, g_setup.hint.buf);
            /* The password never outlives this call, whatever the result. */
            setup_field_clear(&g_setup.password);
            setup_field_clear(&g_setup.verify);
            setup_log("[SETUP] password buffers wiped\n");
            g_setup.result_account = rc;
            break;
        case 1:
            rc = setup_store_save_prefs(&g_setup);
            g_setup.result_prefs = rc;
            break;
        case 2:
            apply_appearance();
            break;
        case 3:
            rc = setup_store_write_marker(&g_setup);
            g_setup.result_marker = rc;
            break;
        default:
            return;
    }
    g_run.state[i] = rc == SETUP_OK ? TASK_OK : rc == SETUP_ERR_UNSUPPORTED ? TASK_WARN : TASK_FAIL;
}

static void render_setting_up(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    draw_header(c, card, ICON_SYM, NXSYM_GEAR, NXC_RGB(0x8E, 0x8E, 0x93), "Setting up NeolyxOS",
                "This only takes a moment. Please don't turn off your computer.");
    nxc_rect_t r = content_rect(card);
    float pw = 380.0f, px = r.x + (r.w - pw) * 0.5f;
    int done = 0;
    for (int i = 0; i < SETUP_TASKS; i++) if (g_run.state[i] >= TASK_OK) done++;
    float frac = (float)done / (float)SETUP_TASKS;
    static float shown;
    shown = setup_reduce_motion() ? frac : nxui_approach(shown, frac, 0.12f, nxui_frame_dt());
    if (done == 0 && g_run.next == 0) shown = 0.0f;
    nxglass_progress(c, nxc_rect(px, r.y + 10.0f, pw, 6.0f), shown);

    for (int i = 0; i < SETUP_TASKS; i++) {
        float y = r.y + 44.0f + 34.0f * (float)i;
        float ix = px + 10.0f, iy = y + 10.0f;
        int st = g_run.state[i];
        nxc_color_t tc = st == TASK_PENDING ? th->text_tertiary : th->text_primary;
        if (st == TASK_OK) {
            nxc_fill_circle(c, ix, iy, 9.0f, th->success);
            nxsym_draw(c, NXSYM_CHECK, ix, iy, 11.0f, NXC_RGB(0xFF, 0xFF, 0xFF), 0.0f);
        } else if (st == TASK_WARN || st == TASK_FAIL) {
            nxc_fill_circle(c, ix, iy, 9.0f, st == TASK_WARN ? th->warning : th->destructive);
            nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(ix - 9.0f, iy - 9.0f, 18.0f, 18.0f), "!",
                              NXC_RGB(0xFF, 0xFF, 0xFF), NXC_ALIGN_CENTER);
        } else if (st == TASK_RUNNING) {
            float a = nxui_time() * 6.0f;
            for (int k = 0; k < 8; k++) {
                float ang = a + (float)k * 0.785f;
                float s = 0.0f, co = 0.0f;
                /* small-angle-free sin/cos via a few Taylor terms on wrapped angle */
                float x = ang - 6.2831853f * (float)(int)(ang / 6.2831853f);
                if (x > 3.14159265f) x -= 6.2831853f;
                float x2 = x * x;
                s = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f)));
                co = 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f));
                nxc_fill_circle(c, ix + co * 6.5f, iy + s * 6.5f, 1.6f,
                                NXC_WITH_ALPHA(th->text_secondary, (uint8_t)(60 + k * 24)));
            }
        } else {
            nxc_stroke_circle(c, ix, iy, 8.0f, 1.5f, th->text_tertiary);
        }
        nxui_text(c, NXUI_TEXT_BODY, ix + 20.0f, iy + 4.5f, k_task_names[i], tc, 0);
        if (st == TASK_WARN)
            nxui_text(c, NXUI_TEXT_FOOTNOTE, px + pw - nxui_text_width(NXUI_TEXT_FOOTNOTE, "Not available yet"),
                      iy + 4.5f, "Not available yet", th->warning, 0);
        else if (st == TASK_FAIL)
            nxui_text(c, NXUI_TEXT_FOOTNOTE, px + pw - nxui_text_width(NXUI_TEXT_FOOTNOTE, "Failed"),
                      iy + 4.5f, "Failed", th->destructive, 0);
    }
}

/* ============ Done ============ */

static void first_name(char *out, size_t cap) {
    size_t n = 0;
    const char *p = g_setup.full_name.buf;
    while (*p == ' ') p++;
    while (*p && *p != ' ' && n + 1 < cap) out[n++] = *p++;
    out[n] = '\0';
}

static void render_done(nxc_canvas_t *c, nxc_rect_t card) {
    const nxui_theme_t *th = nxui_theme();
    char fn[40], sub[96];
    first_name(fn, sizeof(fn));
    setup_strcpy(sub, "Welcome to NeolyxOS", sizeof(sub));
    if (fn[0]) {
        size_t n = setup_strlen(sub);
        const char *sep = ", ";
        while (*sep && n + 1 < sizeof(sub)) sub[n++] = *sep++;
        for (const char *p = fn; *p && n + 2 < sizeof(sub); p++) sub[n++] = *p;
        sub[n++] = '.';
        sub[n] = '\0';
    }
    draw_header(c, card, ICON_CHECK, 0, th->success, "You're all set", sub);

    nxc_rect_t r = content_rect(card);
    nxc_rect_t box = nxc_rect(r.x + 30.0f, r.y, r.w - 60.0f, 4.0f * 38.0f + 8.0f);
    nxglass_card(c, box, 12.0f);
    static const char *const appear[3] = { "Light", "Dark", "Auto" };
    const char *net = (g_setup.net_joined >= 0 && g_setup.net_joined < g_setup.net_count)
                    ? g_setup.nets[g_setup.net_joined].ssid : "Not connected";
    const char *acct = g_setup.result_account == SETUP_OK ? g_setup.account.buf : "Finish later";
    const char *kid = g_setup.ketivee_pending ? "Finish when online" : "Not signed in";
    struct { int sym; const char *t, *d; } rows[4] = {
        { NXSYM_USER, "Account", acct },
        { NXSYM_LOCK, "Ketivee ID", kid },
        { NXSYM_WIFI, "Network", net },
        { NXSYM_BRIGHTNESS, "Appearance", appear[g_setup.appearance % 3] },
    };
    for (int i = 0; i < 4; i++) {
        nxc_rect_t row = nxc_rect(box.x + 4.0f, box.y + 4.0f + 38.0f * (float)i, box.w - 8.0f, 38.0f);
        if (i > 0) nxglass_separator(c, row.x + 36.0f, row.y, row.w - 46.0f);
        nxglass_row(c, row, rows[i].sym, rows[i].t, rows[i].d, 0u);
    }

    const char *note = 0;
    if (g_setup.result_marker != SETUP_OK && g_setup.result_account != SETUP_OK)
        note = "Your choices were saved, but this build has no account service yet, so no password was set. "
               "Setup couldn't record that it finished and will open again next time.";
    else if (g_setup.result_account != SETUP_OK)
        note = "Your choices were saved, but this build has no account service yet, so no password was set. "
               "You'll be asked to finish creating your account when it's available.";
    else if (g_setup.result_marker != SETUP_OK || g_setup.result_prefs != SETUP_OK)
        note = "Some settings couldn't be saved. Setup will open again next time.";
    if (note) {
        nxc_rect_t nb = nxc_rect(box.x, box.y + box.h + 14.0f, box.w, 58.0f);
        nxc_fill_rrect(c, nb, 10.0f, NXC_WITH_ALPHA(th->warning, 34));
        nxsym_draw(c, NXSYM_INFO, nb.x + 18.0f, nb.y + 18.0f, 14.0f, th->warning, 0.0f);
        draw_wrapped(c, NXUI_TEXT_CAPTION, nxc_rect(nb.x + 34.0f, nb.y + 9.0f, nb.w - 46.0f, nb.h),
                     note, th->text_secondary, NXC_ALIGN_LEFT);
    }
}

/* ============ Page dispatch ============ */

void setup_page_render(nxc_canvas_t *c, setup_page_t p, nxc_rect_t card) {
    if (!c) return;
    switch (p) {
        case SP_WELCOME:       render_welcome(c, card); break;
        case SP_LANGUAGE:      render_language(c, card); break;
        case SP_REGION:        render_region(c, card); break;
        case SP_ACCESSIBILITY: render_accessibility(c, card); break;
        case SP_NETWORK:       render_network(c, card); break;
        case SP_ACCOUNT:       render_account(c, card); break;
        case SP_KETIVEE:       render_ketivee(c, card); break;
        case SP_PRIVACY:       render_privacy(c, card); break;
        case SP_APPEARANCE:    render_appearance(c, card); break;
        case SP_SETTING_UP:    render_setting_up(c, card); break;
        case SP_DONE:          render_done(c, card); break;
        default: break;
    }
}

int setup_page_slots(setup_page_t p, setup_slot_t *out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
#define ADD_FIELD(fp)    do { if (n < max) { out[n] = (setup_slot_t){ SETUP_SLOT_FIELD, (fp), 0, 0, 0, 0 }; n++; } } while (0)
#define ADD_LIST(sp, c, h) do { if (n < max) { out[n] = (setup_slot_t){ SETUP_SLOT_LIST, 0, (sp), (c), (h), 0 }; n++; } } while (0)
#define ADD_TOGGLE(fp)   do { if (n < max) { out[n] = (setup_slot_t){ SETUP_SLOT_TOGGLE, 0, 0, 0, 0, (fp) }; n++; } } while (0)
    switch (p) {
        case SP_LANGUAGE: ADD_LIST(&g_setup.language, g_setup_language_count, 0); break;
        case SP_REGION:
            ADD_LIST(&g_setup.region, g_setup_region_count, 0);
            ADD_LIST(&g_setup.keyboard, g_setup_keyboard_count, 0);
            break;
        case SP_ACCESSIBILITY:
            for (int i = 0; i < SETUP_AX_COUNT; i++) ADD_TOGGLE(&g_setup.ax[i]);
            break;
        case SP_NETWORK:
            if (g_setup.net_count > 0) ADD_LIST(&g_setup.net_sel, g_setup.net_count, 0);
            if (net_needs_password()) ADD_FIELD(&g_setup.net_password);
            break;
        case SP_ACCOUNT:
            ADD_FIELD(&g_setup.full_name);
            ADD_FIELD(&g_setup.account);
            ADD_FIELD(&g_setup.password);
            ADD_FIELD(&g_setup.verify);
            ADD_FIELD(&g_setup.hint);
            break;
        case SP_KETIVEE:
            ADD_LIST(&g_setup.ketivee_mode, 2, 1);
            ADD_FIELD(&g_setup.kid_email);
            ADD_FIELD(&g_setup.kid_password);
            break;
        case SP_PRIVACY:
            for (int i = 0; i < SETUP_PRIV_COUNT; i++) ADD_TOGGLE(&g_setup.privacy[i]);
            break;
        case SP_APPEARANCE:
            ADD_LIST(&g_setup.appearance, SETUP_APPEAR_COUNT, 1);
            ADD_LIST(&g_setup.accent, g_setup_accent_count, 1);
            break;
        default: break;
    }
#undef ADD_FIELD
#undef ADD_LIST
#undef ADD_TOGGLE
    return n;
}

static int keyboard_for_region(int region) {
    if (region < 0 || region >= g_setup_region_count) return 0;
    const char *code = g_setup_regions[region].detail;
    for (int i = 0; i < g_setup_keyboard_count; i++) {
        const char *a = g_setup_keyboards[i].code, *b = code;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return i;
    }
    return 0;
}

void setup_page_changed(setup_page_t p, int slot) {
    switch (p) {
        case SP_LANGUAGE:
            setup_log2("[SETUP] language selected: ", g_setup_languages[g_setup.language].code);
            break;
        case SP_REGION:
            if (slot == 0) {
                if (!g_setup.keyboard_user_set) g_setup.keyboard = keyboard_for_region(g_setup.region);
                setup_log2("[SETUP] region selected: ", g_setup_regions[g_setup.region].code);
            } else {
                g_setup.keyboard_user_set = 1;
                setup_log2("[SETUP] keyboard selected: ", g_setup_keyboards[g_setup.keyboard].code);
            }
            break;
        case SP_ACCESSIBILITY:
            if (slot >= 0 && slot < SETUP_AX_COUNT)
                setup_log2(g_setup.ax[slot] ? "[SETUP] accessibility on: " : "[SETUP] accessibility off: ",
                           k_ax_titles[slot]);
            break;
        case SP_PRIVACY:
            if (slot >= 0 && slot < SETUP_PRIV_COUNT)
                setup_log2(g_setup.privacy[slot] ? "[SETUP] privacy on: " : "[SETUP] privacy off: ",
                           k_priv_titles[slot]);
            break;
        case SP_NETWORK:
            g_setup.net_error = 0;
            if (slot == 0) setup_field_clear(&g_setup.net_password);   /* new network: drop old secret */
            break;
        case SP_ACCOUNT:
            if (slot == 0 && !g_setup.account_edited) {
                char derived[SETUP_FIELD_CAP];
                setup_derive_account(g_setup.full_name.buf, derived, sizeof(derived));
                setup_field_set(&g_setup.account, derived);
            } else if (slot == 1) {
                g_setup.account_edited = g_setup.account.len > 0;
            }
            break;
        case SP_KETIVEE:
            if (slot == 0) {
                g_setup.ketivee_attempted = 0;
                setup_log2("[SETUP] ketivee mode: ", g_setup.ketivee_mode ? "create" : "sign in");
            }
            break;
        case SP_APPEARANCE:
            if (slot == 0) apply_appearance();
            else setup_log2("[SETUP] accent selected: ", g_setup_accents[g_setup.accent].name);
            break;
        default: break;
    }
}

void setup_page_enter(setup_page_t p) {
    for (int i = 0; i < SETUP_MAX_SLOTS; i++) g_toggle_t[i] = 0.0f;
    if (p == SP_ACCESSIBILITY)
        for (int i = 0; i < SETUP_AX_COUNT; i++) g_toggle_t[i] = g_setup.ax[i] ? 1.0f : 0.0f;
    if (p == SP_PRIVACY)
        for (int i = 0; i < SETUP_PRIV_COUNT; i++) g_toggle_t[i] = g_setup.privacy[i] ? 1.0f : 0.0f;
    if (p == SP_NETWORK && g_setup.net_count == 0) {
        int n = setup_network_scan(g_setup.nets, SETUP_NET_MAX);
        g_setup.net_count = n;            /* < 0 keeps the error code */
        if (n <= 0) g_setup.net_sel = -1;
    }
    if (p == SP_SETTING_UP) {
        g_run.t = 0.0f;
        g_run.next = 0;
        g_run.done_at = -1.0f;
        for (int i = 0; i < SETUP_TASKS; i++) g_run.state[i] = TASK_PENDING;
        setup_log("[SETUP] applying setup\n");
    }
}

int setup_page_validate(setup_page_t p) {
    switch (p) {
        case SP_NETWORK: {
            int s = g_setup.net_sel;
            if (s < 0 || s >= g_setup.net_count || s == g_setup.net_joined) return 1;
            int need_pw = net_needs_password();
            if (need_pw && g_setup.net_password.len == 0) {
                g_setup.net_error = "Enter the password for this network.";
                return 0;
            }
            int rc = setup_network_join(g_setup.nets[s].ssid, need_pw ? g_setup.net_password.buf : 0,
                                        need_pw ? g_setup.net_password.len : 0);
            setup_field_clear(&g_setup.net_password);
            if (rc != SETUP_OK) {
                g_setup.net_error = rc == SETUP_ERR_UNSUPPORTED
                    ? "Joining networks isn't available yet. Choose Set Up Later."
                    : "Couldn't join this network. Check the password and try again.";
                return 0;
            }
            g_setup.net_joined = s;
            g_setup.net_error = 0;
            return 1;
        }
        case SP_ACCOUNT: {
            g_setup.account_attempted = 1;
            account_errors_t e = account_errors();
            int ok = !e.full && !e.account && !e.password && !e.verify && !e.hint;
            setup_log(ok ? "[SETUP] account form valid\n" : "[SETUP] account form has errors\n");
            return ok;
        }
        case SP_KETIVEE: {
            if (g_setup.kid_email.len == 0 && g_setup.kid_password.len == 0) {
                g_setup.ketivee_pending = 0;
                setup_log("[SETUP] ketivee id: skipped\n");
                return 1;
            }
            g_setup.ketivee_attempted = 1;
            if (setup_check_email(g_setup.kid_email.buf)) return 0;
            if (g_setup.kid_password.len == 0) return 0;
            if (g_setup.ketivee_mode == 1 && g_setup.kid_password.len < 8) return 0;
            /* No Ketivee ID service is reachable during setup: keep only the
             * e-mail as a pending link and drop the password now. */
            setup_field_clear(&g_setup.kid_password);
            g_setup.ketivee_pending = 1;
            setup_log("[SETUP] ketivee id: entered, finishes online (password discarded)\n");
            return 1;
        }
        default:
            return 1;
    }
}

const char *setup_page_continue_label(setup_page_t p) {
    switch (p) {
        case SP_WELCOME:    return "Get Started";
        case SP_NETWORK:
            if (g_setup.net_count <= 0) return "Set Up Later";
            if (g_setup.net_sel >= 0 && g_setup.net_sel != g_setup.net_joined) return "Join";
            return "Continue";
        case SP_KETIVEE:
            if (g_setup.kid_email.len == 0 && g_setup.kid_password.len == 0) return "Continue";
            return g_setup.ketivee_mode ? "Create ID" : "Sign In";
        case SP_APPEARANCE: return "Set Up";
        case SP_SETTING_UP: return 0;
        case SP_DONE:       return "Start Using NeolyxOS";
        default:            return "Continue";
    }
}

const char *setup_page_secondary_label(setup_page_t p) {
    if (p == SP_NETWORK && g_setup.net_count > 0 && g_setup.net_joined < 0) return "Set Up Later";
    if (p == SP_KETIVEE) return "Set Up Later";
    return 0;
}

int setup_page_secondary(setup_page_t p) {
    if (p == SP_NETWORK) {
        g_setup.net_sel = -1;
        g_setup.net_error = 0;
        setup_field_clear(&g_setup.net_password);
        setup_log("[SETUP] network: set up later\n");
        return 1;
    }
    if (p == SP_KETIVEE) {
        setup_field_clear(&g_setup.kid_email);
        setup_field_clear(&g_setup.kid_password);
        g_setup.ketivee_pending = 0;
        g_setup.ketivee_attempted = 0;
        setup_log("[SETUP] ketivee id: set up later\n");
        return 1;
    }
    return 0;
}

int setup_page_tick(setup_page_t p, float dt) {
    if (p != SP_SETTING_UP) return 0;
    g_run.t += dt;
    const float step = 0.6f;
    if (g_run.next < SETUP_TASKS) {
        int cur = g_run.next;
        if (g_run.state[cur] == TASK_PENDING && g_run.t >= 0.35f + step * (float)cur) {
            g_run.state[cur] = TASK_RUNNING;
        } else if (g_run.state[cur] == TASK_RUNNING && g_run.t >= 0.35f + step * (float)cur + 0.4f) {
            run_task(cur);
            g_run.next++;
            if (g_run.next == SETUP_TASKS) g_run.done_at = g_run.t;
        }
        return 0;
    }
    return g_run.done_at >= 0.0f && g_run.t - g_run.done_at > 0.8f;
}
