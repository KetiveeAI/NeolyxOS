/*
 * NeolyxOS - NXRender Bridge Implementation
 * 
 * Initializes NXRender and provides wrappers for desktop shell.
 * This allows gradual migration from pixel-based to NXRender APIs.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxrender_bridge.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ Static State ============ */

static nx_context_t *g_ctx = NULL;
static nx_font_t *g_font_default = NULL;
static nx_font_t *g_font_small = NULL;
static nx_font_t *g_font_large = NULL;
static int g_initialized = 0;

/* ============ Initialization ============ */

int nxr_bridge_init(void *framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
    if (g_initialized) return 0;
    
    nx_debug_print("[NXR] Initializing NXRender bridge...\n");
    
    /* Initialize NXRender graphics context */
    g_ctx = nxgfx_init(framebuffer, width, height, pitch);
    if (!g_ctx) {
        nx_debug_print("[NXR] ERROR: Failed to create graphics context\n");
        return -1;
    }
    
    /* Load default fonts at different sizes */
    g_font_default = nxgfx_font_default(g_ctx, 14);
    g_font_small = nxgfx_font_default(g_ctx, 11);
    g_font_large = nxgfx_font_default(g_ctx, 18);
    
    if (!g_font_default) {
        nx_debug_print("[NXR] WARNING: Using fallback font\n");
    }
    
    g_initialized = 1;
    nx_debug_print("[NXR] NXRender bridge initialized!\n");
    
    return 0;
}

void nxr_bridge_shutdown(void) {
    if (!g_initialized) return;
    
    if (g_font_large) nxgfx_font_destroy(g_font_large);
    if (g_font_small) nxgfx_font_destroy(g_font_small);
    if (g_font_default) nxgfx_font_destroy(g_font_default);
    if (g_ctx) nxgfx_destroy(g_ctx);
    
    g_ctx = NULL;
    g_initialized = 0;
}

nx_context_t* nxr_get_context(void) {
    return g_ctx;
}

nx_font_t* nxr_get_font_default(void) {
    return g_font_default;
}

nx_font_t* nxr_get_font_small(void) {
    return g_font_small;
}

nx_font_t* nxr_get_font_large(void) {
    return g_font_large;
}

/* ============ Drawing Functions ============ */

void nxr_draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    if (!g_ctx || !g_font_default) return;
    nxgfx_draw_text(g_ctx, text, (nx_point_t){x, y}, g_font_default, nxr_argb_to_color(color));
}

void nxr_draw_text_large(int32_t x, int32_t y, const char *text, uint32_t color) {
    if (!g_ctx || !g_font_large) return;
    nxgfx_draw_text(g_ctx, text, (nx_point_t){x, y}, g_font_large, nxr_argb_to_color(color));
}

void nxr_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_fill_rect(g_ctx, nx_rect(x, y, w, h), nxr_argb_to_color(color));
}

void nxr_fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_fill_rounded_rect(g_ctx, nx_rect(x, y, w, h), nxr_argb_to_color(color), radius);
}

void nxr_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_fill_circle(g_ctx, (nx_point_t){cx, cy}, radius, nxr_argb_to_color(color));
}

void nxr_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_draw_line(g_ctx, (nx_point_t){x1, y1}, (nx_point_t){x2, y2}, nxr_argb_to_color(color), 1);
}

void nxr_fill_gradient_h(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2) {
    if (!g_ctx) return;
    nxgfx_fill_gradient(g_ctx, nx_rect(x, y, w, h), 
                        nxr_argb_to_color(c1), nxr_argb_to_color(c2), NX_GRADIENT_H);
}

void nxr_fill_gradient_v(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2) {
    if (!g_ctx) return;
    nxgfx_fill_gradient(g_ctx, nx_rect(x, y, w, h), 
                        nxr_argb_to_color(c1), nxr_argb_to_color(c2), NX_GRADIENT_V);
}

void nxr_draw_image(const void *rgba_data, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    /* TODO: Implement when nxgfx_image_* functions are available */
    (void)rgba_data; (void)x; (void)y; (void)w; (void)h;
}

/* ============ Frame Control ============ */

void nxr_begin_frame(void) {
    /* Reserved for future use (clear damage tracking, etc) */
}

void nxr_end_frame(void) {
    if (!g_ctx) return;
    nxgfx_present(g_ctx);
}

void nxr_clear(uint32_t color) {
    if (!g_ctx) return;
    nxgfx_clear(g_ctx, nxr_argb_to_color(color));
}
