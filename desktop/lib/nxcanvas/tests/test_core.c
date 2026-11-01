/*
 * NeolyxOS - NXCanvas core tests: surfaces, canvas state, span blending,
 * rounded clipping, masks and the math library.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_internal.h"
#include <math.h>

static void test_math(void) {
    double max_sin = 0, max_exp = 0, max_log = 0, max_atan = 0;
    for (int i = -20000; i <= 20000; i++) {
        float x = (float)i * 0.001f;
        double es = fabs((double)nxc__sinf(x) - sin((double)x));
        double ec = fabs((double)nxc__cosf(x) - cos((double)x));
        if (es > max_sin) max_sin = es;
        if (ec > max_sin) max_sin = ec;
        float ex = x * 2.0f;
        double ee = fabs((double)nxc__expf(ex) - exp((double)ex)) / exp((double)ex);
        if (ee > max_exp) max_exp = ee;
        float lx = (float)(i + 20001) * 0.0137f;
        double el = fabs((double)nxc__logf(lx) - log((double)lx));
        if (el > max_log) max_log = el;
        float ay = x, ax = (float)((i * 7919) % 2000) * 0.003f - 3.0f;
        double ea = fabs((double)nxc__atan2f(ay, ax) - atan2((double)ay, (double)ax));
        if (ea > max_atan && !(ax == 0 && ay == 0)) max_atan = ea;
    }
    fprintf(stderr, "math max err: sin/cos %.2e exp(rel) %.2e log %.2e atan2 %.2e\n",
            max_sin, max_exp, max_log, max_atan);
    NXC_CHECK(max_sin < 2e-6);
    NXC_CHECK(max_exp < 2e-6);
    NXC_CHECK(max_log < 2e-6);
    NXC_CHECK(max_atan < 2e-6);
    NXC_CHECK(fabs(nxc__powf(2.0f, 10.0f) - 1024.0f) < 1e-2);
    NXC_CHECK(fabs(nxc__powf(0.5f, 2.2f) - pow(0.5, 2.2)) < 1e-6);
    NXC_CHECK(nxc__floorf(-1.5f) == -2.0f && nxc__ceilf(-1.5f) == -1.0f);
    NXC_CHECK(nxc__floorf(3.0f) == 3.0f && nxc__ceilf(3.0f) == 3.0f);
    NXC_CHECK(fabsf(nxc__sqrtf(2.0f) - 1.41421356f) < 1e-6f);
    NXC_CHECK(!nxc__finitef(NAN) && !nxc__finitef(INFINITY) && nxc__finitef(1.0f));
}

static void test_pixels(void) {
    /* div255 must be exact rounding over the full product range. */
    int bad = 0;
    for (uint32_t x = 0; x <= 65025; x++) {
        uint32_t want = (x + 127) / 255;   /* round(x/255) */
        uint32_t got = nxc__div255(x);
        if (got != want && got != (x * 2 + 255) / 510) bad++;
    }
    NXC_CHECK(bad == 0);
    NXC_CHECK(nxc__premul(0x80FF0000u) == 0x80800000u);
    NXC_CHECK(nxc__over(0xFF102030u, 0xFFFFFFFFu) == 0xFF102030u);
    NXC_CHECK(nxc__over(0x00000000u, 0xFF123456u) == 0xFF123456u);
    uint32_t half = nxc__over(nxc__premul(0x80000000u), 0xFFFFFFFFu);
    NXC_CHECK(nxc_test_color_near(half, 0xFF7F7F7Fu, 1));
}

static void test_canvas(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 64, 48, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(255, 255, 255)));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));

    /* Clip + translate + span */
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_translate(&c, 10, 5));
    NXC_CHECK_OK(nxc_clip_rect(&c, nxc_irect(0, 0, 8, 4)));      /* device 10..18 x 5..9 */
    nxc_irect_t dc = nxc__device_clip(&c);
    NXC_CHECK(dc.x == 10 && dc.y == 5 && dc.w == 8 && dc.h == 4);
    nxc__span_solid(&c, 6, 0, 64, 0xFF0000FFu, 255);
    NXC_CHECK(nxc_test_pixel(&s, 9, 6) == 0xFFFFFFFFu);
    NXC_CHECK(nxc_test_pixel(&s, 10, 6) == 0xFF0000FFu);
    NXC_CHECK(nxc_test_pixel(&s, 17, 6) == 0xFF0000FFu);
    NXC_CHECK(nxc_test_pixel(&s, 18, 6) == 0xFFFFFFFFu);
    nxc__span_solid(&c, 2, 0, 64, 0xFF0000FFu, 255);              /* outside clip rows */
    NXC_CHECK(nxc_test_pixel(&s, 12, 2) == 0xFFFFFFFFu);
    NXC_CHECK_OK(nxc_restore(&c));
    NXC_CHECK(nxc_restore(&c) == NXC_ERR_STATE);                   /* unbalanced */

    /* Global alpha halves a black span over white. */
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_set_alpha(&c, 128));
    nxc__span_solid(&c, 20, 0, 4, 0xFF000000u, 255);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 1, 20), 0xFF7F7F7Fu, 1));
    NXC_CHECK_OK(nxc_restore(&c));

    /* Stack overflow is reported, not ignored. */
    nxc_status_t last = NXC_OK;
    for (int i = 0; i < NXC_STATE_STACK_MAX + 1; i++) last = nxc_save(&c);
    NXC_CHECK(last == NXC_ERR_OVERFLOW);
    while (c.depth > 0) nxc_restore(&c);

    /* Mask draw clipped at the surface edge must not write out of bounds. */
    uint8_t mask[16 * 16];
    for (int i = 0; i < 256; i++) mask[i] = (uint8_t)(i & 0xFF);
    NXC_CHECK_OK(nxc_draw_mask(&c, mask, 16, 16, 16, 56, 40, NXC_RGB(0, 128, 0)));
    NXC_CHECK_OK(nxc_draw_mask(&c, mask, 16, 16, 16, -8, -8, NXC_RGB(0, 128, 0)));
    NXC_CHECK(nxc_draw_mask(&c, mask, 16, 16, 8, 0, 0, NXC_RGB(0, 0, 0)) == NXC_ERR_INVALID_ARG);

    nxc_test_write_png(&s, nxc_test_path("core_canvas.png"));
    nxc_surface_destroy(&s);
}

static void test_rclip(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 120, 80, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(20, 20, 30)));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_clip_rrect(&c, nxc_rect(10.5f, 10.5f, 100, 60), 16));
    for (int y = 0; y < 80; y++) nxc__span_solid(&c, y, 0, 120, 0xFFE0E0F0u, 255);
    NXC_CHECK_OK(nxc_restore(&c));
    /* Corner pixel outside the radius stays background, center is filled,
     * an edge pixel is partially covered (anti-aliased). */
    NXC_CHECK(nxc_test_pixel(&s, 11, 11) == 0xFF14141Eu);
    NXC_CHECK(nxc_test_pixel(&s, 60, 40) == 0xFFE0E0F0u);
    uint32_t edge = nxc_test_pixel(&s, 60, 10);
    NXC_CHECK(edge != 0xFF14141Eu && edge != 0xFFE0E0F0u);
    nxc_test_write_png(&s, nxc_test_path("core_rclip.png"));
    nxc_surface_destroy(&s);
}

static void test_copy(void) {
    nxc_surface_t a, b;
    NXC_CHECK_OK(nxc_surface_create(&a, 16, 16, 0));
    NXC_CHECK_OK(nxc_surface_create(&b, 16, 16, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_clear(&a, NXC_RGB(255, 0, 0)));
    NXC_CHECK_OK(nxc_surface_copy(&b, 10, 10, &a, nxc_irect(-4, -4, 20, 20)));
    NXC_CHECK(nxc_test_pixel(&b, 14, 14) == 0xFFFF0000u);   /* src (0,0) lands at 10+4 */
    NXC_CHECK(nxc_test_pixel(&b, 13, 13) == 0xFF000000u);
    NXC_CHECK(nxc_surface_copy(&b, 0, 0, NULL, nxc_irect(0, 0, 1, 1)) == NXC_ERR_INVALID_ARG);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
}

/* R2: geometric edge coverage blends in linear light on opaque pixels;
 * alpha, full coverage, translucent targets and the _gamma spans keep the
 * sRGB blend. */
static double srgb_decode(uint32_t v) {
    double x = v / 255.0;
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

static void test_linear_edges(void) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 256, 8, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));

    /* White at coverage k over black: the pixel's light is k/255 */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    uint8_t ramp[256];
    for (int i = 0; i < 256; i++) ramp[i] = (uint8_t)i;
    nxc__span_cov(&c, 0, 0, ramp, 256, 0xFFFFFFFFu);
    double worst = 0;
    int monotonic = 1;
    uint32_t prev = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t g = (nxc_test_pixel(&s, i, 0) >> 8) & 0xFFu;
        double err = fabs(srgb_decode(g) - i / 255.0);
        if (err > worst) worst = err;
        if (g < prev) monotonic = 0;
        prev = g;
    }
    fprintf(stderr, "linear edges: worst light error %.4f over the coverage ramp\n", worst);
    NXC_CHECK(worst < 0.0105);             /* within one sRGB step at the top end */
    NXC_CHECK(monotonic);
    NXC_CHECK(((nxc_test_pixel(&s, 128, 0) >> 8) & 0xFFu) >= 186u);   /* not the sRGB 128 */
    NXC_CHECK(nxc_test_pixel(&s, 255, 0) == 0xFFFFFFFFu && nxc_test_pixel(&s, 0, 0) == 0xFF000000u);

    /* The _gamma span keeps the old sRGB mix (text, shadows, masks) */
    nxc__span_cov_gamma(&c, 1, 0, ramp, 256, 0xFFFFFFFFu);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 128, 1), 0xFF808080u, 1));
    nxc__span_solid_gamma(&c, 2, 0, 4, 0xFFFFFFFFu, 128);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 1, 2), 0xFF808080u, 1));
    nxc__span_solid(&c, 3, 0, 4, 0xFFFFFFFFu, 128);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 1, 3), 0xFFBCBCBCu, 1));

    /* A color blended with itself is unchanged, at every value and coverage */
    int same = 1;
    for (uint32_t v = 0; v < 256 && same; v++) {
        uint32_t col = 0xFF000000u | (v << 16) | (v << 8) | v;
        NXC_CHECK_OK(nxc_surface_clear(&s, col));
        nxc__span_cov(&c, 4, 0, ramp, 256, col);
        for (int i = 0; i < 256; i++) if (nxc_test_pixel(&s, i, 4) != col) same = 0;
    }
    NXC_CHECK(same);

    /* Global alpha still composites in sRGB: full coverage at alpha 128 */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_save(&c));
    NXC_CHECK_OK(nxc_set_alpha(&c, 128));
    nxc__span_cov(&c, 5, 0, ramp, 256, 0xFFFFFFFFu);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&s, 255, 5), 0xFF808080u, 1));
    /* and at half coverage the light is half of that pixel's light */
    double half = srgb_decode((nxc_test_pixel(&s, 128, 5) >> 8) & 0xFFu);
    NXC_CHECK(fabs(half - 0.5 * srgb_decode(128)) < 0.004);
    NXC_CHECK_OK(nxc_restore(&c));
    nxc_surface_destroy(&s);

    /* Translucent target: no display color, so the plain blend is kept */
    nxc_surface_t t;
    NXC_CHECK_OK(nxc_surface_create(&t, 16, 2, 0));
    NXC_CHECK_OK(nxc_canvas_init(&c, &t));
    NXC_CHECK_OK(nxc_surface_clear(&t, 0));
    nxc__span_solid(&c, 0, 0, 4, 0xFFFFFFFFu, 128);
    NXC_CHECK(nxc_test_color_near(nxc_test_pixel(&t, 1, 0), 0x80FFFFFFu, 1));   /* white at alpha 128 */
    nxc_surface_destroy(&t);
}

int main(void) {
    nxc_test_init(256, 256);
    test_math();
    test_pixels();
    test_canvas();
    test_rclip();
    test_copy();
    test_linear_edges();
    nxc_shutdown();
    return nxc_test_finish("core");
}
