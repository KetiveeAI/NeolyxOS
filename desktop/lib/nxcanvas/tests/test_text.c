/*
 * NeolyxOS - NXCanvas text tests: UTF-8, layout, ellipsis, cache and a
 * type specimen for visual review.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxc_test.h"
#include "../nxc_font_internal.h"
#include <math.h>

static uint8_t *read_file(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    *size = (uint32_t)n;
    return d;
}

/* Font directory relative to tests/ (override with NXC_FONT_DIR). */
static const char *font_dir(void) {
    const char *d = getenv("NXC_FONT_DIR");
    return (d && *d) ? d : "../../../../resources/fonts/";
}

static const char *font_path(const char *rel) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s%s", font_dir(), rel);
    return buf;
}

static uint8_t *g_blobs[8];
static int g_nblobs;

static nxc_font_t *load(const char *name) {
    uint32_t n;
    uint8_t *d = read_file(name, &n);
    g_blobs[g_nblobs++] = d;
    nxc_font_t *f = NULL;
    NXC_CHECK_OK(nxc_font_load(&f, d, n));
    return f;
}

static double alpha_sum(const nxc_surface_t *s) {
    double t = 0;
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) t += (s->pixels[y * s->stride + x] >> 24) / 255.0;
    return t;
}

static void test_layout(nxc_font_t *f) {
    float scale = 16.0f / nxc__font_units_per_em(f);
    uint32_t gH = nxc__font_glyph_index(f, 'H'), ge = nxc__font_glyph_index(f, 'e');
    uint32_t gl = nxc__font_glyph_index(f, 'l'), go = nxc__font_glyph_index(f, 'o');
    float want = (nxc__font_advance(f, gH) + nxc__font_advance(f, ge) + 2 * nxc__font_advance(f, gl) +
                  nxc__font_advance(f, go)) * scale;
    want += (nxc__font_kerning(f, gH, ge) + nxc__font_kerning(f, ge, gl) + nxc__font_kerning(f, gl, gl) +
             nxc__font_kerning(f, gl, go)) * scale;
    float got = nxc_text_width(f, 16, "Hello", -1, NULL);
    fprintf(stderr, "width Hello: %.3f (want %.3f)\n", got, want);
    NXC_CHECK(fabsf(got - want) < 1e-3f);

    /* Kerning off differs for a kerned pair. */
    nxc_text_opts_t nk = { 0, NXC_ALIGN_LEFT, 0, 1 };
    float av_k = nxc_text_width(f, 40, "AV", -1, NULL), av_n = nxc_text_width(f, 40, "AV", -1, &nk);
    fprintf(stderr, "AV kerned %.2f unkerned %.2f\n", av_k, av_n);
    NXC_CHECK(av_k < av_n);

    /* Tracking adds (n-1) * spacing. */
    nxc_text_opts_t tr = { 2.0f, NXC_ALIGN_LEFT, 0, 0 };
    NXC_CHECK(fabsf(nxc_text_width(f, 16, "Hello", -1, &tr) - (got + 8.0f)) < 1e-3f);

    /* Ellipsis never exceeds max_width and is shorter than the full text. */
    nxc_text_opts_t el = { 0, NXC_ALIGN_LEFT, 60.0f, 0 };
    float full = nxc_text_width(f, 16, "A fairly long window title", -1, NULL);
    float cut = nxc_text_width(f, 16, "A fairly long window title", -1, &el);
    fprintf(stderr, "ellipsis: full %.1f cut %.1f\n", full, cut);
    NXC_CHECK(cut <= 60.0f && cut < full && cut > 30.0f);

    /* Invalid UTF-8 renders replacement glyphs and keeps going. */
    float bad = nxc_text_width(f, 16, "a\xC3\x28" "b\xFF" "c", -1, NULL);
    NXC_CHECK(bad > nxc_text_width(f, 16, "abc", -1, NULL));
    NXC_CHECK(nxc_text_width(f, 16, "", -1, NULL) == 0.0f);
    NXC_CHECK(nxc_text_width(NULL, 16, "x", -1, NULL) == 0.0f);
    NXC_CHECK(nxc_text_width(f, 0.0f, "x", -1, NULL) == 0.0f);
}

static void test_draw(nxc_font_t *f) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 300, 60, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c, f, 20, 10, 40, "Rendering", -1, NXC_RGB(255, 255, 255), NULL));
    double ink = 0;
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 300; x++) ink += (s.pixels[y * 300 + x] & 0xFF) / 255.0;
    fprintf(stderr, "ink: %.1f\n", ink);
    NXC_CHECK(ink > 200 && ink < 1200);
    /* Second draw hits the cache and produces identical pixels. */
    nxc_surface_t s2;
    NXC_CHECK_OK(nxc_surface_create(&s2, 300, 60, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c2;
    NXC_CHECK_OK(nxc_canvas_init(&c2, &s2));
    NXC_CHECK_OK(nxc_surface_clear(&s2, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c2, f, 20, 10, 40, "Rendering", -1, NXC_RGB(255, 255, 255), NULL));
    NXC_CHECK(memcmp(s.pixels, s2.pixels, 300 * 60 * 4) == 0);
    /* Alignment: centered text is symmetric about x. */
    nxc_text_opts_t ctr = { 0, NXC_ALIGN_CENTER, 0, 0 };
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c, f, 20, 150, 40, "HIH", -1, NXC_RGB(255, 255, 255), &ctr));
    int minx = 300, maxx = -1;
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 300; x++)
            if (s.pixels[y * 300 + x] & 0xFF) { if (x < minx) minx = x; if (x > maxx) maxx = x; }
    fprintf(stderr, "centered HIH spans %d..%d\n", minx, maxx);
    NXC_CHECK(abs((minx + maxx) - 299) <= 3);
    /* Huge text goes through the uncached path without error. */
    NXC_CHECK_OK(nxc_draw_text(&c, f, 300, -20, 50, "W", -1, NXC_RGB(255, 0, 0), NULL));
    /* Clipping: text fully outside draws nothing and costs nothing. */
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c, f, 20, 10, -500, "Rendering", -1, NXC_RGB(255, 255, 255), NULL));
    NXC_CHECK(alpha_sum(&s) == 300 * 60);
    nxc_surface_destroy(&s);
    nxc_surface_destroy(&s2);
}

static void test_cache_eviction(nxc_font_t *f) {
    /* Re-init with a tiny pool and draw many distinct glyph sizes. */
    /* Many distinct sizes overflow the ring repeatedly; nothing may break. */
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 512, 512, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    for (int i = 0; i < 400; i++) {
        float px = 10.0f + (float)(i % 150) * 0.9f;
        NXC_CHECK_OK(nxc_draw_text(&c, f, px, 5, 100 + (i % 7) * 50, "Sphinx of black quartz", -1,
                                   NXC_RGB(200, 200, 255), NULL));
    }
    nxc_surface_destroy(&s);
}

static void specimen(nxc_font_t *reg, nxc_font_t *med, nxc_font_t *bold) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 900, 560, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(0, 0, 900, 280), NXC_RGB(0x1c, 0x1f, 0x26)));
    NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(0, 280, 900, 280), NXC_RGB(0xf5, 0xf5, 0xf7)));
    const float sizes[] = { 11, 12, 13, 15, 17, 22, 34, 64 };
    for (int half = 0; half < 2; half++) {
        nxc_color_t col = half == 0 ? NXC_RGB(255, 255, 255) : NXC_RGB(0, 0, 0);
        float y = half * 280 + 22;
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            float px = sizes[i];
            y += px * 1.1f;
            nxc_font_t *f = i % 3 == 0 ? reg : (i % 3 == 1 ? med : bold);
            NXC_CHECK_OK(nxc_draw_text(&c, f, px, 16, y, "Hamburgefonstiv 0123 AVATAR Wo To", -1, col, NULL));
        }
        NXC_CHECK_OK(nxc_draw_text(&c, med, 13.5f, 16, half * 280 + 270,
                                   "File  Edit  View  Window  Help      Sun 25 Oct  22:10", -1, col, NULL));
    }
    nxc_test_write_png(&s, nxc_test_path("text_specimen.png"));
    /* 4x zoom of the small sizes */
    nxc_surface_t z;
    NXC_CHECK_OK(nxc_surface_create(&z, 1200, 320, NXC_SURFACE_OPAQUE));
    nxc_canvas_t zc;
    NXC_CHECK_OK(nxc_canvas_init(&zc, &z));
    NXC_CHECK_OK(nxc_draw_surface(&zc, &s, nxc_irect(0, 20, 300, 80), nxc_rect(0, 0, 1200, 320), 255, NXC_FILTER_NEAREST));
    nxc_test_write_png(&z, nxc_test_path("text_specimen_zoom4.png"));
    nxc_surface_destroy(&z);
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(1024, 1024);
    nxc_font_t *reg = load(font_path("NXSans/NXSans-Text.ttf"));
    nxc_font_t *med = load(font_path("NXSans/NXSans-UI.ttf"));
    nxc_font_t *bold = load(font_path("NXSans/NXSans-Bold.ttf"));
    test_layout(reg);
    test_draw(reg);
    specimen(reg, med, bold);
    test_cache_eviction(reg);
    nxc_font_destroy(reg);
    nxc_font_destroy(med);
    nxc_font_destroy(bold);
    nxc_shutdown();
    for (int i = 0; i < g_nblobs; i++) free(g_blobs[i]);
    return nxc_test_finish("text");
}
