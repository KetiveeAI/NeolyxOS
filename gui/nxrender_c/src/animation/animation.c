/*
 * NeolyxOS - NXRender Animation System Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "animation/animation.h"
#include <math.h>
#include <stddef.h>

/* Easing functions */
float nx_ease_linear(float t) { return t; }
float nx_ease_in_quad(float t) { return t * t; }
float nx_ease_out_quad(float t) { return t * (2.0f - t); }
float nx_ease_in_out_quad(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}
float nx_ease_in_cubic(float t) { return t * t * t; }
float nx_ease_out_cubic(float t) { float u = t - 1.0f; return u * u * u + 1.0f; }
float nx_ease_in_out_cubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}
float nx_ease_out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}
float nx_ease_out_elastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * (2.0f * 3.14159f / 3.0f)) + 1.0f;
}
float nx_ease_out_bounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1; return n1 * t * t + 0.984375f;
}

/* Animation lifecycle */
void nx_anim_init(nx_animation_t* anim, float from, float to, float duration_ms, nx_easing_fn_t easing) {
    if (!anim) return;
    anim->from = from;
    anim->to = to;
    anim->current = from;
    anim->duration_ms = duration_ms;
    anim->elapsed_ms = 0.0f;
    anim->easing = easing ? easing : nx_ease_linear;
    anim->state = NX_ANIM_IDLE;
    anim->on_complete = NULL;
    anim->user_data = NULL;
}

void nx_anim_start(nx_animation_t* anim) {
    if (anim) { anim->state = NX_ANIM_RUNNING; anim->elapsed_ms = 0.0f; }
}
void nx_anim_pause(nx_animation_t* anim) {
    if (anim && anim->state == NX_ANIM_RUNNING) anim->state = NX_ANIM_PAUSED;
}
void nx_anim_resume(nx_animation_t* anim) {
    if (anim && anim->state == NX_ANIM_PAUSED) anim->state = NX_ANIM_RUNNING;
}
void nx_anim_reset(nx_animation_t* anim) {
    if (anim) { anim->elapsed_ms = 0.0f; anim->current = anim->from; anim->state = NX_ANIM_IDLE; }
}

bool nx_anim_update(nx_animation_t* anim, float dt_ms) {
    if (!anim || anim->state != NX_ANIM_RUNNING) return false;
    anim->elapsed_ms += dt_ms;
    if (anim->elapsed_ms >= anim->duration_ms) {
        anim->elapsed_ms = anim->duration_ms;
        anim->current = anim->to;
        anim->state = NX_ANIM_FINISHED;
        if (anim->on_complete) anim->on_complete(anim->user_data);
        return true;
    }
    float t = anim->elapsed_ms / anim->duration_ms;
    float eased = anim->easing(t);
    anim->current = anim->from + (anim->to - anim->from) * eased;
    return true;
}

float nx_anim_value(const nx_animation_t* anim) { return anim ? anim->current : 0.0f; }
bool nx_anim_is_complete(const nx_animation_t* anim) { return anim && anim->state == NX_ANIM_FINISHED; }

/* Spring physics */
void nx_spring_init(nx_spring_t* spring, float initial, float target, float stiffness, float damping) {
    if (!spring) return;
    spring->position = initial;
    spring->velocity = 0.0f;
    spring->target = target;
    spring->stiffness = stiffness;
    spring->damping = damping;
    spring->mass = 1.0f;
}

void nx_spring_update(nx_spring_t* spring, float dt_ms) {
    if (!spring) return;
    float dt = dt_ms / 1000.0f;
    float displacement = spring->position - spring->target;
    float spring_force = -spring->stiffness * displacement;
    float damping_force = -spring->damping * spring->velocity;
    float acceleration = (spring_force + damping_force) / spring->mass;
    spring->velocity += acceleration * dt;
    spring->position += spring->velocity * dt;
}

bool nx_spring_is_settled(const nx_spring_t* spring, float threshold) {
    if (!spring) return true;
    float displacement = spring->position - spring->target;
    return fabsf(displacement) < threshold && fabsf(spring->velocity) < threshold;
}
