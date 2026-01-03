/*
 * NXAudio Server - Main Implementation
 * 
 * User-space audio mixing daemon for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "nxaudio_server.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============ Memory Helpers ============ */

static void* nx_alloc(size_t size) {
    void *p = malloc(size);
    if (p) memset(p, 0, size);
    return p;
}

static void nx_free(void *p) {
    if (p) free(p);
}

/* ============ Math Helpers ============ */

static float vec3_length(nxaudio_pos3_t v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static nxaudio_pos3_t vec3_sub(nxaudio_pos3_t a, nxaudio_pos3_t b) {
    return (nxaudio_pos3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static nxaudio_pos3_t vec3_normalize(nxaudio_pos3_t v) {
    float len = vec3_length(v);
    if (len > 0.0001f) {
        return (nxaudio_pos3_t){v.x / len, v.y / len, v.z / len};
    }
    return v;
}

static float vec3_dot(nxaudio_pos3_t a, nxaudio_pos3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* ============ Server Creation ============ */

nxaudio_server_t* nxaudio_server_create(void) {
    nxaudio_server_t *server = nx_alloc(sizeof(nxaudio_server_t));
    if (!server) return NULL;
    
    server->sample_rate = NXAUDIO_SAMPLE_RATE;
    server->channels = 2;
    server->block_frames = NXAUDIO_BLOCK_FRAMES;
    server->master_gain = 1.0f;
    server->hrtf_enabled = 1;
    
    /* Default listener at origin */
    server->listener.position = (nxaudio_pos3_t){0, 0, 0};
    server->listener.forward = (nxaudio_pos3_t){0, 0, -1};
    server->listener.up = (nxaudio_pos3_t){0, 1, 0};
    server->listener.gain = 1.0f;
    
    /* Allocate mix buffers */
    size_t buffer_size = server->block_frames * server->channels * sizeof(float);
    server->mix_buffer = nx_alloc(buffer_size);
    server->temp_buffer = nx_alloc(buffer_size);
    
    if (!server->mix_buffer || !server->temp_buffer) {
        nxaudio_server_destroy(server);
        return NULL;
    }
    
    return server;
}

void nxaudio_server_destroy(nxaudio_server_t *server) {
    if (!server) return;
    
    /* Stop if running */
    server->running = 0;
    
    /* Free buffers */
    nx_free(server->mix_buffer);
    nx_free(server->temp_buffer);
    
    /* Free source buffers */
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        if (server->sources[i].buffer_data) {
            nx_free(server->sources[i].buffer_data);
        }
    }
    
    nx_free(server);
}

/* ============ Server Initialization ============ */

int nxaudio_server_init(nxaudio_server_t *server) {
    if (!server) return -1;
    
    printf("[NXAudio Server] Initializing v%s\n", NXAUDIO_SERVER_VERSION);
    printf("[NXAudio Server] Sample rate: %d Hz\n", server->sample_rate);
    printf("[NXAudio Server] Block size: %d frames\n", server->block_frames);
    printf("[NXAudio Server] Max sources: %d\n", NXAUDIO_MAX_SOURCES);
    
    /* TODO: Connect to kernel driver via /dev/nxaudio */
    /* TODO: mmap ring buffer */
    /* TODO: Create IPC socket */
    
    printf("[NXAudio Server] Ready\n");
    
    return 0;
}

/* ============ Audio Processing ============ */

float nxaudio_calc_distance_gain(const nxaudio_listener_t *listener,
                                  const nxaudio_source_t *source) {
    nxaudio_pos3_t diff = vec3_sub(source->position, listener->position);
    float distance = vec3_length(diff);
    
    if (distance <= source->min_distance) {
        return 1.0f;
    }
    
    if (distance >= source->max_distance) {
        return 0.0f;
    }
    
    /* Inverse distance falloff */
    float ref = source->min_distance;
    float rolloff = source->rolloff;
    
    return ref / (ref + rolloff * (distance - ref));
}

void nxaudio_server_spatialize(nxaudio_server_t *server,
                                nxaudio_source_t *source,
                                const float *mono_in,
                                float *stereo_out,
                                size_t frames) {
    if (!source->spatial) {
        /* No spatialization - center pan */
        for (size_t i = 0; i < frames; i++) {
            float sample = mono_in[i] * source->gain;
            stereo_out[i * 2] = sample;
            stereo_out[i * 2 + 1] = sample;
        }
        return;
    }
    
    /* Calculate direction to source */
    nxaudio_pos3_t dir = vec3_sub(source->position, server->listener.position);
    dir = vec3_normalize(dir);
    
    /* Calculate right vector */
    nxaudio_pos3_t forward = server->listener.forward;
    nxaudio_pos3_t up = server->listener.up;
    nxaudio_pos3_t right = {
        forward.y * up.z - forward.z * up.y,
        forward.z * up.x - forward.x * up.z,
        forward.x * up.y - forward.y * up.x
    };
    right = vec3_normalize(right);
    
    /* Calculate pan (-1 = left, +1 = right) */
    float pan = vec3_dot(dir, right);
    
    /* Distance attenuation */
    float dist_gain = nxaudio_calc_distance_gain(&server->listener, source);
    
    /* Total gain */
    float gain = source->gain * dist_gain * server->listener.gain;
    
    /* Panning law (constant power) */
    float angle = (pan + 1.0f) * 0.25f * M_PI;
    float gain_l = cosf(angle) * gain;
    float gain_r = sinf(angle) * gain;
    
    /* Apply to samples */
    for (size_t i = 0; i < frames; i++) {
        stereo_out[i * 2] = mono_in[i] * gain_l;
        stereo_out[i * 2 + 1] = mono_in[i] * gain_r;
    }
    
    /* TODO: Apply HRTF for better spatialization */
}

void nxaudio_server_mix(nxaudio_server_t *server, 
                         float *output, size_t frames) {
    /* Clear output buffer */
    size_t out_samples = frames * server->channels;
    memset(output, 0, out_samples * sizeof(float));
    
    /* Mix each active source */
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        nxaudio_source_t *src = &server->sources[i];
        if (!src->playing || !src->buffer_data) continue;
        
        /* Get source samples */
        float *src_data = (float*)src->buffer_data;
        size_t src_frames = src->buffer_size / sizeof(float);
        
        /* Read from current position */
        size_t to_read = frames;
        if (src->buffer_offset + to_read > src_frames) {
            to_read = src_frames - src->buffer_offset;
        }
        
        /* Spatialize source to temp buffer */
        nxaudio_server_spatialize(server, src,
                                   src_data + src->buffer_offset,
                                   server->temp_buffer,
                                   to_read);
        
        /* Mix to output */
        for (size_t s = 0; s < to_read * 2; s++) {
            output[s] += server->temp_buffer[s];
        }
        
        /* Advance position */
        src->buffer_offset += to_read;
        
        /* Handle looping or end */
        if (src->buffer_offset >= src_frames) {
            if (src->looping) {
                src->buffer_offset = 0;
            } else {
                src->playing = 0;
            }
        }
    }
    
    /* Apply master gain and soft clip */
    float master = server->master_gain;
    for (size_t s = 0; s < out_samples; s++) {
        float sample = output[s] * master;
        
        /* Soft clip at ±1.0 */
        if (sample > 1.0f) {
            sample = 1.0f - expf(-sample);
        } else if (sample < -1.0f) {
            sample = -1.0f + expf(sample);
        }
        
        output[s] = sample;
    }
}

int nxaudio_server_process(nxaudio_server_t *server) {
    if (!server || !server->running) return -1;
    
    /* Mix to output buffer */
    nxaudio_server_mix(server, server->mix_buffer, server->block_frames);
    
    /* TODO: Write to kernel ring buffer */
    
    return 0;
}

/* ============ Main Loop ============ */

int nxaudio_server_run(nxaudio_server_t *server) {
    if (!server) return -1;
    
    server->running = 1;
    printf("[NXAudio Server] Starting main loop\n");
    
    while (server->running) {
        if (!server->suspended) {
            nxaudio_server_process(server);
        }
        
        /* TODO: Wait for kernel period or IPC event */
        /* For now just yield */
        /* usleep(1000); */
    }
    
    printf("[NXAudio Server] Stopped\n");
    return 0;
}

void nxaudio_server_stop(nxaudio_server_t *server) {
    if (server) {
        server->running = 0;
    }
}

/* ============ Source Management ============ */

static int find_free_source(nxaudio_server_t *server) {
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        if (server->sources[i].id == 0) {
            return i;
        }
    }
    return -1;
}

int nxaudio_server_source_create(nxaudio_server_t *server,
                                  uint32_t client_id,
                                  const void *data, size_t size) {
    int slot = find_free_source(server);
    if (slot < 0) return -1;
    
    nxaudio_source_t *src = &server->sources[slot];
    
    src->id = slot + 1;
    src->client_id = client_id;
    src->gain = 1.0f;
    src->pitch = 1.0f;
    src->min_distance = 1.0f;
    src->max_distance = 100.0f;
    src->rolloff = 1.0f;
    src->spatial = 1;
    src->looping = 0;
    src->playing = 0;
    src->buffer_offset = 0;
    
    /* Copy buffer data */
    if (data && size > 0) {
        src->buffer_data = nx_alloc(size);
        if (!src->buffer_data) {
            src->id = 0;
            return -1;
        }
        memcpy(src->buffer_data, data, size);
        src->buffer_size = size;
    }
    
    server->source_count++;
    return src->id;
}

void nxaudio_server_source_destroy(nxaudio_server_t *server, uint32_t id) {
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        if (server->sources[i].id == id) {
            nxaudio_source_t *src = &server->sources[i];
            nx_free(src->buffer_data);
            memset(src, 0, sizeof(nxaudio_source_t));
            server->source_count--;
            break;
        }
    }
}

void nxaudio_server_source_play(nxaudio_server_t *server, uint32_t id) {
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        if (server->sources[i].id == id) {
            server->sources[i].playing = 1;
            break;
        }
    }
}

void nxaudio_server_source_stop(nxaudio_server_t *server, uint32_t id) {
    for (int i = 0; i < NXAUDIO_MAX_SOURCES; i++) {
        if (server->sources[i].id == id) {
            server->sources[i].playing = 0;
            server->sources[i].buffer_offset = 0;
            break;
        }
    }
}

/* ============ IPC Handler ============ */

int nxaudio_server_handle_message(nxaudio_server_t *server, 
                                   int client_fd,
                                   const void *data, size_t len) {
    if (!server || !data || len < 1) return -1;
    
    const uint8_t *msg = data;
    uint8_t type = msg[0];
    
    switch (type) {
        case NXMSG_CONNECT:
            printf("[NXAudio Server] Client connected: fd=%d\n", client_fd);
            break;
            
        case NXMSG_DISCONNECT:
            printf("[NXAudio Server] Client disconnected: fd=%d\n", client_fd);
            break;
            
        case NXMSG_MASTER_GAIN:
            if (len >= 5) {
                float gain;
                memcpy(&gain, msg + 1, 4);
                server->master_gain = gain;
            }
            break;
            
        default:
            printf("[NXAudio Server] Unknown message type: 0x%02X\n", type);
            return -1;
    }
    
    return 0;
}

/* ============ Main Entry Point ============ */

#ifdef NXAUDIO_SERVER_MAIN

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("NXAudio Server v%s\n", NXAUDIO_SERVER_VERSION);
    printf("NeolyxOS Native Audio Daemon\n\n");
    
    nxaudio_server_t *server = nxaudio_server_create();
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    if (nxaudio_server_init(server) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        nxaudio_server_destroy(server);
        return 1;
    }
    
    nxaudio_server_run(server);
    
    nxaudio_server_destroy(server);
    return 0;
}

#endif /* NXAUDIO_SERVER_MAIN */
