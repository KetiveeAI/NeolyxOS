/*
 * NeolyxOS - Taskbar layout of the dock (DOCK_LAYOUT_TASKBAR)
 *
 * A full-width glass bar along the bottom edge:
 *   [pinned app icons] | [one pill per open window: icon + title]
 * Pinned icons show a running underline when the app has windows; the
 * focused window's pill is highlighted with an accent underline; pills of
 * minimized windows are dimmed. Click a pin to activate or launch its app,
 * a pill to focus its window (or hide it when it is already focused and
 * the shell registered a hide handler). Right click opens the dock's
 * context menu for the app.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "dock_internal.h"

#define TB_PIN_SLOT     44.0f
#define TB_PIN_ICON     30.0f
#define TB_PILL_H       34.0f
#define TB_PILL_MAX     210.0f
#define TB_PILL_MIN     46.0f
#define TB_PILL_GAP     6.0f

static nxc_rect_t g_bar;
static nxc_rect_t g_pin_rect[DOCK_MAX_ICONS];
static int        g_pin_item[DOCK_MAX_ICONS];
static int        g_npins;
static nxc_rect_t g_pill_rect[MAX_WINDOWS];
static int        g_npills;
static int        g_press_kind;        /* 0 none, 1 pin, 2 pill */
static int        g_press_idx = -1;
static float      g_tip_alpha;
static int        g_tip_pin = -1;

nxc_rect_t taskbar_rect(void) { return g_bar; }

int taskbar_contains(int x, int y) { return nxglass_hit(g_bar, x, y); }

/* Lay the bar out for this frame (also used by input before a render). */
static void layout(float hide) {
    float W = (float)nxui_width(), H = (float)nxui_height();
    float bh = (float)DOCK_TASKBAR_HEIGHT;
    g_bar = nxc_rect(0.0f, H - bh + hide * (bh + 4.0f), W, bh);
    const dock_model_t *m = &g_dock_model;
    float x = 10.0f;
    g_npins = 0;
    for (int i = 0; i < m->nitems && g_npins < DOCK_MAX_ICONS; i++) {
        if (m->items[i].type != DOCK_IT_APP || m->items[i].pin < 0) continue;
        g_pin_rect[g_npins] = nxc_rect(x, g_bar.y + (bh - 40.0f) * 0.5f, TB_PIN_SLOT - 4.0f, 40.0f);
        g_pin_item[g_npins] = i;
        g_npins++;
        x += TB_PIN_SLOT;
    }
    x += 14.0f;                          /* separator */
    g_npills = m->nwins;
    if (g_npills <= 0) return;
    float avail = W - x - 10.0f;
    float pw = avail / (float)g_npills - TB_PILL_GAP;
    if (pw > TB_PILL_MAX) pw = TB_PILL_MAX;
    if (pw < TB_PILL_MIN) pw = TB_PILL_MIN;
    for (int i = 0; i < g_npills; i++) {
        g_pill_rect[i] = nxc_rect(x, g_bar.y + (bh - TB_PILL_H) * 0.5f, pw, TB_PILL_H);
        x += pw + TB_PILL_GAP;
        if (x > W) {                      /* no room left: drop the rest */
            g_npills = i + 1;
            break;
        }
    }
}

static int pin_at(int x, int y) {
    for (int i = 0; i < g_npins; i++) if (nxglass_hit(g_pin_rect[i], x, y)) return i;
    return -1;
}

static int pill_at(int x, int y) {
    for (int i = 0; i < g_npills; i++) if (nxglass_hit(g_pill_rect[i], x, y)) return i;
    return -1;
}

void taskbar_render(nxc_canvas_t *c, int mx, int my, float hide) {
    if (!c) return;
    const nxui_theme_t *th = nxui_theme();
    const dock_model_t *m = &g_dock_model;
    if (hide < 0.0f) hide = 0.0f;
    layout(hide);
    int moving = hide > 0.001f;
    int menu_item = dock_menu_item();

    nxglass_panel(c, g_bar, 0.0f, NXGLASS_CHROME, 0, moving);
    nxglass_separator(c, 0.0f, g_bar.y, g_bar.w);

    /* Pinned apps */
    int hp = dock_menu_is_open() ? -1 : pin_at(mx, my);
    for (int i = 0; i < g_npins; i++) {
        const dock_item_t *it = &m->items[g_pin_item[i]];
        nxc_rect_t r = g_pin_rect[i];
        int pressed = g_press_kind == 1 && g_press_idx == i && hp == i;
        if (i == hp || g_pin_item[i] == menu_item)
            nxc_fill_rrect(c, r, 8.0f, pressed ? th->control_fill_pressed : th->control_fill_hover);
        nxapp_icon_draw(c, (nxapp_icon_t)it->kind, r.x + (r.w - TB_PIN_ICON) * 0.5f,
                        r.y + (r.h - TB_PIN_ICON) * 0.5f - 1.0f, TB_PIN_ICON, 255);
        if (it->nwin > 0) {
            float uw = it->focused ? 16.0f : 8.0f;
            nxc_fill_rrect(c, nxc_rect(r.x + (r.w - uw) * 0.5f, g_bar.y + g_bar.h - 5.0f, uw, 3.0f), 1.5f,
                           it->focused ? th->accent : NXC_WITH_ALPHA(th->text_secondary, 220));
        }
    }
    if (g_npins > 0) {
        float sx = g_pin_rect[g_npins - 1].x + TB_PIN_SLOT + 3.0f;
        nxc_fill_rect(c, nxc_rect(sx, g_bar.y + 12.0f, 1.0f, g_bar.h - 24.0f), th->separator);
    }

    /* Window pills */
    int hv = dock_menu_is_open() ? -1 : pill_at(mx, my);
    for (int i = 0; i < g_npills; i++) {
        const dock_win_t *w = &m->wins[i];
        nxc_rect_t r = g_pill_rect[i];
        int pressed = g_press_kind == 2 && g_press_idx == i && hv == i;
        if (w->focused) {
            nxc_fill_rrect(c, r, 9.0f, pressed ? th->control_fill_pressed : th->control_fill_hover);
            nxc_stroke_rrect(c, r, 9.0f, 1.0f, th->highlight);
            nxc_fill_rrect(c, nxc_rect(r.x + r.w * 0.5f - 14.0f, r.y + r.h - 3.0f, 28.0f, 3.0f), 1.5f, th->accent);
        } else if (i == hv) {
            nxc_fill_rrect(c, r, 9.0f, pressed ? th->control_fill_pressed : th->control_fill);
        }
        uint8_t ia = w->minimized ? 130 : 255;
        float is = 20.0f;
        int icon_only = r.w < 90.0f;
        float ix = icon_only ? r.x + (r.w - is) * 0.5f : r.x + 10.0f;
        nxapp_icon_draw(c, (nxapp_icon_t)w->kind, ix, r.y + (r.h - is) * 0.5f, is, ia);
        if (!icon_only) {
            nxc_color_t fg = w->minimized ? th->text_tertiary : (w->focused ? th->text_primary : th->text_secondary);
            nxui_text_in_rect(c, w->focused ? NXUI_TEXT_BODY_MEDIUM : NXUI_TEXT_BODY,
                              nxc_rect(ix + is + 8.0f, r.y, r.w - (ix - r.x) - is - 18.0f, r.h),
                              w->title[0] ? w->title : "Untitled", fg, NXC_ALIGN_LEFT);
        }
    }

    /* Tooltip over a hovered pin */
    float dt = nxui_frame_dt();
    if (hp >= 0) g_tip_pin = hp;
    g_tip_alpha = nxui_approach(g_tip_alpha, hp >= 0 ? 1.0f : 0.0f, 0.05f, dt);
    if (g_tip_pin >= 0 && g_tip_pin < g_npins && g_tip_alpha > 0.02f) {
        const dock_item_t *it = &m->items[g_pin_item[g_tip_pin]];
        nxc_rect_t r = g_pin_rect[g_tip_pin];
        float tw = (float)(int)(nxui_text_width(NXUI_TEXT_BODY_MEDIUM, it->label) + 28.0f);
        nxc_rect_t tip = nxc_rect((float)(int)(r.x + r.w * 0.5f - tw * 0.5f), g_bar.y - 34.0f, tw, 26.0f);
        if (tip.x < 6.0f) tip.x = 6.0f;
        nxc_save(c);
        nxc_set_alpha(c, (uint8_t)(g_tip_alpha * 255.0f));
        nxglass_panel(c, tip, 9.0f, NXGLASS_REGULAR, 1, 1);
        nxc_stroke_rrect(c, tip, 9.0f, 1.0f, th->window_border);
        nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, tip, it->label, th->text_primary, NXC_ALIGN_CENTER);
        nxc_restore(c);
    }
}

int taskbar_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons) {
    int lpress = (buttons & 0x01) && !(prev_buttons & 0x01);
    int lrelease = !(buttons & 0x01) && (prev_buttons & 0x01);
    int rpress = (buttons & 0x02) && !(prev_buttons & 0x02);
    const dock_model_t *m = &g_dock_model;

    if (g_press_kind != 0) {
        if (lrelease) {
            int kind = g_press_kind, idx = g_press_idx;
            g_press_kind = 0;
            g_press_idx = -1;
            if (kind == 1 && pin_at(x, y) == idx && idx < g_npins) {
                dock_item_primary(g_pin_item[idx]);
            } else if (kind == 2 && pill_at(x, y) == idx && idx < g_npills && idx < m->nwins) {
                const dock_win_t *w = &m->wins[idx];
                if (w->focused && dock_hide_available()) dock_hide_window(w->id);
                else if (desktop_activate_window(w->id) != 0) dock_log("warning: taskbar window vanished");
            }
        }
        return 1;
    }
    if (!nxglass_hit(g_bar, x, y)) return 0;
    int p = pin_at(x, y), w = pill_at(x, y);
    if (lpress) {
        if (p >= 0) { g_press_kind = 1; g_press_idx = p; }
        else if (w >= 0) { g_press_kind = 2; g_press_idx = w; }
    } else if (rpress) {
        if (p >= 0) {
            nxc_rect_t r = g_pin_rect[p];
            dock_menu_open(g_pin_item[p], r.x + r.w * 0.5f, g_bar.y - 6.0f, DOCK_MENU_UP);
        } else if (w >= 0 && w < m->nwins) {
            int item = m->wins[w].item;
            if (item < 0) {                          /* Trash windows belong to the Trash item */
                for (int i = 0; i < m->nitems; i++) if (m->items[i].type == DOCK_IT_TRASH) item = i;
            }
            nxc_rect_t r = g_pill_rect[w];
            if (item >= 0) dock_menu_open(item, r.x + r.w * 0.5f, g_bar.y - 6.0f, DOCK_MENU_UP);
        }
    }
    return 1;
}
