/*
 * NXAudio FLAC Decoder
 * 
 * Free Lossless Audio Codec parser
 * Supports: 8-24 bit, mono/stereo, all sample rates
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_FLAC_H
#define NXAUDIO_FLAC_H

#include <stdint.h>
#include <stddef.h>

/* ============ FLAC Block Types ============ */
#define FLAC_BLOCK_STREAMINFO   0
#define FLAC_BLOCK_PADDING      1
#define FLAC_BLOCK_APPLICATION  2
#define FLAC_BLOCK_SEEKTABLE    3
#define FLAC_BLOCK_VORBIS_COMMENT 4
#define FLAC_BLOCK_CUESHEET     5
#define FLAC_BLOCK_PICTURE      6

/* ============ FLAC Stream Info ============ */
typedef struct {
    uint16_t min_block_size;
    uint16_t max_block_size;
    uint32_t min_frame_size;
    uint32_t max_frame_size;
    uint32_t sample_rate;
    uint8_t  channels;
    uint8_t  bits_per_sample;
    uint64_t total_samples;
    uint8_t  md5[16];
} flac_streaminfo_t;

/* ============ FLAC Info ============ */
typedef struct {
    flac_streaminfo_t streaminfo;
    float    duration;
    
    /* Decoded samples */
    float   *samples;
    size_t   samples_size;
} flac_info_t;

/* ============ Public API ============ */

/**
 * Load and decode FLAC file
 */
int flac_load(const char *filepath, flac_info_t *info);

/**
 * Decode FLAC from memory
 */
int flac_decode(const void *data, size_t size, flac_info_t *info);

/**
 * Free decoded data
 */
void flac_free(flac_info_t *info);

/**
 * Get duration
 */
float flac_duration(const flac_info_t *info);

#endif /* NXAUDIO_FLAC_H */
