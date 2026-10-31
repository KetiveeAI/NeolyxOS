/*
 * NeolyxOS - Menu bar menus
 *
 * The application menus in the system menu bar (File, Edit, View, Window,
 * Help): title hit areas, the open dropdown, hover tracking and item
 * activation. Items publish their action through the shell callback
 * registered with menubar_set_handler(); the shell owns what they do.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef MENUBAR_H
#define MENUBAR_H

#include <stdint.h>
#include "nxcanvas.h"

#define MENUBAR_MENU_MAX   6
#define MENUBAR_ITEM_MAX   12

typedef struct {
    const char *label;       /* NULL = separator */
    const char *shortcut;    /* e.g. "Ctrl N", may be NULL */
    int         enabled;
    int         action;      /* passed to the handler */
} menubar_item_t;

typedef struct {
    const char           *title;
    const menubar_item_t *items;
    int                   count;
} menubar_menu_t;

/* Called when an enabled item is activated. */
typedef void (*menubar_action_fn)(int menu, int action);

void menubar_init(void);
void menubar_set_handler(menubar_action_fn fn);

/* Draw the titles starting at x in the bar of height h; records the hit
 * areas used by menubar_handle_mouse. Returns the x after the last title. */
float menubar_render_titles(nxc_canvas_t *c, float x, float h);
/* Draw the open dropdown (above windows and the dock). */
void menubar_render(nxc_canvas_t *c);

/* Returns 1 when the event was consumed (a menu is open, or a title was hit). */
int  menubar_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons);
/* Escape closes, arrows move; returns 1 when consumed. */
int  menubar_handle_key(uint8_t scancode, uint8_t pressed);
int  menubar_is_open(void);
void menubar_close(void);

/* ============ The whole bar ============
 *
 * menubar_render_bar draws the bar background, the focused app's name in
 * bold (from desktop_focused_window_id; "NeolyxOS" when no window has
 * focus), the menu titles, and the status area on the right:
 *
 *   [battery] [volume] [Wi-Fi]  clock  [bell] [Control Center]
 *
 * The bell and Control Center items are centered at fixed distances from
 * the right edge so the shell's click zones keep matching them:
 *   Control Center  center w - MENUBAR_CC_CENTER  (shell zone w-50 .. w-20)
 *   Notifications   center w - MENUBAR_NC_CENTER  (shell zone w-80 .. w-55)
 *
 * clock_text is preformatted by the shell (it owns the RTC cache), may be
 * NULL. Volume and Wi-Fi come from the Control Center state; the battery
 * item is shown only after menubar_set_power_status reports a battery. */
#define MENUBAR_CC_CENTER   35.0f
#define MENUBAR_NC_CENTER   67.0f

void menubar_render_bar(nxc_canvas_t *c, float w, float h, const char *clock_text);

/* percent 0..100, or -1 for "no battery / unknown" (hides the item).
 * charging 0 or 1. Returns 0, or -1 on invalid arguments (nothing changes). */
int  menubar_set_power_status(int percent, int charging);

#endif /* MENUBAR_H */
