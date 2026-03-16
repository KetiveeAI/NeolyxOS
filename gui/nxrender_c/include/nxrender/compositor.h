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

/* 2D Transform for layers */
typedef struct {
    float translate_x, translate_y;
    float scale_x, scale_y;
    float rotation;             /* Radians */
    float anchor_x, anchor_y;   /* 0-1 normalized (0.5,0.5 = center) */
} nx_transform_t;

/* Identity transform */
#define NX_TRANSFORM_IDENTITY ((nx_transform_t){0, 0, 1.0f, 1.0f, 0, 0.5f, 0.5f})

/* Surface - a drawing target */
struct nx_surface {
    uint32_t* buffer;
    uint32_t width;
    uint32_t height;
    nx_rect_t dirty;
    bool needs_redraw;
};

/* Layer blend modes - forward declared if already defined in nxgfx_effects.h */
#ifndef NX_BLEND_MODE_DEFINED
#define NX_BLEND_MODE_DEFINED
typedef enum {
    NX_BLEND_NORMAL,        /* Standard alpha blending */
    NX_BLEND_MULTIPLY,      /* Multiply blend */
    NX_BLEND_SCREEN,        /* Screen blend */
    NX_BLEND_OVERLAY,       /* Overlay blend */
    NX_BLEND_ADD,           /* Additive blend */
    NX_BLEND_SUBTRACT       /* Subtractive blend */
} nx_blend_mode_t;
#endif

/* Layer - a positioned surface with z-order and transform */
struct nx_layer {
    nx_surface_t* surface;
    nx_point_t position;
    int32_t z_order;
    float opacity;
    bool visible;
    nx_transform_t transform;
    nx_blend_mode_t blend_mode;
    /* Clipping */
    nx_rect_t clip_rect;
    bool clip_enabled;
    nx_layer_t* next;
};

/* Damage Tracker - tracks regions needing redraw */
#define NX_MAX_DAMAGE_RECTS 32
typedef struct {
    nx_rect_t rects[NX_MAX_DAMAGE_RECTS];
    uint32_t count;
    bool full_damage;   /* Entire screen needs redraw */
} nx_damage_tracker_t;

/* Frame statistics - forward declared if already defined in device.h */
#ifndef NX_FRAME_STATS_DEFINED
#define NX_FRAME_STATS_DEFINED
typedef struct {
    uint64_t frame_number;
    uint64_t frame_time_us;
    uint64_t composite_time_us;
    uint32_t layers_rendered;
    uint32_t pixels_drawn;
    bool vsync_enabled;
    float fps;
} nx_frame_stats_t;
#endif

/* Compositor - manages surfaces and composites them */
struct nx_compositor {
    nx_context_t* ctx;
    nx_layer_t* layers;
    nx_damage_tracker_t damage_tracker;
    /* Global clip stack */
    nx_rect_t clip_stack[16];
    uint32_t clip_depth;
    /* VSync and timing */
    bool vsync_enabled;
    uint32_t target_fps;
    uint64_t frame_start_time;
    uint64_t last_frame_time;
    uint64_t frame_number;
    /* Frame stats */
    nx_frame_stats_t stats;
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

/* Layer transforms */
void nx_layer_set_position(nx_layer_t* layer, float x, float y);
void nx_layer_set_scale(nx_layer_t* layer, float sx, float sy);
void nx_layer_set_rotation(nx_layer_t* layer, float radians);
void nx_layer_set_anchor(nx_layer_t* layer, float ax, float ay);
void nx_layer_set_transform(nx_layer_t* layer, nx_transform_t transform);
nx_transform_t nx_layer_get_transform(nx_layer_t* layer);

/* Layer clipping */
void nx_layer_set_clip(nx_layer_t* layer, nx_rect_t clip);
void nx_layer_clear_clip(nx_layer_t* layer);
bool nx_layer_has_clip(nx_layer_t* layer);
nx_rect_t nx_layer_get_clip(nx_layer_t* layer);

/* Compositor clip stack */
void nx_compositor_push_clip(nx_compositor_t* comp, nx_rect_t clip);
void nx_compositor_pop_clip(nx_compositor_t* comp);
nx_rect_t nx_compositor_current_clip(nx_compositor_t* comp);

/* Damage tracking */
void nx_damage_add(nx_compositor_t* comp, nx_rect_t rect);
void nx_damage_add_full(nx_compositor_t* comp);
void nx_damage_clear(nx_compositor_t* comp);
bool nx_damage_is_empty(nx_compositor_t* comp);
uint32_t nx_damage_count(nx_compositor_t* comp);
nx_rect_t nx_damage_get(nx_compositor_t* comp, uint32_t index);
nx_rect_t nx_damage_bounds(nx_compositor_t* comp);

/* VSync and timing */
void nx_compositor_set_vsync(nx_compositor_t* comp, bool enabled);
void nx_compositor_set_target_fps(nx_compositor_t* comp, uint32_t fps);
void nx_compositor_begin_frame(nx_compositor_t* comp);
void nx_compositor_end_frame(nx_compositor_t* comp);
void nx_compositor_wait_vsync(nx_compositor_t* comp);
nx_frame_stats_t nx_compositor_get_stats(nx_compositor_t* comp);

/* Layer blend mode */
void nx_layer_set_blend_mode(nx_layer_t* layer, nx_blend_mode_t mode);
nx_blend_mode_t nx_layer_get_blend_mode(nx_layer_t* layer);

/* Compositing */
void nx_compositor_composite(nx_compositor_t* comp);
void nx_compositor_composite_damage_only(nx_compositor_t* comp);

#ifdef __cplusplus
}
#endif
#endif
