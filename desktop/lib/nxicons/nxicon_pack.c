/*
 * NeolyxOS - NXIcon pack: lookup and drawing of the linked system icons
 *
 * See include/nxicon_pack.h. The icon data is read-only and lives in the
 * desktop image (shell/nxicon_pack.S); this file only validates it and
 * hands out views into it, so there is nothing to allocate or free.
 *
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../../include/nxicon_pack.h"
#include "nxicons_internal.h"

/* ============ Linked data (shell/nxicon_pack.S) ============ */

#define NXPACK(sym, name) extern const uint8_t sym[], sym##_end[];
#include "nxicon_pack_index.h"
#undef NXPACK

typedef struct {
    const char    *name;
    const uint8_t *start;
    const uint8_t *end;
} pack_entry_t;

static const pack_entry_t g_entries[] = {
#define NXPACK(sym, name) { name, sym, sym##_end },
#include "nxicon_pack_index.h"
#undef NXPACK
};

#define PACK_COUNT ((int)(sizeof(g_entries) / sizeof(g_entries[0])))

/* NXIB: "NXIB" + version(1) + width(2, LE) + height(2, LE) + flags(1),
 * then width * height straight-alpha RGBA8 pixels. Same layout as the
 * loader in lib/nxi_render.c and tools/build_icon_pack.py. */
#define NXIB_HEADER_SIZE 10u
#define NXIB_VERSION     1u
#define NXIB_MAX_DIM     1024u

/* Per-entry validation result: 0 = not checked, 1 = valid, -1 = rejected */
static int8_t  g_state[PACK_COUNT];
static int32_t g_w[PACK_COUNT], g_h[PACK_COUNT];

static int validate(int i) {
    if (g_state[i]) return g_state[i] > 0;
    const pack_entry_t *e = &g_entries[i];
    g_state[i] = -1;
    if (!e->start || !e->end || e->end < e->start) return 0;
    uintptr_t len = (uintptr_t)(e->end - e->start);
    if (len < NXIB_HEADER_SIZE) return 0;
    const uint8_t *p = e->start;
    if (p[0] != 'N' || p[1] != 'X' || p[2] != 'I' || p[3] != 'B') return 0;
    if (p[4] != NXIB_VERSION || p[9] != 0) return 0;      /* flags: 0 = RGBA */
    uint32_t w = (uint32_t)p[5] | ((uint32_t)p[6] << 8);
    uint32_t h = (uint32_t)p[7] | ((uint32_t)p[8] << 8);
    if (!w || !h || w > NXIB_MAX_DIM || h > NXIB_MAX_DIM) return 0;
    if ((uint64_t)len != (uint64_t)NXIB_HEADER_SIZE + (uint64_t)w * h * 4u) return 0;
    g_w[i] = (int32_t)w;
    g_h[i] = (int32_t)h;
    g_state[i] = 1;
    return 1;
}

int nxicon_pack_init(int *rejected) {
    int ok = 0, bad = 0;
    for (int i = 0; i < PACK_COUNT; i++) {
        if (validate(i)) ok++;
        else bad++;
    }
    if (rejected) *rejected = bad;
    return ok;
}

int nxicon_pack_count(void) { return PACK_COUNT; }

static int name_eq(const char *a, const char *b) {
    int n = 0;
    while (*a && *b && n < NXICON_PACK_NAME_MAX) {
        if (*a != *b) return 0;
        a++; b++; n++;
    }
    return *a == 0 && *b == 0;
}

int nxicon_pack_find(const char *name, nxicon_pack_image_t *out) {
    if (!name || !out || !name[0]) return -1;
    for (int i = 0; i < PACK_COUNT; i++) {
        if (!name_eq(name, g_entries[i].name)) continue;
        if (!validate(i)) return -1;
        out->name = g_entries[i].name;
        out->rgba = g_entries[i].start + NXIB_HEADER_SIZE;
        out->width = g_w[i];
        out->height = g_h[i];
        return 0;
    }
    return -1;
}

nxc_status_t nxicon_pack_draw(nxc_canvas_t *c, const char *name, float x, float y,
                              float size, uint8_t alpha) {
    if (!c || !name || !nxi__finite(x) || !nxi__finite(y) || !nxi__finite(size) ||
        size < 1.0f || size > 4096.0f) {
        return NXC_ERR_INVALID_ARG;
    }
    nxicon_pack_image_t img;
    if (nxicon_pack_find(name, &img) != 0) return NXC_ERR_NOT_FOUND;
    if (alpha == 0) return NXC_OK;
    /* Pack icons are square; fit anything else by its longer side. */
    float k = size / (float)(img.width > img.height ? img.width : img.height);
    float dw = (float)img.width * k, dh = (float)img.height * k;
    nxc_filter_t f = ((float)img.width == dw) ? NXC_FILTER_NEAREST : NXC_FILTER_SMOOTH;
    return nxc_draw_rgba8(c, img.rgba, img.width, img.height, img.width * 4,
                          nxc_rect(x + (size - dw) * 0.5f, y + (size - dh) * 0.5f, dw, dh),
                          alpha, f);
}

/* ============ Folder names ============ */

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

const char *nxicon_pack_folder_name(const char *dir_path) {
    if (!dir_path) return "folder-generic";
    /* Last non-empty path component, bounded. */
    const char *s = dir_path, *last = 0;
    int len = 0, n = 0;
    while (*s && n < 1024) {
        if (*s != '/' && (s == dir_path || s[-1] == '/')) last = s;
        s++; n++;
    }
    if (!last) return "folder-generic";
    while (last[len] && last[len] != '/' && len < 32) len++;
    if (len == 0 || len >= 32) return "folder-generic";

    static const struct { const char *key; const char *icon; } map[] = {
        { "documents", "folder-documents" }, { "downloads", "folder-downloads" },
        { "music", "folder-music" },         { "videos", "folder-video" },
        { "movies", "folder-video" },        { "video", "folder-video" },
        { "desktop", "folder-desktop" },     { "library", "folder-library" },
        { "public", "folder-public" },       { "shared", "folder-shared" },
        { "system", "folder-system" },       { "users", "folder-user" },
        { "user", "folder-user" },           { "home", "folder-user" },
        { "trash", "folder-trash" },         { "bin", "folder-trash" },
        { "pictures", "folder-camera" },     { "photos", "folder-camera" },
        { "camera", "folder-camera" },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        const char *k = map[i].key;
        int j = 0;
        while (j < len && k[j] && lower(last[j]) == k[j]) j++;
        if (j == len && k[j] == 0) return map[i].icon;
    }
    return "folder-generic";
}
