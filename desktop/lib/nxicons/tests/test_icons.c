/*
 * NeolyxOS - NXIcons contact sheet and API checks.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <string.h>
#include "../../nxcanvas/tests/nxc_test.h"
#include "../../../include/nxicons.h"
#include "../../../include/nxicon_pack.h"

static void sheet(nxc_color_t bg, nxc_color_t fg, const char *name) {
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 1240, 720, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, bg));
    /* Symbols at 16, 24, 32 in rows of 15 */
    const float sizes[3] = { 16, 24, 32 };
    float y = 30;
    for (int sz = 0; sz < 3; sz++) {
        float step = sizes[sz] + 18;
        for (int i = 0; i < NXSYM_COUNT; i++) {
            int col = i % 15, row = i / 15;
            float cx = 30 + col * (step + 24), cy = y + row * step;
            NXC_CHECK_OK(nxsym_draw(&c, (nxsym_t)i, cx, cy, sizes[sz], fg, 0.7f));
        }
        y += 3 * step + 10;
    }
    /* App icons at 32, 48, 64, 96, 128 */
    const float asz[5] = { 32, 48, 64, 96, 128 };
    float x = 24;
    y = 440;
    for (int sz = 0; sz < 5; sz++) {
        float ax = x;
        for (int i = 0; i < NXAPP_COUNT; i++) {
            if (sz >= 3 && i >= 9) break;
            NXC_CHECK_OK(nxapp_icon_draw(&c, (nxapp_icon_t)i, ax, y, asz[sz], 255));
            ax += asz[sz] + (sz < 3 ? 8 : 12);
        }
        if (sz == 2) { y = 440 + 32 + 48 + 64 + 30; x = 24; }
        else if (sz == 3) { x = 24 + 9 * (96 + 12) + 20; y -= 0; }
        else y += asz[sz] + 8;
    }
    nxc_test_write_png(&s, nxc_test_path(name));
    nxc_surface_destroy(&s);
}

static void zoom(void) {
    /* 4x zoom of a few 16 px symbols and a 32 px app icon row on dark. */
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 320, 64, NXC_SURFACE_OPAQUE));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, NXC_RGB(0x1c, 0x1f, 0x26)));
    nxsym_t pick[8] = { NXSYM_WIFI, NXSYM_BATTERY, NXSYM_VOLUME, NXSYM_SEARCH, NXSYM_CONTROL_CENTER, NXSYM_BELL, NXSYM_MOON, NXSYM_GEAR };
    for (int i = 0; i < 8; i++) NXC_CHECK_OK(nxsym_draw(&c, pick[i], 16 + i * 36, 16, 16, NXC_RGB(255, 255, 255), 0.66f));
    for (int i = 0; i < 8; i++) NXC_CHECK_OK(nxapp_icon_draw(&c, (nxapp_icon_t)i, 4 + i * 38, 30, 32, 255));
    nxc_surface_t z;
    NXC_CHECK_OK(nxc_surface_create(&z, 1280, 256, NXC_SURFACE_OPAQUE));
    nxc_canvas_t zc;
    NXC_CHECK_OK(nxc_canvas_init(&zc, &z));
    NXC_CHECK_OK(nxc_draw_surface(&zc, &s, nxc_irect(0, 0, 320, 64), nxc_rect(0, 0, 1280, 256), 255, NXC_FILTER_NEAREST));
    nxc_test_write_png(&z, nxc_test_path("icons_zoom4.png"));
    nxc_surface_destroy(&z);
    nxc_surface_destroy(&s);
}

/* The linked NeolyxOS icon pack: every entry valid, lookups exact,
 * folder names mapped, and app icons drawn from pack art (not the
 * vector fallback). */
static void pack_checks(void) {
    int rejected = -1;
    int ok = nxicon_pack_init(&rejected);
    NXC_CHECK(rejected == 0);
    NXC_CHECK(ok == nxicon_pack_count() && ok > 0);
    nxicon_pack_image_t img;
    for (int i = 0; i < NXAPP_COUNT; i++) {
        char name[NXICON_PACK_NAME_MAX] = "app-";
        const char *b = nxapp_icon_name((nxapp_icon_t)i);
        int n = 4;
        while (*b && n < NXICON_PACK_NAME_MAX - 1) name[n++] = *b++;
        name[n] = 0;
        if (nxicon_pack_find(name, &img) != 0) fprintf(stderr, "pack missing %s\n", name);
        NXC_CHECK(nxicon_pack_find(name, &img) == 0 && img.width == img.height && img.width >= 96);
    }
    NXC_CHECK(nxicon_pack_find("app-file", &img) == -1);      /* exact names only */
    NXC_CHECK(nxicon_pack_find("", &img) == -1);
    NXC_CHECK(nxicon_pack_find(NULL, &img) == -1);
    NXC_CHECK(nxicon_pack_find("folder-generic", &img) == 0);
    NXC_CHECK(nxicon_pack_find("device-external", &img) == 0);

    static const struct { const char *path, *icon; } dirs[] = {
        { "/Users/swa/Documents", "folder-documents" }, { "/Users/swa/Downloads/", "folder-downloads" },
        { "Music", "folder-music" }, { "/Users/swa/Movies", "folder-video" },
        { "/Users/swa/Pictures", "folder-camera" }, { "/System", "folder-system" },
        { "/Users", "folder-user" }, { "/projects/neolyx", "folder-generic" },
        { "/", "folder-generic" }, { "", "folder-generic" }, { NULL, "folder-generic" },
        { "/Users/swa/DocumentsOld", "folder-generic" },
    };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        const char *got = nxicon_pack_folder_name(dirs[i].path);
        if (strcmp(got, dirs[i].icon) != 0)
            fprintf(stderr, "folder %s -> %s (want %s)\n", dirs[i].path ? dirs[i].path : "(null)", got, dirs[i].icon);
        NXC_CHECK(strcmp(got, dirs[i].icon) == 0);
        NXC_CHECK(nxicon_pack_find(got, &img) == 0);
    }

    /* App icon pixels come from the pack: centre of the 128 px Files icon
     * matches the pack art closely (the vector fallback is blue). */
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 128, 128, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK_OK(nxc_surface_clear(&s, 0));
    NXC_CHECK_OK(nxapp_icon_draw(&c, NXAPP_FILES, 0, 0, 128, 255));
    NXC_CHECK(nxicon_pack_find("app-files", &img) == 0);
    int worst = 0;
    for (int y = 8; y < 120; y += 16) {
        for (int x = 8; x < 120; x += 16) {
            uint32_t px = nxc_test_pixel(&s, x, y);
            const uint8_t *src = img.rgba + ((size_t)y * img.width + x) * 4;
            if (src[3] != 255) continue;   /* compare opaque pixels only */
            int d[3] = { (int)((px >> 16) & 255) - src[0], (int)((px >> 8) & 255) - src[1], (int)(px & 255) - src[2] };
            for (int k = 0; k < 3; k++) { int a = d[k] < 0 ? -d[k] : d[k]; if (a > worst) worst = a; }
        }
    }
    if (worst > 6) fprintf(stderr, "app-files differs from pack art by %d\n", worst);
    NXC_CHECK(worst <= 6);
    NXC_CHECK(nxicon_pack_draw(&c, "nope", 0, 0, 32, 255) == NXC_ERR_NOT_FOUND);
    NXC_CHECK(nxicon_pack_draw(NULL, "folder-generic", 0, 0, 32, 255) == NXC_ERR_INVALID_ARG);
    nxc_surface_destroy(&s);
}

int main(void) {
    nxc_test_init(1280, 1280);
    NXC_CHECK_OK(nxicons_init(0));
    NXC_CHECK(nxapp_icon_from_name("Files") == NXAPP_FILES);
    NXC_CHECK(nxapp_icon_from_name("system settings") == NXAPP_SETTINGS);
    NXC_CHECK(nxapp_icon_from_name("Font Manager") == NXAPP_FONTS);
    NXC_CHECK(nxapp_icon_from_name("Whatever") == NXAPP_GENERIC);
    NXC_CHECK(nxapp_icon_from_name(NULL) == NXAPP_GENERIC);
    nxc_surface_t s;
    NXC_CHECK_OK(nxc_surface_create(&s, 64, 64, 0));
    nxc_canvas_t c;
    NXC_CHECK_OK(nxc_canvas_init(&c, &s));
    NXC_CHECK(nxsym_draw(&c, NXSYM_COUNT, 0, 0, 16, 0xFFFFFFFF, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxsym_draw(NULL, NXSYM_WIFI, 0, 0, 16, 0xFFFFFFFF, 0) == NXC_ERR_INVALID_ARG);
    NXC_CHECK(nxapp_icon_draw(&c, NXAPP_FILES, 0, 0, 2, 255) == NXC_ERR_INVALID_ARG);
    /* Every symbol renders some ink at 16 px. */
    for (int i = 0; i < NXSYM_COUNT; i++) {
        NXC_CHECK_OK(nxc_surface_clear(&s, 0));
        NXC_CHECK_OK(nxsym_draw(&c, (nxsym_t)i, 32, 32, 16, 0xFFFFFFFF, 0.5f));
        double ink = 0;
        for (int p = 0; p < 64 * 64; p++) ink += (s.pixels[p] >> 24) / 255.0;
        if (!(ink > 8 && ink < 200)) fprintf(stderr, "symbol %s ink %.1f\n", nxsym_name((nxsym_t)i), ink);
        NXC_CHECK(ink > 8 && ink < 200);
    }
    nxc_surface_destroy(&s);
    pack_checks();
    sheet(NXC_RGB(0x1c, 0x1f, 0x26), NXC_RGB(0xF0, 0xF4, 0xFF), "icons_dark.png");
    sheet(NXC_RGB(0xf5, 0xf5, 0xf7), NXC_RGB(0x10, 0x14, 0x1E), "icons_light.png");
    zoom();
    nxc_shutdown();
    return nxc_test_finish("icons");
}
