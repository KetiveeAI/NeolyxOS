/*
 * NeolyxOS - NXGFX GPU Driver Interface
 * Hardware-accelerated rendering via kernel GPU driver
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * GPU COMMUNICATION MODEL:
 * ========================
 * 
 * ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
 * │   NXRender      │────▶│   GPU Driver    │────▶│   GPU Hardware  │
 * │   (User Space)  │     │   (Kernel)      │     │   (VRAM)        │
 * └─────────────────┘     └─────────────────┘     └─────────────────┘
 *         │                       │                       │
 *         │ Command Buffer        │ DMA Transfer          │ Execute
 *         │ (draw calls)          │ (textures/vertices)   │ (shaders)
 *         ▼                       ▼                       ▼
 *    User Memory ────────▶ Kernel Buffer ────────▶ GPU Memory
 *
 * ZERO-COPY RENDERING:
 * ====================
 * NXRender writes commands to a shared ring buffer.
 * GPU driver processes commands without copying data.
 * Double/triple buffering prevents tearing.
 */

#ifndef NXGFX_GPU_DRIVER_H
#define NXGFX_GPU_DRIVER_H

#include "nxgfx/nxgfx.h"
#include "nxgfx/lighting.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * GPU Command Types
 * ============================================================================ */
typedef enum {
    NX_GPU_CMD_NOP = 0,
    NX_GPU_CMD_CLEAR,
    NX_GPU_CMD_DRAW_RECT,
    NX_GPU_CMD_DRAW_CIRCLE,
    NX_GPU_CMD_DRAW_LINE,
    NX_GPU_CMD_DRAW_TRIANGLE,
    NX_GPU_CMD_DRAW_MESH,
    NX_GPU_CMD_DRAW_TEXT,
    NX_GPU_CMD_BLIT_TEXTURE,
    NX_GPU_CMD_SET_SCISSOR,
    NX_GPU_CMD_SET_BLEND,
    NX_GPU_CMD_SET_SHADER,
    NX_GPU_CMD_UPLOAD_TEXTURE,
    NX_GPU_CMD_UPLOAD_VERTICES,
    NX_GPU_CMD_PRESENT,
    NX_GPU_CMD_FENCE
} nx_gpu_cmd_type_t;

/* GPU command structure (64 bytes aligned for cache efficiency) */
typedef struct {
    uint32_t type;          /* Command type */
    uint32_t flags;         /* Command flags */
    union {
        struct { uint32_t color; } clear;
        struct { int32_t x, y, w, h; uint32_t color; uint32_t radius; } rect;
        struct { int32_t cx, cy; uint32_t r; uint32_t color; } circle;
        struct { int32_t x1, y1, x2, y2; uint32_t color; uint32_t width; } line;
        struct { uint32_t texture_id; int32_t x, y, w, h; } blit;
        struct { uint32_t vertex_buffer; uint32_t index_buffer; uint32_t count; } mesh;
        struct { int32_t x1, y1, x2, y2; } scissor;
        struct { uint32_t mode; } blend;
        struct { uint32_t shader_id; } shader;
        struct { uint32_t fence_value; } fence;
    } data;
    uint32_t _padding[6];   /* Align to 64 bytes */
} nx_gpu_cmd_t;

/* ============================================================================
 * GPU Buffer Types
 * ============================================================================ */

/* Vertex format for 3D rendering */
typedef struct {
    float position[3];      /* x, y, z */
    float normal[3];        /* nx, ny, nz */
    float texcoord[2];      /* u, v */
    uint32_t color;         /* RGBA packed */
} nx_vertex_t;

/* GPU buffer handle */
typedef struct {
    uint32_t id;
    uint32_t size;
    uint32_t usage;         /* STATIC, DYNAMIC, STREAM */
    void* mapped_ptr;       /* For CPU access */
} nx_gpu_buffer_t;

/* Texture format */
typedef enum {
    NX_TEX_RGBA8,
    NX_TEX_BGRA8,
    NX_TEX_RGB8,
    NX_TEX_R8,
    NX_TEX_DEPTH24,
    NX_TEX_DEPTH32F
} nx_texture_format_t;

/* GPU texture handle */
typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    nx_texture_format_t format;
} nx_gpu_texture_t;

/* ============================================================================
 * GPU Command Ring Buffer
 * ============================================================================ */
#define NX_GPU_CMD_BUFFER_SIZE 4096

typedef struct {
    nx_gpu_cmd_t commands[NX_GPU_CMD_BUFFER_SIZE];
    volatile uint32_t write_index;  /* Written by CPU */
    volatile uint32_t read_index;   /* Read by GPU */
    volatile uint32_t fence_value;  /* Sync primitive */
} nx_gpu_cmd_buffer_t;

/* ============================================================================
 * GPU Driver State
 * ============================================================================ */
typedef struct {
    /* Device info */
    char name[64];
    uint32_t vendor_id;
    uint32_t device_id;
    uint64_t vram_size;
    uint32_t max_texture_size;
    
    /* Capabilities */
    bool supports_compute;
    bool supports_raytracing;
    bool supports_hdr;
    
    /* Current state */
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t refresh_rate;
} nx_gpu_info_t;

typedef struct {
    nx_gpu_info_t info;
    
    /* Command submission */
    nx_gpu_cmd_buffer_t* cmd_buffer;
    uint32_t pending_commands;
    
    /* Framebuffers (double buffering) */
    nx_gpu_texture_t front_buffer;
    nx_gpu_texture_t back_buffer;
    
    /* Resource tracking */
    uint32_t next_buffer_id;
    uint32_t next_texture_id;
    
    /* Kernel driver file descriptor */
    int driver_fd;
    
    /* Memory mapped regions */
    void* mmio_base;
    void* vram_base;
    
    /* VSync support */
    bool vsync_enabled;
    uint64_t last_frame_time;
    uint64_t frame_count;
} nx_gpu_driver_t;

/* ============================================================================
 * GPU Driver API
 * ============================================================================ */

/* Driver lifecycle */
nx_gpu_driver_t* nx_gpu_driver_init(void);
void nx_gpu_driver_shutdown(nx_gpu_driver_t* gpu);

/* Get GPU information */
const nx_gpu_info_t* nx_gpu_get_info(nx_gpu_driver_t* gpu);

/* Buffer management */
nx_gpu_buffer_t nx_gpu_create_buffer(nx_gpu_driver_t* gpu, uint32_t size, uint32_t usage);
void nx_gpu_destroy_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer);
void* nx_gpu_map_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer);
void nx_gpu_unmap_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer);
void nx_gpu_upload_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer, const void* data, uint32_t size);

/* Texture management */
nx_gpu_texture_t nx_gpu_create_texture(nx_gpu_driver_t* gpu, uint32_t w, uint32_t h, nx_texture_format_t fmt);
void nx_gpu_destroy_texture(nx_gpu_driver_t* gpu, nx_gpu_texture_t texture);
void nx_gpu_upload_texture(nx_gpu_driver_t* gpu, nx_gpu_texture_t texture, const void* data);

/* Command submission */
void nx_gpu_begin_frame(nx_gpu_driver_t* gpu);
void nx_gpu_end_frame(nx_gpu_driver_t* gpu);
void nx_gpu_submit(nx_gpu_driver_t* gpu);
void nx_gpu_wait_idle(nx_gpu_driver_t* gpu);

/* Draw commands */
void nx_gpu_cmd_clear(nx_gpu_driver_t* gpu, nx_color_t color);
void nx_gpu_cmd_rect(nx_gpu_driver_t* gpu, nx_rect_t rect, nx_color_t color, uint32_t radius);
void nx_gpu_cmd_circle(nx_gpu_driver_t* gpu, nx_point_t center, uint32_t radius, nx_color_t color);
void nx_gpu_cmd_line(nx_gpu_driver_t* gpu, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t width);
void nx_gpu_cmd_blit(nx_gpu_driver_t* gpu, nx_gpu_texture_t tex, nx_rect_t dst);
void nx_gpu_cmd_mesh(nx_gpu_driver_t* gpu, nx_gpu_buffer_t verts, nx_gpu_buffer_t idx, uint32_t count);

/* Present to screen */
void nx_gpu_present(nx_gpu_driver_t* gpu);

/* VSync control */
void nx_gpu_set_vsync(nx_gpu_driver_t* gpu, bool enabled);

/* Performance counters */
uint64_t nx_gpu_get_frame_time_us(nx_gpu_driver_t* gpu);
uint32_t nx_gpu_get_fps(nx_gpu_driver_t* gpu);

#ifdef __cplusplus
}
#endif
#endif
