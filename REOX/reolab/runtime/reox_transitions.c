/*
 * REOX Transition System - Implementation
 * Advanced display transitions and effects
 */

#include "reox_transitions.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Default Transition Configs
 * ============================================================================ */

rx_transition_config rx_transition_default(void) {
    return (rx_transition_config){
        .type = RX_TRANS_FADE,
        .duration = 0.3f,
        .delay = 0.0f,
        .easing = RX_EASE_OUT,
        .interactive = false,
        .blur_amount = 0.0f,
        .overlay_color = {0, 0, 0, 0}
    };
}

rx_transition_config rx_transition_fast(void) {
    return (rx_transition_config){
        .type = RX_TRANS_FADE,
        .duration = 0.15f,
        .delay = 0.0f,
        .easing = RX_EASE_OUT,
        .interactive = false,
        .blur_amount = 0.0f,
        .overlay_color = {0, 0, 0, 0}
    };
}

rx_transition_config rx_transition_smooth(void) {
    return (rx_transition_config){
        .type = RX_TRANS_CROSSFADE,
        .duration = 0.5f,
        .delay = 0.0f,
        .easing = RX_EASE_IN_OUT,
        .interactive = true,
        .blur_amount = 0.1f,
        .overlay_color = {0, 0, 0, 0}
    };
}

/* ============================================================================
 * Page Transition Implementation
 * ============================================================================ */

rx_page_transition* rx_page_transition_create(rx_view* from, rx_view* to, rx_transition_config config) {
    rx_page_transition* trans = (rx_page_transition*)calloc(1, sizeof(rx_page_transition));
    if (!trans) return NULL;
    
    trans->from_view = from;
    trans->to_view = to;
    trans->config = config;
    trans->progress = 0.0f;
    trans->completed = false;
    trans->cancelled = false;
    
    return trans;
}

void rx_page_transition_start(rx_page_transition* trans) {
    if (!trans) return;
    
    trans->progress = 0.0f;
    trans->completed = false;
    trans->cancelled = false;
    
    if (trans->on_start) {
        trans->on_start(trans->user_data);
    }
}

void rx_page_transition_update(rx_page_transition* trans, float dt) {
    if (!trans || trans->completed || trans->cancelled) return;
    
    /* Handle delay */
    if (trans->config.delay > 0) {
        trans->config.delay -= dt;
        return;
    }
    
    /* Update progress */
    if (trans->config.duration > 0) {
        trans->progress += dt / trans->config.duration;
        if (trans->progress > 1.0f) trans->progress = 1.0f;
    } else {
        trans->progress = 1.0f;
    }
    
    /* Apply easing */
    float eased = rx_ease(trans->config.easing, trans->progress);
    
    /* Apply transition based on type */
    if (trans->from_view && trans->to_view) {
        switch (trans->config.type) {
            case RX_TRANS_FADE:
                /* Fade out from, fade in to */
                /* from_view->opacity = 1.0f - eased; */
                /* to_view->opacity = eased; */
                break;
                
            case RX_TRANS_SLIDE_LEFT:
                /* Slide from right to left */
                trans->to_view->box.frame.x = trans->to_view->box.frame.width * (1.0f - eased);
                trans->from_view->box.frame.x = -trans->from_view->box.frame.width * eased;
                break;
                
            case RX_TRANS_SLIDE_RIGHT:
                /* Slide from left to right */
                trans->to_view->box.frame.x = -trans->to_view->box.frame.width * (1.0f - eased);
                trans->from_view->box.frame.x = trans->from_view->box.frame.width * eased;
                break;
                
            case RX_TRANS_SLIDE_UP:
                trans->to_view->box.frame.y = trans->to_view->box.frame.height * (1.0f - eased);
                trans->from_view->box.frame.y = -trans->from_view->box.frame.height * eased;
                break;
                
            case RX_TRANS_SLIDE_DOWN:
                trans->to_view->box.frame.y = -trans->to_view->box.frame.height * (1.0f - eased);
                trans->from_view->box.frame.y = trans->from_view->box.frame.height * eased;
                break;
                
            case RX_TRANS_ZOOM_IN:
                /* Scale from 0.8 to 1.0 */
                /* to_view->scale = 0.8f + 0.2f * eased; */
                break;
                
            case RX_TRANS_ZOOM_OUT:
                /* from_view->scale = 1.0f - 0.2f * eased; */
                break;
                
            default:
                break;
        }
    }
    
    /* Callback on progress */
    if (trans->on_update) {
        trans->on_update(trans->progress, trans->user_data);
    }
    
    /* Check completion */
    if (trans->progress >= 1.0f) {
        trans->completed = true;
        if (trans->on_complete) {
            trans->on_complete(trans->user_data);
        }
    }
}

void rx_page_transition_set_progress(rx_page_transition* trans, float progress) {
    if (!trans) return;
    
    trans->progress = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    
    if (trans->on_update) {
        trans->on_update(trans->progress, trans->user_data);
    }
}

void rx_page_transition_cancel(rx_page_transition* trans) {
    if (!trans) return;
    
    trans->cancelled = true;
    
    if (trans->on_cancel) {
        trans->on_cancel(trans->user_data);
    }
}

void rx_page_transition_finish(rx_page_transition* trans) {
    if (!trans) return;
    
    trans->progress = 1.0f;
    trans->completed = true;
    
    if (trans->on_complete) {
        trans->on_complete(trans->user_data);
    }
}

void rx_page_transition_destroy(rx_page_transition* trans) {
    free(trans);
}

/* ============================================================================
 * Shared Element Transition Implementation
 * ============================================================================ */

rx_shared_element_transition* rx_shared_transition_create(void) {
    rx_shared_element_transition* trans = (rx_shared_element_transition*)calloc(1, sizeof(rx_shared_element_transition));
    if (!trans) return NULL;
    
    trans->config = rx_transition_default();
    trans->config.duration = 0.4f;
    trans->config.easing = RX_EASE_OUT_CUBIC;
    
    return trans;
}

void rx_shared_transition_add(rx_shared_element_transition* trans, const char* tag, rx_view* source, rx_view* target) {
    if (!trans || !tag || !source || !target) return;
    
    trans->elements = (rx_shared_element*)realloc(
        trans->elements,
        sizeof(rx_shared_element) * (trans->element_count + 1)
    );
    
    rx_shared_element* elem = &trans->elements[trans->element_count];
    elem->tag = tag;
    elem->source = source;
    elem->target = target;
    elem->source_bounds = source->box.frame;
    elem->target_bounds = target->box.frame;
    
    trans->element_count++;
}

void rx_shared_transition_start(rx_shared_element_transition* trans) {
    if (!trans) return;
    
    trans->progress = 0.0f;
    trans->active = true;
    
    /* Capture source bounds */
    for (size_t i = 0; i < trans->element_count; i++) {
        trans->elements[i].source_bounds = trans->elements[i].source->box.frame;
        trans->elements[i].target_bounds = trans->elements[i].target->box.frame;
    }
}

void rx_shared_transition_update(rx_shared_element_transition* trans, float dt) {
    if (!trans || !trans->active) return;
    
    trans->progress += dt / trans->config.duration;
    if (trans->progress > 1.0f) trans->progress = 1.0f;
    
    float eased = rx_ease(trans->config.easing, trans->progress);
    
    /* Interpolate each element */
    for (size_t i = 0; i < trans->element_count; i++) {
        rx_shared_element* elem = &trans->elements[i];
        rx_view* view = elem->source;
        
        /* Interpolate position and size */
        view->box.frame.x = elem->source_bounds.x + 
            (elem->target_bounds.x - elem->source_bounds.x) * eased;
        view->box.frame.y = elem->source_bounds.y + 
            (elem->target_bounds.y - elem->source_bounds.y) * eased;
        view->box.frame.width = elem->source_bounds.width + 
            (elem->target_bounds.width - elem->source_bounds.width) * eased;
        view->box.frame.height = elem->source_bounds.height + 
            (elem->target_bounds.height - elem->source_bounds.height) * eased;
    }
    
    if (trans->progress >= 1.0f) {
        trans->active = false;
    }
}

void rx_shared_transition_destroy(rx_shared_element_transition* trans) {
    if (trans) {
        free(trans->elements);
        free(trans);
    }
}

/* ============================================================================
 * View Morphing Implementation
 * ============================================================================ */

rx_morph_state rx_morph_capture_state(rx_view* view) {
    rx_morph_state state = {0};
    if (!view) return state;
    
    state.bounds = view->box.frame;
    state.background = view->box.background;
    state.corners = view->box.corner_radius;
    state.opacity = 1.0f;
    state.scale = 1.0f;
    state.rotation = 0.0f;
    
    return state;
}

rx_morph_state rx_morph_interpolate(rx_morph_state from, rx_morph_state to, float t) {
    rx_morph_state result;
    
    /* Interpolate bounds */
    result.bounds.x = from.bounds.x + (to.bounds.x - from.bounds.x) * t;
    result.bounds.y = from.bounds.y + (to.bounds.y - from.bounds.y) * t;
    result.bounds.width = from.bounds.width + (to.bounds.width - from.bounds.width) * t;
    result.bounds.height = from.bounds.height + (to.bounds.height - from.bounds.height) * t;
    
    /* Interpolate background color */
    result.background.r = from.background.r + (uint8_t)((to.background.r - from.background.r) * t);
    result.background.g = from.background.g + (uint8_t)((to.background.g - from.background.g) * t);
    result.background.b = from.background.b + (uint8_t)((to.background.b - from.background.b) * t);
    result.background.a = from.background.a + (uint8_t)((to.background.a - from.background.a) * t);
    
    /* Interpolate corner radius */
    result.corners.top_left = from.corners.top_left + (to.corners.top_left - from.corners.top_left) * t;
    result.corners.top_right = from.corners.top_right + (to.corners.top_right - from.corners.top_right) * t;
    result.corners.bottom_right = from.corners.bottom_right + (to.corners.bottom_right - from.corners.bottom_right) * t;
    result.corners.bottom_left = from.corners.bottom_left + (to.corners.bottom_left - from.corners.bottom_left) * t;
    
    /* Interpolate transform properties */
    result.opacity = from.opacity + (to.opacity - from.opacity) * t;
    result.scale = from.scale + (to.scale - from.scale) * t;
    result.rotation = from.rotation + (to.rotation - from.rotation) * t;
    
    return result;
}

void rx_morph_apply_state(rx_view* view, rx_morph_state state) {
    if (!view) return;
    
    view->box.frame = state.bounds;
    view->box.background = state.background;
    view->box.corner_radius = state.corners;
    /* Note: opacity, scale, rotation would need view transform support */
}

rx_morph_animation* rx_morph_create(rx_view* view, rx_morph_state from, rx_morph_state to, float duration) {
    rx_morph_animation* morph = (rx_morph_animation*)calloc(1, sizeof(rx_morph_animation));
    if (!morph) return NULL;
    
    morph->view = view;
    morph->from_state = from;
    morph->to_state = to;
    morph->current_state = from;
    morph->base.duration = duration;
    morph->base.easing = RX_EASE_OUT_CUBIC;
    morph->base.state = RX_ANIM_IDLE;
    
    return morph;
}

void rx_morph_start(rx_morph_animation* morph) {
    if (!morph) return;
    morph->base.state = RX_ANIM_RUNNING;
    morph->base.elapsed = 0;
}

void rx_morph_update(rx_morph_animation* morph, float dt) {
    if (!morph || morph->base.state != RX_ANIM_RUNNING) return;
    
    morph->base.elapsed += dt;
    float progress = morph->base.duration > 0 ? morph->base.elapsed / morph->base.duration : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    
    float eased = rx_ease(morph->base.easing, progress);
    
    morph->current_state = rx_morph_interpolate(morph->from_state, morph->to_state, eased);
    rx_morph_apply_state(morph->view, morph->current_state);
    
    if (progress >= 1.0f) {
        morph->base.state = RX_ANIM_COMPLETED;
    }
}

void rx_morph_destroy(rx_morph_animation* morph) {
    free(morph);
}

/* ============================================================================
 * Particle System Implementation
 * ============================================================================ */

static float random_range(float min, float max) {
    return min + ((float)rand() / RAND_MAX) * (max - min);
}

static rx_color random_color_between(rx_color a, rx_color b) {
    float t = (float)rand() / RAND_MAX;
    return (rx_color){
        a.r + (uint8_t)((b.r - a.r) * t),
        a.g + (uint8_t)((b.g - a.g) * t),
        a.b + (uint8_t)((b.b - a.b) * t),
        a.a + (uint8_t)((b.a - a.a) * t)
    };
}

rx_particle_emitter* rx_particles_create(rx_particle_emitter_config config) {
    rx_particle_emitter* emitter = (rx_particle_emitter*)calloc(1, sizeof(rx_particle_emitter));
    if (!emitter) return NULL;
    
    emitter->config = config;
    emitter->particles = (rx_particle*)calloc(config.max_particles, sizeof(rx_particle));
    emitter->emitting = false;
    
    return emitter;
}

void rx_particles_set_position(rx_particle_emitter* emitter, rx_point pos) {
    if (emitter) {
        emitter->config.position = pos;
    }
}

void rx_particles_start(rx_particle_emitter* emitter) {
    if (emitter) {
        emitter->emitting = true;
    }
}

void rx_particles_stop(rx_particle_emitter* emitter) {
    if (emitter) {
        emitter->emitting = false;
    }
}

static void emit_particle(rx_particle_emitter* emitter) {
    /* Find inactive particle slot */
    rx_particle* p = NULL;
    for (int i = 0; i < emitter->config.max_particles; i++) {
        if (!emitter->particles[i].active) {
            p = &emitter->particles[i];
            break;
        }
    }
    if (!p) return;
    
    /* Initialize particle */
    rx_particle_emitter_config* cfg = &emitter->config;
    
    /* Position within emit area */
    p->position.x = cfg->position.x + random_range(-cfg->emit_area.width/2, cfg->emit_area.width/2);
    p->position.y = cfg->position.y + random_range(-cfg->emit_area.height/2, cfg->emit_area.height/2);
    
    /* Velocity based on angle and spread */
    float angle = (cfg->emit_angle + random_range(-cfg->emit_spread/2, cfg->emit_spread/2)) * M_PI / 180.0f;
    float speed = random_range(cfg->min_speed, cfg->max_speed);
    p->velocity.x = cosf(angle) * speed;
    p->velocity.y = sinf(angle) * speed;
    
    p->acceleration = cfg->gravity;
    
    /* Color */
    if (cfg->color_randomize) {
        p->color = random_color_between(cfg->start_color, cfg->end_color);
        p->end_color = cfg->end_color;
    } else {
        p->color = cfg->start_color;
        p->end_color = cfg->end_color;
    }
    
    /* Size and lifetime */
    p->size = random_range(cfg->min_size, cfg->max_size);
    p->end_size = p->size * cfg->end_size_scale;
    p->lifetime = random_range(cfg->min_lifetime, cfg->max_lifetime);
    p->age = 0;
    
    /* Rotation */
    p->rotation = random_range(0, 360);
    p->rotation_speed = random_range(-180, 180);
    
    p->opacity = 1.0f;
    p->active = true;
    
    emitter->particle_count++;
}

void rx_particles_burst(rx_particle_emitter* emitter, int count) {
    if (!emitter) return;
    
    for (int i = 0; i < count; i++) {
        emit_particle(emitter);
    }
}

void rx_particles_update(rx_particle_emitter* emitter, float dt) {
    if (!emitter) return;
    
    /* Emit new particles */
    if (emitter->emitting && emitter->config.emit_rate > 0) {
        emitter->emit_accumulator += dt;
        float emit_interval = 1.0f / emitter->config.emit_rate;
        
        while (emitter->emit_accumulator >= emit_interval) {
            emit_particle(emitter);
            emitter->emit_accumulator -= emit_interval;
        }
    }
    
    /* Update existing particles */
    rx_rect bounds = {{0,0}, {0,0}};
    bool first = true;
    
    for (int i = 0; i < emitter->config.max_particles; i++) {
        rx_particle* p = &emitter->particles[i];
        if (!p->active) continue;
        
        p->age += dt;
        
        if (p->age >= p->lifetime) {
            p->active = false;
            emitter->particle_count--;
            continue;
        }
        
        float life_progress = p->age / p->lifetime;
        
        /* Apply physics */
        p->velocity.x += p->acceleration.x * dt;
        p->velocity.y += p->acceleration.y * dt;
        
        /* Apply drag */
        p->velocity.x *= (1.0f - emitter->config.drag * dt);
        p->velocity.y *= (1.0f - emitter->config.drag * dt);
        
        /* Apply turbulence */
        if (emitter->config.turbulence > 0) {
            p->velocity.x += random_range(-1, 1) * emitter->config.turbulence * dt;
            p->velocity.y += random_range(-1, 1) * emitter->config.turbulence * dt;
        }
        
        /* Update position */
        p->position.x += p->velocity.x * dt;
        p->position.y += p->velocity.y * dt;
        
        /* Update rotation */
        p->rotation += p->rotation_speed * dt;
        
        /* Interpolate color */
        p->color.r = p->color.r + (uint8_t)((p->end_color.r - p->color.r) * life_progress * 0.1f);
        p->color.g = p->color.g + (uint8_t)((p->end_color.g - p->color.g) * life_progress * 0.1f);
        p->color.b = p->color.b + (uint8_t)((p->end_color.b - p->color.b) * life_progress * 0.1f);
        
        /* Interpolate size */
        float current_size = p->size + (p->end_size - p->size) * life_progress;
        
        /* Fade out */
        p->opacity = 1.0f - life_progress;
        
        /* Update bounds */
        if (first) {
            bounds.x = p->position.x;
            bounds.y = p->position.y;
            bounds.width = current_size;
            bounds.height = current_size;
            first = false;
        } else {
            if (p->position.x < bounds.x) bounds.x = p->position.x;
            if (p->position.y < bounds.y) bounds.y = p->position.y;
            if (p->position.x + current_size > bounds.x + bounds.width) 
                bounds.width = p->position.x + current_size - bounds.x;
            if (p->position.y + current_size > bounds.y + bounds.height)
                bounds.height = p->position.y + current_size - bounds.y;
        }
    }
    
    emitter->bounds = bounds;
}

void rx_particles_render(rx_particle_emitter* emitter, void* context) {
    if (!emitter) return;
    (void)context;
    /* Rendering would be done by the graphics system */
}

void rx_particles_destroy(rx_particle_emitter* emitter) {
    if (emitter) {
        free(emitter->particles);
        free(emitter);
    }
}

/* ============================================================================
 * Preset Particle Effects
 * ============================================================================ */

rx_particle_emitter* rx_particles_confetti(rx_point origin) {
    rx_particle_emitter_config config = {
        .emit_rate = 0,
        .max_particles = 200,
        .position = origin,
        .emit_area = {100, 20},
        .emit_angle = -90,
        .emit_spread = 60,
        .min_speed = 200,
        .max_speed = 400,
        .min_lifetime = 2.0f,
        .max_lifetime = 4.0f,
        .min_size = 6,
        .max_size = 12,
        .end_size_scale = 1.0f,
        .start_color = {255, 100, 100, 255},
        .end_color = {100, 100, 255, 255},
        .color_randomize = true,
        .gravity = {0, 200},
        .drag = 0.3f,
        .turbulence = 50,
        .shape = RX_PARTICLE_SQUARE,
        .additive_blend = false,
        .blur_amount = 0
    };
    
    return rx_particles_create(config);
}

rx_particle_emitter* rx_particles_fireworks(rx_point origin, rx_color color) {
    rx_particle_emitter_config config = {
        .emit_rate = 0,
        .max_particles = 100,
        .position = origin,
        .emit_area = {0, 0},
        .emit_angle = 0,
        .emit_spread = 360,
        .min_speed = 100,
        .max_speed = 300,
        .min_lifetime = 0.5f,
        .max_lifetime = 1.5f,
        .min_size = 3,
        .max_size = 6,
        .end_size_scale = 0.1f,
        .start_color = color,
        .end_color = {color.r, color.g, color.b, 0},
        .color_randomize = false,
        .gravity = {0, 150},
        .drag = 0.5f,
        .turbulence = 0,
        .shape = RX_PARTICLE_CIRCLE,
        .additive_blend = true,
        .blur_amount = 0.2f
    };
    
    return rx_particles_create(config);
}

rx_particle_emitter* rx_particles_snow(rx_rect area) {
    rx_particle_emitter_config config = {
        .emit_rate = 30,
        .max_particles = 500,
        .position = {area.x + area.width/2, area.y},
        .emit_area = {area.width, 10},
        .emit_angle = 90,
        .emit_spread = 30,
        .min_speed = 30,
        .max_speed = 80,
        .min_lifetime = 5.0f,
        .max_lifetime = 10.0f,
        .min_size = 3,
        .max_size = 8,
        .end_size_scale = 1.0f,
        .start_color = {255, 255, 255, 200},
        .end_color = {255, 255, 255, 50},
        .color_randomize = false,
        .gravity = {0, 10},
        .drag = 0.1f,
        .turbulence = 20,
        .shape = RX_PARTICLE_CIRCLE,
        .additive_blend = false,
        .blur_amount = 0.1f
    };
    
    return rx_particles_create(config);
}

rx_particle_emitter* rx_particles_sparks(rx_point origin) {
    rx_particle_emitter_config config = {
        .emit_rate = 50,
        .max_particles = 100,
        .position = origin,
        .emit_area = {5, 5},
        .emit_angle = -90,
        .emit_spread = 45,
        .min_speed = 100,
        .max_speed = 250,
        .min_lifetime = 0.3f,
        .max_lifetime = 0.8f,
        .min_size = 2,
        .max_size = 4,
        .end_size_scale = 0.2f,
        .start_color = {255, 200, 50, 255},
        .end_color = {255, 100, 0, 0},
        .color_randomize = false,
        .gravity = {0, 300},
        .drag = 0.2f,
        .turbulence = 30,
        .shape = RX_PARTICLE_CIRCLE,
        .additive_blend = true,
        .blur_amount = 0.3f
    };
    
    return rx_particles_create(config);
}

rx_particle_emitter* rx_particles_fire(rx_point origin) {
    rx_particle_emitter_config config = {
        .emit_rate = 60,
        .max_particles = 150,
        .position = origin,
        .emit_area = {30, 5},
        .emit_angle = -90,
        .emit_spread = 30,
        .min_speed = 50,
        .max_speed = 120,
        .min_lifetime = 0.5f,
        .max_lifetime = 1.2f,
        .min_size = 15,
        .max_size = 30,
        .end_size_scale = 0.3f,
        .start_color = {255, 200, 50, 220},
        .end_color = {255, 50, 0, 0},
        .color_randomize = true,
        .gravity = {0, -20},
        .drag = 0.1f,
        .turbulence = 40,
        .shape = RX_PARTICLE_CIRCLE,
        .additive_blend = true,
        .blur_amount = 0.5f
    };
    
    return rx_particles_create(config);
}

rx_particle_emitter* rx_particles_magic(rx_point origin, rx_color color) {
    rx_particle_emitter_config config = {
        .emit_rate = 40,
        .max_particles = 80,
        .position = origin,
        .emit_area = {20, 20},
        .emit_angle = 0,
        .emit_spread = 360,
        .min_speed = 20,
        .max_speed = 60,
        .min_lifetime = 1.0f,
        .max_lifetime = 2.0f,
        .min_size = 4,
        .max_size = 8,
        .end_size_scale = 0.0f,
        .start_color = color,
        .end_color = {color.r, color.g, color.b, 0},
        .color_randomize = true,
        .gravity = {0, -30},
        .drag = 0.05f,
        .turbulence = 50,
        .shape = RX_PARTICLE_STAR,
        .additive_blend = true,
        .blur_amount = 0.3f
    };
    
    return rx_particles_create(config);
}

/* ============================================================================
 * Screen Transition Manager
 * ============================================================================ */

rx_screen_transition_manager* rx_screen_mgr_create(void) {
    rx_screen_transition_manager* mgr = (rx_screen_transition_manager*)calloc(1, sizeof(rx_screen_transition_manager));
    if (!mgr) return NULL;
    
    mgr->default_config = rx_transition_default();
    mgr->history_capacity = 16;
    mgr->history = (rx_page_transition**)calloc(mgr->history_capacity, sizeof(rx_page_transition*));
    
    return mgr;
}

void rx_screen_mgr_push(rx_screen_transition_manager* mgr, rx_view* view, rx_transition_config config) {
    if (!mgr || !view) return;
    
    /* Get current view from history */
    rx_view* from_view = NULL;
    if (mgr->history_count > 0 && mgr->history[mgr->history_count - 1]) {
        from_view = mgr->history[mgr->history_count - 1]->to_view;
    }
    
    /* Create transition */
    rx_page_transition* trans = rx_page_transition_create(from_view, view, config);
    
    /* Add to history */
    if (mgr->history_count >= mgr->history_capacity) {
        mgr->history_capacity *= 2;
        mgr->history = (rx_page_transition**)realloc(mgr->history, 
            sizeof(rx_page_transition*) * mgr->history_capacity);
    }
    mgr->history[mgr->history_count++] = trans;
    
    /* Start transition */
    mgr->current_transition = trans;
    rx_page_transition_start(trans);
}

void rx_screen_mgr_pop(rx_screen_transition_manager* mgr) {
    if (!mgr || mgr->history_count <= 1) return;
    
    /* Get views */
    rx_page_transition* current = mgr->history[mgr->history_count - 1];
    rx_page_transition* previous = mgr->history[mgr->history_count - 2];
    
    /* Create reverse transition */
    rx_transition_config config = mgr->default_config;
    
    /* Reverse the direction */
    switch (config.type) {
        case RX_TRANS_SLIDE_LEFT: config.type = RX_TRANS_SLIDE_RIGHT; break;
        case RX_TRANS_SLIDE_RIGHT: config.type = RX_TRANS_SLIDE_LEFT; break;
        case RX_TRANS_SLIDE_UP: config.type = RX_TRANS_SLIDE_DOWN; break;
        case RX_TRANS_SLIDE_DOWN: config.type = RX_TRANS_SLIDE_UP; break;
        default: break;
    }
    
    rx_page_transition* trans = rx_page_transition_create(
        current->to_view, 
        previous->to_view, 
        config
    );
    
    mgr->current_transition = trans;
    rx_page_transition_start(trans);
    
    /* Remove from history */
    rx_page_transition_destroy(current);
    mgr->history_count--;
}

void rx_screen_mgr_update(rx_screen_transition_manager* mgr, float dt) {
    if (!mgr || !mgr->current_transition) return;
    
    rx_page_transition_update(mgr->current_transition, dt);
    
    if (mgr->current_transition->completed) {
        mgr->current_transition = NULL;
    }
}

void rx_screen_mgr_destroy(rx_screen_transition_manager* mgr) {
    if (!mgr) return;
    
    for (size_t i = 0; i < mgr->history_count; i++) {
        rx_page_transition_destroy(mgr->history[i]);
    }
    free(mgr->history);
    free(mgr);
}
