/*
 * NXAudio AIFF Parser Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "aiff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============ Big-Endian Helpers ============ */

static uint32_t read_be32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint16_t read_be16(const uint8_t *p) {
    return (p[0] << 8) | p[1];
}

/* Convert 80-bit extended precision float to double */
static double read_extended(const uint8_t *p) {
    int sign = (p[0] >> 7) & 1;
    int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mantissa = 0;
    
    for (int i = 0; i < 8; i++) {
        mantissa = (mantissa << 8) | p[2 + i];
    }
    
    if (exp == 0 && mantissa == 0) return 0.0;
    if (exp == 0x7FFF) return sign ? -INFINITY : INFINITY;
    
    exp -= 16383;  /* Remove bias */
    
    double value = (double)mantissa / (1ULL << 63);
    value = ldexp(value, exp);
    
    return sign ? -value : value;
}

/* ============ AIFF Parsing ============ */

int aiff_parse(const void *data, size_t size, aiff_info_t *info) {
    if (!data || !info || size < 12) return -1;
    
    memset(info, 0, sizeof(*info));
    
    const uint8_t *ptr = (const uint8_t*)data;
    
    /* Check FORM header */
    if (read_be32(ptr) != AIFF_FORM) {
        fprintf(stderr, "[AIFF] Invalid FORM header\n");
        return -1;
    }
    
    uint32_t form_size = read_be32(ptr + 4);
    (void)form_size;
    
    /* Check AIFF or AIFC type */
    uint32_t aiff_type = read_be32(ptr + 8);
    if (aiff_type != AIFF_AIFF && aiff_type != AIFF_AIFC) {
        fprintf(stderr, "[AIFF] Not an AIFF file\n");
        return -2;
    }
    
    ptr += 12;
    const uint8_t *end = (const uint8_t*)data + size;
    
    /* Parse chunks */
    while (ptr + 8 < end) {
        uint32_t chunk_id = read_be32(ptr);
        uint32_t chunk_size = read_be32(ptr + 4);
        const uint8_t *chunk_data = ptr + 8;
        
        if (chunk_id == AIFF_COMM) {
            /* Common chunk */
            if (chunk_size >= 18) {
                info->channels = read_be16(chunk_data);
                info->num_frames = read_be32(chunk_data + 2);
                info->bits_per_sample = read_be16(chunk_data + 6);
                info->sample_rate = (uint32_t)read_extended(chunk_data + 8);
            }
        }
        else if (chunk_id == AIFF_SSND) {
            /* Sound data chunk */
            uint32_t offset = read_be32(chunk_data);
            /* uint32_t block_size = read_be32(chunk_data + 4); */
            
            info->data = chunk_data + 8 + offset;
            info->data_size = chunk_size - 8 - offset;
        }
        
        /* Move to next chunk (pad to even boundary) */
        ptr += 8 + chunk_size;
        if (chunk_size & 1) ptr++;
    }
    
    /* Calculate duration */
    if (info->sample_rate > 0) {
        info->duration = (float)info->num_frames / (float)info->sample_rate;
    }
    
    printf("[AIFF] Parsed: %d Hz, %d ch, %d bit, %.2f sec\n",
           info->sample_rate, info->channels, info->bits_per_sample, info->duration);
    
    return 0;
}

int aiff_load(const char *filepath, aiff_info_t *info, void **out_data) {
    if (!filepath || !info || !out_data) return -1;
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[AIFF] Cannot open: %s\n", filepath);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    void *data = malloc(size);
    if (!data) {
        fclose(f);
        return -2;
    }
    
    fread(data, 1, size, f);
    fclose(f);
    
    int result = aiff_parse(data, size, info);
    if (result != 0) {
        free(data);
        return result;
    }
    
    *out_data = data;
    return 0;
}

void aiff_free(void *data) {
    if (data) free(data);
}

size_t aiff_to_float(const aiff_info_t *info, float *out, size_t max_frames) {
    if (!info || !out || !info->data) return 0;
    
    size_t frames = (max_frames < info->num_frames) ? max_frames : info->num_frames;
    size_t samples = frames * info->channels;
    
    const uint8_t *src = (const uint8_t*)info->data;
    
    switch (info->bits_per_sample) {
        case 8:
            /* Signed 8-bit */
            for (size_t i = 0; i < samples; i++) {
                out[i] = (float)(int8_t)src[i] / 128.0f;
            }
            break;
            
        case 16:
            /* Big-endian 16-bit */
            for (size_t i = 0; i < samples; i++) {
                int16_t val = (src[i*2] << 8) | src[i*2 + 1];
                out[i] = (float)val / 32768.0f;
            }
            break;
            
        case 24:
            /* Big-endian 24-bit */
            for (size_t i = 0; i < samples; i++) {
                int32_t val = (src[i*3] << 24) | (src[i*3+1] << 16) | (src[i*3+2] << 8);
                val >>= 8;
                out[i] = (float)val / 8388608.0f;
            }
            break;
            
        case 32:
            /* Big-endian 32-bit */
            for (size_t i = 0; i < samples; i++) {
                int32_t val = (src[i*4] << 24) | (src[i*4+1] << 16) | 
                              (src[i*4+2] << 8) | src[i*4+3];
                out[i] = (float)val / 2147483648.0f;
            }
            break;
            
        default:
            return 0;
    }
    
    return frames;
}
