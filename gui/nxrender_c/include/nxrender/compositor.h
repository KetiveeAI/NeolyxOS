/*
 * NeolyxOS - NXRender Compositor
 * Surface and layer management with damage tracking
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_COMPOSITOR_H
#define NXRENDER_COMPOSITOR_H

#include "nxgfx/nxgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_surface nx_surface_t;
typedef struct nx_layer nx_layer_t;
typedef struct nx_compositor nx_compositor_t;

/* Surface - a drawing target */
struct nx_surface {
    uint32_t* buffer;
    uint32_t width;
    uint32_t height;
    nx_rect_t dirty;
    bool needs_redraw;
};

/* Layer - a positioned surface with z-order */
struct nx_layer {
    nx_surface_t* surface;
    nx_point_t position;
    int32_t z_order;
    float opacity;
    bool visible;
    nx_layer_t* next;
};

/* Compositor - manages surfaces and composites them */
struct nx_compositor {
    nx_context_t* ctx;
    nx_layer_t* layers;
    nx_rect_t damage;
    bool has_damage;
};

/* Create compositor */
nx_compositor_t* nx_compositor_create(nx_context_t* ctx);
void nx_compositor_destroy(nx_compositor_t* comp);

/* Surface management */
nx_surface_t* nx_surface_create(uint32_t width, uint32_t height);
void nx_surface_destroy(nx_surface_t* surface);
void nx_surface_clear(nx_surface_t* surface, nx_color_t color);
void nx_surface_mark_dirty(nx_surface_t* surface, nx_rect_t rect);

/* Layer management */
nx_layer_t* nx_layer_create(nx_surface_t* surface, int32_t z_order);
void nx_layer_destroy(nx_layer_t* layer);
void nx_compositor_add_layer(nx_compositor_t* comp, nx_layer_t* layer);
void nx_compositor_remove_layer(nx_compositor_t* comp, nx_layer_t* layer);

/* Compositing */
void nx_compositor_damage(nx_compositor_t* comp, nx_rect_t rect);
void nx_compositor_composite(nx_compositor_t* comp);

#ifdef __cplusplus
}
#endif
#endif
