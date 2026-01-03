/*
 * REOX Image System
 * Advanced image management, filters, and effects for NeolyxOS UI
 * 
 * Features:
 * - Image loading and caching
 * - Real-time filters (blur, sharpen, etc.)
 * - Image transformations
 * - Texture management
 * - Sprite sheets and atlases
 */

#ifndef REOX_IMAGE_SYSTEM_H
#define REOX_IMAGE_SYSTEM_H

#include "reox_ui.h"
#include "reox_color_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Image Format Types
 * ============================================================================ */

typedef enum rx_image_format {
    RX_IMAGE_RGBA8,           /* 32-bit RGBA */
    RX_IMAGE_RGB8,            /* 24-bit RGB */
    RX_IMAGE_GRAY8,           /* 8-bit grayscale */
    RX_IMAGE_GRAY16,          /* 16-bit grayscale */
    RX_IMAGE_RGBA16F,         /* 64-bit RGBA float */
    RX_IMAGE_RGBA32F,         /* 128-bit RGBA float */
} rx_image_format;

typedef enum rx_image_scaling {
    RX_SCALE_NEAREST,         /* Pixel art, sharp edges */
    RX_SCALE_BILINEAR,        /* Smooth, standard */
    RX_SCALE_BICUBIC,         /* High quality */
    RX_SCALE_LANCZOS,         /* Best quality, slowest */
} rx_image_scaling;

/* ============================================================================
 * Image Structure
 * ============================================================================ */

typedef struct rx_image {
    uint8_t* data;
    int width;
    int height;
    rx_image_format format;
    int stride;               /* Bytes per row */
    bool owns_data;           /* Should free data on destroy */
    
    /* GPU texture handle (optional) */
    uint32_t texture_id;
    bool texture_dirty;
    
    /* Metadata */
    const char* source_path;
    float dpi;
} rx_image;

/* ============================================================================
 * Image Loading and Creation
 * ============================================================================ */

extern rx_image* rx_image_create(int width, int height, rx_image_format format);
extern rx_image* rx_image_create_with_data(int width, int height, rx_image_format format, void* data);
extern rx_image* rx_image_load(const char* path);
extern rx_image* rx_image_load_from_memory(const void* data, size_t size);
extern rx_image* rx_image_copy(rx_image* src);
extern void rx_image_destroy(rx_image* img);

/* Image saving */
extern bool rx_image_save_png(rx_image* img, const char* path);
extern bool rx_image_save_jpeg(rx_image* img, const char* path, int quality);
extern void* rx_image_encode_png(rx_image* img, size_t* out_size);
extern void* rx_image_encode_jpeg(rx_image* img, size_t* out_size, int quality);

/* ============================================================================
 * Pixel Access
 * ============================================================================ */

extern rx_color rx_image_get_pixel(rx_image* img, int x, int y);
extern void rx_image_set_pixel(rx_image* img, int x, int y, rx_color color);
extern rx_color rx_image_sample(rx_image* img, float u, float v, rx_image_scaling filter);
extern void rx_image_fill(rx_image* img, rx_color color);
extern void rx_image_fill_rect(rx_image* img, rx_rect rect, rx_color color);

/* ============================================================================
 * Image Transformations
 * ============================================================================ */

extern rx_image* rx_image_resize(rx_image* img, int new_width, int new_height, rx_image_scaling filter);
extern rx_image* rx_image_crop(rx_image* img, rx_rect region);
extern rx_image* rx_image_rotate(rx_image* img, float degrees);
extern rx_image* rx_image_flip_horizontal(rx_image* img);
extern rx_image* rx_image_flip_vertical(rx_image* img);
extern rx_image* rx_image_transpose(rx_image* img);

/* In-place transforms */
extern void rx_image_rotate_90(rx_image* img);
extern void rx_image_rotate_180(rx_image* img);
extern void rx_image_rotate_270(rx_image* img);

/* ============================================================================
 * Image Filters
 * ============================================================================ */

typedef enum rx_filter_type {
    RX_FILTER_BLUR,
    RX_FILTER_GAUSSIAN_BLUR,
    RX_FILTER_MOTION_BLUR,
    RX_FILTER_SHARPEN,
    RX_FILTER_UNSHARP_MASK,
    RX_FILTER_EDGE_DETECT,
    RX_FILTER_EMBOSS,
    RX_FILTER_SOBEL,
} rx_filter_type;

/* Blur filters */
extern rx_image* rx_image_blur(rx_image* img, float radius);
extern rx_image* rx_image_gaussian_blur(rx_image* img, float sigma);
extern rx_image* rx_image_motion_blur(rx_image* img, float angle, float distance);
extern rx_image* rx_image_box_blur(rx_image* img, int radius);

/* Sharpen filters */
extern rx_image* rx_image_sharpen(rx_image* img, float amount);
extern rx_image* rx_image_unsharp_mask(rx_image* img, float amount, float radius, float threshold);

/* Edge detection */
extern rx_image* rx_image_edge_detect(rx_image* img);
extern rx_image* rx_image_sobel(rx_image* img);
extern rx_image* rx_image_canny(rx_image* img, float low_threshold, float high_threshold);

/* Stylization */
extern rx_image* rx_image_emboss(rx_image* img, float angle, float height);
extern rx_image* rx_image_oil_paint(rx_image* img, int radius, int levels);
extern rx_image* rx_image_pixelate(rx_image* img, int block_size);
extern rx_image* rx_image_posterize(rx_image* img, int levels);
extern rx_image* rx_image_dither(rx_image* img, rx_palette* palette);

/* ============================================================================
 * Color Adjustments
 * ============================================================================ */

extern rx_image* rx_image_brightness(rx_image* img, float amount);
extern rx_image* rx_image_contrast(rx_image* img, float amount);
extern rx_image* rx_image_saturation(rx_image* img, float amount);
extern rx_image* rx_image_hue_rotate(rx_image* img, float degrees);
extern rx_image* rx_image_gamma(rx_image* img, float gamma);
extern rx_image* rx_image_levels(rx_image* img, float in_black, float in_white, float gamma, float out_black, float out_white);
extern rx_image* rx_image_curves(rx_image* img, float* curve, int curve_points);
extern rx_image* rx_image_vibrance(rx_image* img, float amount);
extern rx_image* rx_image_temperature(rx_image* img, float kelvin);

/* Color effects */
extern rx_image* rx_image_grayscale(rx_image* img);
extern rx_image* rx_image_sepia(rx_image* img, float amount);
extern rx_image* rx_image_invert(rx_image* img);
extern rx_image* rx_image_threshold(rx_image* img, float level);
extern rx_image* rx_image_tint(rx_image* img, rx_color color, float amount);
extern rx_image* rx_image_duotone(rx_image* img, rx_color shadow, rx_color highlight);
extern rx_image* rx_image_color_matrix(rx_image* img, float matrix[20]);

/* ============================================================================
 * Compositing
 * ============================================================================ */

extern void rx_image_blend(rx_image* dst, rx_image* src, int x, int y, rx_blend_mode mode, float opacity);
extern rx_image* rx_image_composite(rx_image* base, rx_image* overlay, rx_blend_mode mode);
extern rx_image* rx_image_mask(rx_image* img, rx_image* mask);
extern rx_image* rx_image_alpha_composite(rx_image* dst, rx_image* src);

/* ============================================================================
 * Special Effects
 * ============================================================================ */

/* Glow and bloom */
extern rx_image* rx_image_glow(rx_image* img, rx_color color, float radius, float intensity);
extern rx_image* rx_image_bloom(rx_image* img, float threshold, float intensity, float radius);

/* Shadows and depth */
extern rx_image* rx_image_drop_shadow(rx_image* img, float offset_x, float offset_y, float blur, rx_color color);
extern rx_image* rx_image_inner_shadow(rx_image* img, float offset_x, float offset_y, float blur, rx_color color);
extern rx_image* rx_image_bevel(rx_image* img, float depth, float softness, float angle);

/* Distortions */
extern rx_image* rx_image_fisheye(rx_image* img, float strength);
extern rx_image* rx_image_swirl(rx_image* img, float angle, float radius);
extern rx_image* rx_image_ripple(rx_image* img, float wavelength, float amplitude, float phase);
extern rx_image* rx_image_perspective(rx_image* img, rx_point corners[4]);
extern rx_image* rx_image_spherize(rx_image* img, float amount);

/* Noise */
extern rx_image* rx_image_add_noise(rx_image* img, float amount);
extern rx_image* rx_image_reduce_noise(rx_image* img, float strength);
extern rx_image* rx_image_grain(rx_image* img, float amount, float size);

/* ============================================================================
 * Convolution Kernel
 * ============================================================================ */

typedef struct rx_kernel {
    float* values;
    int width;
    int height;
    float divisor;
    float offset;
} rx_kernel;

extern rx_kernel* rx_kernel_create(int width, int height);
extern rx_kernel* rx_kernel_gaussian(int size, float sigma);
extern rx_kernel* rx_kernel_sharpen(void);
extern rx_kernel* rx_kernel_edge_detect(void);
extern rx_kernel* rx_kernel_emboss(void);
extern rx_image* rx_image_convolve(rx_image* img, rx_kernel* kernel);
extern void rx_kernel_destroy(rx_kernel* kernel);

/* ============================================================================
 * Image Cache System
 * ============================================================================ */

typedef struct rx_image_cache {
    struct rx_cache_entry* entries;
    size_t entry_count;
    size_t max_entries;
    size_t max_bytes;
    size_t current_bytes;
} rx_image_cache;

extern rx_image_cache* rx_image_cache_create(size_t max_bytes);
extern rx_image* rx_image_cache_get(rx_image_cache* cache, const char* key);
extern void rx_image_cache_put(rx_image_cache* cache, const char* key, rx_image* img);
extern void rx_image_cache_remove(rx_image_cache* cache, const char* key);
extern void rx_image_cache_clear(rx_image_cache* cache);
extern void rx_image_cache_trim(rx_image_cache* cache, size_t target_bytes);
extern void rx_image_cache_destroy(rx_image_cache* cache);

/* Global cache */
extern rx_image_cache* rx_image_cache_shared(void);

/* ============================================================================
 * Texture Atlas / Sprite Sheet
 * ============================================================================ */

typedef struct rx_sprite {
    const char* name;
    rx_rect frame;            /* Position in atlas */
    rx_point pivot;           /* Rotation center */
    bool rotated;             /* 90 degree rotation in atlas */
} rx_sprite;

typedef struct rx_texture_atlas {
    rx_image* texture;
    rx_sprite* sprites;
    size_t sprite_count;
} rx_texture_atlas;

extern rx_texture_atlas* rx_atlas_load(const char* atlas_json, const char* texture_path);
extern rx_texture_atlas* rx_atlas_pack(rx_image** images, const char** names, size_t count, int max_size);
extern rx_sprite* rx_atlas_get_sprite(rx_texture_atlas* atlas, const char* name);
extern rx_image* rx_atlas_extract_sprite(rx_texture_atlas* atlas, const char* name);
extern void rx_atlas_destroy(rx_texture_atlas* atlas);

/* ============================================================================
 * Animation Sprite Sheet
 * ============================================================================ */

typedef struct rx_sprite_animation {
    rx_texture_atlas* atlas;
    rx_sprite** frames;
    size_t frame_count;
    float fps;
    bool loop;
    
    /* Playback state */
    float current_time;
    size_t current_frame;
    bool playing;
    bool reversed;
} rx_sprite_animation;

extern rx_sprite_animation* rx_sprite_anim_create(rx_texture_atlas* atlas, const char* prefix, float fps);
extern void rx_sprite_anim_play(rx_sprite_animation* anim);
extern void rx_sprite_anim_pause(rx_sprite_animation* anim);
extern void rx_sprite_anim_stop(rx_sprite_animation* anim);
extern void rx_sprite_anim_update(rx_sprite_animation* anim, float dt);
extern rx_sprite* rx_sprite_anim_current(rx_sprite_animation* anim);
extern void rx_sprite_anim_destroy(rx_sprite_animation* anim);

/* ============================================================================
 * 9-Slice Image (Stretchable UI elements)
 * ============================================================================ */

typedef struct rx_nine_slice {
    rx_image* image;
    rx_edge_insets insets;    /* Non-stretchable border sizes */
} rx_nine_slice;

extern rx_nine_slice* rx_nine_slice_create(rx_image* img, rx_edge_insets insets);
extern rx_image* rx_nine_slice_render(rx_nine_slice* slice, int width, int height);
extern void rx_nine_slice_destroy(rx_nine_slice* slice);

#ifdef __cplusplus
}
#endif

#endif /* REOX_IMAGE_SYSTEM_H */
