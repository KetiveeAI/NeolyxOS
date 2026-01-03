/*
 * NeolyxOS - NXRender Advanced Animation Implementation
 * High-precision, frame-perfect animation engine
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 199309L
#include "animation/animation_advanced.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ============================================================================
 * Bezier Curve Presets (CSS-compatible)
 * ============================================================================ */
const nx_bezier_t NX_BEZIER_LINEAR =       { 0.0f, 0.0f, 1.0f, 1.0f };
const nx_bezier_t NX_BEZIER_EASE =         { 0.25f, 0.1f, 0.25f, 1.0f };
const nx_bezier_t NX_BEZIER_EASE_IN =      { 0.42f, 0.0f, 1.0f, 1.0f };
const nx_bezier_t NX_BEZIER_EASE_OUT =     { 0.0f, 0.0f, 0.58f, 1.0f };
const nx_bezier_t NX_BEZIER_EASE_IN_OUT =  { 0.42f, 0.0f, 0.58f, 1.0f };
const nx_bezier_t NX_BEZIER_EASE_IN_BACK =   { 0.6f, -0.28f, 0.735f, 0.045f };
const nx_bezier_t NX_BEZIER_EASE_OUT_BACK =  { 0.175f, 0.885f, 0.32f, 1.275f };
const nx_bezier_t NX_BEZIER_EASE_IN_OUT_BACK = { 0.68f, -0.55f, 0.265f, 1.55f };

/* ============================================================================
 * High-Precision Time
 * ============================================================================ */
nx_time_ns_t nx_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (nx_time_ns_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

float nx_time_to_seconds(nx_time_ns_t ns) {
    return (float)ns / 1000000000.0f;
}

nx_time_ns_t nx_seconds_to_time(float seconds) {
    return (nx_time_ns_t)(seconds * 1000000000.0f);
}

/* ============================================================================
 * Cubic Bezier Evaluation
 * 
 * A cubic bezier curve is defined by 4 points:
 * P0 = (0, 0)     - Start point
 * P1 = (x1, y1)   - First control point
 * P2 = (x2, y2)   - Second control point
 * P3 = (1, 1)     - End point
 *
 * For a given time t, we solve for the X position, then sample Y.
 * This requires Newton-Raphson iteration for precision.
 * ============================================================================ */

static float bezier_sample_curve_x(const nx_bezier_t* c, float t) {
    /* X(t) = 3*(1-t)^2*t*x1 + 3*(1-t)*t^2*x2 + t^3 */
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0f - t;
    float mt2 = mt * mt;
    return 3.0f * mt2 * t * c->x1 + 3.0f * mt * t2 * c->x2 + t3;
}

static float bezier_sample_curve_y(const nx_bezier_t* c, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0f - t;
    float mt2 = mt * mt;
    return 3.0f * mt2 * t * c->y1 + 3.0f * mt * t2 * c->y2 + t3;
}

static float bezier_sample_curve_derivative_x(const nx_bezier_t* c, float t) {
    /* dX/dt = 3*(1-t)^2*x1 + 6*(1-t)*t*(x2-x1) + 3*t^2*(1-x2) */
    float mt = 1.0f - t;
    return 3.0f * mt * mt * c->x1 + 
           6.0f * mt * t * (c->x2 - c->x1) + 
           3.0f * t * t * (1.0f - c->x2);
}

float nx_bezier_eval(const nx_bezier_t* curve, float x) {
    if (!curve) return x;
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    
    /* Newton-Raphson to find t for given x */
    float t = x;  /* Initial guess */
    for (int i = 0; i < 8; i++) {
        float current_x = bezier_sample_curve_x(curve, t);
        float error = current_x - x;
        if (fabsf(error) < 0.0001f) break;
        
        float derivative = bezier_sample_curve_derivative_x(curve, t);
        if (fabsf(derivative) < 0.0001f) break;
        
        t -= error / derivative;
        t = fmaxf(0.0f, fminf(1.0f, t));
    }
    
    return bezier_sample_curve_y(curve, t);
}

/* ============================================================================
 * Keyframe Animation
 * ============================================================================ */

nx_keyframe_anim_t* nx_keyframe_anim_create(float duration_ms) {
    nx_keyframe_anim_t* anim = calloc(1, sizeof(nx_keyframe_anim_t));
    if (!anim) return NULL;
    anim->duration_ms = duration_ms;
    anim->playback_rate = 1.0f;
    return anim;
}

void nx_keyframe_anim_destroy(nx_keyframe_anim_t* anim) {
    free(anim);
}

void nx_keyframe_add(nx_keyframe_anim_t* anim, float time, float value, nx_bezier_t easing) {
    if (!anim || anim->keyframe_count >= NX_MAX_KEYFRAMES) return;
    
    /* Insert sorted by time */
    uint32_t insert_idx = anim->keyframe_count;
    for (uint32_t i = 0; i < anim->keyframe_count; i++) {
        if (anim->keyframes[i].time > time) {
            insert_idx = i;
            break;
        }
    }
    
    /* Shift existing keyframes */
    for (uint32_t i = anim->keyframe_count; i > insert_idx; i--) {
        anim->keyframes[i] = anim->keyframes[i - 1];
    }
    
    anim->keyframes[insert_idx] = (nx_keyframe_t){
        .time = time,
        .value = value,
        .easing = easing
    };
    anim->keyframe_count++;
}

void nx_keyframe_anim_play(nx_keyframe_anim_t* anim) {
    if (anim) anim->playing = true;
}

void nx_keyframe_anim_pause(nx_keyframe_anim_t* anim) {
    if (anim) anim->playing = false;
}

void nx_keyframe_anim_reset(nx_keyframe_anim_t* anim) {
    if (anim) { anim->elapsed_ms = 0; anim->playing = false; }
}

bool nx_keyframe_anim_update(nx_keyframe_anim_t* anim, float dt_ms) {
    if (!anim || !anim->playing) return false;
    
    anim->elapsed_ms += dt_ms * anim->playback_rate;
    
    float t = anim->elapsed_ms / anim->duration_ms;
    if (anim->reverse) t = 1.0f - t;
    
    if (anim->elapsed_ms >= anim->duration_ms) {
        if (anim->loop) {
            anim->elapsed_ms = fmodf(anim->elapsed_ms, anim->duration_ms);
        } else {
            anim->elapsed_ms = anim->duration_ms;
            anim->playing = false;
            if (anim->on_complete) anim->on_complete(anim->user_data);
            return true;
        }
    }
    
    /* Call update callback with current value */
    if (anim->on_update) {
        anim->on_update(nx_keyframe_anim_value(anim), anim->user_data);
    }
    
    return true;
}

float nx_keyframe_anim_value(const nx_keyframe_anim_t* anim) {
    if (!anim || anim->keyframe_count == 0) return 0.0f;
    if (anim->keyframe_count == 1) return anim->keyframes[0].value;
    
    float t = anim->elapsed_ms / anim->duration_ms;
    t = fmaxf(0.0f, fminf(1.0f, t));
    if (anim->reverse) t = 1.0f - t;
    
    /* Find surrounding keyframes */
    uint32_t next_idx = 0;
    for (uint32_t i = 0; i < anim->keyframe_count; i++) {
        if (anim->keyframes[i].time > t) {
            next_idx = i;
            break;
        }
        next_idx = i + 1;
    }
    
    if (next_idx == 0) return anim->keyframes[0].value;
    if (next_idx >= anim->keyframe_count) return anim->keyframes[anim->keyframe_count - 1].value;
    
    const nx_keyframe_t* prev = &anim->keyframes[next_idx - 1];
    const nx_keyframe_t* next = &anim->keyframes[next_idx];
    
    /* Interpolate between keyframes */
    float segment_t = (t - prev->time) / (next->time - prev->time);
    float eased_t = nx_bezier_eval(&next->easing, segment_t);
    
    return prev->value + (next->value - prev->value) * eased_t;
}

/* ============================================================================
 * Timeline
 * ============================================================================ */

nx_timeline_t* nx_timeline_create(void) {
    return calloc(1, sizeof(nx_timeline_t));
}

void nx_timeline_destroy(nx_timeline_t* timeline) {
    free(timeline);
}

void nx_timeline_add(nx_timeline_t* timeline, void* anim, float start_time) {
    if (!timeline || timeline->entry_count >= NX_MAX_TIMELINE_ENTRIES) return;
    
    timeline->entries[timeline->entry_count++] = (nx_timeline_entry_t){
        .animation = anim,
        .start_time = start_time,
        .duration = 0,
        .finished = false
    };
}

void nx_timeline_play(nx_timeline_t* timeline) {
    if (timeline) timeline->playing = true;
}

void nx_timeline_pause(nx_timeline_t* timeline) {
    if (timeline) timeline->playing = false;
}

void nx_timeline_reset(nx_timeline_t* timeline) {
    if (!timeline) return;
    timeline->elapsed_ms = 0;
    timeline->playing = false;
    for (uint32_t i = 0; i < timeline->entry_count; i++) {
        timeline->entries[i].finished = false;
    }
}

bool nx_timeline_update(nx_timeline_t* timeline, float dt_ms) {
    if (!timeline || !timeline->playing) return false;
    
    timeline->elapsed_ms += dt_ms;
    
    bool all_finished = true;
    for (uint32_t i = 0; i < timeline->entry_count; i++) {
        nx_timeline_entry_t* entry = &timeline->entries[i];
        if (entry->finished) continue;
        
        if (timeline->elapsed_ms >= entry->start_time) {
            float local_dt = timeline->elapsed_ms - entry->start_time;
            if (local_dt <= dt_ms) {
                /* Animation just started */
            }
            all_finished = false;
        }
    }
    
    if (all_finished && timeline->on_complete) {
        timeline->on_complete(timeline->user_data);
    }
    
    return !all_finished;
}

/* ============================================================================
 * Staggered Animation
 * ============================================================================ */

nx_stagger_t* nx_stagger_create(uint32_t count, float stagger_ms, float duration_ms) {
    nx_stagger_t* stagger = calloc(1, sizeof(nx_stagger_t));
    if (!stagger) return NULL;
    stagger->item_count = count;
    stagger->stagger_ms = stagger_ms;
    stagger->item_duration_ms = duration_ms;
    stagger->easing = NX_BEZIER_EASE_OUT;
    return stagger;
}

void nx_stagger_destroy(nx_stagger_t* stagger) {
    free(stagger);
}

void nx_stagger_play(nx_stagger_t* stagger) {
    if (stagger) stagger->playing = true;
}

bool nx_stagger_update(nx_stagger_t* stagger, float dt_ms) {
    if (!stagger || !stagger->playing) return false;
    stagger->elapsed_ms += dt_ms;
    
    float total = stagger->stagger_ms * (stagger->item_count - 1) + stagger->item_duration_ms;
    return stagger->elapsed_ms < total;
}

float nx_stagger_value_at(const nx_stagger_t* stagger, uint32_t index) {
    if (!stagger || index >= stagger->item_count) return 0.0f;
    
    float start_time = stagger->stagger_ms * index;
    float local_time = stagger->elapsed_ms - start_time;
    
    if (local_time < 0) return 0.0f;
    if (local_time >= stagger->item_duration_ms) return 1.0f;
    
    float t = local_time / stagger->item_duration_ms;
    return nx_bezier_eval(&stagger->easing, t);
}

/* ============================================================================
 * Advanced Spring Physics
 * ============================================================================ */

nx_spring_adv_t* nx_spring_adv_create(float mass, float stiffness, float damping) {
    nx_spring_adv_t* spring = calloc(1, sizeof(nx_spring_adv_t));
    if (!spring) return NULL;
    spring->mass = mass;
    spring->stiffness = stiffness;
    spring->damping = damping;
    spring->rest_threshold = 0.001f;
    spring->rest_velocity = 0.001f;
    
    /* Precompute physics constants */
    spring->omega = sqrtf(stiffness / mass);  /* Angular frequency */
    spring->zeta = damping / (2.0f * sqrtf(stiffness * mass));  /* Damping ratio */
    
    return spring;
}

nx_spring_adv_t* nx_spring_adv_from_preset(nx_spring_preset_t preset) {
    switch (preset) {
        case NX_SPRING_GENTLE:   return nx_spring_adv_create(1.0f, 120.0f, 14.0f);
        case NX_SPRING_WOBBLY:   return nx_spring_adv_create(1.0f, 180.0f, 12.0f);
        case NX_SPRING_STIFF:    return nx_spring_adv_create(1.0f, 400.0f, 28.0f);
        case NX_SPRING_SLOW:     return nx_spring_adv_create(1.0f, 200.0f, 26.0f);
        case NX_SPRING_MOLASSES: return nx_spring_adv_create(1.0f, 100.0f, 30.0f);
        default: return nx_spring_adv_create(1.0f, 170.0f, 20.0f);
    }
}

void nx_spring_adv_destroy(nx_spring_adv_t* spring) {
    free(spring);
}

void nx_spring_adv_set_target(nx_spring_adv_t* spring, float target) {
    if (spring) {
        spring->target = target;
        spring->settled = false;
    }
}

void nx_spring_adv_update(nx_spring_adv_t* spring, float dt_ms) {
    if (!spring || spring->settled) return;
    
    float dt = dt_ms / 1000.0f;
    
    /* Spring force: F = -k * x */
    float displacement = spring->position - spring->target;
    float spring_force = -spring->stiffness * displacement;
    
    /* Damping force: F = -c * v */
    float damping_force = -spring->damping * spring->velocity;
    
    /* Total acceleration: a = F / m */
    float acceleration = (spring_force + damping_force) / spring->mass;
    
    /* Integrate velocity and position */
    spring->velocity += acceleration * dt;
    spring->position += spring->velocity * dt;
    
    /* Check if settled */
    if (fabsf(displacement) < spring->rest_threshold &&
        fabsf(spring->velocity) < spring->rest_velocity) {
        spring->position = spring->target;
        spring->velocity = 0;
        spring->settled = true;
    }
}

float nx_spring_adv_value(const nx_spring_adv_t* spring) {
    return spring ? spring->position : 0.0f;
}

bool nx_spring_adv_is_settled(const nx_spring_adv_t* spring) {
    return spring ? spring->settled : true;
}

/* ============================================================================
 * Animation Controller
 * ============================================================================ */

nx_anim_controller_t* nx_anim_controller_create(void) {
    nx_anim_controller_t* ctrl = calloc(1, sizeof(nx_anim_controller_t));
    if (!ctrl) return NULL;
    ctrl->last_update = nx_time_now();
    ctrl->time_scale = 1.0f;
    return ctrl;
}

void nx_anim_controller_destroy(nx_anim_controller_t* ctrl) {
    free(ctrl);
}

void nx_anim_controller_add(nx_anim_controller_t* ctrl, void* anim, uint32_t type) {
    if (!ctrl || ctrl->count >= NX_MAX_ACTIVE_ANIMS) return;
    ctrl->animations[ctrl->count] = anim;
    ctrl->anim_types[ctrl->count] = type;
    ctrl->count++;
}

void nx_anim_controller_remove(nx_anim_controller_t* ctrl, void* anim) {
    if (!ctrl) return;
    for (uint32_t i = 0; i < ctrl->count; i++) {
        if (ctrl->animations[i] == anim) {
            for (uint32_t j = i; j < ctrl->count - 1; j++) {
                ctrl->animations[j] = ctrl->animations[j + 1];
                ctrl->anim_types[j] = ctrl->anim_types[j + 1];
            }
            ctrl->count--;
            return;
        }
    }
}

void nx_anim_controller_update(nx_anim_controller_t* ctrl) {
    if (!ctrl) return;
    
    nx_time_ns_t now = nx_time_now();
    float dt_ms = (float)(now - ctrl->last_update) / 1000000.0f;  /* ns to ms */
    dt_ms *= ctrl->time_scale;
    ctrl->last_update = now;
    
    /* Update each animation based on type */
    for (uint32_t i = 0; i < ctrl->count; i++) {
        /* Type-specific update would go here */
        (void)ctrl->animations[i];
        (void)ctrl->anim_types[i];
    }
}

/* Property binding (placeholder implementation) */
void nx_bind_animation(void* widget, nx_anim_property_t prop, void* animation, float (*getter)(void*)) {
    (void)widget; (void)prop; (void)animation; (void)getter;
    /* Would register binding for automatic property updates */
}
