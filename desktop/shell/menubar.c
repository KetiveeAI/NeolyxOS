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
#include "../include/nxglass.h"
#include "../include/desktop_shell.h"
#include "../include/control_center.h"
#include "../include/notification_center.h"
#include "../include/network_indicator.h"

extern void serial_puts(const char *s);

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
    { "About This System",  NULL,      1, MB_HELP_ABOUT },
};

#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
static const menubar_menu_t g_menus[] = {
    { "File",   g_file_items,   N(g_file_items) },
    { "Edit",   g_edit_items,   N(g_edit_items) },
    { "View",   g_view_items,   N(g_view_items) },
    { "Window", g_window_items, N(g_window_items) },
    { "Help",   g_help_items,   N(g_help_items) },
};
#define STD_MENU_COUNT N(g_menus)

/* ============ App-specific menu sets ============
 *
 * A shell-hosted app can replace the standard titles while its window has
 * focus (menubar_register_app_menus). The set in use is picked from the
 * focused window title each frame; actions of an app item go to the
 * app's handler, actions of a standard menu reused in the set (Window,
 * Help) still go to the shell handler. */

#define APP_SET_MAX 4

typedef struct {
    char                  app[32];
    const menubar_menu_t *menus;
    int                   count;
    menubar_action_fn     handler;
} app_menu_set_t;

static app_menu_set_t g_app_sets[APP_SET_MAX];
static int g_app_set_count;
static const app_menu_set_t *g_cur_set;     /* NULL = standard menus */

static const menubar_menu_t *cur_menus(void) { return g_cur_set ? g_cur_set->menus : g_menus; }
static int cur_count(void) {
    int n = g_cur_set ? g_cur_set->count : STD_MENU_COUNT;
    return n > MENUBAR_MENU_MAX ? MENUBAR_MENU_MAX : n;
}
#define MENU_COUNT cur_count()

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

static int name_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

int menubar_register_app_menus(const char *app_name, const menubar_menu_t *menus, int count,
                               menubar_action_fn handler) {
    if (!app_name || !app_name[0] || !menus || count <= 0 || count > MENUBAR_MENU_MAX || !handler) {
        serial_puts("[MENUBAR] rejected app menu set: invalid arguments\n");
        return -1;
    }
    int len = 0;
    while (app_name[len]) len++;
    if (len >= (int)sizeof(g_app_sets[0].app)) {
        serial_puts("[MENUBAR] rejected app menu set: name too long\n");
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (!menus[i].title || !menus[i].items || menus[i].count <= 0 || menus[i].count > MENUBAR_ITEM_MAX) {
            serial_puts("[MENUBAR] rejected app menu set: bad menu\n");
            return -1;
        }
    }
    app_menu_set_t *slot = 0;
    for (int i = 0; i < g_app_set_count; i++)
        if (name_eq_ci(g_app_sets[i].app, app_name)) slot = &g_app_sets[i];
    if (!slot) {
        if (g_app_set_count >= APP_SET_MAX) {
            serial_puts("[MENUBAR] rejected app menu set: table full\n");
            return -1;
        }
        slot = &g_app_sets[g_app_set_count++];
    }
    for (int i = 0; i <= len; i++) slot->app[i] = app_name[i];
    slot->menus = menus;
    slot->count = count;
    slot->handler = handler;
    serial_puts("[MENUBAR] app menus registered: ");
    serial_puts(app_name);
    serial_puts("\n");
    return 0;
}

const menubar_menu_t *menubar_standard_menu(const char *title) {
    if (!title) return 0;
    for (int i = 0; i < STD_MENU_COUNT; i++)
        if (name_eq_ci(g_menus[i].title, title)) return &g_menus[i];
    return 0;
}

/* Pick the menu set for the focused app. A change closes an open menu so
 * an index never points into the other set. */
static void select_set(const char *app_name) {
    const app_menu_set_t *want = 0;
    for (int i = 0; app_name && i < g_app_set_count; i++)
        if (name_eq_ci(g_app_sets[i].app, app_name)) { want = &g_app_sets[i]; break; }
    if (want != g_cur_set) {
        g_cur_set = want;
        g_mb.open = -1;
        g_mb.hover_item = -1;
        g_mb.hover_title = -1;
    }
}
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
        float tw = nxui_text_width(NXUI_TEXT_MENU, cur_menus()[i].title);
        float pad = 10.0f;
        g_mb.title_x[i] = x - pad;
        g_mb.title_w[i] = tw + 2.0f * pad;
        if (g_mb.open == i) {
            nxc_fill_rrect(c, nxc_rect(g_mb.title_x[i], 3.0f, g_mb.title_w[i], h - 6.0f), 6.0f, th->control_fill_pressed);
        }
        nxui_text_in_rect(c, NXUI_TEXT_MENU, nxc_rect(x, 0, tw + 2.0f, h), cur_menus()[i].title, th->text_primary, NXC_ALIGN_LEFT);
        x += tw + 20.0f;
    }
    return x;
}

void menubar_render(nxc_canvas_t *c) {
    if (!c || !g_mb.initialized) return;
    g_mb.anim = nxui_approach(g_mb.anim, g_mb.open >= 0 ? 1.0f : 0.0f, 0.035f, nxui_frame_dt());
    if (g_mb.open < 0 || g_mb.anim < 0.02f) return;
    const nxui_theme_t *th = nxui_theme();
    const menubar_menu_t *m = &cur_menus()[g_mb.open];
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
    const menubar_menu_t *m = &cur_menus()[g_mb.open];
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
    const menubar_menu_t *m = &cur_menus()[g_mb.open];
    if (item >= 0 && item < m->count && m->items[item].label && m->items[item].enabled) {
        int action = m->items[item].action;
        if (g_cur_set && action >= MENUBAR_APP_ACTION_BASE) g_cur_set->handler(g_mb.open, action);
        else if (g_mb.handler) g_mb.handler(g_mb.open, action);
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
    const menubar_menu_t *m = &cur_menus()[g_mb.open];
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

/* ============ The whole bar (background, app name, status area) ============ */

/* Power status for the battery item: -1 = no battery / unknown (hidden).
 * There is no battery syscall yet; the shell or a power service reports it. */
#ifdef NX_HOST_PREVIEW
/* PREVIEW ONLY: demo battery so the host preview shows the item. */
static int g_power_pct = 82;
#else
static int g_power_pct = -1;
#endif
static int g_power_charging = 0;

int menubar_set_power_status(int percent, int charging) {
    if (percent < -1 || percent > 100 || (charging != 0 && charging != 1)) {
        serial_puts("[MENUBAR] rejected power status\n");
        return -1;
    }
    if (percent != g_power_pct || charging != g_power_charging) {
        g_power_pct = percent;
        g_power_charging = charging;
        serial_puts("[MENUBAR] power status updated\n");
    }
    return 0;
}

/* Focused app name, cached with its width (re-measured only on change). */
static char  g_app_name[WINDOW_TITLE_MAX];
static float g_app_name_w;
static uint32_t g_app_name_win;

static void str_copy(char *dst, const char *src, int cap) {
    int i = 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void refresh_app_name(void) {
    uint32_t id = desktop_focused_window_id();
    const char *name = "NeolyxOS";
    if (id) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            const desktop_window_t *w = desktop_window_slot(i);
            if (w && w->id == id) {
                if (w->title[0] && !w->minimized) name = w->title;   /* a minimized window has no focus */
                else id = 0;
                break;
            }
        }
    }
    if (id == g_app_name_win && g_app_name[0] && str_eq(name, g_app_name)) return;
    g_app_name_win = id;
    str_copy(g_app_name, name, (int)sizeof(g_app_name));
    g_app_name_w = nxui_text_width(NXUI_TEXT_MENU_BOLD, g_app_name);
    select_set(id ? g_app_name : 0);
    if (g_app_name_w > 220.0f) g_app_name_w = 220.0f;
}

/* Pressed-state pill behind a status item that has its panel open. */
static void status_pill(nxc_canvas_t *c, float cx, float h, float w) {
    nxc_fill_rrect(c, nxc_rect(cx - w * 0.5f, 3.0f, w, h - 6.0f), 6.0f, nxui_theme()->control_fill_pressed);
}

void menubar_render_bar(nxc_canvas_t *c, float w, float h, const char *clock_text) {
    if (!c || !(w > 0.0f) || !(h > 0.0f)) return;
    if (!g_mb.initialized) menubar_init();
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t bar = nxc_rect(0.0f, 0.0f, w, h);
    nxglass_panel(c, bar, 0.0f, NXGLASS_CHROME, 0, 0);
    nxc_fill_rect(c, nxc_rect(0.0f, h - 1.0f, w, 1.0f), th->separator);

    /* Left: focused app (bold), then the menus */
    refresh_app_name();
    float x = 14.0f;
    nxui_text_in_rect(c, NXUI_TEXT_MENU_BOLD, nxc_rect(x, 0.0f, g_app_name_w + 2.0f, h), g_app_name,
                      th->text_primary, NXC_ALIGN_LEFT);
    x += g_app_name_w + 22.0f;
    menubar_render_titles(c, x, h);

    /* Right: Control Center and notifications at their fixed slots */
    float cy = h * 0.5f;
    nxc_color_t ic = th->text_primary;
    if (control_center_is_visible()) status_pill(c, w - MENUBAR_CC_CENTER, h, 28.0f);
    nxsym_draw(c, NXSYM_CONTROL_CENTER, w - MENUBAR_CC_CENTER, cy, 16.0f, ic, 0.0f);
    if (notification_center_is_visible()) status_pill(c, w - MENUBAR_NC_CENTER, h, 28.0f);
    nxsym_draw(c, NXSYM_BELL, w - MENUBAR_NC_CENTER, cy, 16.0f, ic, 0.0f);

    /* Clock, right-aligned before the bell */
    float right = w - MENUBAR_NC_CENTER - 20.0f;
    if (clock_text && clock_text[0]) {
        float cw = nxui_text_width(NXUI_TEXT_MENU, clock_text);
        nxui_text_in_rect(c, NXUI_TEXT_MENU, nxc_rect(right - cw - 2.0f, 0.0f, cw + 2.0f, h), clock_text,
                          ic, NXC_ALIGN_RIGHT);
        right -= cw + 18.0f;
    }

    /* Wi-Fi / Ethernet (nxnet-backed indicator, refreshed about once a second) */
    const control_center_state_t *cc = control_center_get_state();
    {
        static float last_net_update = -10.0f;
        float now = nxui_time();
        if (now - last_net_update >= 1.0f || now < last_net_update) {
            network_indicator_update();
            last_net_update = now;
        }
    }
    network_indicator_draw_icon(c, right - 8.0f, cy, 16.0f, ic);
    right -= 28.0f;

    /* Volume */
    int vol = cc ? cc->volume : -1;
    if (vol >= 0 && vol <= 100) {
        nxsym_draw(c, vol == 0 ? NXSYM_VOLUME_MUTE : NXSYM_VOLUME, right - 8.0f, cy, 16.0f, ic,
                   (float)vol / 100.0f);
        right -= 30.0f;
    }

    /* Battery (only when one is reported) */
    if (g_power_pct >= 0) {
        nxsym_draw(c, g_power_charging ? NXSYM_BATTERY_CHARGING : NXSYM_BATTERY, right - 9.0f, cy, 18.0f,
                   g_power_pct <= 10 && !g_power_charging ? th->destructive : ic, (float)g_power_pct / 100.0f);
    }
}
