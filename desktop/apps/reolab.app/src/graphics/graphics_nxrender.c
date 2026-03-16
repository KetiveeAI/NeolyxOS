/*
 * Reolab - Graphics Backend (NXRender Implementation)
 * NeolyxOS Application
 * 
 * Native NXRender graphics - no SDL dependency.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NXRender includes - use path from include dir */
#include "nxgfx/nxgfx.h"
#include "nxrender/window.h"
#include "nxrender/application.h"

struct RLGraphics {
    nx_context_t* ctx;
    nx_window_t* window;
    nx_font_t* font;
    nx_font_t* font_small;
    nx_font_t* font_large;
    int width, height;
    bool quit;
    float mouse_x, mouse_y;
    bool mouse_down[3];
    char text_input[256];
};

RLGraphics* rl_graphics_create(const char* title, int width, int height) {
    RLGraphics* gfx = (RLGraphics*)calloc(1, sizeof(RLGraphics));
    if (!gfx) return NULL;
    
    gfx->width = width;
    gfx->height = height;
    gfx->quit = false;
    
    /* Initialize NXRender context */
    /* For native NeolyxOS, fb comes from kernel framebuffer */
    /* For now, create with NULL - will use syscall for framebuffer */
    gfx->ctx = nxgfx_init(NULL, width, height, width * 4);
    
    if (!gfx->ctx) {
        fprintf(stderr, "[Graphics] Failed to create NXRender context\n");
        free(gfx);
        return NULL;
    }
    
    /* Create window */
    gfx->window = nx_window_create(title, nx_rect(0, 0, width, height), 0);
    
    /* Load fonts */
    gfx->font = nxgfx_font_default(gfx->ctx, 14);
    gfx->font_small = nxgfx_font_default(gfx->ctx, 11);
    gfx->font_large = nxgfx_font_default(gfx->ctx, 18);
    
    printf("[Graphics] NXRender initialized %dx%d\n", width, height);
    return gfx;
}

void rl_graphics_destroy(RLGraphics* gfx) {
    if (!gfx) return;
    
    if (gfx->font) nxgfx_font_destroy(gfx->font);
    if (gfx->font_small) nxgfx_font_destroy(gfx->font_small);
    if (gfx->font_large) nxgfx_font_destroy(gfx->font_large);
    if (gfx->window) nx_window_destroy(gfx->window);
    if (gfx->ctx) nxgfx_destroy(gfx->ctx);
    
    free(gfx);
    printf("[Graphics] NXRender destroyed\n");
}

void rl_graphics_begin_frame(RLGraphics* gfx) {
    if (!gfx) return;
    gfx->text_input[0] = '\0';
}

void rl_graphics_end_frame(RLGraphics* gfx) {
    if (!gfx || !gfx->ctx) return;
    nxgfx_present(gfx->ctx);
}

bool rl_graphics_poll_events(RLGraphics* gfx) {
    if (!gfx) return false;
    
    /* For NeolyxOS native, events come from window manager */
    /* Simplified for now - just check quit flag */
    
    /* TODO: Integrate with nx_wm_handle_event when running in desktop */
    
    return !gfx->quit;
}

bool rl_graphics_should_quit(RLGraphics* gfx) {
    return gfx ? gfx->quit : true;
}

/* Convert RLColor to nx_color_t */
static inline nx_color_t to_nx_color(RLColor c) {
    return (nx_color_t){ c.r, c.g, c.b, c.a };
}

void rl_graphics_clear(RLGraphics* gfx, RLColor color) {
    if (!gfx || !gfx->ctx) return;
    nxgfx_clear(gfx->ctx, to_nx_color(color));
}

void rl_graphics_fill_rect(RLGraphics* gfx, RLRect rect, RLColor color) {
    if (!gfx || !gfx->ctx) return;
    nxgfx_fill_rect(gfx->ctx, 
        nx_rect((int)rect.x, (int)rect.y, (uint32_t)rect.w, (uint32_t)rect.h),
        to_nx_color(color));
}

void rl_graphics_fill_rounded_rect(RLGraphics* gfx, RLRect rect, RLColor color, float radius) {
    if (!gfx || !gfx->ctx) return;
    nxgfx_fill_rounded_rect(gfx->ctx,
        nx_rect((int)rect.x, (int)rect.y, (uint32_t)rect.w, (uint32_t)rect.h),
        to_nx_color(color), (uint32_t)radius);
}

void rl_graphics_draw_line(RLGraphics* gfx, float x1, float y1, float x2, float y2, RLColor color) {
    if (!gfx || !gfx->ctx) return;
    nxgfx_draw_line(gfx->ctx,
        (nx_point_t){(int)x1, (int)y1},
        (nx_point_t){(int)x2, (int)y2},
        to_nx_color(color), 1);
}

void rl_graphics_draw_text(RLGraphics* gfx, const char* text, float x, float y, float size, RLColor color) {
    if (!gfx || !gfx->ctx || !text || !text[0]) return;
    
    nx_font_t* font = gfx->font;
    if (size >= 16) font = gfx->font_large;
    else if (size <= 12) font = gfx->font_small;
    
    if (!font) return;
    
    nxgfx_draw_text(gfx->ctx, text, (nx_point_t){(int)x, (int)y}, font, to_nx_color(color));
}

void rl_graphics_measure_text(RLGraphics* gfx, const char* text, float size, float* width, float* height) {
    if (!gfx || !gfx->ctx || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    nx_font_t* font = gfx->font;
    if (size >= 16) font = gfx->font_large;
    else if (size <= 12) font = gfx->font_small;
    
    if (!font) {
        if (width) *width = strlen(text) * size * 0.6f;
        if (height) *height = size;
        return;
    }
    
    nx_size_t sz = nxgfx_measure_text(gfx->ctx, text, font);
    if (width) *width = (float)sz.width;
    if (height) *height = (float)sz.height;
}

void rl_graphics_get_mouse(RLGraphics* gfx, float* x, float* y) {
    if (!gfx) return;
    if (x) *x = gfx->mouse_x;
    if (y) *y = gfx->mouse_y;
}

bool rl_graphics_is_mouse_down(RLGraphics* gfx, int button) {
    if (!gfx || button < 0 || button > 2) return false;
    return gfx->mouse_down[button];
}

bool rl_graphics_is_key_down(RLGraphics* gfx, int key) {
    (void)gfx;
    (void)key;
    /* TODO: Check NX keyboard state */
    return false;
}

const char* rl_graphics_get_text_input(RLGraphics* gfx) {
    return gfx ? gfx->text_input : "";
}
