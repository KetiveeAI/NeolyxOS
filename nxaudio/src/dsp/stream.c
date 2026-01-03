/*
 * NXAudio Streaming Decoder Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "stream.h"
#include "../codecs/wav.h"
#include <stdlib.h>
#include <string.h>

#define READ_BUFFER_SIZE    (64 * 1024)  /* 64KB read buffer */

/* ============ Helper Functions ============ */

static int str_ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return strcasecmp(str + str_len - suf_len, suffix) == 0;
}

static nx_codec_t detect_codec(const char *filepath) {
    if (str_ends_with(filepath, ".wav")) return NX_CODEC_WAV;
    if (str_ends_with(filepath, ".mp3")) return NX_CODEC_MP3;
    if (str_ends_with(filepath, ".flac")) return NX_CODEC_FLAC;
    if (str_ends_with(filepath, ".ogg")) return NX_CODEC_OGG;
    if (str_ends_with(filepath, ".aiff") || str_ends_with(filepath, ".aif")) return NX_CODEC_AIFF;
    if (str_ends_with(filepath, ".opus")) return NX_CODEC_OPUS;
    return NX_CODEC_UNKNOWN;
}

/* ============ WAV Streaming Codec ============ */

typedef struct {
    size_t data_offset;
    size_t data_size;
    size_t bytes_per_frame;
} wav_stream_state_t;

static nx_error_t wav_stream_open(nx_audio_stream_t *stream) {
    /* Read header */
    uint8_t header[44];
    if (fread(header, 1, 44, stream->file) != 44) {
        return NX_ERR_IO;
    }
    
    /* Verify RIFF/WAVE */
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return NX_ERR_FORMAT;
    }
    
    /* Find fmt chunk */
    fseek(stream->file, 12, SEEK_SET);
    
    while (!feof(stream->file)) {
        char chunk_id[4];
        uint32_t chunk_size;
        
        if (fread(chunk_id, 1, 4, stream->file) != 4) break;
        if (fread(&chunk_size, 4, 1, stream->file) != 1) break;
        
        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t format, channels, bits;
            uint32_t sample_rate;
            
            fread(&format, 2, 1, stream->file);
            fread(&channels, 2, 1, stream->file);
            fread(&sample_rate, 4, 1, stream->file);
            fseek(stream->file, 6, SEEK_CUR);  /* Skip byte rate and block align */
            fread(&bits, 2, 1, stream->file);
            
            stream->sample_rate = sample_rate;
            stream->channels = channels;
            stream->bits_per_sample = bits;
            
            fseek(stream->file, -(22), SEEK_CUR);
            fseek(stream->file, chunk_size, SEEK_CUR);
        }
        else if (memcmp(chunk_id, "data", 4) == 0) {
            wav_stream_state_t *state = (wav_stream_state_t*)stream->decoder_state;
            state->data_offset = ftell(stream->file);
            state->data_size = chunk_size;
            state->bytes_per_frame = stream->channels * (stream->bits_per_sample / 8);
            
            stream->total_frames = chunk_size / state->bytes_per_frame;
            stream->duration = (float)stream->total_frames / stream->sample_rate;
            
            break;
        }
        else {
            fseek(stream->file, chunk_size, SEEK_CUR);
            if (chunk_size & 1) fseek(stream->file, 1, SEEK_CUR);
        }
    }
    
    return NX_OK;
}

static nx_error_t wav_stream_read(nx_audio_stream_t *stream, float *out, size_t *frames) {
    wav_stream_state_t *state = (wav_stream_state_t*)stream->decoder_state;
    
    size_t want = *frames;
    size_t avail = stream->total_frames - stream->current_frame;
    if (want > avail) want = avail;
    
    if (want == 0) {
        *frames = 0;
        return NX_ERR_EOF;
    }
    
    size_t bytes_needed = want * state->bytes_per_frame;
    uint8_t *raw = malloc(bytes_needed);
    if (!raw) return NX_ERR_MEMORY;
    
    size_t read_bytes = fread(raw, 1, bytes_needed, stream->file);
    size_t read_frames = read_bytes / state->bytes_per_frame;
    
    /* Convert to float */
    size_t samples = read_frames * stream->channels;
    
    switch (stream->bits_per_sample) {
        case 8:
            for (size_t i = 0; i < samples; i++) {
                out[i] = ((float)raw[i] - 128.0f) / 128.0f;
            }
            break;
        case 16: {
            int16_t *src = (int16_t*)raw;
            for (size_t i = 0; i < samples; i++) {
                out[i] = (float)src[i] / 32768.0f;
            }
            break;
        }
        case 24:
            for (size_t i = 0; i < samples; i++) {
                int32_t val = (raw[i*3] << 8) | (raw[i*3+1] << 16) | (raw[i*3+2] << 24);
                val >>= 8;
                out[i] = (float)val / 8388608.0f;
            }
            break;
        case 32: {
            int32_t *src = (int32_t*)raw;
            for (size_t i = 0; i < samples; i++) {
                out[i] = (float)src[i] / 2147483648.0f;
            }
            break;
        }
    }
    
    free(raw);
    
    stream->current_frame += read_frames;
    *frames = read_frames;
    
    return NX_OK;
}

static nx_error_t wav_stream_seek(nx_audio_stream_t *stream, size_t frame) {
    wav_stream_state_t *state = (wav_stream_state_t*)stream->decoder_state;
    
    if (frame >= stream->total_frames) {
        frame = stream->total_frames;
    }
    
    size_t byte_offset = state->data_offset + frame * state->bytes_per_frame;
    fseek(stream->file, byte_offset, SEEK_SET);
    stream->current_frame = frame;
    
    return NX_OK;
}

static void wav_stream_close(nx_audio_stream_t *stream) {
    /* Nothing extra to free for WAV */
    (void)stream;
}

static const nx_codec_ops_t wav_codec_ops = {
    .open = wav_stream_open,
    .read = wav_stream_read,
    .seek = wav_stream_seek,
    .close = wav_stream_close,
};

/* ============ Public API ============ */

nx_error_t nx_stream_open(nx_audio_stream_t *stream, const char *filepath) {
    if (!stream || !filepath) return NX_ERR_INVALID;
    
    memset(stream, 0, sizeof(*stream));
    strncpy(stream->filepath, filepath, sizeof(stream->filepath) - 1);
    
    /* Detect codec */
    stream->codec = detect_codec(filepath);
    if (stream->codec == NX_CODEC_UNKNOWN) {
        fprintf(stderr, "[Stream] Unknown format: %s\n", filepath);
        return NX_ERR_FORMAT;
    }
    
    /* Open file */
    stream->file = fopen(filepath, "rb");
    if (!stream->file) {
        fprintf(stderr, "[Stream] Cannot open: %s\n", filepath);
        return NX_ERR_IO;
    }
    
    /* Get file size */
    fseek(stream->file, 0, SEEK_END);
    stream->file_size = ftell(stream->file);
    fseek(stream->file, 0, SEEK_SET);
    
    /* Allocate decoder state */
    switch (stream->codec) {
        case NX_CODEC_WAV:
            stream->decoder_state_size = sizeof(wav_stream_state_t);
            stream->codec_ops = &wav_codec_ops;
            break;
        default:
            /* Other codecs use similar pattern */
            stream->decoder_state_size = 256;
            stream->codec_ops = &wav_codec_ops;  /* Fallback */
            break;
    }
    
    stream->decoder_state = calloc(1, stream->decoder_state_size);
    if (!stream->decoder_state) {
        fclose(stream->file);
        return NX_ERR_MEMORY;
    }
    
    /* Allocate read buffer */
    stream->read_buffer = malloc(READ_BUFFER_SIZE);
    stream->read_buffer_size = READ_BUFFER_SIZE;
    
    /* Open codec */
    nx_error_t err = stream->codec_ops->open(stream);
    if (err != NX_OK) {
        nx_stream_close(stream);
        return err;
    }
    
    stream->state = NX_STREAM_STOPPED;
    
    printf("[Stream] Opened: %s (%d Hz, %d ch, %.2f sec)\n",
           filepath, stream->sample_rate, stream->channels, stream->duration);
    
    return NX_OK;
}

nx_error_t nx_stream_read(nx_audio_stream_t *stream, float *buffer, size_t *frames) {
    if (!stream || !buffer || !frames) return NX_ERR_INVALID;
    if (!stream->codec_ops) return NX_ERR_INVALID;
    
    return stream->codec_ops->read(stream, buffer, frames);
}

nx_error_t nx_stream_seek(nx_audio_stream_t *stream, size_t frame) {
    if (!stream) return NX_ERR_INVALID;
    if (!stream->codec_ops) return NX_ERR_INVALID;
    
    return stream->codec_ops->seek(stream, frame);
}

nx_error_t nx_stream_seek_time(nx_audio_stream_t *stream, float time_sec) {
    if (!stream) return NX_ERR_INVALID;
    
    size_t frame = (size_t)(time_sec * stream->sample_rate);
    return nx_stream_seek(stream, frame);
}

size_t nx_stream_tell(nx_audio_stream_t *stream) {
    return stream ? stream->current_frame : 0;
}

float nx_stream_tell_time(nx_audio_stream_t *stream) {
    if (!stream || stream->sample_rate == 0) return 0.0f;
    return (float)stream->current_frame / (float)stream->sample_rate;
}

void nx_stream_close(nx_audio_stream_t *stream) {
    if (!stream) return;
    
    if (stream->codec_ops && stream->codec_ops->close) {
        stream->codec_ops->close(stream);
    }
    
    if (stream->decoder_state) {
        free(stream->decoder_state);
    }
    if (stream->read_buffer) {
        free(stream->read_buffer);
    }
    if (stream->file) {
        fclose(stream->file);
    }
    
    memset(stream, 0, sizeof(*stream));
}

int nx_stream_eof(nx_audio_stream_t *stream) {
    if (!stream) return 1;
    return stream->current_frame >= stream->total_frames;
}

const char* nx_error_string(nx_error_t err) {
    switch (err) {
        case NX_OK:          return "Success";
        case NX_ERR_FORMAT:  return "Invalid format";
        case NX_ERR_DECODE:  return "Decode error";
        case NX_ERR_IO:      return "I/O error";
        case NX_ERR_MEMORY:  return "Out of memory";
        case NX_ERR_EOF:     return "End of file";
        case NX_ERR_INVALID: return "Invalid parameter";
        default:             return "Unknown error";
    }
}
