/*
 * NeolyxOS - Animated Icon System Implementation
 * 
 * Uses NXRender animation primitives for smooth icon animations.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "../include/nxicon_anim.h"
#include "../include/nxi_render.h"
#include "../include/nxrender_bridge.h"
#include <stddef.h>

/* ============ Constants ============ */

#define NXI_HOVER_SCALE      1.15f
#define NXI_PRESS_SCALE      0.90f
#define NXI_DOCK_MAX_SCALE   1.5f
#define NXI_SPRING_STIFFNESS 300.0f
#define NXI_SPRING_DAMPING   20.0f
#define NXI_SPRING_THRESHOLD 0.001f

/* ============ Math Helpers ============ */

static float nxi_abs(float x) { return x < 0 ? -x : x; }
static float nxi_min(float a, float b) { return a < b ? a : b; }
static float nxi_max(float a, float b) { return a > b ? a : b; }
static float nxi_clamp(float v, float lo, float hi) { return nxi_min(nxi_max(v, lo), hi); }
static float nxi_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* Sine approximation for animation */
static float nxi_sin(float x) {
    const float PI = 3.14159265f;
    x = x - ((int)(x / (2 * PI))) * (2 * PI);
    if (x < 0) x += 2 * PI;
    if (x > PI) { x -= PI; return -(x - x*x*x/6.0f + x*x*x*x*x/120.0f); }
    return x - x*x*x/6.0f + x*x*x*x*x/120.0f;
}

/* ============ Easing Functions ============ */

static float ease_out_quad(float t) { return t * (2.0f - t); }
static float ease_out_cubic(float t) { float u = t - 1.0f; return u * u * u + 1.0f; }
static float ease_out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static float ease_out_elastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float PI = 3.14159265f;
    float pow2 = 1.0f;
    for (int i = 0; i < (int)(-10.0f * t); i++) pow2 *= 2.0f;
    if (-10.0f * t < 0) pow2 = 1.0f / pow2;
    return pow2 * nxi_sin((t * 10.0f - 0.75f) * (2.0f * PI / 3.0f)) + 1.0f;
}
static float ease_out_bounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1; return n1 * t * t + 0.984375f;
}

/* ============ Initialization ============ */

void nxi_anim_init(nxi_animated_icon_t *icon, uint32_t icon_id,
                   int32_t x, int32_t y, int32_t size, uint32_t color) {
    if (!icon) return;
    
    icon->icon_id = icon_id;
    icon->base_x = x;
    icon->base_y = y;
    icon->base_size = size;
    icon->tint_color = color;
    
    icon->state = NXI_STATE_IDLE;
    icon->anim_type = NXI_ANIM_NONE;
    
    icon->scale = 1.0f;
    icon->target_scale = 1.0f;
    icon->opacity = 1.0f;
    icon->target_opacity = 1.0f;
    icon->offset_x = 0.0f;
    icon->offset_y = 0.0f;
    icon->rotation = 0.0f;
    
    icon->velocity_scale = 0.0f;
    icon->velocity_x = 0.0f;
    icon->velocity_y = 0.0f;
    
    icon->elapsed_ms = 0.0f;
    icon->duration_ms = 200.0f;
    icon->loop_count = 0;
    
    icon->on_complete = NULL;
    icon->user_data = NULL;
}

void nxi_anim_reset(nxi_animated_icon_t *icon) {
    if (!icon) return;
    icon->scale = 1.0f;
    icon->target_scale = 1.0f;
    icon->opacity = 1.0f;
    icon->offset_x = 0.0f;
    icon->offset_y = 0.0f;
    icon->rotation = 0.0f;
    icon->velocity_scale = 0.0f;
    icon->velocity_x = 0.0f;
    icon->velocity_y = 0.0f;
    icon->elapsed_ms = 0.0f;
    icon->state = NXI_STATE_IDLE;
    icon->anim_type = NXI_ANIM_NONE;
}

/* ============ Animation Control ============ */

void nxi_anim_start(nxi_animated_icon_t *icon, nxi_anim_type_t type, 
                    float duration_ms) {
    nxi_anim_start_loop(icon, type, duration_ms, 1);
}

void nxi_anim_start_loop(nxi_animated_icon_t *icon, nxi_anim_type_t type,
                         float duration_ms, int32_t loop_count) {
    if (!icon) return;
    
    icon->anim_type = type;
    icon->duration_ms = duration_ms;
    icon->loop_count = loop_count;
    icon->elapsed_ms = 0.0f;
    icon->state = NXI_STATE_ANIMATING;
    
    /* Set initial values based on animation type */
    switch (type) {
        case NXI_ANIM_FADE_IN:
            icon->opacity = 0.0f;
            icon->target_opacity = 1.0f;
            break;
        case NXI_ANIM_FADE_OUT:
            icon->opacity = 1.0f;
            icon->target_opacity = 0.0f;
            break;
        case NXI_ANIM_SLIDE_UP:
            icon->offset_y = (float)icon->base_size;
            icon->opacity = 0.0f;
            break;
        case NXI_ANIM_BOUNCE:
            icon->target_scale = 1.0f;
            icon->offset_y = -(float)icon->base_size * 0.5f;
            break;
        default:
            break;
    }
}

void nxi_anim_stop(nxi_animated_icon_t *icon) {
    if (!icon) return;
    icon->anim_type = NXI_ANIM_NONE;
    icon->state = NXI_STATE_IDLE;
    icon->scale = 1.0f;
    icon->opacity = 1.0f;
    icon->offset_x = 0.0f;
    icon->offset_y = 0.0f;
}

/* ============ State Changes ============ */

void nxi_anim_on_hover_enter(nxi_animated_icon_t *icon) {
    if (!icon) return;
    icon->state = NXI_STATE_HOVERED;
    icon->target_scale = NXI_HOVER_SCALE;
    icon->anim_type = NXI_ANIM_SPRING;
}

void nxi_anim_on_hover_leave(nxi_animated_icon_t *icon) {
    if (!icon) return;
    icon->state = NXI_STATE_IDLE;
    icon->target_scale = 1.0f;
    icon->anim_type = NXI_ANIM_SPRING;
}

void nxi_anim_on_press(nxi_animated_icon_t *icon) {
    if (!icon) return;
    icon->state = NXI_STATE_PRESSED;
    icon->target_scale = NXI_PRESS_SCALE;
    icon->anim_type = NXI_ANIM_SPRING;
}

void nxi_anim_on_release(nxi_animated_icon_t *icon) {
    if (!icon) return;
    if (icon->state == NXI_STATE_PRESSED) {
        icon->state = NXI_STATE_HOVERED;
        icon->target_scale = NXI_HOVER_SCALE;
    }
}

/* ============ Update ============ */

static void update_spring(nxi_animated_icon_t *icon, float dt_ms) {
    float dt = dt_ms / 1000.0f;
    
    /* Spring physics for scale */
    float displacement = icon->scale - icon->target_scale;
    float spring_force = -NXI_SPRING_STIFFNESS * displacement;
    float damping_force = -NXI_SPRING_DAMPING * icon->velocity_scale;
    float acceleration = spring_force + damping_force;
    
    icon->velocity_scale += acceleration * dt;
    icon->scale += icon->velocity_scale * dt;
    
    /* Spring physics for position */
    float disp_x = icon->offset_x - 0.0f;
    float disp_y = icon->offset_y - 0.0f;
    
    float force_x = -NXI_SPRING_STIFFNESS * disp_x - NXI_SPRING_DAMPING * icon->velocity_x;
    float force_y = -NXI_SPRING_STIFFNESS * disp_y - NXI_SPRING_DAMPING * icon->velocity_y;
    
    icon->velocity_x += force_x * dt;
    icon->velocity_y += force_y * dt;
    icon->offset_x += icon->velocity_x * dt;
    icon->offset_y += icon->velocity_y * dt;
}

bool nxi_anim_update(nxi_animated_icon_t *icon, float dt_ms) {
    if (!icon) return false;
    
    /* Always update spring physics if in spring mode */
    if (icon->anim_type == NXI_ANIM_SPRING) {
        update_spring(icon, dt_ms);
        
        /* Check if settled */
        float scale_diff = nxi_abs(icon->scale - icon->target_scale);
        float pos_diff = nxi_abs(icon->offset_x) + nxi_abs(icon->offset_y);
        float vel = nxi_abs(icon->velocity_scale) + nxi_abs(icon->velocity_x) + nxi_abs(icon->velocity_y);
        
        if (scale_diff < NXI_SPRING_THRESHOLD && pos_diff < NXI_SPRING_THRESHOLD && vel < NXI_SPRING_THRESHOLD) {
            icon->scale = icon->target_scale;
            icon->offset_x = 0.0f;
            icon->offset_y = 0.0f;
            if (icon->state == NXI_STATE_ANIMATING) {
                icon->anim_type = NXI_ANIM_NONE;
                icon->state = NXI_STATE_IDLE;
            }
        }
        return true;
    }
    
    /* Time-based animations */
    if (icon->anim_type == NXI_ANIM_NONE) return false;
    
    icon->elapsed_ms += dt_ms;
    float t = nxi_clamp(icon->elapsed_ms / icon->duration_ms, 0.0f, 1.0f);
    
    switch (icon->anim_type) {
        case NXI_ANIM_HOVER:
            icon->scale = nxi_lerp(1.0f, NXI_HOVER_SCALE, ease_out_cubic(t));
            break;
            
        case NXI_ANIM_PRESS:
            icon->scale = nxi_lerp(1.0f, NXI_PRESS_SCALE, ease_out_quad(t));
            break;
            
        case NXI_ANIM_PULSE: {
            float pulse = (nxi_sin(icon->elapsed_ms * 0.01f) + 1.0f) * 0.5f;
            icon->scale = 1.0f + pulse * 0.1f;
            t = 0.0f;  /* Keep running */
            break;
        }
        
        case NXI_ANIM_BOUNCE: {
            float bounce = ease_out_bounce(t);
            icon->offset_y = -(float)icon->base_size * 0.5f * (1.0f - bounce);
            icon->scale = 1.0f + (1.0f - bounce) * 0.15f;
            break;
        }
        
        case NXI_ANIM_SHAKE: {
            float shake = nxi_sin(icon->elapsed_ms * 0.1f) * (1.0f - t);
            icon->offset_x = shake * 5.0f;
            break;
        }
        
        case NXI_ANIM_SPIN:
            icon->rotation = icon->elapsed_ms * 0.36f;  /* 360deg per second */
            t = 0.0f;  /* Keep running */
            break;
            
        case NXI_ANIM_FADE_IN:
            icon->opacity = ease_out_quad(t);
            break;
            
        case NXI_ANIM_FADE_OUT:
            icon->opacity = 1.0f - ease_out_quad(t);
            break;
            
        case NXI_ANIM_SLIDE_UP:
            icon->offset_y = (float)icon->base_size * (1.0f - ease_out_back(t));
            icon->opacity = ease_out_quad(t);
            break;
            
        default:
            break;
    }
    
    /* Check if animation complete */
    if (t >= 1.0f) {
        if (icon->loop_count > 0) {
            icon->loop_count--;
            icon->elapsed_ms = 0.0f;
        } else if (icon->loop_count == 0) {
            icon->anim_type = NXI_ANIM_NONE;
            icon->state = NXI_STATE_IDLE;
            if (icon->on_complete) {
                icon->on_complete(icon->user_data);
            }
            return false;
        }
        /* loop_count == -1 means infinite */
    }
    
    return true;
}

/* ============ Render ============ */

void nxi_anim_render(nx_context_t *ctx, nxi_animated_icon_t *icon) {
    if (!icon || icon->opacity <= 0.01f) return;
    
    /* Calculate animated position and size */
    int32_t size = (int32_t)(icon->base_size * icon->scale);
    int32_t size_diff = size - icon->base_size;
    
    int32_t x = icon->base_x - size_diff / 2 + (int32_t)icon->offset_x;
    int32_t y = icon->base_y - size_diff / 2 + (int32_t)icon->offset_y;
    
    /* Apply opacity to color */
    uint32_t color = icon->tint_color;
    uint8_t alpha = (uint8_t)((color >> 24) * icon->opacity);
    color = (alpha << 24) | (color & 0x00FFFFFF);
    
    /* Render icon */
    nxi_render_icon(ctx, icon->icon_id, x, y, size, color);
}

/* ============ Utility ============ */

bool nxi_anim_hit_test(const nxi_animated_icon_t *icon, int32_t x, int32_t y) {
    if (!icon) return false;
    
    int32_t size = (int32_t)(icon->base_size * icon->scale);
    int32_t size_diff = size - icon->base_size;
    
    int32_t ix = icon->base_x - size_diff / 2 + (int32_t)icon->offset_x;
    int32_t iy = icon->base_y - size_diff / 2 + (int32_t)icon->offset_y;
    
    return x >= ix && x < ix + size && y >= iy && y < iy + size;
}

void nxi_anim_get_bounds(const nxi_animated_icon_t *icon,
                          int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    if (!icon) return;
    
    int32_t size = (int32_t)(icon->base_size * icon->scale);
    int32_t size_diff = size - icon->base_size;
    
    if (x) *x = icon->base_x - size_diff / 2 + (int32_t)icon->offset_x;
    if (y) *y = icon->base_y - size_diff / 2 + (int32_t)icon->offset_y;
    if (w) *w = size;
    if (h) *h = size;
}

/* ============ Dock Animations ============ */

void nxi_dock_bounce_start(nxi_animated_icon_t *icon) {
    if (!icon) return;
    nxi_anim_start_loop(icon, NXI_ANIM_BOUNCE, 600.0f, 3);
}

void nxi_dock_magnify(nxi_animated_icon_t *icon, float proximity) {
    if (!icon) return;
    
    /* proximity: 0.0 = far away, 1.0 = directly under cursor */
    float scale = 1.0f + (NXI_DOCK_MAX_SCALE - 1.0f) * proximity;
    icon->target_scale = scale;
    
    /* Use spring physics for smooth interpolation */
    icon->anim_type = NXI_ANIM_SPRING;
}
