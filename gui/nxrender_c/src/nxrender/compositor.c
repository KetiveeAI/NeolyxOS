/*
 * NeolyxOS - NXRender Compositor Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE
#include "nxrender/compositor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

nx_surface_t* nx_surface_create(uint32_t width, uint32_t height) {
    nx_surface_t* s = (nx_surface_t*)malloc(sizeof(nx_surface_t));
    if (!s) return NULL;
    s->buffer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    if (!s->buffer) { free(s); return NULL; }
    s->width = width;
    s->height = height;
    s->dirty = (nx_rect_t){0, 0, width, height};
    s->needs_redraw = true;
    return s;
}

void nx_surface_destroy(nx_surface_t* surface) {
    if (surface) {
        free(surface->buffer);
        free(surface);
    }
}

void nx_surface_clear(nx_surface_t* surface, nx_color_t color) {
    if (!surface) return;
    uint32_t argb = ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) |
                    ((uint32_t)color.g << 8) | color.b;
    for (uint32_t i = 0; i < surface->width * surface->height; i++) {
        surface->buffer[i] = argb;
    }
    surface->dirty = (nx_rect_t){0, 0, surface->width, surface->height};
}

void nx_surface_mark_dirty(nx_surface_t* surface, nx_rect_t rect) {
    if (!surface) return;
    if (!surface->needs_redraw) {
        surface->dirty = rect;
        surface->needs_redraw = true;
    } else {
        int32_t x0 = surface->dirty.x < rect.x ? surface->dirty.x : rect.x;
        int32_t y0 = surface->dirty.y < rect.y ? surface->dirty.y : rect.y;
        int32_t x1_old = surface->dirty.x + surface->dirty.width;
        int32_t x1_new = rect.x + rect.width;
        int32_t y1_old = surface->dirty.y + surface->dirty.height;
        int32_t y1_new = rect.y + rect.height;
        surface->dirty.x = x0;
        surface->dirty.y = y0;
        surface->dirty.width = (x1_old > x1_new ? x1_old : x1_new) - x0;
        surface->dirty.height = (y1_old > y1_new ? y1_old : y1_new) - y0;
    }
}

nx_layer_t* nx_layer_create(nx_surface_t* surface, int32_t z_order) {
    nx_layer_t* layer = (nx_layer_t*)malloc(sizeof(nx_layer_t));
    if (!layer) return NULL;
    layer->surface = surface;
    layer->position = (nx_point_t){0, 0};
    layer->z_order = z_order;
    layer->opacity = 1.0f;
    layer->visible = true;
    layer->transform = NX_TRANSFORM_IDENTITY;
    layer->blend_mode = NX_BLEND_NORMAL;
    layer->clip_rect = (nx_rect_t){0, 0, 0, 0};
    layer->clip_enabled = false;
    layer->next = NULL;
    return layer;
}

void nx_layer_destroy(nx_layer_t* layer) {
    if (layer) free(layer);
}

nx_compositor_t* nx_compositor_create(nx_context_t* ctx) {
    nx_compositor_t* comp = (nx_compositor_t*)malloc(sizeof(nx_compositor_t));
    if (!comp) return NULL;
    comp->ctx = ctx;
    comp->layers = NULL;
    comp->damage_tracker.count = 0;
    comp->damage_tracker.full_damage = false;
    comp->clip_depth = 0;
    /* VSync and timing */
    comp->vsync_enabled = true;
    comp->target_fps = 60;
    comp->frame_start_time = 0;
    comp->last_frame_time = 0;
    comp->frame_number = 0;
    memset(&comp->stats, 0, sizeof(comp->stats));
    return comp;
}


void nx_compositor_destroy(nx_compositor_t* comp) {
    if (comp) {
        nx_layer_t* l = comp->layers;
        while (l) {
            nx_layer_t* next = l->next;
            nx_layer_destroy(l);
            l = next;
        }
        free(comp);
    }
}

void nx_compositor_add_layer(nx_compositor_t* comp, nx_layer_t* layer) {
    if (!comp || !layer) return;
    if (!comp->layers || layer->z_order < comp->layers->z_order) {
        layer->next = comp->layers;
        comp->layers = layer;
        return;
    }
    nx_layer_t* curr = comp->layers;
    while (curr->next && curr->next->z_order <= layer->z_order) {
        curr = curr->next;
    }
    layer->next = curr->next;
    curr->next = layer;
}

void nx_compositor_remove_layer(nx_compositor_t* comp, nx_layer_t* layer) {
    if (!comp || !layer) return;
    if (comp->layers == layer) {
        comp->layers = layer->next;
        return;
    }
    nx_layer_t* curr = comp->layers;
    while (curr->next && curr->next != layer) curr = curr->next;
    if (curr->next == layer) curr->next = layer->next;
}

/* ============================================================================
 * Damage Tracking Implementation
 * ============================================================================ */

static bool rects_overlap(nx_rect_t a, nx_rect_t b) {
    return !(a.x + (int32_t)a.width <= b.x || b.x + (int32_t)b.width <= a.x ||
             a.y + (int32_t)a.height <= b.y || b.y + (int32_t)b.height <= a.y);
}

static nx_rect_t rect_union(nx_rect_t a, nx_rect_t b) {
    int32_t x1 = a.x < b.x ? a.x : b.x;
    int32_t y1 = a.y < b.y ? a.y : b.y;
    int32_t x2 = (a.x + (int32_t)a.width) > (b.x + (int32_t)b.width) 
               ? (a.x + (int32_t)a.width) : (b.x + (int32_t)b.width);
    int32_t y2 = (a.y + (int32_t)a.height) > (b.y + (int32_t)b.height)
               ? (a.y + (int32_t)a.height) : (b.y + (int32_t)b.height);
    return (nx_rect_t){ x1, y1, (uint32_t)(x2 - x1), (uint32_t)(y2 - y1) };
}

void nx_damage_add(nx_compositor_t* comp, nx_rect_t rect) {
    if (!comp || rect.width == 0 || rect.height == 0) return;
    
    nx_damage_tracker_t* dt = &comp->damage_tracker;
    if (dt->full_damage) return;  /* Already full damage */
    
    /* Try to merge with existing rect */
    for (uint32_t i = 0; i < dt->count; i++) {
        if (rects_overlap(dt->rects[i], rect)) {
            dt->rects[i] = rect_union(dt->rects[i], rect);
            return;
        }
    }
    
    /* Add new rect or merge all if full */
    if (dt->count < NX_MAX_DAMAGE_RECTS) {
        dt->rects[dt->count++] = rect;
    } else {
        /* Too many rects, mark full damage */
        dt->full_damage = true;
    }
}

void nx_damage_add_full(nx_compositor_t* comp) {
    if (!comp) return;
    comp->damage_tracker.full_damage = true;
}

void nx_damage_clear(nx_compositor_t* comp) {
    if (!comp) return;
    comp->damage_tracker.count = 0;
    comp->damage_tracker.full_damage = false;
}

bool nx_damage_is_empty(nx_compositor_t* comp) {
    if (!comp) return true;
    return comp->damage_tracker.count == 0 && !comp->damage_tracker.full_damage;
}

uint32_t nx_damage_count(nx_compositor_t* comp) {
    if (!comp) return 0;
    return comp->damage_tracker.count;
}

nx_rect_t nx_damage_get(nx_compositor_t* comp, uint32_t index) {
    if (!comp || index >= comp->damage_tracker.count) {
        return (nx_rect_t){0, 0, 0, 0};
    }
    return comp->damage_tracker.rects[index];
}

nx_rect_t nx_damage_bounds(nx_compositor_t* comp) {
    if (!comp || nx_damage_is_empty(comp)) {
        return (nx_rect_t){0, 0, 0, 0};
    }
    
    if (comp->damage_tracker.full_damage && comp->ctx) {
        return (nx_rect_t){0, 0, nxgfx_width(comp->ctx), nxgfx_height(comp->ctx)};
    }
    
    nx_rect_t bounds = comp->damage_tracker.rects[0];
    for (uint32_t i = 1; i < comp->damage_tracker.count; i++) {
        bounds = rect_union(bounds, comp->damage_tracker.rects[i]);
    }
    return bounds;
}

void nx_compositor_composite(nx_compositor_t* comp) {
    if (!comp || !comp->ctx) return;
    
    for (nx_layer_t* l = comp->layers; l; l = l->next) {
        if (!l->visible || !l->surface) continue;
        nx_surface_t* s = l->surface;
        nx_transform_t* t = &l->transform;
        
        /* Calculate transform parameters */
        float cos_r = cosf(t->rotation);
        float sin_r = sinf(t->rotation);
        float anchor_px = t->anchor_x * s->width;
        float anchor_py = t->anchor_y * s->height;
        
        for (uint32_t sy = 0; sy < s->height; sy++) {
            for (uint32_t sx = 0; sx < s->width; sx++) {
                uint32_t src = s->buffer[sy * s->width + sx];
                uint8_t a = (src >> 24) & 0xFF;
                if (a == 0) continue;
                a = (uint8_t)(a * l->opacity);
                
                /* Apply transform: translate to anchor, scale, rotate, translate back, then position */
                float px = (float)sx - anchor_px;
                float py = (float)sy - anchor_py;
                
                /* Scale */
                px *= t->scale_x;
                py *= t->scale_y;
                
                /* Rotate */
                float rx = px * cos_r - py * sin_r;
                float ry = px * sin_r + py * cos_r;
                
                /* Translate back and apply layer position */
                int32_t dx = (int32_t)(rx + anchor_px * t->scale_x + l->position.x + t->translate_x);
                int32_t dy = (int32_t)(ry + anchor_py * t->scale_y + l->position.y + t->translate_y);
                
                nx_color_t c = { (src >> 16), (src >> 8), src, a };
                nxgfx_put_pixel(comp->ctx, dx, dy, c);
            }
        }
        s->needs_redraw = false;
    }
    nx_damage_clear(comp);
}

/* Damage-optimized compositing - only redraws damaged regions */
void nx_compositor_composite_damage_only(nx_compositor_t* comp) {
    if (!comp || !comp->ctx) return;
    
    /* Skip if no damage */
    if (nx_damage_is_empty(comp)) return;
    
    /* Get damage bounds and set as clip */
    nx_rect_t bounds = nx_damage_bounds(comp);
    nx_compositor_push_clip(comp, bounds);
    
    /* Composite layers */
    for (nx_layer_t* l = comp->layers; l; l = l->next) {
        if (!l->visible || !l->surface) continue;
        nx_surface_t* s = l->surface;
        nx_transform_t* t = &l->transform;
        
        float cos_r = cosf(t->rotation);
        float sin_r = sinf(t->rotation);
        float anchor_px = t->anchor_x * s->width;
        float anchor_py = t->anchor_y * s->height;
        
        for (uint32_t sy = 0; sy < s->height; sy++) {
            for (uint32_t sx = 0; sx < s->width; sx++) {
                uint32_t src = s->buffer[sy * s->width + sx];
                uint8_t a = (src >> 24) & 0xFF;
                if (a == 0) continue;
                a = (uint8_t)(a * l->opacity);
                
                float px = (float)sx - anchor_px;
                float py = (float)sy - anchor_py;
                px *= t->scale_x;
                py *= t->scale_y;
                float rx = px * cos_r - py * sin_r;
                float ry = px * sin_r + py * cos_r;
                int32_t dx = (int32_t)(rx + anchor_px * t->scale_x + l->position.x + t->translate_x);
                int32_t dy = (int32_t)(ry + anchor_py * t->scale_y + l->position.y + t->translate_y);
                
                /* Skip if outside damage bounds */
                if (dx < bounds.x || dx >= (int32_t)(bounds.x + bounds.width) ||
                    dy < bounds.y || dy >= (int32_t)(bounds.y + bounds.height)) {
                    continue;
                }
                
                nx_color_t c = { (src >> 16), (src >> 8), src, a };
                nxgfx_put_pixel(comp->ctx, dx, dy, c);
            }
        }
        s->needs_redraw = false;
    }
    
    nx_compositor_pop_clip(comp);
    nx_damage_clear(comp);
}

/* Layer transform functions */
void nx_layer_set_position(nx_layer_t* layer, float x, float y) {
    if (!layer) return;
    layer->transform.translate_x = x;
    layer->transform.translate_y = y;
}

void nx_layer_set_scale(nx_layer_t* layer, float sx, float sy) {
    if (!layer) return;
    layer->transform.scale_x = sx;
    layer->transform.scale_y = sy;
}

void nx_layer_set_rotation(nx_layer_t* layer, float radians) {
    if (!layer) return;
    layer->transform.rotation = radians;
}

void nx_layer_set_anchor(nx_layer_t* layer, float ax, float ay) {
    if (!layer) return;
    layer->transform.anchor_x = ax;
    layer->transform.anchor_y = ay;
}

void nx_layer_set_transform(nx_layer_t* layer, nx_transform_t transform) {
    if (!layer) return;
    layer->transform = transform;
}

nx_transform_t nx_layer_get_transform(nx_layer_t* layer) {
    if (!layer) return NX_TRANSFORM_IDENTITY;
    return layer->transform;
}

/* ============================================================================
 * Layer Clipping Functions
 * ============================================================================ */

void nx_layer_set_clip(nx_layer_t* layer, nx_rect_t clip) {
    if (!layer) return;
    layer->clip_rect = clip;
    layer->clip_enabled = true;
}

void nx_layer_clear_clip(nx_layer_t* layer) {
    if (!layer) return;
    layer->clip_enabled = false;
}

bool nx_layer_has_clip(nx_layer_t* layer) {
    return layer ? layer->clip_enabled : false;
}

nx_rect_t nx_layer_get_clip(nx_layer_t* layer) {
    if (!layer) return (nx_rect_t){0, 0, 0, 0};
    return layer->clip_rect;
}

/* ============================================================================
 * Compositor Clip Stack
 * ============================================================================ */

void nx_compositor_push_clip(nx_compositor_t* comp, nx_rect_t clip) {
    if (!comp || comp->clip_depth >= 16) return;
    
    if (comp->clip_depth > 0) {
        /* Intersect with current clip */
        nx_rect_t current = comp->clip_stack[comp->clip_depth - 1];
        int32_t x1 = clip.x > current.x ? clip.x : current.x;
        int32_t y1 = clip.y > current.y ? clip.y : current.y;
        int32_t x2 = (clip.x + (int32_t)clip.width) < (current.x + (int32_t)current.width) 
                    ? (clip.x + (int32_t)clip.width) : (current.x + (int32_t)current.width);
        int32_t y2 = (clip.y + (int32_t)clip.height) < (current.y + (int32_t)current.height)
                    ? (clip.y + (int32_t)clip.height) : (current.y + (int32_t)current.height);
        
        if (x2 > x1 && y2 > y1) {
            clip = (nx_rect_t){ x1, y1, (uint32_t)(x2 - x1), (uint32_t)(y2 - y1) };
        } else {
            clip = (nx_rect_t){ 0, 0, 0, 0 };
        }
    }
    
    comp->clip_stack[comp->clip_depth++] = clip;
    
    /* Apply to context */
    if (comp->ctx) {
        nxgfx_set_clip(comp->ctx, clip);
    }
}

void nx_compositor_pop_clip(nx_compositor_t* comp) {
    if (!comp || comp->clip_depth == 0) return;
    
    comp->clip_depth--;
    
    if (comp->ctx) {
        if (comp->clip_depth > 0) {
            nxgfx_set_clip(comp->ctx, comp->clip_stack[comp->clip_depth - 1]);
        } else {
            nxgfx_clear_clip(comp->ctx);
        }
    }
}

nx_rect_t nx_compositor_current_clip(nx_compositor_t* comp) {
    if (!comp || comp->clip_depth == 0) {
        return (nx_rect_t){ 0, 0, 0xFFFFFFFF, 0xFFFFFFFF };
    }
    return comp->clip_stack[comp->clip_depth - 1];
}

/* ============================================================================
 * VSync and Frame Timing
 * ============================================================================ */

#include <time.h>
#include <unistd.h>

static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

void nx_compositor_set_vsync(nx_compositor_t* comp, bool enabled) {
    if (!comp) return;
    comp->vsync_enabled = enabled;
}

void nx_compositor_set_target_fps(nx_compositor_t* comp, uint32_t fps) {
    if (!comp || fps == 0) return;
    comp->target_fps = fps;
}

void nx_compositor_begin_frame(nx_compositor_t* comp) {
    if (!comp) return;
    comp->frame_start_time = get_time_us();
    comp->frame_number++;
    comp->stats.layers_rendered = 0;
    comp->stats.pixels_drawn = 0;
}

void nx_compositor_end_frame(nx_compositor_t* comp) {
    if (!comp) return;
    
    uint64_t now = get_time_us();
    comp->stats.composite_time_us = now - comp->frame_start_time;
    
    /* VSync - wait for target frame time */
    if (comp->vsync_enabled && comp->target_fps > 0) {
        uint64_t target_frame_time = 1000000 / comp->target_fps;
        uint64_t elapsed = comp->stats.composite_time_us;
        if (elapsed < target_frame_time) {
            usleep((useconds_t)(target_frame_time - elapsed));
        }
    }
    
    now = get_time_us();
    comp->stats.frame_time_us = now - comp->frame_start_time;
    comp->last_frame_time = now;
    comp->stats.frame_number = comp->frame_number;
    comp->stats.vsync_enabled = comp->vsync_enabled;
    
    /* Calculate FPS */
    if (comp->stats.frame_time_us > 0) {
        comp->stats.fps = 1000000.0f / comp->stats.frame_time_us;
    }
}

void nx_compositor_wait_vsync(nx_compositor_t* comp) {
    if (!comp || !comp->vsync_enabled || comp->target_fps == 0) return;
    
    uint64_t target_frame_time = 1000000 / comp->target_fps;
    uint64_t now = get_time_us();
    uint64_t next_frame = comp->last_frame_time + target_frame_time;
    
    if (now < next_frame) {
        usleep((useconds_t)(next_frame - now));
    }
}

nx_frame_stats_t nx_compositor_get_stats(nx_compositor_t* comp) {
    if (!comp) return (nx_frame_stats_t){0};
    return comp->stats;
}

/* ============================================================================
 * Layer Blend Mode
 * ============================================================================ */

void nx_layer_set_blend_mode(nx_layer_t* layer, nx_blend_mode_t mode) {
    if (!layer) return;
    layer->blend_mode = mode;
}

nx_blend_mode_t nx_layer_get_blend_mode(nx_layer_t* layer) {
    return layer ? layer->blend_mode : NX_BLEND_NORMAL;
}
