/*
 * NeolyxOS Calendar - Animation System Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "animation.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Math helpers */
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Easing functions implementation */
float cal_ease(cal_easing_t easing, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    
    switch (easing) {
        case EASE_LINEAR:
            return t;
            
        case EASE_IN:
        case EASE_IN_QUAD:
            return t * t;
            
        case EASE_OUT:
        case EASE_OUT_QUAD:
            return 1.0f - (1.0f - t) * (1.0f - t);
            
        case EASE_IN_OUT:
        case EASE_IN_OUT_QUAD:
            return t < 0.5f 
                ? 2.0f * t * t 
                : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
            
        case EASE_IN_EXPO:
            return t == 0.0f ? 0.0f : powf(2.0f, 10.0f * t - 10.0f);
            
        case EASE_OUT_EXPO:
            return t == 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
            
        case EASE_IN_BACK: {
            float c1 = 1.70158f;
            float c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
            
        case EASE_OUT_BACK: {
            float c1 = 1.70158f;
            float c3 = c1 + 1.0f;
            float tm = t - 1.0f;
            return 1.0f + c3 * tm * tm * tm + c1 * tm * tm;
        }
            
        case EASE_BOUNCE: {
            float n1 = 7.5625f;
            float d1 = 2.75f;
            
            if (t < 1.0f / d1) {
                return n1 * t * t;
            } else if (t < 2.0f / d1) {
                float tv = t - 1.5f / d1;
                return n1 * tv * tv + 0.75f;
            } else if (t < 2.5f / d1) {
                float tv = t - 2.25f / d1;
                return n1 * tv * tv + 0.9375f;
            } else {
                float tv = t - 2.625f / d1;
                return n1 * tv * tv + 0.984375f;
            }
        }
            
        case EASE_SPRING:
            /* Spring is handled separately with physics */
            return t;
            
        default:
            return t;
    }
}

/* Spring presets */
cal_spring_config_t cal_spring_default(void) {
    return (cal_spring_config_t){.stiffness = 200.0f, .damping = 20.0f, .mass = 1.0f};
}

cal_spring_config_t cal_spring_bouncy(void) {
    return (cal_spring_config_t){.stiffness = 300.0f, .damping = 10.0f, .mass = 1.0f};
}

cal_spring_config_t cal_spring_stiff(void) {
    return (cal_spring_config_t){.stiffness = 500.0f, .damping = 30.0f, .mass = 1.0f};
}

/* Spring physics update */
float cal_spring_update(cal_animation_t *anim, float dt) {
    float target = anim->to;
    float current = anim->current;
    float velocity = anim->spring_velocity;
    
    float stiffness = anim->spring.stiffness;
    float damping = anim->spring.damping;
    float mass = anim->spring.mass;
    
    /* Spring force: F = -k * x - d * v */
    float displacement = current - target;
    float spring_force = -stiffness * displacement;
    float damping_force = -damping * velocity;
    float acceleration = (spring_force + damping_force) / mass;
    
    /* Integrate */
    velocity += acceleration * dt;
    current += velocity * dt;
    
    anim->spring_velocity = velocity;
    anim->current = current;
    
    /* Check if settled (velocity and displacement near zero) */
    if (fabsf(velocity) < 0.01f && fabsf(displacement) < 0.01f) {
        anim->current = target;
        anim->state = ANIM_FINISHED;
    }
    
    return current;
}

/* Create animation */
cal_animation_t *cal_anim_create(cal_anim_property_t prop, float from, float to, float duration_ms, cal_easing_t easing) {
    cal_animation_t *anim = (cal_animation_t *)malloc(sizeof(cal_animation_t));
    if (!anim) return NULL;
    
    memset(anim, 0, sizeof(cal_animation_t));
    anim->property = prop;
    anim->from = from;
    anim->to = to;
    anim->current = from;
    anim->duration_ms = duration_ms;
    anim->elapsed_ms = 0.0f;
    anim->easing = easing;
    anim->state = ANIM_IDLE;
    anim->spring = cal_spring_default();
    anim->spring_velocity = 0.0f;
    
    return anim;
}

/* Destroy animation */
void cal_anim_destroy(cal_animation_t *anim) {
    if (anim) free(anim);
}

/* Start animation */
void cal_anim_start(cal_animation_t *anim) {
    if (!anim) return;
    anim->elapsed_ms = 0.0f;
    anim->current = anim->from;
    anim->spring_velocity = 0.0f;
    anim->state = ANIM_RUNNING;
}

/* Pause animation */
void cal_anim_pause(cal_animation_t *anim) {
    if (anim && anim->state == ANIM_RUNNING) {
        anim->state = ANIM_PAUSED;
    }
}

/* Resume animation */
void cal_anim_resume(cal_animation_t *anim) {
    if (anim && anim->state == ANIM_PAUSED) {
        anim->state = ANIM_RUNNING;
    }
}

/* Reset animation */
void cal_anim_reset(cal_animation_t *anim) {
    if (!anim) return;
    anim->elapsed_ms = 0.0f;
    anim->current = anim->from;
    anim->spring_velocity = 0.0f;
    anim->state = ANIM_IDLE;
}

/* Create animator */
cal_animator_t *cal_animator_create(void) {
    cal_animator_t *animator = (cal_animator_t *)malloc(sizeof(cal_animator_t));
    if (!animator) return NULL;
    
    animator->active_capacity = 16;
    animator->active = (cal_animation_t **)malloc(sizeof(cal_animation_t *) * animator->active_capacity);
    animator->active_count = 0;
    animator->last_tick = 0;
    
    return animator;
}

/* Destroy animator */
void cal_animator_destroy(cal_animator_t *animator) {
    if (!animator) return;
    
    for (int i = 0; i < animator->active_count; i++) {
        cal_anim_destroy(animator->active[i]);
    }
    
    free(animator->active);
    free(animator);
}

/* Add animation to scheduler */
void cal_animator_add(cal_animator_t *animator, cal_animation_t *anim) {
    if (!animator || !anim) return;
    
    if (animator->active_count >= animator->active_capacity) {
        animator->active_capacity *= 2;
        animator->active = (cal_animation_t **)realloc(animator->active, 
            sizeof(cal_animation_t *) * animator->active_capacity);
    }
    
    animator->active[animator->active_count++] = anim;
}

/* Update all animations */
bool cal_animator_update(cal_animator_t *animator, uint64_t current_tick) {
    if (!animator) return false;
    
    /* Calculate delta time */
    float dt_ms = (animator->last_tick == 0) ? CAL_ANIM_FRAME_MS : (float)(current_tick - animator->last_tick);
    animator->last_tick = current_tick;
    
    bool any_active = false;
    
    for (int i = 0; i < animator->active_count; i++) {
        cal_animation_t *anim = animator->active[i];
        if (!anim || anim->state != ANIM_RUNNING) continue;
        
        any_active = true;
        
        if (anim->easing == EASE_SPRING) {
            /* Spring physics */
            cal_spring_update(anim, dt_ms / 1000.0f);
        } else {
            /* Normal easing */
            anim->elapsed_ms += dt_ms;
            float progress = anim->elapsed_ms / anim->duration_ms;
            
            if (progress >= 1.0f) {
                anim->current = anim->to;
                anim->state = ANIM_FINISHED;
                
                if (anim->on_complete) {
                    anim->on_complete(anim->callback_ctx);
                }
            } else {
                float eased = cal_ease(anim->easing, progress);
                anim->current = anim->from + (anim->to - anim->from) * eased;
            }
        }
    }
    
    /* Remove finished animations */
    int write = 0;
    for (int read = 0; read < animator->active_count; read++) {
        if (animator->active[read]->state != ANIM_FINISHED) {
            animator->active[write++] = animator->active[read];
        }
    }
    animator->active_count = write;
    
    return any_active;
}

/* Preset: Fade in */
cal_animation_t *cal_anim_fade_in(float duration_ms) {
    return cal_anim_create(ANIM_PROP_OPACITY, 0.0f, 1.0f, duration_ms, EASE_OUT);
}

/* Preset: Fade out */
cal_animation_t *cal_anim_fade_out(float duration_ms) {
    return cal_anim_create(ANIM_PROP_OPACITY, 1.0f, 0.0f, duration_ms, EASE_IN);
}

/* Preset: Slide in */
cal_animation_t *cal_anim_slide_in(int direction, float distance, float duration_ms) {
    cal_anim_property_t prop = (direction == 0 || direction == 1) ? ANIM_PROP_X : ANIM_PROP_Y;
    float from = (direction == 0 || direction == 2) ? -distance : distance;
    return cal_anim_create(prop, from, 0.0f, duration_ms, EASE_OUT_BACK);
}

/* Preset: Scale in */
cal_animation_t *cal_anim_scale_in(float duration_ms) {
    return cal_anim_create(ANIM_PROP_SCALE, 0.0f, 1.0f, duration_ms, EASE_OUT_BACK);
}

/* Preset: Bounce */
cal_animation_t *cal_anim_bounce(float duration_ms) {
    return cal_anim_create(ANIM_PROP_SCALE, 1.0f, 1.2f, duration_ms, EASE_BOUNCE);
}

/* Preset: Pulse */
cal_animation_t *cal_anim_pulse(float duration_ms) {
    return cal_anim_create(ANIM_PROP_OPACITY, 0.5f, 1.0f, duration_ms, EASE_IN_OUT);
}

/* Color interpolation */
uint32_t cal_color_lerp(uint32_t from, uint32_t to, float t) {
    if (t <= 0.0f) return from;
    if (t >= 1.0f) return to;
    
    uint8_t a1 = (from >> 24) & 0xFF;
    uint8_t r1 = (from >> 16) & 0xFF;
    uint8_t g1 = (from >> 8) & 0xFF;
    uint8_t b1 = from & 0xFF;
    
    uint8_t a2 = (to >> 24) & 0xFF;
    uint8_t r2 = (to >> 16) & 0xFF;
    uint8_t g2 = (to >> 8) & 0xFF;
    uint8_t b2 = to & 0xFF;
    
    uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
    
    return (a << 24) | (r << 16) | (g << 8) | b;
}
