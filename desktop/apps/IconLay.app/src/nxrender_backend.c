/*
 * NXRender Backend for IconLay
 * 
 * GPU-accelerated rendering using NXRender/NXSVG.
 * Replaces SDL2 software rasterizer for 60 FPS performance.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxrender_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static nx_context_t* g_ctx = NULL;
static nxsvg_image_t** g_layer_svgs = NULL;
static uint32_t g_layer_svg_count = 0;
static uint32_t g_layer_svg_capacity = 0;

int nxrender_backend_init(void* framebuffer, uint32_t width, uint32_t height, uint32_t pitch) {
    g_ctx = nxgfx_init(framebuffer, width, height, pitch);
    if (!g_ctx) return -1;
    
    g_layer_svg_capacity = 16;
    g_layer_svgs = calloc(g_layer_svg_capacity, sizeof(nxsvg_image_t*));
    g_layer_svg_count = 0;
    
    return 0;
}

void nxrender_backend_shutdown(void) {
    for (uint32_t i = 0; i < g_layer_svg_count; i++) {
        if (g_layer_svgs[i]) nxsvg_free(g_layer_svgs[i]);
    }
    free(g_layer_svgs);
    g_layer_svgs = NULL;
    g_layer_svg_count = 0;
    
    if (g_ctx) {
        nxgfx_destroy(g_ctx);
        g_ctx = NULL;
    }
}

nx_context_t* nxrender_backend_get_context(void) {
    return g_ctx;
}

nxsvg_image_t* nxrender_backend_load_svg(const char* path) {
    nxsvg_image_t* svg = nxsvg_load(path);
    if (!svg) return NULL;
    
    if (g_layer_svg_count >= g_layer_svg_capacity) {
        g_layer_svg_capacity *= 2;
        g_layer_svgs = realloc(g_layer_svgs, g_layer_svg_capacity * sizeof(nxsvg_image_t*));
    }
    g_layer_svgs[g_layer_svg_count++] = svg;
    
    return svg;
}

nxsvg_image_t* nxrender_backend_parse_svg(const char* data, size_t len) {
    nxsvg_image_t* svg = nxsvg_parse(data, len);
    if (!svg) return NULL;
    
    if (g_layer_svg_count >= g_layer_svg_capacity) {
        g_layer_svg_capacity *= 2;
        g_layer_svgs = realloc(g_layer_svgs, g_layer_svg_capacity * sizeof(nxsvg_image_t*));
    }
    g_layer_svgs[g_layer_svg_count++] = svg;
    
    return svg;
}

void nxrender_backend_render_svg(nxsvg_image_t* svg, float x, float y, float scale, uint32_t color_override) {
    if (!g_ctx || !svg) return;
    
    /* Override fill color if specified */
    if (color_override != 0) {
        for (uint32_t i = 0; i < svg->path_count; i++) {
            svg->paths[i].fill = (nx_color_t){
                (color_override >> 24) & 0xFF,
                (color_override >> 16) & 0xFF,
                (color_override >> 8) & 0xFF,
                color_override & 0xFF
            };
        }
    }
    
    /* Center the SVG at (x,y) by computing top-left position */
    float half_w = svg->width * scale * 0.5f;
    float half_h = svg->height * scale * 0.5f;
    int32_t render_x = (int32_t)(x - half_w);
    int32_t render_y = (int32_t)(y - half_h);
    
    nxsvg_render(g_ctx, svg, (nx_point_t){render_x, render_y}, scale);
}

void nxrender_backend_clear(uint32_t color) {
    if (!g_ctx) return;
    nxgfx_clear(g_ctx, (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    });
}

void nxrender_backend_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_fill_rect(g_ctx, nx_rect(x, y, w, h), (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    });
}

void nxrender_backend_fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, 
                                         uint32_t color, uint32_t radius) {
    if (!g_ctx) return;
    nxgfx_fill_rounded_rect(g_ctx, nx_rect(x, y, w, h), (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    }, radius);
}

void nxrender_backend_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t color) {
    if (!g_ctx) return;
    nxgfx_fill_circle(g_ctx, (nx_point_t){cx, cy}, radius, (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    });
}

void nxrender_backend_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, 
                                 uint32_t color, uint32_t thickness) {
    if (!g_ctx) return;
    nxgfx_draw_line(g_ctx, (nx_point_t){x1, y1}, (nx_point_t){x2, y2}, (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    }, thickness);
}

void nxrender_backend_fill_gradient(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                     uint32_t color1, uint32_t color2, int horizontal) {
    if (!g_ctx) return;
    nxgfx_fill_gradient(g_ctx, nx_rect(x, y, w, h), 
        (nx_color_t){(color1 >> 24) & 0xFF, (color1 >> 16) & 0xFF, (color1 >> 8) & 0xFF, color1 & 0xFF},
        (nx_color_t){(color2 >> 24) & 0xFF, (color2 >> 16) & 0xFF, (color2 >> 8) & 0xFF, color2 & 0xFF},
        horizontal ? NX_GRADIENT_H : NX_GRADIENT_V
    );
}

void nxrender_backend_fill_card(int32_t x, int32_t y, uint32_t w, uint32_t h,
                                 uint32_t color, uint32_t radius, int elevated) {
    if (!g_ctx) return;
    nxgfx_fill_card(g_ctx, nx_rect(x, y, w, h), (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    }, radius, elevated);
}

void nxrender_backend_draw_text(const char* text, int32_t x, int32_t y, 
                                 uint32_t color, uint32_t size) {
    if (!g_ctx || !text) return;
    
    nx_font_t* font = nxgfx_font_default(g_ctx, size);
    if (!font) return;
    
    nxgfx_draw_text(g_ctx, text, (nx_point_t){x, y}, font, (nx_color_t){
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    });
}

void nxrender_backend_set_clip(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g_ctx) return;
    nxgfx_set_clip(g_ctx, nx_rect(x, y, w, h));
}

void nxrender_backend_clear_clip(void) {
    if (!g_ctx) return;
    nxgfx_clear_clip(g_ctx);
}

void nxrender_backend_present(void) {
    if (!g_ctx) return;
    nxgfx_present(g_ctx);
}

/* Convert NXI path commands to NXSVG for rendering */
nxsvg_image_t* nxrender_backend_paths_to_svg(const nxi_path_cmd_t* paths, uint32_t count,
                                               float width, float height, uint32_t fill_color) {
    if (!paths || count == 0) return NULL;
    
    /* Allocate SVG image structure */
    nxsvg_image_t* svg = calloc(1, sizeof(nxsvg_image_t));
    if (!svg) return NULL;
    
    svg->width = width;
    svg->height = height;
    svg->paths = calloc(1, sizeof(nxsvg_path_t));
    svg->path_count = 1;
    
    nxsvg_path_t* path = &svg->paths[0];
    path->capacity = count + 8;
    path->commands = calloc(path->capacity, sizeof(nxsvg_cmd_t));
    path->count = 0;
    path->fill = (nx_color_t){
        (fill_color >> 24) & 0xFF,
        (fill_color >> 16) & 0xFF,
        (fill_color >> 8) & 0xFF,
        fill_color & 0xFF
    };
    path->width = width;
    path->height = height;
    
    /* Convert NXI commands to NXSVG commands */
    for (uint32_t i = 0; i < count; i++) {
        const nxi_path_cmd_t* cmd = &paths[i];
        nxsvg_cmd_t* out = &path->commands[path->count++];
        
        switch (cmd->type) {
            case NXI_PATH_MOVE:
                out->type = NXSVG_CMD_MOVE;
                out->x = cmd->x;
                out->y = cmd->y;
                break;
            case NXI_PATH_LINE:
                out->type = NXSVG_CMD_LINE;
                out->x = cmd->x;
                out->y = cmd->y;
                break;
            case NXI_PATH_CUBIC:
                out->type = NXSVG_CMD_CUBIC;
                out->x = cmd->x;
                out->y = cmd->y;
                out->x1 = cmd->x1;
                out->y1 = cmd->y1;
                out->x2 = cmd->x2;
                out->y2 = cmd->y2;
                break;
            case NXI_PATH_QUAD:
                out->type = NXSVG_CMD_QUAD;
                out->x = cmd->x;
                out->y = cmd->y;
                out->x1 = cmd->x1;
                out->y1 = cmd->y1;
                break;
            case NXI_PATH_CLOSE:
                out->type = NXSVG_CMD_CLOSE;
                break;
        }
    }
    
    return svg;
}
