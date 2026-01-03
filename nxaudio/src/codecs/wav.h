/*
 * NXAudio WAV Parser
 * 
 * Complete WAV file parser supporting:
 * - PCM 8/16/24/32-bit
 * - Float 32-bit
 * - Multiple channels
 * - Various sample rates
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_WAV_H
#define NXAUDIO_WAV_H

#include <stdint.h>
#include <stddef.h>

/* ============ WAV Format Codes ============ */
#define WAV_FORMAT_PCM          0x0001
#define WAV_FORMAT_IEEE_FLOAT   0x0003
#define WAV_FORMAT_ALAW         0x0006
#define WAV_FORMAT_MULAW        0x0007
#define WAV_FORMAT_EXTENSIBLE   0xFFFE

/* ============ WAV Header Structures ============ */

typedef struct {
    char     chunk_id[4];       /* "RIFF" */
    uint32_t chunk_size;
    char     format[4];         /* "WAVE" */
} __attribute__((packed)) wav_riff_header_t;

typedef struct {
    char     chunk_id[4];       /* "fmt " */
    uint32_t chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_fmt_chunk_t;

typedef struct {
    char     chunk_id[4];       /* "data" */
    uint32_t chunk_size;
} __attribute__((packed)) wav_data_chunk_t;

/* ============ Parsed WAV Info ============ */

typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_size;
    size_t   num_frames;
    
    /* Data pointer (within original buffer) */
    const void *data;
} wav_info_t;

/* ============ Public API ============ */

/**
 * Parse WAV file from memory
 * 
 * @param data      Pointer to WAV file data
 * @param size      Size of data
 * @param info      Output WAV info
 * 
 * Returns: 0 on success, negative on error
 */
int wav_parse(const void *data, size_t size, wav_info_t *info);

/**
 * Parse WAV file from path
 */
int wav_load(const char *filepath, wav_info_t *info, void **out_data);

/**
 * Free loaded WAV data
 */
void wav_free(void *data);

/**
 * Convert WAV to float samples
 * 
 * @param info          Parsed WAV info
 * @param out_samples   Output buffer (caller allocates)
 * @param max_frames    Maximum frames to convert
 * 
 * Returns: Number of frames converted
 */
size_t wav_to_float(const wav_info_t *info, float *out_samples, size_t max_frames);

/**
 * Get WAV duration in seconds
 */
float wav_duration(const wav_info_t *info);

#endif /* NXAUDIO_WAV_H */
