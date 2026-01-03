/*
 * NXAudio DSP Effects
 * 
 * Real-time audio effects:
 * - Volume/Gain
 * - Pan
 * - Low-pass filter
 * - High-pass filter
 * - Normalize
 * - Float-to-int conversion
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_EFFECTS_H
#define NXAUDIO_EFFECTS_H

#include <stdint.h>
#include <stddef.h>

/* ============ Filter Types ============ */
typedef enum {
    NX_FILTER_LOWPASS,
    NX_FILTER_HIGHPASS,
    NX_FILTER_BANDPASS,
    NX_FILTER_NOTCH,
} nx_filter_type_t;

/* ============ Biquad Filter ============ */
typedef struct {
    /* Coefficients */
    float a0, a1, a2;
    float b1, b2;
    
    /* State */
    float x1, x2;
    float y1, y2;
} nx_biquad_t;

/* ============ Effects Chain ============ */
typedef struct {
    float       gain;           /* Gain multiplier */
    float       pan;            /* -1 to 1 */
    nx_biquad_t lowpass;
    nx_biquad_t highpass;
    int         lowpass_enabled;
    int         highpass_enabled;
    float       fade_gain;      /* For fade in/out */
    float       fade_target;
    float       fade_rate;
} nx_effects_t;

/* ============ Basic DSP Functions ============ */

/**
 * Apply gain to buffer
 */
void nx_dsp_gain(float *buffer, size_t samples, float gain);

/**
 * Apply stereo pan
 */
void nx_dsp_pan(float *left, float *right, size_t samples, float pan);

/**
 * Normalize to peak
 */
void nx_dsp_normalize(float *buffer, size_t samples, float target_peak);

/**
 * Soft clip to prevent distortion
 */
void nx_dsp_soft_clip(float *buffer, size_t samples);

/**
 * Hard clip/limit
 */
void nx_dsp_hard_clip(float *buffer, size_t samples, float threshold);

/**
 * Mix two buffers (a += b * gain)
 */
void nx_dsp_mix(float *a, const float *b, size_t samples, float gain);

/**
 * Fade in/out
 */
void nx_dsp_fade(float *buffer, size_t samples, float start_gain, float end_gain);

/* ============ Float <-> Int Conversion ============ */

/**
 * Float to signed 16-bit
 */
void nx_dsp_float_to_s16(const float *in, int16_t *out, size_t samples);

/**
 * Float to signed 24-bit (packed)
 */
void nx_dsp_float_to_s24(const float *in, uint8_t *out, size_t samples);

/**
 * Float to signed 32-bit
 */
void nx_dsp_float_to_s32(const float *in, int32_t *out, size_t samples);

/**
 * Signed 16-bit to float
 */
void nx_dsp_s16_to_float(const int16_t *in, float *out, size_t samples);

/* ============ Biquad Filter ============ */

/**
 * Initialize biquad filter
 */
void nx_biquad_init(nx_biquad_t *bq, nx_filter_type_t type, 
                    float freq, float sample_rate, float q);

/**
 * Reset filter state
 */
void nx_biquad_reset(nx_biquad_t *bq);

/**
 * Process samples through filter
 */
void nx_biquad_process(nx_biquad_t *bq, float *buffer, size_t samples);

/* ============ Effects Chain ============ */

/**
 * Initialize effects chain
 */
void nx_effects_init(nx_effects_t *fx, float sample_rate);

/**
 * Set lowpass frequency
 */
void nx_effects_set_lowpass(nx_effects_t *fx, float freq, float sample_rate);

/**
 * Set highpass frequency
 */
void nx_effects_set_highpass(nx_effects_t *fx, float freq, float sample_rate);

/**
 * Process buffer through effects chain
 */
void nx_effects_process(nx_effects_t *fx, float *buffer, size_t samples, int channels);

#endif /* NXAUDIO_EFFECTS_H */
