/*
 * NeolyxOS - NXRender Device API
 * Platform-agnostic rendering device abstraction
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * ARCHITECTURE (like Metal/Vulkan/DirectX):
 * ==========================================
 * 
 *     ┌─────────────────────────────────────────────┐
 *     │              NXRender Device                │
 *     │   (Abstract interface to GPU/Software)      │
 *     └─────────────────────┬───────────────────────┘
 *                           │
 *         ┌─────────────────┼─────────────────┐
 *         │                 │                 │
 *         ▼                 ▼                 ▼
 *    ┌─────────┐      ┌─────────┐      ┌─────────┐
 *    │   GPU   │      │Software │      │ Future  │
 *    │ Backend │      │ Backend │      │ Vulkan  │
 *    └─────────┘      └─────────┘      └─────────┘
 *
 * USAGE:
 * ======
 *   1. Create device: nx_device_create(NX_BACKEND_SOFTWARE)
 *   2. Create command buffer: nx_device_create_command_buffer(dev)
 *   3. Record commands: nx_cmd_clear(), nx_cmd_draw_rect(), etc.
 *   4. Submit: nx_device_submit(dev, cmdbuf)
 *   5. Present: nx_device_present(dev)
 */

#ifndef NXRENDER_DEVICE_H
#define NXRENDER_DEVICE_H

#include "nxgfx/nxgfx.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Backend Types
 * ============================================================================ */
typedef enum {
    NX_BACKEND_AUTO,        /* Auto-detect best backend */
    NX_BACKEND_SOFTWARE,    /* CPU software rendering */
    NX_BACKEND_GPU,         /* Hardware GPU (kernel driver) */
    NX_BACKEND_VULKAN       /* Vulkan (future) */
} nx_backend_type_t;

/* ============================================================================
 * Device Capabilities
 * ============================================================================ */
typedef struct {
    char name[64];
    nx_backend_type_t backend;
    uint64_t vram_size;
    uint32_t max_texture_size;
    uint32_t max_render_targets;
    bool supports_compute;
    bool supports_geometry_shaders;
    bool supports_tessellation;
    bool supports_raytracing;
    bool supports_hdr;
    bool supports_vsync;
    uint32_t max_samples;           /* MSAA */
} nx_device_caps_t;

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */
typedef struct nx_device nx_device_t;
typedef struct nx_command_buffer nx_command_buffer_t;
typedef struct nx_render_target nx_render_target_t;
typedef struct nx_texture nx_texture_t;
typedef struct nx_buffer nx_buffer_t;
typedef struct nx_pipeline nx_pipeline_t;

/* ============================================================================
 * Device Creation
 * ============================================================================ */

/* Device configuration */
typedef struct {
    nx_backend_type_t preferred_backend;
    uint32_t width;
    uint32_t height;
    void* framebuffer;              /* Optional: external framebuffer */
    uint32_t framebuffer_pitch;
    bool enable_vsync;
    bool enable_debug;
} nx_device_config_t;

/* Default configuration */
nx_device_config_t nx_device_default_config(void);

/* Create device with configuration */
nx_device_t* nx_device_create(nx_device_config_t config);

/* Destroy device and all resources */
void nx_device_destroy(nx_device_t* dev);

/* Get device capabilities */
const nx_device_caps_t* nx_device_caps(nx_device_t* dev);

/* Get underlying nxgfx context (for direct drawing) */
nx_context_t* nx_device_context(nx_device_t* dev);

/* ============================================================================
 * Command Buffer
 * ============================================================================ */

/* Create reusable command buffer */
nx_command_buffer_t* nx_device_create_command_buffer(nx_device_t* dev);

/* Destroy command buffer */
void nx_command_buffer_destroy(nx_command_buffer_t* cb);

/* Begin recording commands */
void nx_cmd_begin(nx_command_buffer_t* cb);

/* End recording */
void nx_cmd_end(nx_command_buffer_t* cb);

/* Reset command buffer for reuse */
void nx_cmd_reset(nx_command_buffer_t* cb);

/* ============================================================================
 * Render Commands
 * ============================================================================ */

/* Clear framebuffer */
void nx_cmd_clear(nx_command_buffer_t* cb, nx_color_t color);

/* Clear depth buffer */
void nx_cmd_clear_depth(nx_command_buffer_t* cb);

/* Set viewport */
void nx_cmd_set_viewport(nx_command_buffer_t* cb, nx_rect_t viewport);

/* Set scissor (clipping) */
void nx_cmd_set_scissor(nx_command_buffer_t* cb, nx_rect_t scissor);

/* Draw primitives */
void nx_cmd_draw_rect(nx_command_buffer_t* cb, nx_rect_t rect, nx_color_t color);
void nx_cmd_draw_rounded_rect(nx_command_buffer_t* cb, nx_rect_t rect, nx_color_t color, uint32_t radius);
void nx_cmd_draw_circle(nx_command_buffer_t* cb, nx_point_t center, uint32_t radius, nx_color_t color);
void nx_cmd_draw_line(nx_command_buffer_t* cb, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t width);
void nx_cmd_draw_triangle(nx_command_buffer_t* cb, nx_point_t p0, nx_point_t p1, nx_point_t p2, nx_color_t color);

/* Draw gradient */
void nx_cmd_draw_gradient(nx_command_buffer_t* cb, nx_rect_t rect, 
                           nx_color_t c1, nx_color_t c2, nx_gradient_dir_t dir);
void nx_cmd_draw_radial_gradient(nx_command_buffer_t* cb, nx_point_t center, 
                                  uint32_t radius, nx_color_t inner, nx_color_t outer);

/* Draw Bezier curve */
void nx_cmd_draw_bezier(nx_command_buffer_t* cb, nx_point_t p0, nx_point_t p1,
                         nx_point_t p2, nx_point_t p3, nx_color_t color, uint32_t thickness);

/* Draw texture/image */
void nx_cmd_draw_texture(nx_command_buffer_t* cb, nx_texture_t* tex, nx_rect_t dest);
void nx_cmd_draw_texture_region(nx_command_buffer_t* cb, nx_texture_t* tex, 
                                 nx_rect_t src, nx_rect_t dest);

/* 3D drawing (with depth) */
void nx_cmd_draw_triangle_3d(nx_command_buffer_t* cb,
                              float x0, float y0, float z0,
                              float x1, float y1, float z1,
                              float x2, float y2, float z2,
                              nx_color_t color);

/* Draw indexed mesh */
void nx_cmd_draw_mesh(nx_command_buffer_t* cb, nx_buffer_t* vertices, 
                       nx_buffer_t* indices, uint32_t index_count);

/* ============================================================================
 * Device Submission
 * ============================================================================ */

/* Submit command buffer for execution */
void nx_device_submit(nx_device_t* dev, nx_command_buffer_t* cb);

/* Wait for all submitted work to complete */
void nx_device_wait_idle(nx_device_t* dev);

/* Present framebuffer to screen */
void nx_device_present(nx_device_t* dev);

/* Copy framebuffer to external buffer */
void nx_device_copy_framebuffer(nx_device_t* dev, void* dest, uint32_t dest_pitch);

/* ============================================================================
 * Resource Creation
 * ============================================================================ */

/* Texture creation */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;            /* NX_TEX_RGBA8, etc. */
    bool generate_mipmaps;
    const void* data;           /* Optional initial data */
} nx_texture_desc_t;

nx_texture_t* nx_device_create_texture(nx_device_t* dev, nx_texture_desc_t desc);
void nx_texture_destroy(nx_texture_t* tex);
uint32_t nx_texture_width(nx_texture_t* tex);
uint32_t nx_texture_height(nx_texture_t* tex);

/* Buffer creation (vertices, indices) */
typedef enum {
    NX_BUFFER_VERTEX,
    NX_BUFFER_INDEX,
    NX_BUFFER_UNIFORM
} nx_buffer_type_t;

typedef struct {
    nx_buffer_type_t type;
    uint32_t size;
    const void* data;           /* Optional initial data */
} nx_buffer_desc_t;

nx_buffer_t* nx_device_create_buffer(nx_device_t* dev, nx_buffer_desc_t desc);
void nx_buffer_destroy(nx_buffer_t* buf);
void nx_buffer_update(nx_buffer_t* buf, const void* data, uint32_t size);

/* ============================================================================
 * Frame Statistics
 * ============================================================================ */
#ifndef NX_FRAME_STATS_DEFINED
#define NX_FRAME_STATS_DEFINED
typedef struct {
    uint64_t frame_number;
    uint64_t frame_time_us;
    uint32_t commands_executed;
    uint32_t draw_calls;
    uint32_t triangles_drawn;
    uint32_t textures_bound;
} nx_frame_stats_t;
#endif

nx_frame_stats_t nx_device_frame_stats(nx_device_t* dev);

#ifdef __cplusplus
}
#endif
#endif /* NXRENDER_DEVICE_H */
