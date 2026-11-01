/*
 * NeolyxOS - NXCanvas rounded-clip span tests
 *
 * Every window body, card and panel is drawn inside nxc_clip_rrect. A span
 * that reaches the rounded clip's edge evaluates the clip's coverage only
 * near that edge; the run between (where the coverage is exactly 255)
 * takes the plain path. These checks pin that down:
 *   - interior pixels equal the same drawing without the clip, bit for bit;
 *   - pixels outside the rounded rect are never written;
 *   - edge pixels lie between the background and the fill;
 *   - a stress image (solid, translucent, shape coverage, image and
 *     gradient spans, global alpha, fractional clips, radii up to half the
 *     size) prints a hash, so an A/B against an older engine can show the
 *     output is unchanged.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_internal.h"

#define TW 120
#define TH 90
#define COLS 6
#define ROWS 5

typedef struct { float x, y, w, h, rad; } clip_case_t;

static const clip_case_t k_cases[] = {
    { 10.0f, 10.0f, 100.0f, 70.0f, 12.0f },     /* window-like, integer */
    { 10.5f, 9.25f, 99.0f, 70.5f, 12.0f },      /* fractional edges */
    { 4.0f, 4.0f, 112.0f, 82.0f, 0.5f },        /* tiny radius */
    { 20.0f, 15.0f, 80.0f, 60.0f, 30.0f },      /* radius = half height */
    { 30.0f, 20.0f, 60.0f, 50.0f, 40.0f },      /* radius above half: clamped by caller */
    { 2.3f, 3.7f, 115.1f, 83.3f, 7.9f },
    { 50.0f, 10.0f, 3.0f, 70.0f, 1.5f },        /* thin column */
    { 10.0f, 40.0f, 100.0f, 2.0f, 1.0f },       /* thin row */
    { -20.0f, -10.0f, 100.0f, 80.0f, 16.0f },   /* hanging off the tile */
    { 60.0f, 45.0f, 100.0f, 80.0f, 16.0f },
};
#define NCASES ((int)(sizeof(k_cases) / sizeof(k_cases[0])))

static void fill_background(nxc_surface_t *s) {
    for (int32_t y = 0; y < s->height; y++) {
        for (int32_t x = 0; x < s->width; x++) {
            uint32_t r = (uint32_t)(x * 7 + y * 3) & 0xFFu, g = (uint32_t)(x * 2 + y * 5) & 0xFFu;
            uint32_t b = (uint32_t)((x ^ y) * 11) & 0xFFu;
            s->pixels[(int64_t)y * s->stride + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

/* One drawing operation inside the current clip, tile origin (ox, oy). */
static void draw_op(nxc_canvas_t *c, int op, float ox, float oy, const nxc_surface_t *img) {
    nxc_rect_t all = nxc_rect(ox, oy, (float)TW, (float)TH);
    switch (op) {
        case 0: NXC_CHECK_OK(nxc_fill_rect(c, all, NXC_RGB(0xE8, 0x40, 0x20))); break;
        case 1: NXC_CHECK_OK(nxc_fill_rect(c, all, NXC_ARGB(140, 0x20, 0xA0, 0xF0))); break;
        case 2: NXC_CHECK_OK(nxc_fill_rrect(c, nxc_rect(ox + 6.0f, oy + 5.0f, TW - 12.0f, TH - 10.0f), 9.0f,
                                            NXC_RGB(0xF0, 0xF0, 0xF0))); break;
        case 3: {
            nxc_paint_t p = nxc_paint_linear(ox, oy, ox + TW, oy + TH, NXC_RGB(0x10, 0x20, 0xC0),
                                             NXC_ARGB(200, 0xF0, 0xD0, 0x30));
            NXC_CHECK_OK(nxc_fill_rect_paint(c, all, &p));
            break;
        }
        case 4:
            NXC_CHECK_OK(nxc_draw_surface(c, img, nxc_irect(0, 0, img->width, img->height), all, 255,
                                          NXC_FILTER_NEAREST));
            break;
        default:
            nxc_save(c);
            nxc_set_alpha(c, 128);
            NXC_CHECK_OK(nxc_fill_rect(c, all, NXC_RGB(0x30, 0xF0, 0x60)));
            nxc_restore(c);
            break;
    }
}

#define NOPS 6

static uint64_t fnv(const nxc_surface_t *s) {
    uint64_t h = 1469598103934665603ull;
    for (int32_t y = 0; y < s->height; y++) {
        for (int32_t x = 0; x < s->width; x++) {
            uint32_t p = s->pixels[(int64_t)y * s->stride + x];
            for (int k = 0; k < 4; k++) { h ^= (p >> (8 * k)) & 0xFFu; h *= 1099511628211ull; }
        }
    }
    return h;
}

/* Interior equals the unclipped drawing, outside is untouched. */
static void test_interior_and_outside(const nxc_surface_t *img) {
    nxc_surface_t a, b, bg;
    NXC_CHECK_OK(nxc_surface_create(&a, TW, TH, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&b, TW, TH, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&bg, TW, TH, NXC_SURFACE_OPAQUE));
    fill_background(&bg);
    int bad_in = 0, bad_out = 0, bad_mid = 0, checked = 0;
    for (int ci = 0; ci < NCASES; ci++) {
        clip_case_t k = k_cases[ci];
        float rad = k.rad;
        float maxr = 0.5f * (k.w < k.h ? k.w : k.h);
        if (rad > maxr) rad = maxr;
        for (int op = 0; op < NOPS; op++) {
            nxc_canvas_t ca, cb;
            fill_background(&a);
            fill_background(&b);
            NXC_CHECK_OK(nxc_canvas_init(&ca, &a));
            NXC_CHECK_OK(nxc_canvas_init(&cb, &b));
            NXC_CHECK_OK(nxc_clip_rrect(&ca, nxc_rect(k.x, k.y, k.w, k.h), rad));
            draw_op(&ca, op, 0.0f, 0.0f, img);
            draw_op(&cb, op, 0.0f, 0.0f, img);      /* no clip */
            for (int32_t y = 0; y < TH; y++) {
                for (int32_t x = 0; x < TW; x++) {
                    float px = (float)x + 0.5f, py = (float)y + 0.5f;
                    /* distance outside / inside the rounded rect (double, independent) */
                    double hx = 0.5 * k.w, hy = 0.5 * k.h;
                    double qx = __builtin_fabs(px - (k.x + hx)) - (hx - rad);
                    double qy = __builtin_fabs(py - (k.y + hy)) - (hy - rad);
                    double sd = (qx > 0 && qy > 0) ? __builtin_sqrt(qx * qx + qy * qy) - rad
                                                   : (qx > qy ? qx : qy) - rad;
                    uint32_t pa = a.pixels[(int64_t)y * a.stride + x];
                    uint32_t pb = b.pixels[(int64_t)y * b.stride + x];
                    uint32_t p0 = bg.pixels[(int64_t)y * bg.stride + x];
                    checked++;
                    if (sd <= -0.75) {
                        if (pa != pb) bad_in++;
                    } else if (sd >= 0.75) {
                        if (pa != p0) bad_out++;
                    } else {
                        /* each channel between background and fill */
                        for (int sh = 0; sh < 24; sh += 8) {
                            int va = (int)((pa >> sh) & 0xFFu), v0 = (int)((p0 >> sh) & 0xFFu);
                            int vb = (int)((pb >> sh) & 0xFFu);
                            int lo = v0 < vb ? v0 : vb, hi = v0 < vb ? vb : v0;
                            if (va < lo - 1 || va > hi + 1) { bad_mid++; break; }
                        }
                    }
                }
            }
        }
    }
    fprintf(stderr, "rclip: %d pixels checked, interior mismatches %d, outside writes %d, "
            "edge out of range %d\n", checked, bad_in, bad_out, bad_mid);
    NXC_CHECK(bad_in == 0);
    NXC_CHECK(bad_out == 0);
    NXC_CHECK(bad_mid == 0);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
    nxc_surface_destroy(&bg);
}

/* All cases and operations in one image; prints its hash for A/B runs. */
static void test_stress_image(const nxc_surface_t *img) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, TW * COLS, TH * ROWS * 2, NXC_SURFACE_OPAQUE));
    fill_background(&s);
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    int tile = 0;
    for (int ci = 0; ci < NCASES; ci++) {
        clip_case_t k = k_cases[ci];
        float maxr = 0.5f * (k.w < k.h ? k.w : k.h);
        float rad = k.rad > maxr ? maxr : k.rad;
        for (int op = 0; op < NOPS; op++, tile++) {
            float ox = (float)((tile % COLS) * TW), oy = (float)((tile / COLS) * TH);
            nxc_save(&c);
            NXC_CHECK_OK(nxc_clip_rect(&c, nxc_irect((int32_t)ox, (int32_t)oy, TW, TH)));
            NXC_CHECK_OK(nxc_clip_rrect(&c, nxc_rect(ox + k.x, oy + k.y, k.w, k.h), rad));
            draw_op(&c, op, ox, oy, img);
            nxc_restore(&c);
        }
    }
    uint64_t h = fnv(&s);
    fprintf(stderr, "rclip stress image hash %016llx\n", (unsigned long long)h);
    NXC_CHECK(nxc_test_write_png(&s, nxc_test_path("rclip_stress.png")) == 0);
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(1024, 1024);
    nxc_surface_t img;
    NXC_CHECK_OK(nxc_surface_create(&img, 40, 30, 0));
    for (int32_t y = 0; y < img.height; y++) {
        for (int32_t x = 0; x < img.width; x++) {
            uint32_t a = (uint32_t)((x * 9 + y * 4) % 256);
            uint32_t v = (uint32_t)((x * 6) & 0xFF);
            /* premultiplied: channels <= alpha */
            uint32_t r = v * a / 255u, g = (255u - v) * a / 255u, b = (a / 2u);
            img.pixels[(int64_t)y * img.stride + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    test_interior_and_outside(&img);
    test_stress_image(&img);
    nxc_surface_destroy(&img);
    return nxc_test_finish("rclip");
}
