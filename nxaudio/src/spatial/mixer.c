/*
 * NXAudio Object Mixer
 * 
 * Real-time mixing of multiple 3D audio objects
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "nxaudio/nxaudio.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ============ Mixer Configuration ============ */
#define MIXER_MAX_OBJECTS       128
#define MIXER_BUFFER_SIZE       1024
#define MIXER_OUTPUT_CHANNELS   2

/* ============ Object State ============ */
typedef struct {
    int             active;
    int             object_id;
    float           position_x, position_y, position_z;
    float           gain;
    float           distance_gain;
    float           current_sample_pos;
    float           pitch;
} mixer_object_state_t;

/* ============ Mixer State ============ */
typedef struct {
    float           mix_buffer[MIXER_BUFFER_SIZE * MIXER_OUTPUT_CHANNELS];
    float           temp_buffer[MIXER_BUFFER_SIZE * MIXER_OUTPUT_CHANNELS];
    mixer_object_state_t objects[MIXER_MAX_OBJECTS];
    int             object_count;
    float           master_gain;
    int             sample_rate;
} audio_mixer_t;

/* ============ Mixer Functions ============ */

/**
 * Create audio mixer
 */
audio_mixer_t* mixer_create(int sample_rate) {
    audio_mixer_t *mixer = (audio_mixer_t*)calloc(1, sizeof(audio_mixer_t));
    if (!mixer) return NULL;
    
    mixer->sample_rate = sample_rate;
    mixer->master_gain = 1.0f;
    mixer->object_count = 0;
    
    return mixer;
}

/**
 * Destroy audio mixer
 */
void mixer_destroy(audio_mixer_t *mixer) {
    if (mixer) free(mixer);
}

/**
 * Add object to mixer
 */
int mixer_add_object(audio_mixer_t *mixer, int object_id) {
    if (!mixer || mixer->object_count >= MIXER_MAX_OBJECTS) {
        return -1;
    }
    
    /* Find free slot */
    for (int i = 0; i < MIXER_MAX_OBJECTS; i++) {
        if (!mixer->objects[i].active) {
            mixer->objects[i].active = 1;
            mixer->objects[i].object_id = object_id;
            mixer->objects[i].position_x = 0;
            mixer->objects[i].position_y = 0;
            mixer->objects[i].position_z = 0;
            mixer->objects[i].gain = 1.0f;
            mixer->objects[i].distance_gain = 1.0f;
            mixer->objects[i].current_sample_pos = 0;
            mixer->objects[i].pitch = 1.0f;
            mixer->object_count++;
            return i;
        }
    }
    
    return -1;
}

/**
 * Remove object from mixer
 */
void mixer_remove_object(audio_mixer_t *mixer, int slot) {
    if (!mixer || slot < 0 || slot >= MIXER_MAX_OBJECTS) return;
    
    if (mixer->objects[slot].active) {
        mixer->objects[slot].active = 0;
        mixer->object_count--;
    }
}

/**
 * Update object position
 */
void mixer_update_object(audio_mixer_t *mixer, int slot,
                          float x, float y, float z, float gain) {
    if (!mixer || slot < 0 || slot >= MIXER_MAX_OBJECTS) return;
    
    mixer_object_state_t *obj = &mixer->objects[slot];
    obj->position_x = x;
    obj->position_y = y;
    obj->position_z = z;
    obj->gain = gain;
}

/**
 * Calculate gain based on position (simple distance model)
 */
static float calculate_position_gain(float x, float y, float z,
                                       float listener_x, float listener_y, float listener_z,
                                       float min_dist, float max_dist) {
    float dx = x - listener_x;
    float dy = y - listener_y;
    float dz = z - listener_z;
    
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    
    if (distance <= min_dist) return 1.0f;
    if (distance >= max_dist) return 0.0f;
    
    /* Inverse distance */
    return min_dist / distance;
}

/**
 * Calculate stereo pan based on position
 */
static void calculate_stereo_pan(float x, float z,
                                   float listener_x, float listener_z,
                                   float *left_gain, float *right_gain) {
    float dx = x - listener_x;
    float dz = z - listener_z;
    
    float angle = atan2f(dx, -dz);
    
    /* Convert angle to pan (-1 = left, +1 = right) */
    float pan = sinf(angle);
    
    /* Constant power panning */
    *left_gain = cosf((pan + 1.0f) * 0.25f * 3.14159f);
    *right_gain = sinf((pan + 1.0f) * 0.25f * 3.14159f);
}

/**
 * Mix all active objects into output buffer
 */
void mixer_process(audio_mixer_t *mixer,
                    float *output,
                    size_t num_frames,
                    float listener_x,
                    float listener_y,
                    float listener_z,
                    float (*get_sample_fn)(int object_id, size_t sample_pos)) {
    if (!mixer || !output) return;
    
    /* Clear mix buffer */
    memset(mixer->mix_buffer, 0, num_frames * MIXER_OUTPUT_CHANNELS * sizeof(float));
    
    /* Mix each active object */
    for (int i = 0; i < MIXER_MAX_OBJECTS; i++) {
        mixer_object_state_t *obj = &mixer->objects[i];
        if (!obj->active) continue;
        
        /* Calculate distance gain */
        float dist_gain = calculate_position_gain(
            obj->position_x, obj->position_y, obj->position_z,
            listener_x, listener_y, listener_z,
            1.0f, 100.0f
        );
        
        /* Calculate stereo panning */
        float left_gain, right_gain;
        calculate_stereo_pan(
            obj->position_x, obj->position_z,
            listener_x, listener_z,
            &left_gain, &right_gain
        );
        
        /* Total gain */
        float total_gain = obj->gain * dist_gain * mixer->master_gain;
        
        /* Add samples to mix buffer */
        for (size_t f = 0; f < num_frames; f++) {
            float sample = 0.0f;
            
            if (get_sample_fn) {
                sample = get_sample_fn(obj->object_id, (size_t)obj->current_sample_pos);
            }
            
            mixer->mix_buffer[f * 2] += sample * total_gain * left_gain;
            mixer->mix_buffer[f * 2 + 1] += sample * total_gain * right_gain;
            
            obj->current_sample_pos += obj->pitch;
        }
    }
    
    /* Copy to output */
    memcpy(output, mixer->mix_buffer, num_frames * MIXER_OUTPUT_CHANNELS * sizeof(float));
}

/**
 * Apply soft clipping to prevent harsh distortion
 */
void mixer_soft_clip(float *buffer, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        float x = buffer[i];
        
        /* Tanh soft clipping */
        if (x > 1.0f) {
            buffer[i] = 1.0f - expf(-x);
        } else if (x < -1.0f) {
            buffer[i] = -1.0f + expf(x);
        }
    }
}

/**
 * Apply limiter to prevent clipping
 */
void mixer_limit(float *buffer, size_t num_samples, float threshold) {
    for (size_t i = 0; i < num_samples; i++) {
        if (buffer[i] > threshold) {
            buffer[i] = threshold;
        } else if (buffer[i] < -threshold) {
            buffer[i] = -threshold;
        }
    }
}
