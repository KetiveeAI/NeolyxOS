/*
 * NXAudio Engine Core Implementation
 * 
 * decoder → stream → resampler → mixer → effects → device
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_SOUNDS          64
#define DECODE_BUFFER_SIZE  4096
#define RESAMPLE_BUFFER_SIZE 8192

/* ============ Helpers ============ */

static nx_sound_state_t* get_sound(nx_engine_t *engine, nx_sound_t id) {
    if (!engine || id < 0 || id >= engine->max_sounds) return NULL;
    
    nx_sound_state_t *s = &engine->sounds[id];
    return s->active ? s : NULL;
}

/* ============ Initialization ============ */

nx_error_t nx_engine_create(nx_engine_t *engine) {
    nx_engine_config_t config = {
        .output_sample_rate = 48000,
        .output_channels = 2,
        .buffer_frames = 1024,
        .resample_quality = NX_RESAMPLE_MEDIUM,
    };
    return nx_engine_create_ex(engine, &config);
}

nx_error_t nx_engine_create_ex(nx_engine_t *engine, const nx_engine_config_t *config) {
    if (!engine || !config) return NX_ERR_INVALID;
    
    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    engine->max_sounds = MAX_SOUNDS;
    
    /* Allocate sound slots */
    engine->sounds = (nx_sound_state_t*)calloc(MAX_SOUNDS, sizeof(nx_sound_state_t));
    if (!engine->sounds) return NX_ERR_MEMORY;
    
    /* Initialize mixer */
    nx_mixer_init(&engine->mixer, config->output_sample_rate, config->output_channels);
    
    /* Allocate output buffer */
    engine->output_frames = config->buffer_frames;
    engine->output_buffer = (float*)malloc(engine->output_frames * config->output_channels * sizeof(float));
    if (!engine->output_buffer) {
        free(engine->sounds);
        return NX_ERR_MEMORY;
    }
    
    /* Initialize master effects */
    nx_effects_init(&engine->master_effects, config->output_sample_rate);
    
    engine->initialized = 1;
    engine->running = 1;
    
    printf("[Engine] Created: %d Hz, %d ch, %zu buffer\n",
           config->output_sample_rate, config->output_channels, engine->output_frames);
    
    return NX_OK;
}

void nx_engine_destroy(nx_engine_t *engine) {
    if (!engine) return;
    
    /* Unload all sounds */
    if (engine->sounds) {
        for (int i = 0; i < engine->max_sounds; i++) {
            if (engine->sounds[i].active) {
                nx_engine_unload(engine, i);
            }
        }
        free(engine->sounds);
    }
    
    /* Free mixer */
    nx_mixer_destroy(&engine->mixer);
    
    /* Free output buffer */
    if (engine->output_buffer) {
        free(engine->output_buffer);
    }
    
    memset(engine, 0, sizeof(*engine));
}

/* ============ Sound Management ============ */

nx_sound_t nx_engine_load(nx_engine_t *engine, const char *filepath) {
    if (!engine || !filepath) return NX_SOUND_INVALID;
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < engine->max_sounds; i++) {
        if (!engine->sounds[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        fprintf(stderr, "[Engine] No free sound slots\n");
        return NX_SOUND_INVALID;
    }
    
    nx_sound_state_t *sound = &engine->sounds[slot];
    memset(sound, 0, sizeof(*sound));
    
    /* Open stream */
    if (nx_stream_open(&sound->stream, filepath) != NX_OK) {
        return NX_SOUND_INVALID;
    }
    
    /* Initialize resampler if needed */
    if (sound->stream.sample_rate != engine->config.output_sample_rate) {
        if (nx_resample_init(&sound->resampler,
                             sound->stream.sample_rate,
                             engine->config.output_sample_rate,
                             sound->stream.channels,
                             engine->config.resample_quality) != 0) {
            nx_stream_close(&sound->stream);
            return NX_SOUND_INVALID;
        }
    }
    
    /* Initialize effects */
    nx_effects_init(&sound->effects, engine->config.output_sample_rate);
    
    /* Allocate buffers */
    sound->buffer_size = DECODE_BUFFER_SIZE;
    sound->decode_buffer = (float*)malloc(DECODE_BUFFER_SIZE * sound->stream.channels * sizeof(float));
    sound->resample_buffer = (float*)malloc(RESAMPLE_BUFFER_SIZE * sound->stream.channels * sizeof(float));
    
    if (!sound->decode_buffer || !sound->resample_buffer) {
        nx_stream_close(&sound->stream);
        if (sound->decode_buffer) free(sound->decode_buffer);
        if (sound->resample_buffer) free(sound->resample_buffer);
        return NX_SOUND_INVALID;
    }
    
    /* Load metadata */
    nx_metadata_load(filepath, &sound->metadata);
    
    sound->active = 1;
    sound->volume = 1.0f;
    sound->pan = 0.0f;
    sound->mixer_channel = -1;
    
    engine->active_sounds++;
    
    printf("[Engine] Loaded: %s (slot %d)\n", filepath, slot);
    
    return slot;
}

void nx_engine_unload(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return;
    
    /* Remove from mixer */
    if (sound->mixer_channel >= 0) {
        nx_mixer_remove_channel(&engine->mixer, sound->mixer_channel);
    }
    
    /* Close stream */
    nx_stream_close(&sound->stream);
    
    /* Free resampler */
    nx_resample_destroy(&sound->resampler);
    
    /* Free buffers */
    if (sound->decode_buffer) free(sound->decode_buffer);
    if (sound->resample_buffer) free(sound->resample_buffer);
    
    sound->active = 0;
    engine->active_sounds--;
}

/* ============ Playback Control ============ */

nx_error_t nx_engine_play(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return NX_ERR_INVALID;
    
    sound->stream.state = NX_STREAM_PLAYING;
    sound->paused = 0;
    
    return NX_OK;
}

nx_error_t nx_engine_pause(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return NX_ERR_INVALID;
    
    sound->stream.state = NX_STREAM_PAUSED;
    sound->paused = 1;
    
    return NX_OK;
}

nx_error_t nx_engine_stop(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return NX_ERR_INVALID;
    
    sound->stream.state = NX_STREAM_STOPPED;
    nx_stream_seek(&sound->stream, 0);
    
    return NX_OK;
}

void nx_engine_set_volume(nx_engine_t *engine, nx_sound_t id, float volume) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return;
    
    sound->volume = (volume < 0) ? 0 : volume;
    sound->effects.gain = sound->volume;
}

void nx_engine_set_pan(nx_engine_t *engine, nx_sound_t id, float pan) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return;
    
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    sound->pan = pan;
    sound->effects.pan = pan;
}

void nx_engine_set_loop(nx_engine_t *engine, nx_sound_t id, int loop) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return;
    
    sound->looping = loop ? 1 : 0;
}

nx_error_t nx_engine_seek(nx_engine_t *engine, nx_sound_t id, float time_sec) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return NX_ERR_INVALID;
    
    return nx_stream_seek_time(&sound->stream, time_sec);
}

float nx_engine_get_position(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return 0.0f;
    
    return nx_stream_tell_time(&sound->stream);
}

float nx_engine_get_duration(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return 0.0f;
    
    return sound->stream.duration;
}

const nx_metadata_t* nx_engine_get_metadata(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return NULL;
    
    return &sound->metadata;
}

int nx_engine_is_playing(nx_engine_t *engine, nx_sound_t id) {
    nx_sound_state_t *sound = get_sound(engine, id);
    if (!sound) return 0;
    
    return sound->stream.state == NX_STREAM_PLAYING && !sound->paused;
}

void nx_engine_set_master_volume(nx_engine_t *engine, float volume) {
    if (!engine) return;
    nx_mixer_set_master(&engine->mixer, volume);
    engine->master_effects.gain = volume;
}

/* ============ Audio Rendering ============ */

size_t nx_engine_render(nx_engine_t *engine, float *output, size_t frames) {
    if (!engine || !output) return 0;
    
    /* Clear output */
    memset(output, 0, frames * engine->config.output_channels * sizeof(float));
    
    /* Process each active sound */
    for (int i = 0; i < engine->max_sounds; i++) {
        nx_sound_state_t *sound = &engine->sounds[i];
        if (!sound->active) continue;
        if (sound->stream.state != NX_STREAM_PLAYING) continue;
        if (sound->paused) continue;
        
        size_t frames_read = frames;
        float *src = sound->decode_buffer;
        
        /* Decode from stream */
        nx_error_t err = nx_stream_read(&sound->stream, sound->decode_buffer, &frames_read);
        
        if (err == NX_ERR_EOF) {
            if (sound->looping) {
                nx_stream_seek(&sound->stream, 0);
                frames_read = frames;
                nx_stream_read(&sound->stream, sound->decode_buffer, &frames_read);
            } else {
                sound->stream.state = NX_STREAM_FINISHED;
                continue;
            }
        }
        
        if (frames_read == 0) continue;
        
        /* Resample if needed */
        if (sound->stream.sample_rate != engine->config.output_sample_rate) {
            size_t resampled = nx_resample_process(&sound->resampler,
                                                   sound->decode_buffer, frames_read,
                                                   sound->resample_buffer, RESAMPLE_BUFFER_SIZE);
            src = sound->resample_buffer;
            frames_read = resampled;
        }
        
        /* Apply effects */
        nx_effects_process(&sound->effects, src, frames_read, sound->stream.channels);
        
        /* Mix to output */
        size_t out_samples = frames_read * engine->config.output_channels;
        for (size_t s = 0; s < out_samples && s < frames * engine->config.output_channels; s++) {
            output[s] += src[s % (frames_read * sound->stream.channels)];
        }
    }
    
    /* Apply master effects */
    nx_effects_process(&engine->master_effects, output, 
                       frames, engine->config.output_channels);
    
    return frames;
}

size_t nx_engine_render_s16(nx_engine_t *engine, int16_t *output, size_t frames) {
    if (!engine || !output) return 0;
    
    /* Render to float first */
    float *float_buf = (float*)malloc(frames * engine->config.output_channels * sizeof(float));
    if (!float_buf) return 0;
    
    size_t rendered = nx_engine_render(engine, float_buf, frames);
    
    /* Convert to int16 */
    nx_dsp_float_to_s16(float_buf, output, rendered * engine->config.output_channels);
    
    free(float_buf);
    
    return rendered;
}
