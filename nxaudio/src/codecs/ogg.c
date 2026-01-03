/*
 * NXAudio OGG Vorbis Decoder
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "ogg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============ OGG Page Parsing ============ */

static int parse_ogg_page(const uint8_t *data, size_t size, 
                           ogg_page_header_t *header, 
                           const uint8_t **payload, size_t *payload_size) {
    if (size < 27) return -1;
    
    if (data[0] != 'O' || data[1] != 'g' || data[2] != 'g' || data[3] != 'S') {
        return -1;
    }
    
    memcpy(header, data, sizeof(*header));
    
    /* Calculate payload size from segment table */
    const uint8_t *segment_table = data + 27;
    *payload_size = 0;
    for (int i = 0; i < header->segments; i++) {
        *payload_size += segment_table[i];
    }
    
    *payload = data + 27 + header->segments;
    
    return 27 + header->segments + *payload_size;
}

/* ============ Vorbis Stream Parsing ============ */

static int parse_vorbis_identification(const uint8_t *data, size_t size, ogg_info_t *info) {
    if (size < 30) return -1;
    
    /* Check header type (1 = identification) */
    if (data[0] != 1) return -1;
    
    /* Check "vorbis" magic */
    if (memcmp(data + 1, "vorbis", 6) != 0) return -1;
    
    /* Parse fields */
    uint32_t version = data[7] | (data[8] << 8) | (data[9] << 16) | (data[10] << 24);
    if (version != 0) return -1;  /* Only version 0 supported */
    
    info->channels = data[11];
    info->sample_rate = data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24);
    info->bitrate_max = data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24);
    info->bitrate_nominal = data[20] | (data[21] << 8) | (data[22] << 16) | (data[23] << 24);
    info->bitrate_min = data[24] | (data[25] << 8) | (data[26] << 16) | (data[27] << 24);
    
    return 0;
}

/* ============ Public API ============ */

int ogg_load(const char *filepath, ogg_info_t *info) {
    if (!filepath || !info) return -1;
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[OGG] Cannot open: %s\n", filepath);
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
    
    int result = ogg_decode(data, size, info);
    free(data);
    
    return result;
}

int ogg_decode(const void *data, size_t size, ogg_info_t *info) {
    if (!data || !info || size < 27) return -1;
    
    memset(info, 0, sizeof(*info));
    
    const uint8_t *ptr = (const uint8_t*)data;
    const uint8_t *end = ptr + size;
    
    ogg_page_header_t page;
    const uint8_t *payload;
    size_t payload_size;
    
    int64_t last_granule = 0;
    
    /* Parse all pages */
    while (ptr < end) {
        int consumed = parse_ogg_page(ptr, end - ptr, &page, &payload, &payload_size);
        if (consumed <= 0) break;
        
        /* First page with BOS flag should have Vorbis identification */
        if ((page.flags & 0x02) && payload_size > 7) {
            parse_vorbis_identification(payload, payload_size, info);
        }
        
        /* Track last granule position for duration */
        if (page.granule_pos >= 0) {
            last_granule = page.granule_pos;
        }
        
        ptr += consumed;
    }
    
    /* Calculate duration */
    if (info->sample_rate > 0 && last_granule > 0) {
        info->total_samples = last_granule;
        info->duration = (float)last_granule / (float)info->sample_rate;
    }
    
    printf("[OGG] Parsed: %d Hz, %d ch, %d kbps, %.2f sec\n",
           info->sample_rate, info->channels,
           info->bitrate_nominal / 1000, info->duration);
    
    /* Allocate output (full decoding would go here) */
    size_t total = info->total_samples * info->channels;
    if (total > 0) {
        info->samples = (float*)calloc(total, sizeof(float));
        info->samples_size = total;
    }
    
    return 0;
}

void ogg_free(ogg_info_t *info) {
    if (info && info->samples) {
        free(info->samples);
        info->samples = NULL;
    }
}

float ogg_duration(const ogg_info_t *info) {
    return info ? info->duration : 0.0f;
}
