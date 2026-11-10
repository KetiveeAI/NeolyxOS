/*
 * NeolyxOS - view host: run one desktop app view as its own process
 *
 * The built-in apps (Path, Notes, Settings, ...) are written as app views
 * (shell/app_view.h): render, key, mouse and scroll callbacks that draw
 * with NxUI on NXCanvas. Inside the desktop they draw into the desktop's
 * frame. The view host runs the same view in a separate process: it opens
 * one window through the window server (libnxwindow), draws the view into
 * an app backbuffer (nxui_init_app), copies it into the window buffer and
 * turns window events into the view's callbacks. The view's code is the
 * same in both places; only the few desktop_* calls a view makes are
 * answered here (viewhost.c) instead of by the shell.
 *
 * A hosted app never touches the screen, the cursor or the global input
 * queue: everything goes through its window.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_VIEWHOST_H
#define NEOLYX_VIEWHOST_H

#include <stdint.h>
#include "desktop_shell.h"

typedef struct {
    const char *title;          /* window title; the shell finds the window by it */
    uint32_t    width, height;  /* default content size */
    int         resizable;
    /* Called once after the window exists (win_id is the host's window).
     * Non-zero ends the app. May be NULL. */
    int  (*open)(uint32_t win_id);
    void (*render)(desktop_window_t *win, void *ctx);
    void (*key)(desktop_window_t *win, uint8_t scancode, uint8_t pressed, void *ctx);
    void (*mouse)(desktop_window_t *win, int32_t lx, int32_t ly,
                  uint8_t buttons, uint8_t prev_buttons, void *ctx);
    void (*scroll)(desktop_window_t *win, int32_t lx, int32_t ly, int32_t notches, void *ctx);
    /* The shell asked the window to show a path (program argument at start,
     * or a shell open later). 0 if shown. May be NULL. */
    int  (*open_path)(const char *path);
} viewhost_app_t;

/*
 * Connect to the window server, open the window, run until the user closes
 * it or the server goes away. argv[1], when given, goes to open_path once
 * the window is open. Returns the process exit status (0 = normal close).
 */
int viewhost_run(const viewhost_app_t *app, int argc, char **argv);

/* Ask the shell to open an absolute path (window protocol 1.1 SHELL_OPEN):
 * a folder in Files, a text file in Notes. 0 once sent; the shell may still
 * refuse (logged when its ERROR arrives). */
int viewhost_shell_open(const char *path);

#endif /* NEOLYX_VIEWHOST_H */
