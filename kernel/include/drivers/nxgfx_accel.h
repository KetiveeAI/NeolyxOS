/*
 * NXGraphics 2D Acceleration
 * 
 * Hardware-accelerated 2D operations for NeolyxOS.
 * Software fallback for QEMU/Bochs.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_NXGFX_ACCEL_H
#define NEOLYX_NXGFX_ACCEL_H

#include <stdint.h>

/* ============ Rectangle ============ */

typedef struct {
    int32_t x, y;
    uint32_t width, height;
} nxgfx_rect_t;

/* ============ Color ============ */

typedef struct {
    uint8_t r, g, b, a;
} nxgfx_color_t;

#define NXGFX_RGB(r,g,b)    ((nxgfx_color_t){(r),(g),(b),255})
#define NXGFX_RGBA(r,g,b,a) ((nxgfx_color_t){(r),(g),(b),(a)})

/* ============ 2D Acceleration API ============ */

/**
 * nxgfx_accel_init - Initialize 2D acceleration
 * 
 * @fb: Framebuffer address
 * @width: Screen width
 * @height: Screen height
 * @pitch: Bytes per row
 * 
 * Returns: 0 on success
 */
int nxgfx_accel_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch);

/**
 * nxgfx_accel_fill_rect - Fast rectangle fill
 */
void nxgfx_accel_fill_rect(const nxgfx_rect_t *rect, nxgfx_color_t color);

/**
 * nxgfx_accel_blit - Copy pixels from source to destination
 */
void nxgfx_accel_blit(const nxgfx_rect_t *src_rect, const nxgfx_rect_t *dst_rect,
                      const uint32_t *src_pixels);

/**
 * nxgfx_accel_blend - Alpha blend rectangle
 */
void nxgfx_accel_blend(const nxgfx_rect_t *rect, nxgfx_color_t color);

/**
 * nxgfx_accel_line - Draw line with Bresenham
 */
void nxgfx_accel_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nxgfx_color_t color);

/**
 * nxgfx_accel_circle - Draw circle outline
 */
void nxgfx_accel_circle(int32_t cx, int32_t cy, int32_t radius, nxgfx_color_t color);

/**
 * nxgfx_accel_gradient_h - Horizontal gradient fill
 */
void nxgfx_accel_gradient_h(const nxgfx_rect_t *rect, nxgfx_color_t start, nxgfx_color_t end);

#endif /* NEOLYX_NXGFX_ACCEL_H */
