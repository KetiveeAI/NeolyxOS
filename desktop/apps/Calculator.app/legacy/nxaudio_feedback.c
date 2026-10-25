/*
 * NeolyxOS Calculator.app - Audio Feedback Implementation
 * 
 * Low-latency audio feedback for button presses.
 * Uses NXAudio when available, otherwise silent stubs.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#include <stdint.h>
#include <stddef.h>
#include "nxaudio_feedback.h"

/* ============ Build Configuration ============ */

#ifdef NXAUDIO_REAL
#include <nxaudio/nxaudio.h>
#define AUDIO_AVAILABLE 1
#else
#define AUDIO_AVAILABLE 0
#endif

/* ============ Audio State ============ */

static struct {
    int initialized;
    int enabled;
    float volume;
#if AUDIO_AVAILABLE
    nxaudio_context_t ctx;
    nxaudio_buffer_t buf_click_number;
    nxaudio_buffer_t buf_click_operator;
    nxaudio_buffer_t buf_click_function;
    nxaudio_buffer_t buf_result;
    nxaudio_buffer_t buf_error;
    nxaudio_buffer_t buf_clear;
    nxaudio_buffer_t buf_memory;
#endif
} g_audio = {
    .initialized = 0,
    .enabled = 1,
    .volume = 0.5f
};

/* ============ Tone Generation ============ */

#if AUDIO_AVAILABLE

/* Generate a simple sine wave tone */
static void generate_tone(int16_t *samples, int count, float freq, float duration, float fade) {
    float sample_rate = 48000.0f;
    int samples_to_write = (int)(duration * sample_rate);
    if (samples_to_write > count) samples_to_write = count;
    
    float two_pi = 6.28318530718f;
    float amplitude = 16000.0f;
    
    for (int i = 0; i < samples_to_write; i++) {
        float t = (float)i / sample_rate;
        float env = 1.0f;
        
        /* Apply fade out */
        if (fade > 0 && t > duration - fade) {
            env = (duration - t) / fade;
        }
        
        /* Sine wave */
        float phase = two_pi * freq * t;
        float sample = amplitude * env;
        
        /* Simple sin approximation */
        float x = phase;
        while (x > 3.14159f) x -= 6.28318f;
        while (x < -3.14159f) x += 6.28318f;
        float sin_val = x - (x * x * x) / 6.0f + (x * x * x * x * x) / 120.0f;
        
        samples[i] = (int16_t)(sample * sin_val);
    }
    
    /* Zero remaining */
    for (int i = samples_to_write; i < count; i++) {
        samples[i] = 0;
    }
}

/* Generate click sound (short burst with harmonics) */
static void generate_click(int16_t *samples, int count, float duration) {
    float sample_rate = 48000.0f;
    int samples_to_write = (int)(duration * sample_rate);
    if (samples_to_write > count) samples_to_write = count;
    
    float amplitude = 12000.0f;
    float decay = 20.0f;
    
    for (int i = 0; i < samples_to_write; i++) {
        float t = (float)i / sample_rate;
        float env = amplitude * (1.0f - t / duration);
        
        /* Fast decay for click */
        if (t > 0.005f) {
            env *= (1.0f - (t - 0.005f) / (duration - 0.005f));
        }
        
        /* Mix of frequencies for click */
        float sample = 0;
        sample += 0.5f * env * ((i % 10 < 5) ? 1.0f : -1.0f);  /* Noise-like */
        
        samples[i] = (int16_t)sample;
    }
    
    for (int i = samples_to_write; i < count; i++) {
        samples[i] = 0;
    }
}

static nxaudio_buffer_t create_tone_buffer(float freq, float duration, float fade) {
    int samples = (int)(48000 * duration) + 100;
    int16_t *data = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!data) return NXAUDIO_INVALID_HANDLE;
    
    generate_tone(data, samples, freq, duration, fade);
    
    nxaudio_buffer_t buf = nxaudio_buffer_create_mem(
        data, samples * sizeof(int16_t),
        NXAUDIO_FORMAT_S16, 48000, 1
    );
    
    free(data);
    return buf;
}

static nxaudio_buffer_t create_click_buffer(float duration) {
    int samples = (int)(48000 * duration) + 100;
    int16_t *data = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!data) return NXAUDIO_INVALID_HANDLE;
    
    generate_click(data, samples, duration);
    
    nxaudio_buffer_t buf = nxaudio_buffer_create_mem(
        data, samples * sizeof(int16_t),
        NXAUDIO_FORMAT_S16, 48000, 1
    );
    
    free(data);
    return buf;
}

#endif /* AUDIO_AVAILABLE */

/* ============ Public API ============ */

int audio_init(void) {
    if (g_audio.initialized) return 0;
    
#if AUDIO_AVAILABLE
    if (nxaudio_init() != NXAUDIO_SUCCESS) {
        return -1;
    }
    
    g_audio.ctx = nxaudio_context_create();
    if (g_audio.ctx == NXAUDIO_INVALID_HANDLE) {
        nxaudio_shutdown();
        return -1;
    }
    
    /* Create sound buffers */
    g_audio.buf_click_number = create_click_buffer(0.020f);     /* 20ms click */
    g_audio.buf_click_operator = create_click_buffer(0.030f);   /* 30ms click */
    g_audio.buf_click_function = create_click_buffer(0.025f);   /* 25ms click */
    g_audio.buf_result = create_tone_buffer(880.0f, 0.050f, 0.020f);  /* A5 success */
    g_audio.buf_error = create_tone_buffer(220.0f, 0.100f, 0.030f);   /* A3 error */
    g_audio.buf_clear = create_tone_buffer(440.0f, 0.040f, 0.015f);   /* A4 clear */
    g_audio.buf_memory = create_tone_buffer(660.0f, 0.035f, 0.010f);  /* E5 memory */
#endif
    
    g_audio.initialized = 1;
    return 0;
}

void audio_shutdown(void) {
    if (!g_audio.initialized) return;
    
#if AUDIO_AVAILABLE
    nxaudio_buffer_destroy(g_audio.buf_click_number);
    nxaudio_buffer_destroy(g_audio.buf_click_operator);
    nxaudio_buffer_destroy(g_audio.buf_click_function);
    nxaudio_buffer_destroy(g_audio.buf_result);
    nxaudio_buffer_destroy(g_audio.buf_error);
    nxaudio_buffer_destroy(g_audio.buf_clear);
    nxaudio_buffer_destroy(g_audio.buf_memory);
    
    nxaudio_context_destroy(g_audio.ctx);
    nxaudio_shutdown();
#endif
    
    g_audio.initialized = 0;
}

void audio_play(audio_feedback_t type) {
    if (!g_audio.initialized || !g_audio.enabled) return;
    
#if AUDIO_AVAILABLE
    nxaudio_buffer_t buf = NXAUDIO_INVALID_HANDLE;
    
    switch (type) {
        case AUDIO_CLICK_NUMBER:
            buf = g_audio.buf_click_number;
            break;
        case AUDIO_CLICK_OPERATOR:
            buf = g_audio.buf_click_operator;
            break;
        case AUDIO_CLICK_FUNCTION:
            buf = g_audio.buf_click_function;
            break;
        case AUDIO_RESULT:
            buf = g_audio.buf_result;
            break;
        case AUDIO_ERROR:
            buf = g_audio.buf_error;
            break;
        case AUDIO_CLEAR:
            buf = g_audio.buf_clear;
            break;
        case AUDIO_MEMORY:
            buf = g_audio.buf_memory;
            break;
    }
    
    if (buf != NXAUDIO_INVALID_HANDLE) {
        nxaudio_object_config_t config = {
            .position = {0, 0, 0},
            .velocity = {0, 0, 0},
            .gain = g_audio.volume,
            .pitch = 1.0f,
            .looping = 0,
            .spatial = 0,  /* Non-spatial for UI sounds */
            .reverb = 0,
            .occlusion = 0
        };
        
        nxaudio_object_t obj = nxaudio_object_create(g_audio.ctx, buf, &config);
        if (obj != NXAUDIO_INVALID_HANDLE) {
            nxaudio_object_play(obj);
            /* Object auto-destroys after playback in non-looping mode */
        }
    }
#else
    (void)type;  /* Silence unused warning */
#endif
}

void haptic_trigger(haptic_feedback_t type) {
    /* Haptic feedback stub - will be implemented when NXHaptic API is available */
    (void)type;
    
    /* Future implementation:
     * #ifdef NXHAPTIC_REAL
     * switch (type) {
     *     case HAPTIC_LIGHT:
     *         nxhaptic_trigger(NXHAPTIC_TAP_LIGHT);
     *         break;
     *     ...
     * }
     * #endif
     */
}

void audio_set_enabled(int enabled) {
    g_audio.enabled = enabled ? 1 : 0;
}

int audio_is_enabled(void) {
    return g_audio.enabled;
}

void audio_set_volume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    g_audio.volume = volume;
}
