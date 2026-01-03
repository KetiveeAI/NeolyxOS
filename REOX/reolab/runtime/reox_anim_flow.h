/*
 * REOX Animation Flow Control
 * Timeline-based animation sequencing and orchestration for NeolyxOS UI
 * 
 * Features:
 * - Timeline with tracks
 * - Sequence builder
 * - Parallel and serial execution
 * - Custom animation curves
 * - State machine integration
 */

#ifndef REOX_ANIM_FLOW_H
#define REOX_ANIM_FLOW_H

#include "reox_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Animation Timeline
 * ============================================================================ */

typedef struct rx_timeline_entry {
    rx_animation* animation;
    float start_time;         /* Seconds from timeline start */
    float duration;           /* Override animation duration if > 0 */
    const char* label;        /* Optional label for seek */
    struct rx_timeline_entry* next;
} rx_timeline_entry;

typedef struct rx_timeline_track {
    const char* name;
    rx_timeline_entry* entries;
    size_t entry_count;
    bool muted;
    bool solo;
    float weight;             /* Track blend weight (0-1) */
} rx_timeline_track;

typedef struct rx_timeline {
    rx_timeline_track** tracks;
    size_t track_count;
    float duration;           /* Total timeline duration */
    float current_time;
    float playback_speed;
    bool playing;
    bool loop;
    bool ping_pong;           /* Alternate direction each loop */
    bool reversed;
    
    /* Markers for seek points */
    struct {
        const char* name;
        float time;
    }* markers;
    size_t marker_count;
    
    /* Callbacks */
    void (*on_complete)(void* user_data);
    void (*on_loop)(void* user_data);
    void (*on_marker)(const char* marker, void* user_data);
    void* user_data;
} rx_timeline;

/* Timeline creation and control */
extern rx_timeline* rx_timeline_create(float duration);
extern rx_timeline_track* rx_timeline_add_track(rx_timeline* tl, const char* name);
extern void rx_timeline_add_animation(rx_timeline* tl, rx_timeline_track* track, rx_animation* anim, float start_time);
extern void rx_timeline_add_marker(rx_timeline* tl, const char* name, float time);

/* Playback control */
extern void rx_timeline_play(rx_timeline* tl);
extern void rx_timeline_pause(rx_timeline* tl);
extern void rx_timeline_stop(rx_timeline* tl);
extern void rx_timeline_seek(rx_timeline* tl, float time);
extern void rx_timeline_seek_marker(rx_timeline* tl, const char* marker);
extern void rx_timeline_set_speed(rx_timeline* tl, float speed);
extern void rx_timeline_update(rx_timeline* tl, float dt);
extern void rx_timeline_destroy(rx_timeline* tl);

/* ============================================================================
 * Sequence Builder (Fluent API)
 * ============================================================================ */

typedef struct rx_sequence {
    rx_animation** animations;
    float* delays;
    size_t count;
    size_t capacity;
    size_t current_index;
    bool playing;
    
    /* Callbacks */
    void (*on_step)(size_t index, void* user_data);
    void (*on_complete)(void* user_data);
    void* user_data;
} rx_sequence;

/* Sequence creation */
extern rx_sequence* rx_sequence_create(void);

/* Add animations to sequence */
extern rx_sequence* rx_seq_add(rx_sequence* seq, rx_animation* anim);
extern rx_sequence* rx_seq_add_delay(rx_sequence* seq, float delay);
extern rx_sequence* rx_seq_add_callback(rx_sequence* seq, void (*callback)(void* data), void* data);
extern rx_sequence* rx_seq_add_parallel(rx_sequence* seq, rx_animation** anims, size_t count);

/* Convenience builders */
extern rx_sequence* rx_seq_fade_in(rx_sequence* seq, rx_view* view, float duration);
extern rx_sequence* rx_seq_fade_out(rx_sequence* seq, rx_view* view, float duration);
extern rx_sequence* rx_seq_slide(rx_sequence* seq, rx_view* view, rx_point from, rx_point to, float duration);
extern rx_sequence* rx_seq_scale(rx_sequence* seq, rx_view* view, float from, float to, float duration);
extern rx_sequence* rx_seq_wait(rx_sequence* seq, float duration);

/* Playback */
extern void rx_sequence_start(rx_sequence* seq);
extern void rx_sequence_pause(rx_sequence* seq);
extern void rx_sequence_resume(rx_sequence* seq);
extern void rx_sequence_skip(rx_sequence* seq);
extern void rx_sequence_update(rx_sequence* seq, float dt);
extern void rx_sequence_destroy(rx_sequence* seq);

/* ============================================================================
 * Parallel Animation Group
 * ============================================================================ */

typedef struct rx_parallel {
    rx_animation** animations;
    size_t count;
    size_t capacity;
    size_t completed_count;
    bool playing;
    
    /* Completion mode */
    bool wait_for_all;        /* Wait for all or complete when first finishes */
    
    void (*on_complete)(void* user_data);
    void* user_data;
} rx_parallel;

extern rx_parallel* rx_parallel_create(void);
extern rx_parallel* rx_parallel_add(rx_parallel* par, rx_animation* anim);
extern void rx_parallel_start(rx_parallel* par);
extern bool rx_parallel_update(rx_parallel* par, float dt);
extern void rx_parallel_destroy(rx_parallel* par);

/* ============================================================================
 * Custom Animation Curves
 * ============================================================================ */

typedef struct rx_curve_point {
    float time;               /* 0-1 */
    float value;              /* Output value */
    float in_tangent;         /* Tangent coming in */
    float out_tangent;        /* Tangent going out */
} rx_curve_point;

typedef struct rx_animation_curve {
    rx_curve_point* points;
    size_t point_count;
    bool wrap_loop;           /* Loop curve */
    bool wrap_ping_pong;      /* Ping-pong at ends */
} rx_animation_curve;

extern rx_animation_curve* rx_curve_create(void);
extern void rx_curve_add_key(rx_animation_curve* curve, float time, float value);
extern void rx_curve_add_key_smooth(rx_animation_curve* curve, float time, float value, float in_tangent, float out_tangent);
extern float rx_curve_evaluate(rx_animation_curve* curve, float t);
extern void rx_curve_smooth_tangents(rx_animation_curve* curve);
extern void rx_curve_destroy(rx_animation_curve* curve);

/* Preset curves */
extern rx_animation_curve* rx_curve_linear(void);
extern rx_animation_curve* rx_curve_ease_in(void);
extern rx_animation_curve* rx_curve_ease_out(void);
extern rx_animation_curve* rx_curve_ease_in_out(void);
extern rx_animation_curve* rx_curve_bounce(void);
extern rx_animation_curve* rx_curve_elastic(void);
extern rx_animation_curve* rx_curve_overshoot(void);

/* ============================================================================
 * Animation State Machine
 * ============================================================================ */

typedef struct rx_anim_state {
    const char* name;
    rx_animation* animation;
    bool loop;
} rx_anim_state;

typedef struct rx_anim_transition {
    const char* from_state;
    const char* to_state;
    float duration;           /* Blend duration */
    rx_easing easing;
    bool (*condition)(void* user_data);  /* Transition condition */
    void* condition_data;
} rx_anim_transition;

typedef struct rx_anim_state_machine {
    rx_anim_state* states;
    size_t state_count;
    rx_anim_transition* transitions;
    size_t transition_count;
    
    rx_anim_state* current_state;
    rx_anim_state* next_state;
    float blend_time;
    float blend_progress;
    
    void (*on_state_change)(const char* from, const char* to, void* user_data);
    void* user_data;
} rx_anim_state_machine;

extern rx_anim_state_machine* rx_state_machine_create(void);
extern void rx_state_machine_add_state(rx_anim_state_machine* sm, const char* name, rx_animation* anim, bool loop);
extern void rx_state_machine_add_transition(rx_anim_state_machine* sm, const char* from, const char* to, float duration, rx_easing easing);
extern void rx_state_machine_set_state(rx_anim_state_machine* sm, const char* name);
extern void rx_state_machine_trigger(rx_anim_state_machine* sm, const char* trigger);
extern void rx_state_machine_update(rx_anim_state_machine* sm, float dt);
extern const char* rx_state_machine_current(rx_anim_state_machine* sm);
extern void rx_state_machine_destroy(rx_anim_state_machine* sm);

/* ============================================================================
 * Animation Layers (Blending multiple animations)
 * ============================================================================ */

typedef struct rx_anim_layer {
    const char* name;
    rx_animation* animation;
    float weight;             /* Blend weight 0-1 */
    bool additive;            /* Additive or override */
    uint32_t mask;            /* Property mask for partial application */
} rx_anim_layer;

typedef struct rx_anim_mixer {
    rx_anim_layer* layers;
    size_t layer_count;
    rx_view* target;
} rx_anim_mixer;

extern rx_anim_mixer* rx_mixer_create(rx_view* target);
extern int rx_mixer_add_layer(rx_anim_mixer* mixer, const char* name, rx_animation* anim, float weight);
extern void rx_mixer_set_weight(rx_anim_mixer* mixer, const char* name, float weight);
extern void rx_mixer_update(rx_anim_mixer* mixer, float dt);
extern void rx_mixer_destroy(rx_anim_mixer* mixer);

/* ============================================================================
 * Choreographer (High-level orchestration)
 * ============================================================================ */

typedef struct rx_choreographer {
    rx_timeline** timelines;
    rx_sequence** sequences;
    rx_parallel** parallels;
    size_t timeline_count;
    size_t sequence_count;
    size_t parallel_count;
    
    bool auto_update;
    float global_speed;
} rx_choreographer;

extern rx_choreographer* rx_choreographer_create(void);
extern void rx_choreographer_add_timeline(rx_choreographer* ch, rx_timeline* tl);
extern void rx_choreographer_add_sequence(rx_choreographer* ch, rx_sequence* seq);
extern void rx_choreographer_add_parallel(rx_choreographer* ch, rx_parallel* par);
extern void rx_choreographer_update(rx_choreographer* ch, float dt);
extern void rx_choreographer_pause_all(rx_choreographer* ch);
extern void rx_choreographer_resume_all(rx_choreographer* ch);
extern void rx_choreographer_stop_all(rx_choreographer* ch);
extern void rx_choreographer_set_speed(rx_choreographer* ch, float speed);
extern void rx_choreographer_destroy(rx_choreographer* ch);

/* Global choreographer */
extern rx_choreographer* rx_choreographer_shared(void);

#ifdef __cplusplus
}
#endif

#endif /* REOX_ANIM_FLOW_H */
