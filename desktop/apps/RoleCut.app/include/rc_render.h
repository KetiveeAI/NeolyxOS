/*
 * RoleCut GPU Rendering Engine
 * 
 * Hardware-accelerated video rendering using NXGFX.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef RC_RENDER_H
#define RC_RENDER_H

#include "rolecut.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Render Backend
 * ============================================================================ */

typedef enum {
    RC_BACKEND_SOFTWARE = 0,    /* CPU fallback */
    RC_BACKEND_NXGFX,           /* NXGFX 2D acceleration */
    RC_BACKEND_VULKAN,          /* Vulkan (future) */
    RC_BACKEND_METAL,           /* Metal on future Neolyx ARM (future) */
} rc_render_backend_t;

/* ============================================================================
 * Pixel Formats
 * ============================================================================ */

typedef enum {
    RC_PIXEL_RGBA8 = 0,         /* 8-bit RGBA */
    RC_PIXEL_BGRA8,             /* 8-bit BGRA */
    RC_PIXEL_RGB10A2,           /* 10-bit RGB, 2-bit alpha (HDR) */
    RC_PIXEL_RGBA16F,           /* 16-bit float (compositing) */
    RC_PIXEL_RGBA32F,           /* 32-bit float (grading) */
    RC_PIXEL_YUV420,            /* YUV 4:2:0 */
    RC_PIXEL_YUV422,            /* YUV 4:2:2 */
    RC_PIXEL_YUV444,            /* YUV 4:4:4 */
} rc_pixel_format_t;

/* ============================================================================
 * Frame Buffer
 * ============================================================================ */

typedef struct {
    void *data;                 /* Pixel data */
    uint32_t width;
    uint32_t height;
    uint32_t stride;            /* Bytes per row */
    rc_pixel_format_t format;
    
    /* GPU texture handle (if GPU-backed) */
    uint32_t gpu_handle;
    bool is_gpu_backed;
} rc_frame_buffer_t;

/* ============================================================================
 * Render Context
 * ============================================================================ */

typedef struct rc_render_ctx rc_render_ctx_t;

/**
 * Create render context.
 */
rc_render_ctx_t *rc_render_create(rc_render_backend_t backend);

/**
 * Destroy render context.
 */
void rc_render_destroy(rc_render_ctx_t *ctx);

/**
 * Get active backend.
 */
rc_render_backend_t rc_render_get_backend(rc_render_ctx_t *ctx);

/**
 * Check if GPU acceleration available.
 */
bool rc_render_is_gpu_accelerated(rc_render_ctx_t *ctx);

/* ============================================================================
 * Frame Buffer Operations
 * ============================================================================ */

/**
 * Create frame buffer.
 */
rc_frame_buffer_t *rc_framebuffer_create(rc_render_ctx_t *ctx, 
                                          uint32_t width, uint32_t height,
                                          rc_pixel_format_t format);

/**
 * Create GPU-backed frame buffer.
 */
rc_frame_buffer_t *rc_framebuffer_create_gpu(rc_render_ctx_t *ctx,
                                              uint32_t width, uint32_t height,
                                              rc_pixel_format_t format);

/**
 * Destroy frame buffer.
 */
void rc_framebuffer_destroy(rc_frame_buffer_t *fb);

/**
 * Lock buffer for CPU access.
 */
void *rc_framebuffer_lock(rc_frame_buffer_t *fb);

/**
 * Unlock buffer.
 */
void rc_framebuffer_unlock(rc_frame_buffer_t *fb);

/**
 * Upload to GPU.
 */
void rc_framebuffer_upload(rc_render_ctx_t *ctx, rc_frame_buffer_t *fb);

/**
 * Download from GPU.
 */
void rc_framebuffer_download(rc_render_ctx_t *ctx, rc_frame_buffer_t *fb);

/* ============================================================================
 * Compositing Operations
 * ============================================================================ */

typedef enum {
    RC_BLEND_NORMAL = 0,        /* Standard alpha blend */
    RC_BLEND_ADD,               /* Additive */
    RC_BLEND_MULTIPLY,          /* Multiply */
    RC_BLEND_SCREEN,            /* Screen */
    RC_BLEND_OVERLAY,           /* Overlay */
    RC_BLEND_SOFT_LIGHT,        /* Soft light */
    RC_BLEND_HARD_LIGHT,        /* Hard light */
    RC_BLEND_DIFFERENCE,        /* Difference */
    RC_BLEND_EXCLUSION,         /* Exclusion */
    RC_BLEND_HUE,               /* Hue */
    RC_BLEND_SATURATION,        /* Saturation */
    RC_BLEND_COLOR,             /* Color */
    RC_BLEND_LUMINOSITY,        /* Luminosity */
} rc_blend_mode_t;

/**
 * Clear frame buffer.
 */
void rc_render_clear(rc_render_ctx_t *ctx, rc_frame_buffer_t *dst, rc_color_t color);

/**
 * Composite src onto dst.
 */
void rc_render_composite(rc_render_ctx_t *ctx,
                          rc_frame_buffer_t *dst,
                          rc_frame_buffer_t *src,
                          int x, int y,
                          float opacity,
                          rc_blend_mode_t blend);

/**
 * Scale and composite.
 */
void rc_render_composite_scaled(rc_render_ctx_t *ctx,
                                 rc_frame_buffer_t *dst,
                                 rc_frame_buffer_t *src,
                                 int dst_x, int dst_y,
                                 int dst_w, int dst_h,
                                 float opacity,
                                 rc_blend_mode_t blend);

/* ============================================================================
 * Transform Operations
 * ============================================================================ */

typedef struct {
    float m[3][3];              /* 3x3 transformation matrix */
} rc_transform_t;

/**
 * Create identity transform.
 */
rc_transform_t rc_transform_identity(void);

/**
 * Create translation transform.
 */
rc_transform_t rc_transform_translate(float tx, float ty);

/**
 * Create scale transform.
 */
rc_transform_t rc_transform_scale(float sx, float sy);

/**
 * Create rotation transform (radians).
 */
rc_transform_t rc_transform_rotate(float angle);

/**
 * Multiply transforms.
 */
rc_transform_t rc_transform_multiply(rc_transform_t a, rc_transform_t b);

/**
 * Apply transform to frame buffer.
 */
void rc_render_transform(rc_render_ctx_t *ctx,
                          rc_frame_buffer_t *dst,
                          rc_frame_buffer_t *src,
                          rc_transform_t transform);

/* ============================================================================
 * GPU Shader Effects
 * ============================================================================ */

typedef struct rc_shader rc_shader_t;

/**
 * Load shader from effect file.
 */
rc_shader_t *rc_shader_load(rc_render_ctx_t *ctx, const char *path);

/**
 * Destroy shader.
 */
void rc_shader_destroy(rc_shader_t *shader);

/**
 * Set shader parameter.
 */
void rc_shader_set_float(rc_shader_t *shader, const char *name, float value);
void rc_shader_set_vec2(rc_shader_t *shader, const char *name, float x, float y);
void rc_shader_set_vec4(rc_shader_t *shader, const char *name, float x, float y, float z, float w);
void rc_shader_set_color(rc_shader_t *shader, const char *name, rc_color_t color);

/**
 * Apply shader effect.
 */
void rc_render_apply_shader(rc_render_ctx_t *ctx,
                             rc_frame_buffer_t *dst,
                             rc_frame_buffer_t *src,
                             rc_shader_t *shader);

/* ============================================================================
 * Built-in Effects (GPU-accelerated)
 * ============================================================================ */

/**
 * Gaussian blur.
 */
void rc_render_blur(rc_render_ctx_t *ctx,
                     rc_frame_buffer_t *dst,
                     rc_frame_buffer_t *src,
                     float radius);

/**
 * Box blur (faster).
 */
void rc_render_blur_box(rc_render_ctx_t *ctx,
                         rc_frame_buffer_t *dst,
                         rc_frame_buffer_t *src,
                         int passes);

/**
 * Sharpen.
 */
void rc_render_sharpen(rc_render_ctx_t *ctx,
                        rc_frame_buffer_t *dst,
                        rc_frame_buffer_t *src,
                        float amount);

/**
 * Color adjustment.
 */
void rc_render_color_adjust(rc_render_ctx_t *ctx,
                             rc_frame_buffer_t *dst,
                             rc_frame_buffer_t *src,
                             float brightness,
                             float contrast,
                             float saturation,
                             float hue_shift);

/**
 * Apply LUT.
 */
void rc_render_apply_lut(rc_render_ctx_t *ctx,
                          rc_frame_buffer_t *dst,
                          rc_frame_buffer_t *src,
                          const char *lut_path);

/**
 * Chroma key.
 */
void rc_render_chroma_key(rc_render_ctx_t *ctx,
                           rc_frame_buffer_t *dst,
                           rc_frame_buffer_t *src,
                           rc_color_t key_color,
                           float tolerance,
                           float edge_softness);

/**
 * Vignette.
 */
void rc_render_vignette(rc_render_ctx_t *ctx,
                         rc_frame_buffer_t *dst,
                         rc_frame_buffer_t *src,
                         float intensity,
                         float softness);

/**
 * Film grain.
 */
void rc_render_film_grain(rc_render_ctx_t *ctx,
                           rc_frame_buffer_t *dst,
                           rc_frame_buffer_t *src,
                           float intensity,
                           float size);

/* ============================================================================
 * Transition Rendering
 * ============================================================================ */

/**
 * Render transition between two frames.
 * @param progress 0.0 to 1.0
 */
void rc_render_transition(rc_render_ctx_t *ctx,
                           rc_frame_buffer_t *dst,
                           rc_frame_buffer_t *from,
                           rc_frame_buffer_t *to,
                           rc_transition_type_t type,
                           float progress);

/* ============================================================================
 * Timeline Rendering
 * ============================================================================ */

/**
 * Render single frame from timeline at given time.
 */
void rc_render_timeline_frame(rc_render_ctx_t *ctx,
                               rc_frame_buffer_t *dst,
                               rc_timeline_t *timeline,
                               rc_time_t time);

/**
 * Start background render of timeline range.
 */
void rc_render_timeline_range(rc_render_ctx_t *ctx,
                               rc_timeline_t *timeline,
                               rc_time_t start,
                               rc_time_t end,
                               const char *output_path);

#ifdef __cplusplus
}
#endif

#endif /* RC_RENDER_H */
