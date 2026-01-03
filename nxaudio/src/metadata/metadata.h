/*
 * NXAudio Metadata Extractors
 * 
 * Extract metadata from audio files:
 * - ID3v1/v2 (MP3)
 * - Vorbis Comments (OGG, FLAC)
 * - AIFF chunks
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_METADATA_H
#define NXAUDIO_METADATA_H

#include <stdint.h>
#include <stddef.h>

/* ============ Metadata Structure ============ */
typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char genre[64];
    char date[32];
    char comment[512];
    uint32_t track_number;
    uint32_t total_tracks;
    uint32_t disc_number;
    uint32_t total_discs;
    uint32_t year;
    
    /* Album art */
    const uint8_t *cover_art;
    size_t cover_art_size;
    char cover_art_mime[32];
} nx_metadata_t;

/* ============ Public API ============ */

/**
 * Extract metadata from file
 */
int nx_metadata_load(const char *filepath, nx_metadata_t *meta);

/**
 * Extract metadata from memory
 */
int nx_metadata_parse(const void *data, size_t size, nx_metadata_t *meta);

/**
 * Parse ID3v2 tag
 */
int nx_metadata_parse_id3v2(const void *data, size_t size, nx_metadata_t *meta);

/**
 * Parse ID3v1 tag
 */
int nx_metadata_parse_id3v1(const void *data, size_t size, nx_metadata_t *meta);

/**
 * Parse Vorbis comments
 */
int nx_metadata_parse_vorbis(const void *data, size_t size, nx_metadata_t *meta);

/**
 * Clear metadata structure
 */
void nx_metadata_clear(nx_metadata_t *meta);

#endif /* NXAUDIO_METADATA_H */
