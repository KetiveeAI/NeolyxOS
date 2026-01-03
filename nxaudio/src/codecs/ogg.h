/*
 * NXAudio OGG Vorbis Decoder
 * 
 * OGG container and Vorbis audio parser
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_OGG_H
#define NXAUDIO_OGG_H

#include <stdint.h>
#include <stddef.h>

/* ============ OGG Page Header ============ */
typedef struct {
    char     capture[4];        /* "OggS" */
    uint8_t  version;
    uint8_t  flags;
    int64_t  granule_pos;
    uint32_t serial;
    uint32_t page_seq;
    uint32_t checksum;
    uint8_t  segments;
} __attribute__((packed)) ogg_page_header_t;

/* ============ Vorbis Info ============ */
typedef struct {
    uint32_t sample_rate;
    uint8_t  channels;
    uint32_t bitrate_max;
    uint32_t bitrate_nominal;
    uint32_t bitrate_min;
    
    float    duration;
    size_t   total_samples;
    
    /* Decoded samples */
    float   *samples;
    size_t   samples_size;
} ogg_info_t;

/* ============ Public API ============ */

int ogg_load(const char *filepath, ogg_info_t *info);
int ogg_decode(const void *data, size_t size, ogg_info_t *info);
void ogg_free(ogg_info_t *info);
float ogg_duration(const ogg_info_t *info);

#endif /* NXAUDIO_OGG_H */
