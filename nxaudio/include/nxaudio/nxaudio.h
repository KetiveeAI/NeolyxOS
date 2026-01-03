/*
 * NXAudio - NeolyxOS Native Spatial Audio System
 * 
 * Production-grade audio API with:
 * - 3D spatial audio (own HRTF engine, no Dolby dependency)
 * - Real-time convolution
 * - Object-based mixing
 * - Low-latency playback
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_H
#define NXAUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ============ Version ============ */
#define NXAUDIO_VERSION_MAJOR   1
#define NXAUDIO_VERSION_MINOR   0
#define NXAUDIO_VERSION_PATCH   0
#define NXAUDIO_VERSION_STRING  "1.0.0"

/* ============ Handle Types ============ */
typedef int32_t nxaudio_context_t;
typedef int32_t nxaudio_device_t;
typedef int32_t nxaudio_object_t;
typedef int32_t nxaudio_stream_t;
typedef int32_t nxaudio_buffer_t;

#define NXAUDIO_INVALID_HANDLE (-1)

/* ============ Error Codes ============ */
typedef enum {
    NXAUDIO_SUCCESS             = 0,
    NXAUDIO_ERROR_INIT          = -1,
    NXAUDIO_ERROR_INVALID       = -2,
    NXAUDIO_ERROR_NO_MEMORY     = -3,
    NXAUDIO_ERROR_NO_DEVICE     = -4,
    NXAUDIO_ERROR_IO            = -5,
    NXAUDIO_ERROR_FORMAT        = -6,
    NXAUDIO_ERROR_TIMEOUT       = -7,
    NXAUDIO_ERROR_BUSY          = -8,
} nxaudio_error_t;

/* ============ Audio Formats ============ */
typedef enum {
    NXAUDIO_FORMAT_S16,         /* Signed 16-bit */
    NXAUDIO_FORMAT_S24,         /* Signed 24-bit */
    NXAUDIO_FORMAT_S32,         /* Signed 32-bit */
    NXAUDIO_FORMAT_F32,         /* Float 32-bit */
} nxaudio_format_t;

/* ============ Sample Rates ============ */
typedef enum {
    NXAUDIO_RATE_44100  = 44100,
    NXAUDIO_RATE_48000  = 48000,
    NXAUDIO_RATE_96000  = 96000,
    NXAUDIO_RATE_192000 = 192000,
} nxaudio_rate_t;

/* ============ Distance Models ============ */
typedef enum {
    NXAUDIO_DISTANCE_INVERSE,       /* 1/distance */
    NXAUDIO_DISTANCE_LINEAR,        /* Linear falloff */
    NXAUDIO_DISTANCE_EXPONENTIAL,   /* Exponential falloff */
    NXAUDIO_DISTANCE_NONE,          /* No attenuation */
} nxaudio_distance_model_t;

/* ============ 3D Vector ============ */
typedef struct {
    float x, y, z;
} nxaudio_vec3_t;

/* ============ Quaternion (orientation) ============ */
typedef struct {
    float x, y, z, w;
} nxaudio_quat_t;

/* ============ Listener ============ */
typedef struct {
    nxaudio_vec3_t position;
    nxaudio_vec3_t velocity;
    nxaudio_quat_t orientation;
    float          gain;
} nxaudio_listener_t;

/* ============ Audio Object Config ============ */
typedef struct {
    nxaudio_vec3_t position;
    nxaudio_vec3_t velocity;
    float          gain;            /* 0.0 - inf */
    float          pitch;           /* 0.5 - 2.0 */
    float          min_distance;    /* Distance for full volume */
    float          max_distance;    /* Distance for silence */
    float          rolloff;         /* Rolloff factor */
    uint8_t        looping;
    uint8_t        spatial;         /* Enable 3D spatialization */
    uint8_t        reverb;          /* Enable reverb */
    uint8_t        occlusion;       /* Enable occlusion */
} nxaudio_object_config_t;

/* ============ Device Info ============ */
typedef struct {
    char            name[128];
    char            id[64];
    uint32_t        channels;
    uint32_t        sample_rate;
    nxaudio_format_t format;
    uint8_t         is_default;
    uint8_t         is_output;
} nxaudio_device_info_t;

/* ============ Context Config ============ */
typedef struct {
    uint32_t        sample_rate;
    uint32_t        buffer_size;    /* Frames per buffer */
    uint32_t        max_objects;
    nxaudio_distance_model_t distance_model;
    float           doppler_factor;
    float           speed_of_sound;
} nxaudio_context_config_t;

/* ============ HRTF Info ============ */
typedef struct {
    char     name[64];
    uint32_t sample_rate;
    uint32_t ir_length;             /* Impulse response length */
    uint32_t num_positions;
} nxaudio_hrtf_info_t;

/* ===========================================================================
 * System API
 * =========================================================================*/

/**
 * Initialize NXAudio system
 * Must be called before any other function
 */
nxaudio_error_t nxaudio_init(void);

/**
 * Shutdown NXAudio system
 * Releases all resources
 */
void nxaudio_shutdown(void);

/**
 * Get version string
 */
const char* nxaudio_version(void);

/**
 * Get last error message
 */
const char* nxaudio_error_string(nxaudio_error_t error);

/* ===========================================================================
 * Device API
 * =========================================================================*/

/**
 * Get number of audio devices
 */
int nxaudio_device_count(void);

/**
 * Get device info by index
 */
nxaudio_error_t nxaudio_device_info(int index, nxaudio_device_info_t *info);

/**
 * Get default output device
 */
nxaudio_device_t nxaudio_device_default_output(void);

/**
 * Get default input device
 */
nxaudio_device_t nxaudio_device_default_input(void);

/* ===========================================================================
 * Context API
 * =========================================================================*/

/**
 * Create audio context with default config
 */
nxaudio_context_t nxaudio_context_create(void);

/**
 * Create audio context with custom config
 */
nxaudio_context_t nxaudio_context_create_ex(const nxaudio_context_config_t *config);

/**
 * Destroy audio context
 */
void nxaudio_context_destroy(nxaudio_context_t ctx);

/**
 * Set listener properties
 */
nxaudio_error_t nxaudio_context_set_listener(nxaudio_context_t ctx,
                                              const nxaudio_listener_t *listener);

/**
 * Get listener properties
 */
nxaudio_error_t nxaudio_context_get_listener(nxaudio_context_t ctx,
                                              nxaudio_listener_t *listener);

/**
 * Set master gain
 */
nxaudio_error_t nxaudio_context_set_gain(nxaudio_context_t ctx, float gain);

/**
 * Suspend audio processing
 */
nxaudio_error_t nxaudio_context_suspend(nxaudio_context_t ctx);

/**
 * Resume audio processing
 */
nxaudio_error_t nxaudio_context_resume(nxaudio_context_t ctx);

/* ===========================================================================
 * Buffer API
 * =========================================================================*/

/**
 * Create audio buffer from file
 */
nxaudio_buffer_t nxaudio_buffer_create(const char *filepath);

/**
 * Create audio buffer from memory
 */
nxaudio_buffer_t nxaudio_buffer_create_mem(const void *data, size_t size,
                                            nxaudio_format_t format,
                                            uint32_t sample_rate,
                                            uint32_t channels);

/**
 * Destroy audio buffer
 */
void nxaudio_buffer_destroy(nxaudio_buffer_t buffer);

/**
 * Get buffer duration in seconds
 */
float nxaudio_buffer_duration(nxaudio_buffer_t buffer);

/* ===========================================================================
 * Object API (3D Spatial Sources)
 * =========================================================================*/

/**
 * Create audio object with buffer
 */
nxaudio_object_t nxaudio_object_create(nxaudio_context_t ctx,
                                        nxaudio_buffer_t buffer,
                                        const nxaudio_object_config_t *config);

/**
 * Destroy audio object
 */
void nxaudio_object_destroy(nxaudio_object_t obj);

/**
 * Play audio object
 */
nxaudio_error_t nxaudio_object_play(nxaudio_object_t obj);

/**
 * Pause audio object
 */
nxaudio_error_t nxaudio_object_pause(nxaudio_object_t obj);

/**
 * Stop audio object
 */
nxaudio_error_t nxaudio_object_stop(nxaudio_object_t obj);

/**
 * Update audio object properties
 */
nxaudio_error_t nxaudio_object_update(nxaudio_object_t obj,
                                       const nxaudio_object_config_t *config);

/**
 * Set object position
 */
nxaudio_error_t nxaudio_object_set_position(nxaudio_object_t obj,
                                             float x, float y, float z);

/**
 * Set object gain
 */
nxaudio_error_t nxaudio_object_set_gain(nxaudio_object_t obj, float gain);

/**
 * Check if object is playing
 */
int nxaudio_object_is_playing(nxaudio_object_t obj);

/* ===========================================================================
 * HRTF API (Spatial Audio Engine)
 * =========================================================================*/

/**
 * Load HRTF dataset (SOFA format)
 */
nxaudio_error_t nxaudio_hrtf_load(nxaudio_context_t ctx, const char *filepath);

/**
 * Load built-in default HRTF
 */
nxaudio_error_t nxaudio_hrtf_load_default(nxaudio_context_t ctx);

/**
 * Get current HRTF info
 */
nxaudio_error_t nxaudio_hrtf_info(nxaudio_context_t ctx, nxaudio_hrtf_info_t *info);

/**
 * Enable/disable HRTF processing
 */
nxaudio_error_t nxaudio_hrtf_enable(nxaudio_context_t ctx, int enabled);

/* ===========================================================================
 * Reverb API
 * =========================================================================*/

/**
 * Set room reverb parameters
 */
nxaudio_error_t nxaudio_reverb_set(nxaudio_context_t ctx,
                                    float room_size,
                                    float damping,
                                    float wet,
                                    float dry);

/**
 * Enable/disable reverb
 */
nxaudio_error_t nxaudio_reverb_enable(nxaudio_context_t ctx, int enabled);

/* ===========================================================================
 * Streaming API
 * =========================================================================*/

/**
 * Create audio stream for real-time input
 */
nxaudio_stream_t nxaudio_stream_create(nxaudio_context_t ctx,
                                        uint32_t sample_rate,
                                        uint32_t channels,
                                        nxaudio_format_t format);

/**
 * Write samples to stream
 */
nxaudio_error_t nxaudio_stream_write(nxaudio_stream_t stream,
                                      const void *data,
                                      size_t frames);

/**
 * Destroy stream
 */
void nxaudio_stream_destroy(nxaudio_stream_t stream);

#ifdef __cplusplus
}
#endif

#endif /* NXAUDIO_H */
