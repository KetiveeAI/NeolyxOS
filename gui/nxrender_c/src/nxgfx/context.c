/*
 * NeolyxOS - NXGFX Graphics Context
 * Framebuffer-based software renderer
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx.h"
#include <stdlib.h>
#include <string.h>

struct nx_context {
    uint32_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    nx_rect_t clip;
    bool has_clip;
};

/* Pack color to 32-bit ARGB */
static inline uint32_t color_to_argb(nx_color_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

/* Alpha blend two colors */
static inline nx_color_t blend_colors(nx_color_t src, nx_color_t dst) {
    if (src.a == 255) return src;
    if (src.a == 0) return dst;
    uint32_t a = src.a;
    uint32_t inv_a = 255 - a;
    return (nx_color_t){
        .r = (uint8_t)((src.r * a + dst.r * inv_a) / 255),
        .g = (uint8_t)((src.g * a + dst.g * inv_a) / 255),
        .b = (uint8_t)((src.b * a + dst.b * inv_a) / 255),
        .a = 255
    };
}

nx_context_t* nxgfx_init(void* fb, uint32_t w, uint32_t h, uint32_t pitch) {
    if (!fb || w == 0 || h == 0) return NULL;
    nx_context_t* ctx = (nx_context_t*)malloc(sizeof(nx_context_t));
    if (!ctx) return NULL;
    ctx->framebuffer = (uint32_t*)fb;
    ctx->width = w;
    ctx->height = h;
    ctx->pitch = pitch;
    ctx->has_clip = false;
    ctx->clip = nx_rect(0, 0, w, h);
    return ctx;
}

void nxgfx_destroy(nx_context_t* ctx) {
    if (ctx) free(ctx);
}

uint32_t nxgfx_width(const nx_context_t* ctx) { return ctx ? ctx->width : 0; }
uint32_t nxgfx_height(const nx_context_t* ctx) { return ctx ? ctx->height : 0; }

void nxgfx_put_pixel(nx_context_t* ctx, int32_t x, int32_t y, nx_color_t color) {
    if (!ctx || x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height) return;
    if (ctx->has_clip && !nx_rect_contains(ctx->clip, (nx_point_t){x, y})) return;
    uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
    row[x] = color_to_argb(color);
}

void nxgfx_clear(nx_context_t* ctx, nx_color_t color) {
    if (!ctx) return;
    uint32_t argb = color_to_argb(color);
    for (uint32_t y = 0; y < ctx->height; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
        for (uint32_t x = 0; x < ctx->width; x++) row[x] = argb;
    }
}

void nxgfx_fill_rect(nx_context_t* ctx, nx_rect_t rect, nx_color_t color) {
    if (!ctx) return;
    int32_t x0 = rect.x < 0 ? 0 : rect.x;
    int32_t y0 = rect.y < 0 ? 0 : rect.y;
    int32_t x1 = (int32_t)(rect.x + rect.width);
    int32_t y1 = (int32_t)(rect.y + rect.height);
    if (x1 > (int32_t)ctx->width) x1 = ctx->width;
    if (y1 > (int32_t)ctx->height) y1 = ctx->height;
    if (ctx->has_clip) {
        if (x0 < ctx->clip.x) x0 = ctx->clip.x;
        if (y0 < ctx->clip.y) y0 = ctx->clip.y;
        if (x1 > (int32_t)(ctx->clip.x + ctx->clip.width)) x1 = ctx->clip.x + ctx->clip.width;
        if (y1 > (int32_t)(ctx->clip.y + ctx->clip.height)) y1 = ctx->clip.y + ctx->clip.height;
    }
    uint32_t argb = color_to_argb(color);
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)ctx->framebuffer + y * ctx->pitch);
        for (int32_t x = x0; x < x1; x++) row[x] = argb;
    }
}

void nxgfx_draw_rect(nx_context_t* ctx, nx_rect_t r, nx_color_t c, uint32_t s) {
    if (!ctx || s == 0) return;
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y, r.width, s), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y + r.height - s, r.width, s), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y, s, r.height), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x + r.width - s, r.y, s, r.height), c);
}

void nxgfx_fill_circle(nx_context_t* ctx, nx_point_t center, uint32_t radius, nx_color_t color) {
    if (!ctx || radius == 0) return;
    int32_t r = (int32_t)radius;
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                nxgfx_put_pixel(ctx, center.x + dx, center.y + dy, color);
            }
        }
    }
}

void nxgfx_draw_line(nx_context_t* ctx, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t w) {
    if (!ctx) return;
    int32_t dx = p2.x > p1.x ? p2.x - p1.x : p1.x - p2.x;
    int32_t dy = p2.y > p1.y ? p2.y - p1.y : p1.y - p2.y;
    int32_t sx = p1.x < p2.x ? 1 : -1;
    int32_t sy = p1.y < p2.y ? 1 : -1;
    int32_t err = dx - dy;
    int32_t x = p1.x, y = p1.y;
    while (1) {
        nxgfx_put_pixel(ctx, x, y, color);
        if (x == p2.x && y == p2.y) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
    (void)w;
}

void nxgfx_fill_rounded_rect(nx_context_t* ctx, nx_rect_t r, nx_color_t c, uint32_t rad) {
    if (!ctx) return;
    if (rad == 0) { nxgfx_fill_rect(ctx, r, c); return; }
    nxgfx_fill_rect(ctx, nx_rect(r.x + rad, r.y, r.width - 2*rad, r.height), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x, r.y + rad, rad, r.height - 2*rad), c);
    nxgfx_fill_rect(ctx, nx_rect(r.x + r.width - rad, r.y + rad, rad, r.height - 2*rad), c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + rad, r.y + rad}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + r.width - rad - 1, r.y + rad}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + rad, r.y + r.height - rad - 1}, rad, c);
    nxgfx_fill_circle(ctx, (nx_point_t){r.x + r.width - rad - 1, r.y + r.height - rad - 1}, rad, c);
}

void nxgfx_fill_gradient(nx_context_t* ctx, nx_rect_t r, nx_color_t c1, nx_color_t c2, nx_gradient_dir_t dir) {
    if (!ctx || r.width == 0 || r.height == 0) return;
    for (uint32_t y = 0; y < r.height; y++) {
        for (uint32_t x = 0; x < r.width; x++) {
            float t = (dir == NX_GRADIENT_H) ? (float)x / r.width :
                      (dir == NX_GRADIENT_V) ? (float)y / r.height :
                      ((float)x + (float)y) / (r.width + r.height);
            nx_color_t c = {
                .r = (uint8_t)(c1.r + t * (c2.r - c1.r)),
                .g = (uint8_t)(c1.g + t * (c2.g - c1.g)),
                .b = (uint8_t)(c1.b + t * (c2.b - c1.b)),
                .a = 255
            };
            nxgfx_put_pixel(ctx, r.x + x, r.y + y, c);
        }
    }
}

void nxgfx_set_clip(nx_context_t* ctx, nx_rect_t clip) {
    if (!ctx) return;
    ctx->clip = clip;
    ctx->has_clip = true;
}

void nxgfx_clear_clip(nx_context_t* ctx) {
    if (!ctx) return;
    ctx->has_clip = false;
}

void nxgfx_present(nx_context_t* ctx) { (void)ctx; }
