/*
 * NXAudio Unified Audio Loader
 * 
 * Automatically detects and loads any supported audio format:
 * - WAV (PCM, float)
 * - MP3 (MPEG-1/2 Layer III)
 * - FLAC (lossless)
 * - OGG Vorbis
 * - AIFF (Apple)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_LOADER_H
#define NXAUDIO_LOADER_H

#include <stdint.h>
#include <stddef.h>

/* ============ Audio Format ============ */
typedef enum {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_WAV,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_FLAC,
    AUDIO_FORMAT_OGG,
    AUDIO_FORMAT_AIFF,
    AUDIO_FORMAT_OPUS,
    AUDIO_FORMAT_AAC,
    AUDIO_FORMAT_M4A,
} audio_format_t;

/* ============ Loaded Audio ============ */
typedef struct {
    audio_format_t format;
    uint32_t       sample_rate;
    uint16_t       channels;
    uint16_t       bits_per_sample;
    
    float         *samples;         /* Interleaved float samples */
    size_t         num_frames;
    float          duration;
    
    /* Metadata */
    char           title[256];
    char           artist[256];
    char           album[256];
} audio_data_t;

/* ============ Public API ============ */

/**
 * Detect audio format from file extension
 */
audio_format_t audio_detect_format_ext(const char *filepath);

/**
 * Detect audio format from file magic bytes
 */
audio_format_t audio_detect_format_magic(const void *data, size_t size);

/**
 * Load audio file (auto-detect format)
 */
int audio_load(const char *filepath, audio_data_t *audio);

/**
 * Decode audio from memory
 */
int audio_decode(const void *data, size_t size, audio_data_t *audio);

/**
 * Free loaded audio
 */
void audio_free(audio_data_t *audio);

/**
 * Get format name string
 */
const char* audio_format_name(audio_format_t format);

/**
 * Check if format is supported
 */
int audio_format_supported(audio_format_t format);

#endif /* NXAUDIO_LOADER_H */
