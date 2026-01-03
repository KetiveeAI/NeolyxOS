/*
 * NXGraphics 2D Acceleration Implementation
 * 
 * Software-optimized 2D operations with future hardware accel hooks.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/drivers/nxgfx_accel.h"

/* ============ Static State ============ */

static struct {
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  /* In pixels */
    int initialized;
} g_accel = {0};

/* ============ Helpers ============ */

static inline uint32_t color_to_pixel(nxgfx_color_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | 
           ((uint32_t)c.g << 8) | c.b;
}

static inline void put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && x < (int32_t)g_accel.width && 
        y >= 0 && y < (int32_t)g_accel.height) {
        g_accel.framebuffer[y * g_accel.pitch + x] = color;
    }
}

static inline uint32_t get_pixel(int32_t x, int32_t y) {
    if (x >= 0 && x < (int32_t)g_accel.width && 
        y >= 0 && y < (int32_t)g_accel.height) {
        return g_accel.framebuffer[y * g_accel.pitch + x];
    }
    return 0;
}

static inline int32_t abs32(int32_t x) {
    return x < 0 ? -x : x;
}

static inline uint8_t blend_channel(uint8_t bg, uint8_t fg, uint8_t alpha) {
    return (uint8_t)((fg * alpha + bg * (255 - alpha)) / 255);
}

/* ============ Public API ============ */

int nxgfx_accel_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!fb || width == 0 || height == 0) return -1;
    
    g_accel.framebuffer = fb;
    g_accel.width = width;
    g_accel.height = height;
    g_accel.pitch = pitch / sizeof(uint32_t);
    g_accel.initialized = 1;
    
    return 0;
}

void nxgfx_accel_fill_rect(const nxgfx_rect_t *rect, nxgfx_color_t color) {
    if (!g_accel.initialized || !rect) return;
    
    uint32_t pixel = color_to_pixel(color);
    
    int32_t x0 = rect->x;
    int32_t y0 = rect->y;
    int32_t x1 = rect->x + (int32_t)rect->width;
    int32_t y1 = rect->y + (int32_t)rect->height;
    
    /* Clip to screen */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)g_accel.width) x1 = g_accel.width;
    if (y1 > (int32_t)g_accel.height) y1 = g_accel.height;
    
    /* Fast fill using word writes */
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = &g_accel.framebuffer[y * g_accel.pitch + x0];
        for (int32_t x = x0; x < x1; x++) {
            *row++ = pixel;
        }
    }
}

void nxgfx_accel_blit(const nxgfx_rect_t *src_rect, const nxgfx_rect_t *dst_rect,
                      const uint32_t *src_pixels) {
    if (!g_accel.initialized || !src_rect || !dst_rect || !src_pixels) return;
    
    int32_t dx = dst_rect->x;
    int32_t dy = dst_rect->y;
    uint32_t w = src_rect->width < dst_rect->width ? src_rect->width : dst_rect->width;
    uint32_t h = src_rect->height < dst_rect->height ? src_rect->height : dst_rect->height;
    
    for (uint32_t y = 0; y < h; y++) {
        int32_t screen_y = dy + (int32_t)y;
        if (screen_y < 0 || screen_y >= (int32_t)g_accel.height) continue;
        
        for (uint32_t x = 0; x < w; x++) {
            int32_t screen_x = dx + (int32_t)x;
            if (screen_x < 0 || screen_x >= (int32_t)g_accel.width) continue;
            
            uint32_t src_idx = (src_rect->y + y) * src_rect->width + (src_rect->x + x);
            g_accel.framebuffer[screen_y * g_accel.pitch + screen_x] = src_pixels[src_idx];
        }
    }
}

void nxgfx_accel_blend(const nxgfx_rect_t *rect, nxgfx_color_t color) {
    if (!g_accel.initialized || !rect || color.a == 0) return;
    
    /* Opaque fast path */
    if (color.a == 255) {
        nxgfx_accel_fill_rect(rect, color);
        return;
    }
    
    int32_t x0 = rect->x;
    int32_t y0 = rect->y;
    int32_t x1 = rect->x + (int32_t)rect->width;
    int32_t y1 = rect->y + (int32_t)rect->height;
    
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)g_accel.width) x1 = g_accel.width;
    if (y1 > (int32_t)g_accel.height) y1 = g_accel.height;
    
    for (int32_t y = y0; y < y1; y++) {
        for (int32_t x = x0; x < x1; x++) {
            uint32_t bg = get_pixel(x, y);
            uint8_t bg_r = (bg >> 16) & 0xFF;
            uint8_t bg_g = (bg >> 8) & 0xFF;
            uint8_t bg_b = bg & 0xFF;
            
            uint8_t r = blend_channel(bg_r, color.r, color.a);
            uint8_t g = blend_channel(bg_g, color.g, color.a);
            uint8_t b = blend_channel(bg_b, color.b, color.a);
            
            put_pixel(x, y, (0xFF << 24) | (r << 16) | (g << 8) | b);
        }
    }
}

void nxgfx_accel_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nxgfx_color_t color) {
    if (!g_accel.initialized) return;
    
    uint32_t pixel = color_to_pixel(color);
    
    /* Bresenham's line algorithm */
    int32_t dx = abs32(x1 - x0);
    int32_t dy = abs32(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    
    while (1) {
        put_pixel(x0, y0, pixel);
        
        if (x0 == x1 && y0 == y1) break;
        
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void nxgfx_accel_circle(int32_t cx, int32_t cy, int32_t radius, nxgfx_color_t color) {
    if (!g_accel.initialized || radius <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    
    /* Midpoint circle algorithm */
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 1 - radius;
    
    while (x >= y) {
        put_pixel(cx + x, cy + y, pixel);
        put_pixel(cx + y, cy + x, pixel);
        put_pixel(cx - y, cy + x, pixel);
        put_pixel(cx - x, cy + y, pixel);
        put_pixel(cx - x, cy - y, pixel);
        put_pixel(cx - y, cy - x, pixel);
        put_pixel(cx + y, cy - x, pixel);
        put_pixel(cx + x, cy - y, pixel);
        
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void nxgfx_accel_gradient_h(const nxgfx_rect_t *rect, nxgfx_color_t start, nxgfx_color_t end) {
    if (!g_accel.initialized || !rect || rect->width == 0) return;
    
    int32_t x0 = rect->x;
    int32_t y0 = rect->y;
    int32_t x1 = rect->x + (int32_t)rect->width;
    int32_t y1 = rect->y + (int32_t)rect->height;
    
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)g_accel.width) x1 = g_accel.width;
    if (y1 > (int32_t)g_accel.height) y1 = g_accel.height;
    
    for (int32_t x = x0; x < x1; x++) {
        /* Calculate interpolation factor (0-255) */
        int32_t t = ((x - rect->x) * 255) / (int32_t)rect->width;
        
        uint8_t r = (uint8_t)((start.r * (255 - t) + end.r * t) / 255);
        uint8_t g = (uint8_t)((start.g * (255 - t) + end.g * t) / 255);
        uint8_t b = (uint8_t)((start.b * (255 - t) + end.b * t) / 255);
        
        uint32_t pixel = (0xFF << 24) | (r << 16) | (g << 8) | b;
        
        for (int32_t y = y0; y < y1; y++) {
            g_accel.framebuffer[y * g_accel.pitch + x] = pixel;
        }
    }
}
