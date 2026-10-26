/*
 * NeolyxOS - NXCanvas effects tests: surface/mask blur (energy, symmetry,
 * Gaussian profile, clipping, continuity), cached rounded-rect shadows
 * (analytic accuracy, nine-slice seams, cache eviction, re-init) and
 * frosted-glass backdrops (live vs pre-blurred, saturation, grain,
 * highlight, overflow).
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
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

#define MAXW 1400
#define MAXH 1000

static uint32_t g_rng = 0x1234567u;
static uint32_t rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* Random premultiplied pixel. */
static uint32_t rnd_px(void) {
    uint32_t a = rnd() & 0xFF;
    uint32_t r = a ? rnd() % (a + 1) : 0, g = a ? rnd() % (a + 1) : 0, b = a ? rnd() % (a + 1) : 0;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void sum_channels(const nxc_surface_t *s, double out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            uint32_t p = s->pixels[(size_t)y * s->stride + x];
            for (int c = 0; c < 4; c++) out[c] += (double)((p >> (8 * c)) & 0xFF);
        }
    }
}

static int premul_ok(const nxc_surface_t *s) {
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            uint32_t p = s->pixels[(size_t)y * s->stride + x];
            uint32_t a = p >> 24;
            if (((p >> 16) & 0xFF) > a || ((p >> 8) & 0xFF) > a || (p & 0xFF) > a) return 0;
        }
    }
    return 1;
}

static int surf_equal(const nxc_surface_t *a, const nxc_surface_t *b) {
    if (a->width != b->width || a->height != b->height) return 0;
    for (int y = 0; y < a->height; y++)
        if (memcmp(a->pixels + (size_t)y * a->stride, b->pixels + (size_t)y * b->stride,
                   (size_t)a->width * 4) != 0) return 0;
    return 1;
}

static uint32_t alpha_at(const nxc_surface_t *s, int x, int y) {
    return s->pixels[(size_t)y * s->stride + x] >> 24;
}

/* Write a NEAREST-upscaled crop (k x) of s for close inspection. */
static void write_zoom(const nxc_surface_t *s, int x0, int y0, int w, int h, int k,
                       const char *name) {
    nxc_surface_t z;
    if (nxc_surface_create(&z, w * k, h * k, 0) != NXC_OK) return;
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < w * k; x++) {
            int sx = x0 + x / k, sy = y0 + y / k;
            uint32_t p = 0;
            if (sx >= 0 && sy >= 0 && sx < s->width && sy < s->height)
                p = s->pixels[(size_t)sy * s->stride + sx];
            z.pixels[(size_t)y * z.stride + x] = p;
        }
    nxc_test_write_png(&z, nxc_test_path(name));
    nxc_surface_destroy(&z);
}

/* ============ Blur ============ */

static void test_blur_basic(void) {
    nxc_surface_t s, ref;
    NXC_CHECK_OK(nxc_surface_create(&s, 200, 160, 0));
    NXC_CHECK_OK(nxc_surface_create(&ref, 200, 160, 0));
    for (int y = 40; y < 120; y++)
        for (int x = 40; x < 160; x++) s.pixels[(size_t)y * s.stride + x] = rnd_px();
    NXC_CHECK_OK(nxc_surface_copy(&ref, 0, 0, &s, nxc_irect(0, 0, 200, 160)));
    nxc_irect_t all = nxc_irect(0, 0, 200, 160);

    /* sigma <= 0 is a no-op; NaN/inf/negative sizes are rejected. */
    NXC_CHECK_OK(nxc_blur_surface(&s, all, 0.0f));
    NXC_CHECK_OK(nxc_blur_surface(&s, all, -3.0f));
    NXC_CHECK(surf_equal(&s, &ref));
    NXC_CHECK(nxc_blur_surface(&s, all, NAN) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_surface(&s, all, INFINITY) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_surface(&s, nxc_irect(0, 0, -1, 5), 2.0f) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_surface(NULL, all, 2.0f) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(surf_equal(&s, &ref));

    /* Energy is preserved (content well inside the region). */
    double before[4], after[4];
    sum_channels(&s, before);
    NXC_CHECK_OK(nxc_blur_surface(&s, all, 4.0f));
    sum_channels(&s, after);
    double worst = 0;
    for (int c = 0; c < 4; c++) {
        double e = fabs(after[c] - before[c]) / before[c];
        if (e > worst) worst = e;
    }
    fprintf(stderr, "blur energy: worst channel drift %.4f%%\n", worst * 100.0);
    NXC_CHECK(worst < 0.01);
    NXC_CHECK(premul_ok(&s));
    NXC_CHECK(!surf_equal(&s, &ref));

    /* Opaque surfaces keep alpha 0xFF. */
    nxc_surface_t o;
    NXC_CHECK_OK(nxc_surface_create(&o, 64, 64, NXC_SURFACE_OPAQUE));
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) o.pixels[y * 64 + x] = 0xFF000000u | (rnd() & 0xFFFFFF);
    NXC_CHECK_OK(nxc_blur_surface(&o, nxc_irect(0, 0, 64, 64), 3.0f));
    int bad_alpha = 0;
    for (int i = 0; i < 64 * 64; i++) bad_alpha += (o.pixels[i] >> 24) != 0xFF;
    NXC_CHECK(bad_alpha == 0);
    nxc_surface_destroy(&o);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&ref);
}

static void test_blur_symmetry(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 101, 101, 0));
    s.pixels[50 * 101 + 50] = 0xFFFFFFFFu;
    NXC_CHECK_OK(nxc_blur_surface(&s, nxc_irect(0, 0, 101, 101), 1.2f));
    int asym = 0, diag = 0;
    double total = 0;
    for (int dy = -20; dy <= 20; dy++)
        for (int dx = -20; dx <= 20; dx++) {
            uint32_t p = s.pixels[(50 + dy) * 101 + 50 + dx];
            uint32_t mx = s.pixels[(50 + dy) * 101 + 50 - dx];
            uint32_t my = s.pixels[(50 - dy) * 101 + 50 + dx];
            uint32_t tr = s.pixels[(50 + dx) * 101 + 50 + dy];
            if (p != mx || p != my) asym++;
            if (!nxc_test_color_near(p, tr, 1)) diag++;
            total += (double)(p >> 24);
        }
    uint32_t peak = alpha_at(&s, 50, 50);
    double ideal = 255.0 / (2.0 * M_PI * 1.2 * 1.2);
    fprintf(stderr, "blur impulse: peak %u (ideal %.1f), energy %.1f of 255\n", peak, ideal, total);
    NXC_CHECK(asym == 0);
    NXC_CHECK(diag == 0);
    NXC_CHECK(fabs((double)peak - ideal) <= 3.0);
    NXC_CHECK(fabs(total - 255.0) <= 255.0 * 0.05);
    nxc_surface_destroy(&s);
}

/* A blurred step must follow the Gaussian CDF, in both directions. */
static void test_blur_profile(void) {
    const float sigmas[] = { 0.6f, 1.0f, 2.5f, 6.0f, 17.0f, 40.0f };
    for (unsigned k = 0; k < sizeof(sigmas) / sizeof(sigmas[0]); k++) {
        float sg = sigmas[k];
        nxc_surface_t h, v;
        NXC_CHECK_OK(nxc_surface_create(&h, 600, 8, 0));
        NXC_CHECK_OK(nxc_surface_create(&v, 8, 600, 0));
        for (int y = 0; y < 8; y++)
            for (int x = 300; x < 600; x++) h.pixels[y * 600 + x] = 0xFFFFFFFFu;
        for (int y = 300; y < 600; y++)
            for (int x = 0; x < 8; x++) v.pixels[y * 8 + x] = 0xFFFFFFFFu;
        NXC_CHECK_OK(nxc_blur_surface(&h, nxc_irect(0, 0, 600, 8), sg));
        NXC_CHECK_OK(nxc_blur_surface(&v, nxc_irect(0, 0, 8, 600), sg));
        double err = 0, errv = 0;
        for (int i = 100; i < 500; i++) {
            double want = 255.0 * 0.5 * erfc(-((double)i + 0.5 - 300.0) / (sg * M_SQRT2));
            double e = fabs((double)alpha_at(&h, i, 4) - want);
            double ev = fabs((double)alpha_at(&v, 4, i) - want);
            if (e > err) err = e;
            if (ev > errv) errv = ev;
        }
        fprintf(stderr, "blur step sigma %5.1f: max error vs Gaussian %.2f (h) %.2f (v)\n",
                sg, err, errv);
        NXC_CHECK(err <= 3.0 && errv <= 3.0);
        nxc_surface_destroy(&h);
        nxc_surface_destroy(&v);
    }
}

static void test_blur_clip_continuity(void) {
    nxc_surface_t a, b;
    NXC_CHECK_OK(nxc_surface_create(&a, 64, 48, 0));
    NXC_CHECK_OK(nxc_surface_create(&b, 64, 48, 0));
    for (int i = 0; i < 64 * 48; i++) a.pixels[i] = rnd_px();
    NXC_CHECK_OK(nxc_surface_copy(&b, 0, 0, &a, nxc_irect(0, 0, 64, 48)));
    /* A region hanging over every edge behaves as the clipped region. */
    NXC_CHECK_OK(nxc_blur_surface(&a, nxc_irect(-100, -100, 400, 400), 5.0f));
    NXC_CHECK_OK(nxc_blur_surface(&b, nxc_irect(0, 0, 64, 48), 5.0f));
    NXC_CHECK(surf_equal(&a, &b));
    /* Outside the surface: nothing happens. Huge sigma is capped. */
    NXC_CHECK_OK(nxc_blur_surface(&a, nxc_irect(100, 100, 10, 10), 5.0f));
    NXC_CHECK(surf_equal(&a, &b));
    NXC_CHECK_OK(nxc_blur_surface(&a, nxc_irect(0, 0, 64, 48), 1.0e9f));
    NXC_CHECK(premul_ok(&a));
    /* A sub-region leaves its surroundings untouched. */
    for (int i = 0; i < 64 * 48; i++) a.pixels[i] = rnd_px();
    NXC_CHECK_OK(nxc_surface_copy(&b, 0, 0, &a, nxc_irect(0, 0, 64, 48)));
    NXC_CHECK_OK(nxc_blur_surface(&a, nxc_irect(10, 12, 20, 9), 2.0f));
    int outside_changed = 0, inside_changed = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++) {
            int in = x >= 10 && x < 30 && y >= 12 && y < 21;
            int ch = a.pixels[y * 64 + x] != b.pixels[y * 64 + x];
            if (!in && ch) outside_changed++;
            if (in && ch) inside_changed++;
        }
    NXC_CHECK(outside_changed == 0 && inside_changed > 0);

    /* Continuity: a small sigma change gives a small output change. */
    nxc_surface_t s1, s2;
    NXC_CHECK_OK(nxc_surface_create(&s1, 200, 4, 0));
    NXC_CHECK_OK(nxc_surface_create(&s2, 200, 4, 0));
    for (int y = 0; y < 4; y++)
        for (int x = 100; x < 200; x++) s1.pixels[y * 200 + x] = s2.pixels[y * 200 + x] = 0xFFFFFFFFu;
    NXC_CHECK_OK(nxc_blur_surface(&s1, nxc_irect(0, 0, 200, 4), 3.00f));
    NXC_CHECK_OK(nxc_blur_surface(&s2, nxc_irect(0, 0, 200, 4), 3.05f));
    int maxd = 0;
    for (int x = 0; x < 200; x++) {
        int d = abs((int)alpha_at(&s1, x, 1) - (int)alpha_at(&s2, x, 1));
        if (d > maxd) maxd = d;
    }
    fprintf(stderr, "blur continuity: sigma 3.00 -> 3.05 max change %d\n", maxd);
    NXC_CHECK(maxd <= 2);
    nxc_surface_destroy(&s1);
    nxc_surface_destroy(&s2);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
}

static void test_blur_mask(void) {
    enum { W = 50, H = 40, STRIDE = 64 };
    static uint8_t m[STRIDE * H];
    memset(m, 0xAB, sizeof(m));
    long before = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t v = (x >= 15 && x < 35 && y >= 12 && y < 28) ? (uint8_t)(rnd() & 0xFF) : 0;
            m[y * STRIDE + x] = v;
            before += v;
        }
    NXC_CHECK_OK(nxc_blur_mask(m, W, H, STRIDE, 2.5f));
    long after = 0;
    int pad_bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) after += m[y * STRIDE + x];
        for (int x = W; x < STRIDE; x++) pad_bad += m[y * STRIDE + x] != 0xAB;
    }
    fprintf(stderr, "mask blur energy: %ld -> %ld\n", before, after);
    NXC_CHECK(pad_bad == 0);
    NXC_CHECK(labs(after - before) <= before / 100);
    NXC_CHECK(nxc_blur_mask(m, W, H, W - 1, 2.0f) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_mask(NULL, W, H, STRIDE, 2.0f) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_mask(m, 0, H, STRIDE, 2.0f) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_blur_mask(m, W, H, STRIDE, NAN) == NXC_ERR_INVALID_ARG);
    NXC_CHECK_OK(nxc_blur_mask(m, W, H, STRIDE, 0.0f));
    /* Line-buffer capacity is reported, not overrun. */
    static uint8_t wide[16384];
    NXC_CHECK(nxc_blur_mask(wide, 16384, 1, 16384, 2.0f) == NXC_ERR_OVERFLOW);
}

/* ============ Shadows ============ */

/* 1D Gaussian-blurred box [e0, e1] seen by the pixel centered at c (the
 * pixel's own box filter included by sub-sampling). */
static double box_profile(double c, double e0, double e1, double sg) {
    double acc = 0;
    for (int k = 0; k < 16; k++) {
        double u = c - 0.5 + ((double)k + 0.5) / 16.0;
        if (sg <= 0) acc += (u >= e0 && u < e1) ? 1.0 : 0.0;
        else acc += 0.5 * (erfc(-(u - e0) / (sg * M_SQRT2)) - erfc(-(u - e1) / (sg * M_SQRT2)));
    }
    return acc / 16.0;
}

/* Draw one shadow in white onto a clear transparent surface. */
static void shadow_on(nxc_surface_t *s, nxc_rect_t r, float rad, float sg, float dx, float dy,
                      float spread) {
    nxc_canvas_t c;
    memset(s->pixels, 0, (size_t)s->stride * s->height * 4);
    NXC_CHECK_OK(nxc_canvas_init(&c, s));
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, r, rad, sg, dx, dy, spread, 0xFFFFFFFFu));
}

static void test_shadow_rect_accuracy(void) {
    struct { float x, y, w, h, sg; } cs[] = {
        {  20.0f,  20.0f,  100.0f,  60.0f,  0.0f },
        {  20.0f,  20.0f,  100.0f,  60.0f,  1.5f },
        {  30.25f, 20.5f,  100.0f,  60.0f,  4.0f },
        {  60.0f,  50.0f,  300.0f, 200.0f, 12.0f },
        { 120.3f, 110.7f,  500.0f, 300.0f, 30.0f },
        {  50.0f,  50.0f,   40.0f,  30.0f,  8.0f },
        {  40.0f,  40.0f,    6.0f,   4.0f,  3.0f },
        {  40.0f,  40.0f,    0.5f,   0.5f,  0.0f },
        { 200.0f, 200.0f,   20.0f, 600.0f, 64.0f },
    };
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 800, 1000, 0));
    for (unsigned k = 0; k < sizeof(cs) / sizeof(cs[0]); k++) {
        shadow_on(&s, nxc_rect(cs[k].x, cs[k].y, cs[k].w, cs[k].h), 0.0f, cs[k].sg, 0, 0, 0);
        double maxe = 0;
        for (int y = 0; y < s.height; y++) {
            double fy = box_profile(y + 0.5, cs[k].y, cs[k].y + cs[k].h, cs[k].sg);
            for (int x = 0; x < s.width; x++) {
                double want = 255.0 * fy * box_profile(x + 0.5, cs[k].x, cs[k].x + cs[k].w, cs[k].sg);
                double e = fabs((double)alpha_at(&s, x, y) - want);
                if (e > maxe) maxe = e;
            }
        }
        fprintf(stderr, "shadow rect %gx%g sigma %4.1f at (%g,%g): max error %.2f\n",
                cs[k].w, cs[k].h, cs[k].sg, cs[k].x, cs[k].y, maxe);
        NXC_CHECK(maxe <= 4.0);
        NXC_CHECK(premul_ok(&s));
    }
    nxc_surface_destroy(&s);
}

/* Reference rounded-rect shadow: 8x8 supersampled coverage blurred at full
 * resolution with the public mask blur. */
static void rrect_reference(uint8_t *m, int w, int h, double x0, double y0, double rw,
                            double rh, double rad, float sg) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int hit = 0;
            for (int sy = 0; sy < 8; sy++)
                for (int sx = 0; sx < 8; sx++) {
                    double px = x + (sx + 0.5) / 8.0, py = y + (sy + 0.5) / 8.0;
                    double cx = x0 + rw / 2, cy = y0 + rh / 2;
                    double qx = fabs(px - cx) - (rw / 2 - rad), qy = fabs(py - cy) - (rh / 2 - rad);
                    double ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
                    double d = sqrt(ox * ox + oy * oy) + fmin(fmax(qx, qy), 0.0) - rad;
                    hit += d <= 0;
                }
            m[y * w + x] = (uint8_t)((hit * 255 + 32) / 64);
        }
    NXC_CHECK_OK(nxc_blur_mask(m, w, h, w, sg));
}

static void test_shadow_rrect_accuracy(void) {
    struct { float x, y, w, h, rad, sg, spread; int W, H; } cs[] = {
        { 100.0f, 100.0f, 1100.0f, 750.0f, 12.0f, 30.0f, 2.0f, 1300, 950 },
        {  40.0f,  40.0f,  200.0f,  50.0f, 25.0f,  6.0f, 0.0f,  280, 130 },
        {  50.0f,  50.0f,  120.0f, 120.0f, 60.0f, 10.0f, 0.0f,  220, 220 },
        {  20.0f,  20.0f,   64.0f,  64.0f, 14.0f,  2.0f, 0.0f,  104, 104 },
        {  60.5f,  40.25f, 300.0f, 180.0f, 18.0f, 16.0f, 0.0f,  420, 260 },
        {  50.0f,  50.0f,  400.0f,  44.0f, 22.0f, 18.0f, 0.0f,  500, 150 },
    };
    for (unsigned k = 0; k < sizeof(cs) / sizeof(cs[0]); k++) {
        nxc_surface_t s;
        NXC_CHECK_OK(nxc_surface_create(&s, cs[k].W, cs[k].H, 0));
        shadow_on(&s, nxc_rect(cs[k].x, cs[k].y, cs[k].w, cs[k].h), cs[k].rad, cs[k].sg, 0, 0,
                  cs[k].spread);
        uint8_t *ref = malloc((size_t)cs[k].W * cs[k].H);
        double sp = cs[k].spread;
        rrect_reference(ref, cs[k].W, cs[k].H, cs[k].x - sp, cs[k].y - sp, cs[k].w + 2 * sp,
                        cs[k].h + 2 * sp, cs[k].rad + sp, cs[k].sg);
        int maxe = 0;
        for (int y = 0; y < cs[k].H; y++)
            for (int x = 0; x < cs[k].W; x++) {
                int e = abs((int)alpha_at(&s, x, y) - (int)ref[y * cs[k].W + x]);
                if (e > maxe) maxe = e;
            }
        fprintf(stderr, "shadow rrect %gx%g r=%g sigma %g: max diff vs full-res reference %d\n",
                cs[k].w, cs[k].h, cs[k].rad, cs[k].sg, maxe);
        NXC_CHECK(maxe <= 4);
        free(ref);
        nxc_surface_destroy(&s);
    }
}

/* Along every row and column a blurred convex shape is unimodal: values
 * rise to a maximum and then fall. A nine-slice seam or a stale cached
 * tile would break that. Returns the number of violations > tol. */
static int unimodal_violations(const nxc_surface_t *s, int tol) {
    int bad = 0;
    for (int pass = 0; pass < 2; pass++) {
        int lines = pass ? s->width : s->height, len = pass ? s->height : s->width;
        for (int l = 0; l < lines; l++) {
            int peak = 0, pi = 0;
            for (int i = 0; i < len; i++) {
                int v = (int)(pass ? alpha_at(s, l, i) : alpha_at(s, i, l));
                if (v > peak) { peak = v; pi = i; }
            }
            int prev = 0;
            for (int i = 0; i <= pi; i++) {
                int v = (int)(pass ? alpha_at(s, l, i) : alpha_at(s, i, l));
                if (v < prev - tol) bad++;
                if (v > prev) prev = v;
            }
            prev = 0;
            for (int i = len - 1; i >= pi; i--) {
                int v = (int)(pass ? alpha_at(s, l, i) : alpha_at(s, i, l));
                if (v < prev - tol) bad++;
                if (v > prev) prev = v;
            }
        }
    }
    return bad;
}

static void test_shadow_seams(void) {
    /* Sweep sizes across the short/long (full mask / nine-slice) switch. */
    const float sigmas[] = { 1.5f, 3.0f, 8.0f, 18.0f, 30.0f };
    const float radii[] = { 0.0f, 6.0f, 12.0f, 40.0f };
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 700, 500, 0));
    int total_bad = 0, cases = 0;
    for (unsigned a = 0; a < sizeof(sigmas) / sizeof(sigmas[0]); a++)
        for (unsigned b = 0; b < sizeof(radii) / sizeof(radii[0]); b++)
            for (float w = 8.0f; w < 520.0f; w = w * 1.37f + 3.0f) {
                float h = w * 0.55f + 4.0f;
                shadow_on(&s, nxc_rect(350.0f - w / 2 + 0.3f, 250.0f - h / 2 + 0.6f, w, h),
                          radii[b], sigmas[a], 0, 0, 0);
                int bad = unimodal_violations(&s, 1);
                if (bad) fprintf(stderr, "  seam: sigma %g r %g size %gx%g: %d\n",
                                 sigmas[a], radii[b], w, h, bad);
                total_bad += bad;
                cases++;
            }
    fprintf(stderr, "shadow seams: %d cases, %d unimodality violations\n", cases, total_bad);
    NXC_CHECK(total_bad == 0);

    /* Both sides of the mode switch agree on the shared near edge. */
    for (unsigned a = 0; a < sizeof(sigmas) / sizeof(sigmas[0]); a++) {
        float sg = sigmas[a];
        nxc_surface_t s2;
        NXC_CHECK_OK(nxc_surface_create(&s2, 700, 500, 0));
        int maxd = 0;
        for (float w = 20.0f; w < 420.0f; w += 7.0f) {
            shadow_on(&s, nxc_rect(150.0f, 100.0f, w, 260.0f), 10.0f, sg, 0, 0, 0);
            shadow_on(&s2, nxc_rect(150.0f, 100.0f, w + 0.75f, 260.0f), 10.0f, sg, 0, 0, 0);
            for (int y = 0; y < 500; y++)
                for (int x = 0; x < 150 + (int)(w / 2); x++) {
                    int d = abs((int)alpha_at(&s, x, y) - (int)alpha_at(&s2, x, y));
                    if (d > maxd) maxd = d;
                }
        }
        fprintf(stderr, "shadow mode switch sigma %g: max near-edge change for +0.75px width %d\n",
                sg, maxd);
        NXC_CHECK(maxd <= 4);   /* 4/255 at sigma 8 is invisible; the modes agree */
        nxc_surface_destroy(&s2);
    }
    nxc_surface_destroy(&s);
}

static void test_shadow_api(void) {
    nxc_surface_t s, ref;
    NXC_CHECK_OK(nxc_surface_create(&s, 300, 200, 0));
    NXC_CHECK_OK(nxc_surface_create(&ref, 300, 200, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_rect_t r = nxc_rect(50, 40, 120, 80);
    NXC_CHECK(nxc_draw_shadow_rrect(NULL, r, 8, 4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, nxc_rect(NAN, 0, 10, 10), 8, 4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, nxc_rect(0, 0, -1, 10), 8, 4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, r, -1, 4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, r, 8, -4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, r, 8, INFINITY, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, r, 8, 4, NAN, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_shadow_rrect(&c, nxc_rect(1e9f, 0, 10, 10), 8, 4, 0, 0, 0, 0xFF000000u) == NXC_ERR_INVALID_ARG);
    /* Nothing to draw: shrunk away, transparent color, far off-screen. */
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, r, 8, 4, 0, 0, -70.0f, 0xFF000000u));
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, r, 8, 4, 0, 0, 0, 0x00000000u));
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(-9e7f, 5e7f, 100, 100), 8, 30, 0, 0, 0, 0xFF000000u));
    NXC_CHECK(surf_equal(&s, &ref));
    /* Huge sigma is capped, not rejected. Clipped at every surface edge. */
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(-40, -30, 380, 260), 20, 1000.0f, 0, 0, 0, 0x80000000u));
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(250, 150, 200, 200), 20, 6.0f, 3, 4, 2, 0x80000000u));
    NXC_CHECK(premul_ok(&s));

    /* Offset (dx, dy) moves the shadow exactly. */
    nxc_surface_t a, b;
    NXC_CHECK_OK(nxc_surface_create(&a, 200, 200, 0));
    NXC_CHECK_OK(nxc_surface_create(&b, 200, 200, 0));
    shadow_on(&a, nxc_rect(40, 40, 80, 60), 10, 5, 0, 0, 0);
    shadow_on(&b, nxc_rect(40, 40, 80, 60), 10, 5, 7, 11, 0);
    int moved_bad = 0;
    for (int y = 0; y < 180; y++)
        for (int x = 0; x < 180; x++)
            moved_bad += alpha_at(&a, x, y) != alpha_at(&b, x + 7, y + 11);
    NXC_CHECK(moved_bad == 0);

    /* Global alpha and clip are honored. */
    memset(b.pixels, 0, 200 * 200 * 4);
    nxc_canvas_t cb;
    NXC_CHECK_OK(nxc_canvas_init(&cb, &b));
    NXC_CHECK_OK(nxc_set_alpha(&cb, 128));
    NXC_CHECK_OK(nxc_clip_rect(&cb, nxc_irect(0, 0, 80, 200)));
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&cb, nxc_rect(40, 40, 80, 60), 10, 5, 0, 0, 0, 0xFFFFFFFFu));
    int ga_bad = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 200; x++) {
            int want = x < 80 ? (int)((alpha_at(&a, x, y) * 128 + 127) / 255) : 0;
            if (abs((int)alpha_at(&b, x, y) - want) > 1) ga_bad++;
        }
    NXC_CHECK(ga_bad == 0);

    /* Deterministic across cache eviction (more keys than slots). */
    shadow_on(&a, nxc_rect(30.5f, 33.25f, 90, 70), 12, 6, 0, 0, 0);
    for (int i = 0; i < 80; i++) shadow_on(&b, nxc_rect(20, 20, 40 + i, 30 + i), (float)i * 0.5f, 1.0f + (float)i * 0.25f, 0, 0, 0);
    shadow_on(&b, nxc_rect(30.5f, 33.25f, 90, 70), 12, 6, 0, 0, 0);
    NXC_CHECK(surf_equal(&a, &b));
    /* ... and across engine re-initialization (cache rebuilt). */
    nxc_shutdown();
    nxc_test_init(MAXW, MAXH);
    shadow_on(&b, nxc_rect(30.5f, 33.25f, 90, 70), 12, 6, 0, 0, 0);
    NXC_CHECK(surf_equal(&a, &b));

    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&ref);
}

static void *small_alloc(uint64_t size) {
    void *p = NULL;
    return posix_memalign(&p, 16, (size_t)size) == 0 ? p : NULL;
}

/* A cache too small for a mask reports NO_MEMORY and draws nothing. */
static void test_shadow_small_pool(void) {
    nxc_shutdown();
    nxc_env_t env;
    memset(&env, 0, sizeof(env));
    env.alloc = small_alloc;
    env.free = free;
    env.max_width = 400;
    env.max_height = 400;
    env.shadow_cache_bytes = 2048;
    NXC_CHECK_OK(nxc_init(&env));
    nxc_surface_t s, ref;
    NXC_CHECK_OK(nxc_surface_create(&s, 400, 400, 0));
    NXC_CHECK_OK(nxc_surface_create(&ref, 400, 400, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK(nxc_draw_shadow_rrect(&c, nxc_rect(50, 50, 300, 300), 150, 10, 0, 0, 0, 0xFF000000u) == NXC_ERR_NO_MEMORY);
    NXC_CHECK(surf_equal(&s, &ref));
    /* Small masks still fit (nine-slice tile of a large rect). */
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(50, 50, 300, 300), 8, 2, 0, 0, 0, 0xFF000000u));
    NXC_CHECK(alpha_at(&s, 200, 200) == 255);
    /* Live backdrop larger than the pool: OVERFLOW, nothing drawn. */
    nxc_surface_t t;
    NXC_CHECK_OK(nxc_surface_create(&t, 400, 400, NXC_SURFACE_OPAQUE));
    nxc_canvas_t ct;
    NXC_CHECK_OK(nxc_canvas_init(&ct, &t));
    for (int i = 0; i < 400 * 400; i++) t.pixels[i] = 0xFF000000u | (rnd() & 0xFFFFFF);
    nxc_surface_t tref;
    NXC_CHECK_OK(nxc_surface_create(&tref, 400, 400, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_copy(&tref, 0, 0, &t, nxc_irect(0, 0, 400, 400)));
    nxc_material_t m;
    memset(&m, 0, sizeof(m));
    m.blur_sigma = 10;
    m.saturation = 1.5f;
    NXC_CHECK(nxc_draw_backdrop(&ct, nxc_rect(10, 10, 300, 300), 12, NULL, &m) == NXC_ERR_OVERFLOW);
    NXC_CHECK(surf_equal(&t, &tref));
    NXC_CHECK(ct.depth == 0);
    nxc_surface_destroy(&t);
    nxc_surface_destroy(&tref);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&ref);
    nxc_shutdown();
    nxc_test_init(MAXW, MAXH);
}

/* Visual sheet: UI-like shadows on a light background. */
static void test_shadow_visual(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 1280, 800, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(236, 238, 242)));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    double t0 = now_ms();
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(90, 40, 1100, 750), 12, 30, 0, 22, 2,
                                       NXC_ARGB(150, 0, 0, 12)));
    double t1 = now_ms();
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(90, 40, 1100, 750), 12, 30, 0, 22, 2,
                                       NXC_ARGB(0, 0, 0, 12)));   /* transparent: no-op */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(236, 238, 242)));
    double t2 = now_ms();
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(90, 40, 1100, 750), 12, 30, 0, 22, 2,
                                       NXC_ARGB(150, 0, 0, 12)));
    double t3 = now_ms();
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(90, 40, 1100, 750), 12, 3, 0, 1.5f, 0,
                                       NXC_ARGB(110, 0, 0, 12)));
    fprintf(stderr, "window shadow 1100x750 sigma 30 (ASan build): first %.2f ms, cached %.2f ms\n",
            t1 - t0, t3 - t2);
    /* Window body so the shadow reads as a drop shadow. */
    for (int y = 40; y < 790; y++)
        for (int x = 90; x < 1190; x++) {
            float qx = fabsf(x + 0.5f - 640.0f) - (550.0f - 12.0f), qy = fabsf(y + 0.5f - 415.0f) - (375.0f - 12.0f);
            float d = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) - 12.0f : fmaxf(qx, qy) - 12.0f;
            float cv = fminf(fmaxf(0.5f - d, 0.0f), 1.0f);
            if (cv > 0) {
                uint32_t p = s.pixels[y * 1280 + x];
                uint32_t k = (uint32_t)(cv * 255 + 0.5f);
                s.pixels[y * 1280 + x] = nxc__over(nxc__mul_px(0xFFFAFAFCu, k), p);
            }
        }
    nxc_test_write_png(&s, nxc_test_path("effects_window_shadow.png"));
    write_zoom(&s, 20, 700, 140, 100, 4, "effects_window_corner_zoom.png");

    /* Sheet of smaller elements. */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(236, 238, 242)));
    struct { float x, y, w, h, r, sg, dy, sp; uint8_t a; } e[] = {
        {  60,  60, 220, 140, 14, 18, 10, 0, 120 },
        { 340,  60, 220, 140, 14,  2,  1, 0,  90 },
        { 620,  60, 160, 160, 80, 12,  6, 0, 120 },
        { 840,  80, 380,  64, 32, 16,  8, 0, 120 },
        {  60, 300, 120,  36,  8, 1.5f, 1, 0, 110 },
        { 220, 300, 120,  36, 18, 3,  1.5f, 0, 110 },
        { 380, 300,  36,  36, 18, 4,  2, 0, 120 },
        { 460, 290,  12,  60,  6, 6,  3, 0, 140 },
        { 520, 300, 300, 200, 22, 30, 22, 2, 150 },
        { 900, 300, 300, 200,  0,  8,  4, 0, 120 },
        {  60, 420, 360, 300, 10, 48,  0, 0, 200 },
        { 900, 560, 330, 160, 40, 64, 10, 0, 160 },
    };
    for (unsigned k = 0; k < sizeof(e) / sizeof(e[0]); k++) {
        NXC_CHECK_OK(nxc_draw_shadow_rrect(&c, nxc_rect(e[k].x, e[k].y, e[k].w, e[k].h), e[k].r,
                                           e[k].sg, 0, e[k].dy, e[k].sp, NXC_ARGB(e[k].a, 0, 0, 16)));
    }
    nxc_test_write_png(&s, nxc_test_path("effects_shadow_sheet.png"));
    write_zoom(&s, 40, 40, 90, 70, 6, "effects_shadow_card_corner_zoom.png");
    write_zoom(&s, 440, 280, 60, 100, 6, "effects_shadow_small_zoom.png");
    nxc_surface_destroy(&s);
}

/* ============ Backdrop ============ */

/* Colorful test pattern: hue sweep, stripes, rings and a checkerboard. */
static void pattern(nxc_surface_t *s) {
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) {
            double hx = (double)x / s->width * 6.0;
            int seg = (int)hx;
            double f = hx - seg;
            double r = 0, g = 0, b = 0;
            switch (seg % 6) {
                case 0: r = 1; g = f; break;
                case 1: r = 1 - f; g = 1; break;
                case 2: g = 1; b = f; break;
                case 3: g = 1 - f; b = 1; break;
                case 4: b = 1; r = f; break;
                default: b = 1 - f; r = 1; break;
            }
            double shade = 0.35 + 0.65 * (double)y / s->height;
            if (((x / 24) + (y / 24)) % 2 == 0 && y > s->height / 2) shade *= 0.25;
            if (((x / 6) % 2) && y < 60) shade = 1.0, r = g = b = 1.0;
            double dx = x - 300.0, dy = y - 200.0, d = sqrt(dx * dx + dy * dy);
            if (fmod(d, 30.0) < 6.0) r = g = b = 0.05;
            uint32_t R = (uint32_t)(r * shade * 255), G = (uint32_t)(g * shade * 255), B = (uint32_t)(b * shade * 255);
            s->pixels[(size_t)y * s->stride + x] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
}

static void test_backdrop(void) {
    const int W = 640, H = 420;
    nxc_surface_t bg, blurred, a, b;
    NXC_CHECK_OK(nxc_surface_create(&bg, W, H, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&blurred, W, H, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&a, W, H, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&b, W, H, NXC_SURFACE_OPAQUE));
    pattern(&bg);
    NXC_CHECK_OK(nxc_surface_copy(&blurred, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    NXC_CHECK_OK(nxc_blur_surface(&blurred, nxc_irect(0, 0, W, H), 14.0f));

    nxc_material_t m;
    memset(&m, 0, sizeof(m));
    m.blur_sigma = 14.0f;
    m.saturation = 1.6f;
    m.tint = NXC_ARGB(70, 255, 255, 255);
    m.noise = 0;
    m.border = 0;
    nxc_canvas_t ca, cb;

    /* Live blur equals the pre-blurred source inside the panel. */
    NXC_CHECK_OK(nxc_surface_copy(&a, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    NXC_CHECK_OK(nxc_surface_copy(&b, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    NXC_CHECK_OK(nxc_canvas_init(&ca, &a));
    NXC_CHECK_OK(nxc_canvas_init(&cb, &b));
    nxc_rect_t panel = nxc_rect(60.5f, 40.25f, 330.0f, 220.0f);
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, panel, 20.0f, &blurred, &m));
    NXC_CHECK_OK(nxc_draw_backdrop(&cb, panel, 20.0f, NULL, &m));
    int maxd = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t pa = a.pixels[y * W + x], pb = b.pixels[y * W + x];
            for (int s = 0; s < 32; s += 8) {
                int d = abs((int)((pa >> s) & 0xFF) - (int)((pb >> s) & 0xFF));
                if (d > maxd) maxd = d;
            }
        }
    fprintf(stderr, "backdrop live vs pre-blurred: max channel diff %d\n", maxd);
    NXC_CHECK(maxd <= 1);
    NXC_CHECK(ca.depth == 0 && cb.depth == 0);
    /* Outside the rounded rect nothing changed; corner pixel stays background. */
    NXC_CHECK(a.pixels[41 * W + 61] == bg.pixels[41 * W + 61]);
    NXC_CHECK(a.pixels[10 * W + 10] == bg.pixels[10 * W + 10]);
    NXC_CHECK(a.pixels[150 * W + 200] != bg.pixels[150 * W + 200]);

    /* Identity material over a copy reproduces the source exactly. */
    nxc_material_t id;
    memset(&id, 0, sizeof(id));
    id.saturation = 1.0f;
    NXC_CHECK_OK(nxc_surface_copy(&a, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(100, 100, 200, 150), 0.0f, &blurred, &id));
    int id_bad = 0;
    for (int y = 100; y < 250; y++)
        for (int x = 100; x < 300; x++) id_bad += a.pixels[y * W + x] != blurred.pixels[y * W + x];
    NXC_CHECK(id_bad == 0);

    /* Saturation 0 is gray; grain is bounded by its amplitude, zero-mean. */
    nxc_material_t gray = id;
    gray.saturation = 0.0f;
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(100, 100, 200, 150), 0.0f, &blurred, &gray));
    int not_gray = 0;
    for (int y = 100; y < 250; y++)
        for (int x = 100; x < 300; x++) {
            uint32_t p = a.pixels[y * W + x];
            if (((p >> 16) & 0xFF) != ((p >> 8) & 0xFF) || ((p >> 8) & 0xFF) != (p & 0xFF)) not_gray++;
        }
    NXC_CHECK(not_gray == 0);
    nxc_material_t grain = id;
    grain.noise = 6;
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(100, 100, 200, 150), 0.0f, &blurred, &grain));
    int over = 0;
    double mean = 0;
    for (int y = 100; y < 250; y++)
        for (int x = 100; x < 300; x++) {
            uint32_t p = a.pixels[y * W + x], q = blurred.pixels[y * W + x];
            int d = (int)((p >> 8) & 0xFF) - (int)((q >> 8) & 0xFF);
            if (abs(d) > 6) over++;
            mean += d;
        }
    mean /= 200.0 * 150.0;
    fprintf(stderr, "backdrop grain: mean offset %.3f\n", mean);
    NXC_CHECK(over == 0);
    NXC_CHECK(fabs(mean) < 0.5);

    /* Highlight: edge pixels pick up the border color, the interior not. */
    nxc_material_t hl = id;
    hl.border = NXC_ARGB(255, 255, 0, 255);
    NXC_CHECK_OK(nxc_surface_copy(&a, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(100, 100, 200, 150), 10.0f, &blurred, &hl));
    NXC_CHECK(a.pixels[100 * W + 200] == 0xFFFF00FFu);       /* top edge, mid */
    NXC_CHECK(a.pixels[175 * W + 100] == 0xFFFF00FFu);       /* left edge, mid */
    NXC_CHECK(a.pixels[175 * W + 101] == blurred.pixels[175 * W + 101]);
    NXC_CHECK(a.pixels[175 * W + 200] == blurred.pixels[175 * W + 200]);

    /* Argument validation; failures draw nothing. */
    NXC_CHECK_OK(nxc_surface_copy(&b, 0, 0, &a, nxc_irect(0, 0, W, H)));
    nxc_material_t badm = id;
    badm.noise = 17;
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, 8, &blurred, &badm) == NXC_ERR_INVALID_ARG);
    badm = id;
    badm.saturation = NAN;
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, 8, &blurred, &badm) == NXC_ERR_INVALID_ARG);
    badm = id;
    badm.blur_sigma = NAN;
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, 8, NULL, &badm) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, NAN, &blurred, &id) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, 8, &blurred, NULL) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_draw_backdrop(&ca, nxc_rect(0, 0, -5, 5), 8, &blurred, &id) == NXC_ERR_INVALID_ARG);
    nxc_surface_t small;
    NXC_CHECK_OK(nxc_surface_create(&small, 10, 10, 0));
    NXC_CHECK(nxc_draw_backdrop(&ca, panel, 8, &small, &id) == NXC_ERR_INVALID_ARG);
    nxc_surface_destroy(&small);
    NXC_CHECK(surf_equal(&a, &b));
    /* Empty / off-screen / edge-clipped panels. */
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(10, 10, 0, 40), 8, &blurred, &id));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(5000, 10, 40, 40), 8, NULL, &m));
    NXC_CHECK(surf_equal(&a, &b));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(-30, -20, 120, 90), 16, NULL, &m));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(W - 70, H - 50, 120, 90), 16, NULL, &m));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(300, 300, 1.5f, 1.5f), 4, NULL, &hl));
    NXC_CHECK(ca.depth == 0);

    /* Visual: several materials over the pattern. */
    NXC_CHECK_OK(nxc_surface_copy(&a, 0, 0, &bg, nxc_irect(0, 0, W, H)));
    nxc_material_t glass = m;
    glass.noise = 4;
    glass.border = NXC_ARGB(90, 255, 255, 255);
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&ca, nxc_rect(40, 40, 300, 200), 22, 18, 0, 10, 0, NXC_ARGB(120, 0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(40, 40, 300, 200), 22, &blurred, &glass));
    nxc_material_t dark = glass;
    dark.tint = NXC_ARGB(140, 20, 20, 30);
    dark.saturation = 1.8f;
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(380.5f, 60.5f, 220, 120), 28, NULL, &dark));
    nxc_material_t dock = glass;
    dock.tint = NXC_ARGB(60, 255, 255, 255);
    NXC_CHECK_OK(nxc_draw_shadow_rrect(&ca, nxc_rect(120, 330, 400, 70), 20, 16, 0, 8, 0, NXC_ARGB(120, 0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_backdrop(&ca, nxc_rect(120, 330, 400, 70), 20, NULL, &dock));
    nxc_test_write_png(&bg, nxc_test_path("effects_backdrop_pattern.png"));
    nxc_test_write_png(&a, nxc_test_path("effects_backdrop.png"));
    write_zoom(&a, 30, 30, 70, 50, 6, "effects_backdrop_corner_zoom.png");
    write_zoom(&a, 370, 50, 60, 40, 6, "effects_backdrop_frac_corner_zoom.png");

    nxc_surface_destroy(&bg);
    nxc_surface_destroy(&blurred);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
}

static void test_blur_visual(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 640, 420, NXC_SURFACE_OPAQUE));
    pattern(&s);
    NXC_CHECK_OK(nxc_blur_surface(&s, nxc_irect(0, 0, 320, 420), 3.0f));
    NXC_CHECK_OK(nxc_blur_surface(&s, nxc_irect(320, 0, 320, 210), 12.0f));
    NXC_CHECK_OK(nxc_blur_surface(&s, nxc_irect(320, 210, 320, 210), 40.0f));
    nxc_test_write_png(&s, nxc_test_path("effects_blur.png"));
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(MAXW, MAXH);
    test_blur_basic();
    test_blur_symmetry();
    test_blur_profile();
    test_blur_clip_continuity();
    test_blur_mask();
    test_shadow_rect_accuracy();
    test_shadow_rrect_accuracy();
    test_shadow_seams();
    test_shadow_api();
    test_shadow_small_pool();
    test_shadow_visual();
    test_backdrop();
    test_blur_visual();
    nxc_shutdown();
    return nxc_test_finish("effects");
}
