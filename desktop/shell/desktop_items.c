/*
 * NeolyxOS - Desktop items (see include/desktop_items.h)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/desktop_items.h"
#include "../include/nxpath_relay.h"
#include "../include/app_view.h"
#include "../include/nxui.h"
#include "../include/nxicons.h"
#include "../include/nxicon_pack.h"
#include "../include/nxsyscall.h"

#define DI_ICON      52.0f
#define DI_CELL_W    96.0f
#define DI_CELL_H    92.0f
#define DI_TOP       44.0f          /* below the menu bar */
#define DI_RIGHT     12.0f
#define DI_BOTTOM   120.0f          /* keep clear of the dock */
#define DI_MAX       24
#define DI_POLL_MS 5000u            /* ask the relay again after this long */
#define DI_DBL_MS   450u

static struct {
    char     selected[NXRELAY_NAME_MAX + 1];
    uint64_t last_poll;
    uint64_t last_click_ms;
    int      last_click_idx;
    int      reserve_first;
} D = { "", 0, 0, -1, 0 };

static int di_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void di_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    for (; i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

/* Current ~/Desktop listing (cached by the relay; re-asked every 5 s). */
static const nxrelay_listing_t *di_listing(char *dir, int cap) {
    if (nxrelay_location(NXRELAY_LOC_DESKTOP, dir, cap) != NXRELAY_OK) return 0;
    uint64_t now = nx_gettime();
    uint32_t flags = 0;
    if (now - D.last_poll >= DI_POLL_MS) { flags = NXRELAY_REFRESH; D.last_poll = now; }
    const nxrelay_listing_t *l = nxrelay_list(dir, flags);
    return (l && l->status == NXRELAY_OK) ? l : 0;
}

/* Cell of item slot i (column-major from the top right). */
static nxc_rect_t di_cell(int slot, int32_t sw, int32_t sh) {
    int rows = (int)(((float)sh - DI_TOP - DI_BOTTOM) / DI_CELL_H);
    if (rows < 1) rows = 1;
    int col = slot / rows, row = slot % rows;
    float x = (float)sw - DI_RIGHT - DI_CELL_W * (float)(col + 1);
    float y = DI_TOP + DI_CELL_H * (float)row;
    return nxc_rect(x, y, DI_CELL_W, DI_CELL_H);
}

void desktop_items_render(nxc_canvas_t *c, int32_t sw, int32_t sh, int reserve_first) {
    if (!c || sw <= 0 || sh <= 0) return;
    D.reserve_first = reserve_first ? 1 : 0;
    char dir[NXRELAY_PATH_MAX];
    const nxrelay_listing_t *l = di_listing(dir, (int)sizeof(dir));
    if (!l) return;
    const nxui_theme_t *th = nxui_theme();
    int n = (int)l->count < DI_MAX ? (int)l->count : DI_MAX;
    for (int i = 0; i < n; i++) {
        const nxrelay_entry_t *e = &l->entries[i];
        nxc_rect_t cell = di_cell(i + D.reserve_first, sw, sh);
        float ix = cell.x + (cell.w - DI_ICON) * 0.5f, iy = cell.y + 4.0f;
        int sel = D.selected[0] && di_streq(D.selected, e->name);
        if (sel) nxc_fill_rrect(c, nxc_rect(ix - 6.0f, iy - 4.0f, DI_ICON + 12.0f, DI_ICON + 8.0f), 10.0f,
                                NXC_ARGB(70, 0xFF, 0xFF, 0xFF));
        if (e->kind == NXRELAY_KIND_FOLDER) {
            char p[NXRELAY_PATH_MAX];
            const char *icon = nxrelay_join(dir, e->name, p, (int)sizeof(p)) == NXRELAY_OK ?
                               nxicon_pack_folder_name(p) : "folder-generic";
            if (nxicon_pack_draw(c, icon, ix, iy, DI_ICON, 255) != NXC_OK)
                nxsym_draw(c, NXSYM_FOLDER, ix + DI_ICON * 0.5f, iy + DI_ICON * 0.5f, DI_ICON * 0.8f, th->accent, 0.0f);
        } else {
            nxc_rect_t page = nxc_rect(ix + DI_ICON * 0.16f, iy, DI_ICON * 0.68f, DI_ICON);
            nxui_elevation(c, page, 6.0f, 1);
            nxc_fill_rrect(c, page, 6.0f, NXC_RGB(0xFA, 0xFA, 0xFC));
            nxsym_draw(c, NXSYM_DOCUMENT, ix + DI_ICON * 0.5f, iy + DI_ICON * 0.5f, DI_ICON * 0.4f,
                       NXC_RGB(0x6E, 0x6E, 0x78), 0.0f);
        }
        nxc_rect_t lab = nxc_rect(cell.x + 2.0f, iy + DI_ICON + 6.0f, cell.w - 4.0f, 18.0f);
        if (sel) nxc_fill_rrect(c, lab, 5.0f, th->accent);
        /* Labels sit on the wallpaper: a soft shadow keeps them readable */
        if (!sel) nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(lab.x, lab.y + 1.0f, lab.w, lab.h), e->name,
                                    NXC_ARGB(150, 0, 0, 0), NXC_ALIGN_CENTER);
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, lab, e->name, NXC_RGB(0xFF, 0xFF, 0xFF), NXC_ALIGN_CENTER);
    }
}

int desktop_items_click(int32_t x, int32_t y, int32_t sw, int32_t sh) {
    char dir[NXRELAY_PATH_MAX];
    const nxrelay_listing_t *l = di_listing(dir, (int)sizeof(dir));
    if (!l) { D.selected[0] = '\0'; return 0; }
    int n = (int)l->count < DI_MAX ? (int)l->count : DI_MAX;
    for (int i = 0; i < n; i++) {
        nxc_rect_t cell = di_cell(i + D.reserve_first, sw, sh);
        if ((float)x < cell.x || (float)x >= cell.x + cell.w || (float)y < cell.y || (float)y >= cell.y + cell.h)
            continue;
        const nxrelay_entry_t *e = &l->entries[i];
        uint64_t now = nx_gettime();
        if (i == D.last_click_idx && di_streq(D.selected, e->name) && now - D.last_click_ms < DI_DBL_MS) {
            char target[NXRELAY_PATH_MAX];
            int ok = e->kind == NXRELAY_KIND_FOLDER
                         ? nxrelay_join(dir, e->name, target, (int)sizeof(target)) == NXRELAY_OK
                         : (di_strcpy(target, dir, (int)sizeof(target)), 1);
            if (ok && path_view_open_at(target) != 0) nx_debug_print("[DESKTOP] items: Path failed to open\n");
            D.last_click_idx = -1;
            return 1;
        }
        di_strcpy(D.selected, e->name, (int)sizeof(D.selected));
        D.last_click_idx = i;
        D.last_click_ms = now;
        return 1;
    }
    D.selected[0] = '\0';
    D.last_click_idx = -1;
    return 0;
}
