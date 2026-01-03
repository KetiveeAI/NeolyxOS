/* Copyright (c) 2025 Swanaya Gupta
 * KETIVEEAI License v1.1 - Always Free.
 * See LICENSE in repo root.
 */

/*
 * NeolyxSpatial - 3D Audio Engine
 * 
 * Binaural HRTF rendering for immersive audio:
 * - Time-domain FIR convolution (fast for short kernels)
 * - Overlap-add FFT convolution (high performance)
 * - SOFA HRTF file support
 * - Object-based spatial audio
 * - Distance attenuation and Doppler
 * - Height virtualization
 */

#ifndef NXAUDIO_SPATIAL_H
#define NXAUDIO_SPATIAL_H

#include <stdint.h>
#include <stddef.h>

/* HRTF filter length */
#define NX_HRTF_LEN_SHORT       128
#define NX_HRTF_LEN_MEDIUM      256
#define NX_HRTF_LEN_LONG        512
#define NX_HRTF_LEN_MAX         2048

/* Maximum objects */
#define NX_SPATIAL_MAX_OBJECTS  64

/* Distance models */
typedef enum {
    NX_DIST_NONE,
    NX_DIST_INVERSE,
    NX_DIST_LINEAR,
    NX_DIST_EXPONENTIAL,
} nx_distance_model_t;

/* 3D Vector */
typedef struct {
    float x, y, z;
} nx_vec3_t;

/* Listener state */
typedef struct {
    nx_vec3_t   position;
    nx_vec3_t   forward;
    nx_vec3_t   up;
    nx_vec3_t   velocity;
    float       gain;
} nx_listener_t;

/* Spatial object */
typedef struct {
    int             id;
    int             active;
    nx_vec3_t       position;
    nx_vec3_t       velocity;
    float           gain;
    float           min_distance;
    float           max_distance;
    float           rolloff;
    int             spatial_enabled;
    
    /* Internal rendering state */
    int             hrtf_index_l;
    int             hrtf_index_r;
    float           distance_gain;
    float           doppler_shift;
    
    /* FIR state (overlap buffer) */
    float          *overlap_l;
    float          *overlap_r;
} nx_object_t;

/* HRTF dataset */
typedef struct {
    char            name[64];
    uint32_t        sample_rate;
    uint32_t        filter_length;
    uint32_t        num_elevations;
    uint32_t        num_azimuths;
    uint32_t        num_positions;
    
    /* Filter data [elevation][azimuth][channel][sample] */
    float          *filters_l;
    float          *filters_r;
    
    /* Precomputed FFT (for overlap-add) */
    float          *fft_filters_l;
    float          *fft_filters_r;
    int             fft_size;
    
    /* Elevation/azimuth lookup */
    float          *elevations;
    float          *azimuths;
} nx_hrtf_t;

/* Spatial engine */
typedef struct {
    uint32_t            sample_rate;
    uint32_t            channels;
    uint32_t            block_size;
    
    /* Listener */
    nx_listener_t       listener;
    
    /* Objects */
    nx_object_t         objects[NX_SPATIAL_MAX_OBJECTS];
    int                 num_objects;
    
    /* HRTF */
    nx_hrtf_t          *hrtf;
    int                 hrtf_enabled;
    int                 use_fft;        /* Use FFT convolution */
    
    /* Processing buffers */
    float              *work_buffer;
    float              *fft_in;
    float              *fft_out;
    size_t              work_size;
    
    /* Settings */
    nx_distance_model_t distance_model;
    float               speed_of_sound;
    float               doppler_factor;
    float               reverb_mix;
    int                 height_virtualization;
} nx_spatial_t;

/* Error codes */
#define NX_SPATIAL_OK           0
#define NX_SPATIAL_ERR_MEMORY   (-1)
#define NX_SPATIAL_ERR_FILE     (-2)
#define NX_SPATIAL_ERR_FORMAT   (-3)
#define NX_SPATIAL_ERR_INVALID  (-4)

/* Creation/destruction */
nx_spatial_t* nx_spatial_create(uint32_t sample_rate, uint32_t block_size);
void nx_spatial_destroy(nx_spatial_t *sp);

/* HRTF management */
int nx_spatial_load_hrtf(nx_spatial_t *sp, const char *filepath);
int nx_spatial_load_hrtf_default(nx_spatial_t *sp);
void nx_spatial_unload_hrtf(nx_spatial_t *sp);
void nx_spatial_enable_hrtf(nx_spatial_t *sp, int enable);

/* Listener control */
void nx_spatial_set_listener(nx_spatial_t *sp, const nx_listener_t *listener);
void nx_spatial_set_listener_pos(nx_spatial_t *sp, float x, float y, float z);
void nx_spatial_set_listener_orientation(nx_spatial_t *sp, 
                                          float fx, float fy, float fz,
                                          float ux, float uy, float uz);

/* Object management */
int nx_spatial_add_object(nx_spatial_t *sp, int id, const nx_object_t *obj);
void nx_spatial_remove_object(nx_spatial_t *sp, int id);
void nx_spatial_update_object(nx_spatial_t *sp, int id, const nx_object_t *obj);
void nx_spatial_set_object_pos(nx_spatial_t *sp, int id, float x, float y, float z);
void nx_spatial_set_object_gain(nx_spatial_t *sp, int id, float gain);
nx_object_t* nx_spatial_get_object(nx_spatial_t *sp, int id);

/* Settings */
void nx_spatial_set_distance_model(nx_spatial_t *sp, nx_distance_model_t model);
void nx_spatial_set_doppler(nx_spatial_t *sp, float factor);
void nx_spatial_set_speed_of_sound(nx_spatial_t *sp, float speed);
void nx_spatial_enable_reverb(nx_spatial_t *sp, float mix);
void nx_spatial_enable_height(nx_spatial_t *sp, int enable);

/* Rendering */
/* Render single object to stereo output */
void nx_spatial_render_object(nx_spatial_t *sp, int id,
                               const float *mono_in, size_t frames,
                               float *stereo_out);

/* Render all active objects and mix */
void nx_spatial_render(nx_spatial_t *sp, 
                        float **object_inputs, size_t frames,
                        float *stereo_out);

/* Internal HRTF processing (exposed for advanced use) */
void nx_spatial_apply_hrtf(nx_spatial_t *sp,
                            const float *mono_in, size_t frames,
                            int hrtf_index,
                            float *left_out, float *right_out);

/* FFT convolution (for long HRTFs) */
void nx_spatial_convolve_fft(nx_spatial_t *sp,
                              const float *in, size_t frames,
                              const float *kernel_fft, size_t kernel_len,
                              float *out, float *overlap);

#endif /* NXAUDIO_SPATIAL_H */
