/*
 * NeolyxOS - NXCanvas shape tests: exact rect coverage, rounded rects,
 * circles, ring strokes, lines, paints, clipping, global alpha and
 * argument validation. Reference coverages are computed here in double
 * precision by direct numerical integration, independently of the engine.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_internal.h"
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============ Helpers ============ */

static uint32_t alpha_at(const nxc_surface_t *s, int x, int y) {
    return s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] >> 24;
}

static void clear_surface(nxc_surface_t *s) {
    for (int y = 0; y < s->height; y++) {
        memset(s->pixels + (size_t)y * (size_t)s->stride, 0, (size_t)s->width * 4u);
    }
}

static double alpha_sum(const nxc_surface_t *s) {
    double sum = 0.0;
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) sum += (double)alpha_at(s, x, y);
    return sum / 255.0;
}

static int surface_blank(const nxc_surface_t *s) {
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++)
            if (s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] != 0u) return 0;
    return 1;
}

static int byte_of(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 1.0) return 255;
    return (int)(v * 255.0 + 0.5);
}

/* Exact coverage of pixel (px, py) by the rect [x0, x1) x [y0, y1). */
static double ref_rect(double x0, double y0, double x1, double y1, int px, int py) {
    double ox = fmin(x1, px + 1.0) - fmax(x0, (double)px);
    double oy = fmin(y1, py + 1.0) - fmax(y0, (double)py);
    return (ox > 0 && oy > 0) ? ox * oy : 0.0;
}

/* Coverage of pixel (px, py) by a rounded rect, by midpoint integration of
 * the shape's vertical extent across the pixel column. */
static double ref_rrect(double x0, double y0, double x1, double y1, double r, int px, int py) {
    const int n = 2000;
    double cl = x0 + r, cr = x1 - r, acc = 0.0;
    for (int i = 0; i < n; i++) {
        double t = px + (i + 0.5) / n;
        if (t < x0 || t > x1) continue;
        double dx = (cl - t > 0) ? cl - t : ((t - cr > 0) ? t - cr : 0.0);
        double inset = 0.0;
        if (dx > 0) {
            double q = r * r - dx * dx;
            inset = r - (q > 0 ? sqrt(q) : 0.0);
        }
        double lo = fmax(y0 + inset, (double)py), hi = fmin(y1 - inset, py + 1.0);
        if (hi > lo) acc += hi - lo;
    }
    return acc / n;
}

static double ref_disk(double cx, double cy, double r, int px, int py) {
    return ref_rrect(cx - r, cy - r, cx + r, cy + r, r, px, py);
}

/* Intersect [*a, *b] with {y : lo <= c0 + c1 * y <= hi}. */
static int lin_interval(double c0, double c1, double lo, double hi, double *a, double *b) {
    if (fabs(c1) < 1e-15) return c0 >= lo && c0 <= hi;
    double y0 = (lo - c0) / c1, y1 = (hi - c0) / c1;
    if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }
    *a = fmax(*a, y0);
    *b = fmin(*b, y1);
    return *a <= *b;
}

/* Exact-area reference for a round-capped line (capsule): the capsule is
 * convex, so each vertical line meets it in one interval, the hull of the
 * intervals of the body and the two end disks. */
static double ref_capsule(double ax, double ay, double bx, double by, double hw, int px, int py) {
    const int n = 1000;
    double dx = bx - ax, dy = by - ay, len = sqrt(dx * dx + dy * dy);
    double ux = dx / len, uy = dy / len, acc = 0.0;
    for (int i = 0; i < n; i++) {
        double t = px + (i + 0.5) / n;
        double lo = 1e300, hi = -1e300;
        double a = -1e300, b = 1e300;
        if (lin_interval((t - ax) * ux - ay * uy, uy, 0.0, len, &a, &b) &&
            lin_interval(-(t - ax) * uy - ay * ux, ux, -hw, hw, &a, &b)) {
            lo = fmin(lo, a);
            hi = fmax(hi, b);
        }
        double ex[2] = { ax, bx }, ey[2] = { ay, by };
        for (int k = 0; k < 2; k++) {
            double q = hw * hw - (t - ex[k]) * (t - ex[k]);
            if (q >= 0) {
                double s = sqrt(q);
                lo = fmin(lo, ey[k] - s);
                hi = fmax(hi, ey[k] + s);
            }
        }
        double l2 = fmax(lo, (double)py), h2 = fmin(hi, py + 1.0);
        if (h2 > l2) acc += h2 - l2;
    }
    return acc / n;
}

/* Copy region (x, y, w, h) of src scaled up by k with nearest filtering. */
static void write_zoom(const nxc_surface_t *src, int x, int y, int w, int h, int k, const char *name) {
    nxc_surface_t z;
    if (nxc_surface_create(&z, w * k, h * k, 0) != NXC_OK) return;
    for (int j = 0; j < h * k; j++)
        for (int i = 0; i < w * k; i++) {
            int sx = x + i / k, sy = y + j / k;
            uint32_t p = 0;
            if (sx >= 0 && sy >= 0 && sx < src->width && sy < src->height)
                p = src->pixels[(size_t)sy * (size_t)src->stride + (size_t)sx];
            z.pixels[(size_t)j * (size_t)z.stride + (size_t)i] = p;
        }
    nxc_test_write_png(&z, nxc_test_path(name));
    nxc_surface_destroy(&z);
}

/* ============ Validation ============ */

static void test_validation(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 64, 64, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_rect_t ok = nxc_rect(4, 4, 20, 20);
    nxc_paint_t solid = nxc_paint_solid(NXC_RGB(255, 0, 0));
    const float nan = NAN, inf = INFINITY;
    const nxc_color_t col = NXC_RGB(255, 255, 255);

    /* NULL canvas / paint */
    NXC_CHECK(nxc_fill_rect(NULL, ok, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rect_paint(NULL, ok, &solid) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rect_paint(&c, ok, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect(NULL, ok, 4, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect_paint(&c, ok, 4, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_rrect(NULL, ok, 4, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle(NULL, 10, 10, 5, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle_paint(&c, 10, 10, 5, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_circle(NULL, 10, 10, 5, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_line(NULL, 0, 0, 10, 10, 1, col) == NXC_ERR_INVALID_ARG);

    /* NaN / inf / negative sizes */
    uint32_t errs = nxc_error_count();
    NXC_CHECK(nxc_fill_rect(&c, nxc_rect(nan, 0, 4, 4), col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_error_count() == errs + 1);
    NXC_CHECK(nxc_fill_rect(&c, nxc_rect(0, inf, 4, 4), col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rect(&c, nxc_rect(0, 0, -1, 4), col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rect(&c, nxc_rect(0, 0, 4, nan), col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rect_paint(&c, nxc_rect(0, 0, inf, 4), &solid) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect(&c, ok, nan, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect(&c, ok, -1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect(&c, ok, inf, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_rrect_paint(&c, nxc_rect(0, 0, -2, 2), 1, &solid) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_rrect(&c, ok, 4, nan, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_rrect(&c, ok, 4, -1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_rrect(&c, ok, -4, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle(&c, nan, 10, 5, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle(&c, 10, 10, -5, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle(&c, 10, 10, inf, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_fill_circle_paint(&c, 10, inf, 5, &solid) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_circle(&c, 10, 10, 5, -1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_stroke_circle(&c, 10, 10, nan, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_line(&c, 0, nan, 10, 10, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_line(&c, 0, 0, inf, 10, 1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_line(&c, 0, 0, 10, 10, -1, col) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_line(&c, 0, 0, 10, 10, nan, col) == NXC_ERR_INVALID_ARG);

    /* Radii beyond the supported range (after clamping) */
    NXC_CHECK(nxc_fill_circle(&c, 10, 10, 2.0e6f, col) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxc_fill_circle_paint(&c, 10, 10, 2.0e6f, &solid) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxc_stroke_circle(&c, 10, 10, 1.0e6f, 4.0f, col) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxc_draw_line(&c, 0, 0, 10, 10, 3.0e6f, col) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxc_fill_rrect(&c, nxc_rect(0, 0, 1e8f, 1e8f), 1e8f, col) == NXC_ERR_UNSUPPORTED);
    NXC_CHECK(nxc_stroke_rrect(&c, nxc_rect(0, 0, 1e8f, 1e8f), 1e8f, 1, col) == NXC_ERR_UNSUPPORTED);
    /* ...but a huge radius on a small rect is just clamped. */
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(70, 70, 10, 10), 1e30f, col));

    /* Bad paints are rejected by the paint preparation. */
    nxc_paint_t bad = nxc_paint_linear(0, 0, 10, 0, col, col);
    bad.nstops = 1;
    NXC_CHECK(nxc_fill_rect_paint(&c, ok, &bad) == NXC_ERR_INVALID_ARG);
    bad = nxc_paint_radial(5, 5, -3, col, col);
    NXC_CHECK(nxc_fill_circle_paint(&c, 10, 10, 5, &bad) == NXC_ERR_INVALID_ARG);
    bad = nxc_paint_solid(col);
    bad.type = (nxc_paint_type_t)99;
    NXC_CHECK(nxc_fill_rrect_paint(&c, ok, 3, &bad) == NXC_ERR_INVALID_ARG);

    /* Broken canvas */
    nxc_canvas_t broken = c;
    broken.target = NULL;
    NXC_CHECK(nxc_fill_rect(&broken, ok, col) == NXC_ERR_INVALID_ARG);
    broken = c;
    broken.st.tx = nan;
    NXC_CHECK(nxc_fill_circle(&broken, 10, 10, 4, col) == NXC_ERR_INVALID_ARG);

    /* Failing draws drew nothing. */
    NXC_CHECK(surface_blank(&s));

    /* Empty-but-valid draws succeed and draw nothing. */
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(5, 5, 0, 10), col));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(5, 5, 10, 0), col));
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(5, 5, 0, 0), 3, col));
    NXC_CHECK_OK(nxc_stroke_rrect(&c, ok, 3, 0, col));
    NXC_CHECK_OK(nxc_fill_circle(&c, 20, 20, 0, col));
    NXC_CHECK_OK(nxc_stroke_circle(&c, 20, 20, 5, 0, col));
    NXC_CHECK_OK(nxc_stroke_circle(&c, 20, 20, 0, 0, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 0, 0, 30, 30, 0, col));
    NXC_CHECK_OK(nxc_fill_rect(&c, ok, 0x00FFFFFFu));                  /* alpha 0 color */
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(1e30f, 1e30f, 5, 5), col)); /* far off-screen */
    NXC_CHECK_OK(nxc_fill_circle(&c, -1e30f, 5, 5, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 1e30f, 1e30f, 2e30f, 1e30f, 4, col));
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_set_alpha(&c, 0));
    NXC_CHECK_OK(nxc_fill_rect(&c, ok, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 0, 0, 60, 60, 4, col));
    NXC_CHECK_OK(nxc_restore(&c));
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_clip_rect(&c, nxc_irect(10, 10, 0, 0)));
    NXC_CHECK_OK(nxc_fill_circle(&c, 12, 12, 8, col));
    NXC_CHECK_OK(nxc_restore(&c));
    NXC_CHECK(surface_blank(&s));
    nxc_surface_destroy(&s);
}

/* ============ Rects ============ */

static void test_rect_exact(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 64, 48, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(255, 255, 255)));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(10, 5, 20, 7), NXC_RGB(10, 20, 30)));
    NXC_CHECK_OK(nxc_translate(&c, 3, 20));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(-3, 0, 4, 3), NXC_RGB(200, 0, 0)));
    int bad = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++) {
            uint32_t want = 0xFFFFFFFFu;
            if (x >= 10 && x < 30 && y >= 5 && y < 12) want = 0xFF0A141Eu;
            if (x >= 0 && x < 4 && y >= 20 && y < 23) want = 0xFFC80000u;
            if (nxc_test_pixel(&s, x, y) != want) bad++;
        }
    NXC_CHECK(bad == 0);
    nxc_surface_destroy(&s);

    /* Alpha-only surface: integer rect is exactly 255 inside, 0 outside,
     * also with an integer rect spilling past every edge. */
    NXC_CHECK_OK(nxc_surface_create(&s, 32, 32, 0));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(-5, -7, 12, 13), NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(25, 20, 30, 40), NXC_RGB(255, 255, 255)));
    bad = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t want = ((x < 7 && y < 6) || (x >= 25 && y >= 20)) ? 255u : 0u;
            if (alpha_at(&s, x, y) != want) bad++;
        }
    NXC_CHECK(bad == 0);
    nxc_surface_destroy(&s);
}

static void test_rect_fractional(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 40, 40, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    const float rects[][4] = {
        { 3.25f, 4.5f, 10.5f, 6.75f }, { 7.9f, 7.9f, 0.2f, 0.2f }, { 1.5f, 1.0f, 0.5f, 10.0f },
        { 2.3f, 3.6f, 5.1f, 0.3f },    { 0.1f, 0.9f, 39.8f, 38.2f }, { 12.75f, 3.0f, 1.0f, 1.0f },
        { -0.5f, -0.25f, 3.0f, 2.0f }, { 38.6f, 37.3f, 5.0f, 5.0f },
    };
    const float tx[] = { 0.0f, 0.375f, -0.125f };
    int bad = 0, near = 0;
    for (size_t t = 0; t < sizeof(tx) / sizeof(tx[0]); t++) {
        for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); i++) {
            clear_surface(&s);
            NXC_CHECK_OK(nxc_canvas_init(&c, &s));
            NXC_CHECK_OK(nxc_translate(&c, tx[t], 2.0f * tx[t]));
            const float *r = rects[i];
            NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(r[0], r[1], r[2], r[3]), NXC_RGB(255, 255, 255)));
            double x0 = (double)r[0] + tx[t], y0 = (double)r[1] + 2.0 * tx[t];
            double x1 = x0 + r[2], y1 = y0 + r[3];
            for (int y = 0; y < 40; y++)
                for (int x = 0; x < 40; x++) {
                    int want = byte_of(ref_rect(x0, y0, x1, y1, x, y));
                    int got = (int)alpha_at(&s, x, y);
                    if (got != want) {
                        if (abs(got - want) > 1) bad++;
                        else near++;
                    }
                }
        }
    }
    fprintf(stderr, "rect fractional: %d mismatches > 1, %d off by one\n", bad, near);
    NXC_CHECK(bad == 0);
    NXC_CHECK(near <= 4);

    /* Coverage of one known pixel: 0.75 x 0.5 of pixel (3, 4) */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(3.25f, 4.5f, 10, 10), NXC_RGB(255, 255, 255)));
    NXC_CHECK(alpha_at(&s, 3, 4) == 96u);    /* round(0.375 * 255) */
    NXC_CHECK(alpha_at(&s, 4, 4) == 128u);   /* round(0.5 * 255) */
    NXC_CHECK(alpha_at(&s, 3, 5) == 191u);   /* round(0.75 * 255) */
    NXC_CHECK(alpha_at(&s, 4, 5) == 255u);
    nxc_surface_destroy(&s);
}

/* Shapes straddling every surface edge must not write outside it. The
 * surface is wrapped inside a larger buffer whose margin is a sentinel. */
static void test_clipping(void) {
    const int W = 50, H = 40, STRIDE = 58, ROWS = H + 6;
    uint32_t *buf = (uint32_t *)malloc((size_t)STRIDE * ROWS * 4u);
    NXC_CHECK(buf != NULL);
    if (!buf) return;
    for (int i = 0; i < STRIDE * ROWS; i++) buf[i] = 0x12345678u;
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_wrap(&s, buf + 3 * STRIDE + 4, W, H, STRIDE, 0));
    clear_surface(&s);
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    const nxc_color_t col = NXC_ARGB(200, 255, 255, 255);
    nxc_paint_t lin = nxc_paint_linear(0, 0, 50, 40, NXC_RGB(255, 0, 0), NXC_RGB(0, 0, 255));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(-3.3f, -2.7f, 10.1f, 9.9f), col));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(44.6f, 35.2f, 20, 20), col));
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(-10.5f, 15.5f, 80, 3), &lin));
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(-6.5f, 20.25f, 15, 25), 7.5f, col));
    NXC_CHECK_OK(nxc_fill_rrect_paint(&c, nxc_rect(42.3f, -5.6f, 12, 14), 6, &lin));
    NXC_CHECK_OK(nxc_stroke_rrect(&c, nxc_rect(-2.5f, -2.5f, 55, 45), 9, 1.5f, col));
    NXC_CHECK_OK(nxc_fill_circle(&c, 0.3f, 39.6f, 6.4f, col));
    NXC_CHECK_OK(nxc_fill_circle_paint(&c, 49.8f, 20, 9, &lin));
    NXC_CHECK_OK(nxc_stroke_circle(&c, 25, -1.5f, 8, 2.5f, col));
    NXC_CHECK_OK(nxc_stroke_circle(&c, 25, 41.5f, 30, 0.7f, col));
    NXC_CHECK_OK(nxc_draw_line(&c, -20, 5, 70, 35, 3, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 25, -10, 25.5f, 60, 1, col));
    NXC_CHECK_OK(nxc_draw_line(&c, -0.4f, 39.7f, 49.7f, 0.2f, 6, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 49.9f, 39.9f, 49.9f, 39.9f, 5, col));     /* dot at the corner */
    NXC_CHECK_OK(nxc_draw_line(&c, -1e30f, 22.5f, 1e30f, 22.5f, 2, col));
    NXC_CHECK_OK(nxc_draw_line(&c, -1e30f, -1e30f, 1e30f, 1e30f, 2, col));
    int guard_bad = 0;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < STRIDE; x++) {
            int inside = (y >= 3 && y < 3 + H && x >= 4 && x < 4 + W);
            if (!inside && buf[y * STRIDE + x] != 0x12345678u) guard_bad++;
        }
    NXC_CHECK(guard_bad == 0);
    nxc_test_write_png(&s, nxc_test_path("shapes_clip_edges.png"));

    /* A rect covering everything (any finite size) fills every pixel exactly. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(-1e30f, -1e30f, 2e30f, 2e30f), NXC_RGB(1, 2, 3)));
    int full_bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (nxc_test_pixel(&s, x, y) != 0xFF010203u) full_bad++;
    NXC_CHECK(full_bad == 0);
    /* An rrect with corners far off-screen also covers everything. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(-1e9f, -1e9f, 3e9f, 3e9f), 20, NXC_RGB(1, 2, 3)));
    full_bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (nxc_test_pixel(&s, x, y) != 0xFF010203u) full_bad++;
    NXC_CHECK(full_bad == 0);

    /* Canvas clip rect: nothing outside it. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_clip_rect(&c, nxc_irect(10, 8, 20, 15)));
    NXC_CHECK_OK(nxc_fill_circle(&c, 20, 15, 30, col));
    NXC_CHECK_OK(nxc_draw_line(&c, 0, 0, 50, 40, 4, col));
    NXC_CHECK_OK(nxc_restore(&c));
    int clip_bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int in = x >= 10 && x < 30 && y >= 8 && y < 23;
            uint32_t a = alpha_at(&s, x, y);
            /* Inside: the circle alone gives 200; the line over it composites higher. */
            if ((!in && a != 0) || (in && a < 200)) clip_bad++;
        }
    NXC_CHECK(clip_bad == 0);
    for (int i = 0; i < STRIDE * ROWS; i++) {
        int y = i / STRIDE, x = i % STRIDE;
        int inside = (y >= 3 && y < 3 + H && x >= 4 && x < 4 + W);
        if (!inside && buf[i] != 0x12345678u) guard_bad++;
    }
    NXC_CHECK(guard_bad == 0);
    free(buf);
}

/* ============ Rounded rects ============ */

static void check_rrect_ref(nxc_surface_t *s, nxc_canvas_t *c, float x, float y, float w, float h,
                            float radius, int *max_err) {
    clear_surface(s);
    NXC_CHECK_OK(nxc_fill_rrect(c, nxc_rect(x, y, w, h), radius, NXC_RGB(255, 255, 255)));
    double r = fmin((double)radius, 0.5 * fmin((double)w, (double)h));
    double x0 = (double)x + c->st.tx, y0 = (double)y + c->st.ty;
    for (int py = 0; py < s->height; py++)
        for (int px = 0; px < s->width; px++) {
            int want = byte_of(ref_rrect(x0, y0, x0 + w, y0 + h, r, px, py));
            int got = (int)alpha_at(s, px, py);
            int d = abs(got - want);
            if (d > *max_err) *max_err = d;
        }
}

static void test_rrect(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 72, 56, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));

    /* Per-pixel accuracy against numerical integration. */
    int max_err = 0;
    check_rrect_ref(&s, &c, 10, 8, 50, 40, 12.5f, &max_err);
    check_rrect_ref(&s, &c, 3.3f, 4.7f, 61.2f, 33.9f, 7.3f, &max_err);
    check_rrect_ref(&s, &c, 20.25f, 10.5f, 9.5f, 30.0f, 100.0f, &max_err);   /* radius > size */
    check_rrect_ref(&s, &c, 5.6f, 6.1f, 3.2f, 2.4f, 1.0f, &max_err);         /* tiny */
    check_rrect_ref(&s, &c, 30.1f, 20.9f, 0.6f, 0.8f, 0.25f, &max_err);      /* sub-pixel */
    check_rrect_ref(&s, &c, 1.5f, 1.5f, 69, 53, 0.4f, &max_err);             /* small radius */
    check_rrect_ref(&s, &c, 2, 3, 60, 1.5f, 0.75f, &max_err);                /* 1.5 px tall pill */
    NXC_CHECK_OK(nxc_translate(&c, 0.3f, -0.45f));
    check_rrect_ref(&s, &c, 12, 9, 44, 30, 9, &max_err);
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    fprintf(stderr, "rrect max |coverage - reference| = %d/255\n", max_err);
    NXC_CHECK(max_err <= 1);

    /* Mirror symmetry (exactly representable geometry -> bit-identical). */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(10, 8, 50, 40), 12.5f, NXC_RGB(255, 255, 255)));
    int asym = 0;
    for (int y = 0; y < 56; y++)
        for (int x = 0; x < 72; x++) {
            int mx = 69 - x, my = 55 - y;
            if (mx >= 0 && mx < 72 && alpha_at(&s, x, y) != alpha_at(&s, mx, y)) asym++;
            if (my >= 0 && my < 56 && alpha_at(&s, x, y) != alpha_at(&s, x, my)) asym++;
        }
    NXC_CHECK(asym == 0);
    /* Fractional but symmetric about x = 35.5, y = 27.75 (+-1 allowed). */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(7.3f, 4.1f, 56.4f, 47.3f), 9.7f, NXC_RGB(255, 255, 255)));
    double ax = 7.3f + 7.3f + 56.4f, ay = 4.1f + 4.1f + 47.3f;   /* 2 * center */
    int asym1 = 0;
    for (int y = 4; y < 52; y++)
        for (int x = 7; x < 64; x++) {
            int mx = (int)lround(ax - x - 1.0), my = (int)lround(ay - y - 1.0);
            (void)my;
            if (mx >= 0 && mx < 72 && abs((int)alpha_at(&s, x, y) - (int)alpha_at(&s, mx, y)) > 1) asym1++;
        }
    NXC_CHECK(asym1 == 0);

    /* Integer-aligned straight edges are crisp. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(10, 10, 40, 30), 6, NXC_RGB(255, 255, 255)));
    NXC_CHECK(alpha_at(&s, 25, 10) == 255u && alpha_at(&s, 25, 9) == 0u);
    NXC_CHECK(alpha_at(&s, 10, 25) == 255u && alpha_at(&s, 9, 25) == 0u);
    NXC_CHECK(alpha_at(&s, 49, 25) == 255u && alpha_at(&s, 50, 25) == 0u);
    NXC_CHECK(alpha_at(&s, 25, 39) == 255u && alpha_at(&s, 25, 40) == 0u);
    NXC_CHECK(alpha_at(&s, 10, 10) < 64u);           /* corner pixel mostly outside */
    NXC_CHECK(fabs(alpha_sum(&s) - (40.0 * 30.0 - (4.0 - M_PI) * 36.0)) < 0.5);

    /* Radius clamping: >= half the shorter side equals exactly half. */
    nxc_surface_t s2;
    NXC_CHECK_OK(nxc_surface_create(&s2, 72, 56, 0));
    nxc_canvas_t c2;
    NXC_CHECK_OK(nxc_canvas_init(&c2, &s2));
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(10.5f, 10.25f, 40, 20), 1000, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_rrect(&c2, nxc_rect(10.5f, 10.25f, 40, 20), 10, NXC_RGB(255, 255, 255)));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 72u * 56u * 4u) == 0);
    /* Square rrect with full radius equals the circle. */
    clear_surface(&s);
    clear_surface(&s2);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(20.5f, 10.25f, 30, 30), 15, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_circle(&c2, 35.5f, 25.25f, 15, NXC_RGB(255, 255, 255)));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 72u * 56u * 4u) == 0);
    /* Radius 0 equals fill_rect. */
    clear_surface(&s);
    clear_surface(&s2);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(3.3f, 4.6f, 30.2f, 20.1f), 0, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_rect(&c2, nxc_rect(3.3f, 4.6f, 30.2f, 20.1f), NXC_RGB(255, 255, 255)));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 72u * 56u * 4u) == 0);
    nxc_surface_destroy(&s2);
    nxc_surface_destroy(&s);
}

/* ============ Circles ============ */

static void test_circle(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 560, 560, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));

    /* Total coverage vs pi r^2 across radii and sub-pixel centers. */
    const float radii[] = { 0.3f, 0.75f, 1.5f, 2.0f, 3.0f, 5.3f, 10.0f, 31.7f, 100.0f, 250.0f };
    const float fr[][2] = { { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 0.13f, 0.71f }, { 0.37f, 0.06f } };
    double worst_rel = 0.0, worst_abs = 0.0;
    for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); i++)
        for (size_t k = 0; k < sizeof(fr) / sizeof(fr[0]); k++) {
            clear_surface(&s);
            float r = radii[i];
            NXC_CHECK_OK(nxc_fill_circle(&c, 280 + fr[k][0], 280 + fr[k][1], r, NXC_RGB(255, 255, 255)));
            double want = M_PI * (double)r * (double)r;
            double err = fabs(alpha_sum(&s) - want);
            if (r >= 1.5f) {
                if (err / want > worst_rel) worst_rel = err / want;
                NXC_CHECK(err <= 0.005 * want);
            } else {
                if (err > worst_abs) worst_abs = err;
                NXC_CHECK(err <= 0.02);
            }
        }
    fprintf(stderr, "circle area: worst rel err %.5f%% (r >= 1.5), worst abs err %.4f px (r < 1.5)\n",
            worst_rel * 100.0, worst_abs);
    nxc_surface_destroy(&s);

    /* Per-pixel accuracy against numerical integration. */
    NXC_CHECK_OK(nxc_surface_create(&s, 96, 96, 0));
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    const float cases[][3] = {
        { 48, 48, 1.5f }, { 47.3f, 48.8f, 3 }, { 48.5f, 48.5f, 10.25f }, { 47.71f, 48.13f, 40 },
        { 50.2f, 46.9f, 0.6f }, { -4900.37f, 48.4f, 4990.0f }, { 48.6f, -99950.3f, 100000.0f },
    };
    int max_err = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        clear_surface(&s);
        NXC_CHECK_OK(nxc_fill_circle(&c, cases[i][0], cases[i][1], cases[i][2], NXC_RGB(255, 255, 255)));
        for (int y = 0; y < 96; y++)
            for (int x = 0; x < 96; x++) {
                int want = byte_of(ref_disk(cases[i][0], cases[i][1], cases[i][2], x, y));
                int d = abs((int)alpha_at(&s, x, y) - want);
                if (d > max_err) max_err = d;
            }
    }
    fprintf(stderr, "circle max |coverage - reference| = %d/255\n", max_err);
    NXC_CHECK(max_err <= 1);

    /* Four-fold mirror symmetry is exact; the diagonal (transpose) within 1. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_fill_circle(&c, 48, 48, 20.5f, NXC_RGB(255, 255, 255)));
    int asym = 0, tasym = 0;
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            uint32_t a = alpha_at(&s, x, y);
            if (a != alpha_at(&s, 95 - x, y) || a != alpha_at(&s, x, 95 - y)) asym++;
            if (abs((int)a - (int)alpha_at(&s, y, x)) > 1) tasym++;
        }
    NXC_CHECK(asym == 0);
    NXC_CHECK(tasym == 0);
    nxc_surface_destroy(&s);
}

/* ============ Strokes ============ */

static void test_strokes(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 120, 100, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));

    /* Ring area and per-pixel accuracy for thick and hairline rings. */
    const float rings[][4] = {
        { 60.3f, 50.6f, 20.0f, 3.0f }, { 60.0f, 50.0f, 30.5f, 0.5f }, { 59.5f, 50.5f, 12.0f, 0.25f },
        { 61.17f, 49.4f, 40.0f, 1.0f }, { 60.0f, 50.0f, 1.0f, 3.0f },     /* inner radius < 0 */
    };
    int max_err = 0;
    for (size_t i = 0; i < sizeof(rings) / sizeof(rings[0]); i++) {
        clear_surface(&s);
        const float *g = rings[i];
        NXC_CHECK_OK(nxc_stroke_circle(&c, g[0], g[1], g[2], g[3], NXC_RGB(255, 255, 255)));
        double ro = g[2] + 0.5 * g[3], ri = fmax(g[2] - 0.5 * g[3], 0.0);
        double want = M_PI * (ro * ro - ri * ri);
        double got = alpha_sum(&s);
        NXC_CHECK(fabs(got - want) <= 0.005 * want + 0.05);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 120; x++) {
                double ref = ref_disk(g[0], g[1], ro, x, y) - (ri > 0 ? ref_disk(g[0], g[1], ri, x, y) : 0.0);
                int d = abs((int)alpha_at(&s, x, y) - byte_of(ref));
                if (d > max_err) max_err = d;
            }
    }
    fprintf(stderr, "stroke_circle max |coverage - reference| = %d/255\n", max_err);
    NXC_CHECK(max_err <= 1);

    /* stroke_rrect vs reference (outer minus inset inner). */
    const float rr[][6] = {
        { 10.0f, 10.0f, 100.0f, 80.0f, 12.0f, 1.0f }, { 5.5f, 6.25f, 90.3f, 70.6f, 20.0f, 4.5f },
        { 20.0f, 20.0f, 60.0f, 40.0f, 8.0f, 0.4f },   { 30.2f, 15.7f, 50.0f, 30.0f, 2.0f, 6.0f },
        { 40.0f, 40.0f, 20.0f, 10.0f, 3.0f, 7.0f },   /* width beyond half: filled */
    };
    max_err = 0;
    for (size_t i = 0; i < sizeof(rr) / sizeof(rr[0]); i++) {
        clear_surface(&s);
        const float *g = rr[i];
        NXC_CHECK_OK(nxc_stroke_rrect(&c, nxc_rect(g[0], g[1], g[2], g[3]), g[4], g[5], NXC_RGB(255, 255, 255)));
        double x0 = g[0], y0 = g[1], x1 = x0 + g[2], y1 = y0 + g[3];
        double R = fmin(g[4], 0.5 * fmin(g[2], g[3]));
        double w = g[5];
        int filled = w >= 0.5 * fmin(g[2], g[3]);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 120; x++) {
                double ref = ref_rrect(x0, y0, x1, y1, R, x, y);
                if (!filled) ref -= ref_rrect(x0 + w, y0 + w, x1 - w, y1 - w, fmax(R - w, 0.0), x, y);
                int d = abs((int)alpha_at(&s, x, y) - byte_of(ref));
                if (d > max_err) max_err = d;
            }
    }
    fprintf(stderr, "stroke_rrect max |coverage - reference| = %d/255\n", max_err);
    NXC_CHECK(max_err <= 1);

    /* Hairline border on integer bounds: constant coverage along the edges. */
    clear_surface(&s);
    NXC_CHECK_OK(nxc_stroke_rrect(&c, nxc_rect(20, 20, 60, 40), 8, 0.4f, NXC_RGB(255, 255, 255)));
    int uneven = 0;
    for (int x = 28; x < 72; x++) {
        if (alpha_at(&s, x, 20) != 102u || alpha_at(&s, x, 59) != 102u) uneven++;
        if (alpha_at(&s, x, 21) != 0u || alpha_at(&s, x, 19) != 0u) uneven++;
    }
    for (int y = 28; y < 52; y++) {
        if (alpha_at(&s, 20, y) != 102u || alpha_at(&s, 79, y) != 102u) uneven++;
    }
    NXC_CHECK(uneven == 0);
    nxc_test_write_png(&s, nxc_test_path("shapes_hairline_rrect.png"));
    nxc_surface_destroy(&s);
}

/* ============ Lines ============ */

static void test_lines(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 160, 160, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    const nxc_color_t white = NXC_RGB(255, 255, 255);

    /* Axis-aligned 1 px lines on pixel centers are crisp. */
    NXC_CHECK_OK(nxc_draw_line(&c, 10, 20.5f, 50, 20.5f, 1, white));
    NXC_CHECK_OK(nxc_draw_line(&c, 70.5f, 10, 70.5f, 60, 1, white));
    int bad = 0;
    for (int x = 10; x < 50; x++) {
        if (alpha_at(&s, x, 20) != 255u || alpha_at(&s, x, 19) != 0u || alpha_at(&s, x, 21) != 0u) bad++;
    }
    for (int y = 10; y < 60; y++) {
        if (alpha_at(&s, 70, y) != 255u || alpha_at(&s, 69, y) != 0u || alpha_at(&s, 71, y) != 0u) bad++;
    }
    NXC_CHECK(bad == 0);
    NXC_CHECK(alpha_at(&s, 9, 20) > 64u && alpha_at(&s, 9, 20) < 160u);   /* round cap */
    NXC_CHECK(alpha_at(&s, 8, 20) == 0u && alpha_at(&s, 51, 20) == 0u);

    /* Fat lines at many angles: total coverage vs capsule area, and every
     * pixel against the exact capsule. */
    double worst = 0.0;
    int body_err = 0, cap_err = 0;
    for (int k = 0; k < 24; k++) {
        double ang = k * (M_PI / 24.0) + 0.05;
        float hw = 3.0f, len = 60.0f;
        float ax = 80.0f - 0.5f * len * (float)cos(ang), ay = 80.0f - 0.5f * len * (float)sin(ang);
        float bx = 80.0f + 0.5f * len * (float)cos(ang), by = 80.0f + 0.5f * len * (float)sin(ang);
        clear_surface(&s);
        NXC_CHECK_OK(nxc_draw_line(&c, ax, ay, bx, by, 2 * hw, white));
        double L = hypot((double)bx - ax, (double)by - ay);
        double want = L * 2.0 * hw + M_PI * hw * hw;
        double rel = fabs(alpha_sum(&s) - want) / want;
        if (rel > worst) worst = rel;
        if ((k % 4) == 0) {
            double ux = ((double)bx - ax) / L, uy = ((double)by - ay) / L;
            for (int y = 40; y < 120; y++)
                for (int x = 40; x < 120; x++) {
                    int d = abs((int)alpha_at(&s, x, y) - byte_of(ref_capsule(ax, ay, bx, by, hw, x, y)));
                    double u = (x + 0.5 - ax) * ux + (y + 0.5 - ay) * uy;
                    if (u > 1.0 && u < L - 1.0) { if (d > body_err) body_err = d; }
                    else if (d > cap_err) cap_err = d;
                }
        }
    }
    fprintf(stderr, "line (width 6): worst area err %.3f%%, body max err %d/255, cap max err %d/255\n",
            worst * 100.0, body_err, cap_err);
    NXC_CHECK(worst < 0.005);
    NXC_CHECK(body_err <= 1);
    NXC_CHECK(cap_err <= 8);

    /* Hairline continuity: every column across a 30-degree 0.5 px line
     * holds the slab's vertical cross-section w / cos(angle). */
    clear_surface(&s);
    double ang = 30.0 * M_PI / 180.0;
    NXC_CHECK_OK(nxc_draw_line(&c, 10, 20, 10 + 140 * (float)cos(ang), 20 + 140 * (float)sin(ang), 0.5f, white));
    double want_col = 0.5 / cos(ang), worst_col = 0.0;
    for (int x = 20; x < 120; x++) {
        double col = 0.0;
        for (int y = 0; y < 160; y++) col += alpha_at(&s, x, y) / 255.0;
        double e = fabs(col - want_col) / want_col;
        if (e > worst_col) worst_col = e;
    }
    fprintf(stderr, "hairline 0.5px @30deg: worst column cross-section err %.2f%%\n", worst_col * 100.0);
    NXC_CHECK(worst_col < 0.02);
    nxc_test_write_png(&s, nxc_test_path("shapes_hairline_line.png"));

    /* Thin lines across all angles keep their cross-section (no gaps). */
    for (int k = 0; k < 36; k++) {
        double a = k * (M_PI / 36.0) + 0.013;
        clear_surface(&s);
        float x0 = 80 - 70 * (float)cos(a), y0 = 80 - 70 * (float)sin(a);
        float x1 = 80 + 70 * (float)cos(a), y1 = 80 + 70 * (float)sin(a);
        NXC_CHECK_OK(nxc_draw_line(&c, x0, y0, x1, y1, 0.75f, white));
        double L = hypot((double)x1 - x0, (double)y1 - y0);
        double want = L * 0.75;
        double got = alpha_sum(&s);
        /* Caps add pi*hw^2 exactly; the slab cap model adds slightly more. */
        NXC_CHECK(got > want && got < want + 1.0);
    }

    /* Zero-length line is a dot identical to a filled circle. */
    nxc_surface_t s2;
    NXC_CHECK_OK(nxc_surface_create(&s2, 160, 160, 0));
    nxc_canvas_t c2;
    NXC_CHECK_OK(nxc_canvas_init(&c2, &s2));
    clear_surface(&s);
    NXC_CHECK_OK(nxc_draw_line(&c, 40.3f, 50.6f, 40.3f, 50.6f, 7, white));
    NXC_CHECK_OK(nxc_fill_circle(&c2, 40.3f, 50.6f, 3.5f, white));
    {
        int nd = 0;
        for (int i = 0; i < 160 * 160; i++)
            if (s.pixels[i] != s2.pixels[i]) {
                if (nd < 4) fprintf(stderr, "dot/circle differ at (%d,%d): %08x vs %08x\n",
                                    i % 160, i / 160, s.pixels[i], s2.pixels[i]);
                nd++;
            }
        NXC_CHECK(nd == 0);
    }

    /* Far-away endpoints: same pixels as a line clipped by hand. */
    clear_surface(&s);
    clear_surface(&s2);
    NXC_CHECK_OK(nxc_draw_line(&c, -1e30f, 30.5f, 1e30f, 30.5f, 2, white));
    NXC_CHECK_OK(nxc_draw_line(&c2, -100, 30.5f, 300, 30.5f, 2, white));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 160u * 160u * 4u) == 0);
    clear_surface(&s);
    clear_surface(&s2);
    double a2 = 0.3;
    NXC_CHECK_OK(nxc_draw_line(&c, 20, 30, 20 + 1e20f * (float)cos(a2), 30 + 1e20f * (float)sin(a2), 3, white));
    NXC_CHECK_OK(nxc_draw_line(&c2, 20, 30, 20 + 400 * (float)cos(a2), 30 + 400 * (float)sin(a2), 3, white));
    int diff = 0;
    for (int i = 0; i < 160 * 160; i++) {
        if (abs((int)(s.pixels[i] >> 24) - (int)(s2.pixels[i] >> 24)) > 1) diff++;
    }
    NXC_CHECK(diff == 0);
    nxc_surface_destroy(&s2);
    nxc_surface_destroy(&s);
}

/* ============ Global alpha and translucency ============ */

static void test_alpha(void) {
    nxc_surface_t s, s2;
    NXC_CHECK_OK(nxc_surface_create(&s, 64, 64, 0));
    NXC_CHECK_OK(nxc_surface_create(&s2, 64, 64, 0));
    nxc_canvas_t c, c2;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_canvas_init(&c2, &s2));
    NXC_CHECK_OK(nxc_set_alpha(&c, 128));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(2, 2, 10, 10), NXC_RGB(255, 255, 255)));
    NXC_CHECK(alpha_at(&s, 5, 5) == 128u);
    NXC_CHECK_OK(nxc_fill_rrect(&c, nxc_rect(14.3f, 3.6f, 40, 30), 9, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_rrect(&c2, nxc_rect(14.3f, 3.6f, 40, 30), 9, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_draw_line(&c, 3, 50, 60, 40, 2.5f, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_draw_line(&c2, 3, 50, 60, 40, 2.5f, NXC_RGB(255, 255, 255)));
    int bad = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 14; x < 64; x++) {
            int want = (int)((alpha_at(&s2, x, y) * 128u + 127u) / 255u);
            if (abs((int)alpha_at(&s, x, y) - want) > 1) bad++;
        }
    NXC_CHECK(bad == 0);

    /* Translucent color over an opaque background blends once per pixel. */
    nxc_surface_t o;
    NXC_CHECK_OK(nxc_surface_create(&o, 32, 32, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&o, NXC_RGB(0, 0, 0)));
    nxc_canvas_t co;
    NXC_CHECK_OK(nxc_canvas_init(&co, &o));
    NXC_CHECK_OK(nxc_fill_rrect(&co, nxc_rect(4, 4, 24, 24), 6, NXC_ARGB(128, 255, 255, 255)));
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&o, 16, 16), 0xFF808080u, 1));
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&o, 16, 4), 0xFF808080u, 1));
    nxc_surface_destroy(&o);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&s2);
}

/* ============ Paints ============ */

static void test_paint(void) {
    nxc_surface_t s, s2;
    NXC_CHECK_OK(nxc_surface_create(&s, 128, 64, 0));
    NXC_CHECK_OK(nxc_surface_create(&s2, 128, 64, 0));
    nxc_canvas_t c, c2;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_canvas_init(&c2, &s2));

    /* Gradient ends match the stops; solid paint equals the color path. */
    nxc_paint_t lin = nxc_paint_linear(0, 0, 100, 0, NXC_RGB(255, 0, 0), NXC_RGB(0, 0, 255));
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(0, 0, 100, 10), &lin));
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 0, 5), 0xFFFF0000u, 4));
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 99, 5), 0xFF0000FFu, 4));
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 50, 5), 0xFF7F007Fu, 4));
    clear_surface(&s);
    nxc_paint_t sol = nxc_paint_solid(NXC_ARGB(200, 10, 200, 30));
    NXC_CHECK_OK(nxc_fill_rrect_paint(&c, nxc_rect(3.3f, 4.4f, 50, 30), 8, &sol));
    NXC_CHECK_OK(nxc_fill_rrect(&c2, nxc_rect(3.3f, 4.4f, 50, 30), 8, NXC_ARGB(200, 10, 200, 30)));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 128u * 64u * 4u) == 0);

    /* Paint variants produce the same coverage as the solid variants. */
    struct { int kind; } kinds[3] = { { 0 }, { 1 }, { 2 } };
    nxc_paint_t rad = nxc_paint_radial(60, 30, 40, NXC_RGB(255, 255, 0), NXC_RGB(0, 128, 255));
    for (int k = 0; k < 3; k++) {
        clear_surface(&s);
        clear_surface(&s2);
        if (kinds[k].kind == 0) {
            NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(5.3f, 3.7f, 100.1f, 50.6f), &lin));
            NXC_CHECK_OK(nxc_fill_rect(&c2, nxc_rect(5.3f, 3.7f, 100.1f, 50.6f), NXC_RGB(255, 255, 255)));
        } else if (kinds[k].kind == 1) {
            NXC_CHECK_OK(nxc_fill_rrect_paint(&c, nxc_rect(5.3f, 3.7f, 100.1f, 50.6f), 14.2f, &rad));
            NXC_CHECK_OK(nxc_fill_rrect(&c2, nxc_rect(5.3f, 3.7f, 100.1f, 50.6f), 14.2f, NXC_RGB(255, 255, 255)));
        } else {
            NXC_CHECK_OK(nxc_fill_circle_paint(&c, 60.4f, 31.2f, 27.7f, &rad));
            NXC_CHECK_OK(nxc_fill_circle(&c2, 60.4f, 31.2f, 27.7f, NXC_RGB(255, 255, 255)));
        }
        int bad = 0;
        for (int i = 0; i < 128 * 64; i++)
            if ((s.pixels[i] >> 24) != (s2.pixels[i] >> 24)) bad++;
        NXC_CHECK(bad == 0);
    }

    /* Paint coordinates follow the canvas translation. */
    clear_surface(&s);
    clear_surface(&s2);
    NXC_CHECK_OK(nxc_translate(&c, 20, 10));
    nxc_paint_t lin_nd = lin;
    lin_nd.dither = 0;
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(0, 0, 100, 20), &lin_nd));
    NXC_CHECK_OK(nxc_fill_rect_paint(&c2, nxc_rect(0, 0, 100, 20), &lin_nd));
    NXC_CHECK(memcmp(s.pixels + 10 * 128 + 20, s2.pixels, 100u * 4u) == 0);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&s2);
}

/*
 * Banding check for a subtle full-width gradient (#1a2238 -> #232d4a over
 * 800 px). With effective dithering the per-column average of a channel
 * over the 4x4 dither period tracks the ideal continuous value closely;
 * without it the column means jump by whole levels.
 */
static void analyze_banding(const nxc_surface_t *s, const char *label, double *max_err_out, double *rms_out) {
    const double c0[3] = { 0x1a, 0x22, 0x38 }, c1[3] = { 0x23, 0x2d, 0x4a };
    double max_err = 0.0, sq = 0.0;
    int count = 0;
    for (int x = 8; x < s->width - 8; x++) {
        double t = (x + 0.5) / (double)s->width;
        for (int ch = 0; ch < 3; ch++) {
            double sum = 0.0;
            int shift = 16 - 8 * ch;
            /* Average a 4x4 block (one dither period) centered on column x. */
            for (int y = 0; y < 16; y++)
                for (int dx = -2; dx < 2; dx++)
                    sum += (double)((nxc_test_pixel(s, x + dx, y) >> shift) & 0xFFu);
            double mean = sum / 64.0;
            double ideal = c0[ch] + (c1[ch] - c0[ch]) * t;
            double e = fabs(mean - ideal);
            if (e > max_err) max_err = e;
            sq += e * e;
            count++;
        }
    }
    *max_err_out = max_err;
    *rms_out = sqrt(sq / count);
    fprintf(stderr, "gradient banding (%s): 4x4-mean vs ideal max %.3f, rms %.3f levels\n",
            label, max_err, *rms_out);
}

static void test_gradient_banding(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 800, 96, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_paint_t g = nxc_paint_linear(0, 0, 800, 0, NXC_RGB(0x1a, 0x22, 0x38), NXC_RGB(0x23, 0x2d, 0x4a));
    double max_d, rms_d, max_n, rms_n;
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(0, 0, 800, 48), &g));
    g.dither = 0;
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(0, 48, 800, 48), &g));
    analyze_banding(&s, "dither on", &max_d, &rms_d);
    /* Shift the undithered half up for the same analysis rows. */
    nxc_surface_t lower;
    lower = s;
    lower.pixels = s.pixels + 48 * s.stride;
    lower.height = 48;
    analyze_banding(&lower, "dither off", &max_n, &rms_n);
    /* Dithering must at least not be worse than plain quantization. */
    NXC_CHECK(rms_d <= rms_n + 0.05);
    nxc_test_write_png(&s, nxc_test_path("shapes_gradient_subtle.png"));
    /* Contrast-stretched copy (x16 around the darkest value) to make
     * quantization steps and the dither pattern visible. */
    nxc_surface_t v;
    NXC_CHECK_OK(nxc_surface_create(&v, 800, 96, NXC_SURFACE_OPAQUE));
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 800; x++) {
            uint32_t p = s.pixels[y * s.stride + x], o = 0xFF000000u;
            const int base[3] = { 0x1a, 0x22, 0x38 };
            for (int ch = 0; ch < 3; ch++) {
                int shift = 16 - 8 * ch;
                int val = ((int)((p >> shift) & 0xFFu) - base[ch]) * 16 + 16;
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                o |= (uint32_t)val << shift;
            }
            v.pixels[y * v.stride + x] = o;
        }
    nxc_test_write_png(&v, nxc_test_path("shapes_gradient_subtle_x16.png"));
    nxc_surface_destroy(&v);
    nxc_surface_destroy(&s);
}

/* Rows wider than the scratch buffers (surface wider than env max_width)
 * are processed in chunks. */
static void test_wide_surface(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 3000, 12, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_paint_t lin = nxc_paint_linear(0, 0, 3000, 0, NXC_RGB(255, 0, 0), NXC_RGB(0, 0, 255));
    NXC_CHECK_OK(nxc_fill_rect_paint(&c, nxc_rect(0.5f, 0.5f, 2999, 3), &lin));
    NXC_CHECK_OK(nxc_draw_line(&c, 1, 6.2f, 2998, 6.9f, 1.3f, NXC_RGB(255, 255, 255)));
    NXC_CHECK_OK(nxc_fill_rrect_paint(&c, nxc_rect(2, 8.5f, 2996, 3.2f), 1.5f, &lin));
    NXC_CHECK(alpha_at(&s, 2900, 2) == 255u && alpha_at(&s, 2900, 0) == 128u);
    NXC_CHECK(alpha_at(&s, 2500, 6) > 0u);
    NXC_CHECK(alpha_at(&s, 2990, 10) == 255u);
    nxc_surface_destroy(&s);
}

/* ============ Showcase ============ */

static void draw_showcase(nxc_canvas_t *c, int dark) {
    nxc_color_t fg = dark ? NXC_RGB(230, 234, 245) : NXC_RGB(30, 34, 48);
    nxc_color_t panel = dark ? NXC_RGB(0x2a, 0x33, 0x52) : NXC_RGB(255, 255, 255);
    nxc_color_t border = dark ? NXC_ARGB(90, 255, 255, 255) : NXC_ARGB(60, 0, 0, 0);
    nxc_color_t accent = NXC_RGB(0x4c, 0x8d, 0xff);
    nxc_paint_t bg = dark
        ? nxc_paint_linear(0, 0, 960, 600, NXC_RGB(0x1a, 0x22, 0x38), NXC_RGB(0x23, 0x2d, 0x4a))
        : nxc_paint_linear(0, 0, 960, 600, NXC_RGB(0xee, 0xf1, 0xf6), NXC_RGB(0xd6, 0xdd, 0xe8));
    nxc_fill_rect_paint(c, nxc_rect(0, 0, 960, 600), &bg);

    /* Window with title bar controls and hairline border. */
    nxc_fill_rrect(c, nxc_rect(30, 30, 400, 250), 12, panel);
    nxc_stroke_rrect(c, nxc_rect(30, 30, 400, 250), 12, 1, border);
    nxc_fill_circle(c, 52, 50, 6, NXC_RGB(0xff, 0x5f, 0x57));
    nxc_fill_circle(c, 72, 50, 6, NXC_RGB(0xfe, 0xbc, 0x2e));
    nxc_fill_circle(c, 92, 50, 6, NXC_RGB(0x28, 0xc8, 0x40));
    nxc_fill_rect(c, nxc_rect(30, 70, 400, 1), border);
    /* Buttons and pills at fractional positions. */
    nxc_paint_t btn = nxc_paint_linear(0, 90, 0, 122, NXC_RGB(0x5a, 0x9a, 0xff), NXC_RGB(0x2f, 0x6c, 0xe6));
    nxc_fill_rrect_paint(c, nxc_rect(50.5f, 90.25f, 120, 32), 8, &btn);
    nxc_fill_rrect(c, nxc_rect(185.3f, 90.6f, 120, 32), 16, NXC_WITH_ALPHA(accent, 60));
    nxc_stroke_rrect(c, nxc_rect(185.3f, 90.6f, 120, 32), 16, 1.5f, accent);
    nxc_fill_rrect(c, nxc_rect(320, 96, 90, 20), 1000, NXC_RGB(0x28, 0xc8, 0x40));
    nxc_fill_circle(c, 400, 106, 8, NXC_RGB(255, 255, 255));
    /* Fractional rect strips. */
    for (int i = 0; i < 16; i++) {
        nxc_fill_rect(c, nxc_rect(50 + i * 22 + i * 0.125f, 140, 10.5f + i * 0.0625f, 6 + i * 0.25f), fg);
    }
    /* Circles of growing radius. */
    float cx = 55;
    const float rads[] = { 0.5f, 1, 1.5f, 2, 2.5f, 3, 4, 5, 6.5f, 8, 11, 14 };
    for (int i = 0; i < 12; i++) {
        cx += rads[i] + 6;
        nxc_fill_circle(c, cx + 0.3f * i, 200.5f, rads[i], fg);
        cx += rads[i];
    }
    /* Rings of growing width. */
    const float widths[] = { 0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 5 };
    for (int i = 0; i < 8; i++) nxc_stroke_circle(c, 70 + i * 46.3f, 250.3f, 16, widths[i], accent);

    /* Starbursts: line widths 0.5, 1 and 3. */
    const float sw[3] = { 0.5f, 1.0f, 3.0f };
    for (int b = 0; b < 3; b++) {
        float ox = 540 + b * 150, oy = 110;
        for (int k = 0; k < 32; k++) {
            double a = k * (2.0 * M_PI / 32.0) + 0.01;
            nxc_draw_line(c, ox + 12 * (float)cos(a), oy + 12 * (float)sin(a),
                          ox + 68 * (float)cos(a), oy + 68 * (float)sin(a), sw[b], fg);
        }
    }
    /* Stroked rounded rects of varying width. */
    const float bw[] = { 0.5f, 1, 1.5f, 3, 6 };
    for (int i = 0; i < 5; i++) {
        nxc_stroke_rrect(c, nxc_rect(470 + i * 95.4f, 210.5f, 80, 60), 4 + i * 4.5f, bw[i], fg);
    }
    /* Gradient shapes. */
    nxc_paint_t rad = nxc_paint_radial(150, 400, 90, NXC_RGB(0xff, 0xd8, 0x6b), NXC_RGB(0xff, 0x4f, 0x8b));
    nxc_fill_circle_paint(c, 150, 400, 90, &rad);
    nxc_paint_t lin = nxc_paint_linear(280, 320, 480, 480, NXC_RGB(0x6a, 0x5a, 0xff), NXC_RGB(0x2d, 0xd4, 0xbf));
    nxc_fill_rrect_paint(c, nxc_rect(280, 320, 200, 160), 28, &lin);
    /* Translucent overlaps with global alpha. */
    nxc_save(c);
    nxc_set_alpha(c, 170);
    nxc_fill_circle(c, 560, 380, 50, NXC_RGB(255, 60, 60));
    nxc_fill_circle(c, 610, 380, 50, NXC_RGB(60, 255, 60));
    nxc_fill_circle(c, 585, 425, 50, NXC_RGB(60, 120, 255));
    nxc_restore(c);
    /* Rounded clip containing rects and lines. */
    nxc_save(c);
    nxc_clip_rrect(c, nxc_rect(700, 320, 220, 160), 24);
    nxc_fill_rect(c, nxc_rect(690, 310, 240, 180), panel);
    for (int i = 0; i < 12; i++) {
        nxc_draw_line(c, 690, 310 + i * 18.0f, 930, 380 + i * 18.0f, 2, NXC_WITH_ALPHA(accent, 200));
    }
    nxc_restore(c);
    nxc_stroke_rrect(c, nxc_rect(700, 320, 220, 160), 24, 1, border);
    /* Nearly horizontal / vertical hairlines and fat diagonals. */
    nxc_draw_line(c, 40, 520, 920, 528, 1, fg);
    nxc_draw_line(c, 40, 540, 920, 541.5f, 0.6f, fg);
    nxc_draw_line(c, 40, 560.5f, 920, 560.5f, 1, fg);
    nxc_draw_line(c, 930.5f, 300, 931.5f, 590, 1, fg);
    nxc_draw_line(c, 520, 470, 640, 590, 12, NXC_WITH_ALPHA(fg, 180));
    nxc_draw_line(c, 650, 590, 680, 470, 7, accent);
}

static void test_showcase(void) {
    nxc_surface_t s;
    nxc_canvas_t c;
    for (int dark = 1; dark >= 0; dark--) {
        NXC_CHECK_OK(nxc_surface_create(&s, 960, 600, NXC_SURFACE_OPAQUE));
        NXC_CHECK_OK(nxc_canvas_init(&c, &s));
        uint32_t errs = nxc_error_count();
        draw_showcase(&c, dark);
        NXC_CHECK(nxc_error_count() == errs);
        nxc_test_write_png(&s, nxc_test_path(dark ? "shapes_showcase_dark.png" : "shapes_showcase_light.png"));
        if (dark) {
            write_zoom(&s, 55, 180, 230, 40, 4, "shapes_zoom_circles.png");
            write_zoom(&s, 40, 225, 380, 50, 3, "shapes_zoom_rings.png");
            write_zoom(&s, 640, 60, 120, 100, 4, "shapes_zoom_star1.png");
            write_zoom(&s, 490, 60, 100, 100, 4, "shapes_zoom_star05.png");
            write_zoom(&s, 26, 26, 100, 60, 6, "shapes_zoom_window_corner.png");
            write_zoom(&s, 180, 86, 240, 40, 4, "shapes_zoom_buttons.png");
            write_zoom(&s, 460, 200, 300, 80, 3, "shapes_zoom_strokes.png");
            write_zoom(&s, 40, 515, 200, 50, 4, "shapes_zoom_hairlines.png");
            write_zoom(&s, 45, 135, 200, 15, 6, "shapes_zoom_frac_rects.png");
        }
        nxc_surface_destroy(&s);
    }
}

/* Rough timing of a desktop-like frame (informational; sanitizers slow it). */
static void test_perf(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 1280, 800, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_paint_t bg = nxc_paint_linear(0, 0, 1280, 800, NXC_RGB(0x1a, 0x22, 0x38), NXC_RGB(0x23, 0x2d, 0x4a));
    const int frames = 5;
    clock_t t0 = clock();
    for (int f = 0; f < frames; f++) {
        nxc_fill_rect_paint(&c, nxc_rect(0, 0, 1280, 800), &bg);
        for (int i = 0; i < 6; i++) {
            nxc_rect_t w = nxc_rect(60.0f + i * 150, 50.0f + i * 70, 640, 420);
            nxc_fill_rrect(&c, w, 12, NXC_RGB(0x2a, 0x33, 0x52));
            nxc_stroke_rrect(&c, w, 12, 1, NXC_ARGB(80, 255, 255, 255));
        }
        for (int i = 0; i < 60; i++) nxc_fill_circle(&c, 40.0f + i * 20, 760, 8, NXC_RGB(200, 200, 220));
        for (int i = 0; i < 100; i++) nxc_draw_line(&c, 0, i * 8.0f, 1280, i * 8.0f + 30, 1, NXC_ARGB(60, 255, 255, 255));
    }
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / frames;
    fprintf(stderr, "perf: desktop-like 1280x800 frame %.2f ms (host, sanitizers on)\n", ms);
    nxc_surface_destroy(&s);
}

/* Drawing before init / after shutdown reports NXC_ERR_STATE. */
static void test_state(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 16, 16, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    uint32_t *px = s.pixels;
    nxc_shutdown();
    NXC_CHECK(nxc_fill_rect(&c, nxc_rect(0, 0, 4, 4), NXC_RGB(1, 1, 1)) == NXC_ERR_STATE);
    NXC_CHECK(nxc_fill_circle(&c, 5, 5, 3, NXC_RGB(1, 1, 1)) == NXC_ERR_STATE);
    NXC_CHECK(nxc_draw_line(&c, 0, 0, 5, 5, 1, NXC_RGB(1, 1, 1)) == NXC_ERR_STATE);
    int blank = 1;
    for (int i = 0; i < 256; i++) if (px[i] != 0u) blank = 0;
    NXC_CHECK(blank);
    free(px);                       /* engine is down: release the host block directly */
    nxc_test_init(1280, 800);
}

int main(void) {
    nxc_test_init(1280, 800);
    test_validation();
    test_rect_exact();
    test_rect_fractional();
    test_clipping();
    test_rrect();
    test_circle();
    test_strokes();
    test_lines();
    test_alpha();
    test_paint();
    test_gradient_banding();
    test_wide_surface();
    test_showcase();
    test_perf();
    test_state();
    nxc_shutdown();
    return nxc_test_finish("shapes");
}
