/*
 * NeolyxOS - Menu bar menus
 *
 * See menubar.h. Geometry: titles are laid out by the menu bar renderer
 * each frame (text width + padding); the dropdown hangs under the open
 * title with 1 px overlap so it reads as attached. Hovering another
 * title while a menu is open switches menus, like every desktop does.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/menubar.h"
#include "../include/nxui.h"

/* ============ Menu definitions ============ */

enum {
    MB_FILE_NEW_WINDOW = 1, MB_FILE_NEW_FOLDER, MB_FILE_OPEN, MB_FILE_CLOSE, MB_FILE_QUIT,
    MB_EDIT_UNDO, MB_EDIT_REDO, MB_EDIT_CUT, MB_EDIT_COPY, MB_EDIT_PASTE, MB_EDIT_SELECT_ALL,
    MB_VIEW_ICONS, MB_VIEW_LIST, MB_VIEW_SIDEBAR, MB_VIEW_APPEARANCE, MB_VIEW_GRID,
    MB_WIN_MINIMIZE, MB_WIN_ZOOM, MB_WIN_TILE_LEFT, MB_WIN_TILE_RIGHT, MB_WIN_BRING_ALL,
    MB_HELP_SHORTCUTS, MB_HELP_ABOUT,
};

static const menubar_item_t g_file_items[] = {
    { "New Window",   "NX N",       1, MB_FILE_NEW_WINDOW },
    { "New Folder",   "Shift NX N", 1, MB_FILE_NEW_FOLDER },
    { "Open\xE2\x80\xA6", "NX O",   1, MB_FILE_OPEN },
    { NULL, NULL, 0, 0 },
    { "Close Window", "NX W",       1, MB_FILE_CLOSE },
    { NULL, NULL, 0, 0 },
    { "Quit",         "NX Q",       1, MB_FILE_QUIT },
};
static const menubar_item_t g_edit_items[] = {
    { "Undo",       "Ctrl Z",       1, MB_EDIT_UNDO },
    { "Redo",       "Shift Ctrl Z", 1, MB_EDIT_REDO },
    { NULL, NULL, 0, 0 },
    { "Cut",        "Ctrl X",       1, MB_EDIT_CUT },
    { "Copy",       "Ctrl C",       1, MB_EDIT_COPY },
    { "Paste",      "Ctrl V",       0, MB_EDIT_PASTE },
    { "Select All", "Ctrl A",       1, MB_EDIT_SELECT_ALL },
};
static const menubar_item_t g_view_items[] = {
    { "as Icons",          "NX 1", 1, MB_VIEW_ICONS },
    { "as List",           "NX 2", 1, MB_VIEW_LIST },
    { NULL, NULL, 0, 0 },
    { "Show Sidebar",      NULL,   1, MB_VIEW_SIDEBAR },
    { "Toggle Appearance", NULL,   1, MB_VIEW_APPEARANCE },
    { "Snap Grid",         NULL,   1, MB_VIEW_GRID },
};
static const menubar_item_t g_window_items[] = {
    { "Minimize",           "NX M",      1, MB_WIN_MINIMIZE },
    { "Zoom",               NULL,        1, MB_WIN_ZOOM },
    { NULL, NULL, 0, 0 },
    { "Tile Left",          "NX \xE2\x86\x90", 1, MB_WIN_TILE_LEFT },
    { "Tile Right",         "NX \xE2\x86\x92", 1, MB_WIN_TILE_RIGHT },
    { NULL, NULL, 0, 0 },
    { "Bring All to Front", NULL,        1, MB_WIN_BRING_ALL },
};
static const menubar_item_t g_help_items[] = {
    { "Keyboard Shortcuts", "Hold NX", 1, MB_HELP_SHORTCUTS },
    { NULL, NULL, 0, 0 },
    { "About NeolyxOS",     NULL,      1, MB_HELP_ABOUT },
};

#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
static const menubar_menu_t g_menus[] = {
    { "File",   g_file_items,   N(g_file_items) },
    { "Edit",   g_edit_items,   N(g_edit_items) },
    { "View",   g_view_items,   N(g_view_items) },
    { "Window", g_window_items, N(g_window_items) },
    { "Help",   g_help_items,   N(g_help_items) },
};
#define MENU_COUNT N(g_menus)

/* ============ State ============ */

#define ITEM_H      26.0f
#define SEP_H       9.0f
#define MENU_W      228.0f
#define MENU_PAD    5.0f

typedef struct {
    int   open;            /* -1 or menu index */
    int   hover_title;
    int   hover_item;      /* index into items, -1 */
    float title_x[MENUBAR_MENU_MAX], title_w[MENUBAR_MENU_MAX];
    float bar_h;
    float anim;            /* 0..1 open progress */
    menubar_action_fn handler;
    int   initialized;
} menubar_state_t;

static menubar_state_t g_mb;

void menubar_init(void) {
    g_mb.open = -1;
    g_mb.hover_title = -1;
    g_mb.hover_item = -1;
    g_mb.bar_h = 28.0f;
    g_mb.anim = 0.0f;
    g_mb.initialized = 1;
}

void menubar_set_handler(menubar_action_fn fn) { g_mb.handler = fn; }
int menubar_is_open(void) { return g_mb.open >= 0; }
void menubar_close(void) { g_mb.open = -1; g_mb.hover_item = -1; }

static float menu_height(const menubar_menu_t *m) {
    float h = 2.0f * MENU_PAD;
    for (int i = 0; i < m->count; i++) h += m->items[i].label ? ITEM_H : SEP_H;
    return h;
}

/* ============ Rendering ============ */

float menubar_render_titles(nxc_canvas_t *c, float x, float h) {
    if (!c || !g_mb.initialized) return x;
    const nxui_theme_t *th = nxui_theme();
    g_mb.bar_h = h;
    for (int i = 0; i < MENU_COUNT && i < MENUBAR_MENU_MAX; i++) {
        float tw = nxui_text_width(NXUI_TEXT_MENU, g_menus[i].title);
        float pad = 10.0f;
        g_mb.title_x[i] = x - pad;
        g_mb.title_w[i] = tw + 2.0f * pad;
        if (g_mb.open == i) {
            nxc_fill_rrect(c, nxc_rect(g_mb.title_x[i], 3.0f, g_mb.title_w[i], h - 6.0f), 6.0f, th->control_fill_pressed);
        }
        nxui_text_in_rect(c, NXUI_TEXT_MENU, nxc_rect(x, 0, tw + 2.0f, h), g_menus[i].title, th->text_primary, NXC_ALIGN_LEFT);
        x += tw + 20.0f;
    }
    return x;
}

void menubar_render(nxc_canvas_t *c) {
    if (!c || !g_mb.initialized) return;
    g_mb.anim = nxui_approach(g_mb.anim, g_mb.open >= 0 ? 1.0f : 0.0f, 0.035f, nxui_frame_dt());
    if (g_mb.open < 0 || g_mb.anim < 0.02f) return;
    const nxui_theme_t *th = nxui_theme();
    const menubar_menu_t *m = &g_menus[g_mb.open];
    float mh = menu_height(m);
    float x = g_mb.title_x[g_mb.open];
    float y = g_mb.bar_h - 1.0f;
    float ease = nxui_ease_out_cubic(g_mb.anim);
    nxc_rect_t box = nxc_rect(x, y - (1.0f - ease) * 6.0f, MENU_W, mh);

    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(ease * 255.0f));
    nxui_panel(c, box, 10.0f, &th->mat_popover, 2);
    nxc_stroke_rrect(c, box, 10.0f, 1.0f, th->window_border);

    float iy = box.y + MENU_PAD;
    for (int i = 0; i < m->count; i++) {
        const menubar_item_t *it = &m->items[i];
        if (!it->label) {
            nxc_fill_rect(c, nxc_rect(box.x + 12.0f, iy + SEP_H * 0.5f, MENU_W - 24.0f, 1.0f), th->separator);
            iy += SEP_H;
            continue;
        }
        nxc_rect_t row = nxc_rect(box.x + MENU_PAD, iy, MENU_W - 2.0f * MENU_PAD, ITEM_H);
        int hot = (i == g_mb.hover_item) && it->enabled;
        if (hot) nxc_fill_rrect(c, row, 6.0f, th->accent);
        nxc_color_t fg = hot ? th->text_on_accent : (it->enabled ? th->text_primary : th->text_tertiary);
        nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(row.x + 10.0f, row.y, row.w - 20.0f, row.h), it->label, fg, NXC_ALIGN_LEFT);
        if (it->shortcut) {
            nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(row.x + 10.0f, row.y, row.w - 20.0f, row.h), it->shortcut,
                              hot ? NXC_WITH_ALPHA(th->text_on_accent, 200) : th->text_tertiary, NXC_ALIGN_RIGHT);
        }
        iy += ITEM_H;
    }
    nxc_restore(c);
}

/* ============ Input ============ */

static int title_at(int x, int y) {
    if (y < 0 || (float)y >= g_mb.bar_h) return -1;
    for (int i = 0; i < MENU_COUNT; i++) {
        if ((float)x >= g_mb.title_x[i] && (float)x < g_mb.title_x[i] + g_mb.title_w[i]) return i;
    }
    return -1;
}

/* Item index under (x, y) for the open menu, -1 outside; separators -1. */
static int item_at(int x, int y, int *inside_box) {
    *inside_box = 0;
    if (g_mb.open < 0) return -1;
    const menubar_menu_t *m = &g_menus[g_mb.open];
    float bx = g_mb.title_x[g_mb.open], by = g_mb.bar_h - 1.0f, mh = menu_height(m);
    if ((float)x < bx || (float)x >= bx + MENU_W || (float)y < by || (float)y >= by + mh) return -1;
    *inside_box = 1;
    float iy = by + MENU_PAD;
    for (int i = 0; i < m->count; i++) {
        float h = m->items[i].label ? ITEM_H : SEP_H;
        if ((float)y >= iy && (float)y < iy + h) return m->items[i].label ? i : -1;
        iy += h;
    }
    return -1;
}

static void activate(int item) {
    const menubar_menu_t *m = &g_menus[g_mb.open];
    if (item >= 0 && item < m->count && m->items[item].label && m->items[item].enabled && g_mb.handler) {
        g_mb.handler(g_mb.open, m->items[item].action);
    }
    menubar_close();
}

int menubar_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_mb.initialized) return 0;
    int press = (buttons & 0x01) && !(prev_buttons & 0x01);
    int release = !(buttons & 0x01) && (prev_buttons & 0x01);
    int t = title_at(x, y);
    g_mb.hover_title = t;

    if (g_mb.open < 0) {
        if (press && t >= 0) {
            g_mb.open = t;
            g_mb.hover_item = -1;
            return 1;
        }
        return 0;
    }
    /* A menu is open: titles switch, rows highlight, clicks activate or close. */
    if (t >= 0 && t != g_mb.open) {
        g_mb.open = t;
        g_mb.hover_item = -1;
    }
    int inside = 0;
    int it = item_at(x, y, &inside);
    g_mb.hover_item = it;
    if (press) {
        if (t == g_mb.open) { menubar_close(); return 1; }     /* toggle off */
        if (inside) { if (it >= 0) activate(it); return 1; }
        if (t < 0) { menubar_close(); return 1; }              /* click elsewhere */
    } else if (release && inside && it >= 0 && y > (int)g_mb.bar_h + 4) {
        activate(it);                                          /* press-drag-release */
        return 1;
    }
    return 1;
}

int menubar_handle_key(uint8_t scancode, uint8_t pressed) {
    if (!g_mb.initialized || g_mb.open < 0 || !pressed) return 0;
    const menubar_menu_t *m = &g_menus[g_mb.open];
    switch (scancode) {
        case 0x01: menubar_close(); return 1;                           /* Esc */
        case 0x4B: g_mb.open = (g_mb.open + MENU_COUNT - 1) % MENU_COUNT; g_mb.hover_item = -1; return 1;
        case 0x4D: g_mb.open = (g_mb.open + 1) % MENU_COUNT; g_mb.hover_item = -1; return 1;
        case 0x50: {                                                    /* Down */
            int i = g_mb.hover_item;
            for (int k = 0; k < m->count; k++) {
                i = (i + 1) % m->count;
                if (m->items[i].label && m->items[i].enabled) break;
            }
            g_mb.hover_item = i;
            return 1;
        }
        case 0x48: {                                                    /* Up */
            int i = g_mb.hover_item < 0 ? 0 : g_mb.hover_item;
            for (int k = 0; k < m->count; k++) {
                i = (i + m->count - 1) % m->count;
                if (m->items[i].label && m->items[i].enabled) break;
            }
            g_mb.hover_item = i;
            return 1;
        }
        case 0x1C: case 0x39:                                           /* Enter, Space */
            activate(g_mb.hover_item);
            return 1;
        default:
            return 1;                                                    /* swallow while open */
    }
}
