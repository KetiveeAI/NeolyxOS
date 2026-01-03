/*
 * REOX Transition System
 * Advanced display transitions and morphing effects for NeolyxOS UI
 * 
 * Features:
 * - Page transitions (push, pop, fade, slide, zoom)
 * - View morphing between states
 * - Particle effects system
 * - Shared element transitions
 * - Motion blur effects
 */

#ifndef REOX_TRANSITIONS_H
#define REOX_TRANSITIONS_H

#include "reox_ui.h"
#include "reox_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Transition Types
 * ============================================================================ */

typedef enum rx_transition_type {
    /* Basic transitions */
    RX_TRANS_NONE,
    RX_TRANS_FADE,
    RX_TRANS_SLIDE_LEFT,
    RX_TRANS_SLIDE_RIGHT,
    RX_TRANS_SLIDE_UP,
    RX_TRANS_SLIDE_DOWN,
    RX_TRANS_ZOOM_IN,
    RX_TRANS_ZOOM_OUT,
    
    /* Advanced transitions */
    RX_TRANS_FLIP_HORIZONTAL,
    RX_TRANS_FLIP_VERTICAL,
    RX_TRANS_CUBE_LEFT,
    RX_TRANS_CUBE_RIGHT,
    RX_TRANS_DISSOLVE,
    RX_TRANS_WIPE_LEFT,
    RX_TRANS_WIPE_RIGHT,
    RX_TRANS_IRIS_OPEN,
    RX_TRANS_IRIS_CLOSE,
    
    /* macOS-style transitions */
    RX_TRANS_CROSSFADE,
    RX_TRANS_MOVE_IN,
    RX_TRANS_PUSH,
    RX_TRANS_REVEAL,
    
    /* Material-style transitions */
    RX_TRANS_CONTAINER_TRANSFORM,
    RX_TRANS_SHARED_AXIS,
    RX_TRANS_FADE_THROUGH,
} rx_transition_type;

/* ============================================================================
 * Transition Configuration
 * ============================================================================ */

typedef struct rx_transition_config {
    rx_transition_type type;
    float duration;           /* Seconds */
    float delay;              /* Start delay */
    rx_easing easing;
    bool interactive;         /* Can be gesture-driven */
    float blur_amount;        /* Motion blur (0-1) */
    rx_color overlay_color;   /* For fade effects */
} rx_transition_config;

/* Default transition configs */
extern rx_transition_config rx_transition_default(void);
extern rx_transition_config rx_transition_fast(void);
extern rx_transition_config rx_transition_smooth(void);

/* ============================================================================
 * Page Transition Context
 * ============================================================================ */

typedef struct rx_page_transition {
    rx_view* from_view;
    rx_view* to_view;
    rx_transition_config config;
    
    /* Internal state */
    float progress;           /* 0.0 - 1.0 */
    bool completed;
    bool cancelled;
    
    /* Callbacks */
    void (*on_start)(void* user_data);
    void (*on_update)(float progress, void* user_data);
    void (*on_complete)(void* user_data);
    void (*on_cancel)(void* user_data);
    void* user_data;
} rx_page_transition;

/* Create and control page transitions */
extern rx_page_transition* rx_page_transition_create(rx_view* from, rx_view* to, rx_transition_config config);
extern void rx_page_transition_start(rx_page_transition* trans);
extern void rx_page_transition_update(rx_page_transition* trans, float dt);
extern void rx_page_transition_set_progress(rx_page_transition* trans, float progress);
extern void rx_page_transition_cancel(rx_page_transition* trans);
extern void rx_page_transition_finish(rx_page_transition* trans);
extern void rx_page_transition_destroy(rx_page_transition* trans);

/* ============================================================================
 * Shared Element Transition
 * Links elements across different views for smooth morphing
 * ============================================================================ */

typedef struct rx_shared_element {
    const char* tag;          /* Identifier to match elements */
    rx_view* source;
    rx_view* target;
    rx_rect source_bounds;
    rx_rect target_bounds;
} rx_shared_element;

typedef struct rx_shared_element_transition {
    rx_shared_element* elements;
    size_t element_count;
    rx_transition_config config;
    float progress;
    bool active;
} rx_shared_element_transition;

extern rx_shared_element_transition* rx_shared_transition_create(void);
extern void rx_shared_transition_add(rx_shared_element_transition* trans, const char* tag, rx_view* source, rx_view* target);
extern void rx_shared_transition_start(rx_shared_element_transition* trans);
extern void rx_shared_transition_update(rx_shared_element_transition* trans, float dt);
extern void rx_shared_transition_destroy(rx_shared_element_transition* trans);

/* ============================================================================
 * View Morphing
 * Smooth transformation between two different view states
 * ============================================================================ */

typedef struct rx_morph_state {
    rx_rect bounds;
    rx_color background;
    rx_corner_radii corners;
    float opacity;
    float scale;
    float rotation;           /* Degrees */
} rx_morph_state;

typedef struct rx_morph_animation {
    rx_animation base;
    rx_view* view;
    rx_morph_state from_state;
    rx_morph_state to_state;
    rx_morph_state current_state;
} rx_morph_animation;

extern rx_morph_animation* rx_morph_create(rx_view* view, rx_morph_state from, rx_morph_state to, float duration);
extern rx_morph_state rx_morph_capture_state(rx_view* view);
extern void rx_morph_apply_state(rx_view* view, rx_morph_state state);
extern rx_morph_state rx_morph_interpolate(rx_morph_state from, rx_morph_state to, float t);
extern void rx_morph_start(rx_morph_animation* morph);
extern void rx_morph_update(rx_morph_animation* morph, float dt);
extern void rx_morph_destroy(rx_morph_animation* morph);

/* ============================================================================
 * Particle System
 * ============================================================================ */

typedef enum rx_particle_shape {
    RX_PARTICLE_CIRCLE,
    RX_PARTICLE_SQUARE,
    RX_PARTICLE_TRIANGLE,
    RX_PARTICLE_STAR,
    RX_PARTICLE_CUSTOM,
} rx_particle_shape;

typedef struct rx_particle {
    rx_point position;
    rx_point velocity;
    rx_point acceleration;
    rx_color color;
    rx_color end_color;
    float size;
    float end_size;
    float lifetime;
    float age;
    float rotation;
    float rotation_speed;
    float opacity;
    bool active;
} rx_particle;

typedef struct rx_particle_emitter_config {
    /* Emission */
    float emit_rate;          /* Particles per second */
    int max_particles;
    rx_point position;
    rx_size emit_area;
    float emit_angle;         /* Degrees, center direction */
    float emit_spread;        /* Degrees, spread around center */
    
    /* Initial velocity */
    float min_speed;
    float max_speed;
    
    /* Lifetime */
    float min_lifetime;
    float max_lifetime;
    
    /* Size */
    float min_size;
    float max_size;
    float end_size_scale;     /* Relative to start size */
    
    /* Color */
    rx_color start_color;
    rx_color end_color;
    bool color_randomize;
    
    /* Physics */
    rx_point gravity;
    float drag;               /* Air resistance */
    float turbulence;
    
    /* Appearance */
    rx_particle_shape shape;
    bool additive_blend;
    float blur_amount;
} rx_particle_emitter_config;

typedef struct rx_particle_emitter {
    rx_particle_emitter_config config;
    rx_particle* particles;
    int particle_count;
    float emit_accumulator;
    bool emitting;
    rx_rect bounds;           /* Bounding box for culling */
} rx_particle_emitter;

/* Particle emitter lifecycle */
extern rx_particle_emitter* rx_particles_create(rx_particle_emitter_config config);
extern void rx_particles_set_position(rx_particle_emitter* emitter, rx_point pos);
extern void rx_particles_start(rx_particle_emitter* emitter);
extern void rx_particles_stop(rx_particle_emitter* emitter);
extern void rx_particles_burst(rx_particle_emitter* emitter, int count);
extern void rx_particles_update(rx_particle_emitter* emitter, float dt);
extern void rx_particles_render(rx_particle_emitter* emitter, void* context);
extern void rx_particles_destroy(rx_particle_emitter* emitter);

/* Preset particle effects */
extern rx_particle_emitter* rx_particles_confetti(rx_point origin);
extern rx_particle_emitter* rx_particles_fireworks(rx_point origin, rx_color color);
extern rx_particle_emitter* rx_particles_snow(rx_rect area);
extern rx_particle_emitter* rx_particles_sparks(rx_point origin);
extern rx_particle_emitter* rx_particles_bubbles(rx_rect area);
extern rx_particle_emitter* rx_particles_smoke(rx_point origin);
extern rx_particle_emitter* rx_particles_fire(rx_point origin);
extern rx_particle_emitter* rx_particles_magic(rx_point origin, rx_color color);

/* ============================================================================
 * Motion Blur
 * ============================================================================ */

typedef struct rx_motion_blur {
    rx_view* view;
    float intensity;          /* 0.0 - 1.0 */
    int samples;              /* Quality (3-16 typically) */
    rx_point direction;       /* Blur direction */
    bool auto_direction;      /* Base on velocity */
    rx_point velocity;        /* For auto direction */
} rx_motion_blur;

extern rx_motion_blur* rx_motion_blur_create(rx_view* view, float intensity);
extern void rx_motion_blur_set_velocity(rx_motion_blur* blur, rx_point velocity);
extern void rx_motion_blur_update(rx_motion_blur* blur, float dt);
extern void rx_motion_blur_destroy(rx_motion_blur* blur);

/* ============================================================================
 * Screen Transition Manager
 * ============================================================================ */

typedef struct rx_screen_transition_manager {
    rx_page_transition* current_transition;
    rx_page_transition** history;
    size_t history_count;
    size_t history_capacity;
    rx_transition_config default_config;
} rx_screen_transition_manager;

extern rx_screen_transition_manager* rx_screen_mgr_create(void);
extern void rx_screen_mgr_push(rx_screen_transition_manager* mgr, rx_view* view, rx_transition_config config);
extern void rx_screen_mgr_pop(rx_screen_transition_manager* mgr);
extern void rx_screen_mgr_replace(rx_screen_transition_manager* mgr, rx_view* view, rx_transition_config config);
extern void rx_screen_mgr_update(rx_screen_transition_manager* mgr, float dt);
extern void rx_screen_mgr_destroy(rx_screen_transition_manager* mgr);

#ifdef __cplusplus
}
#endif

#endif /* REOX_TRANSITIONS_H */
