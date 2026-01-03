/*
 * NXAudio Test - Play Audio with ALSA
 * 
 * Simple test that generates a tone and plays it through ALSA
 * 
 * Compile: gcc -o nxaudio_test nxaudio_test.c -lasound -lm
 * Run: ./nxaudio_test
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE     48000
#define CHANNELS        2
#define DURATION_SEC    3
#define PI              3.14159265358979323846

/* Generate stereo audio with 3D spatial effect */
void generate_spatial_audio(float *buffer, size_t num_frames, 
                             float angle, float frequency) {
    /* Calculate stereo gains based on angle (simple panning) */
    float pan = sinf(angle);  /* -1 to 1 */
    float left_gain = cosf((pan + 1.0f) * 0.25f * PI);
    float right_gain = sinf((pan + 1.0f) * 0.25f * PI);
    
    for (size_t i = 0; i < num_frames; i++) {
        float t = (float)i / SAMPLE_RATE;
        
        /* Generate tone with envelope */
        float env = 1.0f;
        if (i < 4800) env = (float)i / 4800.0f;  /* 100ms attack */
        if (i > num_frames - 4800) env = (float)(num_frames - i) / 4800.0f;  /* 100ms release */
        
        float sample = sinf(2.0f * PI * frequency * t) * 0.5f * env;
        
        buffer[i * 2] = sample * left_gain;
        buffer[i * 2 + 1] = sample * right_gain;
    }
}

int main(int argc, char *argv[]) {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *params;
    int err;
    
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║          NXAudio Spatial Audio Test                  ║\n");
    printf("║          Copyright (c) 2025 KetiveeAI                ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    
    /* Open ALSA device */
    err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Error: Cannot open audio device: %s\n", snd_strerror(err));
        return 1;
    }
    
    printf("✓ Opened ALSA audio device\n");
    
    /* Configure hardware parameters */
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(pcm, params, CHANNELS);
    
    unsigned int sample_rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm, params, &sample_rate, 0);
    
    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        fprintf(stderr, "Error: Cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return 1;
    }
    
    printf("✓ Configured: %d Hz, %d channels, float32\n", sample_rate, CHANNELS);
    
    /* Prepare buffer */
    size_t frames_per_segment = SAMPLE_RATE / 2;  /* 0.5 sec per position */
    float *buffer = malloc(frames_per_segment * CHANNELS * sizeof(float));
    
    if (!buffer) {
        fprintf(stderr, "Error: Cannot allocate buffer\n");
        snd_pcm_close(pcm);
        return 1;
    }
    
    printf("\n🔊 Playing 3D spatial audio demo...\n");
    printf("   Sound will move: CENTER → RIGHT → BEHIND → LEFT → FRONT\n\n");
    
    /* Play sound at different positions */
    float positions[] = {
        0.0f,           /* Center */
        PI / 2,         /* Right */
        PI,             /* Behind */
        -PI / 2,        /* Left */
        0.0f,           /* Front again */
        -PI / 4         /* Front-left */
    };
    
    const char *position_names[] = {
        "CENTER",
        "RIGHT",
        "BEHIND",
        "LEFT",
        "FRONT",
        "FRONT-LEFT"
    };
    
    float frequencies[] = {440.0f, 523.25f, 659.25f, 783.99f, 880.0f, 1046.5f};
    
    for (int pos = 0; pos < 6; pos++) {
        printf("   ▶ Position: %-10s (%.0f Hz)\n", position_names[pos], frequencies[pos]);
        
        generate_spatial_audio(buffer, frames_per_segment, 
                               positions[pos], frequencies[pos]);
        
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, buffer, frames_per_segment);
        if (frames < 0) {
            frames = snd_pcm_recover(pcm, frames, 0);
        }
        if (frames < 0) {
            fprintf(stderr, "     Write error: %s\n", snd_strerror(frames));
        }
    }
    
    /* Wait for playback to complete */
    snd_pcm_drain(pcm);
    
    printf("\n✓ Playback complete!\n");
    
    /* Cleanup */
    free(buffer);
    snd_pcm_close(pcm);
    
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  NXAudio test finished successfully!                 ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    
    return 0;
}
