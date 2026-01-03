/*
 * REOX Animation System - Implementation
 * Smooth animations for NeolyxOS UI
 */

#include "reox_animation.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Easing Function Implementation
 * ============================================================================ */

/* Helper functions */
static float ease_in_quad(float t) { return t * t; }
static float ease_out_quad(float t) { return 1 - (1 - t) * (1 - t); }
static float ease_in_out_quad(float t) {
    return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2;
}

static float ease_in_cubic(float t) { return t * t * t; }
static float ease_out_cubic(float t) { return 1 - powf(1 - t, 3); }
static float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

static float ease_in_quart(float t) { return t * t * t * t; }
static float ease_out_quart(float t) { return 1 - powf(1 - t, 4); }
static float ease_in_out_quart(float t) {
    return t < 0.5f ? 8 * t * t * t * t : 1 - powf(-2 * t + 2, 4) / 2;
}

static float ease_in_expo(float t) {
    return t == 0 ? 0 : powf(2, 10 * t - 10);
}
static float ease_out_expo(float t) {
    return t == 1 ? 1 : 1 - powf(2, -10 * t);
}
static float ease_in_out_expo(float t) {
    if (t == 0) return 0;
    if (t == 1) return 1;
    return t < 0.5f ? powf(2, 20 * t - 10) / 2 : (2 - powf(2, -20 * t + 10)) / 2;
}

static float ease_in_back(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1;
    return c3 * t * t * t - c1 * t * t;
}
static float ease_out_back(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
static float ease_in_out_back(float t) {
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;
    return t < 0.5f 
        ? (powf(2 * t, 2) * ((c2 + 1) * 2 * t - c2)) / 2
        : (powf(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
}

static float ease_in_elastic(float t) {
    const float c4 = (2 * M_PI) / 3;
    if (t == 0) return 0;
    if (t == 1) return 1;
    return -powf(2, 10 * t - 10) * sinf((t * 10 - 10.75f) * c4);
}
static float ease_out_elastic(float t) {
    const float c4 = (2 * M_PI) / 3;
    if (t == 0) return 0;
    if (t == 1) return 1;
    return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * c4) + 1;
}
static float ease_in_out_elastic(float t) {
    const float c5 = (2 * M_PI) / 4.5f;
    if (t == 0) return 0;
    if (t == 1) return 1;
    return t < 0.5f
        ? -(powf(2, 20 * t - 10) * sinf((20 * t - 11.125f) * c5)) / 2
        : (powf(2, -20 * t + 10) * sinf((20 * t - 11.125f) * c5)) / 2 + 1;
}

static float ease_bounce_out(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    
    if (t < 1 / d1) {
        return n1 * t * t;
    } else if (t < 2 / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

float rx_ease(rx_easing easing, float t) {
    /* Clamp t */
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    
    switch (easing) {
        case RX_EASE_LINEAR:        return t;
        case RX_EASE_IN:            return ease_in_cubic(t);
        case RX_EASE_OUT:           return ease_out_cubic(t);
        case RX_EASE_IN_OUT:        return ease_in_out_cubic(t);
        case RX_EASE_IN_QUAD:       return ease_in_quad(t);
        case RX_EASE_OUT_QUAD:      return ease_out_quad(t);
        case RX_EASE_IN_OUT_QUAD:   return ease_in_out_quad(t);
        case RX_EASE_IN_CUBIC:      return ease_in_cubic(t);
        case RX_EASE_OUT_CUBIC:     return ease_out_cubic(t);
        case RX_EASE_IN_OUT_CUBIC:  return ease_in_out_cubic(t);
        case RX_EASE_IN_QUART:      return ease_in_quart(t);
        case RX_EASE_OUT_QUART:     return ease_out_quart(t);
        case RX_EASE_IN_OUT_QUART:  return ease_in_out_quart(t);
        case RX_EASE_IN_EXPO:       return ease_in_expo(t);
        case RX_EASE_OUT_EXPO:      return ease_out_expo(t);
        case RX_EASE_IN_OUT_EXPO:   return ease_in_out_expo(t);
        case RX_EASE_IN_BACK:       return ease_in_back(t);
        case RX_EASE_OUT_BACK:      return ease_out_back(t);
        case RX_EASE_IN_OUT_BACK:   return ease_in_out_back(t);
        case RX_EASE_IN_ELASTIC:    return ease_in_elastic(t);
        case RX_EASE_OUT_ELASTIC:   return ease_out_elastic(t);
        case RX_EASE_IN_OUT_ELASTIC: return ease_in_out_elastic(t);
        case RX_EASE_BOUNCE:        return ease_bounce_out(t);
        case RX_EASE_SPRING:        return ease_out_elastic(t);  /* Approximation */
        default:                    return t;
    }
}

float rx_ease_bezier(float t, float x1, float y1, float x2, float y2) {
    /* Cubic bezier approximation */
    float cx = 3.0f * x1;
    float bx = 3.0f * (x2 - x1) - cx;
    float ax = 1.0f - cx - bx;
    
    float cy = 3.0f * y1;
    float by = 3.0f * (y2 - y1) - cy;
    float ay = 1.0f - cy - by;
    
    /* Newton-Raphson iteration to find t for x */
    float guess = t;
    for (int i = 0; i < 5; i++) {
        float x = ((ax * guess + bx) * guess + cx) * guess;
        float dx = (3.0f * ax * guess + 2.0f * bx) * guess + cx;
        if (fabsf(dx) < 1e-6f) break;
        guess -= (x - t) / dx;
    }
    
    /* Return y for found t */
    return ((ay * guess + by) * guess + cy) * guess;
}

/* ============================================================================
 * Spring Physics Implementation
 * ============================================================================ */

rx_spring_config rx_spring_default(void) {
    return (rx_spring_config){ .stiffness = 170, .damping = 26, .mass = 1, .velocity = 0 };
}

rx_spring_config rx_spring_bouncy(void) {
    return (rx_spring_config){ .stiffness = 180, .damping = 12, .mass = 1, .velocity = 0 };
}

rx_spring_config rx_spring_stiff(void) {
    return (rx_spring_config){ .stiffness = 400, .damping = 40, .mass = 1, .velocity = 0 };
}

rx_spring_config rx_spring_gentle(void) {
    return (rx_spring_config){ .stiffness = 120, .damping = 14, .mass = 1, .velocity = 0 };
}

rx_spring* rx_spring_create(float initial, rx_spring_config config) {
    rx_spring* s = (rx_spring*)calloc(1, sizeof(rx_spring));
    if (!s) return NULL;
    
    s->current = initial;
    s->target = initial;
    s->velocity = config.velocity;
    s->config = config;
    s->settled = true;
    
    return s;
}

void rx_spring_set_target(rx_spring* spring, float target) {
    if (!spring) return;
    spring->target = target;
    spring->settled = false;
}

float rx_spring_update(rx_spring* spring, float dt) {
    if (!spring || spring->settled) return spring ? spring->current : 0;
    
    /* Spring force: F = -kx - cv */
    float displacement = spring->current - spring->target;
    float spring_force = -spring->config.stiffness * displacement;
    float damping_force = -spring->config.damping * spring->velocity;
    
    /* Acceleration: a = F/m */
    float acceleration = (spring_force + damping_force) / spring->config.mass;
    
    /* Semi-implicit Euler integration */
    spring->velocity += acceleration * dt;
    spring->current += spring->velocity * dt;
    
    /* Check if settled */
    if (fabsf(displacement) < 0.001f && fabsf(spring->velocity) < 0.001f) {
        spring->current = spring->target;
        spring->velocity = 0;
        spring->settled = true;
    }
    
    return spring->current;
}

bool rx_spring_is_settled(rx_spring* spring) {
    return spring ? spring->settled : true;
}

void rx_spring_destroy(rx_spring* spring) {
    free(spring);
}

/* ============================================================================
 * Animation Implementation
 * ============================================================================ */

static uint64_t anim_next_id = 1;

rx_animation* rx_animate(float from, float to, float duration) {
    rx_animation* anim = (rx_animation*)calloc(1, sizeof(rx_animation));
    if (!anim) return NULL;
    
    anim->id = anim_next_id++;
    anim->from = from;
    anim->to = to;
    anim->current = from;
    anim->duration = duration;
    anim->elapsed = 0;
    anim->delay = 0;
    anim->easing = RX_EASE_OUT;
    anim->state = RX_ANIM_IDLE;
    anim->repeat_count = 0;
    anim->current_repeat = 0;
    
    return anim;
}

rx_animation* rx_animate_spring(float from, float to, rx_spring_config config) {
    rx_animation* anim = rx_animate(from, to, 1.0f);  /* Duration approximated */
    if (anim) {
        anim->easing = RX_EASE_SPRING;
    }
    return anim;
}

rx_animation* rx_anim_easing(rx_animation* anim, rx_easing easing) {
    if (anim) anim->easing = easing;
    return anim;
}

rx_animation* rx_anim_delay(rx_animation* anim, float delay) {
    if (anim) anim->delay = delay;
    return anim;
}

rx_animation* rx_anim_repeat(rx_animation* anim, int count) {
    if (anim) anim->repeat_count = count;
    return anim;
}

rx_animation* rx_anim_auto_reverse(rx_animation* anim, bool reverse) {
    if (anim) anim->auto_reverse = reverse;
    return anim;
}

rx_animation* rx_anim_on_update(rx_animation* anim, rx_anim_callback cb, void* data) {
    if (anim) {
        anim->on_update = cb;
        anim->user_data = data;
    }
    return anim;
}

rx_animation* rx_anim_on_complete(rx_animation* anim, rx_anim_complete_callback cb, void* data) {
    if (anim) {
        anim->on_complete = cb;
        if (!anim->user_data) anim->user_data = data;
    }
    return anim;
}

void rx_anim_start(rx_animation* anim) {
    if (anim) anim->state = RX_ANIM_RUNNING;
}

void rx_anim_pause(rx_animation* anim) {
    if (anim && anim->state == RX_ANIM_RUNNING) {
        anim->state = RX_ANIM_PAUSED;
    }
}

void rx_anim_resume(rx_animation* anim) {
    if (anim && anim->state == RX_ANIM_PAUSED) {
        anim->state = RX_ANIM_RUNNING;
    }
}

void rx_anim_stop(rx_animation* anim) {
    if (anim) {
        anim->state = RX_ANIM_IDLE;
        anim->elapsed = 0;
        anim->current = anim->from;
    }
}

void rx_anim_reset(rx_animation* anim) {
    if (!anim) return;
    anim->elapsed = 0;
    anim->current = anim->from;
    anim->current_repeat = 0;
    anim->reversed = false;
}

bool rx_anim_update(rx_animation* anim, float dt) {
    if (!anim || anim->state != RX_ANIM_RUNNING) return false;
    
    /* Handle delay */
    if (anim->delay > 0) {
        anim->delay -= dt;
        return true;
    }
    
    /* Update elapsed time */
    anim->elapsed += dt;
    
    /* Calculate progress */
    float progress = anim->duration > 0 ? anim->elapsed / anim->duration : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    
    /* Apply easing */
    float eased = rx_ease(anim->easing, anim->reversed ? 1.0f - progress : progress);
    
    /* Calculate current value */
    anim->current = anim->from + (anim->to - anim->from) * eased;
    
    /* Callback */
    if (anim->on_update) {
        anim->on_update(anim->current, anim->user_data);
    }
    
    /* Check completion */
    if (progress >= 1.0f) {
        if (anim->auto_reverse && !anim->reversed) {
            anim->reversed = true;
            anim->elapsed = 0;
        } else if (anim->repeat_count < 0 || anim->current_repeat < anim->repeat_count) {
            anim->current_repeat++;
            anim->elapsed = 0;
            anim->reversed = false;
        } else {
            anim->state = RX_ANIM_COMPLETED;
            if (anim->on_complete) {
                anim->on_complete(anim->user_data);
            }
            return false;
        }
    }
    
    return true;
}

void rx_anim_destroy(rx_animation* anim) {
    free(anim);
}

/* ============================================================================
 * Keyframe Animation Implementation
 * ============================================================================ */

rx_keyframe_animation* rx_keyframes_create(float duration) {
    rx_keyframe_animation* anim = (rx_keyframe_animation*)calloc(1, sizeof(rx_keyframe_animation));
    if (!anim) return NULL;
    
    anim->base.duration = duration;
    anim->base.state = RX_ANIM_IDLE;
    anim->use_percentages = true;
    
    return anim;
}

void rx_keyframes_add(rx_keyframe_animation* anim, float time, float value, rx_easing easing) {
    if (!anim) return;
    
    anim->keyframes = (rx_keyframe*)realloc(
        anim->keyframes,
        sizeof(rx_keyframe) * (anim->keyframe_count + 1)
    );
    
    rx_keyframe* kf = &anim->keyframes[anim->keyframe_count];
    kf->time = time;
    kf->value = value;
    kf->easing = easing;
    anim->keyframe_count++;
}

float rx_keyframes_get_value(rx_keyframe_animation* anim, float t) {
    if (!anim || anim->keyframe_count == 0) return 0;
    
    /* Find surrounding keyframes */
    rx_keyframe* prev = &anim->keyframes[0];
    rx_keyframe* next = prev;
    
    for (size_t i = 0; i < anim->keyframe_count; i++) {
        if (anim->keyframes[i].time <= t) {
            prev = &anim->keyframes[i];
        }
        if (anim->keyframes[i].time >= t) {
            next = &anim->keyframes[i];
            break;
        }
    }
    
    /* Interpolate */
    if (prev == next || next->time == prev->time) {
        return prev->value;
    }
    
    float local_t = (t - prev->time) / (next->time - prev->time);
    float eased = rx_ease(next->easing, local_t);
    return prev->value + (next->value - prev->value) * eased;
}

void rx_keyframes_destroy(rx_keyframe_animation* anim) {
    if (anim) {
        free(anim->keyframes);
        free(anim);
    }
}

/* ============================================================================
 * Color Animation Implementation
 * ============================================================================ */

rx_color_animation* rx_animate_color(rx_color from, rx_color to, float duration) {
    rx_color_animation* anim = (rx_color_animation*)calloc(1, sizeof(rx_color_animation));
    if (!anim) return NULL;
    
    anim->base.duration = duration;
    anim->base.state = RX_ANIM_IDLE;
    anim->base.easing = RX_EASE_OUT;
    anim->from_color = from;
    anim->to_color = to;
    anim->current_color = from;
    
    return anim;
}

rx_color rx_color_anim_get(rx_color_animation* anim) {
    if (!anim) return (rx_color){0, 0, 0, 0};
    
    float t = anim->base.duration > 0 ? anim->base.elapsed / anim->base.duration : 1.0f;
    if (t > 1.0f) t = 1.0f;
    float eased = rx_ease(anim->base.easing, t);
    
    return (rx_color){
        (uint8_t)(anim->from_color.r + (anim->to_color.r - anim->from_color.r) * eased),
        (uint8_t)(anim->from_color.g + (anim->to_color.g - anim->from_color.g) * eased),
        (uint8_t)(anim->from_color.b + (anim->to_color.b - anim->from_color.b) * eased),
        (uint8_t)(anim->from_color.a + (anim->to_color.a - anim->from_color.a) * eased)
    };
}

/* ============================================================================
 * View Animation Implementation
 * ============================================================================ */

rx_animation* rx_view_animate(rx_view* view, rx_animatable_property prop,
                               float to, float duration) {
    if (!view) return NULL;
    
    float from = 0;
    switch (prop) {
        case RX_PROP_X: from = view->box.frame.x; break;
        case RX_PROP_Y: from = view->box.frame.y; break;
        case RX_PROP_WIDTH: from = view->box.frame.width; break;
        case RX_PROP_HEIGHT: from = view->box.frame.height; break;
        case RX_PROP_CORNER_RADIUS: from = view->box.corner_radius.top_left; break;
        default: from = 0; break;
    }
    
    return rx_animate(from, to, duration);
}

rx_animation* rx_view_animate_from(rx_view* view, rx_animatable_property prop,
                                    float from, float to, float duration) {
    return rx_animate(from, to, duration);
}

rx_animation* rx_view_fade_in(rx_view* view, float duration) {
    return rx_animate(0.0f, 1.0f, duration);
}

rx_animation* rx_view_fade_out(rx_view* view, float duration) {
    return rx_animate(1.0f, 0.0f, duration);
}

rx_animation* rx_view_slide_in(rx_view* view, float from_x, float from_y, float duration) {
    /* Would return group of X and Y animations */
    return rx_animate(from_y, view ? view->box.frame.y : 0, duration);
}

rx_animation* rx_view_slide_out(rx_view* view, float to_x, float to_y, float duration) {
    return rx_animate(view ? view->box.frame.y : 0, to_y, duration);
}

rx_animation* rx_view_scale_in(rx_view* view, float duration) {
    rx_animation* anim = rx_animate(0.0f, 1.0f, duration);
    if (anim) anim->easing = RX_EASE_OUT_BACK;
    return anim;
}

rx_animation* rx_view_scale_out(rx_view* view, float duration) {
    rx_animation* anim = rx_animate(1.0f, 0.0f, duration);
    if (anim) anim->easing = RX_EASE_IN_BACK;
    return anim;
}

rx_animation* rx_view_bounce(rx_view* view, float duration) {
    rx_animation* anim = rx_animate(1.0f, 1.2f, duration * 0.5f);
    if (anim) {
        anim->auto_reverse = true;
        anim->easing = RX_EASE_OUT;
    }
    return anim;
}

rx_animation* rx_view_shake(rx_view* view, float duration) {
    rx_animation* anim = rx_animate(-10.0f, 10.0f, duration * 0.1f);
    if (anim) {
        anim->repeat_count = 4;
        anim->auto_reverse = true;
    }
    return anim;
}

rx_animation* rx_view_pulse(rx_view* view, float duration) {
    rx_animation* anim = rx_animate(1.0f, 0.5f, duration * 0.5f);
    if (anim) {
        anim->auto_reverse = true;
        anim->repeat_count = -1;  /* Infinite */
        anim->easing = RX_EASE_IN_OUT;
    }
    return anim;
}

/* ============================================================================
 * Animator Implementation
 * ============================================================================ */

static rx_animator* shared_animator = NULL;

rx_animator* rx_animator_shared(void) {
    if (!shared_animator) {
        shared_animator = rx_animator_create();
    }
    return shared_animator;
}

rx_animator* rx_animator_create(void) {
    rx_animator* animator = (rx_animator*)calloc(1, sizeof(rx_animator));
    if (!animator) return NULL;
    
    animator->time_scale = 1.0f;
    animator->paused = false;
    
    return animator;
}

void rx_animator_destroy(rx_animator* animator) {
    if (!animator) return;
    
    /* Free all animations */
    rx_animation* anim = animator->animations;
    while (anim) {
        rx_animation* next = anim->next;
        rx_anim_destroy(anim);
        anim = next;
    }
    
    free(animator);
}

void rx_animator_add(rx_animator* animator, rx_animation* anim) {
    if (!animator || !anim) return;
    
    anim->next = animator->animations;
    animator->animations = anim;
    animator->animation_count++;
    
    rx_anim_start(anim);
}

void rx_animator_remove(rx_animator* animator, rx_animation* anim) {
    if (!animator || !anim) return;
    
    rx_animation** ptr = &animator->animations;
    while (*ptr) {
        if (*ptr == anim) {
            *ptr = anim->next;
            animator->animation_count--;
            rx_anim_destroy(anim);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

void rx_animator_cancel_all(rx_animator* animator) {
    if (!animator) return;
    
    rx_animation* anim = animator->animations;
    while (anim) {
        rx_animation* next = anim->next;
        rx_anim_destroy(anim);
        anim = next;
    }
    
    animator->animations = NULL;
    animator->animation_count = 0;
}

void rx_animator_update(rx_animator* animator, float dt) {
    if (!animator || animator->paused) return;
    
    dt *= animator->time_scale;
    
    rx_animation** ptr = &animator->animations;
    while (*ptr) {
        rx_animation* anim = *ptr;
        
        if (!rx_anim_update(anim, dt)) {
            /* Animation completed, remove it */
            *ptr = anim->next;
            animator->animation_count--;
            rx_anim_destroy(anim);
        } else {
            ptr = &anim->next;
        }
    }
}

void rx_animator_pause(rx_animator* animator) {
    if (animator) animator->paused = true;
}

void rx_animator_resume(rx_animator* animator) {
    if (animator) animator->paused = false;
}

void rx_animator_set_time_scale(rx_animator* animator, float scale) {
    if (animator) animator->time_scale = scale;
}

/* ============================================================================
 * Animation Groups
 * ============================================================================ */

rx_animation* rx_anim_then(rx_animation* first, rx_animation* second) {
    if (!first) return second;
    if (!second) return first;
    
    second->delay = first->duration;
    return first;
}

rx_animation_group* rx_anim_group(rx_animation** anims, size_t count) {
    rx_animation_group* group = (rx_animation_group*)calloc(1, sizeof(rx_animation_group));
    if (!group) return NULL;
    
    group->animations = (rx_animation**)malloc(sizeof(rx_animation*) * count);
    memcpy(group->animations, anims, sizeof(rx_animation*) * count);
    group->count = count;
    group->state = RX_ANIM_IDLE;
    
    return group;
}

void rx_anim_group_start(rx_animation_group* group) {
    if (!group) return;
    group->state = RX_ANIM_RUNNING;
    for (size_t i = 0; i < group->count; i++) {
        rx_anim_start(group->animations[i]);
    }
}

bool rx_anim_group_update(rx_animation_group* group, float dt) {
    if (!group || group->state != RX_ANIM_RUNNING) return false;
    
    bool any_running = false;
    for (size_t i = 0; i < group->count; i++) {
        if (rx_anim_update(group->animations[i], dt)) {
            any_running = true;
        }
    }
    
    if (!any_running) {
        group->state = RX_ANIM_COMPLETED;
    }
    
    return any_running;
}

void rx_anim_group_destroy(rx_animation_group* group) {
    if (!group) return;
    for (size_t i = 0; i < group->count; i++) {
        rx_anim_destroy(group->animations[i]);
    }
    free(group->animations);
    free(group);
}

void rx_anim_stagger(rx_animation** anims, size_t count, float delay_between) {
    for (size_t i = 0; i < count; i++) {
        if (anims[i]) {
            anims[i]->delay += delay_between * i;
        }
    }
}
