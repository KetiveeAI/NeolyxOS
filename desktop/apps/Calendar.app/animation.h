/*
 * NeolyxOS Calendar - Animation System
 * 
 * Smooth animations using ReoxUI-compatible easing and spring physics.
 * 60 FPS target with GPU-friendly rendering.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CALENDAR_ANIMATION_H
#define CALENDAR_ANIMATION_H

#include <stdint.h>
#include <stdbool.h>

/* Animation FPS target */
#define CAL_ANIM_FPS 60
#define CAL_ANIM_FRAME_MS (1000 / CAL_ANIM_FPS)

/* Easing functions - compatible with ReoxUI */
typedef enum {
    EASE_LINEAR = 0,
    EASE_IN,              /* Cubic ease-in */
    EASE_OUT,             /* Cubic ease-out */
    EASE_IN_OUT,          /* Cubic ease-in-out */
    EASE_IN_QUAD,
    EASE_OUT_QUAD,
    EASE_IN_OUT_QUAD,
    EASE_IN_EXPO,
    EASE_OUT_EXPO,
    EASE_IN_BACK,         /* Overshoot start */
    EASE_OUT_BACK,        /* Overshoot end */
    EASE_BOUNCE,
    EASE_SPRING,          /* Spring physics */
    EASE_COUNT
} cal_easing_t;

/* Animation state */
typedef enum {
    ANIM_IDLE = 0,
    ANIM_RUNNING,
    ANIM_PAUSED,
    ANIM_FINISHED
} cal_anim_state_t;

/* Property types that can be animated */
typedef enum {
    ANIM_PROP_X = 0,
    ANIM_PROP_Y,
    ANIM_PROP_WIDTH,
    ANIM_PROP_HEIGHT,
    ANIM_PROP_OPACITY,
    ANIM_PROP_SCALE,
    ANIM_PROP_ROTATION,
    ANIM_PROP_COLOR,
    ANIM_PROP_CUSTOM
} cal_anim_property_t;

/* Spring configuration */
typedef struct {
    float stiffness;      /* 100-500 typical */
    float damping;        /* 10-30 typical */
    float mass;           /* 1.0 typical */
} cal_spring_config_t;

/* Single animation */
typedef struct {
    cal_anim_property_t property;
    float from;
    float to;
    float current;
    float duration_ms;
    float elapsed_ms;
    cal_easing_t easing;
    cal_anim_state_t state;
    cal_spring_config_t spring;
    float spring_velocity;
    void (*on_complete)(void *ctx);
    void *callback_ctx;
} cal_animation_t;

/* Animation group for chaining */
typedef struct {
    cal_animation_t *animations;
    int count;
    int capacity;
    bool parallel;        /* Run all at once or sequentially */
    int current_index;
} cal_anim_group_t;

/* Animation scheduler */
typedef struct {
    cal_animation_t **active;
    int active_count;
    int active_capacity;
    uint64_t last_tick;
} cal_animator_t;

/* Create/destroy animator */
cal_animator_t *cal_animator_create(void);
void cal_animator_destroy(cal_animator_t *anim);

/* Add animation */
cal_animation_t *cal_anim_create(cal_anim_property_t prop, float from, float to, float duration_ms, cal_easing_t easing);
void cal_anim_destroy(cal_animation_t *anim);
void cal_animator_add(cal_animator_t *animator, cal_animation_t *anim);

/* Control */
void cal_anim_start(cal_animation_t *anim);
void cal_anim_pause(cal_animation_t *anim);
void cal_anim_resume(cal_animation_t *anim);
void cal_anim_reset(cal_animation_t *anim);

/* Update - returns true if any animations active */
bool cal_animator_update(cal_animator_t *animator, uint64_t current_tick);

/* Easing function */
float cal_ease(cal_easing_t easing, float t);

/* Spring physics */
cal_spring_config_t cal_spring_default(void);
cal_spring_config_t cal_spring_bouncy(void);
cal_spring_config_t cal_spring_stiff(void);
float cal_spring_update(cal_animation_t *anim, float dt);

/* Preset animations */
cal_animation_t *cal_anim_fade_in(float duration_ms);
cal_animation_t *cal_anim_fade_out(float duration_ms);
cal_animation_t *cal_anim_slide_in(int direction, float distance, float duration_ms);  /* 0=left, 1=right, 2=up, 3=down */
cal_animation_t *cal_anim_scale_in(float duration_ms);
cal_animation_t *cal_anim_bounce(float duration_ms);
cal_animation_t *cal_anim_pulse(float duration_ms);

/* Color interpolation */
uint32_t cal_color_lerp(uint32_t from, uint32_t to, float t);

#endif /* CALENDAR_ANIMATION_H */
