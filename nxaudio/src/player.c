/*
 * NXAudio Player - Play WAV/MP3 Files
 * 
 * Usage: nxaudio_player <audio_file>
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <alsa/asoundlib.h>

/* Include our codecs */
#include "codecs/wav.h"

#define BUFFER_FRAMES   1024

/* Simple string check for file extension */
static int ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return strcasecmp(str + str_len - suf_len, suffix) == 0;
}

/* Play float samples through ALSA */
static int play_audio(float *samples, size_t num_frames, 
                       int channels, int sample_rate) {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *params;
    int err;
    
    /* Open device */
    err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Cannot open audio: %s\n", snd_strerror(err));
        return -1;
    }
    
    /* Configure */
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(pcm, params, channels);
    
    unsigned int rate = sample_rate;
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0);
    
    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        fprintf(stderr, "Cannot configure: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return -1;
    }
    
    printf("Playing: %d Hz, %d channels, %zu frames\n", 
           sample_rate, channels, num_frames);
    
    /* Play in chunks */
    float *ptr = samples;
    size_t remaining = num_frames;
    
    while (remaining > 0) {
        size_t chunk = (remaining > BUFFER_FRAMES) ? BUFFER_FRAMES : remaining;
        
        snd_pcm_sframes_t written = snd_pcm_writei(pcm, ptr, chunk);
        if (written < 0) {
            written = snd_pcm_recover(pcm, written, 0);
        }
        if (written < 0) {
            fprintf(stderr, "Write error: %s\n", snd_strerror(written));
            break;
        }
        
        ptr += written * channels;
        remaining -= written;
        
        /* Progress indicator */
        int progress = (int)(100.0 * (num_frames - remaining) / num_frames);
        printf("\rPlaying: %3d%%", progress);
        fflush(stdout);
    }
    
    printf("\rPlaying: 100%%\n");
    
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    
    return 0;
}

int main(int argc, char *argv[]) {
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║          NXAudio Audio Player                        ║\n");
    printf("║          Copyright (c) 2025 KetiveeAI                ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <audio_file.wav>\n", argv[0]);
        printf("\nSupported formats: WAV (PCM 8/16/24/32-bit, float)\n");
        return 1;
    }
    
    const char *filepath = argv[1];
    printf("Loading: %s\n", filepath);
    
    if (ends_with(filepath, ".wav")) {
        /* Load WAV file */
        wav_info_t wav;
        void *wav_data;
        
        if (wav_load(filepath, &wav, &wav_data) != 0) {
            fprintf(stderr, "Failed to load WAV file\n");
            return 1;
        }
        
        printf("Format: WAV %d-bit, %d Hz, %d ch\n",
               wav.bits_per_sample, wav.sample_rate, wav.channels);
        printf("Duration: %.2f seconds\n\n", wav_duration(&wav));
        
        /* Convert to float */
        float *samples = malloc(wav.num_frames * wav.channels * sizeof(float));
        if (!samples) {
            fprintf(stderr, "Out of memory\n");
            wav_free(wav_data);
            return 1;
        }
        
        wav_to_float(&wav, samples, wav.num_frames);
        
        /* Play */
        play_audio(samples, wav.num_frames, wav.channels, wav.sample_rate);
        
        /* Cleanup */
        free(samples);
        wav_free(wav_data);
    }
    else {
        fprintf(stderr, "Unsupported format. Use .wav files.\n");
        return 1;
    }
    
    printf("\n✓ Playback complete!\n");
    
    return 0;
}
