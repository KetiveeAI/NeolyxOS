/*
 * NXAudio Mixer Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "mixer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PI 3.14159265358979323846f

/* ============ Initialization ============ */

void nx_mixer_init(nx_mixer_t *mixer, uint32_t sample_rate, uint32_t channels) {
    if (!mixer) return;
    
    memset(mixer, 0, sizeof(*mixer));
    mixer->sample_rate = sample_rate;
    mixer->output_channels = channels;
    mixer->master_volume = 1.0f;
    
    /* Initialize all channels */
    for (int i = 0; i < NX_MIXER_MAX_CHANNELS; i++) {
        mixer->channels[i].volume = 1.0f;
        mixer->channels[i].pan = 0.0f;
    }
}

void nx_mixer_destroy(nx_mixer_t *mixer) {
    if (!mixer) return;
    
    if (mixer->output) {
        free(mixer->output);
    }
    memset(mixer, 0, sizeof(*mixer));
}

/* ============ Channel Management ============ */

int nx_mixer_add_channel(nx_mixer_t *mixer, const float *buffer, 
                          size_t frames, int loop) {
    if (!mixer || !buffer) return -1;
    
    /* Find free slot */
    for (int i = 0; i < NX_MIXER_MAX_CHANNELS; i++) {
        if (!mixer->channels[i].active) {
            nx_mixer_channel_t *ch = &mixer->channels[i];
            ch->active = 1;
            ch->buffer = buffer;
            ch->buffer_frames = frames;
            ch->position = 0;
            ch->looping = loop;
            ch->volume = 1.0f;
            ch->pan = 0.0f;
            ch->muted = 0;
            ch->solo = 0;
            mixer->num_active++;
            return i;
        }
    }
    
    return -1;
}

void nx_mixer_remove_channel(nx_mixer_t *mixer, int channel) {
    if (!mixer || channel < 0 || channel >= NX_MIXER_MAX_CHANNELS) return;
    
    if (mixer->channels[channel].active) {
        mixer->channels[channel].active = 0;
        mixer->num_active--;
    }
}

void nx_mixer_set_volume(nx_mixer_t *mixer, int channel, float volume) {
    if (!mixer || channel < 0 || channel >= NX_MIXER_MAX_CHANNELS) return;
    mixer->channels[channel].volume = (volume < 0) ? 0 : volume;
}

void nx_mixer_set_pan(nx_mixer_t *mixer, int channel, float pan) {
    if (!mixer || channel < 0 || channel >= NX_MIXER_MAX_CHANNELS) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    mixer->channels[channel].pan = pan;
}

void nx_mixer_mute(nx_mixer_t *mixer, int channel, int mute) {
    if (!mixer || channel < 0 || channel >= NX_MIXER_MAX_CHANNELS) return;
    mixer->channels[channel].muted = mute ? 1 : 0;
}

void nx_mixer_set_master(nx_mixer_t *mixer, float volume) {
    if (!mixer) return;
    mixer->master_volume = (volume < 0) ? 0 : volume;
}

int nx_mixer_channel_playing(nx_mixer_t *mixer, int channel) {
    if (!mixer || channel < 0 || channel >= NX_MIXER_MAX_CHANNELS) return 0;
    return mixer->channels[channel].active;
}

/* ============ Processing ============ */

/* Constant-power panning */
static void calculate_pan_gains(float pan, float *left, float *right) {
    /* Convert pan (-1 to 1) to angle (0 to PI/2) */
    float angle = (pan + 1.0f) * 0.25f * PI;
    *left = cosf(angle);
    *right = sinf(angle);
}

/* Soft clipping */
static float soft_clip(float x) {
    if (x > 1.0f) {
        return 1.0f - expf(-x);
    } else if (x < -1.0f) {
        return -1.0f + expf(x);
    }
    return x;
}

void nx_mixer_process(nx_mixer_t *mixer, float *out, size_t frames) {
    if (!mixer || !out) return;
    
    size_t total_samples = frames * mixer->output_channels;
    
    /* Clear output */
    memset(out, 0, total_samples * sizeof(float));
    
    /* Check for solo channels */
    int has_solo = 0;
    for (int i = 0; i < NX_MIXER_MAX_CHANNELS; i++) {
        if (mixer->channels[i].active && mixer->channels[i].solo) {
            has_solo = 1;
            break;
        }
    }
    
    /* Mix each active channel */
    for (int ch = 0; ch < NX_MIXER_MAX_CHANNELS; ch++) {
        nx_mixer_channel_t *channel = &mixer->channels[ch];
        
        if (!channel->active) continue;
        if (channel->muted) continue;
        if (has_solo && !channel->solo) continue;
        
        /* Calculate pan gains */
        float left_gain, right_gain;
        calculate_pan_gains(channel->pan, &left_gain, &right_gain);
        
        float volume = channel->volume;
        
        for (size_t f = 0; f < frames; f++) {
            /* Get source sample (mono assumed) */
            float sample;
            if (channel->position < channel->buffer_frames) {
                sample = channel->buffer[channel->position];
                channel->position++;
            } else if (channel->looping) {
                channel->position = 0;
                sample = channel->buffer[0];
                channel->position++;
            } else {
                /* Channel finished */
                channel->active = 0;
                mixer->num_active--;
                break;
            }
            
            sample *= volume;
            
            /* Mix to stereo output */
            if (mixer->output_channels >= 2) {
                out[f * 2] += sample * left_gain;
                out[f * 2 + 1] += sample * right_gain;
            } else {
                out[f] += sample;
            }
        }
    }
    
    /* Apply master volume and soft clipping */
    for (size_t i = 0; i < total_samples; i++) {
        out[i] *= mixer->master_volume;
        out[i] = soft_clip(out[i]);
    }
}
