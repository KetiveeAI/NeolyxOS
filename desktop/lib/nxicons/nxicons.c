/*
 * NeolyxOS - NXIcons: system symbols
 *
 * Every symbol is designed on a 24 x 24 grid centered at the origin
 * (coordinates -12..12) with a 2-unit stroke, round caps and round joins,
 * then scaled to the requested size. Geometry is original; see
 * nxicons.h for the catalogue.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../../include/nxicons.h"
#include "nxicons_internal.h"

/* ============ Drawing context ============ */

/* Scaled drawing frame: unit u maps to device cx + u * k. */
typedef struct {
    nxc_canvas_t *c;
    float cx, cy, k;
    float sw;                 /* stroke width in px */
    nxc_color_t color;
    nxc_status_t st;          /* first error */
    nxc_point_t pbuf[96];
    uint8_t vbuf[96];
    nxc_path_t path;
} sym_ctx_t;

static void sym_note(sym_ctx_t *s, nxc_status_t st) {
    if (s->st == NXC_OK && st != NXC_OK) s->st = st;
}

static inline float X(const sym_ctx_t *s, float u) { return s->cx + u * s->k; }
static inline float Y(const sym_ctx_t *s, float v) { return s->cy + v * s->k; }

static void path_begin(sym_ctx_t *s) {
    nxc_path_init(&s->path, s->pbuf, 96, s->vbuf, 96);
}

static void pm(sym_ctx_t *s, float x, float y) { nxc_path_move_to(&s->path, X(s, x), Y(s, y)); }
static void pl(sym_ctx_t *s, float x, float y) { nxc_path_line_to(&s->path, X(s, x), Y(s, y)); }
static void pc(sym_ctx_t *s, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    nxc_path_cubic_to(&s->path, X(s, c1x), Y(s, c1y), X(s, c2x), Y(s, c2y), X(s, x), Y(s, y));
}
static void pz(sym_ctx_t *s) { nxc_path_close(&s->path); }

static void stroke_path(sym_ctx_t *s, float width_units, nxc_color_t col) {
    nxc_stroke_t st;
    st.width = width_units * s->k;
    if (st.width < 1.0f) st.width = 1.0f;
    st.cap = NXC_CAP_ROUND;
    st.join = NXC_JOIN_ROUND;
    st.miter_limit = 4.0f;
    sym_note(s, nxc_stroke_path(s->c, &s->path, &st, col));
}

static void fill_path(sym_ctx_t *s, nxc_color_t col, nxc_fill_rule_t rule) {
    sym_note(s, nxc_fill_path(s->c, &s->path, rule, col));
}

/* Stroked line between two grid points. */
static void line(sym_ctx_t *s, float x0, float y0, float x1, float y1, nxc_color_t col) {
    sym_note(s, nxc_draw_line(s->c, X(s, x0), Y(s, y0), X(s, x1), Y(s, y1), s->sw, col));
}

static void dot(sym_ctx_t *s, float x, float y, float r, nxc_color_t col) {
    sym_note(s, nxc_fill_circle(s->c, X(s, x), Y(s, y), r * s->k, col));
}

static void ring(sym_ctx_t *s, float x, float y, float r, float w, nxc_color_t col) {
    float sw = w * s->k;
    if (sw < 1.0f) sw = 1.0f;
    sym_note(s, nxc_stroke_circle(s->c, X(s, x), Y(s, y), r * s->k, sw, col));
}

static void rrect_fill(sym_ctx_t *s, float x, float y, float w, float h, float r, nxc_color_t col) {
    sym_note(s, nxc_fill_rrect(s->c, nxc_rect(X(s, x), Y(s, y), w * s->k, h * s->k), r * s->k, col));
}

static void rrect_stroke(sym_ctx_t *s, float x, float y, float w, float h, float r, nxc_color_t col) {
    path_begin(s);
    nxc_path_add_rrect(&s->path, nxc_rect(X(s, x), Y(s, y), w * s->k, h * s->k), r * s->k);
    stroke_path(s, 2.0f, col);
}

/* Stroked arc of radius r around (x, y) from angle a0 to a1 (radians,
 * screen clockwise). */
static void arc(sym_ctx_t *s, float x, float y, float r, float a0, float a1, float w, nxc_color_t col) {
    path_begin(s);
    nxc_path_add_arc(&s->path, X(s, x), Y(s, y), r * s->k, a0, a1);
    stroke_path(s, w, col);
}

static nxc_color_t dim(nxc_color_t col, float k) { return nxc_color_fade(col, k); }

#define DEG(d) ((d) * (NXC_PI / 180.0f))

/* ============ Symbols ============ */

static void sym_wifi(sym_ctx_t *s, float value, int off) {
    nxc_color_t col = s->color;
    int lit = value <= 0.0f ? 0 : value < 0.34f ? 1 : value < 0.67f ? 2 : 3;
    if (value < 0.0f) lit = 3;                 /* negative = unknown: all on */
    /* Arcs open upward: from -135 to -45 degrees around a point low in the box. */
    const float cy = 8.0f;
    const float radii[3] = { 5.5f, 10.0f, 14.5f };
    for (int i = 0; i < 3; i++) {
        nxc_color_t c = (i < lit || off) ? col : dim(col, 0.3f);
        arc(s, 0.0f, cy, radii[i], DEG(-132), DEG(-48), 2.0f, c);
    }
    dot(s, 0.0f, cy - 1.2f, 1.8f, col);
    if (off) {
        /* Slash: drawn with a thin gap so the strokes read separately. */
        line(s, -9.0f, -9.0f, 9.0f, 9.0f, col);
    }
}

static void sym_ethernet(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    rrect_stroke(s, -3.5f, -10.5f, 7.0f, 5.5f, 1.2f, col);
    rrect_stroke(s, -11.0f, 4.0f, 7.0f, 5.5f, 1.2f, col);
    rrect_stroke(s, 4.0f, 4.0f, 7.0f, 5.5f, 1.2f, col);
    path_begin(s);
    pm(s, 0.0f, -5.0f); pl(s, 0.0f, -1.0f);
    pm(s, -7.5f, 4.0f); pl(s, -7.5f, -1.0f); pl(s, 7.5f, -1.0f); pl(s, 7.5f, 4.0f);
    stroke_path(s, 2.0f, col);
}

static void sym_bluetooth(sym_ctx_t *s) {
    path_begin(s);
    pm(s, -5.5f, -5.5f); pl(s, 5.0f, 5.0f); pl(s, 0.0f, 10.5f); pl(s, 0.0f, -10.5f);
    pl(s, 5.0f, -5.0f); pl(s, -5.5f, 5.5f);
    stroke_path(s, 2.0f, s->color);
}

static void sym_battery(sym_ctx_t *s, float value, int charging) {
    nxc_color_t col = s->color;
    rrect_stroke(s, -10.5f, -5.5f, 18.0f, 11.0f, 2.5f, col);
    rrect_fill(s, 8.5f, -2.0f, 2.5f, 4.0f, 1.0f, col);
    if (value < 0.0f) value = 1.0f;
    if (value > 1.0f) value = 1.0f;
    float w = 14.0f * value;
    if (w > 0.6f) rrect_fill(s, -8.5f, -3.5f, w, 7.0f, 1.2f, col);
    if (charging) {
        /* Lightning bolt punched in the opposite color of the fill. */
        nxc_color_t bolt = (value > 0.45f) ? NXC_WITH_ALPHA(col ^ 0x00FFFFFFu, 255) : col;
        path_begin(s);
        pm(s, 0.5f, -5.0f); pl(s, -3.5f, 0.8f); pl(s, -0.5f, 0.8f); pl(s, -1.5f, 5.0f);
        pl(s, 3.0f, -0.8f); pl(s, 0.0f, -0.8f); pz(s);
        fill_path(s, bolt, NXC_FILL_NONZERO);
    }
}

static void sym_volume(sym_ctx_t *s, float value, int mute) {
    nxc_color_t col = s->color;
    path_begin(s);
    pm(s, -11.0f, -3.5f); pl(s, -6.5f, -3.5f); pl(s, -1.5f, -8.5f); pl(s, -1.5f, 8.5f);
    pl(s, -6.5f, 3.5f); pl(s, -11.0f, 3.5f); pz(s);
    fill_path(s, col, NXC_FILL_NONZERO);
    stroke_path(s, 1.2f, col);          /* rounds the corners */
    if (mute) {
        line(s, 4.0f, -3.5f, 10.0f, 3.5f, col);
        line(s, 10.0f, -3.5f, 4.0f, 3.5f, col);
        return;
    }
    int waves = value < 0.0f ? 2 : value <= 0.0f ? 0 : value < 0.5f ? 1 : 2;
    if (waves >= 1) arc(s, -1.5f, 0.0f, 5.5f, DEG(-42), DEG(42), 2.0f, col);
    else arc(s, -1.5f, 0.0f, 5.5f, DEG(-42), DEG(42), 2.0f, dim(col, 0.3f));
    if (waves >= 2) arc(s, -1.5f, 0.0f, 10.0f, DEG(-42), DEG(42), 2.0f, col);
    else arc(s, -1.5f, 0.0f, 10.0f, DEG(-42), DEG(42), 2.0f, dim(col, 0.3f));
}

static void sym_rays(sym_ctx_t *s, float r0, float r1, nxc_color_t col) {
    for (int i = 0; i < 8; i++) {
        float a = DEG(45.0f * (float)i);
        float ca = nxc_cosf_pub(a), sa = nxc_sinf_pub(a);
        line(s, ca * r0, sa * r0, ca * r1, sa * r1, col);
    }
}

static void sym_brightness(sym_ctx_t *s, float value) {
    nxc_color_t col = s->color;
    if (value < 0.0f) value = 1.0f;
    ring(s, 0.0f, 0.0f, 4.5f, 2.0f, col);
    float len = 1.2f + 2.0f * (value > 1.0f ? 1.0f : value);
    sym_rays(s, 8.0f, 8.0f + len, col);
}

static void sym_sun(sym_ctx_t *s) {
    dot(s, 0.0f, 0.0f, 4.8f, s->color);
    sym_rays(s, 7.8f, 10.5f, s->color);
}

static void sym_moon(sym_ctx_t *s) {
    /* Crescent: the outer disc minus a disc shifted up-right, as a single
     * closed path of two arcs through the intersection points. */
    const float R = 9.5f, r = 8.2f, ox = 4.6f, oy = -3.6f;
    float d2 = ox * ox + oy * oy, d = nxc_sqrtf_pub(d2);
    float a = (R * R - r * r + d2) / (2.0f * d);
    float h2 = R * R - a * a;
    float h = h2 > 0.0f ? nxc_sqrtf_pub(h2) : 0.0f;
    float mx = a * ox / d, my = a * oy / d;
    float p1x = mx + h * (-oy) / d, p1y = my + h * ox / d;
    float p2x = mx - h * (-oy) / d, p2y = my - h * ox / d;
    float ang1 = nxc_atan2f_pub(p1y, p1x), ang2 = nxc_atan2f_pub(p2y, p2x);
    /* Outer arc the long way round (away from the inner center). */
    float away = nxc_atan2f_pub(-oy, -ox);
    float sweep = ang2 - ang1;
    while (sweep <= 0.0f) sweep += 2.0f * NXC_PI;
    float mid = ang1 + 0.5f * sweep;
    float dm = mid - away;
    while (dm > NXC_PI) dm -= 2.0f * NXC_PI;
    while (dm < -NXC_PI) dm += 2.0f * NXC_PI;
    if (dm > NXC_PI * 0.5f || dm < -NXC_PI * 0.5f) { float t = ang1; ang1 = ang2; ang2 = t; sweep = 2.0f * NXC_PI - sweep; }
    path_begin(s);
    nxc_path_add_arc(&s->path, X(s, 0), Y(s, 0), R * s->k, ang1, ang1 + sweep);
    /* Inner arc back from p2 to p1 around the shifted center. */
    float b2 = nxc_atan2f_pub(p2y - oy, p2x - ox), b1 = nxc_atan2f_pub(p1y - oy, p1x - ox);
    float back = b1 - b2;
    while (back > NXC_PI) back -= 2.0f * NXC_PI;
    while (back < -NXC_PI) back += 2.0f * NXC_PI;
    nxc_path_add_arc(&s->path, X(s, ox), Y(s, oy), r * s->k, b2, b2 + back);
    pz(s);
    fill_path(s, s->color, NXC_FILL_NONZERO);
}

static void sym_search(sym_ctx_t *s) {
    ring(s, -2.2f, -2.2f, 6.3f, 2.0f, s->color);
    line(s, 2.6f, 2.6f, 8.8f, 8.8f, s->color);
}

static void sym_control_center(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    rrect_stroke(s, -10.5f, -8.5f, 21.0f, 7.5f, 3.75f, col);
    dot(s, -6.75f, -4.75f, 2.1f, col);
    rrect_stroke(s, -10.5f, 1.0f, 21.0f, 7.5f, 3.75f, col);
    dot(s, 6.75f, 4.75f, 2.1f, col);
}

static void sym_bell(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    path_begin(s);
    pm(s, -8.5f, 5.0f);
    pc(s, -8.5f, -1.5f, -6.5f, -8.5f, 0.0f, -8.5f);
    pc(s, 6.5f, -8.5f, 8.5f, -1.5f, 8.5f, 5.0f);
    pl(s, 10.0f, 7.0f); pl(s, -10.0f, 7.0f); pz(s);
    stroke_path(s, 2.0f, col);
    dot(s, 0.0f, -9.3f, 1.3f, col);
    arc(s, 0.0f, 7.0f, 3.0f, DEG(20), DEG(160), 2.0f, col);
}

static void sym_grid(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    const float a = -10.0f, b = 1.5f, w = 8.5f, r = 2.2f;
    rrect_fill(s, a, a, w, w, r, col);
    rrect_fill(s, b, a, w, w, r, col);
    rrect_fill(s, a, b, w, w, r, col);
    rrect_fill(s, b, b, w, w, r, col);
}

static void sym_chevron(sym_ctx_t *s, int dir) {
    path_begin(s);
    switch (dir) {
        case 0: pm(s, 3.5f, -7.0f); pl(s, -3.5f, 0.0f); pl(s, 3.5f, 7.0f); break;   /* left */
        case 1: pm(s, -3.5f, -7.0f); pl(s, 3.5f, 0.0f); pl(s, -3.5f, 7.0f); break;  /* right */
        case 2: pm(s, -7.0f, 3.5f); pl(s, 0.0f, -3.5f); pl(s, 7.0f, 3.5f); break;   /* up */
        default: pm(s, -7.0f, -3.5f); pl(s, 0.0f, 3.5f); pl(s, 7.0f, -3.5f); break; /* down */
    }
    stroke_path(s, 2.2f, s->color);
}

static void sym_close(sym_ctx_t *s) {
    line(s, -6.0f, -6.0f, 6.0f, 6.0f, s->color);
    line(s, 6.0f, -6.0f, -6.0f, 6.0f, s->color);
}

static void sym_minimize(sym_ctx_t *s) { line(s, -7.0f, 0.0f, 7.0f, 0.0f, s->color); }

static void sym_maximize(sym_ctx_t *s) {
    /* Two opposing corner arrows */
    path_begin(s);
    pm(s, -8.0f, -2.5f); pl(s, -8.0f, -8.0f); pl(s, -2.5f, -8.0f);
    pm(s, 8.0f, 2.5f); pl(s, 8.0f, 8.0f); pl(s, 2.5f, 8.0f);
    pm(s, -8.0f, -8.0f); pl(s, -2.0f, -2.0f);
    pm(s, 8.0f, 8.0f); pl(s, 2.0f, 2.0f);
    stroke_path(s, 2.0f, s->color);
}

static void sym_plus(sym_ctx_t *s) {
    line(s, -7.0f, 0.0f, 7.0f, 0.0f, s->color);
    line(s, 0.0f, -7.0f, 0.0f, 7.0f, s->color);
}

static void sym_check(sym_ctx_t *s) {
    path_begin(s);
    pm(s, -8.0f, 0.5f); pl(s, -2.5f, 6.0f); pl(s, 8.5f, -6.0f);
    stroke_path(s, 2.4f, s->color);
}

static void sym_gear(sym_ctx_t *s) {
    /* Eight teeth: alternating outer (10.5) and inner (8.0) radius points,
     * with the tooth flanks angled; the hub is cut with even-odd. */
    const int teeth = 8;
    const float ro = 10.5f, ri = 8.0f;
    path_begin(s);
    for (int i = 0; i < teeth; i++) {
        float a = (float)i * 2.0f * NXC_PI / (float)teeth;
        float step = 2.0f * NXC_PI / (float)teeth;
        float a0 = a - step * 0.22f, a1 = a + step * 0.22f;     /* tooth top */
        float b0 = a - step * 0.34f, b1 = a + step * 0.34f;     /* tooth base */
        float x;
        x = nxc_cosf_pub(b0) * ri; float y = nxc_sinf_pub(b0) * ri;
        if (i == 0) pm(s, x, y); else pl(s, x, y);
        pl(s, nxc_cosf_pub(a0) * ro, nxc_sinf_pub(a0) * ro);
        pl(s, nxc_cosf_pub(a1) * ro, nxc_sinf_pub(a1) * ro);
        pl(s, nxc_cosf_pub(b1) * ri, nxc_sinf_pub(b1) * ri);
    }
    pz(s);
    nxc_path_add_circle(&s->path, X(s, 0), Y(s, 0), 3.4f * s->k);
    fill_path(s, s->color, NXC_FILL_EVENODD);
    /* A thin round-joined stroke softens the tooth corners. */
    path_begin(s);
    for (int i = 0; i < teeth; i++) {
        float a = (float)i * 2.0f * NXC_PI / (float)teeth;
        float step = 2.0f * NXC_PI / (float)teeth;
        float a0 = a - step * 0.22f, a1 = a + step * 0.22f;
        float b0 = a - step * 0.34f, b1 = a + step * 0.34f;
        float x = nxc_cosf_pub(b0) * ri, y = nxc_sinf_pub(b0) * ri;
        if (i == 0) pm(s, x, y); else pl(s, x, y);
        pl(s, nxc_cosf_pub(a0) * ro, nxc_sinf_pub(a0) * ro);
        pl(s, nxc_cosf_pub(a1) * ro, nxc_sinf_pub(a1) * ro);
        pl(s, nxc_cosf_pub(b1) * ri, nxc_sinf_pub(b1) * ri);
    }
    pz(s);
    stroke_path(s, 1.4f, s->color);
}

static void sym_power(sym_ctx_t *s) {
    arc(s, 0.0f, 0.5f, 8.5f, DEG(-55), DEG(235), 2.0f, s->color);
    line(s, 0.0f, -10.5f, 0.0f, -1.5f, s->color);
}

static void sym_lock(sym_ctx_t *s, int open) {
    nxc_color_t col = s->color;
    rrect_fill(s, -8.0f, -1.0f, 16.0f, 11.5f, 2.6f, col);
    /* Keyhole knocked out in a contrasting tone: draw as a dark dot. */
    dot(s, 0.0f, 4.0f, 1.7f, NXC_WITH_ALPHA(col ^ 0x00FFFFFFu, 200));
    path_begin(s);
    if (!open) {
        pm(s, -5.0f, -1.0f); pl(s, -5.0f, -5.0f);
        pc(s, -5.0f, -8.0f, -2.8f, -10.0f, 0.0f, -10.0f);
        pc(s, 2.8f, -10.0f, 5.0f, -8.0f, 5.0f, -5.0f);
        pl(s, 5.0f, -1.0f);
    } else {
        pm(s, -5.0f, -1.0f); pl(s, -5.0f, -5.0f);
        pc(s, -5.0f, -8.0f, -2.8f, -10.0f, 0.0f, -10.0f);
        pc(s, 2.8f, -10.0f, 5.0f, -8.0f, 5.0f, -5.0f);
        pl(s, 5.0f, -4.0f);
    }
    stroke_path(s, 2.0f, col);
}

static void sym_user(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    dot(s, 0.0f, -4.5f, 4.6f, col);
    path_begin(s);
    pm(s, -9.5f, 10.5f);
    pc(s, -9.5f, 4.0f, -5.5f, 1.8f, 0.0f, 1.8f);
    pc(s, 5.5f, 1.8f, 9.5f, 4.0f, 9.5f, 10.5f);
    pz(s);
    fill_path(s, col, NXC_FILL_NONZERO);
}

static void sym_keyboard(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    rrect_stroke(s, -11.0f, -6.5f, 22.0f, 13.0f, 2.2f, col);
    for (int i = 0; i < 5; i++) rrect_fill(s, -7.8f + 3.6f * (float)i - 0.8f, -3.3f, 1.8f, 1.8f, 0.5f, col);
    for (int i = 0; i < 4; i++) rrect_fill(s, -6.0f + 3.6f * (float)i - 0.8f, 0.0f, 1.8f, 1.8f, 0.5f, col);
    rrect_fill(s, -5.0f, 3.3f, 10.0f, 1.6f, 0.6f, col);
}

static void sym_airplane(sym_ctx_t *s) {
    path_begin(s);
    pm(s, 0.0f, -11.0f);
    pc(s, 1.6f, -11.0f, 2.2f, -8.0f, 2.2f, -5.0f);
    pl(s, 2.2f, -3.5f); pl(s, 11.0f, 2.5f); pl(s, 11.0f, 5.0f); pl(s, 2.2f, 2.0f);
    pl(s, 2.2f, 6.5f); pl(s, 5.0f, 8.5f); pl(s, 5.0f, 10.5f); pl(s, 0.0f, 9.0f);
    pl(s, -5.0f, 10.5f); pl(s, -5.0f, 8.5f); pl(s, -2.2f, 6.5f); pl(s, -2.2f, 2.0f);
    pl(s, -11.0f, 5.0f); pl(s, -11.0f, 2.5f); pl(s, -2.2f, -3.5f); pl(s, -2.2f, -5.0f);
    pc(s, -2.2f, -8.0f, -1.6f, -11.0f, 0.0f, -11.0f);
    pz(s);
    fill_path(s, s->color, NXC_FILL_NONZERO);
}

static void sym_shield(sym_ctx_t *s) {
    path_begin(s);
    pm(s, 0.0f, -10.5f);
    pc(s, 3.5f, -8.5f, 7.0f, -7.5f, 10.0f, -7.5f);
    pc(s, 10.0f, 2.0f, 6.5f, 8.0f, 0.0f, 11.0f);
    pc(s, -6.5f, 8.0f, -10.0f, 2.0f, -10.0f, -7.5f);
    pc(s, -7.0f, -7.5f, -3.5f, -8.5f, 0.0f, -10.5f);
    pz(s);
    stroke_path(s, 2.0f, s->color);
    path_begin(s);
    pm(s, -4.0f, 0.5f); pl(s, -1.2f, 3.3f); pl(s, 4.5f, -2.8f);
    stroke_path(s, 2.0f, s->color);
}

static void sym_play(sym_ctx_t *s) {
    path_begin(s);
    pm(s, -6.5f, -8.5f); pl(s, 8.5f, 0.0f); pl(s, -6.5f, 8.5f); pz(s);
    fill_path(s, s->color, NXC_FILL_NONZERO);
    stroke_path(s, 1.6f, s->color);
}

static void sym_pause(sym_ctx_t *s) {
    rrect_fill(s, -7.5f, -8.5f, 5.0f, 17.0f, 1.6f, s->color);
    rrect_fill(s, 2.5f, -8.5f, 5.0f, 17.0f, 1.6f, s->color);
}

static void sym_skip(sym_ctx_t *s, int next) {
    float m = next ? 1.0f : -1.0f;
    path_begin(s);
    pm(s, -8.5f * m, -7.5f); pl(s, 3.0f * m, 0.0f); pl(s, -8.5f * m, 7.5f); pz(s);
    fill_path(s, s->color, NXC_FILL_NONZERO);
    stroke_path(s, 1.4f, s->color);
    rrect_fill(s, next ? 5.0f : -8.0f, -7.5f, 3.0f, 15.0f, 1.2f, s->color);
}

static void sym_folder(sym_ctx_t *s) {
    path_begin(s);
    pm(s, -11.0f, 8.5f);
    pl(s, -11.0f, -6.0f);
    pc(s, -11.0f, -7.2f, -10.2f, -8.0f, -9.0f, -8.0f);
    pl(s, -4.0f, -8.0f);
    pl(s, -1.0f, -5.0f);
    pl(s, 9.0f, -5.0f);
    pc(s, 10.2f, -5.0f, 11.0f, -4.2f, 11.0f, -3.0f);
    pl(s, 11.0f, 8.5f);
    pz(s);
    stroke_path(s, 2.0f, s->color);
    line(s, -11.0f, -1.0f, 11.0f, -1.0f, s->color);
}

static void sym_document(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    path_begin(s);
    pm(s, -7.5f, -10.0f); pl(s, 2.0f, -10.0f); pl(s, 7.5f, -4.5f); pl(s, 7.5f, 10.0f);
    pl(s, -7.5f, 10.0f); pz(s);
    stroke_path(s, 2.0f, col);
    path_begin(s);
    pm(s, 2.0f, -10.0f); pl(s, 2.0f, -4.5f); pl(s, 7.5f, -4.5f);
    stroke_path(s, 2.0f, col);
    line(s, -3.5f, 0.5f, 3.5f, 0.5f, col);
    line(s, -3.5f, 4.5f, 3.5f, 4.5f, col);
}

static void sym_trash(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    line(s, -9.5f, -6.5f, 9.5f, -6.5f, col);
    path_begin(s);
    pm(s, -3.0f, -6.5f); pl(s, -3.0f, -9.5f); pl(s, 3.0f, -9.5f); pl(s, 3.0f, -6.5f);
    stroke_path(s, 2.0f, col);
    path_begin(s);
    pm(s, -7.5f, -6.5f); pl(s, -6.5f, 10.0f); pl(s, 6.5f, 10.0f); pl(s, 7.5f, -6.5f);
    stroke_path(s, 2.0f, col);
    line(s, -2.5f, -2.5f, -2.0f, 6.5f, col);
    line(s, 2.5f, -2.5f, 2.0f, 6.5f, col);
}

static void sym_clock(sym_ctx_t *s) {
    ring(s, 0.0f, 0.0f, 9.5f, 2.0f, s->color);
    line(s, 0.0f, 0.0f, 0.0f, -5.5f, s->color);
    line(s, 0.0f, 0.0f, 4.0f, 2.5f, s->color);
}

static void sym_warning(sym_ctx_t *s) {
    path_begin(s);
    pm(s, 0.0f, -9.5f); pl(s, 10.5f, 8.5f); pl(s, -10.5f, 8.5f); pz(s);
    stroke_path(s, 2.0f, s->color);
    line(s, 0.0f, -3.0f, 0.0f, 2.0f, s->color);
    dot(s, 0.0f, 5.3f, 1.3f, s->color);
}

static void sym_info(sym_ctx_t *s) {
    ring(s, 0.0f, 0.0f, 9.5f, 2.0f, s->color);
    dot(s, 0.0f, -4.5f, 1.4f, s->color);
    line(s, 0.0f, -1.0f, 0.0f, 5.0f, s->color);
}

static void sym_mic(sym_ctx_t *s) {
    nxc_color_t col = s->color;
    rrect_fill(s, -3.5f, -10.5f, 7.0f, 12.5f, 3.5f, col);
    arc(s, 0.0f, -1.0f, 7.0f, DEG(10), DEG(170), 2.0f, col);
    line(s, 0.0f, 6.0f, 0.0f, 9.5f, col);
    line(s, -4.0f, 9.5f, 4.0f, 9.5f, col);
}

/* ============ Public API ============ */

static const char *const g_sym_names[NXSYM_COUNT] = {
    "wifi", "wifi-off", "ethernet", "bluetooth", "battery", "battery-charging",
    "volume", "volume-mute", "brightness", "moon", "sun", "search", "control-center",
    "bell", "grid", "chevron-left", "chevron-right", "chevron-up", "chevron-down",
    "close", "minimize", "maximize", "plus", "check", "gear", "power", "lock", "unlock",
    "user", "keyboard", "airplane", "shield", "play", "pause", "next", "prev", "folder",
    "document", "trash", "clock", "warning", "info", "mic",
};

const char *nxsym_name(nxsym_t sym) {
    if ((int)sym < 0 || sym >= NXSYM_COUNT) return "invalid";
    return g_sym_names[sym];
}

nxc_status_t nxsym_draw(nxc_canvas_t *c, nxsym_t sym, float cx, float cy, float size,
                        nxc_color_t color, float value) {
    if (!c || (int)sym < 0 || sym >= NXSYM_COUNT || !nxi__finite(cx) || !nxi__finite(cy) ||
        !nxi__finite(size) || size < 2.0f || size > 4096.0f || !nxi__finite(value)) {
        return NXC_ERR_INVALID_ARG;
    }
    if ((color >> 24) == 0) return NXC_OK;
    sym_ctx_t s;
    s.c = c;
    s.cx = cx;
    s.cy = cy;
    s.k = size / 24.0f;
    s.sw = 2.0f * s.k;
    if (s.sw < 1.0f) s.sw = 1.0f;
    s.color = color;
    s.st = NXC_OK;
    switch (sym) {
        case NXSYM_WIFI:             sym_wifi(&s, value, 0); break;
        case NXSYM_WIFI_OFF:         sym_wifi(&s, -1.0f, 1); break;
        case NXSYM_ETHERNET:         sym_ethernet(&s); break;
        case NXSYM_BLUETOOTH:        sym_bluetooth(&s); break;
        case NXSYM_BATTERY:          sym_battery(&s, value, 0); break;
        case NXSYM_BATTERY_CHARGING: sym_battery(&s, value, 1); break;
        case NXSYM_VOLUME:           sym_volume(&s, value, 0); break;
        case NXSYM_VOLUME_MUTE:      sym_volume(&s, 0.0f, 1); break;
        case NXSYM_BRIGHTNESS:       sym_brightness(&s, value); break;
        case NXSYM_MOON:             sym_moon(&s); break;
        case NXSYM_SUN:              sym_sun(&s); break;
        case NXSYM_SEARCH:           sym_search(&s); break;
        case NXSYM_CONTROL_CENTER:   sym_control_center(&s); break;
        case NXSYM_BELL:             sym_bell(&s); break;
        case NXSYM_GRID:             sym_grid(&s); break;
        case NXSYM_CHEVRON_LEFT:     sym_chevron(&s, 0); break;
        case NXSYM_CHEVRON_RIGHT:    sym_chevron(&s, 1); break;
        case NXSYM_CHEVRON_UP:       sym_chevron(&s, 2); break;
        case NXSYM_CHEVRON_DOWN:     sym_chevron(&s, 3); break;
        case NXSYM_CLOSE:            sym_close(&s); break;
        case NXSYM_MINIMIZE:         sym_minimize(&s); break;
        case NXSYM_MAXIMIZE:         sym_maximize(&s); break;
        case NXSYM_PLUS:             sym_plus(&s); break;
        case NXSYM_CHECK:            sym_check(&s); break;
        case NXSYM_GEAR:             sym_gear(&s); break;
        case NXSYM_POWER:            sym_power(&s); break;
        case NXSYM_LOCK:             sym_lock(&s, 0); break;
        case NXSYM_UNLOCK:           sym_lock(&s, 1); break;
        case NXSYM_USER:             sym_user(&s); break;
        case NXSYM_KEYBOARD:         sym_keyboard(&s); break;
        case NXSYM_AIRPLANE:         sym_airplane(&s); break;
        case NXSYM_SHIELD:           sym_shield(&s); break;
        case NXSYM_PLAY:             sym_play(&s); break;
        case NXSYM_PAUSE:            sym_pause(&s); break;
        case NXSYM_NEXT:             sym_skip(&s, 1); break;
        case NXSYM_PREV:             sym_skip(&s, 0); break;
        case NXSYM_FOLDER:           sym_folder(&s); break;
        case NXSYM_DOCUMENT:         sym_document(&s); break;
        case NXSYM_TRASH:            sym_trash(&s); break;
        case NXSYM_CLOCK:            sym_clock(&s); break;
        case NXSYM_WARNING:          sym_warning(&s); break;
        case NXSYM_INFO:             sym_info(&s); break;
        case NXSYM_MIC:              sym_mic(&s); break;
        default: return NXC_ERR_INVALID_ARG;
    }
    return s.st;
}
