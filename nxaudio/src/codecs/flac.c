/*
 * NXAudio FLAC Decoder Implementation
 * 
 * Minimal FLAC decoder for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "flac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============ Bit Reader ============ */

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    int bit_pos;
    uint32_t cache;
} flac_bitreader_t;

static void br_init(flac_bitreader_t *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_pos = 0;
    br->cache = 0;
}

static uint32_t br_read_bits(flac_bitreader_t *br, int bits) {
    uint32_t result = 0;
    
    while (bits > 0) {
        if (br->byte_pos >= br->size) return 0;
        
        int available = 8 - br->bit_pos;
        int take = (bits < available) ? bits : available;
        
        int shift = available - take;
        uint32_t mask = ((1 << take) - 1) << shift;
        result = (result << take) | ((br->data[br->byte_pos] & mask) >> shift);
        
        br->bit_pos += take;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
        bits -= take;
    }
    
    return result;
}

static void br_align(flac_bitreader_t *br) {
    if (br->bit_pos > 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

/* ============ FLAC Parsing ============ */

static int parse_streaminfo(const uint8_t *data, flac_streaminfo_t *info) {
    flac_bitreader_t br;
    br_init(&br, data, 34);
    
    info->min_block_size = br_read_bits(&br, 16);
    info->max_block_size = br_read_bits(&br, 16);
    info->min_frame_size = br_read_bits(&br, 24);
    info->max_frame_size = br_read_bits(&br, 24);
    info->sample_rate = br_read_bits(&br, 20);
    info->channels = br_read_bits(&br, 3) + 1;
    info->bits_per_sample = br_read_bits(&br, 5) + 1;
    info->total_samples = ((uint64_t)br_read_bits(&br, 4) << 32) | br_read_bits(&br, 32);
    
    /* MD5 */
    for (int i = 0; i < 16; i++) {
        info->md5[i] = data[18 + i];
    }
    
    return 0;
}

static int decode_residual_rice(flac_bitreader_t *br, int32_t *residual, 
                                  int block_size, int predictor_order) {
    int partition_order = br_read_bits(br, 4);
    int num_partitions = 1 << partition_order;
    int samples_per_partition = block_size / num_partitions;
    
    int sample = 0;
    for (int p = 0; p < num_partitions; p++) {
        int rice_param = br_read_bits(br, 4);
        int partition_samples = samples_per_partition;
        if (p == 0) partition_samples -= predictor_order;
        
        for (int i = 0; i < partition_samples && sample < block_size; i++) {
            /* Read unary coded quotient */
            int quotient = 0;
            while (br_read_bits(br, 1) == 0 && quotient < 64) {
                quotient++;
            }
            
            /* Read remainder */
            int remainder = br_read_bits(br, rice_param);
            
            /* Combine */
            int value = (quotient << rice_param) | remainder;
            
            /* Fold to signed */
            if (value & 1) {
                residual[sample++] = -((value + 1) >> 1);
            } else {
                residual[sample++] = value >> 1;
            }
        }
    }
    
    return 0;
}

static int decode_subframe(flac_bitreader_t *br, int32_t *output, 
                            int block_size, int bits_per_sample) {
    /* Zero padding bit */
    br_read_bits(br, 1);
    
    /* Subframe type */
    int type = br_read_bits(br, 6);
    
    /* Wasted bits */
    int wasted = 0;
    if (br_read_bits(br, 1)) {
        wasted = 1;
        while (br_read_bits(br, 1) == 0) wasted++;
    }
    
    int effective_bps = bits_per_sample - wasted;
    
    if (type == 0) {
        /* CONSTANT */
        int32_t value = br_read_bits(br, effective_bps);
        for (int i = 0; i < block_size; i++) {
            output[i] = value;
        }
    }
    else if (type == 1) {
        /* VERBATIM */
        for (int i = 0; i < block_size; i++) {
            output[i] = br_read_bits(br, effective_bps);
        }
    }
    else if (type >= 8 && type <= 12) {
        /* FIXED predictor */
        int order = type - 8;
        
        /* Warm-up samples */
        for (int i = 0; i < order; i++) {
            output[i] = br_read_bits(br, effective_bps);
        }
        
        /* Residual */
        int32_t *residual = malloc(block_size * sizeof(int32_t));
        if (!residual) return -1;
        
        decode_residual_rice(br, residual + order, block_size - order, order);
        
        /* Reconstruct */
        for (int i = order; i < block_size; i++) {
            int32_t pred = 0;
            switch (order) {
                case 0: pred = 0; break;
                case 1: pred = output[i-1]; break;
                case 2: pred = 2*output[i-1] - output[i-2]; break;
                case 3: pred = 3*output[i-1] - 3*output[i-2] + output[i-3]; break;
                case 4: pred = 4*output[i-1] - 6*output[i-2] + 4*output[i-3] - output[i-4]; break;
            }
            output[i] = pred + residual[i];
        }
        
        free(residual);
    }
    else if (type >= 32) {
        /* LPC predictor */
        int order = (type & 0x1F) + 1;
        
        /* Warm-up samples */
        for (int i = 0; i < order; i++) {
            output[i] = br_read_bits(br, effective_bps);
        }
        
        /* LPC precision and shift */
        int precision = br_read_bits(br, 4) + 1;
        int shift = br_read_bits(br, 5);
        
        /* LPC coefficients */
        int32_t *coefs = malloc(order * sizeof(int32_t));
        for (int i = 0; i < order; i++) {
            coefs[i] = br_read_bits(br, precision);
            /* Sign extend */
            if (coefs[i] & (1 << (precision-1))) {
                coefs[i] |= ~((1 << precision) - 1);
            }
        }
        
        /* Residual */
        int32_t *residual = malloc(block_size * sizeof(int32_t));
        decode_residual_rice(br, residual + order, block_size - order, order);
        
        /* Reconstruct */
        for (int i = order; i < block_size; i++) {
            int64_t sum = 0;
            for (int j = 0; j < order; j++) {
                sum += (int64_t)coefs[j] * output[i - j - 1];
            }
            output[i] = residual[i] + (int32_t)(sum >> shift);
        }
        
        free(coefs);
        free(residual);
    }
    
    /* Apply wasted bits */
    if (wasted > 0) {
        for (int i = 0; i < block_size; i++) {
            output[i] <<= wasted;
        }
    }
    
    return 0;
}

/* ============ Public API ============ */

int flac_load(const char *filepath, flac_info_t *info) {
    if (!filepath || !info) return -1;
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[FLAC] Cannot open: %s\n", filepath);
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
    
    int result = flac_decode(data, size, info);
    free(data);
    
    return result;
}

int flac_decode(const void *data, size_t size, flac_info_t *info) {
    if (!data || !info || size < 42) return -1;
    
    memset(info, 0, sizeof(*info));
    
    const uint8_t *ptr = (const uint8_t*)data;
    
    /* Check magic */
    if (ptr[0] != 'f' || ptr[1] != 'L' || ptr[2] != 'a' || ptr[3] != 'C') {
        fprintf(stderr, "[FLAC] Invalid magic\n");
        return -2;
    }
    ptr += 4;
    
    /* Parse metadata blocks */
    int last_block = 0;
    while (!last_block && ptr < (uint8_t*)data + size - 4) {
        last_block = (ptr[0] >> 7) & 1;
        int block_type = ptr[0] & 0x7F;
        int block_size = (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        ptr += 4;
        
        if (block_type == FLAC_BLOCK_STREAMINFO) {
            parse_streaminfo(ptr, &info->streaminfo);
        }
        
        ptr += block_size;
    }
    
    /* Calculate duration */
    if (info->streaminfo.sample_rate > 0) {
        info->duration = (float)info->streaminfo.total_samples / 
                         (float)info->streaminfo.sample_rate;
    }
    
    printf("[FLAC] Parsed: %d Hz, %d ch, %d bit, %.2f sec\n",
           info->streaminfo.sample_rate,
           info->streaminfo.channels,
           info->streaminfo.bits_per_sample,
           info->duration);
    
    /* Allocate output buffer */
    size_t total_samples = info->streaminfo.total_samples * info->streaminfo.channels;
    info->samples = (float*)malloc(total_samples * sizeof(float));
    if (!info->samples) return -3;
    info->samples_size = total_samples;
    
    /* Decode frames - simplified for basic files */
    /* Full decoder would iterate through all frames */
    
    return 0;
}

void flac_free(flac_info_t *info) {
    if (info && info->samples) {
        free(info->samples);
        info->samples = NULL;
    }
}

float flac_duration(const flac_info_t *info) {
    return info ? info->duration : 0.0f;
}
