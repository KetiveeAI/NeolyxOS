/*
 * NeolyxOS - Animated Icon System
 * 
 * Provides animated icon rendering with hover, press, and pulse effects
 * using NXRender animation primitives.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef NXICON_ANIM_H
#define NXICON_ANIM_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct nx_context;
typedef struct nx_context nx_context_t;

/* ============ Animation Types ============ */

typedef enum {
    NXI_ANIM_NONE = 0,
    NXI_ANIM_HOVER,      /* Scale up on hover */
    NXI_ANIM_PRESS,      /* Scale down on press */
    NXI_ANIM_PULSE,      /* Continuous pulsing */
    NXI_ANIM_BOUNCE,     /* Bounce on dock */
    NXI_ANIM_SHAKE,      /* Error shake */
    NXI_ANIM_SPIN,       /* Loading spinner */
    NXI_ANIM_FADE_IN,    /* Fade in appearance */
    NXI_ANIM_FADE_OUT,   /* Fade out disappearance */
    NXI_ANIM_SLIDE_UP,   /* Slide up appearance */
    NXI_ANIM_SPRING,     /* Spring physics */
} nxi_anim_type_t;

typedef enum {
    NXI_STATE_IDLE = 0,
    NXI_STATE_HOVERED,
    NXI_STATE_PRESSED,
    NXI_STATE_ANIMATING,
} nxi_state_t;

/* ============ Animated Icon Structure ============ */

typedef struct {
    uint32_t icon_id;
    int32_t base_x;
    int32_t base_y;
    int32_t base_size;
    uint32_t tint_color;
    
    /* Animation state */
    nxi_state_t state;
    nxi_anim_type_t anim_type;
    
    /* Animation values */
    float scale;           /* Current scale (1.0 = normal) */
    float target_scale;    /* Target scale */
    float opacity;         /* Current opacity (0.0-1.0) */
    float target_opacity;
    float offset_x;        /* Position offset */
    float offset_y;
    float rotation;        /* Rotation in degrees */
    
    /* Spring physics state */
    float velocity_scale;
    float velocity_x;
    float velocity_y;
    
    /* Timing */
    float elapsed_ms;
    float duration_ms;
    int32_t loop_count;    /* -1 = infinite */
    
    /* Callbacks */
    void (*on_complete)(void *user_data);
    void *user_data;
} nxi_animated_icon_t;

/* ============ Initialization ============ */

/* Initialize an animated icon */
void nxi_anim_init(nxi_animated_icon_t *icon, uint32_t icon_id,
                   int32_t x, int32_t y, int32_t size, uint32_t color);

/* Reset animation state */
void nxi_anim_reset(nxi_animated_icon_t *icon);

/* ============ Animation Control ============ */

/* Start an animation */
void nxi_anim_start(nxi_animated_icon_t *icon, nxi_anim_type_t type, 
                    float duration_ms);

/* Start looping animation */
void nxi_anim_start_loop(nxi_animated_icon_t *icon, nxi_anim_type_t type,
                         float duration_ms, int32_t loop_count);

/* Stop current animation */
void nxi_anim_stop(nxi_animated_icon_t *icon);

/* ============ State Changes ============ */

/* Called when mouse enters icon bounds */
void nxi_anim_on_hover_enter(nxi_animated_icon_t *icon);

/* Called when mouse leaves icon bounds */
void nxi_anim_on_hover_leave(nxi_animated_icon_t *icon);

/* Called when mouse button pressed on icon */
void nxi_anim_on_press(nxi_animated_icon_t *icon);

/* Called when mouse button released on icon */
void nxi_anim_on_release(nxi_animated_icon_t *icon);

/* ============ Update and Render ============ */

/* Update animation (call every frame with delta time in ms) */
bool nxi_anim_update(nxi_animated_icon_t *icon, float dt_ms);

/* Render the animated icon */
void nxi_anim_render(nx_context_t *ctx, nxi_animated_icon_t *icon);

/* ============ Utility ============ */

/* Check if point is inside icon bounds (accounting for animation) */
bool nxi_anim_hit_test(const nxi_animated_icon_t *icon, int32_t x, int32_t y);

/* Get current animated bounds */
void nxi_anim_get_bounds(const nxi_animated_icon_t *icon,
                          int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* ============ Dock-specific Animations ============ */

/* Bounce animation for dock icons (app launching) */
void nxi_dock_bounce_start(nxi_animated_icon_t *icon);

/* Magnification effect for dock (mouse proximity) */
void nxi_dock_magnify(nxi_animated_icon_t *icon, float proximity);

#endif /* NXICON_ANIM_H */
