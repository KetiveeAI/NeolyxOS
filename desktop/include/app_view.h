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
    /* Scroll wheel while the pointer is over the content area (focused or
     * not). LOCAL coordinates; notches > 0 scrolls toward the end. May be NULL. */
    void (*scroll)(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);
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
void settings_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);
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
void taskmgr_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);

/* shell/views/path_view.c  (Path, the file browser; reads through nxpath_relay.h) */
int  path_view_open(uint32_t win_id);
void path_view_render(desktop_window_t *win, void *ctx);
void path_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void path_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);
void path_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);
/* Open Path at a folder (launches or focuses the window). 0 on success. */
int  path_view_open_at(const char *path);

/* shell/views/shell_view.c  (Shell: the command shell, formerly Terminal) */
int  shell_view_open(uint32_t win_id);
void shell_view_render(desktop_window_t *win, void *ctx);
void shell_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void shell_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);
void shell_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);

/* shell/views/notes_view.c  (Notes: the plain-text note editor; notes live in ~/Documents/Notes) */
int  notes_view_open(uint32_t win_id);
void notes_view_render(desktop_window_t *win, void *ctx);
void notes_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx);
void notes_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx);
void notes_view_scroll(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);
/* Open a text file (full path) in Notes, launching or focusing the window.
 * The open note is saved first; returns -1 if that save fails. */
int  notes_view_open_file(const char *path);

#endif /* APP_VIEW_H */
