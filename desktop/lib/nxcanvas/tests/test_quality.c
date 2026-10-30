/*
 * NeolyxOS - NXCanvas text quality tests: font information ('name',
 * OS/2, post), character map iteration, fallback chains, vertical
 * hinting, coverage curves, glyph cache ring correctness and timing.
 * Writes a before/after specimen (hinting + curves off vs on).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 200809L
#include "nxc_test.h"
#include "../nxc_font_internal.h"
#include <math.h>
#include <time.h>

static const char *font_dir(void) {
    const char *d = getenv("NXC_FONT_DIR");
    return (d && *d) ? d : "../../../../resources/fonts/";
}

static uint8_t *read_file(const char *rel, uint32_t *size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s%s", font_dir(), rel);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    *size = (uint32_t)n;
    return d;
}

typedef struct { uint8_t *data; uint32_t size; nxc_font_t *font; } face_t;

static face_t load(const char *rel) {
    face_t fc;
    fc.data = read_file(rel, &fc.size);
    fc.font = NULL;
    NXC_CHECK_OK(nxc_font_load(&fc.font, fc.data, fc.size));
    return fc;
}

static void unload(face_t *fc) {
    nxc_font_destroy(fc->font);
    free(fc->data);
    fc->font = NULL;
    fc->data = NULL;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* Valid, NUL-terminated UTF-8 within cap bytes. */
static int utf8_ok(const char *s, size_t cap) {
    size_t n = strnlen(s, cap);
    if (n >= cap) return 0;
    for (size_t i = 0; i < n;) {
        unsigned char b = (unsigned char)s[i];
        size_t k = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        if (k == 0 || i + k > n) return 0;
        for (size_t j = 1; j < k; j++) if (((unsigned char)s[i + j] & 0xC0) != 0x80) return 0;
        if (b < 0x20) return 0;
        i += k;
    }
    return 1;
}

static int info_strings_ok(const nxc_font_info_t *in) {
    return utf8_ok(in->family, sizeof(in->family)) && utf8_ok(in->style, sizeof(in->style)) &&
           utf8_ok(in->full_name, sizeof(in->full_name)) && utf8_ok(in->version, sizeof(in->version)) &&
           utf8_ok(in->copyright, sizeof(in->copyright)) && utf8_ok(in->license, sizeof(in->license));
}

/* ============ Font information ============ */

static void test_info(face_t *text, face_t *semi, face_t *mono, face_t *ital) {
    nxc_font_info_t in;
    NXC_CHECK_OK(nxc_font_info(text->font, &in));
    fprintf(stderr, "info: '%s' / '%s' / '%s' w%u glyphs %u kern %u\n", in.family, in.style,
            in.version, in.weight, in.num_glyphs, in.kerning);
    NXC_CHECK(strcmp(in.family, "Source Sans 3") == 0);
    NXC_CHECK(strcmp(in.style, "Regular") == 0);
    NXC_CHECK(in.weight == 400 && !in.italic && !in.monospace);
    NXC_CHECK(in.num_glyphs == 2478 && in.units_per_em == 1000 && in.kerning == 2);
    NXC_CHECK(in.file_size == text->size);
    NXC_CHECK(strstr(in.license, "SIL Open Font License") != NULL);
    NXC_CHECK(strncmp(in.version, "Version 3.052", 13) == 0);
    NXC_CHECK(info_strings_ok(&in));

    NXC_CHECK_OK(nxc_font_info(semi->font, &in));
    NXC_CHECK(in.weight == 600 && strcmp(in.family, "Source Sans 3") == 0 &&
              strcmp(in.style, "SemiBold") == 0);
    NXC_CHECK_OK(nxc_font_info(mono->font, &in));
    NXC_CHECK(in.monospace == 1 && strcmp(in.family, "Cascadia Mono") == 0);
    /* The 512-byte license field truncates on a character boundary. */
    NXC_CHECK(strlen(in.license) > 400 && info_strings_ok(&in));
    NXC_CHECK_OK(nxc_font_info(ital->font, &in));
    NXC_CHECK(in.italic == 1 && in.weight == 400);

    NXC_CHECK(nxc_font_info(NULL, &in) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(in.family[0] == 0);
    NXC_CHECK(nxc_font_info(text->font, NULL) == NXC_ERR_INVALID_ARG);
}

/* Random corruption inside the 'name' table never yields invalid strings. */
static void test_name_fuzz(face_t *text) {
    const uint8_t *d = text->data;
    uint32_t ntab = ((uint32_t)d[4] << 8) | d[5], noff = 0, nlen = 0;
    for (uint32_t i = 0; i < ntab; i++) {
        const uint8_t *r = d + 12 + 16 * i;
        if (memcmp(r, "name", 4) == 0) {
            noff = ((uint32_t)r[8] << 24) | ((uint32_t)r[9] << 16) | ((uint32_t)r[10] << 8) | r[11];
            nlen = ((uint32_t)r[12] << 24) | ((uint32_t)r[13] << 16) | ((uint32_t)r[14] << 8) | r[15];
        }
    }
    NXC_CHECK(noff > 0 && nlen > 64);
    uint8_t *copy = malloc(text->size);
    uint32_t seed = 12345, loaded = 0;
    for (int it = 0; it < 3000; it++) {
        memcpy(copy, d, text->size);
        int flips = 1 + it % 16;
        for (int k = 0; k < flips; k++) {
            seed = seed * 1664525u + 1013904223u;
            /* Bias toward the header and records, where offsets live. */
            uint32_t span = (it & 1) ? (nlen < 400 ? nlen : 400) : nlen;
            uint32_t at = noff + (seed >> 8) % span;
            seed = seed * 1664525u + 1013904223u;
            copy[at] = (uint8_t)(seed >> 24);
        }
        nxc_font_t *f = NULL;
        if (nxc_font_load(&f, copy, text->size) == NXC_OK) {
            loaded++;
            nxc_font_info_t in;
            NXC_CHECK_OK(nxc_font_info(f, &in));
            NXC_CHECK(info_strings_ok(&in));
            nxc_font_destroy(f);
        }
    }
    fprintf(stderr, "name fuzz: 3000 iterations, %u loaded\n", loaded);
    NXC_CHECK(loaded > 2500);   /* a bad name table never rejects the font */
    free(copy);
}

/* ============ Character map ============ */

static void test_chars(face_t *text) {
    nxc_font_t *f = text->font;
    NXC_CHECK(nxc_font_has_char(f, 'A') && nxc_font_has_char(f, 0x20AC));
    NXC_CHECK(!nxc_font_has_char(f, 0x2500));           /* box drawing: not in NX Sans */
    NXC_CHECK(!nxc_font_has_char(NULL, 'A'));
    uint32_t first = nxc_font_next_char(f, 0);
    NXC_CHECK(first <= 0x20);
    NXC_CHECK(nxc_font_next_char(f, 'A') == 'A');
    NXC_CHECK(nxc_font_next_char(f, 0x110000) == NXC_NO_CHAR);
    uint32_t n = 0, prev = 0, ok = 1;
    double t0 = now_ms();
    for (uint32_t cp = nxc_font_next_char(f, 0); cp != NXC_NO_CHAR; cp = nxc_font_next_char(f, cp + 1)) {
        if (n > 0 && cp <= prev) ok = 0;
        if (!nxc_font_has_char(f, cp)) ok = 0;
        prev = cp;
        if (++n > 200000) break;
    }
    fprintf(stderr, "char map: %u mapped code points, last U+%04X, %.1f ms\n", n, prev, now_ms() - t0);
    NXC_CHECK(ok && n > 1500 && n < 2478);
}

/* ============ Fallback ============ */

static void test_fallback(face_t *text, face_t *mono) {
    nxc_font_t *f = text->font;
    const char *s = "a\xE2\x94\x80" "b";               /* "a─b" */
    float before = nxc_text_width(f, 20, s, -1, NULL);
    const nxc_font_t *chain[1] = { mono->font };
    NXC_CHECK_OK(nxc_font_set_fallbacks(f, chain, 1));
    float after = nxc_text_width(f, 20, s, -1, NULL);
    float want = nxc_text_width(f, 20, "ab", -1, NULL) + nxc_text_width(mono->font, 20, "\xE2\x94\x80", -1, NULL);
    /* Kerning a-b differs once the box glyph sits between them. */
    float ab_kern = nxc_text_width(f, 20, "ab", -1, NULL) -
                    (nxc_text_width(f, 20, "a", -1, NULL) + nxc_text_width(f, 20, "b", -1, NULL));
    fprintf(stderr, "fallback: before %.2f after %.2f want %.2f\n", before, after, want - ab_kern);
    NXC_CHECK(fabsf(after - (want - ab_kern)) < 0.01f);
    NXC_CHECK(fabsf(after - before) > 0.5f);

    /* The box glyph draws ink with the fallback. */
    nxc_surface_t sf;
    NXC_CHECK_OK(nxc_surface_create(&sf, 120, 40, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &sf));
    NXC_CHECK_OK(nxc_surface_clear(&sf, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c, f, 20, 4, 28, "\xE2\x94\x80\xE2\x94\x80", -1, NXC_RGB(255, 255, 255), NULL));
    int ink = 0;
    for (int i = 0; i < 120 * 40; i++) ink += (sf.pixels[i] & 0xFF) > 128;
    NXC_CHECK(ink > 10);
    nxc_text_stats_t st;
    nxc_text_stats(&st);
    NXC_CHECK(st.fallbacks > 0);

    /* Bad chains are rejected and leave the chain unchanged. */
    const nxc_font_t *self[1] = { f };
    NXC_CHECK(nxc_font_set_fallbacks(f, self, 1) == NXC_ERR_INVALID_ARG);
    const nxc_font_t *five[5] = { mono->font, mono->font, mono->font, mono->font, mono->font };
    NXC_CHECK(nxc_font_set_fallbacks(f, five, 5) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxc_font_set_fallbacks(f, NULL, 1) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(fabsf(nxc_text_width(f, 20, s, -1, NULL) - after) < 0.01f);

    /* A destroyed fallback is skipped (no use after free under ASan). */
    face_t tmp = load("NXSansMono/NXSans-Mono.ttf");
    const nxc_font_t *chain2[1] = { tmp.font };
    NXC_CHECK_OK(nxc_font_set_fallbacks(f, chain2, 1));
    NXC_CHECK(fabsf(nxc_text_width(f, 20, s, -1, NULL) - after) < 0.01f);
    unload(&tmp);
    NXC_CHECK(fabsf(nxc_text_width(f, 20, s, -1, NULL) - before) < 0.01f);

    NXC_CHECK_OK(nxc_font_set_fallbacks(f, NULL, 0));
    NXC_CHECK(fabsf(nxc_text_width(f, 20, s, -1, NULL) - before) < 0.01f);
    nxc_surface_destroy(&sf);
}

/* ============ Hinting and curves ============ */

/* Total grayness (sum of min(v, 255 - v)) of a rendering: horizontal
 * edges that straddle two pixel rows add a lot of it, so hinting that
 * aligns flat tops/bottoms to the grid must lower it. */
static long grayness(const nxc_surface_t *s) {
    long n = 0;
    for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) {
            int v = (int)(s->pixels[y * s->stride + x] & 0xFF);
            n += v < 255 - v ? v : 255 - v;
        }
    return n;
}

static long render_grayness(nxc_font_t *f, float px, const char *txt) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 400, 40, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&c, f, px, 4, 28, txt, -1, NXC_RGB(255, 255, 255), NULL));
    long n = grayness(&s);
    nxc_surface_destroy(&s);
    return n;
}

static void test_hinting(face_t *text) {
    nxc_text_quality_t q;
    nxc_text_get_quality(&q);
    NXC_CHECK(q.vertical_hinting == 1 && q.subpixel_phases == 4 && q.gamma == 1);
    /* Linear coverage so only geometry differs. */
    nxc_text_quality_t off = { 0, 24.0f, 4, 0.0f, 0 }, on = { 1, 24.0f, 4, 0.0f, 0 };
    const float sizes[] = { 11.0f, 12.0f, 13.0f, 13.5f, 15.0f, 17.0f };
    long tot_off = 0, tot_on = 0;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        NXC_CHECK_OK(nxc_text_set_quality(&off));
        long a = render_grayness(text->font, sizes[i], "zzzz xxxx nnnn");
        NXC_CHECK_OK(nxc_text_set_quality(&on));
        long b = render_grayness(text->font, sizes[i], "zzzz xxxx nnnn");
        fprintf(stderr, "hinting %.1fpx: grayness %ld -> %ld\n", sizes[i], a, b);
        tot_off += a;
        tot_on += b;
    }
    NXC_CHECK(tot_on * 10 < tot_off * 9);      /* at least 10% crisper overall */
    /* Hinting changes only vertical geometry: layout width is identical. */
    NXC_CHECK_OK(nxc_text_set_quality(&off));
    float w0 = nxc_text_width(text->font, 13, "Hamburgefonstiv", -1, NULL);
    NXC_CHECK_OK(nxc_text_set_quality(&on));
    NXC_CHECK(nxc_text_width(text->font, 13, "Hamburgefonstiv", -1, NULL) == w0);

    /* Validation. */
    nxc_text_quality_t bad = on;
    bad.subpixel_phases = 3;
    NXC_CHECK(nxc_text_set_quality(&bad) == NXC_ERR_INVALID_ARG);
    bad = on; bad.contrast = 1.5f;
    NXC_CHECK(nxc_text_set_quality(&bad) == NXC_ERR_INVALID_ARG);
    bad = on; bad.hint_max_px = NAN;
    NXC_CHECK(nxc_text_set_quality(&bad) == NXC_ERR_INVALID_ARG);
    NXC_CHECK_OK(nxc_text_set_quality(NULL));
    nxc_text_get_quality(&q);
    NXC_CHECK(q.vertical_hinting == 1 && q.contrast == 0.5f && q.gamma == 1);
}

/* ============ Cache ring ============ */

static void test_cache_ring(face_t *text) {
    nxc_surface_t a, b;
    NXC_CHECK_OK(nxc_surface_create(&a, 300, 40, NXC_SURFACE_OPAQUE));
    NXC_CHECK_OK(nxc_surface_create(&b, 300, 40, NXC_SURFACE_OPAQUE));
    nxc_canvas_t ca, cb;
    NXC_CHECK_OK(nxc_canvas_init(&ca, &a));
    NXC_CHECK_OK(nxc_canvas_init(&cb, &b));
    NXC_CHECK_OK(nxc_surface_clear(&a, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_surface_clear(&b, NXC_RGB(0, 0, 0)));
    NXC_CHECK_OK(nxc_draw_text(&ca, text->font, 13, 4.25f, 28, "Quartz glyph cache", -1, NXC_RGB(255, 255, 255), NULL));
    /* Push far more bitmap bytes than the ring holds. */
    nxc_surface_t big;
    NXC_CHECK_OK(nxc_surface_create(&big, 1000, 200, NXC_SURFACE_OPAQUE));
    nxc_canvas_t cbig;
    NXC_CHECK_OK(nxc_canvas_init(&cbig, &big));
    for (int i = 0; i < 600; i++) {
        float px = 20.0f + (float)(i % 120) * 0.875f;
        NXC_CHECK_OK(nxc_draw_text(&cbig, text->font, px, (float)(i % 4) * 0.25f, 150,
                                   "The quick brown fox jumps", -1, NXC_RGB(255, 255, 255), NULL));
    }
    nxc_text_stats_t st;
    nxc_text_stats(&st);
    fprintf(stderr, "ring: %u hits %u misses %u evictions, arena %u KiB, %u slots\n",
            st.hits, st.misses, st.evictions, st.arena_bytes / 1024u, st.capacity);
    NXC_CHECK(st.evictions > 0 || st.misses * 900u > st.arena_bytes);
    NXC_CHECK_OK(nxc_draw_text(&cb, text->font, 13, 4.25f, 28, "Quartz glyph cache", -1, NXC_RGB(255, 255, 255), NULL));
    NXC_CHECK(memcmp(a.pixels, b.pixels, 300 * 40 * 4) == 0);
    nxc_surface_destroy(&big);
    nxc_surface_destroy(&a);
    nxc_surface_destroy(&b);
}

/* ============ Timing (reported, loosely bounded) ============ */

static void test_timing(face_t *text) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 800, 600, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    const char *line = "Settings  Network  Bluetooth  Display  Sound  Fonts";   /* 51 glyphs */
    NXC_CHECK_OK(nxc_draw_text(&c, text->font, 13, 10, 20, line, -1, NXC_RGB(240, 240, 240), NULL));
    double t0 = now_ms();
    const int reps = 2000;
    for (int i = 0; i < reps; i++)
        NXC_CHECK_OK(nxc_draw_text(&c, text->font, 13, 10, 20 + (float)(i % 28) * 20, line, -1,
                                   NXC_RGB(240, 240, 240), NULL));
    double hot = (now_ms() - t0) * 1000.0 / (reps * 51.0);
    /* Cold: every size new, so every glyph rasterizes. */
    t0 = now_ms();
    int cold_glyphs = 0;
    for (int i = 0; i < 60; i++) {
        NXC_CHECK_OK(nxc_draw_text(&c, text->font, 30.0f + (float)i * 0.125f, 10, 300, line, -1,
                                   NXC_RGB(240, 240, 240), NULL));
        cold_glyphs += 51;
    }
    double cold = (now_ms() - t0) * 1000.0 / cold_glyphs;
    fprintf(stderr, "timing (ASan build): cached %.2f us/glyph, uncached-size %.2f us/glyph\n", hot, cold);
    NXC_CHECK(hot < cold);
    nxc_surface_destroy(&s);
}

/* ============ Before / after specimen ============ */

static void specimen_pair(face_t *text, face_t *med, face_t *bold) {
    const int W = 420, H = 260;
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, W * 2, H, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    nxc_text_quality_t legacy = { 0, 24.0f, 4, 0.0f, 0 };
    for (int half = 0; half < 2; half++) {
        NXC_CHECK_OK(nxc_text_set_quality(half == 0 ? &legacy : NULL));
        float x0 = (float)(half * W);
        NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(x0, 0, W, H / 2), NXC_RGB(0x1c, 0x1f, 0x26)));
        NXC_CHECK_OK(nxc_fill_rect(&c, nxc_rect(x0, H / 2, W, H / 2), NXC_RGB(0xf5, 0xf5, 0xf7)));
        for (int band = 0; band < 2; band++) {
            nxc_color_t col = band == 0 ? NXC_RGB(0xF2, 0xF2, 0xF5) : NXC_RGB(0x1d, 0x1d, 0x1f);
            float y = (float)(band * H / 2) + 24;
            NXC_CHECK_OK(nxc_draw_text(&c, text->font, 11, x0 + 12, y, "Caption 11 \xC2\xB7 Connected \xC2\xB7 No media", -1, col, NULL));
            NXC_CHECK_OK(nxc_draw_text(&c, text->font, 12, x0 + 12, y + 20, "Footnote 12 \xC2\xB7 0 blocked \xC2\xB7 0 connections", -1, col, NULL));
            NXC_CHECK_OK(nxc_draw_text(&c, text->font, 13, x0 + 12, y + 41, "Body 13 \xC2\xB7 Firewall active, everything fine", -1, col, NULL));
            NXC_CHECK_OK(nxc_draw_text(&c, med->font, 13.5f, x0 + 12, y + 63, "File  Edit  View  Window  Help", -1, col, NULL));
            NXC_CHECK_OK(nxc_draw_text(&c, bold->font, 15, x0 + 12, y + 86, "Headline 15 Bluetooth Ethernet", -1, col, NULL));
        }
    }
    NXC_CHECK_OK(nxc_text_set_quality(NULL));
    nxc_test_write_png(&s, nxc_test_path("text_quality_before_after.png"));
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(1024, 1024);
    face_t text = load("NXSans/NXSans-Text.ttf");
    face_t med = load("NXSans/NXSans-UI.ttf");
    face_t semi = load("NXSans/NXSans-Display.ttf");
    face_t bold = load("NXSans/NXSans-Bold.ttf");
    face_t ital = load("NXSans/NXSans-Italic.ttf");
    face_t mono = load("NXSansMono/NXSans-Mono.ttf");
    test_info(&text, &semi, &mono, &ital);
    test_name_fuzz(&text);
    test_chars(&text);
    test_fallback(&text, &mono);
    test_hinting(&text);
    test_cache_ring(&text);
    test_timing(&text);
    specimen_pair(&text, &med, &bold);
    unload(&text);
    unload(&med);
    unload(&semi);
    unload(&bold);
    unload(&ital);
    unload(&mono);
    nxc_shutdown();
    return nxc_test_finish("quality");
}
