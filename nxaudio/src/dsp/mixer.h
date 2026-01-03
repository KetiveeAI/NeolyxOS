/*
 * NXAudio Mixer - Multi-Channel Audio Mixing
 * 
 * Mix multiple audio sources with:
 * - Per-channel volume and pan
 * - Master volume
 * - Clipping protection
 * - Low-latency processing
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_MIXER_H
#define NXAUDIO_MIXER_H

#include <stdint.h>
#include <stddef.h>

#define NX_MIXER_MAX_CHANNELS   32

/* ============ Channel State ============ */
typedef struct {
    int         active;
    float       volume;         /* 0.0 - 1.0 */
    float       pan;            /* -1.0 (L) to 1.0 (R) */
    int         muted;
    int         solo;
    
    /* Source */
    const float *buffer;
    size_t       buffer_frames;
    size_t       position;
    int          looping;
} nx_mixer_channel_t;

/* ============ Mixer ============ */
typedef struct {
    nx_mixer_channel_t channels[NX_MIXER_MAX_CHANNELS];
    int         num_active;
    
    float       master_volume;
    int         master_mute;
    
    uint32_t    sample_rate;
    uint32_t    output_channels;
    
    /* Output buffer */
    float      *output;
    size_t      output_size;
} nx_mixer_t;

/* ============ Public API ============ */

/**
 * Initialize mixer
 */
void nx_mixer_init(nx_mixer_t *mixer, uint32_t sample_rate, uint32_t channels);

/**
 * Cleanup mixer
 */
void nx_mixer_destroy(nx_mixer_t *mixer);

/**
 * Add channel, returns channel index or -1
 */
int nx_mixer_add_channel(nx_mixer_t *mixer, const float *buffer, 
                          size_t frames, int loop);

/**
 * Remove channel
 */
void nx_mixer_remove_channel(nx_mixer_t *mixer, int channel);

/**
 * Set channel volume
 */
void nx_mixer_set_volume(nx_mixer_t *mixer, int channel, float volume);

/**
 * Set channel pan (-1 left, 0 center, +1 right)
 */
void nx_mixer_set_pan(nx_mixer_t *mixer, int channel, float pan);

/**
 * Mute/unmute channel
 */
void nx_mixer_mute(nx_mixer_t *mixer, int channel, int mute);

/**
 * Set master volume
 */
void nx_mixer_set_master(nx_mixer_t *mixer, float volume);

/**
 * Process and mix all channels
 * @param out: Output buffer (stereo interleaved)
 * @param frames: Number of frames to produce
 */
void nx_mixer_process(nx_mixer_t *mixer, float *out, size_t frames);

/**
 * Check if channel is still playing
 */
int nx_mixer_channel_playing(nx_mixer_t *mixer, int channel);

#endif /* NXAUDIO_MIXER_H */
