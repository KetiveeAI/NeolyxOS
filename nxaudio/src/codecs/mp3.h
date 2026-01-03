/*
 * NXAudio MP3 Decoder
 * 
 * Minimal MP3 decoder for NeolyxOS
 * Based on public domain minimp3
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_MP3_H
#define NXAUDIO_MP3_H

#include <stdint.h>
#include <stddef.h>

/* ============ Constants ============ */
#define MP3_MAX_SAMPLES_PER_FRAME   1152
#define MP3_MAX_FRAME_SIZE          1441

/* ============ MP3 Frame Info ============ */
typedef struct {
    int     frame_bytes;
    int     channels;
    int     hz;
    int     layer;
    int     bitrate_kbps;
} mp3_frame_info_t;

/* ============ MP3 Decoder State ============ */
typedef struct {
    float   mdct_overlap[2][288];
    float   qmf_state[960];
    int     reserv;
    int     free_format_bytes;
    uint8_t header[4];
    uint8_t reserv_buf[511];
} mp3_decoder_t;

/* ============ Parsed MP3 Info ============ */
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t num_frames;
    size_t   total_samples;
    float    duration;
    
    /* Decoded data */
    float   *samples;
    size_t   samples_size;
} mp3_info_t;

/* ============ Public API ============ */

/**
 * Initialize MP3 decoder
 */
void mp3_init(mp3_decoder_t *dec);

/**
 * Decode single MP3 frame
 * 
 * @param dec       Decoder state
 * @param mp3       Input MP3 data
 * @param mp3_bytes Size of input data
 * @param pcm       Output PCM samples (must hold 1152*2 samples)
 * @param info      Output frame info
 * 
 * Returns: Number of bytes consumed
 */
int mp3_decode_frame(mp3_decoder_t *dec, 
                     const uint8_t *mp3, int mp3_bytes,
                     float *pcm, mp3_frame_info_t *info);

/**
 * Load and decode entire MP3 file
 * 
 * @param filepath  Path to MP3 file
 * @param info      Output MP3 info (caller frees samples)
 * 
 * Returns: 0 on success
 */
int mp3_load(const char *filepath, mp3_info_t *info);

/**
 * Decode MP3 from memory
 */
int mp3_decode(const void *data, size_t size, mp3_info_t *info);

/**
 * Free decoded MP3 data
 */
void mp3_free(mp3_info_t *info);

#endif /* NXAUDIO_MP3_H */
