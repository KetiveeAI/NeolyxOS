/*
 * NXAudio Resampler
 * 
 * Sample rate conversion:
 * - Linear interpolation (fast)
 * - Sinc interpolation (high quality)
 * - Device compatibility
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_RESAMPLE_H
#define NXAUDIO_RESAMPLE_H

#include <stdint.h>
#include <stddef.h>

/* ============ Quality Mode ============ */
typedef enum {
    NX_RESAMPLE_FAST,       /* Linear - low latency */
    NX_RESAMPLE_MEDIUM,     /* Cubic - balanced */
    NX_RESAMPLE_HIGH,       /* Sinc - high quality */
} nx_resample_quality_t;

/* ============ Resampler State ============ */
typedef struct {
    uint32_t input_rate;
    uint32_t output_rate;
    double   ratio;
    uint32_t channels;
    
    nx_resample_quality_t quality;
    
    /* State for interpolation */
    double   position;      /* Fractional position */
    float   *history;       /* Sample history for filtering */
    size_t   history_size;
    size_t   history_pos;
} nx_resampler_t;

/* ============ Public API ============ */

/**
 * Initialize resampler
 */
int nx_resample_init(nx_resampler_t *rs, 
                      uint32_t input_rate, 
                      uint32_t output_rate,
                      uint32_t channels,
                      nx_resample_quality_t quality);

/**
 * Cleanup resampler
 */
void nx_resample_destroy(nx_resampler_t *rs);

/**
 * Reset resampler state
 */
void nx_resample_reset(nx_resampler_t *rs);

/**
 * Calculate output frames for given input
 */
size_t nx_resample_output_frames(nx_resampler_t *rs, size_t input_frames);

/**
 * Calculate input frames needed for output
 */
size_t nx_resample_input_frames(nx_resampler_t *rs, size_t output_frames);

/**
 * Process samples
 * @param in: Input samples (interleaved)
 * @param in_frames: Number of input frames
 * @param out: Output samples (interleaved)
 * @param out_frames: Output buffer size in frames
 * @return: Actual output frames produced
 */
size_t nx_resample_process(nx_resampler_t *rs,
                            const float *in, size_t in_frames,
                            float *out, size_t out_frames);

#endif /* NXAUDIO_RESAMPLE_H */
