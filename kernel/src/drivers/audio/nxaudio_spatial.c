/*
 * NXAudio Spatial Audio Engine
 * Public Brand: Ketivee Oohm
 * 
 * Implementation of 3D spatial audio processing with:
 *   - HRTF (Head-Related Transfer Function) for binaural 3D
 *   - Multi-channel mixing (stereo, 5.1, 7.1)
 *   - Echo/Reverb effects
 *   - Dynamic audio balancing
 *   - EQ processing
 * 
 * "NeolyxOS Audio Stack — Ketivee Oohm"
 * 
 * ARCHITECTURE NOTE:
 * Current (v1): DSP in kernel is acceptable for boot chime and basic spatial.
 * Future: Move DSP/spatial to nxaudio-server (user-space), keep kernel as
 * transport only (DMA, IRQ, hardware registers). This follows macOS/Windows
 * evolution pattern.
 * 
 * FLOATING POINT WARNING:
 * This module uses float for v1 simplicity. For long-term kernel usage,
 * migrate to fixed-point or move DSP to user-space server.
 * 
 * Copyright (c) 2025 Ketivee Oohm / KetiveeAI.
 */

#include "drivers/nxaudio.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);

/* ============ Global State ============ */

static nxaudio_spatial_context_t nxaudio_spatial_ctx;
static int nxaudio_spatial_initialized = 0;

/* Ketivee Oohm branding */
static const char *NXAUDIO_SPATIAL_VERSION = "1.0.0";
static const char *NXAUDIO_SPATIAL_BRAND = "Sound: Ketivee Oohm";

/* ============ Math Helpers ============ */

/*
 * Fixed-point sine approximation.
 * NOTE: Low-precision approximation (~8-bit effective resolution).
 * Suitable for spatial panning and basic LFO, NOT for high-frequency
 * modulation or precision synthesis.
 */
static int32_t nxaudio_spatial_sin(int32_t angle) {
    /* angle: 0-1024 = 0-2π */
    /* Returns -32767 to 32767 */
    angle = angle & 1023;
    if (angle > 512) angle = 1024 - angle;
    if (angle > 256) angle = 512 - angle;
    return (angle * 32767) / 256;
}

static int32_t nxaudio_spatial_cos(int32_t angle) {
    return nxaudio_spatial_sin(angle + 256);
}

/* Square root approximation */
static float nxaudio_spatial_sqrt(float x) {
    if (x <= 0) return 0;
    float guess = x / 2;
    for (int i = 0; i < 8; i++) {
        guess = (guess + x / guess) / 2;
    }
    return guess;
}

/* ============ HRTF Database ============ */

/*
 * Simplified HRTF coefficients for 8 azimuth angles.
 * This is an MVP choice - 8 angles is sufficient for v1 spatial positioning.
 * 
 * DO NOT expand to full CIPIC/KEMAR datasets yet:
 *   - Full datasets are megabytes
 *   - Causes cache pressure in kernel
 *   - Diminishing returns for non-audiophile use
 * 
 * When more precision is needed, move to user-space nxaudio-server.
 */
static nxaudio_spatial_hrtf_t default_hrtf[8] = {
    /* Front (0°) */
    {{ 32767, 32000, 30000, 28000, 25000, 22000, 19000, 16000 }, 
     { 32767, 32000, 30000, 28000, 25000, 22000, 19000, 16000 }, 0, 0},
    
    /* Front-Right (45°) */
    {{ 32767, 31000, 28000, 24000, 20000, 16000, 12000, 8000 },
     { 30000, 32767, 31000, 28000, 24000, 20000, 16000, 12000 }, 45, 0},
    
    /* Right (90°) */
    {{ 24000, 22000, 18000, 14000, 10000, 7000, 4000, 2000 },
     { 32767, 32000, 30000, 28000, 25000, 22000, 19000, 16000 }, 90, 0},
    
    /* Rear-Right (135°) */
    {{ 20000, 18000, 15000, 12000, 9000, 6000, 4000, 2000 },
     { 28000, 30000, 32767, 30000, 26000, 22000, 18000, 14000 }, 135, 0},
    
    /* Rear (180°) */
    {{ 22000, 20000, 17000, 14000, 11000, 8000, 5000, 3000 },
     { 22000, 20000, 17000, 14000, 11000, 8000, 5000, 3000 }, 180, 0},
    
    /* Rear-Left (225°) */
    {{ 28000, 30000, 32767, 30000, 26000, 22000, 18000, 14000 },
     { 20000, 18000, 15000, 12000, 9000, 6000, 4000, 2000 }, 225, 0},
    
    /* Left (270°) */
    {{ 32767, 32000, 30000, 28000, 25000, 22000, 19000, 16000 },
     { 24000, 22000, 18000, 14000, 10000, 7000, 4000, 2000 }, 270, 0},
    
    /* Front-Left (315°) */
    {{ 30000, 32767, 31000, 28000, 24000, 20000, 16000, 12000 },
     { 32767, 31000, 28000, 24000, 20000, 16000, 12000, 8000 }, 315, 0},
};

/* ============ Reverb Delay Buffer ============ */

/*
 * Single delay line reverb - provides room ambience, not cathedral reverb.
 * This is intentionally scoped: kernel audio should never chase realism.
 * For complex reverb (Schroeder, convolution), use user-space nxaudio-server.
 */
#define REVERB_BUFFER_SIZE 4800  /* 100ms at 48kHz */
static int16_t reverb_buffer_l[REVERB_BUFFER_SIZE];
static int16_t reverb_buffer_r[REVERB_BUFFER_SIZE];
static uint32_t reverb_write_pos = 0;

/* ============ Initialization ============ */

int nxaudio_spatial_init(void) {
    if (nxaudio_spatial_initialized) return 0;
    
    serial_puts("[Ketivee Oohm] Initializing spatial audio engine\n");
    
    /* Clear context */
    for (int i = 0; i < (int)sizeof(nxaudio_spatial_ctx); i++) {
        ((uint8_t*)&nxaudio_spatial_ctx)[i] = 0;
    }
    
    /* Initialize listener at origin, facing forward */
    nxaudio_spatial_ctx.listener.position.x = 0;
    nxaudio_spatial_ctx.listener.position.y = 0;
    nxaudio_spatial_ctx.listener.position.z = 0;
    nxaudio_spatial_ctx.listener.forward.x = 0;
    nxaudio_spatial_ctx.listener.forward.y = 0;
    nxaudio_spatial_ctx.listener.forward.z = 1;
    nxaudio_spatial_ctx.listener.up.x = 0;
    nxaudio_spatial_ctx.listener.up.y = 1;
    nxaudio_spatial_ctx.listener.up.z = 0;
    
    /* Default reverb settings (small room) */
    nxaudio_spatial_ctx.reverb.delay_ms = 50;
    nxaudio_spatial_ctx.reverb.decay = 0.3f;
    nxaudio_spatial_ctx.reverb.wet_mix = 0.2f;
    nxaudio_spatial_ctx.reverb.room_size = 0.5f;
    nxaudio_spatial_ctx.reverb.damping = 0.5f;
    nxaudio_spatial_ctx.reverb.enabled = 0;
    
    /* Default EQ (flat) */
    nxaudio_spatial_ctx.eq.bass = 0;
    nxaudio_spatial_ctx.eq.mid = 0;
    nxaudio_spatial_ctx.eq.treble = 0;
    nxaudio_spatial_ctx.eq.presence = 0;
    
    /* Default balance (centered) */
    nxaudio_spatial_ctx.balance.left_right = 0;
    nxaudio_spatial_ctx.balance.front_rear = 0;
    nxaudio_spatial_ctx.balance.center_level = 1.0f;
    nxaudio_spatial_ctx.balance.subwoofer_level = 1.0f;
    
    /* Set up HRTF database */
    nxaudio_spatial_ctx.hrtf_data = default_hrtf;
    nxaudio_spatial_ctx.hrtf_count = 8;
    
    /* Clear reverb buffers */
    for (int i = 0; i < REVERB_BUFFER_SIZE; i++) {
        reverb_buffer_l[i] = 0;
        reverb_buffer_r[i] = 0;
    }
    
    nxaudio_spatial_ctx.enabled = 1;
    nxaudio_spatial_ctx.surround_mode = 0;  /* Stereo */
    nxaudio_spatial_ctx.master_gain = 1.0f;
    
    nxaudio_spatial_initialized = 1;
    
    serial_puts("[Ketivee Oohm] Spatial audio ready\n");
    serial_puts("[Ketivee Oohm] ");
    serial_puts(NXAUDIO_SPATIAL_BRAND);
    serial_puts("\n");
    
    return 0;
}

nxaudio_spatial_context_t *nxaudio_spatial_get_context(void) {
    return &nxaudio_spatial_ctx;
}

void nxaudio_spatial_set_enabled(int enabled) {
    nxaudio_spatial_ctx.enabled = enabled;
}

/* ============ Source Management ============ */

int nxaudio_spatial_create_source(void) {
    if (nxaudio_spatial_ctx.source_count >= NXAUDIO_SPATIAL_MAX_SOURCES) {
        return -1;
    }
    
    int id = nxaudio_spatial_ctx.source_count;
    nxaudio_spatial_source_t *src = &nxaudio_spatial_ctx.sources[id];
    
    src->id = id;
    src->position.x = 0;
    src->position.y = 0;
    src->position.z = 1;  /* Front */
    src->volume = 1.0f;
    src->pitch = 1.0f;
    src->distance = 1.0f;
    src->rolloff = 1.0f;
    src->active = 0;
    
    nxaudio_spatial_ctx.source_count++;
    return id;
}

void nxaudio_spatial_destroy_source(int source_id) {
    if (source_id < 0 || source_id >= nxaudio_spatial_ctx.source_count) return;
    nxaudio_spatial_ctx.sources[source_id].active = 0;
}

void nxaudio_spatial_set_source_position(int source_id, float x, float y, float z) {
    if (source_id < 0 || source_id >= nxaudio_spatial_ctx.source_count) return;
    
    nxaudio_spatial_ctx.sources[source_id].position.x = x;
    nxaudio_spatial_ctx.sources[source_id].position.y = y;
    nxaudio_spatial_ctx.sources[source_id].position.z = z;
    
    /* Calculate distance from listener */
    float dx = x - nxaudio_spatial_ctx.listener.position.x;
    float dy = y - nxaudio_spatial_ctx.listener.position.y;
    float dz = z - nxaudio_spatial_ctx.listener.position.z;
    nxaudio_spatial_ctx.sources[source_id].distance = nxaudio_spatial_sqrt(dx*dx + dy*dy + dz*dz);
}

void nxaudio_spatial_set_source_volume(int source_id, float volume) {
    if (source_id < 0 || source_id >= nxaudio_spatial_ctx.source_count) return;
    nxaudio_spatial_ctx.sources[source_id].volume = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
}

int nxaudio_spatial_source_play(int source_id, const void *data, uint32_t len,
                     uint32_t sample_rate, audio_format_t format) {
    if (source_id < 0 || source_id >= nxaudio_spatial_ctx.source_count) return -1;
    (void)data; (void)len; (void)sample_rate; (void)format;
    
    nxaudio_spatial_ctx.sources[source_id].active = 1;
    
    /* In full implementation, would queue audio data for mixing */
    return 0;
}

/* ============ Listener ============ */

void nxaudio_spatial_set_listener_position(float x, float y, float z) {
    nxaudio_spatial_ctx.listener.position.x = x;
    nxaudio_spatial_ctx.listener.position.y = y;
    nxaudio_spatial_ctx.listener.position.z = z;
}

void nxaudio_spatial_set_listener_orientation(float fx, float fy, float fz,
                                    float ux, float uy, float uz) {
    nxaudio_spatial_ctx.listener.forward.x = fx;
    nxaudio_spatial_ctx.listener.forward.y = fy;
    nxaudio_spatial_ctx.listener.forward.z = fz;
    nxaudio_spatial_ctx.listener.up.x = ux;
    nxaudio_spatial_ctx.listener.up.y = uy;
    nxaudio_spatial_ctx.listener.up.z = uz;
}

/* ============ Effects ============ */

void nxaudio_spatial_set_reverb(float delay_ms, float decay, float wet_mix) {
    nxaudio_spatial_ctx.reverb.delay_ms = delay_ms;
    nxaudio_spatial_ctx.reverb.decay = decay < 0 ? 0 : (decay > 1 ? 1 : decay);
    nxaudio_spatial_ctx.reverb.wet_mix = wet_mix < 0 ? 0 : (wet_mix > 1 ? 1 : wet_mix);
}

void nxaudio_spatial_enable_reverb(int enabled) {
    nxaudio_spatial_ctx.reverb.enabled = enabled;
}

void nxaudio_spatial_set_eq(float bass, float mid, float treble) {
    nxaudio_spatial_ctx.eq.bass = bass;
    nxaudio_spatial_ctx.eq.mid = mid;
    nxaudio_spatial_ctx.eq.treble = treble;
}

/* ============ Audio Balancing ============ */

void nxaudio_spatial_set_balance(float balance) {
    nxaudio_spatial_ctx.balance.left_right = balance < -1 ? -1 : (balance > 1 ? 1 : balance);
}

void nxaudio_spatial_set_fade(float fade) {
    nxaudio_spatial_ctx.balance.front_rear = fade < -1 ? -1 : (fade > 1 ? 1 : fade);
}

void nxaudio_spatial_set_surround_mode(int mode) {
    if (mode >= 0 && mode <= 2) {
        nxaudio_spatial_ctx.surround_mode = mode;
    }
}

/* ============ HRTF Processing ============ */

void nxaudio_spatial_apply_hrtf(int16_t *buffer, uint32_t samples,
                     float azimuth, float elevation) {
    if (!nxaudio_spatial_ctx.enabled || !buffer) return;
    (void)elevation;  /* Simplified: ignore elevation for now */
    
    /* Find closest HRTF entry */
    int best_idx = 0;
    float best_diff = 360;
    
    for (int i = 0; i < nxaudio_spatial_ctx.hrtf_count; i++) {
        float diff = azimuth - nxaudio_spatial_ctx.hrtf_data[i].azimuth;
        if (diff < 0) diff = -diff;
        if (diff > 180) diff = 360 - diff;
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    
    nxaudio_spatial_hrtf_t *hrtf = &nxaudio_spatial_ctx.hrtf_data[best_idx];
    
    /* Apply HRTF filter (simplified convolution) */
    for (uint32_t i = 0; i < samples; i += 2) {
        int32_t left = buffer[i];
        int32_t right = buffer[i + 1];
        
        /* Apply HRTF coefficients (simplified) */
        int fidx = (i / 2) & 7;  /* Use first 8 coefficients */
        left = (left * hrtf->left_ir[fidx]) >> 15;
        right = (right * hrtf->right_ir[fidx]) >> 15;
        
        /* Apply balance */
        if (nxaudio_spatial_ctx.balance.left_right < 0) {
            right = (int32_t)(right * (1.0f + nxaudio_spatial_ctx.balance.left_right));
        } else if (nxaudio_spatial_ctx.balance.left_right > 0) {
            left = (int32_t)(left * (1.0f - nxaudio_spatial_ctx.balance.left_right));
        }
        
        buffer[i] = (int16_t)(left > 32767 ? 32767 : (left < -32767 ? -32767 : left));
        buffer[i + 1] = (int16_t)(right > 32767 ? 32767 : (right < -32767 ? -32767 : right));
    }
    
    /* Apply reverb if enabled */
    if (nxaudio_spatial_ctx.reverb.enabled) {
        uint32_t delay_samples = (uint32_t)(nxaudio_spatial_ctx.reverb.delay_ms * 48);  /* 48kHz */
        if (delay_samples > REVERB_BUFFER_SIZE) delay_samples = REVERB_BUFFER_SIZE;
        
        for (uint32_t i = 0; i < samples; i += 2) {
            uint32_t read_pos = (reverb_write_pos + REVERB_BUFFER_SIZE - delay_samples) 
                                % REVERB_BUFFER_SIZE;
            
            /* Read delayed signal */
            int32_t delayed_l = reverb_buffer_l[read_pos];
            int32_t delayed_r = reverb_buffer_r[read_pos];
            
            /* Mix with current signal */
            int32_t wet = (int32_t)(nxaudio_spatial_ctx.reverb.wet_mix * 32767);
            int32_t dry = 32767 - wet;
            
            int32_t out_l = ((buffer[i] * dry) + (delayed_l * wet)) >> 15;
            int32_t out_r = ((buffer[i + 1] * dry) + (delayed_r * wet)) >> 15;
            
            buffer[i] = (int16_t)(out_l > 32767 ? 32767 : (out_l < -32767 ? -32767 : out_l));
            buffer[i + 1] = (int16_t)(out_r > 32767 ? 32767 : (out_r < -32767 ? -32767 : out_r));
            
            /* Write to delay buffer with decay */
            int32_t decay = (int32_t)(nxaudio_spatial_ctx.reverb.decay * 32767);
            reverb_buffer_l[reverb_write_pos] = (int16_t)((buffer[i] * decay) >> 15);
            reverb_buffer_r[reverb_write_pos] = (int16_t)((buffer[i + 1] * decay) >> 15);
            
            reverb_write_pos = (reverb_write_pos + 1) % REVERB_BUFFER_SIZE;
        }
    }
}

int nxaudio_spatial_mix_3d_sources(int16_t *output, uint32_t samples) {
    if (!nxaudio_spatial_ctx.enabled || !output) return -1;
    
    /* Clear output buffer */
    for (uint32_t i = 0; i < samples * 2; i++) {
        output[i] = 0;
    }
    
    int mixed = 0;
    
    /* Mix each active source */
    for (int s = 0; s < nxaudio_spatial_ctx.source_count; s++) {
        nxaudio_spatial_source_t *src = &nxaudio_spatial_ctx.sources[s];
        if (!src->active) continue;
        
        /* Calculate azimuth from source position relative to listener */
        float dx = src->position.x - nxaudio_spatial_ctx.listener.position.x;
        float dz = src->position.z - nxaudio_spatial_ctx.listener.position.z;
        
        /* Simple angle calculation (atan2 approximation) */
        float azimuth = 0;
        if (dz != 0) {
            float ratio = dx / dz;
            if (ratio < 0) ratio = -ratio;
            azimuth = ratio * 45;  /* Simplified */
            if (dx < 0) azimuth = -azimuth;
            if (dz < 0) azimuth = 180 - azimuth;
        } else {
            azimuth = dx > 0 ? 90 : 270;
        }
        if (azimuth < 0) azimuth += 360;
        
        /* Apply distance attenuation */
        float attenuation = 1.0f / (1.0f + src->distance * src->rolloff);
        float gain = src->volume * attenuation * nxaudio_spatial_ctx.master_gain;
        
        /* Source audio would be mixed here with HRTF */
        (void)gain;
        (void)azimuth;
        
        mixed++;
    }
    
    return mixed;
}

/* ============ Branding ============ */

const char *nxaudio_spatial_get_version(void) {
    return NXAUDIO_SPATIAL_VERSION;
}

const char *nxaudio_spatial_get_brand(void) {
    return NXAUDIO_SPATIAL_BRAND;
}
