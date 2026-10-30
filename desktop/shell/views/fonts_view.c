/*
 * NeolyxOS - Fonts app view (shell-hosted, see app_view.h)
 *
 * Placeholder wired into the app view registry; the Fonts area
 * replaces this file.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "app_view.h"
#include "nxui.h"

int fonts_view_open(uint32_t win_id) { (void)win_id; return 0; }

void fonts_view_render(desktop_window_t *win, void *ctx) {
    (void)ctx;
    if (!win) return;
    nxc_rect_t r = app_view_content_rect(win);
    nxui_text_in_rect(nxui_canvas(), NXUI_TEXT_BODY, r, win->title,
                      nxui_theme()->text_secondary, NXC_ALIGN_CENTER);
}

void fonts_view_key(desktop_window_t *win, uint8_t sc, uint8_t pressed, void *ctx) {
    (void)win; (void)sc; (void)pressed; (void)ctx;
}

void fonts_view_mouse(desktop_window_t *win, int32_t lx, int32_t ly, uint8_t b, uint8_t pb, void *ctx) {
    (void)win; (void)lx; (void)ly; (void)b; (void)pb; (void)ctx;
}
