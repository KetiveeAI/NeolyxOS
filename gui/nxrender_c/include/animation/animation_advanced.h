/*
 * NeolyxOS - NXRender Advanced Animation System
 * High-precision animation engine with complex sequences
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * ANIMATION PRECISION:
 * ====================
 * This system achieves smooth 60 FPS animation through:
 * 
 * 1. HIGH-PRECISION TIMING
 *    - Uses nanosecond-precision clocks
 *    - Frame-perfect interpolation
 *    - Handles variable frame rates
 *
 * 2. ANIMATION CURVES (Bezier)
 *    - Custom timing curves for natural motion
 *    - iOS/Android-quality easing
 *    - Cubic bezier with 4 control points
 *
 * 3. ANIMATION COMPOSITION
 *    - Parallel: multiple animations at once
 *    - Sequential: one after another
 *    - Staggered: delayed starts
 *
 * 4. SPRING PHYSICS
 *    - Critically damped for no overshoot
 *    - Under-damped for bouncy feel
 *    - Over-damped for heavy objects
 *
 * HOW NXRENDER ANIMATION WORKS:
 * =============================
 * 
 * 1. Create animation timeline
 * 2. Add keyframes with values and timing
 * 3. Each frame: update animations, get interpolated values
 * 4. Apply values to widget properties
 * 5. Request redraw
 * 
 * Example flow:
 *   Frame 0:   Button.opacity = 0.0
 *   Frame 15:  Button.opacity = 0.5  (interpolated)
 *   Frame 30:  Button.opacity = 1.0
 */

#ifndef NXRENDER_ANIMATION_ADVANCED_H
#define NXRENDER_ANIMATION_ADVANCED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * High-Precision Time
 * ============================================================================ */
typedef int64_t nx_time_ns_t;  /* Nanosecond precision */

nx_time_ns_t nx_time_now(void);
float nx_time_to_seconds(nx_time_ns_t ns);
nx_time_ns_t nx_seconds_to_time(float seconds);

/* ============================================================================
 * Cubic Bezier Curve (Custom Easing)
 * ============================================================================ */
typedef struct {
    float x1, y1;  /* Control point 1 (start tangent) */
    float x2, y2;  /* Control point 2 (end tangent) */
} nx_bezier_t;

/* Common bezier presets (like CSS) */
extern const nx_bezier_t NX_BEZIER_LINEAR;
extern const nx_bezier_t NX_BEZIER_EASE;
extern const nx_bezier_t NX_BEZIER_EASE_IN;
extern const nx_bezier_t NX_BEZIER_EASE_OUT;
extern const nx_bezier_t NX_BEZIER_EASE_IN_OUT;
extern const nx_bezier_t NX_BEZIER_EASE_IN_BACK;    /* Overshoot at start */
extern const nx_bezier_t NX_BEZIER_EASE_OUT_BACK;   /* Overshoot at end */
extern const nx_bezier_t NX_BEZIER_EASE_IN_OUT_BACK;

/* Evaluate bezier curve at t (0-1) */
float nx_bezier_eval(const nx_bezier_t* curve, float t);

/* ============================================================================
 * Keyframe Animation
 * ============================================================================ */
#define NX_MAX_KEYFRAMES 32

typedef struct {
    float time;         /* Normalized time (0-1) */
    float value;        /* Value at this keyframe */
    nx_bezier_t easing; /* Easing TO this keyframe */
} nx_keyframe_t;

typedef struct {
    nx_keyframe_t keyframes[NX_MAX_KEYFRAMES];
    uint32_t keyframe_count;
    float duration_ms;
    float elapsed_ms;
    bool playing;
    bool loop;
    bool reverse;
    float playback_rate;  /* 1.0 = normal, 2.0 = double speed */
    
    /* Callbacks */
    void (*on_complete)(void* user_data);
    void (*on_update)(float value, void* user_data);
    void* user_data;
} nx_keyframe_anim_t;

nx_keyframe_anim_t* nx_keyframe_anim_create(float duration_ms);
void nx_keyframe_anim_destroy(nx_keyframe_anim_t* anim);
void nx_keyframe_add(nx_keyframe_anim_t* anim, float time, float value, nx_bezier_t easing);
void nx_keyframe_anim_play(nx_keyframe_anim_t* anim);
void nx_keyframe_anim_pause(nx_keyframe_anim_t* anim);
void nx_keyframe_anim_reset(nx_keyframe_anim_t* anim);
bool nx_keyframe_anim_update(nx_keyframe_anim_t* anim, float dt_ms);
float nx_keyframe_anim_value(const nx_keyframe_anim_t* anim);

/* ============================================================================
 * Animation Timeline (Group multiple animations)
 * ============================================================================ */
#define NX_MAX_TIMELINE_ENTRIES 64

typedef struct {
    void* animation;    /* Pointer to any animation type */
    float start_time;   /* When to start (ms from timeline start) */
    float duration;     /* Override duration (0 = use animation's own) */
    bool finished;
} nx_timeline_entry_t;

typedef struct {
    nx_timeline_entry_t entries[NX_MAX_TIMELINE_ENTRIES];
    uint32_t entry_count;
    float elapsed_ms;
    float total_duration;
    bool playing;
    bool loop;
    void (*on_complete)(void* user_data);
    void* user_data;
} nx_timeline_t;

nx_timeline_t* nx_timeline_create(void);
void nx_timeline_destroy(nx_timeline_t* timeline);
void nx_timeline_add(nx_timeline_t* timeline, void* anim, float start_time);
void nx_timeline_play(nx_timeline_t* timeline);
void nx_timeline_pause(nx_timeline_t* timeline);
void nx_timeline_reset(nx_timeline_t* timeline);
bool nx_timeline_update(nx_timeline_t* timeline, float dt_ms);

/* ============================================================================
 * Staggered Animation (Cascading delays)
 * ============================================================================ */
typedef struct {
    uint32_t item_count;
    float stagger_ms;       /* Delay between each item */
    float item_duration_ms; /* Duration of each item's animation */
    float elapsed_ms;
    nx_bezier_t easing;
    bool playing;
} nx_stagger_t;

nx_stagger_t* nx_stagger_create(uint32_t count, float stagger_ms, float duration_ms);
void nx_stagger_destroy(nx_stagger_t* stagger);
void nx_stagger_play(nx_stagger_t* stagger);
bool nx_stagger_update(nx_stagger_t* stagger, float dt_ms);
float nx_stagger_value_at(const nx_stagger_t* stagger, uint32_t index); /* Get value for item */

/* ============================================================================
 * Spring Animation (Physics-based, more control)
 * ============================================================================ */
typedef struct {
    /* Spring parameters */
    float mass;         /* Object mass (affects inertia) */
    float stiffness;    /* Spring strength (k) */
    float damping;      /* Friction (c) */
    
    /* State */
    float position;
    float velocity;
    float target;
    
    /* Precision control */
    float rest_threshold;   /* When to consider "at rest" */
    float rest_velocity;    /* Velocity threshold for rest */
    
    /* Precomputed for performance */
    float omega;        /* Angular frequency */
    float zeta;         /* Damping ratio */
    
    bool settled;
} nx_spring_adv_t;

/* Spring presets */
typedef enum {
    NX_SPRING_GENTLE,       /* Slow, smooth */
    NX_SPRING_WOBBLY,       /* Bouncy */
    NX_SPRING_STIFF,        /* Quick, snappy */
    NX_SPRING_SLOW,         /* Heavy object */
    NX_SPRING_MOLASSES      /* Very slow, no bounce */
} nx_spring_preset_t;

nx_spring_adv_t* nx_spring_adv_create(float mass, float stiffness, float damping);
nx_spring_adv_t* nx_spring_adv_from_preset(nx_spring_preset_t preset);
void nx_spring_adv_destroy(nx_spring_adv_t* spring);
void nx_spring_adv_set_target(nx_spring_adv_t* spring, float target);
void nx_spring_adv_update(nx_spring_adv_t* spring, float dt_ms);
float nx_spring_adv_value(const nx_spring_adv_t* spring);
bool nx_spring_adv_is_settled(const nx_spring_adv_t* spring);

/* ============================================================================
 * Animation Controller (Manages all active animations)
 * ============================================================================ */
#define NX_MAX_ACTIVE_ANIMS 128

typedef struct {
    void* animations[NX_MAX_ACTIVE_ANIMS];
    uint32_t anim_types[NX_MAX_ACTIVE_ANIMS];  /* Type identifier */
    uint32_t count;
    nx_time_ns_t last_update;
    float time_scale;   /* Global speed multiplier */
} nx_anim_controller_t;

nx_anim_controller_t* nx_anim_controller_create(void);
void nx_anim_controller_destroy(nx_anim_controller_t* ctrl);
void nx_anim_controller_add(nx_anim_controller_t* ctrl, void* anim, uint32_t type);
void nx_anim_controller_remove(nx_anim_controller_t* ctrl, void* anim);
void nx_anim_controller_update(nx_anim_controller_t* ctrl);  /* Call each frame */
void nx_anim_controller_pause_all(nx_anim_controller_t* ctrl);
void nx_anim_controller_resume_all(nx_anim_controller_t* ctrl);

/* ============================================================================
 * Animation Property Binding (Animate widget properties)
 * ============================================================================ */
typedef enum {
    NX_ANIM_PROP_X,
    NX_ANIM_PROP_Y,
    NX_ANIM_PROP_WIDTH,
    NX_ANIM_PROP_HEIGHT,
    NX_ANIM_PROP_OPACITY,
    NX_ANIM_PROP_SCALE,
    NX_ANIM_PROP_ROTATION,
    NX_ANIM_PROP_COLOR_R,
    NX_ANIM_PROP_COLOR_G,
    NX_ANIM_PROP_COLOR_B,
    NX_ANIM_PROP_COLOR_A
} nx_anim_property_t;

typedef struct {
    void* target;           /* Widget pointer */
    nx_anim_property_t prop;
    void* animation;        /* Animation source */
    float (*get_value)(void* anim);  /* Function to get current value */
} nx_property_binding_t;

void nx_bind_animation(void* widget, nx_anim_property_t prop, void* animation, float (*getter)(void*));

#ifdef __cplusplus
}
#endif
#endif
