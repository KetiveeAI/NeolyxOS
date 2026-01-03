/*
 * NXAudio AIFF Parser
 * 
 * Audio Interchange File Format (Apple)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_AIFF_H
#define NXAUDIO_AIFF_H

#include <stdint.h>
#include <stddef.h>

/* ============ AIFF Chunk Types ============ */
#define AIFF_FORM   0x464F524D  /* 'FORM' */
#define AIFF_AIFF   0x41494646  /* 'AIFF' */
#define AIFF_AIFC   0x41494643  /* 'AIFC' */
#define AIFF_COMM   0x434F4D4D  /* 'COMM' */
#define AIFF_SSND   0x53534E44  /* 'SSND' */

/* ============ AIFF Info ============ */
typedef struct {
    uint16_t channels;
    uint32_t num_frames;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    
    const void *data;
    size_t data_size;
    float duration;
} aiff_info_t;

/* ============ Public API ============ */

int aiff_parse(const void *data, size_t size, aiff_info_t *info);
int aiff_load(const char *filepath, aiff_info_t *info, void **out_data);
void aiff_free(void *data);
size_t aiff_to_float(const aiff_info_t *info, float *out, size_t max_frames);

#endif /* NXAUDIO_AIFF_H */
