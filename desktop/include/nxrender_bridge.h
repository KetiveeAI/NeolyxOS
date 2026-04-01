/*
 * NeolyxOS - NXRender Bridge Layer
 * 
 * Stub implementation for desktop shell compatibility.
 * Desktop uses direct pixel rendering instead of NXRender.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_BRIDGE_H
#define NXRENDER_BRIDGE_H

#include <stdint.h>

/* Forward declarations for stub types */
typedef struct nx_context nx_context_t;
typedef struct nx_font nx_font_t;

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

/* Convenience drawing functions (stubs - desktop uses direct pixel rendering) */
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
void nxr_end_frame(void);
void nxr_clear(uint32_t color);

#endif /* NXRENDER_BRIDGE_H */
