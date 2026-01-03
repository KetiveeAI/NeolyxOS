/*
 * RoleCut Audio Engine
 * 
 * Professional audio processing using NXAudio Ketivee Oohm.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef RC_AUDIO_H
#define RC_AUDIO_H

#include "rolecut.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Audio Formats
 * ============================================================================ */

typedef enum {
    RC_AUDIO_S16 = 0,           /* Signed 16-bit */
    RC_AUDIO_S24,               /* Signed 24-bit */
    RC_AUDIO_S32,               /* Signed 32-bit */
    RC_AUDIO_F32,               /* Float 32-bit */
} rc_audio_format_t;

typedef struct {
    rc_audio_format_t format;
    uint32_t sample_rate;       /* 44100, 48000, 96000 */
    uint8_t channels;           /* 1=mono, 2=stereo, 6=5.1, 8=7.1 */
} rc_audio_config_t;

/* ============================================================================
 * Audio Buffer
 * ============================================================================ */

typedef struct {
    void *data;
    uint32_t sample_count;
    rc_audio_config_t config;
} rc_audio_buffer_t;

/**
 * Create audio buffer.
 */
rc_audio_buffer_t *rc_audio_buffer_create(uint32_t samples, rc_audio_config_t config);

/**
 * Destroy audio buffer.
 */
void rc_audio_buffer_destroy(rc_audio_buffer_t *buf);

/**
 * Copy audio buffer.
 */
rc_audio_buffer_t *rc_audio_buffer_copy(rc_audio_buffer_t *src);

/**
 * Mix audio buffers together.
 */
void rc_audio_buffer_mix(rc_audio_buffer_t *dst, rc_audio_buffer_t *src, float volume);

/* ============================================================================
 * Audio Engine
 * ============================================================================ */

typedef struct rc_audio_engine rc_audio_engine_t;

/**
 * Create audio engine.
 */
rc_audio_engine_t *rc_audio_engine_create(rc_audio_config_t config);

/**
 * Destroy audio engine.
 */
void rc_audio_engine_destroy(rc_audio_engine_t *engine);

/**
 * Get engine config.
 */
rc_audio_config_t rc_audio_engine_get_config(rc_audio_engine_t *engine);

/* ============================================================================
 * Waveform Visualization
 * ============================================================================ */

typedef struct {
    float *peaks;               /* Peak values */
    float *rms;                 /* RMS values */
    uint32_t count;             /* Number of data points */
    rc_duration_t duration;
} rc_waveform_t;

/**
 * Generate waveform data for visualization.
 */
rc_waveform_t *rc_audio_generate_waveform(rc_audio_buffer_t *buffer, uint32_t resolution);

/**
 * Destroy waveform data.
 */
void rc_audio_waveform_destroy(rc_waveform_t *waveform);

/* ============================================================================
 * Audio Effects
 * ============================================================================ */

typedef enum {
    /* Volume/Dynamics */
    RC_AUDIO_FX_GAIN,           /* Volume adjustment */
    RC_AUDIO_FX_NORMALIZE,      /* Normalize levels */
    RC_AUDIO_FX_COMPRESSOR,     /* Dynamic range compression */
    RC_AUDIO_FX_LIMITER,        /* Peak limiter */
    RC_AUDIO_FX_GATE,           /* Noise gate */
    RC_AUDIO_FX_EXPANDER,       /* Dynamic expander */
    
    /* EQ */
    RC_AUDIO_FX_EQ_PARAMETRIC,  /* Parametric EQ */
    RC_AUDIO_FX_EQ_GRAPHIC,     /* Graphic EQ */
    RC_AUDIO_FX_LOWPASS,        /* Low pass filter */
    RC_AUDIO_FX_HIGHPASS,       /* High pass filter */
    RC_AUDIO_FX_BANDPASS,       /* Band pass filter */
    
    /* Spatial */
    RC_AUDIO_FX_REVERB,         /* Reverb (room sim) */
    RC_AUDIO_FX_DELAY,          /* Echo/delay */
    RC_AUDIO_FX_CHORUS,         /* Chorus effect */
    RC_AUDIO_FX_FLANGER,        /* Flanger */
    RC_AUDIO_FX_PHASER,         /* Phaser */
    RC_AUDIO_FX_SPATIAL_3D,     /* Ketivee Oohm spatial */
    
    /* Restoration */
    RC_AUDIO_FX_DENOISE,        /* Noise reduction */
    RC_AUDIO_FX_DECLICK,        /* Click removal */
    RC_AUDIO_FX_DEHUM,          /* Hum removal */
    
    /* Pitch/Time */
    RC_AUDIO_FX_PITCH_SHIFT,    /* Pitch shifter */
    RC_AUDIO_FX_TIME_STRETCH,   /* Time stretch */
    
    RC_AUDIO_FX_COUNT
} rc_audio_fx_type_t;

typedef struct rc_audio_fx rc_audio_fx_t;

/**
 * Create audio effect.
 */
rc_audio_fx_t *rc_audio_fx_create(rc_audio_fx_type_t type);

/**
 * Destroy audio effect.
 */
void rc_audio_fx_destroy(rc_audio_fx_t *fx);

/**
 * Set effect parameter.
 */
void rc_audio_fx_set_param(rc_audio_fx_t *fx, const char *name, float value);

/**
 * Get effect parameter.
 */
float rc_audio_fx_get_param(rc_audio_fx_t *fx, const char *name);

/**
 * Process audio through effect.
 */
void rc_audio_fx_process(rc_audio_fx_t *fx, rc_audio_buffer_t *buffer);

/* ============================================================================
 * Multi-track Mixer
 * ============================================================================ */

#define RC_MIXER_MAX_TRACKS 32

typedef struct {
    int id;
    char name[64];
    float volume;               /* 0.0 - 2.0 (0-200%) */
    float pan;                  /* -1.0 (L) to 1.0 (R) */
    bool muted;
    bool solo;
    
    /* Effects chain */
    rc_audio_fx_t *effects[16];
    int effect_count;
} rc_mixer_track_t;

typedef struct rc_mixer rc_mixer_t;

/**
 * Create mixer.
 */
rc_mixer_t *rc_mixer_create(rc_audio_config_t config);

/**
 * Destroy mixer.
 */
void rc_mixer_destroy(rc_mixer_t *mixer);

/**
 * Add track.
 */
int rc_mixer_add_track(rc_mixer_t *mixer, const char *name);

/**
 * Remove track.
 */
void rc_mixer_remove_track(rc_mixer_t *mixer, int track_id);

/**
 * Get track.
 */
rc_mixer_track_t *rc_mixer_get_track(rc_mixer_t *mixer, int track_id);

/**
 * Set master volume.
 */
void rc_mixer_set_master_volume(rc_mixer_t *mixer, float volume);

/**
 * Add audio to track.
 */
void rc_mixer_feed_track(rc_mixer_t *mixer, int track_id, 
                          rc_audio_buffer_t *buffer, rc_time_t time);

/**
 * Mix all tracks to output.
 */
void rc_mixer_render(rc_mixer_t *mixer, rc_audio_buffer_t *output,
                      rc_time_t start, rc_duration_t duration);

/* ============================================================================
 * Audio Ducking
 * ============================================================================ */

typedef struct {
    float threshold;            /* Trigger level (0.0-1.0) */
    float reduction;            /* Reduction amount (dB) */
    float attack_ms;            /* Attack time */
    float release_ms;           /* Release time */
} rc_ducking_config_t;

/**
 * Apply audio ducking (lower music when voice active).
 */
void rc_audio_apply_ducking(rc_audio_buffer_t *target,
                             rc_audio_buffer_t *sidechain,
                             rc_ducking_config_t config);

/* ============================================================================
 * Metering
 * ============================================================================ */

typedef struct {
    float peak_l;               /* Left channel peak */
    float peak_r;               /* Right channel peak */
    float rms_l;                /* Left channel RMS */
    float rms_r;                /* Right channel RMS */
    float lufs;                 /* Loudness (LUFS) */
} rc_audio_levels_t;

/**
 * Get audio levels for metering.
 */
rc_audio_levels_t rc_audio_get_levels(rc_audio_buffer_t *buffer);

/* ============================================================================
 * Ketivee Oohm Integration
 * ============================================================================ */

/**
 * Enable Ketivee Oohm 3D spatial processing.
 */
void rc_audio_enable_spatial(rc_audio_engine_t *engine, bool enabled);

/**
 * Set 3D source position for clip.
 */
void rc_audio_set_spatial_position(rc_clip_t *clip, float x, float y, float z);

/**
 * Apply room reverb simulation.
 */
void rc_audio_set_room_reverb(rc_audio_engine_t *engine, 
                               float room_size, float damping, float wet);

#ifdef __cplusplus
}
#endif

#endif /* RC_AUDIO_H */
