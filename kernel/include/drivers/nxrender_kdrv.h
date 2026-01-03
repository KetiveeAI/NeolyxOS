/*
 * NXRender Kernel Driver (nxrender_kdrv)
 * 
 * Kernel-side GUI acceleration driver for NXRender
 * Handles command buffer submission from user space
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXRENDER_KDRV_H
#define NXRENDER_KDRV_H

#include <stdint.h>
#include <stddef.h>

#define NXRENDER_KDRV_VERSION "1.0.0"

/* Command types - must match user-space gpu_driver.h */
typedef enum {
    NXRENDER_CMD_NOP = 0,
    NXRENDER_CMD_CLEAR,
    NXRENDER_CMD_RECT,
    NXRENDER_CMD_CIRCLE,
    NXRENDER_CMD_LINE,
    NXRENDER_CMD_BLIT,
    NXRENDER_CMD_MESH,
    NXRENDER_CMD_SCISSOR,
    NXRENDER_CMD_BLEND,
    NXRENDER_CMD_TEXTURE_UPLOAD,
    NXRENDER_CMD_PRESENT,
    NXRENDER_CMD_FENCE,
} nxrender_cmd_type_t;

/* Command structure */
typedef struct {
    uint32_t type;
    uint32_t flags;
    union {
        struct { uint32_t color; } clear;
        struct { int32_t x, y, w, h; uint32_t color; uint32_t radius; } rect;
        struct { int32_t cx, cy; uint32_t r; uint32_t color; } circle;
        struct { int32_t x1, y1, x2, y2; uint32_t color; uint32_t width; } line;
        struct { uint32_t texture_id; int32_t sx, sy, sw, sh; int32_t dx, dy, dw, dh; } blit;
        struct { int32_t x, y, w, h; } scissor;
        struct { uint32_t mode; } blend;
        struct { uint32_t fence_id; } fence;
        uint8_t raw[56];
    } data;
} nxrender_cmd_t;

/* Command buffer */
#define NXRENDER_CMD_BUFFER_SIZE 4096

typedef struct {
    nxrender_cmd_t commands[NXRENDER_CMD_BUFFER_SIZE];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint32_t fence_value;
    uint32_t flags;
} nxrender_cmd_buffer_t;

/* Texture format */
typedef enum {
    NXRENDER_TEX_RGBA8 = 0,
    NXRENDER_TEX_BGRA8,
    NXRENDER_TEX_RGB8,
    NXRENDER_TEX_R8,
} nxrender_tex_format_t;

/* Texture descriptor */
typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t vram_offset;
    uint64_t size;
    int in_use;
} nxrender_texture_t;

/* Surface (render target) */
typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t buffer_offset;
    int is_double_buffered;
    int current_buffer;
} nxrender_surface_t;

/* Performance counters */
typedef struct {
    uint64_t frames_rendered;
    uint64_t commands_processed;
    uint64_t pixels_drawn;
    uint64_t bytes_uploaded;
    uint32_t fps;
    uint32_t frame_time_us;
} nxrender_perf_t;

/* VSync mode */
typedef enum {
    NXRENDER_VSYNC_OFF = 0,
    NXRENDER_VSYNC_ON,
    NXRENDER_VSYNC_ADAPTIVE,
} nxrender_vsync_t;

/* ============ Driver API ============ */

/* Lifecycle */
int nxrender_kdrv_init(void);
void nxrender_kdrv_shutdown(void);

/* Command buffer */
nxrender_cmd_buffer_t* nxrender_kdrv_get_cmd_buffer(void);
int nxrender_kdrv_submit(void);
int nxrender_kdrv_wait_idle(void);

/* Surfaces */
int nxrender_kdrv_create_surface(uint32_t width, uint32_t height, int double_buffer);
int nxrender_kdrv_destroy_surface(int surface_id);
int nxrender_kdrv_set_target(int surface_id);
int nxrender_kdrv_present(int surface_id);

/* Textures */
int nxrender_kdrv_create_texture(uint32_t width, uint32_t height, uint32_t format);
int nxrender_kdrv_destroy_texture(int texture_id);
int nxrender_kdrv_upload_texture(int texture_id, const void *data, size_t size);

/* VSync */
int nxrender_kdrv_set_vsync(nxrender_vsync_t mode);
int nxrender_kdrv_wait_vblank(void);

/* Performance */
void nxrender_kdrv_get_perf(nxrender_perf_t *perf);
void nxrender_kdrv_reset_perf(void);

/* Framebuffer access (from nxgfx_kdrv) */
uint32_t* nxrender_kdrv_get_framebuffer(void);
uint32_t nxrender_kdrv_get_width(void);
uint32_t nxrender_kdrv_get_height(void);
uint32_t nxrender_kdrv_get_pitch(void);

/* Debug */
void nxrender_kdrv_debug(void);

#endif /* NXRENDER_KDRV_H */
