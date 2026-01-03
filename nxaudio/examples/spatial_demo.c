/*
 * NXAudio Example - Simple 3D Audio Demo
 * 
 * Demonstrates spatial audio with moving sound source
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include <nxaudio/nxaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#define PI 3.14159265358979323846f

int main(int argc, char *argv[]) {
    printf("NXAudio 3D Audio Demo\n");
    printf("=====================\n\n");
    
    /* Initialize NXAudio */
    if (nxaudio_init() != NXAUDIO_SUCCESS) {
        fprintf(stderr, "Failed to initialize NXAudio\n");
        return 1;
    }
    
    printf("NXAudio version: %s\n", nxaudio_version());
    
    /* Create audio context */
    nxaudio_context_t ctx = nxaudio_context_create();
    if (ctx == NXAUDIO_INVALID_HANDLE) {
        fprintf(stderr, "Failed to create context\n");
        nxaudio_shutdown();
        return 1;
    }
    
    printf("Audio context created\n");
    
    /* Load default HRTF */
    if (nxaudio_hrtf_load_default(ctx) == NXAUDIO_SUCCESS) {
        nxaudio_hrtf_info_t info;
        nxaudio_hrtf_info(ctx, &info);
        printf("Loaded HRTF: %s (%d positions)\n", info.name, info.num_positions);
    }
    
    /* Set listener position (center) */
    nxaudio_listener_t listener = {
        .position = {0.0f, 0.0f, 0.0f},
        .velocity = {0.0f, 0.0f, 0.0f},
        .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
        .gain = 1.0f
    };
    nxaudio_context_set_listener(ctx, &listener);
    
    /* Create audio buffer (would load from file in real usage) */
    /* For demo, we create a simple tone */
    size_t sample_rate = 48000;
    size_t duration_samples = sample_rate * 2;  /* 2 seconds */
    float *tone_data = malloc(duration_samples * sizeof(float));
    
    if (!tone_data) {
        fprintf(stderr, "Failed to allocate audio buffer\n");
        nxaudio_context_destroy(ctx);
        nxaudio_shutdown();
        return 1;
    }
    
    /* Generate 440Hz sine wave */
    for (size_t i = 0; i < duration_samples; i++) {
        float t = (float)i / sample_rate;
        tone_data[i] = sinf(2.0f * PI * 440.0f * t) * 0.5f;
    }
    
    nxaudio_buffer_t buffer = nxaudio_buffer_create_mem(
        tone_data, duration_samples * sizeof(float),
        NXAUDIO_FORMAT_F32, sample_rate, 1
    );
    
    free(tone_data);
    
    if (buffer == NXAUDIO_INVALID_HANDLE) {
        fprintf(stderr, "Failed to create audio buffer\n");
        nxaudio_context_destroy(ctx);
        nxaudio_shutdown();
        return 1;
    }
    
    printf("Audio buffer created (%.2f seconds)\n", 
           nxaudio_buffer_duration(buffer));
    
    /* Create 3D audio object */
    nxaudio_object_config_t config = {
        .position = {3.0f, 0.0f, -2.0f},  /* Start right and forward */
        .velocity = {0.0f, 0.0f, 0.0f},
        .gain = 1.0f,
        .pitch = 1.0f,
        .min_distance = 1.0f,
        .max_distance = 50.0f,
        .rolloff = 1.0f,
        .looping = 1,
        .spatial = 1,
        .reverb = 1,
        .occlusion = 0
    };
    
    nxaudio_object_t obj = nxaudio_object_create(ctx, buffer, &config);
    if (obj == NXAUDIO_INVALID_HANDLE) {
        fprintf(stderr, "Failed to create audio object\n");
        nxaudio_buffer_destroy(buffer);
        nxaudio_context_destroy(ctx);
        nxaudio_shutdown();
        return 1;
    }
    
    printf("3D audio object created at (%.1f, %.1f, %.1f)\n",
           config.position.x, config.position.y, config.position.z);
    
    /* Enable reverb */
    nxaudio_reverb_set(ctx, 0.8f, 0.5f, 0.3f, 1.0f);
    nxaudio_reverb_enable(ctx, 1);
    printf("Room reverb enabled\n");
    
    /* Play the sound */
    printf("\nPlaying spatial audio...\n");
    printf("The sound will orbit around you.\n\n");
    
    nxaudio_object_play(obj);
    
    /* Animate the sound source in a circle */
    float angle = 0.0f;
    float radius = 3.0f;
    
    for (int i = 0; i < 100; i++) {
        /* Calculate new position on circle */
        float x = radius * sinf(angle);
        float z = -radius * cosf(angle);
        
        /* Update object position */
        nxaudio_object_set_position(obj, x, 0.0f, z);
        
        /* Print position */
        printf("\rPosition: (%+5.2f, %+5.2f, %+5.2f)", x, 0.0f, z);
        fflush(stdout);
        
        /* Advance angle */
        angle += 0.1f;
        if (angle > 2.0f * PI) angle -= 2.0f * PI;
        
        /* Wait 100ms */
        usleep(100000);
    }
    
    printf("\n\nDemo complete!\n");
    
    /* Cleanup */
    nxaudio_object_stop(obj);
    nxaudio_object_destroy(obj);
    nxaudio_buffer_destroy(buffer);
    nxaudio_context_destroy(ctx);
    nxaudio_shutdown();
    
    printf("Cleanup complete, goodbye!\n");
    
    return 0;
}
