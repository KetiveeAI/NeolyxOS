/* Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

/*
 * NeolyxSpatial Test Harness
 * 
 * Test spatial audio with multiple orbiting objects
 * 
 * Compile: gcc -o spatial_test spatial_test.c nx_spatial.c -lm -lasound
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <alsa/asoundlib.h>

#include "../spatial/nx_spatial.h"

#define SAMPLE_RATE     48000
#define BLOCK_SIZE      1024
#define CHANNELS        2
#define NUM_OBJECTS     4
#define DURATION_SEC    10

#define PI 3.14159265358979323846f

/* Generate test tone */
static void generate_tone(float *buffer, size_t frames, float freq, float phase) {
    for (size_t i = 0; i < frames; i++) {
        buffer[i] = 0.3f * sinf(2.0f * PI * freq * ((float)i / SAMPLE_RATE) + phase);
    }
}

int main(int argc, char *argv[]) {
    printf("NeolyxSpatial Test Harness\n");
    printf("==========================\n\n");
    
    (void)argc; (void)argv;
    
    /* Create spatial engine */
    nx_spatial_t *sp = nx_spatial_create(SAMPLE_RATE, BLOCK_SIZE);
    if (!sp) {
        fprintf(stderr, "Failed to create spatial engine\n");
        return 1;
    }
    
    /* Load default HRTF */
    if (nx_spatial_load_hrtf_default(sp) != NX_SPATIAL_OK) {
        fprintf(stderr, "Failed to load HRTF\n");
        nx_spatial_destroy(sp);
        return 1;
    }
    
    printf("HRTF loaded: %s\n\n", sp->hrtf->name);
    
    /* Set listener at origin */
    nx_listener_t listener = {
        .position = {0, 0, 0},
        .forward = {0, 0, -1},
        .up = {0, 1, 0},
        .velocity = {0, 0, 0},
        .gain = 1.0f
    };
    nx_spatial_set_listener(sp, &listener);
    
    /* Add test objects at different positions */
    float tones[] = {261.63f, 329.63f, 392.0f, 523.25f};  /* C4, E4, G4, C5 */
    
    for (int i = 0; i < NUM_OBJECTS; i++) {
        float angle = (float)i * 2.0f * PI / NUM_OBJECTS;
        
        nx_object_t obj = {
            .id = i,
            .active = 1,
            .position = {3.0f * sinf(angle), 0, 3.0f * cosf(angle)},
            .velocity = {0, 0, 0},
            .gain = 1.0f,
            .min_distance = 1.0f,
            .max_distance = 20.0f,
            .rolloff = 1.0f,
            .spatial_enabled = 1
        };
        
        nx_spatial_add_object(sp, i, &obj);
        
        printf("Object %d: tone %.1f Hz at (%.1f, %.1f, %.1f)\n",
               i, tones[i], obj.position.x, obj.position.y, obj.position.z);
    }
    
    printf("\n");
    
    /* Open ALSA device */
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *params;
    
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf(stderr, "Cannot open audio device\n");
        nx_spatial_destroy(sp);
        return 1;
    }
    
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(pcm, params, CHANNELS);
    
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0);
    snd_pcm_hw_params(pcm, params);
    
    printf("Audio: %d Hz, %d channels\n\n", rate, CHANNELS);
    
    /* Allocate buffers */
    float *mono_buffers[NUM_OBJECTS];
    for (int i = 0; i < NUM_OBJECTS; i++) {
        mono_buffers[i] = (float*)malloc(BLOCK_SIZE * sizeof(float));
    }
    float *stereo_out = (float*)malloc(BLOCK_SIZE * CHANNELS * sizeof(float));
    
    /* Render loop */
    printf("Playing spatial audio for %d seconds...\n", DURATION_SEC);
    printf("Objects are orbiting around the listener.\n\n");
    
    int total_blocks = (SAMPLE_RATE * DURATION_SEC) / BLOCK_SIZE;
    float global_phase = 0;
    
    for (int block = 0; block < total_blocks; block++) {
        /* Update object positions (orbit) */
        float t = (float)block * BLOCK_SIZE / SAMPLE_RATE;
        float orbit_speed = 0.5f;  /* Radians per second */
        
        for (int i = 0; i < NUM_OBJECTS; i++) {
            float angle = (float)i * 2.0f * PI / NUM_OBJECTS + t * orbit_speed;
            float radius = 3.0f;
            
            nx_spatial_set_object_pos(sp, i, 
                                       radius * sinf(angle),
                                       0.5f * sinf(t * 2.0f + i),  /* Height variation */
                                       radius * cosf(angle));
        }
        
        /* Generate tones for each object */
        for (int i = 0; i < NUM_OBJECTS; i++) {
            generate_tone(mono_buffers[i], BLOCK_SIZE, tones[i], 
                          global_phase + i * PI / 2);
        }
        
        global_phase += 2.0f * PI * tones[0] * BLOCK_SIZE / SAMPLE_RATE;
        
        /* Render spatial audio */
        nx_spatial_render(sp, mono_buffers, BLOCK_SIZE, stereo_out);
        
        /* Write to ALSA */
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, stereo_out, BLOCK_SIZE);
        if (frames < 0) {
            frames = snd_pcm_recover(pcm, frames, 0);
        }
        
        /* Progress */
        if (block % 50 == 0) {
            int percent = (block * 100) / total_blocks;
            printf("\rProgress: %3d%%", percent);
            fflush(stdout);
        }
    }
    
    printf("\rProgress: 100%%\n\n");
    
    /* Drain and cleanup */
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    
    for (int i = 0; i < NUM_OBJECTS; i++) {
        free(mono_buffers[i]);
    }
    free(stereo_out);
    
    nx_spatial_destroy(sp);
    
    printf("Test complete!\n");
    
    return 0;
}
