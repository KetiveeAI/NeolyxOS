/*
 * NXAudio Engine Core
 * 
 * Unified audio pipeline connecting all components:
 * decoder -> stream -> resampler -> mixer -> spatial -> effects -> device
 * 
 * This is the main runtime for NeolyxOS audio.
 * 
 * Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

#ifndef NXAUDIO_ENGINE_H
#define NXAUDIO_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "stream.h"
#include "mixer.h"
#include "resample.h"
#include "effects.h"
#include "../metadata/metadata.h"
#include "../spatial/nx_spatial.h"

/* ============ Engine Configuration ============ */
typedef struct {
    uint32_t output_sample_rate;
    uint32_t output_channels;
    uint32_t buffer_frames;
    nx_resample_quality_t resample_quality;
} nx_engine_config_t;

/* ============ Sound Handle ============ */
typedef int32_t nx_sound_t;
#define NX_SOUND_INVALID (-1)

/* ============ Sound State ============ */
typedef struct {
    int              active;
    nx_audio_stream_t stream;
    nx_resampler_t   resampler;
    nx_effects_t     effects;
    nx_metadata_t    metadata;
    
    float           *decode_buffer;
    float           *resample_buffer;
    size_t           buffer_size;
    
    int              mixer_channel;
    float            volume;
    float            pan;
    int              looping;
    int              paused;
} nx_sound_state_t;

/* ============ Engine Instance ============ */
typedef struct {
    nx_engine_config_t config;
    
    /* Sound slots */
    nx_sound_state_t *sounds;
    int               max_sounds;
    int               active_sounds;
    
    /* Mixer */
    nx_mixer_t        mixer;
    
    /* Output buffer */
    float            *output_buffer;
    size_t            output_frames;
    
    /* Master effects */
    nx_effects_t      master_effects;
    
    /* State */
    int               initialized;
    int               running;
} nx_engine_t;

/* ============ Public API ============ */

/**
 * Create engine with default config
 */
nx_error_t nx_engine_create(nx_engine_t *engine);

/**
 * Create engine with custom config
 */
nx_error_t nx_engine_create_ex(nx_engine_t *engine, const nx_engine_config_t *config);

/**
 * Destroy engine
 */
void nx_engine_destroy(nx_engine_t *engine);

/**
 * Load sound from file
 */
nx_sound_t nx_engine_load(nx_engine_t *engine, const char *filepath);

/**
 * Unload sound
 */
void nx_engine_unload(nx_engine_t *engine, nx_sound_t sound);

/**
 * Play sound
 */
nx_error_t nx_engine_play(nx_engine_t *engine, nx_sound_t sound);

/**
 * Pause sound
 */
nx_error_t nx_engine_pause(nx_engine_t *engine, nx_sound_t sound);

/**
 * Stop sound
 */
nx_error_t nx_engine_stop(nx_engine_t *engine, nx_sound_t sound);

/**
 * Set sound volume
 */
void nx_engine_set_volume(nx_engine_t *engine, nx_sound_t sound, float volume);

/**
 * Set sound pan
 */
void nx_engine_set_pan(nx_engine_t *engine, nx_sound_t sound, float pan);

/**
 * Set sound looping
 */
void nx_engine_set_loop(nx_engine_t *engine, nx_sound_t sound, int loop);

/**
 * Seek sound
 */
nx_error_t nx_engine_seek(nx_engine_t *engine, nx_sound_t sound, float time_sec);

/**
 * Get sound position
 */
float nx_engine_get_position(nx_engine_t *engine, nx_sound_t sound);

/**
 * Get sound duration
 */
float nx_engine_get_duration(nx_engine_t *engine, nx_sound_t sound);

/**
 * Get sound metadata
 */
const nx_metadata_t* nx_engine_get_metadata(nx_engine_t *engine, nx_sound_t sound);

/**
 * Check if sound is playing
 */
int nx_engine_is_playing(nx_engine_t *engine, nx_sound_t sound);

/**
 * Set master volume
 */
void nx_engine_set_master_volume(nx_engine_t *engine, float volume);

/**
 * Process audio (call from audio thread)
 * Returns frames rendered
 */
size_t nx_engine_render(nx_engine_t *engine, float *output, size_t frames);

/**
 * Render to int16 output
 */
size_t nx_engine_render_s16(nx_engine_t *engine, int16_t *output, size_t frames);

#endif /* NXAUDIO_ENGINE_H */
