/*
 * NXAudio WAV Parser Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============ Helper Functions ============ */

static int mem_cmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = a, *pb = b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

/* ============ WAV Parsing ============ */

int wav_parse(const void *data, size_t size, wav_info_t *info) {
    if (!data || !info || size < 44) {
        return -1;
    }
    
    const uint8_t *ptr = (const uint8_t*)data;
    const uint8_t *end = ptr + size;
    
    /* Parse RIFF header */
    const wav_riff_header_t *riff = (const wav_riff_header_t*)ptr;
    
    if (mem_cmp(riff->chunk_id, "RIFF", 4) != 0) {
        fprintf(stderr, "[WAV] Invalid RIFF header\n");
        return -2;
    }
    
    if (mem_cmp(riff->format, "WAVE", 4) != 0) {
        fprintf(stderr, "[WAV] Invalid WAVE format\n");
        return -3;
    }
    
    ptr += sizeof(wav_riff_header_t);
    
    /* Find fmt and data chunks */
    const wav_fmt_chunk_t *fmt = NULL;
    const wav_data_chunk_t *data_chunk = NULL;
    const void *audio_data = NULL;
    
    while (ptr + 8 <= end) {
        const char *chunk_id = (const char*)ptr;
        uint32_t chunk_size = *(uint32_t*)(ptr + 4);
        
        if (mem_cmp(chunk_id, "fmt ", 4) == 0) {
            fmt = (const wav_fmt_chunk_t*)ptr;
        }
        else if (mem_cmp(chunk_id, "data", 4) == 0) {
            data_chunk = (const wav_data_chunk_t*)ptr;
            audio_data = ptr + 8;
        }
        
        /* Move to next chunk (8 byte header + chunk data, aligned to 2 bytes) */
        ptr += 8 + chunk_size;
        if (chunk_size & 1) ptr++;  /* Padding byte */
    }
    
    if (!fmt) {
        fprintf(stderr, "[WAV] Missing fmt chunk\n");
        return -4;
    }
    
    if (!data_chunk || !audio_data) {
        fprintf(stderr, "[WAV] Missing data chunk\n");
        return -5;
    }
    
    /* Fill info structure */
    info->format = fmt->audio_format;
    info->channels = fmt->num_channels;
    info->sample_rate = fmt->sample_rate;
    info->bits_per_sample = fmt->bits_per_sample;
    info->data_size = data_chunk->chunk_size;
    info->data = audio_data;
    
    /* Calculate number of frames */
    size_t bytes_per_sample = info->bits_per_sample / 8;
    size_t bytes_per_frame = bytes_per_sample * info->channels;
    info->num_frames = info->data_size / bytes_per_frame;
    
    printf("[WAV] Parsed: %d Hz, %d ch, %d bit, %zu frames (%.2f sec)\n",
           info->sample_rate, info->channels, info->bits_per_sample,
           info->num_frames, wav_duration(info));
    
    return 0;
}

int wav_load(const char *filepath, wav_info_t *info, void **out_data) {
    if (!filepath || !info || !out_data) {
        return -1;
    }
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[WAV] Cannot open file: %s\n", filepath);
        return -1;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 500 * 1024 * 1024) {  /* Max 500MB */
        fclose(f);
        return -2;
    }
    
    /* Allocate buffer */
    void *data = malloc(file_size);
    if (!data) {
        fclose(f);
        return -3;
    }
    
    /* Read file */
    if (fread(data, 1, file_size, f) != (size_t)file_size) {
        free(data);
        fclose(f);
        return -4;
    }
    
    fclose(f);
    
    /* Parse WAV */
    int result = wav_parse(data, file_size, info);
    if (result != 0) {
        free(data);
        return result;
    }
    
    *out_data = data;
    return 0;
}

void wav_free(void *data) {
    if (data) free(data);
}

size_t wav_to_float(const wav_info_t *info, float *out_samples, size_t max_frames) {
    if (!info || !out_samples || !info->data) {
        return 0;
    }
    
    size_t frames = (max_frames < info->num_frames) ? max_frames : info->num_frames;
    size_t total_samples = frames * info->channels;
    
    const uint8_t *src = (const uint8_t*)info->data;
    
    switch (info->bits_per_sample) {
        case 8:
            /* Unsigned 8-bit PCM */
            for (size_t i = 0; i < total_samples; i++) {
                out_samples[i] = ((float)src[i] - 128.0f) / 128.0f;
            }
            break;
            
        case 16: {
            /* Signed 16-bit PCM */
            const int16_t *src16 = (const int16_t*)src;
            for (size_t i = 0; i < total_samples; i++) {
                out_samples[i] = (float)src16[i] / 32768.0f;
            }
            break;
        }
            
        case 24: {
            /* Signed 24-bit PCM */
            for (size_t i = 0; i < total_samples; i++) {
                int32_t sample = (src[i*3] << 8) | (src[i*3+1] << 16) | (src[i*3+2] << 24);
                sample >>= 8;  /* Sign extend */
                out_samples[i] = (float)sample / 8388608.0f;
            }
            break;
        }
            
        case 32:
            if (info->format == WAV_FORMAT_IEEE_FLOAT) {
                /* Float 32-bit */
                memcpy(out_samples, src, total_samples * sizeof(float));
            } else {
                /* Signed 32-bit PCM */
                const int32_t *src32 = (const int32_t*)src;
                for (size_t i = 0; i < total_samples; i++) {
                    out_samples[i] = (float)src32[i] / 2147483648.0f;
                }
            }
            break;
            
        default:
            fprintf(stderr, "[WAV] Unsupported bit depth: %d\n", info->bits_per_sample);
            return 0;
    }
    
    return frames;
}

float wav_duration(const wav_info_t *info) {
    if (!info || info->sample_rate == 0) {
        return 0.0f;
    }
    return (float)info->num_frames / (float)info->sample_rate;
}
