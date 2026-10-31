/*
 * NeolyxOS - Path: the system file browser
 *
 * Path shows the folders and files of the system the way a file manager
 * does: a sidebar of places (Home, Desktop, Documents, ..., Computer), a
 * toolbar with back / forward / enclosing folder / reload and the current
 * location, and a list of the folder's items (folders first).
 *
 *   click            select an item or a sidebar place
 *   double-click     open a folder
 *   Return           open the selected folder
 *   Backspace        enclosing folder
 *   Up / Down        move the selection
 *   F5               reload from the system
 *
 * All file-system access goes through the Path relay (nxpath_relay.h),
 * which validates paths, talks to the kernel and caches listings. Path
 * itself never calls the kernel.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxpath_relay.h"
#include "nxframe.h"
#include "nxui.h"
#include "nxicons.h"
#include "nxicon_pack.h"
#include "nxsyscall.h"

extern int desktop_launch_app(const char *name);

/* ============ Layout ============ */

#define PV_TOOLBAR_H   44.0f
#define PV_SIDEBAR_W  184.0f
#define PV_STATUS_H    NXFRAME_STRIP_H
#define PV_HEADER_H    26.0f
#define PV_ROW_H       28.0f
#define PV_SIDE_ROW_H  28.0f
#define PV_HIST_MAX    24
#define PV_DBL_MS      450u

typedef struct {
    nxc_rect_t toolbar, back, fwd, up, reload, crumb;
    nxc_rect_t sidebar, main, header, list, status;
    int rows_visible;
} pv_layout_t;

/* Sidebar: places, then locations (a section header sits between). */
static const nxrelay_loc_t k_places[] = {
    NXRELAY_LOC_HOME, NXRELAY_LOC_DESKTOP, NXRELAY_LOC_DOCUMENTS, NXRELAY_LOC_DOWNLOADS,
    NXRELAY_LOC_PICTURES, NXRELAY_LOC_MUSIC, NXRELAY_LOC_APPLICATIONS,
};
static const nxrelay_loc_t k_locations[] = {
    NXRELAY_LOC_ROOT, NXRELAY_LOC_SYSTEM, NXRELAY_LOC_VOLUMES,
};
#define PV_NPLACES ((int)(sizeof(k_places) / sizeof(k_places[0])))
#define PV_NLOCS   ((int)(sizeof(k_locations) / sizeof(k_locations[0])))

/* ============ State ============ */

static struct {
    uint32_t win_id;
    char     path[NXRELAY_PATH_MAX];
    char     hist[PV_HIST_MAX][NXRELAY_PATH_MAX];
    int      hist_len, hist_pos;
    int      selected;          /* index into the listing, -1 none */
    int      scroll;            /* first visible row */
    int      hover_row, hover_side, hover_btn;
    uint32_t gen_seen;
    uint64_t last_click_ms;
    int      last_click_row;
    int      show_hidden;
    char     pending[NXRELAY_PATH_MAX];   /* open_at before the window exists */
} P;

static void pv_log(const char *msg, const char *path) {
    char line[NXRELAY_PATH_MAX + 64];
    int n = 0;
    const char *parts[3] = { "[PATH] ", msg, path ? " " : "" };
    for (int p = 0; p < 3; p++)
        for (const char *s = parts[p]; *s && n < (int)sizeof(line) - 2; s++) line[n++] = *s;
    if (path) for (const char *s = path; *s && n < (int)sizeof(line) - 2; s++) line[n++] = *s;
    line[n++] = '\n';
    line[n] = '\0';
    nx_debug_print(line);
}

static void pv_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    for (; i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

static int pv_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static uint32_t pv_flags(void) { return P.show_hidden ? NXRELAY_SHOW_HIDDEN : 0u; }

static const nxrelay_listing_t *pv_listing(void) { return nxrelay_list(P.path, pv_flags()); }

/* ============ Navigation ============ */

/* Go to `path`. push: record in history (not for back/forward). */
static int pv_go(const char *path, int push) {
    char canon[NXRELAY_PATH_MAX];
    if (nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) {
        pv_log("rejected location", path);
        return -1;
    }
    pv_strcpy(P.path, canon, (int)sizeof(P.path));
    P.selected = -1;
    P.scroll = 0;
    P.last_click_row = -1;
    if (push) {
        if (P.hist_len > 0 && pv_streq(P.hist[P.hist_pos], canon)) return 0;
        if (P.hist_len > 0) P.hist_len = P.hist_pos + 1;     /* drop forward history */
        if (P.hist_len >= PV_HIST_MAX) {
            for (int i = 1; i < PV_HIST_MAX; i++) pv_strcpy(P.hist[i - 1], P.hist[i], NXRELAY_PATH_MAX);
            P.hist_len = PV_HIST_MAX - 1;
        }
        pv_strcpy(P.hist[P.hist_len], canon, NXRELAY_PATH_MAX);
        P.hist_pos = P.hist_len++;
    }
    /* Opening a folder always asks the system for a fresh listing. */
    (void)nxrelay_list(P.path, pv_flags() | NXRELAY_REFRESH);
    return 0;
}

static void pv_back(void) {
    if (P.hist_pos > 0) { P.hist_pos--; pv_go(P.hist[P.hist_pos], 0); }
}

static void pv_forward(void) {
    if (P.hist_pos + 1 < P.hist_len) { P.hist_pos++; pv_go(P.hist[P.hist_pos], 0); }
}

static void pv_up(void) {
    char parent[NXRELAY_PATH_MAX];
    if (pv_streq(P.path, "/")) return;
    if (nxrelay_parent(P.path, parent, (int)sizeof(parent)) == NXRELAY_OK) pv_go(parent, 1);
}

static void pv_open_selected(void) {
    const nxrelay_listing_t *l = pv_listing();
    if (P.selected < 0 || (uint32_t)P.selected >= l->count) return;
    const nxrelay_entry_t *e = &l->entries[P.selected];
    if (e->kind != NXRELAY_KIND_FOLDER) return;   /* files open in apps once a launcher exists */
    char next[NXRELAY_PATH_MAX];
    if (nxrelay_join(P.path, e->name, next, (int)sizeof(next)) == NXRELAY_OK) pv_go(next, 1);
    else pv_log("cannot open item in", P.path);
}

/* The best starting place: Home if it exists, else Computer. */
static void pv_start_path(char *out, int cap) {
    nxrelay_kind_t kind;
    if (nxrelay_location(NXRELAY_LOC_HOME, out, cap) == NXRELAY_OK &&
        nxrelay_stat(out, &kind, NULL) == NXRELAY_OK && kind == NXRELAY_KIND_FOLDER) return;
    pv_strcpy(out, "/", cap);
}

/* ============ Layout ============ */

static pv_layout_t pv_layout(const desktop_window_t *win) {
    pv_layout_t L;
    nxc_rect_t r = app_view_content_rect(win);
    L.toolbar = nxc_rect(r.x, r.y, r.w, PV_TOOLBAR_H);
    float by = r.y + (PV_TOOLBAR_H - 28.0f) * 0.5f;
    float bx = r.x + PV_SIDEBAR_W + 10.0f;
    L.back   = nxc_rect(bx, by, 28.0f, 28.0f);
    L.fwd    = nxc_rect(bx + 30.0f, by, 28.0f, 28.0f);
    L.up     = nxc_rect(bx + 64.0f, by, 28.0f, 28.0f);
    L.reload = nxc_rect(r.x + r.w - 38.0f, by, 28.0f, 28.0f);   /* reserved (F5 reloads) */
    L.crumb  = nxc_rect(bx + 100.0f, by, L.reload.x - (bx + 100.0f) - 8.0f, 28.0f);
    /* Neolyx frame: the status strip spans the whole window bottom */
    L.status  = nxc_rect(r.x, r.y + r.h - PV_STATUS_H, r.w, PV_STATUS_H);
    L.sidebar = nxc_rect(r.x, r.y + PV_TOOLBAR_H, PV_SIDEBAR_W, r.h - PV_TOOLBAR_H - PV_STATUS_H);
    L.main    = nxc_rect(r.x + PV_SIDEBAR_W, r.y + PV_TOOLBAR_H, r.w - PV_SIDEBAR_W,
                         r.h - PV_TOOLBAR_H - PV_STATUS_H);
    L.header  = nxc_rect(L.main.x, L.main.y, L.main.w, PV_HEADER_H);
    L.list    = nxc_rect(L.main.x, L.header.y + PV_HEADER_H, L.main.w,
                         L.status.y - (L.header.y + PV_HEADER_H));
    L.rows_visible = L.list.h > 0.0f ? (int)(L.list.h / PV_ROW_H) : 0;
    if (L.rows_visible < 1) L.rows_visible = 1;
    return L;
}

static int pv_in(nxc_rect_t r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Sidebar row geometry: header "Places" (row -1), places, header
 * "Locations", locations. Returns the slot index (places then locations)
 * at y, or -1. */
static float pv_side_y(const pv_layout_t *L, int slot) {
    float y = L->sidebar.y + 30.0f;                      /* below "Places" */
    if (slot >= PV_NPLACES) y += 30.0f;                  /* "Locations" header */
    return y + (float)slot * PV_SIDE_ROW_H;
}

static int pv_side_at(const pv_layout_t *L, float x, float y) {
    if (!pv_in(L->sidebar, x, y)) return -1;
    for (int s = 0; s < PV_NPLACES + PV_NLOCS; s++) {
        float ry = pv_side_y(L, s);
        if (y >= ry && y < ry + PV_SIDE_ROW_H) return s;
    }
    return -1;
}

static nxrelay_loc_t pv_side_loc(int slot) {
    return slot < PV_NPLACES ? k_places[slot] : k_locations[slot - PV_NPLACES];
}

/* ============ Drawing ============ */

static void pv_size_text(const nxrelay_entry_t *e, char *out, int cap) {
    static const char *const u[] = { "bytes", "KB", "MB", "GB", "TB" };
    if (e->kind == NXRELAY_KIND_FOLDER || !e->size_known) { pv_strcpy(out, "--", cap); return; }
    uint64_t v = e->size, frac = 0;
    int k = 0;
    while (v >= 1000u && k < 4) { frac = (v % 1000u) / 100u; v /= 1000u; k++; }
    char num[24];
    int n = 0;
    uint64_t t = v;
    do { num[n++] = (char)('0' + t % 10u); t /= 10u; } while (t && n < 20);
    int o = 0;
    while (n && o < cap - 1) out[o++] = num[--n];
    if (k > 0 && v < 100u && o < cap - 3) { out[o++] = '.'; out[o++] = (char)('0' + frac); }
    if (o < cap - 1) out[o++] = ' ';
    for (const char *s = u[k]; *s && o < cap - 1; s++) out[o++] = *s;
    out[o] = '\0';
}

static const char *pv_kind_text(const nxrelay_entry_t *e) {
    switch (e->kind) {
        case NXRELAY_KIND_FOLDER: return "Folder";
        case NXRELAY_KIND_LINK:   return "Alias";
        case NXRELAY_KIND_DEVICE: return "Device";
        default: break;
    }
    /* Kind from the extension */
    const char *dot = 0;
    for (const char *s = e->name; *s; s++) if (*s == '.') dot = s;
    if (!dot || dot == e->name) return "Document";
    static const struct { const char *ext, *kind; } k[] = {
        { ".txt", "Plain Text" }, { ".md", "Markdown" }, { ".png", "PNG Image" }, { ".jpg", "JPEG Image" },
        { ".nxi", "NeolyxOS Icon" }, { ".svg", "SVG Image" }, { ".ttf", "Font" }, { ".otf", "Font" },
        { ".elf", "Executable" }, { ".app", "Application" }, { ".npa", "NeolyxOS Package" },
        { ".conf", "Configuration" }, { ".nlc", "Configuration" }, { ".log", "Log" }, { ".c", "C Source" },
        { ".h", "C Header" }, { ".bin", "Binary" }, { ".efi", "EFI Application" }, { ".pdf", "PDF Document" }, { ".zip", "ZIP Archive" },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        const char *a = dot, *b = k[i].ext;
        while (*a && *b && (*a == *b || (*a >= 'A' && *a <= 'Z' && *a + 32 == *b))) { a++; b++; }
        if (!*a && !*b) return k[i].kind;
    }
    return "Document";
}

static void pv_icon(nxc_canvas_t *c, const char *dir, const nxrelay_entry_t *e, float x, float y, float s) {
    if (e->kind == NXRELAY_KIND_FOLDER) {
        char p[NXRELAY_PATH_MAX];
        const char *icon = "folder-generic";
        if (nxrelay_join(dir, e->name, p, (int)sizeof(p)) == NXRELAY_OK) icon = nxicon_pack_folder_name(p);
        if (nxicon_pack_draw(c, icon, x, y, s, 255) == NXC_OK) return;
        nxsym_draw(c, NXSYM_FOLDER, x + s * 0.5f, y + s * 0.5f, s * 0.9f, NXC_RGB(0x3D, 0x9B, 0xF5), 0.0f);
        return;
    }
    nxsym_draw(c, NXSYM_DOCUMENT, x + s * 0.5f, y + s * 0.5f, s * 0.85f, nxui_theme()->text_secondary, 0.0f);
}

static void pv_button(nxc_canvas_t *c, nxc_rect_t r, nxsym_t sym, int enabled, int hover) {
    const nxui_theme_t *th = nxui_theme();
    if (enabled && hover) nxc_fill_rrect(c, r, 7.0f, th->control_fill_hover);
    nxsym_draw(c, sym, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 14.0f,
               enabled ? th->text_primary : th->text_tertiary, 0.0f);
}

void path_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    if (!c || !win) return;
    pv_layout_t L = pv_layout(win);
    const nxrelay_listing_t *l = pv_listing();
    P.gen_seen = l->generation;

    /* Rail (runs up through the toolbar band) */
    nxframe_rail(c, nxc_rect(L.sidebar.x, L.toolbar.y, L.sidebar.w, L.sidebar.h + PV_TOOLBAR_H));
    nxframe_rail_group(c, L.sidebar.x + 22.0f, L.sidebar.y + 2.0f, L.sidebar.w - 30.0f, "Places");
    nxframe_rail_group(c, L.sidebar.x + 22.0f, pv_side_y(&L, PV_NPLACES) - NXFRAME_RAIL_GROUP_H,
                       L.sidebar.w - 30.0f, "Locations");
    for (int s = 0; s < PV_NPLACES + PV_NLOCS; s++) {
        nxrelay_loc_t loc = pv_side_loc(s);
        char lp[NXRELAY_PATH_MAX];
        if (nxrelay_location(loc, lp, (int)sizeof(lp)) != NXRELAY_OK) continue;
        float ry = pv_side_y(&L, s);
        nxc_rect_t row = nxc_rect(L.sidebar.x + NXFRAME_RAIL_PAD, ry, L.sidebar.w - 2.0f * NXFRAME_RAIL_PAD,
                                  PV_SIDE_ROW_H - 2.0f);
        int on = pv_streq(lp, P.path);
        nxframe_rail_item(c, row, NXFRAME_SYM_SLOT, nxrelay_location_name(loc), NULL,
                          on ? NXFRAME_SELECTED : (P.hover_side == s ? NXFRAME_HOVER : 0u));
        const char *icon = loc == NXRELAY_LOC_ROOT ? "device-computer" :
                           loc == NXRELAY_LOC_VOLUMES ? "device-disk" :
                           loc == NXRELAY_LOC_HOME ? "folder-user" :
                           loc == NXRELAY_LOC_APPLICATIONS ? "app-launcher" : nxicon_pack_folder_name(lp);
        float gx = row.x + NXFRAME_GLYPH_CX, gy = row.y + row.h * 0.5f;
        if (nxicon_pack_draw(c, icon, gx - 8.0f, gy - 8.0f, 16.0f, 255) != NXC_OK)
            nxsym_draw(c, NXSYM_FOLDER, gx, gy, 15.0f, on ? th->accent : th->text_secondary, 0.0f);
    }

    /* Toolbar */
    nxc_fill_rect(c, nxc_rect(L.main.x, L.toolbar.y, L.main.w, PV_TOOLBAR_H), th->window_bg);
    nxc_fill_rect(c, nxc_rect(L.main.x, L.toolbar.y + PV_TOOLBAR_H - 1.0f, L.main.w, 1.0f), th->separator);
    pv_button(c, L.back, NXSYM_CHEVRON_LEFT, P.hist_pos > 0, P.hover_btn == 1);
    pv_button(c, L.fwd, NXSYM_CHEVRON_RIGHT, P.hist_pos + 1 < P.hist_len, P.hover_btn == 2);
    pv_button(c, L.up, NXSYM_CHEVRON_UP, !pv_streq(P.path, "/"), P.hover_btn == 3);
    /* Location: folder name in bold, full path after it */
    const char *base = nxrelay_basename(P.path);
    const char *title = pv_streq(base, "/") ? "Computer" : base;
    float tw = nxui_text_width(NXUI_TEXT_HEADLINE, title);
    if (tw > L.crumb.w * 0.6f) tw = L.crumb.w * 0.6f;
    nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(L.crumb.x, L.crumb.y, tw + 2.0f, L.crumb.h), title,
                      th->text_primary, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L.crumb.x + tw + 12.0f, L.crumb.y, L.crumb.w - tw - 12.0f, L.crumb.h),
                      P.path, th->text_tertiary, NXC_ALIGN_LEFT);

    /* Column header */
    float name_x = L.main.x + 16.0f, kind_x = L.main.x + L.main.w * 0.58f, size_x = L.main.x + L.main.w - 16.0f;
    nxframe_caps(c, name_x + 28.0f, L.header.y + 17.0f, "Name", th->text_tertiary);
    nxframe_caps(c, kind_x, L.header.y + 17.0f, "Kind", th->text_tertiary);
    nxframe_caps(c, size_x - nxframe_caps(NULL, 0.0f, 0.0f, "Size", 0), L.header.y + 17.0f, "Size", th->text_tertiary);
    nxc_fill_rect(c, nxc_rect(L.main.x, L.header.y + PV_HEADER_H - 1.0f, L.main.w, 1.0f), th->separator);

    /* Rows / empty and error states */
    if (l->status != NXRELAY_OK || l->count == 0) {
        const char *head = l->status == NXRELAY_OK ? "This folder is empty" : "Can't open this folder";
        const char *body = l->status == NXRELAY_OK ? "Items you save here will appear in this list."
                                                   : nxrelay_strerror(l->status);
        float cy = L.list.y + L.list.h * 0.42f;
        nxsym_draw(c, l->status == NXRELAY_OK ? NXSYM_FOLDER : NXSYM_WARNING, L.list.x + L.list.w * 0.5f,
                   cy - 34.0f, 34.0f, th->text_tertiary, 0.0f);
        nxui_text_in_rect(c, NXUI_TEXT_HEADLINE, nxc_rect(L.list.x, cy, L.list.w, 20.0f), head,
                          th->text_secondary, NXC_ALIGN_CENTER);
        nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(L.list.x + 20.0f, cy + 22.0f, L.list.w - 40.0f, 18.0f),
                          body, th->text_tertiary, NXC_ALIGN_CENTER);
    } else {
        int max_scroll = (int)l->count - L.rows_visible;
        if (max_scroll < 0) max_scroll = 0;
        if (P.scroll > max_scroll) P.scroll = max_scroll;
        if (P.scroll < 0) P.scroll = 0;
        nxc_save(c);
        nxc_clip_rect(c, nxc_irect((int32_t)L.list.x, (int32_t)L.list.y, (int32_t)L.list.w, (int32_t)L.list.h));
        for (int i = 0; i <= L.rows_visible; i++) {
            int idx = P.scroll + i;
            if (idx >= (int)l->count) break;
            const nxrelay_entry_t *e = &l->entries[idx];
            float ry = L.list.y + (float)i * PV_ROW_H;
            nxc_rect_t row = nxc_rect(L.main.x + 6.0f, ry + 1.0f, L.main.w - 12.0f, PV_ROW_H - 2.0f);
            int sel = idx == P.selected;
            /* Neolyx selection: accent wash + accent bar, ink stays readable */
            if (sel) {
                nxc_fill_rrect(c, row, 5.0f, NXC_WITH_ALPHA(th->accent, 44));
                nxc_fill_rrect(c, nxc_rect(row.x, row.y + 5.0f, 3.0f, row.h - 10.0f), 1.5f, th->accent);
            } else if (idx == P.hover_row) {
                nxframe_row_wash(c, row, NXFRAME_HOVER);
            }
            if (!sel) nxframe_rule(c, row.x + 28.0f, ry + PV_ROW_H - 1.0f, row.w - 28.0f);
            nxc_color_t ink = th->text_primary;
            nxc_color_t ink2 = th->text_secondary;
            pv_icon(c, P.path, e, name_x, ry + 4.0f, 20.0f);
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(name_x + 28.0f, ry, kind_x - name_x - 36.0f, PV_ROW_H),
                              e->name, ink, NXC_ALIGN_LEFT);
            nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(kind_x, ry, size_x - kind_x - 96.0f, PV_ROW_H),
                              pv_kind_text(e), ink2, NXC_ALIGN_LEFT);
            char sz[24];
            pv_size_text(e, sz, (int)sizeof(sz));
            nxui_text_in_rect(c, NXUI_TEXT_FOOTNOTE, nxc_rect(size_x - 90.0f, ry, 90.0f, PV_ROW_H), sz, ink2,
                              NXC_ALIGN_RIGHT);
        }
        nxc_restore(c);
        /* Scroll indicator */
        if (max_scroll > 0) {
            float track = L.list.h - 8.0f;
            float th_h = track * (float)L.rows_visible / (float)l->count;
            if (th_h < 24.0f) th_h = 24.0f;
            float ty = L.list.y + 4.0f + (track - th_h) * (float)P.scroll / (float)max_scroll;
            nxc_fill_rrect(c, nxc_rect(L.main.x + L.main.w - 8.0f, ty, 4.0f, th_h), 2.0f,
                           NXC_WITH_ALPHA(th->text_tertiary, 120));
        }
    }

    /* Status strip */
    char st[96];
    int o = 0;
    uint32_t n = l->status == NXRELAY_OK ? l->count : 0;
    char num[12];
    int k = 0;
    uint32_t t = n;
    do { num[k++] = (char)('0' + t % 10u); t /= 10u; } while (t && k < 11);
    while (k) st[o++] = num[--k];
    const char *tail = n == 1 ? " item" : " items";
    for (const char *s = tail; *s; s++) st[o++] = *s;
    if (l->truncated) for (const char *s = " (first 256 shown)"; *s; s++) st[o++] = *s;
    if (l->synthesized) for (const char *s = " - standard folders"; *s; s++) st[o++] = *s;
    st[o] = '\0';
    nxframe_strip(c, L.status, l->status == NXRELAY_OK ? st : nxrelay_strerror(l->status));
    desktop_set_window_place(win->id, title);
}

/* ============ Input ============ */

static void pv_ensure_visible(const pv_layout_t *L) {
    if (P.selected < 0) return;
    if (P.selected < P.scroll) P.scroll = P.selected;
    if (P.selected >= P.scroll + L->rows_visible) P.scroll = P.selected - L->rows_visible + 1;
}

void path_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)ctx;
    if (!pressed || !win) return;
    pv_layout_t L = pv_layout(win);
    const nxrelay_listing_t *l = pv_listing();
    int n = l->status == NXRELAY_OK ? (int)l->count : 0;
    switch (sc) {
        case 0x48:  /* Up */
            if (n) { P.selected = P.selected <= 0 ? 0 : P.selected - 1; pv_ensure_visible(&L); }
            break;
        case 0x50:  /* Down */
            if (n) { P.selected = P.selected < 0 ? 0 : (P.selected + 1 < n ? P.selected + 1 : n - 1); pv_ensure_visible(&L); }
            break;
        case 0x49:  /* Page Up */
            P.scroll -= L.rows_visible;
            break;
        case 0x51:  /* Page Down */
            P.scroll += L.rows_visible;
            break;
        case 0x1C:  /* Return */
            pv_open_selected();
            break;
        case 0x0E:  /* Backspace */
            pv_up();
            break;
        case 0x3F:  /* F5 */
            nxrelay_invalidate(P.path);
            (void)nxrelay_list(P.path, pv_flags() | NXRELAY_REFRESH);
            break;
        default:
            break;
    }
}

void path_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)ctx;
    if (!win) return;
    pv_layout_t L = pv_layout(win);
    float x = (float)win->x + (float)lx, y = (float)win->y + (float)APP_VIEW_TITLE_H + (float)ly;
    const nxrelay_listing_t *l = pv_listing();
    int n = l->status == NXRELAY_OK ? (int)l->count : 0;

    /* Hover */
    P.hover_btn = pv_in(L.back, x, y) ? 1 : pv_in(L.fwd, x, y) ? 2 : pv_in(L.up, x, y) ? 3 : 0;
    P.hover_side = pv_side_at(&L, x, y);
    P.hover_row = -1;
    if (pv_in(L.list, x, y)) {
        int r = P.scroll + (int)((y - L.list.y) / PV_ROW_H);
        if (r >= 0 && r < n) P.hover_row = r;
    }

    int click = (b & 0x01) && !(pb & 0x01);
    if (!click) return;

    switch (P.hover_btn) {
        case 1: pv_back(); return;
        case 2: pv_forward(); return;
        case 3: pv_up(); return;
        default: break;
    }
    if (P.hover_side >= 0) {
        char lp[NXRELAY_PATH_MAX];
        if (nxrelay_location(pv_side_loc(P.hover_side), lp, (int)sizeof(lp)) == NXRELAY_OK) pv_go(lp, 1);
        return;
    }
    if (pv_in(L.list, x, y)) {
        /* Scroll track on the right edge pages the list */
        if (x >= L.main.x + L.main.w - 12.0f && n > L.rows_visible) {
            P.scroll += y < L.list.y + L.list.h * 0.5f ? -L.rows_visible : L.rows_visible;
            return;
        }
        uint64_t now = nx_gettime();
        int row = P.hover_row;
        if (row >= 0 && row == P.last_click_row && now - P.last_click_ms < PV_DBL_MS) {
            P.selected = row;
            P.last_click_row = -1;
            pv_open_selected();
            return;
        }
        P.selected = row;
        P.last_click_row = row;
        P.last_click_ms = now;
    }
}

/* ============ Lifecycle ============ */

static int pv_window_open(void);

int path_view_open(uint32_t win_id) {
    if (win_id == 0) return -1;
    /* One Path window: a second launch focuses the first instead. */
    if (pv_window_open() && P.win_id != win_id) {
        pv_log("already open; focusing the existing window", 0);
        if (P.pending[0]) { pv_go(P.pending, 1); P.pending[0] = '\0'; }
        desktop_activate_window(P.win_id);
        return -1;
    }
    if (nxrelay_selftest() != 0) {
        pv_log("relay self-test failed; refusing to open", 0);
        return -1;
    }
    P.win_id = win_id;
    P.hist_len = P.hist_pos = 0;
    P.hover_row = P.hover_side = -1;
    P.hover_btn = 0;
    P.show_hidden = 0;
    char start[NXRELAY_PATH_MAX];
    if (P.pending[0]) {
        pv_strcpy(start, P.pending, (int)sizeof(start));
        P.pending[0] = '\0';
    } else {
        pv_start_path(start, (int)sizeof(start));
    }
    if (pv_go(start, 1) != 0) pv_go("/", 1);
    pv_log("opened at", P.path);
    return 0;
}

static int pv_window_open(void) {
    if (P.win_id == 0) return 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == P.win_id) return 1;
    }
    return 0;
}

int path_view_open_at(const char *path) {
    char canon[NXRELAY_PATH_MAX];
    if (!path || nxrelay_normalize(path, canon, (int)sizeof(canon)) != NXRELAY_OK) {
        pv_log("open_at: rejected", path ? path : "(null)");
        return -1;
    }
    if (pv_window_open()) {
        pv_go(canon, 1);
        return desktop_activate_window(P.win_id);
    }
    pv_strcpy(P.pending, canon, (int)sizeof(P.pending));
    if (desktop_launch_app("Path") != 0) {
        P.pending[0] = '\0';
        return -1;
    }
    return 0;
}
