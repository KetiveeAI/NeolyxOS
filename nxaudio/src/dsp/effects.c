/*
 * NXAudio DSP Effects Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "effects.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f

/* ============ Basic DSP Functions ============ */

void nx_dsp_gain(float *buffer, size_t samples, float gain) {
    if (!buffer) return;
    for (size_t i = 0; i < samples; i++) {
        buffer[i] *= gain;
    }
}

void nx_dsp_pan(float *left, float *right, size_t samples, float pan) {
    if (!left || !right) return;
    
    /* Constant power panning */
    float angle = (pan + 1.0f) * 0.25f * PI;
    float l_gain = cosf(angle);
    float r_gain = sinf(angle);
    
    for (size_t i = 0; i < samples; i++) {
        float mono = (left[i] + right[i]) * 0.5f;
        left[i] = mono * l_gain;
        right[i] = mono * r_gain;
    }
}

void nx_dsp_normalize(float *buffer, size_t samples, float target_peak) {
    if (!buffer || samples == 0) return;
    
    /* Find current peak */
    float peak = 0.0f;
    for (size_t i = 0; i < samples; i++) {
        float abs_val = fabsf(buffer[i]);
        if (abs_val > peak) peak = abs_val;
    }
    
    if (peak < 1e-6f) return;  /* Silence */
    
    /* Apply normalization */
    float gain = target_peak / peak;
    nx_dsp_gain(buffer, samples, gain);
}

void nx_dsp_soft_clip(float *buffer, size_t samples) {
    if (!buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        float x = buffer[i];
        if (x > 1.0f) {
            buffer[i] = 1.0f - expf(-x);
        } else if (x < -1.0f) {
            buffer[i] = -1.0f + expf(x);
        }
    }
}

void nx_dsp_hard_clip(float *buffer, size_t samples, float threshold) {
    if (!buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        if (buffer[i] > threshold) {
            buffer[i] = threshold;
        } else if (buffer[i] < -threshold) {
            buffer[i] = -threshold;
        }
    }
}

void nx_dsp_mix(float *a, const float *b, size_t samples, float gain) {
    if (!a || !b) return;
    
    for (size_t i = 0; i < samples; i++) {
        a[i] += b[i] * gain;
    }
}

void nx_dsp_fade(float *buffer, size_t samples, float start_gain, float end_gain) {
    if (!buffer || samples == 0) return;
    
    float step = (end_gain - start_gain) / (float)samples;
    float gain = start_gain;
    
    for (size_t i = 0; i < samples; i++) {
        buffer[i] *= gain;
        gain += step;
    }
}

/* ============ Float <-> Int Conversion ============ */

void nx_dsp_float_to_s16(const float *in, int16_t *out, size_t samples) {
    if (!in || !out) return;
    
    for (size_t i = 0; i < samples; i++) {
        float s = in[i] * 32768.0f;
        if (s > 32767.0f) s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        out[i] = (int16_t)s;
    }
}

void nx_dsp_float_to_s24(const float *in, uint8_t *out, size_t samples) {
    if (!in || !out) return;
    
    for (size_t i = 0; i < samples; i++) {
        float s = in[i] * 8388608.0f;
        if (s > 8388607.0f) s = 8388607.0f;
        if (s < -8388608.0f) s = -8388608.0f;
        int32_t val = (int32_t)s;
        out[i*3 + 0] = (val >> 0) & 0xFF;
        out[i*3 + 1] = (val >> 8) & 0xFF;
        out[i*3 + 2] = (val >> 16) & 0xFF;
    }
}

void nx_dsp_float_to_s32(const float *in, int32_t *out, size_t samples) {
    if (!in || !out) return;
    
    for (size_t i = 0; i < samples; i++) {
        double s = (double)in[i] * 2147483648.0;
        if (s > 2147483647.0) s = 2147483647.0;
        if (s < -2147483648.0) s = -2147483648.0;
        out[i] = (int32_t)s;
    }
}

void nx_dsp_s16_to_float(const int16_t *in, float *out, size_t samples) {
    if (!in || !out) return;
    
    for (size_t i = 0; i < samples; i++) {
        out[i] = (float)in[i] / 32768.0f;
    }
}

/* ============ Biquad Filter ============ */

void nx_biquad_init(nx_biquad_t *bq, nx_filter_type_t type, 
                    float freq, float sample_rate, float q) {
    if (!bq) return;
    
    memset(bq, 0, sizeof(*bq));
    
    float w0 = 2.0f * PI * freq / sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    
    float a0, a1, a2, b0, b1, b2;
    
    switch (type) {
        case NX_FILTER_LOWPASS:
            b0 = (1.0f - cos_w0) / 2.0f;
            b1 = 1.0f - cos_w0;
            b2 = (1.0f - cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
            
        case NX_FILTER_HIGHPASS:
            b0 = (1.0f + cos_w0) / 2.0f;
            b1 = -(1.0f + cos_w0);
            b2 = (1.0f + cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
            
        case NX_FILTER_BANDPASS:
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
            
        case NX_FILTER_NOTCH:
            b0 = 1.0f;
            b1 = -2.0f * cos_w0;
            b2 = 1.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
            
        default:
            return;
    }
    
    /* Normalize coefficients */
    bq->a0 = b0 / a0;
    bq->a1 = b1 / a0;
    bq->a2 = b2 / a0;
    bq->b1 = a1 / a0;
    bq->b2 = a2 / a0;
}

void nx_biquad_reset(nx_biquad_t *bq) {
    if (!bq) return;
    bq->x1 = bq->x2 = 0.0f;
    bq->y1 = bq->y2 = 0.0f;
}

void nx_biquad_process(nx_biquad_t *bq, float *buffer, size_t samples) {
    if (!bq || !buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        float x = buffer[i];
        float y = bq->a0 * x + bq->a1 * bq->x1 + bq->a2 * bq->x2
                  - bq->b1 * bq->y1 - bq->b2 * bq->y2;
        
        bq->x2 = bq->x1;
        bq->x1 = x;
        bq->y2 = bq->y1;
        bq->y1 = y;
        
        buffer[i] = y;
    }
}

/* ============ Effects Chain ============ */

void nx_effects_init(nx_effects_t *fx, float sample_rate) {
    if (!fx) return;
    
    memset(fx, 0, sizeof(*fx));
    fx->gain = 1.0f;
    fx->pan = 0.0f;
    fx->fade_gain = 1.0f;
    fx->fade_target = 1.0f;
    fx->fade_rate = 0.001f;
    
    /* Default filters */
    nx_biquad_init(&fx->lowpass, NX_FILTER_LOWPASS, 20000.0f, sample_rate, 0.707f);
    nx_biquad_init(&fx->highpass, NX_FILTER_HIGHPASS, 20.0f, sample_rate, 0.707f);
}

void nx_effects_set_lowpass(nx_effects_t *fx, float freq, float sample_rate) {
    if (!fx) return;
    nx_biquad_init(&fx->lowpass, NX_FILTER_LOWPASS, freq, sample_rate, 0.707f);
    fx->lowpass_enabled = 1;
}

void nx_effects_set_highpass(nx_effects_t *fx, float freq, float sample_rate) {
    if (!fx) return;
    nx_biquad_init(&fx->highpass, NX_FILTER_HIGHPASS, freq, sample_rate, 0.707f);
    fx->highpass_enabled = 1;
}

void nx_effects_process(nx_effects_t *fx, float *buffer, size_t samples, int channels) {
    if (!fx || !buffer) return;
    
    size_t total = samples * channels;
    
    /* Apply gain */
    if (fx->gain != 1.0f) {
        nx_dsp_gain(buffer, total, fx->gain);
    }
    
    /* Apply filters (mono processing for now) */
    if (fx->lowpass_enabled) {
        nx_biquad_process(&fx->lowpass, buffer, total);
    }
    
    if (fx->highpass_enabled) {
        nx_biquad_process(&fx->highpass, buffer, total);
    }
    
    /* Apply fade with smoothing */
    if (fabsf(fx->fade_gain - fx->fade_target) > 0.001f) {
        for (size_t i = 0; i < total; i++) {
            fx->fade_gain += (fx->fade_target - fx->fade_gain) * fx->fade_rate;
            buffer[i] *= fx->fade_gain;
        }
    } else if (fx->fade_gain != 1.0f) {
        nx_dsp_gain(buffer, total, fx->fade_gain);
    }
    
    /* Soft clip to prevent distortion */
    nx_dsp_soft_clip(buffer, total);
}
