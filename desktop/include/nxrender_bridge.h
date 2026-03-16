/*
 * NeolyxOS - NXRender Bridge Layer
 * 
 * Connects NXRender graphics library to the desktop shell.
 * Provides initialization and API wrappers for smooth migration.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_BRIDGE_H
#define NXRENDER_BRIDGE_H

#include <stdint.h>
/* Only include nxgfx to avoid nx_event_t conflict with desktop's nxevent.h */
#include <nxgfx/nxgfx.h>

/* Initialize NXRender with kernel framebuffer */
int nxr_bridge_init(void *framebuffer, uint32_t width, uint32_t height, uint32_t pitch);

/* Shutdown NXRender */
void nxr_bridge_shutdown(void);

/* Get context for direct NXRender API calls */
nx_context_t* nxr_get_context(void);

/* Default font access */
nx_font_t* nxr_get_font_default(void);
nx_font_t* nxr_get_font_small(void);
nx_font_t* nxr_get_font_large(void);

/* Convenience drawing functions (replaces desktop_draw_*) */
void nxr_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
void nxr_draw_text_large(int32_t x, int32_t y, const char *text, uint32_t color);
void nxr_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void nxr_fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color);
void nxr_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t color);
void nxr_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);

/* Gradient fills */
void nxr_fill_gradient_h(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2);
void nxr_fill_gradient_v(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2);

/* Image support */
void nxr_draw_image(const void *rgba_data, int32_t x, int32_t y, uint32_t w, uint32_t h);

/* Frame control */
void nxr_begin_frame(void);
void nxr_end_frame(void);      /* Presents to screen with double-buffer */
void nxr_clear(uint32_t color);

/* Convert ARGB to nx_color_t */
static inline nx_color_t nxr_argb_to_color(uint32_t argb) {
    return (nx_color_t){
        .r = (argb >> 16) & 0xFF,
        .g = (argb >> 8) & 0xFF,
        .b = argb & 0xFF,
        .a = (argb >> 24) & 0xFF
    };
}

#endif /* NXRENDER_BRIDGE_H */
