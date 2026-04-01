/*
 * NeolyxOS - NXRender Bridge Implementation
 * 
 * Stub implementation for desktop shell compatibility.
 * Returns success but doesn't actually use NXRender to avoid crashes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxsyscall.h"
#include <stddef.h>
#include <stdint.h>

/* Stub types to avoid including nxgfx.h */
typedef struct { int dummy; } nx_context_t;
typedef struct { int dummy; } nx_font_t;

/* ============ Static State ============ */

static int g_initialized = 0;

/* ============ Initialization ============ */

int nxr_bridge_init(void *framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
    (void)framebuffer; (void)width; (void)height; (void)pitch;
    
    if (g_initialized) return 0;
    
    nx_debug_print("[NXR] Using stub NXRender bridge (no external deps)\n");
    
    g_initialized = 1;
    
    return 0;  /* Success - desktop will use fallback pixel rendering */
}

void nxr_bridge_shutdown(void) {
    g_initialized = 0;
}

nx_context_t* nxr_get_context(void) {
    return NULL;
}

nx_font_t* nxr_get_font_default(void) {
    return NULL;
}

nx_font_t* nxr_get_font_small(void) {
    return NULL;
}

nx_font_t* nxr_get_font_large(void) {
    return NULL;
}

/* ============ Drawing Functions (Stubs) ============ */

void nxr_draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    (void)x; (void)y; (void)text; (void)color;
}

void nxr_draw_text_large(int32_t x, int32_t y, const char *text, uint32_t color) {
    (void)x; (void)y; (void)text; (void)color;
}

void nxr_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}

void nxr_fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color;
}

void nxr_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t color) {
    (void)cx; (void)cy; (void)radius; (void)color;
}

void nxr_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

void nxr_fill_gradient_h(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2) {
    (void)x; (void)y; (void)w; (void)h; (void)c1; (void)c2;
}

void nxr_fill_gradient_v(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t c1, uint32_t c2) {
    (void)x; (void)y; (void)w; (void)h; (void)c1; (void)c2;
}

void nxr_draw_image(const void *rgba_data, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    (void)rgba_data; (void)x; (void)y; (void)w; (void)h;
}

/* ============ Frame Control ============ */

void nxr_begin_frame(void) {
    /* No-op */
}

void nxr_end_frame(void) {
    /* No-op */
}

void nxr_clear(uint32_t color) {
    (void)color;
}

