/*
 * NXRender Backend for IconLay
 * 
 * GPU-accelerated rendering using NXRender/NXSVG.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_BACKEND_H
#define NXRENDER_BACKEND_H

#include "nxi_format.h"
#include <nxgfx/nxgfx.h>
#include <nxgfx/nxsvg.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize NXRender backend with framebuffer */
int nxrender_backend_init(void* framebuffer, uint32_t width, uint32_t height, uint32_t pitch);

/* Shutdown and cleanup */
void nxrender_backend_shutdown(void);

/* Get raw context for advanced use */
nx_context_t* nxrender_backend_get_context(void);

/* SVG loading and rendering */
nxsvg_image_t* nxrender_backend_load_svg(const char* path);
nxsvg_image_t* nxrender_backend_parse_svg(const char* data, size_t len);
void nxrender_backend_render_svg(nxsvg_image_t* svg, float x, float y, float scale, uint32_t color_override);

/* Convert NXI paths to NXSVG for GPU rendering */
nxsvg_image_t* nxrender_backend_paths_to_svg(const nxi_path_cmd_t* paths, uint32_t count,
                                               float width, float height, uint32_t fill_color);

/* Drawing primitives (color = RGBA packed) */
void nxrender_backend_clear(uint32_t color);
void nxrender_backend_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void nxrender_backend_fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, 
                                         uint32_t color, uint32_t radius);
void nxrender_backend_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t color);
void nxrender_backend_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, 
                                 uint32_t color, uint32_t thickness);

/* Gradients */
void nxrender_backend_fill_gradient(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                     uint32_t color1, uint32_t color2, int horizontal);

/* Cards with elevation/shadows */
void nxrender_backend_fill_card(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                 uint32_t color, uint32_t radius, int elevated);

/* Text */
void nxrender_backend_draw_text(const char* text, int32_t x, int32_t y, 
                                 uint32_t color, uint32_t size);

/* Clipping */
void nxrender_backend_set_clip(int32_t x, int32_t y, uint32_t w, uint32_t h);
void nxrender_backend_clear_clip(void);

/* Present to screen */
void nxrender_backend_present(void);

#ifdef __cplusplus
}
#endif

#endif /* NXRENDER_BACKEND_H */
