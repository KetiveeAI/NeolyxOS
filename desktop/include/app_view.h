/*
 * NeolyxOS - Shell-hosted app views
 *
 * A registry of the system apps whose window content is drawn by the
 * desktop shell (Settings, Font Manager, Task Manager, ...). Launching an
 * app by name (dock, app drawer, search, menu bar) looks it up here,
 * creates its window at the default size and installs its callbacks.
 *
 * Each view lives in its own file under shell/views/ and keeps its own
 * static state; one window per view (re-launching focuses it).
 *
 * Coordinates: render callbacks draw in ABSOLUTE screen coordinates.
 * The content area of a window is
 *     x = win->x, y = win->y + APP_VIEW_TITLE_H, w = win->width, h = win->height
 * and is already clipped. Mouse callbacks receive LOCAL coordinates
 * (0,0 = content top-left) and are only called while the pointer is
 * inside the content area of the focused window, or while a button
 * that was pressed inside it is still held (drag capture).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef APP_VIEW_H
#define APP_VIEW_H

#include <stdint.h>
#include "desktop_shell.h"
#include "nxcanvas.h"

#define APP_VIEW_TITLE_H 24

typedef struct {
    const char *name;            /* window title and launch name */
    uint32_t    default_w;       /* content width  (px) */
    uint32_t    default_h;       /* content height (px), excludes title bar */
    /* Called once after the window is created. Return 0 on success; a
     * non-zero return destroys the window and fails the launch. May be NULL. */
    int  (*open)(uint32_t win_id);
    void (*render)(desktop_window_t *win, void *ctx);
    void (*key)(desktop_window_t *win, uint8_t scancode, uint8_t pressed, void *ctx);
    void (*mouse)(desktop_window_t *win, int32_t lx, int32_t ly,
                  uint8_t buttons, uint8_t prev_buttons, void *ctx);
} app_view_t;

/* Find a view by window/launch name (case-insensitive). NULL if none. */
const app_view_t *app_view_find(const char *name);

/* Content rect of a window in absolute coordinates. */
static inline nxc_rect_t app_view_content_rect(const desktop_window_t *win) {
    return nxc_rect((float)win->x, (float)win->y + (float)APP_VIEW_TITLE_H,
                    (float)win->width, (float)win->height);
}

/* ---- View entry points (one block per shell/views/ file) ---- */

/* shell/views/settings_view.c  (Settings area) */
int  settings_view_open(uint32_t win_id);
void settings_view_render(desktop_window_t *win, void *ctx);
void settings_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void settings_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);
/* Open Settings on a pane by id (e.g. "network", "appearance"). Launches
 * or focuses the window. Returns 0 on success. */
int  settings_view_show_pane(const char *pane_id);

/* shell/views/fonts_view.c  (Fonts area) */
int  fonts_view_open(uint32_t win_id);
void fonts_view_render(desktop_window_t *win, void *ctx);
void fonts_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void fonts_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);

/* shell/views/taskmgr_view.c  (Task manager area) */
int  taskmgr_view_open(uint32_t win_id);
void taskmgr_view_render(desktop_window_t *win, void *ctx);
void taskmgr_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void taskmgr_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);

#endif /* APP_VIEW_H */
