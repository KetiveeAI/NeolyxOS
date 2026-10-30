/*
 * NeolyxOS - App switcher (NX+Tab), see switcher.h
 *
 * One entry per app (windows grouped by title), most recently used first
 * (the shell's z-order is the MRU order: focusing raises). The HUD is a
 * cached glass panel once its fade-in settles.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/switcher.h"
#include "../include/window_manager.h"
#include "../include/desktop_shell.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxicons.h"
#include "../include/nxsyscall.h"

#define SW_CELL   96.0f
#define SW_ICON   64.0f
#define SW_PAD    14.0f
#define SW_LABEL  30.0f

typedef struct {
    uint32_t     win;        /* topmost window of the app */
    const char  *title;      /* points into the window (valid while it lives) */
    nxapp_icon_t icon;
    int          windows;    /* number of windows of this app */
    int          minimized;  /* every window of the app is minimized */
} sw_entry_t;

static sw_entry_t g_e[SWITCHER_MAX_APPS];
static int g_n = 0;
static int g_sel = 0;
static int g_open = 0;            /* takes input */
static int g_hover = -1;
static nxui_spring_t g_fade;      /* 0 hidden .. 1 shown */

static int sw_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static const desktop_window_t *sw_window(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const desktop_window_t *w = desktop_window_slot(i);
        if (w && w->id == id) return w;
    }
    return NULL;
}

/* Build the app list from the z-order (topmost first). */
static void sw_collect(void) {
    uint32_t ids[MAX_WINDOWS];
    int n = wm_shell_window_order(ids, MAX_WINDOWS);
    g_n = 0;
    for (int i = 0; i < n; i++) {
        const desktop_window_t *w = sw_window(ids[i]);
        if (!w) continue;
        int found = -1;
        for (int k = 0; k < g_n; k++) {
            if (sw_streq_ci(g_e[k].title, w->title)) { found = k; break; }
        }
        if (found >= 0) {
            g_e[found].windows++;
            if (!w->minimized) {
                if (g_e[found].minimized) g_e[found].win = w->id;   /* prefer a shown window */
                g_e[found].minimized = 0;
            }
            continue;
        }
        if (g_n >= SWITCHER_MAX_APPS) continue;
        g_e[g_n].win = w->id;
        g_e[g_n].title = w->title;
        g_e[g_n].icon = nxapp_icon_from_name(w->title);
        g_e[g_n].windows = 1;
        g_e[g_n].minimized = w->minimized ? 1 : 0;
        g_n++;
    }
}

static nxc_rect_t sw_panel_rect(void) {
    float w = (float)g_n * SW_CELL + 2.0f * SW_PAD;
    float h = SW_ICON + 2.0f * SW_PAD + 12.0f + SW_LABEL;
    float x = ((float)nxui_width() - w) * 0.5f, y = ((float)nxui_height() - h) * 0.5f - 20.0f;
    return nxc_rect((float)(int)x, (float)(int)y, (float)(int)w, (float)(int)h);
}

static nxc_rect_t sw_cell_rect(nxc_rect_t panel, int i) {
    return nxc_rect(panel.x + SW_PAD + SW_CELL * (float)i, panel.y + SW_PAD - 2.0f, SW_CELL, SW_ICON + 20.0f);
}

void switcher_open(int reverse) {
    if (g_open) {
        switcher_step(reverse);
        return;
    }
    sw_collect();
    if (g_n == 0) {
        nx_debug_print("[SWITCHER] no running apps\n");
        return;
    }
    g_sel = g_n > 1 ? (reverse ? g_n - 1 : 1) : 0;
    g_open = 1;
    g_hover = -1;
    nx_debug_print("[SWITCHER] open\n");
}

void switcher_step(int reverse) {
    if (!g_open || g_n <= 0) return;
    g_sel = (g_sel + (reverse ? g_n - 1 : 1)) % g_n;
}

static void sw_close(void) {
    g_open = 0;
    g_hover = -1;
}

void switcher_commit(void) {
    if (!g_open) return;
    uint32_t id = (g_sel >= 0 && g_sel < g_n) ? g_e[g_sel].win : 0;
    sw_close();
    if (id && desktop_activate_window(id) != 0) {
        nx_debug_print("[SWITCHER] selected window vanished\n");
    }
}

void switcher_cancel(void) {
    if (!g_open) return;
    sw_close();
    nx_debug_print("[SWITCHER] cancelled\n");
}

int switcher_active(void) { return g_open || g_fade.value > 0.01f; }
int switcher_open_now(void) { return g_open; }

void switcher_tick(float dt) {
    if (!g_open && g_fade.value <= 0.0f) return;
    nxui_spring_step(&g_fade, g_open ? 1.0f : 0.0f, 420.0f, 41.0f, dt);
    if (!g_open && g_fade.value < 0.01f) {
        g_fade.value = 0.0f;
        g_fade.velocity = 0.0f;
        g_n = 0;   /* drop title pointers once hidden */
    }
}

void switcher_on_window_destroyed(uint32_t window_id) {
    if (g_n == 0) return;
    int o = 0;
    for (int i = 0; i < g_n; i++) {
        if (g_e[i].win == window_id) {
            if (g_sel > o) g_sel--;
            continue;
        }
        g_e[o++] = g_e[i];
    }
    g_n = o;
    if (g_sel >= g_n) g_sel = g_n - 1;
    if (g_n == 0) {
        g_sel = 0;
        sw_close();
    }
}

int switcher_handle_mouse(int32_t x, int32_t y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_open) return 0;
    nxc_rect_t panel = sw_panel_rect();
    g_hover = -1;
    for (int i = 0; i < g_n; i++) {
        if (nxglass_hit(sw_cell_rect(panel, i), x, y)) { g_hover = i; break; }
    }
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        if (g_hover >= 0) {
            g_sel = g_hover;
            switcher_commit();
        } else if (!nxglass_hit(panel, x, y)) {
            switcher_cancel();
        }
    }
    return 1;
}

void switcher_render(nxc_canvas_t *c) {
    if (!c || g_n <= 0 || g_fade.value <= 0.01f) return;
    const nxui_theme_t *th = nxui_theme();
    float t = g_fade.value;
    if (t > 1.0f) t = 1.0f;
    nxc_rect_t panel = sw_panel_rect();
    int settled = g_open && t > 0.985f;

    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(t * 255.0f));
    /* Cached glass once settled; live while fading (the rect is stable). */
    nxglass_panel(c, panel, 22.0f, NXGLASS_ULTRATHIN, 3, settled ? 0 : 1);
    nxc_stroke_rrect(c, panel, 22.0f, 1.0f, th->separator);

    for (int i = 0; i < g_n; i++) {
        nxc_rect_t cell = sw_cell_rect(panel, i);
        if (i == g_sel) {
            nxc_fill_rrect(c, nxc_rect(cell.x + 6.0f, cell.y + 2.0f, cell.w - 12.0f, cell.h - 4.0f), 16.0f,
                           NXC_WITH_ALPHA(th->text_primary, 46));
            nxc_stroke_rrect(c, nxc_rect(cell.x + 6.0f, cell.y + 2.0f, cell.w - 12.0f, cell.h - 4.0f), 16.0f,
                             1.0f, NXC_WITH_ALPHA(th->text_primary, 60));
        } else if (i == g_hover) {
            nxc_fill_rrect(c, nxc_rect(cell.x + 6.0f, cell.y + 2.0f, cell.w - 12.0f, cell.h - 4.0f), 16.0f,
                           th->control_fill_hover);
        }
        uint8_t ia = g_e[i].minimized ? 150 : 255;
        nxapp_icon_draw(c, g_e[i].icon, cell.x + (cell.w - SW_ICON) * 0.5f, cell.y + 10.0f, SW_ICON, ia);
        /* Running indicator dot; a second dot when the app has several windows */
        float dy = cell.y + 10.0f + SW_ICON + 5.0f, cx = cell.x + cell.w * 0.5f;
        if (g_e[i].windows > 1) {
            nxc_fill_circle(c, cx - 4.0f, dy, 1.8f, th->text_secondary);
            nxc_fill_circle(c, cx + 4.0f, dy, 1.8f, th->text_secondary);
        } else {
            nxc_fill_circle(c, cx, dy, 1.8f, th->text_secondary);
        }
    }
    if (g_sel >= 0 && g_sel < g_n && g_e[g_sel].title) {
        nxc_rect_t lr = nxc_rect(panel.x + 10.0f, panel.y + panel.h - SW_LABEL - 8.0f, panel.w - 20.0f, SW_LABEL);
        nxui_text_in_rect(c, NXUI_TEXT_TITLE3, lr, g_e[g_sel].title, th->text_primary, NXC_ALIGN_CENTER);
    }
    nxc_restore(c);
}
