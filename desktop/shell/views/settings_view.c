/*
 * NeolyxOS - Settings app view (shell-hosted, see app_view.h)
 *
 * Placeholder wired into the app view registry; the Settings area
 * replaces this file.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxui.h"

int settings_view_open(uint32_t win_id) { (void)win_id; return 0; }

void settings_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_rect_t r = app_view_content_rect(win);
    nxui_text_in_rect(nxui_canvas(), NXUI_TEXT_BODY, r, win->title,
                      nxui_theme()->text_secondary, NXC_ALIGN_CENTER);
}

void settings_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)sc; (void)pressed; (void)ctx;
}

void settings_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)win; (void)lx; (void)ly; (void)b; (void)pb; (void)ctx;
}

int settings_view_show_pane(const char *pane_id) { (void)pane_id; return -1; }
