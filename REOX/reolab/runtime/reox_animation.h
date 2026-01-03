/*
 * REOX Animation System
 * Smooth animations for NeolyxOS UI
 * 
 * Features:
 * - Easing functions (ease-in, ease-out, spring, bounce)
 * - Property animations
 * - Keyframe animations
 * - Spring physics
 */

#ifndef REOX_ANIMATION_H
#define REOX_ANIMATION_H

#include "reox_ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Animation Constants
 * ============================================================================ */

#define RX_ANIM_FPS 60
#define RX_ANIM_FRAME_TIME (1.0f / RX_ANIM_FPS)

/* ============================================================================
 * Easing Functions
 * ============================================================================ */

typedef enum rx_easing {
    RX_EASE_LINEAR,
    RX_EASE_IN,           /* Cubic ease-in */
    RX_EASE_OUT,          /* Cubic ease-out */
    RX_EASE_IN_OUT,       /* Cubic ease-in-out */
    RX_EASE_IN_QUAD,
    RX_EASE_OUT_QUAD,
    RX_EASE_IN_OUT_QUAD,
    RX_EASE_IN_CUBIC,
    RX_EASE_OUT_CUBIC,
    RX_EASE_IN_OUT_CUBIC,
    RX_EASE_IN_QUART,
    RX_EASE_OUT_QUART,
    RX_EASE_IN_OUT_QUART,
    RX_EASE_IN_EXPO,
    RX_EASE_OUT_EXPO,
    RX_EASE_IN_OUT_EXPO,
    RX_EASE_IN_BACK,      /* Overshoot start */
    RX_EASE_OUT_BACK,     /* Overshoot end */
    RX_EASE_IN_OUT_BACK,
    RX_EASE_IN_ELASTIC,
    RX_EASE_OUT_ELASTIC,
    RX_EASE_IN_OUT_ELASTIC,
    RX_EASE_BOUNCE,
    RX_EASE_SPRING,       /* Spring physics */
} rx_easing;

/* Apply easing function to progress (0.0 to 1.0) */
extern float rx_ease(rx_easing easing, float t);

/* Custom cubic bezier easing */
extern float rx_ease_bezier(float t, float x1, float y1, float x2, float y2);

/* ============================================================================
 * Spring Physics
 * ============================================================================ */

typedef struct rx_spring_config {
    float stiffness;      /* Spring stiffness (100-500 typical) */
    float damping;        /* Damping ratio (10-30 typical) */
    float mass;           /* Mass of object (1.0 typical) */
    float velocity;       /* Initial velocity */
} rx_spring_config;

/* Preset spring configs */
extern rx_spring_config rx_spring_default(void);      /* Smooth default */
extern rx_spring_config rx_spring_bouncy(void);       /* High bounce */
extern rx_spring_config rx_spring_stiff(void);        /* Quick, minimal bounce */
extern rx_spring_config rx_spring_gentle(void);       /* Slow, smooth */

typedef struct rx_spring {
    float current;
    float target;
    float velocity;
    rx_spring_config config;
    bool settled;
} rx_spring;

extern rx_spring* rx_spring_create(float initial, rx_spring_config config);
extern void rx_spring_set_target(rx_spring* spring, float target);
extern float rx_spring_update(rx_spring* spring, float dt);
extern bool rx_spring_is_settled(rx_spring* spring);
extern void rx_spring_destroy(rx_spring* spring);

/* ============================================================================
 * Animation State
 * ============================================================================ */

typedef enum rx_anim_state {
    RX_ANIM_IDLE,
    RX_ANIM_RUNNING,
    RX_ANIM_PAUSED,
    RX_ANIM_COMPLETED,
} rx_anim_state;

/* ============================================================================
 * Property Animation
 * ============================================================================ */

typedef void (*rx_anim_callback)(float value, void* user_data);
typedef void (*rx_anim_complete_callback)(void* user_data);

typedef struct rx_animation {
    /* Animation ID */
    uint64_t id;
    
    /* Values */
    float from;
    float to;
    float current;
    
    /* Timing */
    float duration;       /* Seconds */
    float elapsed;
    float delay;          /* Delay before start */
    
    /* Easing */
    rx_easing easing;
    
    /* State */
    rx_anim_state state;
    bool auto_reverse;
    int repeat_count;     /* -1 = infinite, 0 = once, N = N times */
    int current_repeat;
    bool reversed;
    
    /* Callbacks */
    rx_anim_callback on_update;
    rx_anim_complete_callback on_complete;
    void* user_data;
    
    /* Linked list for animator */
    struct rx_animation* next;
} rx_animation;

/* Create animation */
extern rx_animation* rx_animate(float from, float to, float duration);
extern rx_animation* rx_animate_spring(float from, float to, rx_spring_config config);

/* Configure animation */
extern rx_animation* rx_anim_easing(rx_animation* anim, rx_easing easing);
extern rx_animation* rx_anim_delay(rx_animation* anim, float delay);
extern rx_animation* rx_anim_repeat(rx_animation* anim, int count);
extern rx_animation* rx_anim_auto_reverse(rx_animation* anim, bool reverse);
extern rx_animation* rx_anim_on_update(rx_animation* anim, rx_anim_callback cb, void* data);
extern rx_animation* rx_anim_on_complete(rx_animation* anim, rx_anim_complete_callback cb, void* data);

/* Control animation */
extern void rx_anim_start(rx_animation* anim);
extern void rx_anim_pause(rx_animation* anim);
extern void rx_anim_resume(rx_animation* anim);
extern void rx_anim_stop(rx_animation* anim);
extern void rx_anim_reset(rx_animation* anim);

/* Update animation (call each frame) */
extern bool rx_anim_update(rx_animation* anim, float dt);

/* Destroy animation */
extern void rx_anim_destroy(rx_animation* anim);

/* ============================================================================
 * Keyframe Animation
 * ============================================================================ */

typedef struct rx_keyframe {
    float time;           /* Time in seconds or percentage (0-1) */
    float value;
    rx_easing easing;     /* Easing to this keyframe */
} rx_keyframe;

typedef struct rx_keyframe_animation {
    rx_animation base;
    rx_keyframe* keyframes;
    size_t keyframe_count;
    bool use_percentages; /* If true, keyframe times are 0-1 */
} rx_keyframe_animation;

extern rx_keyframe_animation* rx_keyframes_create(float duration);
extern void rx_keyframes_add(rx_keyframe_animation* anim, float time, float value, rx_easing easing);
extern float rx_keyframes_get_value(rx_keyframe_animation* anim, float t);
extern void rx_keyframes_destroy(rx_keyframe_animation* anim);

/* ============================================================================
 * Color Animation
 * ============================================================================ */

typedef struct rx_color_animation {
    rx_animation base;
    rx_color from_color;
    rx_color to_color;
    rx_color current_color;
} rx_color_animation;

extern rx_color_animation* rx_animate_color(rx_color from, rx_color to, float duration);
extern rx_color rx_color_anim_get(rx_color_animation* anim);

/* ============================================================================
 * View Property Animations
 * ============================================================================ */

typedef enum rx_animatable_property {
    RX_PROP_X,
    RX_PROP_Y,
    RX_PROP_WIDTH,
    RX_PROP_HEIGHT,
    RX_PROP_OPACITY,
    RX_PROP_SCALE,
    RX_PROP_ROTATION,
    RX_PROP_CORNER_RADIUS,
} rx_animatable_property;

typedef struct rx_view_animation {
    rx_animation* anim;
    rx_view* view;
    rx_animatable_property property;
    struct rx_view_animation* next;
} rx_view_animation;

/* Animate view property */
extern rx_animation* rx_view_animate(rx_view* view, rx_animatable_property prop,
                                      float to, float duration);
extern rx_animation* rx_view_animate_from(rx_view* view, rx_animatable_property prop,
                                           float from, float to, float duration);

/* Convenience animations */
extern rx_animation* rx_view_fade_in(rx_view* view, float duration);
extern rx_animation* rx_view_fade_out(rx_view* view, float duration);
extern rx_animation* rx_view_slide_in(rx_view* view, float from_x, float from_y, float duration);
extern rx_animation* rx_view_slide_out(rx_view* view, float to_x, float to_y, float duration);
extern rx_animation* rx_view_scale_in(rx_view* view, float duration);
extern rx_animation* rx_view_scale_out(rx_view* view, float duration);
extern rx_animation* rx_view_bounce(rx_view* view, float duration);
extern rx_animation* rx_view_shake(rx_view* view, float duration);
extern rx_animation* rx_view_pulse(rx_view* view, float duration);

/* ============================================================================
 * Animation Scheduler
 * ============================================================================ */

typedef struct rx_animator {
    rx_animation* animations;
    rx_view_animation* view_animations;
    size_t animation_count;
    uint64_t next_id;
    float time_scale;     /* 1.0 = normal, 0.5 = half speed, 2.0 = double */
    bool paused;
} rx_animator;

/* Global animator */
extern rx_animator* rx_animator_shared(void);

/* Animator lifecycle */
extern rx_animator* rx_animator_create(void);
extern void rx_animator_destroy(rx_animator* animator);

/* Add/remove animations */
extern void rx_animator_add(rx_animator* animator, rx_animation* anim);
extern void rx_animator_remove(rx_animator* animator, rx_animation* anim);
extern void rx_animator_cancel_all(rx_animator* animator);

/* Update all animations */
extern void rx_animator_update(rx_animator* animator, float dt);

/* Control */
extern void rx_animator_pause(rx_animator* animator);
extern void rx_animator_resume(rx_animator* animator);
extern void rx_animator_set_time_scale(rx_animator* animator, float scale);

/* ============================================================================
 * Transition Helpers
 * ============================================================================ */

/* Chain animations (runs sequentially) */
extern rx_animation* rx_anim_then(rx_animation* first, rx_animation* second);

/* Group animations (runs in parallel) */
typedef struct rx_animation_group {
    rx_animation** animations;
    size_t count;
    rx_anim_state state;
} rx_animation_group;

extern rx_animation_group* rx_anim_group(rx_animation** anims, size_t count);
extern void rx_anim_group_start(rx_animation_group* group);
extern bool rx_anim_group_update(rx_animation_group* group, float dt);
extern void rx_anim_group_destroy(rx_animation_group* group);

/* Stagger animations (delayed sequence) */
extern void rx_anim_stagger(rx_animation** anims, size_t count, float delay_between);

#ifdef __cplusplus
}
#endif

#endif /* REOX_ANIMATION_H */
