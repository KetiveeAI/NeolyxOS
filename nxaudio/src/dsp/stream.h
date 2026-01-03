/*
 * NXAudio Streaming Decoder
 * 
 * Chunk-based audio decoding for low memory usage:
 * - Stream from file without loading entirely
 * - Decode on demand
 * - Perfect for music playback
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXAUDIO_STREAM_H
#define NXAUDIO_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ============ Error Codes ============ */
typedef enum {
    NX_OK = 0,
    NX_ERR_FORMAT = -1,
    NX_ERR_DECODE = -2,
    NX_ERR_IO = -3,
    NX_ERR_MEMORY = -4,
    NX_ERR_EOF = -5,
    NX_ERR_INVALID = -6,
} nx_error_t;

/* ============ Stream State ============ */
typedef enum {
    NX_STREAM_STOPPED,
    NX_STREAM_PLAYING,
    NX_STREAM_PAUSED,
    NX_STREAM_FINISHED,
} nx_stream_state_t;

/* ============ Codec Type ============ */
typedef enum {
    NX_CODEC_UNKNOWN,
    NX_CODEC_WAV,
    NX_CODEC_MP3,
    NX_CODEC_FLAC,
    NX_CODEC_OGG,
    NX_CODEC_AIFF,
    NX_CODEC_OPUS,
} nx_codec_t;

/* ============ Forward Declaration ============ */
typedef struct nx_audio_stream nx_audio_stream_t;

/* ============ Codec Callbacks ============ */
typedef struct {
    nx_error_t (*open)(nx_audio_stream_t *stream);
    nx_error_t (*read)(nx_audio_stream_t *stream, float *out, size_t *frames);
    nx_error_t (*seek)(nx_audio_stream_t *stream, size_t frame);
    void       (*close)(nx_audio_stream_t *stream);
} nx_codec_ops_t;

/* ============ Audio Stream ============ */
struct nx_audio_stream {
    /* File handle */
    FILE           *file;
    char            filepath[512];
    size_t          file_size;
    
    /* Format info */
    nx_codec_t      codec;
    uint32_t        sample_rate;
    uint16_t        channels;
    uint16_t        bits_per_sample;
    size_t          total_frames;
    float           duration;
    
    /* Playback state */
    nx_stream_state_t state;
    size_t          current_frame;
    
    /* Decoder state */
    void           *decoder_state;
    size_t          decoder_state_size;
    const nx_codec_ops_t *codec_ops;
    
    /* Buffer for raw file data */
    uint8_t        *read_buffer;
    size_t          read_buffer_size;
    size_t          read_buffer_pos;
    size_t          read_buffer_filled;
    
    /* Metadata */
    char            title[256];
    char            artist[256];
    char            album[256];
    uint32_t        year;
};

/* ============ Public API ============ */

/**
 * Open audio stream from file
 */
nx_error_t nx_stream_open(nx_audio_stream_t *stream, const char *filepath);

/**
 * Read decoded audio frames
 * @param frames: In/Out - requested frames / actual frames read
 */
nx_error_t nx_stream_read(nx_audio_stream_t *stream, float *buffer, size_t *frames);

/**
 * Seek to frame position
 */
nx_error_t nx_stream_seek(nx_audio_stream_t *stream, size_t frame);

/**
 * Seek by time (seconds)
 */
nx_error_t nx_stream_seek_time(nx_audio_stream_t *stream, float time_sec);

/**
 * Get current position in frames
 */
size_t nx_stream_tell(nx_audio_stream_t *stream);

/**
 * Get current position in seconds
 */
float nx_stream_tell_time(nx_audio_stream_t *stream);

/**
 * Close stream and free resources
 */
void nx_stream_close(nx_audio_stream_t *stream);

/**
 * Check if stream is at end
 */
int nx_stream_eof(nx_audio_stream_t *stream);

/**
 * Get error string
 */
const char* nx_error_string(nx_error_t err);

#endif /* NXAUDIO_STREAM_H */
