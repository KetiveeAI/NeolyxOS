/*
 * NXAudio Unified Audio Loader Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "loader.h"
#include "wav.h"
#include "mp3.h"
#include "flac.h"
#include "ogg.h"
#include "aiff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============ Format Detection ============ */

static int str_ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return strcasecmp(str + str_len - suf_len, suffix) == 0;
}

audio_format_t audio_detect_format_ext(const char *filepath) {
    if (!filepath) return AUDIO_FORMAT_UNKNOWN;
    
    if (str_ends_with(filepath, ".wav")) return AUDIO_FORMAT_WAV;
    if (str_ends_with(filepath, ".mp3")) return AUDIO_FORMAT_MP3;
    if (str_ends_with(filepath, ".flac")) return AUDIO_FORMAT_FLAC;
    if (str_ends_with(filepath, ".ogg")) return AUDIO_FORMAT_OGG;
    if (str_ends_with(filepath, ".oga")) return AUDIO_FORMAT_OGG;
    if (str_ends_with(filepath, ".aiff")) return AUDIO_FORMAT_AIFF;
    if (str_ends_with(filepath, ".aif")) return AUDIO_FORMAT_AIFF;
    if (str_ends_with(filepath, ".opus")) return AUDIO_FORMAT_OPUS;
    if (str_ends_with(filepath, ".aac")) return AUDIO_FORMAT_AAC;
    if (str_ends_with(filepath, ".m4a")) return AUDIO_FORMAT_M4A;
    
    return AUDIO_FORMAT_UNKNOWN;
}

audio_format_t audio_detect_format_magic(const void *data, size_t size) {
    if (!data || size < 12) return AUDIO_FORMAT_UNKNOWN;
    
    const uint8_t *p = (const uint8_t*)data;
    
    /* RIFF/WAVE */
    if (p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F' &&
        p[8] == 'W' && p[9] == 'A' && p[10] == 'V' && p[11] == 'E') {
        return AUDIO_FORMAT_WAV;
    }
    
    /* MP3 (sync word or ID3) */
    if ((p[0] == 0xFF && (p[1] & 0xE0) == 0xE0) ||
        (p[0] == 'I' && p[1] == 'D' && p[2] == '3')) {
        return AUDIO_FORMAT_MP3;
    }
    
    /* FLAC */
    if (p[0] == 'f' && p[1] == 'L' && p[2] == 'a' && p[3] == 'C') {
        return AUDIO_FORMAT_FLAC;
    }
    
    /* OGG */
    if (p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S') {
        return AUDIO_FORMAT_OGG;
    }
    
    /* AIFF */
    if (p[0] == 'F' && p[1] == 'O' && p[2] == 'R' && p[3] == 'M' &&
        (memcmp(p + 8, "AIFF", 4) == 0 || memcmp(p + 8, "AIFC", 4) == 0)) {
        return AUDIO_FORMAT_AIFF;
    }
    
    return AUDIO_FORMAT_UNKNOWN;
}

const char* audio_format_name(audio_format_t format) {
    switch (format) {
        case AUDIO_FORMAT_WAV:  return "WAV";
        case AUDIO_FORMAT_MP3:  return "MP3";
        case AUDIO_FORMAT_FLAC: return "FLAC";
        case AUDIO_FORMAT_OGG:  return "OGG Vorbis";
        case AUDIO_FORMAT_AIFF: return "AIFF";
        case AUDIO_FORMAT_OPUS: return "Opus";
        case AUDIO_FORMAT_AAC:  return "AAC";
        case AUDIO_FORMAT_M4A:  return "M4A";
        default:                return "Unknown";
    }
}

int audio_format_supported(audio_format_t format) {
    switch (format) {
        case AUDIO_FORMAT_WAV:
        case AUDIO_FORMAT_MP3:
        case AUDIO_FORMAT_FLAC:
        case AUDIO_FORMAT_OGG:
        case AUDIO_FORMAT_AIFF:
            return 1;
        default:
            return 0;
    }
}

/* ============ Unified Loader ============ */

int audio_load(const char *filepath, audio_data_t *audio) {
    if (!filepath || !audio) return -1;
    
    memset(audio, 0, sizeof(*audio));
    
    /* Read entire file */
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[Audio] Cannot open: %s\n", filepath);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 500 * 1024 * 1024) {
        fclose(f);
        return -2;
    }
    
    void *data = malloc(size);
    if (!data) {
        fclose(f);
        return -3;
    }
    
    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return -4;
    }
    fclose(f);
    
    /* Detect format */
    audio_format_t format = audio_detect_format_magic(data, size);
    if (format == AUDIO_FORMAT_UNKNOWN) {
        format = audio_detect_format_ext(filepath);
    }
    
    printf("[Audio] Loading %s as %s\n", filepath, audio_format_name(format));
    
    int result = -1;
    audio->format = format;
    
    switch (format) {
        case AUDIO_FORMAT_WAV: {
            wav_info_t wav;
            if (wav_parse(data, size, &wav) == 0) {
                audio->sample_rate = wav.sample_rate;
                audio->channels = wav.channels;
                audio->bits_per_sample = wav.bits_per_sample;
                audio->num_frames = wav.num_frames;
                audio->duration = wav_duration(&wav);
                
                audio->samples = malloc(wav.num_frames * wav.channels * sizeof(float));
                if (audio->samples) {
                    wav_to_float(&wav, audio->samples, wav.num_frames);
                    result = 0;
                }
            }
            break;
        }
        
        case AUDIO_FORMAT_MP3: {
            mp3_info_t mp3;
            if (mp3_decode(data, size, &mp3) == 0) {
                audio->sample_rate = mp3.sample_rate;
                audio->channels = mp3.channels;
                audio->bits_per_sample = 16;
                audio->num_frames = mp3.total_samples / mp3.channels;
                audio->duration = mp3.duration;
                audio->samples = mp3.samples;
                mp3.samples = NULL;  /* Transfer ownership */
                result = 0;
            }
            break;
        }
        
        case AUDIO_FORMAT_FLAC: {
            flac_info_t flac;
            if (flac_decode(data, size, &flac) == 0) {
                audio->sample_rate = flac.streaminfo.sample_rate;
                audio->channels = flac.streaminfo.channels;
                audio->bits_per_sample = flac.streaminfo.bits_per_sample;
                audio->num_frames = flac.streaminfo.total_samples;
                audio->duration = flac.duration;
                audio->samples = flac.samples;
                flac.samples = NULL;
                result = 0;
            }
            break;
        }
        
        case AUDIO_FORMAT_OGG: {
            ogg_info_t ogg;
            if (ogg_decode(data, size, &ogg) == 0) {
                audio->sample_rate = ogg.sample_rate;
                audio->channels = ogg.channels;
                audio->bits_per_sample = 16;
                audio->num_frames = ogg.total_samples;
                audio->duration = ogg.duration;
                audio->samples = ogg.samples;
                ogg.samples = NULL;
                result = 0;
            }
            break;
        }
        
        case AUDIO_FORMAT_AIFF: {
            aiff_info_t aiff;
            if (aiff_parse(data, size, &aiff) == 0) {
                audio->sample_rate = aiff.sample_rate;
                audio->channels = aiff.channels;
                audio->bits_per_sample = aiff.bits_per_sample;
                audio->num_frames = aiff.num_frames;
                audio->duration = aiff.duration;
                
                audio->samples = malloc(aiff.num_frames * aiff.channels * sizeof(float));
                if (audio->samples) {
                    aiff_to_float(&aiff, audio->samples, aiff.num_frames);
                    result = 0;
                }
            }
            break;
        }
        
        default:
            fprintf(stderr, "[Audio] Unsupported format: %s\n", audio_format_name(format));
            break;
    }
    
    free(data);
    
    if (result == 0) {
        printf("[Audio] Loaded: %d Hz, %d ch, %zu frames, %.2f sec\n",
               audio->sample_rate, audio->channels, audio->num_frames, audio->duration);
    }
    
    return result;
}

int audio_decode(const void *data, size_t size, audio_data_t *audio) {
    if (!data || !audio || size < 12) return -1;
    
    /* Detect and decode */
    audio_format_t format = audio_detect_format_magic(data, size);
    
    /* Similar to audio_load but from memory */
    /* Implementation would mirror load function */
    (void)format;
    
    return -1;  /* Not fully implemented */
}

void audio_free(audio_data_t *audio) {
    if (audio) {
        if (audio->samples) {
            free(audio->samples);
            audio->samples = NULL;
        }
        memset(audio, 0, sizeof(*audio));
    }
}
