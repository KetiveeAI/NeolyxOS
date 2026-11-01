/*
 * Host test for NXGFX on NXCanvas (R1, docs/desktop/nxgfx-on-nxcanvas.md).
 *
 * Draws what NxUI widgets draw (rounded-rect buttons with centered
 * labels, text at several sizes, circles, lines, arcs) into an XRGB
 * buffer and checks:
 *   - before a font is registered, text falls back to the bitmap font and
 *     still draws;
 *   - a bad font is rejected and leaves the fallback in place;
 *   - with NX Sans registered, text is proportional ("iiii" narrower than
 *     "WWWW"), its height follows the font size, and glyph edges carry
 *     intermediate (anti-aliased) values;
 *   - circle and rounded-rect edges are anti-aliased, opaque fills stay
 *     exact, translucent fills blend instead of overwriting;
 *   - clipping holds: nothing is drawn outside the clip rect.
 * It writes the scene as a PPM so the result can be looked at.
 *
 * Build and run from the repository root:
 *   make -C gui/nxrender_c host-test-lib
 *   gcc -std=c11 -O1 -Wall -Wextra -Igui/nxrender_c/include \
 *       -o /tmp/nxgfx_canvas_test tests/host/nxgfx_canvas_test.c \
 *       gui/nxrender_c/build/host/libnxrender_host.a -lm
 *   /tmp/nxgfx_canvas_test resources/fonts/NXSans/NXSans-Text.ttf /tmp/nxgfx.ppm
 *
 * Build with -DNXGFX_LEGACY against a pre-R1 library to render the same
 * scene the old way (no font registration, no checks) for comparison.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "nxgfx/nxgfx.h"

#define W 720
#define H 400

static uint32_t g_px[W * H];
static int g_fail, g_checks;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

static void log_sink(const char *msg) { fputs(msg, stdout); }

static uint32_t px_at(int x, int y) { return g_px[y * W + x] & 0xFFFFFFu; }

static void clear(uint32_t xrgb) {
    for (int i = 0; i < W * H; i++) g_px[i] = xrgb;
}

/* Pixels in [x0,x1)x[y0,y1) that differ from bg, and those that are
 * neither bg nor fg (intermediate: anti-aliased edges). */
static void count(int x0, int y0, int x1, int y1, uint32_t bg, uint32_t fg,
                  int *changed, int *mid) {
    *changed = 0;
    *mid = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            uint32_t p = px_at(x, y);
            if (p != bg) (*changed)++;
            if (p != bg && p != fg) (*mid)++;
        }
    }
}

static int write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = { (unsigned char)(g_px[i] >> 16), (unsigned char)(g_px[i] >> 8),
                                 (unsigned char)g_px[i] };
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return -1; }
    }
    return fclose(f);
}

static unsigned char *read_file(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || n > 16L * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

/* The NxUI test window, drawn the way the widgets draw it. */
static void draw_scene(nx_context_t *ctx) {
    const nx_color_t bg = nx_rgb(24, 30, 42), text = nx_rgb(236, 240, 248);
    const nx_color_t accent = nx_rgb(120, 190, 255), muted = nx_rgb(150, 160, 180);
    nxgfx_clear(ctx, bg);

    nx_font_t *title = nxgfx_font_default(ctx, 28);
    nx_font_t *body = nxgfx_font_default(ctx, 14);
    nx_font_t *small = nxgfx_font_default(ctx, 12);
    if (!title || !body || !small) { check(0, "font_default"); return; }

    nxgfx_draw_text(ctx, "NxUI in its own window", (nx_point_t){ 24, 16 }, title, text);
    nxgfx_draw_text(ctx, "Button 3 clicks 3", (nx_point_t){ 24, 62 }, body, accent);

    const char *names[6] = { "One", "Two", "Three", "Four", "Five", "Six" };
    const nx_color_t fills[3] = { nx_rgb(96, 66, 140), nx_rgb(84, 100, 128), nx_rgb(132, 92, 150) };
    for (int i = 0; i < 6; i++) {
        nx_rect_t r = nx_rect(24 + (i % 3) * 124, 92 + (i / 3) * 56, 112, 44);
        nxgfx_fill_rounded_rect(ctx, r, fills[i % 3], 10);
        nx_size_t ts = nxgfx_measure_text(ctx, names[i], body);
        nx_point_t tp = { r.x + ((int32_t)r.width - (int32_t)ts.width) / 2,
                          r.y + ((int32_t)r.height - (int32_t)ts.height) / 2 };
        nxgfx_draw_text(ctx, names[i], tp, body, text);
    }
    nxgfx_draw_text(ctx, "Keys: hi  |  Sizes 12 14 20", (nx_point_t){ 24, 210 }, small, muted);

    int y = 236;
    const uint32_t sizes[4] = { 12, 14, 20, 32 };
    for (int i = 0; i < 4; i++) {
        nx_font_t *f = nxgfx_font_default(ctx, sizes[i]);
        if (!f) { check(0, "font_default size"); continue; }
        nxgfx_draw_text(ctx, "Quartz glyphs: crisp, kerned AV To", (nx_point_t){ 24, y }, f, text);
        y += (int)nxgfx_measure_text(ctx, "Ag", f).height + 4;
        nxgfx_font_destroy(f);
    }

    nxgfx_fill_circle(ctx, (nx_point_t){ 520, 120 }, 40, nx_rgb(232, 102, 10));
    nxgfx_fill_circle(ctx, (nx_point_t){ 620, 120 }, 22, nx_rgb(16, 185, 129));
    nxgfx_draw_line(ctx, (nx_point_t){ 470, 190 }, (nx_point_t){ 690, 230 }, accent, 2);
    nxgfx_draw_line(ctx, (nx_point_t){ 470, 250 }, (nx_point_t){ 690, 250 }, muted, 1);
    nxgfx_draw_arc(ctx, (nx_point_t){ 580, 320 }, 46, 200.0f, 520.0f, accent, 6);

    nxgfx_font_destroy(title);
    nxgfx_font_destroy(body);
    nxgfx_font_destroy(small);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s NXSans-Text.ttf out.ppm\n", argv[0]);
        return 2;
    }
    nx_context_t *ctx = nxgfx_init(g_px, W, H, W * 4);
    if (!ctx) { printf("FAIL nxgfx_init\n"); return 1; }

#ifdef NXGFX_LEGACY
    draw_scene(ctx);
    if (write_ppm(argv[2]) != 0) { printf("FAIL write %s\n", argv[2]); return 1; }
    printf("legacy scene written to %s\n", argv[2]);
    nxgfx_destroy(ctx);
    return 0;
#else
    nxgfx_set_log_sink(log_sink);
    /* The engine started in nxgfx_init before the sink was set; a second
     * context logs nothing new, so just check the state. */
    check(nxgfx_engine_ready(), "NXCanvas engine ready");

    /* 1. Bitmap fallback before any font is registered */
    clear(0x000000);
    nx_font_t *f14 = nxgfx_font_default(ctx, 14);
    check(f14 != NULL, "font_default before registration");
    if (!f14) return 1;
    nx_size_t wi = nxgfx_measure_text(ctx, "iiii", f14);
    nx_size_t ww = nxgfx_measure_text(ctx, "WWWW", f14);
    check(wi.width == ww.width && wi.width == 32, "bitmap fallback is fixed width 8 px");
    nxgfx_draw_text(ctx, "Hi", (nx_point_t){ 4, 4 }, f14, NX_COLOR_WHITE);
    int changed, mid;
    count(0, 0, 40, 30, 0x000000, 0xFFFFFF, &changed, &mid);
    check(changed > 20, "bitmap fallback draws");

    /* 2. A bad font is rejected */
    static uint8_t junk[4096];
    memset(junk, 0xA5, sizeof junk);
    check(nxgfx_font_register(junk, sizeof junk) < 0, "garbage font rejected");
    check(nxgfx_font_register(NULL, 4096) < 0, "NULL font rejected");
    check(nxgfx_font_register(junk, 10) < 0, "tiny font rejected");
    wi = nxgfx_measure_text(ctx, "iiii", f14);
    check(wi.width == 32, "still bitmap after rejected font");

    /* 3. NX Sans */
    uint32_t fsize = 0;
    unsigned char *fdata = read_file(argv[1], &fsize);
    check(fdata != NULL, "read font file");
    if (!fdata) return 1;
    check(nxgfx_font_register(fdata, fsize) == 0, "NX Sans registered");

    wi = nxgfx_measure_text(ctx, "iiii", f14);
    ww = nxgfx_measure_text(ctx, "WWWW", f14);
    check(wi.width > 0 && ww.width > 2 * wi.width, "outline text is proportional");
    check(wi.height >= 14 && wi.height <= 24, "14 px line box height in range");
    nx_font_t *f28 = nxgfx_font_default(ctx, 28);
    if (!f28) return 1;
    nx_size_t big = nxgfx_measure_text(ctx, "Ag", f28);
    nx_size_t sm = nxgfx_measure_text(ctx, "Ag", f14);
    check(big.height > sm.height + 8, "line height follows font size");
    check(nxgfx_measure_text(ctx, "", f14).width == 0, "empty string measures 0");

    clear(0x000000);
    nxgfx_draw_text(ctx, "Hamburgefonstiv", (nx_point_t){ 4, 4 }, f28, NX_COLOR_WHITE);
    count(0, 0, 400, 50, 0x000000, 0xFFFFFF, &changed, &mid);
    check(changed > 200, "outline text draws");
    check(mid > changed / 5, "outline text edges are anti-aliased");
    count(0, 50, W, H, 0x000000, 0xFFFFFF, &changed, &mid);
    check(changed == 0, "text stays inside its line box");

    /* UTF-8 does not break the call */
    clear(0x000000);
    nxgfx_draw_text(ctx, "caf\xC3\xA9 \xE2\x86\x92 ok", (nx_point_t){ 4, 4 }, f14, NX_COLOR_WHITE);
    count(0, 0, 200, 30, 0x000000, 0xFFFFFF, &changed, &mid);
    check(changed > 30, "UTF-8 text draws");

    /* 4. Shapes */
    clear(0x000000);
    nxgfx_fill_circle(ctx, (nx_point_t){ 100, 100 }, 30, NX_COLOR_WHITE);
    count(60, 60, 140, 140, 0x000000, 0xFFFFFF, &changed, &mid);
    check(mid > 40, "circle edge anti-aliased");
    check(px_at(100, 100) == 0xFFFFFF && px_at(100, 75) == 0xFFFFFF, "circle interior solid");
    check(px_at(100, 66) == 0x000000 && px_at(135, 100) == 0x000000, "circle exterior untouched");

    clear(0x000000);
    nxgfx_fill_rounded_rect(ctx, nx_rect(200, 50, 100, 40), NX_COLOR_WHITE, 12);
    count(195, 45, 305, 95, 0x000000, 0xFFFFFF, &changed, &mid);
    check(mid > 8, "rounded corners anti-aliased");
    check(px_at(250, 50) == 0xFFFFFF && px_at(250, 89) == 0xFFFFFF, "rrect straight edges exact");
    check(px_at(250, 49) == 0x000000 && px_at(250, 90) == 0x000000, "rrect no bleed");
    check(px_at(200, 50) == 0x000000, "rrect corner cut");

    clear(0x000000);
    nxgfx_fill_rect(ctx, nx_rect(10, 10, 20, 20), nx_rgb(10, 20, 30));
    check(px_at(10, 10) == 0x0A141E && px_at(29, 29) == 0x0A141E && px_at(30, 30) == 0,
          "opaque fill exact");
    nxgfx_fill_rect(ctx, nx_rect(10, 10, 20, 20), nx_rgba(255, 255, 255, 128));
    uint32_t p = px_at(15, 15);
    check(((p >> 16) & 0xFF) > 120 && ((p >> 16) & 0xFF) < 150, "translucent fill blends");
    nxgfx_fill_rect(ctx, nx_rect(40, 10, 20, 20), nx_rgba(255, 0, 0, 0));
    check(px_at(45, 15) == 0, "fully transparent fill draws nothing");

    clear(0x000000);
    nxgfx_draw_line(ctx, (nx_point_t){ 10, 10 }, (nx_point_t){ 50, 10 }, NX_COLOR_WHITE, 1);
    check(px_at(10, 10) == 0xFFFFFF && px_at(50, 10) == 0xFFFFFF && px_at(30, 9) == 0 &&
          px_at(30, 11) == 0, "horizontal hairline is one exact pixel row");

    /* 5. Clipping */
    clear(0x000000);
    nxgfx_set_clip(ctx, nx_rect(100, 100, 50, 50));
    nxgfx_fill_circle(ctx, (nx_point_t){ 125, 125 }, 60, NX_COLOR_WHITE);
    nxgfx_draw_text(ctx, "Clipped text", (nx_point_t){ 80, 110 }, f28, NX_COLOR_WHITE);
    nxgfx_clear_clip(ctx);
    int outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if ((x < 100 || x >= 150 || y < 100 || y >= 150) && px_at(x, y) != 0) outside++;
    check(outside == 0, "nothing drawn outside the clip");
    check(px_at(125, 125) == 0xFFFFFF, "inside the clip drawn");

    nxgfx_font_destroy(f14);
    nxgfx_font_destroy(f28);

    draw_scene(ctx);
    if (write_ppm(argv[2]) != 0) check(0, "write scene");

    nxgfx_destroy(ctx);
    /* fdata stays allocated: the registered face points into it */
    printf("%s: %d checks\n", g_fail ? "FAIL" : "PASS", g_checks);
    return g_fail;
#endif
}
