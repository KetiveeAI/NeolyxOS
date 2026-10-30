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

#endif /* MENUBAR_H */
