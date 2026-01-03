/*
 * NeolyxOS - NXGFX Graphics Library
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXGFX_H
#define NXGFX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core Types */
typedef struct { uint8_t r, g, b, a; } nx_color_t;
typedef struct { int32_t x, y; } nx_point_t;
typedef struct { uint32_t width, height; } nx_size_t;
typedef struct { int32_t x, y; uint32_t width, height; } nx_rect_t;
typedef struct nx_context nx_context_t;
typedef struct nx_font nx_font_t;
typedef struct nx_image nx_image_t;

/* Color helpers */
static inline nx_color_t nx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (nx_color_t){ r, g, b, 255 };
}
static inline nx_color_t nx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (nx_color_t){ r, g, b, a };
}
#define NX_COLOR_WHITE  ((nx_color_t){ 255, 255, 255, 255 })
#define NX_COLOR_BLACK  ((nx_color_t){ 0, 0, 0, 255 })

/* Geometry helpers */
static inline nx_rect_t nx_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    return (nx_rect_t){ x, y, w, h };
}
static inline bool nx_rect_contains(nx_rect_t r, nx_point_t p) {
    return p.x >= r.x && p.x < (int32_t)(r.x + r.width) &&
           p.y >= r.y && p.y < (int32_t)(r.y + r.height);
}

/* Context API */
nx_context_t* nxgfx_init(void* fb, uint32_t w, uint32_t h, uint32_t pitch);
void nxgfx_destroy(nx_context_t* ctx);
uint32_t nxgfx_width(const nx_context_t* ctx);
uint32_t nxgfx_height(const nx_context_t* ctx);

/* Drawing primitives */
void nxgfx_clear(nx_context_t* ctx, nx_color_t color);
void nxgfx_fill_rect(nx_context_t* ctx, nx_rect_t rect, nx_color_t color);
void nxgfx_draw_rect(nx_context_t* ctx, nx_rect_t rect, nx_color_t color, uint32_t stroke);
void nxgfx_fill_rounded_rect(nx_context_t* ctx, nx_rect_t rect, nx_color_t color, uint32_t radius);
void nxgfx_fill_circle(nx_context_t* ctx, nx_point_t center, uint32_t radius, nx_color_t color);
void nxgfx_draw_line(nx_context_t* ctx, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t w);
void nxgfx_put_pixel(nx_context_t* ctx, int32_t x, int32_t y, nx_color_t color);

/* Gradient */
typedef enum { NX_GRADIENT_H, NX_GRADIENT_V, NX_GRADIENT_D } nx_gradient_dir_t;
void nxgfx_fill_gradient(nx_context_t* ctx, nx_rect_t rect, nx_color_t c1, nx_color_t c2, nx_gradient_dir_t dir);

/* Text */
nx_font_t* nxgfx_font_load(nx_context_t* ctx, const char* path, uint32_t size);
nx_font_t* nxgfx_font_default(nx_context_t* ctx, uint32_t size);
void nxgfx_font_destroy(nx_font_t* font);
void nxgfx_draw_text(nx_context_t* ctx, const char* text, nx_point_t pos, nx_font_t* font, nx_color_t color);
nx_size_t nxgfx_measure_text(nx_context_t* ctx, const char* text, nx_font_t* font);

/* Images */
nx_image_t* nxgfx_image_load(nx_context_t* ctx, const char* path);
nx_image_t* nxgfx_image_create(nx_context_t* ctx, const void* data, uint32_t w, uint32_t h);
void nxgfx_image_destroy(nx_image_t* img);
void nxgfx_draw_image(nx_context_t* ctx, const nx_image_t* img, nx_point_t pos);
void nxgfx_draw_image_scaled(nx_context_t* ctx, const nx_image_t* img, nx_rect_t dest);

/* Clipping & Frame */
void nxgfx_set_clip(nx_context_t* ctx, nx_rect_t clip);
void nxgfx_clear_clip(nx_context_t* ctx);
void nxgfx_present(nx_context_t* ctx);

#ifdef __cplusplus
}
#endif
#endif
