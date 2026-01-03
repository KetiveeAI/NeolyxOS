/*
 * NXAudio Resampler Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "resample.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SINC_WINDOW_SIZE    16
#define PI                  3.14159265358979323846

/* ============ Sinc Function ============ */

static float sinc(float x) {
    if (fabsf(x) < 1e-6f) return 1.0f;
    float pix = PI * x;
    return sinf(pix) / pix;
}

/* Lanczos window */
static float lanczos(float x, float a) {
    if (fabsf(x) >= a) return 0.0f;
    return sinc(x) * sinc(x / a);
}

/* ============ Initialization ============ */

int nx_resample_init(nx_resampler_t *rs, 
                      uint32_t input_rate, 
                      uint32_t output_rate,
                      uint32_t channels,
                      nx_resample_quality_t quality) {
    if (!rs || input_rate == 0 || output_rate == 0 || channels == 0) {
        return -1;
    }
    
    memset(rs, 0, sizeof(*rs));
    
    rs->input_rate = input_rate;
    rs->output_rate = output_rate;
    rs->ratio = (double)output_rate / (double)input_rate;
    rs->channels = channels;
    rs->quality = quality;
    rs->position = 0.0;
    
    /* Allocate history buffer for sinc filter */
    if (quality == NX_RESAMPLE_HIGH) {
        rs->history_size = SINC_WINDOW_SIZE * 2 * channels;
    } else {
        rs->history_size = 4 * channels;
    }
    
    rs->history = (float*)calloc(rs->history_size, sizeof(float));
    if (!rs->history) return -1;
    
    rs->history_pos = 0;
    
    return 0;
}

void nx_resample_destroy(nx_resampler_t *rs) {
    if (!rs) return;
    
    if (rs->history) {
        free(rs->history);
    }
    memset(rs, 0, sizeof(*rs));
}

void nx_resample_reset(nx_resampler_t *rs) {
    if (!rs) return;
    
    rs->position = 0.0;
    if (rs->history) {
        memset(rs->history, 0, rs->history_size * sizeof(float));
    }
    rs->history_pos = 0;
}

size_t nx_resample_output_frames(nx_resampler_t *rs, size_t input_frames) {
    if (!rs) return 0;
    return (size_t)(input_frames * rs->ratio + 0.5);
}

size_t nx_resample_input_frames(nx_resampler_t *rs, size_t output_frames) {
    if (!rs || rs->ratio == 0) return 0;
    return (size_t)(output_frames / rs->ratio + 0.5);
}

/* ============ Linear Interpolation ============ */

static size_t resample_linear(nx_resampler_t *rs,
                               const float *in, size_t in_frames,
                               float *out, size_t out_frames) {
    double step = 1.0 / rs->ratio;
    size_t out_count = 0;
    uint32_t ch = rs->channels;
    
    while (out_count < out_frames && rs->position < (double)in_frames - 1) {
        size_t index = (size_t)rs->position;
        double frac = rs->position - (double)index;
        
        for (uint32_t c = 0; c < ch; c++) {
            float s0 = in[index * ch + c];
            float s1 = in[(index + 1) * ch + c];
            out[out_count * ch + c] = s0 + (float)frac * (s1 - s0);
        }
        
        out_count++;
        rs->position += step;
    }
    
    /* Adjust position for next call */
    rs->position -= (double)in_frames;
    if (rs->position < 0) rs->position = 0;
    
    return out_count;
}

/* ============ Cubic Interpolation ============ */

static size_t resample_cubic(nx_resampler_t *rs,
                              const float *in, size_t in_frames,
                              float *out, size_t out_frames) {
    double step = 1.0 / rs->ratio;
    size_t out_count = 0;
    uint32_t ch = rs->channels;
    
    while (out_count < out_frames && rs->position < (double)in_frames - 2) {
        size_t index = (size_t)rs->position;
        double t = rs->position - (double)index;
        
        for (uint32_t c = 0; c < ch; c++) {
            /* Get 4 samples for cubic interpolation */
            float y0 = (index > 0) ? in[(index - 1) * ch + c] : in[c];
            float y1 = in[index * ch + c];
            float y2 = in[(index + 1) * ch + c];
            float y3 = (index + 2 < in_frames) ? in[(index + 2) * ch + c] : y2;
            
            /* Catmull-Rom spline */
            float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float a2 = -0.5f * y0 + 0.5f * y2;
            float a3 = y1;
            
            float t2 = (float)(t * t);
            float t3 = t2 * (float)t;
            
            out[out_count * ch + c] = a0 * t3 + a1 * t2 + a2 * (float)t + a3;
        }
        
        out_count++;
        rs->position += step;
    }
    
    rs->position -= (double)in_frames;
    if (rs->position < 0) rs->position = 0;
    
    return out_count;
}

/* ============ Sinc Interpolation ============ */

static size_t resample_sinc(nx_resampler_t *rs,
                             const float *in, size_t in_frames,
                             float *out, size_t out_frames) {
    double step = 1.0 / rs->ratio;
    size_t out_count = 0;
    uint32_t ch = rs->channels;
    int window = SINC_WINDOW_SIZE;
    
    while (out_count < out_frames && rs->position < (double)in_frames - window) {
        size_t index = (size_t)rs->position;
        double frac = rs->position - (double)index;
        
        for (uint32_t c = 0; c < ch; c++) {
            float sum = 0.0f;
            float weight_sum = 0.0f;
            
            for (int i = -window; i < window; i++) {
                size_t si = index + i;
                if (si >= 0 && si < in_frames) {
                    float w = lanczos((float)(i - frac), (float)window);
                    sum += in[si * ch + c] * w;
                    weight_sum += w;
                }
            }
            
            out[out_count * ch + c] = (weight_sum > 0) ? sum / weight_sum : 0.0f;
        }
        
        out_count++;
        rs->position += step;
    }
    
    rs->position -= (double)in_frames;
    if (rs->position < 0) rs->position = 0;
    
    return out_count;
}

/* ============ Public API ============ */

size_t nx_resample_process(nx_resampler_t *rs,
                            const float *in, size_t in_frames,
                            float *out, size_t out_frames) {
    if (!rs || !in || !out || in_frames == 0) {
        return 0;
    }
    
    /* No resampling needed */
    if (rs->input_rate == rs->output_rate) {
        size_t copy = (in_frames < out_frames) ? in_frames : out_frames;
        memcpy(out, in, copy * rs->channels * sizeof(float));
        return copy;
    }
    
    switch (rs->quality) {
        case NX_RESAMPLE_FAST:
            return resample_linear(rs, in, in_frames, out, out_frames);
            
        case NX_RESAMPLE_MEDIUM:
            return resample_cubic(rs, in, in_frames, out, out_frames);
            
        case NX_RESAMPLE_HIGH:
            return resample_sinc(rs, in, in_frames, out, out_frames);
            
        default:
            return resample_linear(rs, in, in_frames, out, out_frames);
    }
}
