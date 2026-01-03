/* Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

/*
 * NeolyxSpatial - 3D Audio Engine Implementation
 */

#include "nx_spatial.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846f
#define SPEED_OF_SOUND 343.0f

/* Math helpers */
static float vec3_dot(nx_vec3_t a, nx_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3_length(nx_vec3_t v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static nx_vec3_t vec3_normalize(nx_vec3_t v) {
    float len = vec3_length(v);
    if (len > 0.0001f) {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
    return v;
}

static nx_vec3_t vec3_sub(nx_vec3_t a, nx_vec3_t b) {
    return (nx_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static nx_vec3_t vec3_cross(nx_vec3_t a, nx_vec3_t b) {
    return (nx_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

/* Calculate azimuth and elevation from listener to source */
static void calc_angles(const nx_listener_t *L, nx_vec3_t src_pos,
                        float *azimuth, float *elevation) {
    nx_vec3_t dir = vec3_sub(src_pos, L->position);
    dir = vec3_normalize(dir);
    
    nx_vec3_t right = vec3_cross(L->forward, L->up);
    right = vec3_normalize(right);
    
    float front = vec3_dot(dir, L->forward);
    float side = vec3_dot(dir, right);
    float up = vec3_dot(dir, L->up);
    
    *azimuth = atan2f(side, front) * 180.0f / PI;
    *elevation = asinf(up) * 180.0f / PI;
}

/* Distance attenuation */
static float calc_distance_attenuation(nx_spatial_t *sp, float distance, 
                                        float min_dist, float max_dist, float rolloff) {
    if (distance <= min_dist) return 1.0f;
    if (distance >= max_dist) return 0.0f;
    
    float d = (distance - min_dist) / (max_dist - min_dist);
    
    switch (sp->distance_model) {
        case NX_DIST_INVERSE:
            return min_dist / (min_dist + rolloff * (distance - min_dist));
        case NX_DIST_LINEAR:
            return 1.0f - rolloff * d;
        case NX_DIST_EXPONENTIAL:
            return powf(distance / min_dist, -rolloff);
        default:
            return 1.0f;
    }
}

/* Doppler effect */
static float calc_doppler(nx_spatial_t *sp, nx_vec3_t src_pos, nx_vec3_t src_vel) {
    if (sp->doppler_factor <= 0.0f) return 1.0f;
    
    nx_vec3_t dir = vec3_sub(src_pos, sp->listener.position);
    float dist = vec3_length(dir);
    if (dist < 0.001f) return 1.0f;
    
    dir = vec3_normalize(dir);
    
    float v_listener = vec3_dot(sp->listener.velocity, dir);
    float v_source = vec3_dot(src_vel, dir);
    
    float c = sp->speed_of_sound;
    float shift = (c - sp->doppler_factor * v_listener) / 
                  (c - sp->doppler_factor * v_source);
    
    if (shift < 0.5f) shift = 0.5f;
    if (shift > 2.0f) shift = 2.0f;
    
    return shift;
}

/* Get HRTF index from angles */
static int get_hrtf_index(nx_hrtf_t *hrtf, float azimuth, float elevation) {
    if (!hrtf) return 0;
    
    /* Find closest elevation */
    int elev_idx = 0;
    float min_elev_diff = fabsf(hrtf->elevations[0] - elevation);
    for (uint32_t i = 1; i < hrtf->num_elevations; i++) {
        float diff = fabsf(hrtf->elevations[i] - elevation);
        if (diff < min_elev_diff) {
            min_elev_diff = diff;
            elev_idx = i;
        }
    }
    
    /* Find closest azimuth */
    int azim_idx = 0;
    float min_azim_diff = fabsf(hrtf->azimuths[0] - azimuth);
    for (uint32_t i = 1; i < hrtf->num_azimuths; i++) {
        float diff = fabsf(hrtf->azimuths[i] - azimuth);
        if (diff < min_azim_diff) {
            min_azim_diff = diff;
            azim_idx = i;
        }
    }
    
    return elev_idx * hrtf->num_azimuths + azim_idx;
}

/* FIR convolution (time-domain, for short kernels) */
static void fir_convolve(const float *in, size_t in_len,
                          const float *kernel, size_t kernel_len,
                          float *out, float *overlap) {
    size_t i, j;
    
    /* Process with overlap-add */
    for (i = 0; i < in_len; i++) {
        float sum = 0.0f;
        
        /* Convolve with kernel */
        for (j = 0; j < kernel_len; j++) {
            if (i >= j) {
                sum += in[i - j] * kernel[j];
            } else {
                /* Use overlap from previous block */
                size_t ovl_idx = kernel_len - 1 - j + i;
                if (ovl_idx < kernel_len - 1) {
                    sum += overlap[ovl_idx] * kernel[j];
                }
            }
        }
        
        out[i] = sum;
    }
    
    /* Save overlap for next block */
    if (in_len >= kernel_len - 1) {
        memcpy(overlap, in + in_len - (kernel_len - 1), 
               (kernel_len - 1) * sizeof(float));
    } else {
        memmove(overlap, overlap + in_len, 
                (kernel_len - 1 - in_len) * sizeof(float));
        memcpy(overlap + kernel_len - 1 - in_len, in, 
               in_len * sizeof(float));
    }
}

/* Creation/destruction */

nx_spatial_t* nx_spatial_create(uint32_t sample_rate, uint32_t block_size) {
    nx_spatial_t *sp = (nx_spatial_t*)calloc(1, sizeof(nx_spatial_t));
    if (!sp) return NULL;
    
    sp->sample_rate = sample_rate;
    sp->channels = 2;
    sp->block_size = block_size;
    
    /* Default listener */
    sp->listener.position = (nx_vec3_t){0, 0, 0};
    sp->listener.forward = (nx_vec3_t){0, 0, -1};
    sp->listener.up = (nx_vec3_t){0, 1, 0};
    sp->listener.velocity = (nx_vec3_t){0, 0, 0};
    sp->listener.gain = 1.0f;
    
    /* Default settings */
    sp->distance_model = NX_DIST_INVERSE;
    sp->speed_of_sound = SPEED_OF_SOUND;
    sp->doppler_factor = 1.0f;
    sp->reverb_mix = 0.0f;
    
    /* Work buffers */
    sp->work_size = block_size * 4;
    sp->work_buffer = (float*)calloc(sp->work_size, sizeof(float));
    
    printf("[SPATIAL] Created: %d Hz, block %d\n", sample_rate, block_size);
    
    return sp;
}

void nx_spatial_destroy(nx_spatial_t *sp) {
    if (!sp) return;
    
    /* Free object overlaps */
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS; i++) {
        if (sp->objects[i].overlap_l) free(sp->objects[i].overlap_l);
        if (sp->objects[i].overlap_r) free(sp->objects[i].overlap_r);
    }
    
    if (sp->work_buffer) free(sp->work_buffer);
    if (sp->fft_in) free(sp->fft_in);
    if (sp->fft_out) free(sp->fft_out);
    
    nx_spatial_unload_hrtf(sp);
    
    free(sp);
}

/* HRTF management */

int nx_spatial_load_hrtf_default(nx_spatial_t *sp) {
    if (!sp) return NX_SPATIAL_ERR_INVALID;
    
    nx_hrtf_t *hrtf = (nx_hrtf_t*)calloc(1, sizeof(nx_hrtf_t));
    if (!hrtf) return NX_SPATIAL_ERR_MEMORY;
    
    strcpy(hrtf->name, "Default Synthetic HRTF");
    hrtf->sample_rate = sp->sample_rate;
    hrtf->filter_length = NX_HRTF_LEN_SHORT;
    hrtf->num_elevations = 7;
    hrtf->num_azimuths = 24;
    hrtf->num_positions = hrtf->num_elevations * hrtf->num_azimuths;
    
    /* Allocate */
    size_t filter_size = hrtf->num_positions * hrtf->filter_length;
    hrtf->filters_l = (float*)calloc(filter_size, sizeof(float));
    hrtf->filters_r = (float*)calloc(filter_size, sizeof(float));
    hrtf->elevations = (float*)calloc(hrtf->num_elevations, sizeof(float));
    hrtf->azimuths = (float*)calloc(hrtf->num_azimuths, sizeof(float));
    
    if (!hrtf->filters_l || !hrtf->filters_r) {
        free(hrtf);
        return NX_SPATIAL_ERR_MEMORY;
    }
    
    /* Setup elevation angles */
    float elev_values[] = {-40.0f, -20.0f, 0.0f, 20.0f, 40.0f, 60.0f, 80.0f};
    for (uint32_t i = 0; i < hrtf->num_elevations; i++) {
        hrtf->elevations[i] = elev_values[i];
    }
    
    /* Setup azimuth angles */
    for (uint32_t i = 0; i < hrtf->num_azimuths; i++) {
        hrtf->azimuths[i] = (float)i * 360.0f / hrtf->num_azimuths - 180.0f;
    }
    
    /* Generate synthetic HRTF filters */
    /* Using simplified head model with ITD and ILD */
    float head_radius = 0.0875f;  /* 8.75 cm */
    float c = (float)sp->sample_rate;
    
    for (uint32_t e = 0; e < hrtf->num_elevations; e++) {
        float elev_rad = hrtf->elevations[e] * PI / 180.0f;
        
        for (uint32_t a = 0; a < hrtf->num_azimuths; a++) {
            float azim_rad = hrtf->azimuths[a] * PI / 180.0f;
            
            size_t idx = (e * hrtf->num_azimuths + a) * hrtf->filter_length;
            
            /* Interaural time difference (ITD) */
            float itd = head_radius / SPEED_OF_SOUND * 
                        (sinf(azim_rad) + azim_rad);
            int itd_samples = (int)(fabsf(itd) * c);
            if (itd_samples >= (int)hrtf->filter_length) {
                itd_samples = hrtf->filter_length - 1;
            }
            
            /* Interaural level difference (ILD) */
            float ild_db = 10.0f * sinf(azim_rad) * cosf(elev_rad);
            float ild_l = (azim_rad < 0) ? powf(10.0f, ild_db / 20.0f) : 1.0f;
            float ild_r = (azim_rad > 0) ? powf(10.0f, -ild_db / 20.0f) : 1.0f;
            
            /* Create impulse with ITD */
            for (uint32_t s = 0; s < hrtf->filter_length; s++) {
                float t = (float)s / c;
                float env = expf(-t * 500.0f);  /* Decay */
                
                int delay_l = (azim_rad > 0) ? itd_samples : 0;
                int delay_r = (azim_rad < 0) ? itd_samples : 0;
                
                if ((int)s >= delay_l && s < delay_l + 8) {
                    float w = 0.5f * (1.0f - cosf(PI * (s - delay_l) / 8.0f));
                    hrtf->filters_l[idx + s] = ild_l * w * env;
                }
                if ((int)s >= delay_r && s < delay_r + 8) {
                    float w = 0.5f * (1.0f - cosf(PI * (s - delay_r) / 8.0f));
                    hrtf->filters_r[idx + s] = ild_r * w * env;
                }
            }
        }
    }
    
    sp->hrtf = hrtf;
    sp->hrtf_enabled = 1;
    
    printf("[SPATIAL] Loaded default HRTF: %d positions, %d taps\n",
           hrtf->num_positions, hrtf->filter_length);
    
    return NX_SPATIAL_OK;
}

int nx_spatial_load_hrtf(nx_spatial_t *sp, const char *filepath) {
    if (!sp || !filepath) return NX_SPATIAL_ERR_INVALID;
    
    /* SOFA file loading - check extension */
    size_t len = strlen(filepath);
    if (len > 5 && strcmp(filepath + len - 5, ".sofa") == 0) {
        /* TODO: Implement SOFA parser */
        printf("[SPATIAL] SOFA loading not yet implemented: %s\n", filepath);
        return nx_spatial_load_hrtf_default(sp);
    }
    
    /* Try raw binary format */
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("[SPATIAL] Cannot open HRTF file, using default\n");
        return nx_spatial_load_hrtf_default(sp);
    }
    
    /* Read header */
    uint32_t header[4];
    if (fread(header, sizeof(uint32_t), 4, f) != 4) {
        fclose(f);
        return nx_spatial_load_hrtf_default(sp);
    }
    
    /* Parse simple format: sample_rate, filter_len, elevations, azimuths */
    nx_hrtf_t *hrtf = (nx_hrtf_t*)calloc(1, sizeof(nx_hrtf_t));
    hrtf->sample_rate = header[0];
    hrtf->filter_length = header[1];
    hrtf->num_elevations = header[2];
    hrtf->num_azimuths = header[3];
    hrtf->num_positions = hrtf->num_elevations * hrtf->num_azimuths;
    
    size_t filter_size = hrtf->num_positions * hrtf->filter_length;
    hrtf->filters_l = (float*)malloc(filter_size * sizeof(float));
    hrtf->filters_r = (float*)malloc(filter_size * sizeof(float));
    
    if (fread(hrtf->filters_l, sizeof(float), filter_size, f) != filter_size ||
        fread(hrtf->filters_r, sizeof(float), filter_size, f) != filter_size) {
        free(hrtf->filters_l);
        free(hrtf->filters_r);
        free(hrtf);
        fclose(f);
        return nx_spatial_load_hrtf_default(sp);
    }
    
    fclose(f);
    
    strncpy(hrtf->name, filepath, sizeof(hrtf->name) - 1);
    sp->hrtf = hrtf;
    sp->hrtf_enabled = 1;
    
    printf("[SPATIAL] Loaded HRTF: %s\n", filepath);
    
    return NX_SPATIAL_OK;
}

void nx_spatial_unload_hrtf(nx_spatial_t *sp) {
    if (!sp || !sp->hrtf) return;
    
    if (sp->hrtf->filters_l) free(sp->hrtf->filters_l);
    if (sp->hrtf->filters_r) free(sp->hrtf->filters_r);
    if (sp->hrtf->fft_filters_l) free(sp->hrtf->fft_filters_l);
    if (sp->hrtf->fft_filters_r) free(sp->hrtf->fft_filters_r);
    if (sp->hrtf->elevations) free(sp->hrtf->elevations);
    if (sp->hrtf->azimuths) free(sp->hrtf->azimuths);
    
    free(sp->hrtf);
    sp->hrtf = NULL;
    sp->hrtf_enabled = 0;
}

void nx_spatial_enable_hrtf(nx_spatial_t *sp, int enable) {
    if (sp) sp->hrtf_enabled = enable && (sp->hrtf != NULL);
}

/* Listener control */

void nx_spatial_set_listener(nx_spatial_t *sp, const nx_listener_t *listener) {
    if (sp && listener) {
        sp->listener = *listener;
    }
}

void nx_spatial_set_listener_pos(nx_spatial_t *sp, float x, float y, float z) {
    if (sp) {
        sp->listener.position = (nx_vec3_t){x, y, z};
    }
}

void nx_spatial_set_listener_orientation(nx_spatial_t *sp,
                                          float fx, float fy, float fz,
                                          float ux, float uy, float uz) {
    if (sp) {
        sp->listener.forward = vec3_normalize((nx_vec3_t){fx, fy, fz});
        sp->listener.up = vec3_normalize((nx_vec3_t){ux, uy, uz});
    }
}

/* Object management */

int nx_spatial_add_object(nx_spatial_t *sp, int id, const nx_object_t *obj) {
    if (!sp || !obj) return -1;
    
    /* Find slot */
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS; i++) {
        if (!sp->objects[i].active) {
            sp->objects[i] = *obj;
            sp->objects[i].id = id;
            sp->objects[i].active = 1;
            
            /* Allocate overlap buffers */
            size_t ovl_size = sp->hrtf ? sp->hrtf->filter_length : NX_HRTF_LEN_SHORT;
            sp->objects[i].overlap_l = (float*)calloc(ovl_size, sizeof(float));
            sp->objects[i].overlap_r = (float*)calloc(ovl_size, sizeof(float));
            
            sp->num_objects++;
            return i;
        }
    }
    
    return -1;
}

void nx_spatial_remove_object(nx_spatial_t *sp, int id) {
    if (!sp) return;
    
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS; i++) {
        if (sp->objects[i].active && sp->objects[i].id == id) {
            if (sp->objects[i].overlap_l) free(sp->objects[i].overlap_l);
            if (sp->objects[i].overlap_r) free(sp->objects[i].overlap_r);
            memset(&sp->objects[i], 0, sizeof(nx_object_t));
            sp->num_objects--;
            break;
        }
    }
}

void nx_spatial_update_object(nx_spatial_t *sp, int id, const nx_object_t *obj) {
    if (!sp || !obj) return;
    
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS; i++) {
        if (sp->objects[i].active && sp->objects[i].id == id) {
            float *ovl_l = sp->objects[i].overlap_l;
            float *ovl_r = sp->objects[i].overlap_r;
            
            sp->objects[i] = *obj;
            sp->objects[i].overlap_l = ovl_l;
            sp->objects[i].overlap_r = ovl_r;
            sp->objects[i].active = 1;
            break;
        }
    }
}

void nx_spatial_set_object_pos(nx_spatial_t *sp, int id, float x, float y, float z) {
    nx_object_t *obj = nx_spatial_get_object(sp, id);
    if (obj) {
        obj->position = (nx_vec3_t){x, y, z};
    }
}

void nx_spatial_set_object_gain(nx_spatial_t *sp, int id, float gain) {
    nx_object_t *obj = nx_spatial_get_object(sp, id);
    if (obj) {
        obj->gain = gain;
    }
}

nx_object_t* nx_spatial_get_object(nx_spatial_t *sp, int id) {
    if (!sp) return NULL;
    
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS; i++) {
        if (sp->objects[i].active && sp->objects[i].id == id) {
            return &sp->objects[i];
        }
    }
    return NULL;
}

/* Settings */

void nx_spatial_set_distance_model(nx_spatial_t *sp, nx_distance_model_t model) {
    if (sp) sp->distance_model = model;
}

void nx_spatial_set_doppler(nx_spatial_t *sp, float factor) {
    if (sp) sp->doppler_factor = factor;
}

void nx_spatial_set_speed_of_sound(nx_spatial_t *sp, float speed) {
    if (sp) sp->speed_of_sound = speed;
}

void nx_spatial_enable_reverb(nx_spatial_t *sp, float mix) {
    if (sp) sp->reverb_mix = (mix < 0) ? 0 : (mix > 1.0f ? 1.0f : mix);
}

void nx_spatial_enable_height(nx_spatial_t *sp, int enable) {
    if (sp) sp->height_virtualization = enable;
}

/* Rendering */

void nx_spatial_apply_hrtf(nx_spatial_t *sp,
                            const float *mono_in, size_t frames,
                            int hrtf_index,
                            float *left_out, float *right_out) {
    if (!sp || !sp->hrtf || !mono_in) return;
    
    nx_hrtf_t *hrtf = sp->hrtf;
    
    if (hrtf_index < 0 || hrtf_index >= (int)hrtf->num_positions) {
        hrtf_index = 0;
    }
    
    size_t filter_idx = hrtf_index * hrtf->filter_length;
    const float *kernel_l = &hrtf->filters_l[filter_idx];
    const float *kernel_r = &hrtf->filters_r[filter_idx];
    
    /* Time-domain FIR convolution */
    for (size_t i = 0; i < frames; i++) {
        float sum_l = 0.0f;
        float sum_r = 0.0f;
        
        for (size_t j = 0; j < hrtf->filter_length && j <= i; j++) {
            sum_l += mono_in[i - j] * kernel_l[j];
            sum_r += mono_in[i - j] * kernel_r[j];
        }
        
        left_out[i] = sum_l;
        right_out[i] = sum_r;
    }
}

void nx_spatial_render_object(nx_spatial_t *sp, int id,
                               const float *mono_in, size_t frames,
                               float *stereo_out) {
    if (!sp || !mono_in || !stereo_out) return;
    
    nx_object_t *obj = nx_spatial_get_object(sp, id);
    if (!obj || !obj->active) return;
    
    /* Calculate angles and distance */
    nx_vec3_t dir = vec3_sub(obj->position, sp->listener.position);
    float distance = vec3_length(dir);
    
    float azimuth = 0.0f, elevation = 0.0f;
    calc_angles(&sp->listener, obj->position, &azimuth, &elevation);
    
    /* Distance attenuation */
    float dist_gain = calc_distance_attenuation(sp, distance,
                                                 obj->min_distance,
                                                 obj->max_distance,
                                                 obj->rolloff);
    
    /* Doppler */
    float doppler = calc_doppler(sp, obj->position, obj->velocity);
    (void)doppler;  /* Used for pitch shift if implemented */
    
    /* Total gain */
    float gain = obj->gain * dist_gain * sp->listener.gain;
    
    /* Get HRTF index */
    int hrtf_idx = 0;
    if (sp->hrtf_enabled && sp->hrtf) {
        hrtf_idx = get_hrtf_index(sp->hrtf, azimuth, elevation);
    }
    
    /* Apply gain to input */
    float *scaled = sp->work_buffer;
    for (size_t i = 0; i < frames; i++) {
        scaled[i] = mono_in[i] * gain;
    }
    
    /* Apply HRTF if enabled */
    if (sp->hrtf_enabled && sp->hrtf) {
        float *left = sp->work_buffer + frames;
        float *right = sp->work_buffer + frames * 2;
        
        nx_spatial_apply_hrtf(sp, scaled, frames, hrtf_idx, left, right);
        
        /* Interleave to stereo output */
        for (size_t i = 0; i < frames; i++) {
            stereo_out[i * 2] = left[i];
            stereo_out[i * 2 + 1] = right[i];
        }
    } else {
        /* Simple panning fallback */
        float pan = azimuth / 90.0f;
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        
        float angle = (pan + 1.0f) * 0.25f * PI;
        float gain_l = cosf(angle);
        float gain_r = sinf(angle);
        
        for (size_t i = 0; i < frames; i++) {
            stereo_out[i * 2] = scaled[i] * gain_l;
            stereo_out[i * 2 + 1] = scaled[i] * gain_r;
        }
    }
}

void nx_spatial_render(nx_spatial_t *sp,
                        float **object_inputs, size_t frames,
                        float *stereo_out) {
    if (!sp || !stereo_out) return;
    
    size_t out_samples = frames * 2;
    
    /* Clear output */
    memset(stereo_out, 0, out_samples * sizeof(float));
    
    /* Temp buffer for each object */
    float *temp = (float*)malloc(out_samples * sizeof(float));
    if (!temp) return;
    
    /* Render and mix each object */
    int obj_idx = 0;
    for (int i = 0; i < NX_SPATIAL_MAX_OBJECTS && object_inputs; i++) {
        if (sp->objects[i].active) {
            const float *input = object_inputs[obj_idx++];
            if (input) {
                nx_spatial_render_object(sp, sp->objects[i].id, 
                                         input, frames, temp);
                
                /* Mix to output */
                for (size_t s = 0; s < out_samples; s++) {
                    stereo_out[s] += temp[s];
                }
            }
        }
    }
    
    free(temp);
    
    /* Apply master gain */
    for (size_t s = 0; s < out_samples; s++) {
        stereo_out[s] *= sp->listener.gain;
        
        /* Soft clip */
        if (stereo_out[s] > 1.0f) {
            stereo_out[s] = 1.0f - expf(-stereo_out[s]);
        } else if (stereo_out[s] < -1.0f) {
            stereo_out[s] = -1.0f + expf(stereo_out[s]);
        }
    }
}

/* FFT convolution (overlap-add, for long kernels) */
void nx_spatial_convolve_fft(nx_spatial_t *sp,
                              const float *in, size_t frames,
                              const float *kernel_fft, size_t kernel_len,
                              float *out, float *overlap) {
    /* Placeholder - full implementation requires FFT library */
    (void)sp; (void)kernel_fft; (void)overlap;
    
    /* Fallback to direct copy with basic filtering */
    memcpy(out, in, frames * sizeof(float));
    
    /* Simple lowpass for height virtualization effect */
    if (kernel_len > 0) {
        float prev = 0.0f;
        for (size_t i = 0; i < frames; i++) {
            out[i] = 0.8f * out[i] + 0.2f * prev;
            prev = in[i];
        }
    }
}
