/*
 * NXAudio Spatial Engine - HRTF Processing
 * 
 * Own spatial audio engine replacing Dolby/third-party:
 * - HRTF convolution for binaural audio
 * - Distance attenuation models
 * - Doppler effect
 * - 3D object mixing
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "nxaudio/nxaudio.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ============ Constants ============ */
#define HRTF_IR_LENGTH      256     /* Impulse response length */
#define HRTF_NUM_AZIMUTHS   72      /* 5° steps */
#define HRTF_NUM_ELEVATIONS 25      /* -90° to 90° in 7.5° steps */
#define PI                  3.14159265358979323846f

/* ============ HRTF Filter ============ */
typedef struct {
    float left[HRTF_IR_LENGTH];
    float right[HRTF_IR_LENGTH];
} hrtf_filter_t;

/* ============ HRTF Dataset ============ */
typedef struct {
    hrtf_filter_t filters[HRTF_NUM_ELEVATIONS][HRTF_NUM_AZIMUTHS];
    uint32_t sample_rate;
    int loaded;
} hrtf_dataset_t;

/* ============ Convolution State ============ */
typedef struct {
    float input_buffer[HRTF_IR_LENGTH];
    int buffer_pos;
} convolution_state_t;

/* ============ Spatial Processor ============ */
typedef struct {
    hrtf_dataset_t hrtf;
    convolution_state_t conv_left;
    convolution_state_t conv_right;
    float prev_azimuth;
    float prev_elevation;
    float smoothing_factor;
} spatial_processor_t;

/* ============ Math Helpers ============ */

static float clamp(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

static float vec3_length(const nxaudio_vec3_t *v) {
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static void vec3_normalize(nxaudio_vec3_t *v) {
    float len = vec3_length(v);
    if (len > 0.0001f) {
        v->x /= len;
        v->y /= len;
        v->z /= len;
    }
}

static float vec3_dot(const nxaudio_vec3_t *a, const nxaudio_vec3_t *b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

/* ============ Angle Calculation ============ */

/**
 * Calculate azimuth (horizontal angle) from source relative to listener
 * Returns angle in radians: -PI to PI (left to right)
 */
static float calculate_azimuth(const nxaudio_vec3_t *listener_pos,
                                const nxaudio_vec3_t *source_pos) {
    float dx = source_pos->x - listener_pos->x;
    float dz = source_pos->z - listener_pos->z;
    
    return atan2f(dx, -dz);
}

/**
 * Calculate elevation (vertical angle) from source relative to listener
 * Returns angle in radians: -PI/2 to PI/2 (below to above)
 */
static float calculate_elevation(const nxaudio_vec3_t *listener_pos,
                                  const nxaudio_vec3_t *source_pos) {
    float dx = source_pos->x - listener_pos->x;
    float dy = source_pos->y - listener_pos->y;
    float dz = source_pos->z - listener_pos->z;
    
    float horizontal_dist = sqrtf(dx * dx + dz * dz);
    
    if (horizontal_dist < 0.0001f && fabsf(dy) < 0.0001f) {
        return 0.0f;
    }
    
    return atan2f(dy, horizontal_dist);
}

/* ============ Distance Attenuation ============ */

/**
 * Calculate gain based on distance and attenuation model
 */
float spatial_distance_attenuation(float distance,
                                    float min_dist,
                                    float max_dist,
                                    float rolloff,
                                    nxaudio_distance_model_t model) {
    if (distance <= min_dist) {
        return 1.0f;
    }
    
    if (distance >= max_dist) {
        return 0.0f;
    }
    
    float gain = 1.0f;
    
    switch (model) {
        case NXAUDIO_DISTANCE_INVERSE:
            /* Inverse distance: 1 / (1 + rolloff * (d - min) / min) */
            gain = min_dist / (min_dist + rolloff * (distance - min_dist));
            break;
            
        case NXAUDIO_DISTANCE_LINEAR:
            /* Linear falloff */
            gain = 1.0f - rolloff * (distance - min_dist) / (max_dist - min_dist);
            break;
            
        case NXAUDIO_DISTANCE_EXPONENTIAL:
            /* Exponential falloff */
            gain = powf(distance / min_dist, -rolloff);
            break;
            
        case NXAUDIO_DISTANCE_NONE:
        default:
            gain = 1.0f;
            break;
    }
    
    return clamp(gain, 0.0f, 1.0f);
}

/* ============ Doppler Effect ============ */

/**
 * Calculate pitch shift due to Doppler effect
 */
float spatial_doppler_shift(const nxaudio_vec3_t *listener_pos,
                             const nxaudio_vec3_t *listener_vel,
                             const nxaudio_vec3_t *source_pos,
                             const nxaudio_vec3_t *source_vel,
                             float speed_of_sound,
                             float doppler_factor) {
    if (doppler_factor <= 0.0f) {
        return 1.0f;
    }
    
    /* Direction from source to listener */
    nxaudio_vec3_t dir = {
        listener_pos->x - source_pos->x,
        listener_pos->y - source_pos->y,
        listener_pos->z - source_pos->z
    };
    vec3_normalize(&dir);
    
    /* Relative velocity components along direction */
    float v_listener = vec3_dot(listener_vel, &dir);
    float v_source = vec3_dot(source_vel, &dir);
    
    /* Doppler formula */
    float c = speed_of_sound;
    
    /* Clamp velocities to prevent singularities */
    v_listener = clamp(v_listener, -c * 0.9f, c * 0.9f);
    v_source = clamp(v_source, -c * 0.9f, c * 0.9f);
    
    float shift = (c + v_listener * doppler_factor) / 
                  (c + v_source * doppler_factor);
    
    return clamp(shift, 0.5f, 2.0f);
}

/* ============ HRTF Interpolation ============ */

/**
 * Get HRTF filter indices for given angles
 */
static void get_hrtf_indices(float azimuth_rad, float elevation_rad,
                              int *az_idx, int *el_idx) {
    /* Convert to degrees */
    float azimuth_deg = azimuth_rad * 180.0f / PI;
    float elevation_deg = elevation_rad * 180.0f / PI;
    
    /* Wrap azimuth to 0-360 */
    while (azimuth_deg < 0) azimuth_deg += 360.0f;
    while (azimuth_deg >= 360.0f) azimuth_deg -= 360.0f;
    
    /* Map to indices */
    *az_idx = (int)(azimuth_deg / 5.0f + 0.5f) % HRTF_NUM_AZIMUTHS;
    
    /* Clamp elevation to -90 to 90 */
    elevation_deg = clamp(elevation_deg, -90.0f, 90.0f);
    *el_idx = (int)((elevation_deg + 90.0f) / 7.5f + 0.5f);
    *el_idx = clamp(*el_idx, 0, HRTF_NUM_ELEVATIONS - 1);
}

/* ============ Convolution ============ */

/**
 * Apply HRTF convolution to mono sample
 * Outputs stereo (left, right)
 */
void spatial_hrtf_process(spatial_processor_t *proc,
                           float input_sample,
                           float azimuth_rad,
                           float elevation_rad,
                           float *out_left,
                           float *out_right) {
    /* Get filter indices */
    int az_idx, el_idx;
    get_hrtf_indices(azimuth_rad, elevation_rad, &az_idx, &el_idx);
    
    /* Get filter (or use placeholder if not loaded) */
    const float *left_ir;
    const float *right_ir;
    
    if (proc->hrtf.loaded) {
        left_ir = proc->hrtf.filters[el_idx][az_idx].left;
        right_ir = proc->hrtf.filters[el_idx][az_idx].right;
    } else {
        /* Simple stereo panning fallback */
        float pan = sinf(azimuth_rad);
        *out_left = input_sample * (1.0f - pan) * 0.5f;
        *out_right = input_sample * (1.0f + pan) * 0.5f;
        return;
    }
    
    /* Add sample to input buffer */
    proc->conv_left.input_buffer[proc->conv_left.buffer_pos] = input_sample;
    proc->conv_right.input_buffer[proc->conv_right.buffer_pos] = input_sample;
    
    /* Convolve */
    float sum_left = 0.0f;
    float sum_right = 0.0f;
    
    int pos = proc->conv_left.buffer_pos;
    for (int i = 0; i < HRTF_IR_LENGTH; i++) {
        sum_left += proc->conv_left.input_buffer[pos] * left_ir[i];
        sum_right += proc->conv_right.input_buffer[pos] * right_ir[i];
        
        pos--;
        if (pos < 0) pos = HRTF_IR_LENGTH - 1;
    }
    
    /* Update buffer position */
    proc->conv_left.buffer_pos = (proc->conv_left.buffer_pos + 1) % HRTF_IR_LENGTH;
    proc->conv_right.buffer_pos = proc->conv_left.buffer_pos;
    
    *out_left = sum_left;
    *out_right = sum_right;
}

/* ============ Public Interface ============ */

/**
 * Initialize spatial processor
 */
spatial_processor_t* spatial_processor_create(void) {
    spatial_processor_t *proc = (spatial_processor_t*)calloc(1, sizeof(spatial_processor_t));
    if (!proc) return NULL;
    
    proc->smoothing_factor = 0.1f;
    proc->hrtf.loaded = 0;
    
    return proc;
}

/**
 * Destroy spatial processor
 */
void spatial_processor_destroy(spatial_processor_t *proc) {
    if (proc) free(proc);
}

/**
 * Load HRTF from built-in defaults
 */
int spatial_load_default_hrtf(spatial_processor_t *proc) {
    if (!proc) return -1;
    
    /* Generate simple synthetic HRTF for now */
    /* Real implementation would load from SOFA file */
    
    for (int el = 0; el < HRTF_NUM_ELEVATIONS; el++) {
        for (int az = 0; az < HRTF_NUM_AZIMUTHS; az++) {
            hrtf_filter_t *filter = &proc->hrtf.filters[el][az];
            
            /* Simple delay-based HRTF approximation */
            float azimuth_rad = (float)az * 5.0f * PI / 180.0f;
            
            /* ITD (Interaural Time Difference) */
            float itd_samples = 10.0f * sinf(azimuth_rad);
            
            /* ILD (Interaural Level Difference) */
            float ild = 0.3f * sinf(azimuth_rad);
            
            /* Create simple lowpass filter for each ear */
            for (int i = 0; i < HRTF_IR_LENGTH; i++) {
                float t = (float)i / HRTF_IR_LENGTH;
                float decay = expf(-t * 8.0f);
                
                /* Left ear */
                int left_delay = (int)(HRTF_IR_LENGTH / 4 + itd_samples);
                if (i == left_delay) {
                    filter->left[i] = (1.0f - ild) * decay;
                } else {
                    filter->left[i] = 0.0f;
                }
                
                /* Right ear */
                int right_delay = (int)(HRTF_IR_LENGTH / 4 - itd_samples);
                if (i == right_delay) {
                    filter->right[i] = (1.0f + ild) * decay;
                } else {
                    filter->right[i] = 0.0f;
                }
            }
        }
    }
    
    proc->hrtf.sample_rate = 48000;
    proc->hrtf.loaded = 1;
    
    return 0;
}

/**
 * Process audio buffer with 3D spatialization
 */
void spatial_process_buffer(spatial_processor_t *proc,
                             const float *input_mono,
                             float *output_stereo,
                             size_t num_frames,
                             const nxaudio_vec3_t *listener_pos,
                             const nxaudio_vec3_t *source_pos,
                             float gain) {
    if (!proc || !input_mono || !output_stereo) return;
    
    float azimuth = calculate_azimuth(listener_pos, source_pos);
    float elevation = calculate_elevation(listener_pos, source_pos);
    
    for (size_t i = 0; i < num_frames; i++) {
        float in_sample = input_mono[i] * gain;
        float out_left, out_right;
        
        spatial_hrtf_process(proc, in_sample, azimuth, elevation,
                             &out_left, &out_right);
        
        output_stereo[i * 2] = out_left;
        output_stereo[i * 2 + 1] = out_right;
    }
}
