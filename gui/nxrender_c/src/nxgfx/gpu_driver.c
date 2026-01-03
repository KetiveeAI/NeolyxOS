/*
 * NeolyxOS - NXGFX GPU Driver Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 199309L
#include "nxgfx/gpu_driver.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Get current time in microseconds */
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

nx_gpu_driver_t* nx_gpu_driver_init(void) {
    nx_gpu_driver_t* gpu = calloc(1, sizeof(nx_gpu_driver_t));
    if (!gpu) return NULL;
    
    /* Initialize device info (would be queried from kernel driver) */
    strcpy(gpu->info.name, "NeolyxOS Virtual GPU");
    gpu->info.vendor_id = 0x1234;
    gpu->info.device_id = 0x5678;
    gpu->info.vram_size = 256 * 1024 * 1024;  /* 256 MB */
    gpu->info.max_texture_size = 8192;
    gpu->info.framebuffer_width = 1920;
    gpu->info.framebuffer_height = 1080;
    gpu->info.refresh_rate = 60;
    gpu->info.supports_compute = false;
    gpu->info.supports_raytracing = false;
    gpu->info.supports_hdr = false;
    
    /* Allocate command buffer */
    gpu->cmd_buffer = calloc(1, sizeof(nx_gpu_cmd_buffer_t));
    if (!gpu->cmd_buffer) { free(gpu); return NULL; }
    
    /* Initialize IDs */
    gpu->next_buffer_id = 1;
    gpu->next_texture_id = 1;
    gpu->vsync_enabled = true;
    gpu->last_frame_time = get_time_us();
    
    /* In real implementation, would open /dev/nxgpu and mmap VRAM */
    gpu->driver_fd = -1;
    
    return gpu;
}

void nx_gpu_driver_shutdown(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    free(gpu->cmd_buffer);
    free(gpu);
}

const nx_gpu_info_t* nx_gpu_get_info(nx_gpu_driver_t* gpu) {
    return gpu ? &gpu->info : NULL;
}

/* Buffer management */
nx_gpu_buffer_t nx_gpu_create_buffer(nx_gpu_driver_t* gpu, uint32_t size, uint32_t usage) {
    nx_gpu_buffer_t buf = {0};
    if (!gpu) return buf;
    buf.id = gpu->next_buffer_id++;
    buf.size = size;
    buf.usage = usage;
    buf.mapped_ptr = NULL;
    return buf;
}

void nx_gpu_destroy_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer) {
    (void)gpu; (void)buffer;
}

void* nx_gpu_map_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer) {
    (void)gpu;
    if (!buffer.mapped_ptr) buffer.mapped_ptr = malloc(buffer.size);
    return buffer.mapped_ptr;
}

void nx_gpu_unmap_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer) {
    (void)gpu; (void)buffer;
}

void nx_gpu_upload_buffer(nx_gpu_driver_t* gpu, nx_gpu_buffer_t buffer, const void* data, uint32_t size) {
    (void)gpu;
    if (buffer.mapped_ptr && data && size <= buffer.size) {
        memcpy(buffer.mapped_ptr, data, size);
    }
}

/* Texture management */
nx_gpu_texture_t nx_gpu_create_texture(nx_gpu_driver_t* gpu, uint32_t w, uint32_t h, nx_texture_format_t fmt) {
    nx_gpu_texture_t tex = {0};
    if (!gpu) return tex;
    tex.id = gpu->next_texture_id++;
    tex.width = w;
    tex.height = h;
    tex.format = fmt;
    return tex;
}

void nx_gpu_destroy_texture(nx_gpu_driver_t* gpu, nx_gpu_texture_t texture) {
    (void)gpu; (void)texture;
}

void nx_gpu_upload_texture(nx_gpu_driver_t* gpu, nx_gpu_texture_t texture, const void* data) {
    (void)gpu; (void)texture; (void)data;
}

/* Command helpers */
static nx_gpu_cmd_t* alloc_cmd(nx_gpu_driver_t* gpu) {
    if (!gpu || !gpu->cmd_buffer) return NULL;
    uint32_t idx = gpu->cmd_buffer->write_index;
    if (idx >= NX_GPU_CMD_BUFFER_SIZE) return NULL;
    gpu->cmd_buffer->write_index++;
    gpu->pending_commands++;
    return &gpu->cmd_buffer->commands[idx];
}

static uint32_t pack_color(nx_color_t c) {
    return (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
}

void nx_gpu_begin_frame(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    gpu->cmd_buffer->write_index = 0;
    gpu->cmd_buffer->read_index = 0;
    gpu->pending_commands = 0;
}

void nx_gpu_end_frame(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    /* Insert fence command */
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (cmd) {
        cmd->type = NX_GPU_CMD_FENCE;
        cmd->data.fence.fence_value = ++gpu->cmd_buffer->fence_value;
    }
}

void nx_gpu_submit(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    /* In real driver: send commands to kernel via ioctl */
    /* For now, mark as submitted */
    gpu->pending_commands = 0;
}

void nx_gpu_wait_idle(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    /* Wait for GPU to complete all commands */
    while (gpu->cmd_buffer->read_index < gpu->cmd_buffer->write_index) {
        /* Spin wait - real driver would use interrupts */
    }
}

/* Draw commands */
void nx_gpu_cmd_clear(nx_gpu_driver_t* gpu, nx_color_t color) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_CLEAR;
    cmd->data.clear.color = pack_color(color);
}

void nx_gpu_cmd_rect(nx_gpu_driver_t* gpu, nx_rect_t rect, nx_color_t color, uint32_t radius) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_DRAW_RECT;
    cmd->data.rect.x = rect.x;
    cmd->data.rect.y = rect.y;
    cmd->data.rect.w = rect.width;
    cmd->data.rect.h = rect.height;
    cmd->data.rect.color = pack_color(color);
    cmd->data.rect.radius = radius;
}

void nx_gpu_cmd_circle(nx_gpu_driver_t* gpu, nx_point_t center, uint32_t radius, nx_color_t color) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_DRAW_CIRCLE;
    cmd->data.circle.cx = center.x;
    cmd->data.circle.cy = center.y;
    cmd->data.circle.r = radius;
    cmd->data.circle.color = pack_color(color);
}

void nx_gpu_cmd_line(nx_gpu_driver_t* gpu, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t width) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_DRAW_LINE;
    cmd->data.line.x1 = p1.x;
    cmd->data.line.y1 = p1.y;
    cmd->data.line.x2 = p2.x;
    cmd->data.line.y2 = p2.y;
    cmd->data.line.color = pack_color(color);
    cmd->data.line.width = width;
}

void nx_gpu_cmd_blit(nx_gpu_driver_t* gpu, nx_gpu_texture_t tex, nx_rect_t dst) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_BLIT_TEXTURE;
    cmd->data.blit.texture_id = tex.id;
    cmd->data.blit.x = dst.x;
    cmd->data.blit.y = dst.y;
    cmd->data.blit.w = dst.width;
    cmd->data.blit.h = dst.height;
}

void nx_gpu_cmd_mesh(nx_gpu_driver_t* gpu, nx_gpu_buffer_t verts, nx_gpu_buffer_t idx, uint32_t count) {
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (!cmd) return;
    cmd->type = NX_GPU_CMD_DRAW_MESH;
    cmd->data.mesh.vertex_buffer = verts.id;
    cmd->data.mesh.index_buffer = idx.id;
    cmd->data.mesh.count = count;
}

void nx_gpu_present(nx_gpu_driver_t* gpu) {
    if (!gpu) return;
    
    /* Insert present command */
    nx_gpu_cmd_t* cmd = alloc_cmd(gpu);
    if (cmd) cmd->type = NX_GPU_CMD_PRESENT;
    
    /* VSync wait */
    if (gpu->vsync_enabled) {
        uint64_t now = get_time_us();
        uint64_t frame_time = 1000000 / gpu->info.refresh_rate;  /* 16666 us for 60 Hz */
        uint64_t elapsed = now - gpu->last_frame_time;
        if (elapsed < frame_time) {
            /* Sleep for remaining time */
            struct timespec ts = { 0, (frame_time - elapsed) * 1000 };
            nanosleep(&ts, NULL);
        }
        gpu->last_frame_time = get_time_us();
    }
    
    gpu->frame_count++;
}

void nx_gpu_set_vsync(nx_gpu_driver_t* gpu, bool enabled) {
    if (gpu) gpu->vsync_enabled = enabled;
}

uint64_t nx_gpu_get_frame_time_us(nx_gpu_driver_t* gpu) {
    return gpu ? (get_time_us() - gpu->last_frame_time) : 0;
}

uint32_t nx_gpu_get_fps(nx_gpu_driver_t* gpu) {
    if (!gpu) return 0;
    uint64_t elapsed = get_time_us() - gpu->last_frame_time;
    return elapsed > 0 ? (uint32_t)(1000000 / elapsed) : 0;
}
