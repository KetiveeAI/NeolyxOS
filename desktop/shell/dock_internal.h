/*
 * NeolyxOS - Dock internals shared by dock.c, dock_menu.c and taskbar_bar.c
 *
 * Not a public header: only the dock's own translation units include it.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef DOCK_INTERNAL_H
#define DOCK_INTERNAL_H

#include <stdint.h>
#include "../include/dock.h"
#include "../include/desktop_shell.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxicons.h"

/* ============ Model: what the dock shows ============ */

typedef enum {
    DOCK_IT_APP = 0,      /* pinned or running application */
    DOCK_IT_SEP,          /* separator before the minimized section */
    DOCK_IT_MINI,         /* one minimized window */
    DOCK_IT_TRASH
} dock_item_type_t;

#define DOCK_MAX_ITEMS     (DOCK_MAX_ICONS + MAX_WINDOWS + 4)
#define DOCK_MAX_MINI      8           /* minimized windows shown in the dock */

typedef struct {
    dock_item_type_t type;
    int      pin;               /* index into pinned apps, -1 when not pinned */
    int      kind;              /* nxapp_icon_t */
    char     label[32];
    uint32_t key;               /* stable identity for animation state */
    int      nwin;              /* open windows of this app (incl. minimized) */
    int      nmin;              /* of which minimized */
    int      focused;           /* owns the focused window */
    uint32_t front_id;          /* window to activate on click (0 = none) */
    uint32_t win_id;            /* DOCK_IT_MINI: the window */
} dock_item_t;

typedef struct {
    uint32_t id;
    int      item;              /* index of its app item, -1 if none */
    int      minimized;
    int      focused;
    int      kind;              /* nxapp_icon_t */
    char     title[WINDOW_TITLE_MAX];
} dock_win_t;

typedef struct {
    dock_item_t items[DOCK_MAX_ITEMS];
    int         nitems;
    dock_win_t  wins[MAX_WINDOWS];   /* open windows in slot order */
    int         nwins;
} dock_model_t;

/* The current model; refreshed by dock_model_refresh() when the window
 * table or the pinned list changes (cheap signature check otherwise). */
extern dock_model_t g_dock_model;
void dock_model_refresh(void);
void dock_model_invalidate(void);

/* ============ Actions (dock.c) ============ */

/* Primary click: activate the frontmost window, or launch (with bounce). */
void dock_item_primary(int item);
/* Launch the app of an item through the event bus (bounces it). */
void dock_item_launch(int item);
/* Bring every window of an app item to the front (minimized ones too). */
void dock_item_show_all(int item);
/* Hide (minimize) every visible window of an app item. Needs a handler. */
void dock_item_hide(int item);
/* Close every window of an app item. */
void dock_item_quit(int item);
/* Pin / unpin an app item; both persist the pinned list. */
void dock_item_keep(int item);
void dock_item_unpin(int item);
int  dock_hide_available(void);
int  dock_hide_window(uint32_t id);

/* Configuration in effect (validated). */
dock_layout_t   dock_cfg_layout(void);
dock_position_t dock_cfg_position(void);

/* Bounce timer for an item key (seconds left, 0 = none) and the lift in
 * px for a given amplitude. */
float dock_bounce_lift(uint32_t key, float amplitude);

/* Logging helpers ("[DOCK] ..." on the serial log). */
void dock_log(const char *msg);
void dock_log2(const char *msg, const char *arg);

/* ============ Context menu (dock_menu.c) ============ */

/* Side the menu opens toward from its anchor point. */
typedef enum { DOCK_MENU_UP = 0, DOCK_MENU_RIGHT, DOCK_MENU_LEFT } dock_menu_side_t;

void dock_menu_open(int item, float anchor_x, float anchor_y, dock_menu_side_t side);
void dock_menu_render(nxc_canvas_t *c, int mouse_x, int mouse_y);
/* Returns 1 (always consumes) while open. */
int  dock_menu_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons);
/* Item index the menu belongs to, -1 when closed. */
int  dock_menu_item(void);

/* ============ Taskbar (taskbar_bar.c) ============ */

/* hide: 0 (shown) .. 1 (fully hidden below the edge). */
void taskbar_render(nxc_canvas_t *c, int mouse_x, int mouse_y, float hide);
/* Input while the taskbar layout is active. Returns 1 when consumed. */
int  taskbar_handle_mouse(int x, int y, uint8_t buttons, uint8_t prev_buttons);
/* Bar rect of the last frame (for autohide and minimize targets). */
nxc_rect_t taskbar_rect(void);
/* 1 when (x, y) is over the bar. */
int  taskbar_contains(int x, int y);

/* ============ Small helpers (dock.c) ============ */

int  dock_streq_ci(const char *a, const char *b);
void dock_strlcpy(char *dst, const char *src, int cap);

#endif /* DOCK_INTERNAL_H */
