/*
 * NeolyxOS - NXRender Animation System
 * Easing functions and animation state machine
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_ANIMATION_H
#define NXRENDER_ANIMATION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Easing function type */
typedef float (*nx_easing_fn_t)(float t);

/* Built-in easing functions (t in [0,1], returns [0,1]) */
float nx_ease_linear(float t);
float nx_ease_in_quad(float t);
float nx_ease_out_quad(float t);
float nx_ease_in_out_quad(float t);
float nx_ease_in_cubic(float t);
float nx_ease_out_cubic(float t);
float nx_ease_in_out_cubic(float t);
float nx_ease_out_back(float t);
float nx_ease_out_elastic(float t);
float nx_ease_out_bounce(float t);

/* Animation state */
typedef enum {
    NX_ANIM_IDLE,
    NX_ANIM_RUNNING,
    NX_ANIM_PAUSED,
    NX_ANIM_FINISHED
} nx_anim_state_t;

/* Animation structure */
typedef struct {
    float from;
    float to;
    float current;
    float duration_ms;
    float elapsed_ms;
    nx_easing_fn_t easing;
    nx_anim_state_t state;
    void (*on_complete)(void* user_data);
    void* user_data;
} nx_animation_t;

/* Animation lifecycle */
void nx_anim_init(nx_animation_t* anim, float from, float to, float duration_ms, nx_easing_fn_t easing);
void nx_anim_start(nx_animation_t* anim);
void nx_anim_pause(nx_animation_t* anim);
void nx_anim_resume(nx_animation_t* anim);
void nx_anim_reset(nx_animation_t* anim);

/* Update animation (call every frame with delta time in ms) */
bool nx_anim_update(nx_animation_t* anim, float dt_ms);

/* Get current interpolated value */
float nx_anim_value(const nx_animation_t* anim);

/* Check if animation is complete */
bool nx_anim_is_complete(const nx_animation_t* anim);

/* Spring physics animation */
typedef struct {
    float position;
    float velocity;
    float target;
    float stiffness;
    float damping;
    float mass;
} nx_spring_t;

void nx_spring_init(nx_spring_t* spring, float initial, float target, float stiffness, float damping);
void nx_spring_update(nx_spring_t* spring, float dt_ms);
bool nx_spring_is_settled(const nx_spring_t* spring, float threshold);

#ifdef __cplusplus
}
#endif
#endif
