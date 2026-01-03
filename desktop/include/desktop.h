/*
 * NeolyxOS Desktop Environment
 * 
 * NXRender-based desktop with:
 *   - Window manager
 *   - Taskbar
 *   - App launcher
 *   - Desktop icons
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_DESKTOP_H
#define NEOLYX_DESKTOP_H

#include <stdint.h>

/* ============ Desktop Configuration ============ */

typedef struct {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t taskbar_height;
    uint32_t bg_color;
    char wallpaper_path[256];
    int show_desktop_icons;
    int dark_mode;
} desktop_config_t;

/* ============ Window Structure ============ */

typedef struct desktop_window {
    uint32_t id;
    char title[128];
    int32_t x, y;
    uint32_t width, height;
    int minimized;
    int maximized;
    int focused;
    uint32_t *framebuffer;
    struct desktop_window *next;
} desktop_window_t;

/* ============ Desktop State ============ */

typedef struct {
    desktop_config_t config;
    desktop_window_t *windows;
    desktop_window_t *focused_window;
    int32_t mouse_x, mouse_y;
    int mouse_buttons;
    int running;
    uint32_t *screen_fb;
} desktop_state_t;

/* ============ Desktop API ============ */

/**
 * Initialize the desktop environment.
 * @param fb_addr   Framebuffer physical address
 * @param width     Screen width in pixels
 * @param height    Screen height in pixels
 * @param pitch     Framebuffer pitch (bytes per row)
 */
int desktop_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);

/**
 * Start startup applications.
 */
void desktop_start_apps(void);

/**
 * Start the desktop main loop.
 */
void desktop_run(void) __attribute__((noreturn));

/**
 * Create a new window.
 */
desktop_window_t *desktop_create_window(const char *title, int x, int y, 
                                         int width, int height);

/**
 * Destroy a window.
 */
void desktop_destroy_window(desktop_window_t *window);

/**
 * Focus a window.
 */
void desktop_focus_window(desktop_window_t *window);

/**
 * Draw the desktop.
 */
void desktop_draw(void);

/**
 * Handle input events.
 */
void desktop_handle_input(void);

/* ============ Taskbar ============ */

/**
 * Draw the taskbar.
 */
void taskbar_draw(void);

/**
 * Handle taskbar click.
 */
void taskbar_click(int x, int y);

/* ============ App Launcher ============ */

/**
 * Show app launcher.
 */
void launcher_show(void);

/**
 * Hide app launcher.
 */
void launcher_hide(void);

/**
 * Launch an application.
 */
int launcher_run_app(const char *path);

/* ============ Desktop Icons ============ */

typedef struct {
    char name[64];
    char icon_path[256];
    char exec_path[256];
    int x, y;
} desktop_icon_t;

/**
 * Add desktop icon.
 */
int desktop_add_icon(const char *name, const char *icon, const char *exec);

/**
 * Handle icon click.
 */
void desktop_icon_click(int x, int y);

#endif /* NEOLYX_DESKTOP_H */
