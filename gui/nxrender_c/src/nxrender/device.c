/*
 * NeolyxOS - NXRender Device Implementation
 * Platform-agnostic rendering device
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 199309L
#include "nxrender/device.h"
#include "nxgfx/nxgfx.h"
#include "nxgfx/software_renderer.h"
#include "nxgfx/gpu_driver.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

#define MAX_COMMANDS 4096

typedef struct {
    uint32_t type;
    union {
        struct { nx_color_t color; } clear;
        struct { nx_rect_t rect; nx_color_t color; uint32_t radius; } rect;
        struct { nx_point_t center; uint32_t radius; nx_color_t color; } circle;
        struct { nx_point_t p1, p2; nx_color_t color; uint32_t width; } line;
        struct { nx_point_t p0, p1, p2; nx_color_t color; } triangle;
        struct { nx_rect_t rect; nx_color_t c1, c2; nx_gradient_dir_t dir; } gradient;
        struct { nx_point_t center; uint32_t radius; nx_color_t inner, outer; } radial;
        struct { nx_point_t p0, p1, p2, p3; nx_color_t color; uint32_t thickness; } bezier;
        struct { float x0, y0, z0, x1, y1, z1, x2, y2, z2; nx_color_t color; } tri3d;
        struct { nx_rect_t rect; } viewport;
        struct { nx_rect_t rect; } scissor;
    } data;
} nx_recorded_cmd_t;

struct nx_command_buffer {
    nx_recorded_cmd_t commands[MAX_COMMANDS];
    uint32_t count;
    bool recording;
    nx_device_t* device;
};

struct nx_device {
    nx_device_config_t config;
    nx_device_caps_t caps;
    
    /* Backend */
    nx_backend_type_t active_backend;
    nx_software_renderer_t* sw_renderer;
    nx_gpu_driver_t* gpu_driver;
    
    /* Context */
    nx_context_t* ctx;
    
    /* Frame stats */
    uint64_t frame_number;
    uint64_t frame_start_time;
    uint32_t commands_this_frame;
    uint32_t draw_calls_this_frame;
    uint32_t triangles_this_frame;
};

struct nx_texture {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t* data;
    nx_device_t* device;
};

struct nx_buffer {
    uint32_t id;
    nx_buffer_type_t type;
    uint32_t size;
    void* data;
    nx_device_t* device;
};

/* ============================================================================
 * Time Helper
 * ============================================================================ */
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ============================================================================
 * Device Creation
 * ============================================================================ */

nx_device_config_t nx_device_default_config(void) {
    return (nx_device_config_t){
        .preferred_backend = NX_BACKEND_AUTO,
        .width = 800,
        .height = 600,
        .framebuffer = NULL,
        .framebuffer_pitch = 0,
        .enable_vsync = true,
        .enable_debug = false
    };
}

nx_device_t* nx_device_create(nx_device_config_t config) {
    nx_device_t* dev = calloc(1, sizeof(nx_device_t));
    if (!dev) return NULL;
    
    dev->config = config;
    
    /* Determine backend */
    nx_backend_type_t backend = config.preferred_backend;
    if (backend == NX_BACKEND_AUTO) {
        /* Try GPU first, fall back to software */
        backend = NX_BACKEND_SOFTWARE;  /* For now, default to software */
    }
    
    dev->active_backend = backend;
    
    /* Initialize backend */
    switch (backend) {
        case NX_BACKEND_SOFTWARE:
        case NX_BACKEND_AUTO: {
            if (config.framebuffer) {
                dev->sw_renderer = nx_software_renderer_from_framebuffer(
                    config.framebuffer, config.width, config.height, config.framebuffer_pitch);
            } else {
                dev->sw_renderer = nx_software_renderer_create(config.width, config.height);
            }
            if (!dev->sw_renderer) {
                free(dev);
                return NULL;
            }
            dev->ctx = nx_software_renderer_context(dev->sw_renderer);
            
            /* Set capabilities */
            strcpy(dev->caps.name, "NXRender Software Renderer");
            dev->caps.backend = NX_BACKEND_SOFTWARE;
            dev->caps.vram_size = config.width * config.height * 4;
            dev->caps.max_texture_size = 8192;
            dev->caps.max_render_targets = 1;
            dev->caps.supports_compute = false;
            dev->caps.supports_geometry_shaders = false;
            dev->caps.supports_tessellation = false;
            dev->caps.supports_raytracing = false;
            dev->caps.supports_hdr = false;
            dev->caps.supports_vsync = true;
            dev->caps.max_samples = 1;
            break;
        }
        
        case NX_BACKEND_GPU: {
            dev->gpu_driver = nx_gpu_driver_init();
            if (!dev->gpu_driver) {
                /* Fall back to software */
                dev->active_backend = NX_BACKEND_SOFTWARE;
                dev->sw_renderer = nx_software_renderer_create(config.width, config.height);
                if (!dev->sw_renderer) {
                    free(dev);
                    return NULL;
                }
                dev->ctx = nx_software_renderer_context(dev->sw_renderer);
            } else {
                /* Use GPU */
                const nx_gpu_info_t* info = nx_gpu_get_info(dev->gpu_driver);
                strcpy(dev->caps.name, info->name);
                dev->caps.backend = NX_BACKEND_GPU;
                dev->caps.vram_size = info->vram_size;
                dev->caps.max_texture_size = info->max_texture_size;
            }
            break;
        }
        
        case NX_BACKEND_VULKAN:
            /* Not implemented yet - fall back to software */
            dev->active_backend = NX_BACKEND_SOFTWARE;
            dev->sw_renderer = nx_software_renderer_create(config.width, config.height);
            if (!dev->sw_renderer) {
                free(dev);
                return NULL;
            }
            dev->ctx = nx_software_renderer_context(dev->sw_renderer);
            break;
    }
    
    return dev;
}

void nx_device_destroy(nx_device_t* dev) {
    if (!dev) return;
    
    if (dev->sw_renderer) {
        nx_software_renderer_destroy(dev->sw_renderer);
    }
    if (dev->gpu_driver) {
        nx_gpu_driver_shutdown(dev->gpu_driver);
    }
    
    free(dev);
}

const nx_device_caps_t* nx_device_caps(nx_device_t* dev) {
    return dev ? &dev->caps : NULL;
}

nx_context_t* nx_device_context(nx_device_t* dev) {
    return dev ? dev->ctx : NULL;
}

/* ============================================================================
 * Command Buffer
 * ============================================================================ */

nx_command_buffer_t* nx_device_create_command_buffer(nx_device_t* dev) {
    if (!dev) return NULL;
    
    nx_command_buffer_t* cb = calloc(1, sizeof(nx_command_buffer_t));
    if (!cb) return NULL;
    
    cb->device = dev;
    cb->recording = false;
    
    return cb;
}

void nx_command_buffer_destroy(nx_command_buffer_t* cb) {
    if (cb) free(cb);
}

void nx_cmd_begin(nx_command_buffer_t* cb) {
    if (!cb) return;
    cb->count = 0;
    cb->recording = true;
}

void nx_cmd_end(nx_command_buffer_t* cb) {
    if (cb) cb->recording = false;
}

void nx_cmd_reset(nx_command_buffer_t* cb) {
    if (!cb) return;
    cb->count = 0;
    cb->recording = false;
}

/* ============================================================================
 * Command Recording
 * ============================================================================ */

#define RECORD_CMD(cb, cmd_type) \
    if (!cb || !cb->recording || cb->count >= MAX_COMMANDS) return; \
    nx_recorded_cmd_t* cmd = &cb->commands[cb->count++]; \
    cmd->type = cmd_type;

void nx_cmd_clear(nx_command_buffer_t* cb, nx_color_t color) {
    RECORD_CMD(cb, 1);
    cmd->data.clear.color = color;
}

void nx_cmd_clear_depth(nx_command_buffer_t* cb) {
    RECORD_CMD(cb, 2);
}

void nx_cmd_set_viewport(nx_command_buffer_t* cb, nx_rect_t viewport) {
    RECORD_CMD(cb, 3);
    cmd->data.viewport.rect = viewport;
}

void nx_cmd_set_scissor(nx_command_buffer_t* cb, nx_rect_t scissor) {
    RECORD_CMD(cb, 4);
    cmd->data.scissor.rect = scissor;
}

void nx_cmd_draw_rect(nx_command_buffer_t* cb, nx_rect_t rect, nx_color_t color) {
    RECORD_CMD(cb, 10);
    cmd->data.rect.rect = rect;
    cmd->data.rect.color = color;
    cmd->data.rect.radius = 0;
}

void nx_cmd_draw_rounded_rect(nx_command_buffer_t* cb, nx_rect_t rect, nx_color_t color, uint32_t radius) {
    RECORD_CMD(cb, 11);
    cmd->data.rect.rect = rect;
    cmd->data.rect.color = color;
    cmd->data.rect.radius = radius;
}

void nx_cmd_draw_circle(nx_command_buffer_t* cb, nx_point_t center, uint32_t radius, nx_color_t color) {
    RECORD_CMD(cb, 12);
    cmd->data.circle.center = center;
    cmd->data.circle.radius = radius;
    cmd->data.circle.color = color;
}

void nx_cmd_draw_line(nx_command_buffer_t* cb, nx_point_t p1, nx_point_t p2, nx_color_t color, uint32_t width) {
    RECORD_CMD(cb, 13);
    cmd->data.line.p1 = p1;
    cmd->data.line.p2 = p2;
    cmd->data.line.color = color;
    cmd->data.line.width = width;
}

void nx_cmd_draw_triangle(nx_command_buffer_t* cb, nx_point_t p0, nx_point_t p1, nx_point_t p2, nx_color_t color) {
    RECORD_CMD(cb, 14);
    cmd->data.triangle.p0 = p0;
    cmd->data.triangle.p1 = p1;
    cmd->data.triangle.p2 = p2;
    cmd->data.triangle.color = color;
}

void nx_cmd_draw_gradient(nx_command_buffer_t* cb, nx_rect_t rect,
                           nx_color_t c1, nx_color_t c2, nx_gradient_dir_t dir) {
    RECORD_CMD(cb, 20);
    cmd->data.gradient.rect = rect;
    cmd->data.gradient.c1 = c1;
    cmd->data.gradient.c2 = c2;
    cmd->data.gradient.dir = dir;
}

void nx_cmd_draw_radial_gradient(nx_command_buffer_t* cb, nx_point_t center,
                                  uint32_t radius, nx_color_t inner, nx_color_t outer) {
    RECORD_CMD(cb, 21);
    cmd->data.radial.center = center;
    cmd->data.radial.radius = radius;
    cmd->data.radial.inner = inner;
    cmd->data.radial.outer = outer;
}

void nx_cmd_draw_bezier(nx_command_buffer_t* cb, nx_point_t p0, nx_point_t p1,
                         nx_point_t p2, nx_point_t p3, nx_color_t color, uint32_t thickness) {
    RECORD_CMD(cb, 22);
    cmd->data.bezier.p0 = p0;
    cmd->data.bezier.p1 = p1;
    cmd->data.bezier.p2 = p2;
    cmd->data.bezier.p3 = p3;
    cmd->data.bezier.color = color;
    cmd->data.bezier.thickness = thickness;
}

void nx_cmd_draw_texture(nx_command_buffer_t* cb, nx_texture_t* tex, nx_rect_t dest) {
    (void)cb; (void)tex; (void)dest;
    /* TODO: Implement texture drawing */
}

void nx_cmd_draw_texture_region(nx_command_buffer_t* cb, nx_texture_t* tex,
                                 nx_rect_t src, nx_rect_t dest) {
    (void)cb; (void)tex; (void)src; (void)dest;
}

void nx_cmd_draw_triangle_3d(nx_command_buffer_t* cb,
                              float x0, float y0, float z0,
                              float x1, float y1, float z1,
                              float x2, float y2, float z2,
                              nx_color_t color) {
    RECORD_CMD(cb, 30);
    cmd->data.tri3d.x0 = x0; cmd->data.tri3d.y0 = y0; cmd->data.tri3d.z0 = z0;
    cmd->data.tri3d.x1 = x1; cmd->data.tri3d.y1 = y1; cmd->data.tri3d.z1 = z1;
    cmd->data.tri3d.x2 = x2; cmd->data.tri3d.y2 = y2; cmd->data.tri3d.z2 = z2;
    cmd->data.tri3d.color = color;
}

void nx_cmd_draw_mesh(nx_command_buffer_t* cb, nx_buffer_t* vertices,
                       nx_buffer_t* indices, uint32_t index_count) {
    (void)cb; (void)vertices; (void)indices; (void)index_count;
    /* TODO: Implement mesh drawing */
}

/* ============================================================================
 * Command Execution
 * ============================================================================ */

static void execute_commands(nx_device_t* dev, nx_command_buffer_t* cb) {
    if (!dev || !cb || !dev->ctx) return;
    
    nx_context_t* ctx = dev->ctx;
    
    for (uint32_t i = 0; i < cb->count; i++) {
        nx_recorded_cmd_t* cmd = &cb->commands[i];
        dev->commands_this_frame++;
        
        switch (cmd->type) {
            case 1: /* Clear */
                nxgfx_clear(ctx, cmd->data.clear.color);
                break;
                
            case 2: /* Clear depth */
                nxgfx_clear_depth(ctx);
                break;
                
            case 4: /* Scissor */
                nxgfx_set_clip(ctx, cmd->data.scissor.rect);
                break;
                
            case 10: /* Rect */
                dev->draw_calls_this_frame++;
                nxgfx_fill_rect(ctx, cmd->data.rect.rect, cmd->data.rect.color);
                break;
                
            case 11: /* Rounded rect */
                dev->draw_calls_this_frame++;
                nxgfx_fill_rounded_rect(ctx, cmd->data.rect.rect, 
                                         cmd->data.rect.color, cmd->data.rect.radius);
                break;
                
            case 12: /* Circle */
                dev->draw_calls_this_frame++;
                nxgfx_fill_circle(ctx, cmd->data.circle.center,
                                   cmd->data.circle.radius, cmd->data.circle.color);
                break;
                
            case 13: /* Line */
                dev->draw_calls_this_frame++;
                nxgfx_draw_line(ctx, cmd->data.line.p1, cmd->data.line.p2,
                                 cmd->data.line.color, cmd->data.line.width);
                break;
                
            case 14: /* Triangle */
                dev->draw_calls_this_frame++;
                dev->triangles_this_frame++;
                nxgfx_fill_triangle(ctx, cmd->data.triangle.p0,
                                     cmd->data.triangle.p1, cmd->data.triangle.p2,
                                     cmd->data.triangle.color);
                break;
                
            case 20: /* Gradient */
                dev->draw_calls_this_frame++;
                nxgfx_fill_gradient(ctx, cmd->data.gradient.rect,
                                     cmd->data.gradient.c1, cmd->data.gradient.c2,
                                     cmd->data.gradient.dir);
                break;
                
            case 21: /* Radial gradient */
                dev->draw_calls_this_frame++;
                nxgfx_fill_radial_gradient(ctx, cmd->data.radial.center,
                                            cmd->data.radial.radius,
                                            cmd->data.radial.inner,
                                            cmd->data.radial.outer);
                break;
                
            case 22: /* Bezier */
                dev->draw_calls_this_frame++;
                nxgfx_draw_bezier(ctx, cmd->data.bezier.p0, cmd->data.bezier.p1,
                                   cmd->data.bezier.p2, cmd->data.bezier.p3,
                                   cmd->data.bezier.color, cmd->data.bezier.thickness);
                break;
                
            case 30: /* 3D Triangle */
                dev->draw_calls_this_frame++;
                dev->triangles_this_frame++;
                nxgfx_fill_triangle_3d(ctx,
                    cmd->data.tri3d.x0, cmd->data.tri3d.y0, cmd->data.tri3d.z0,
                    cmd->data.tri3d.x1, cmd->data.tri3d.y1, cmd->data.tri3d.z1,
                    cmd->data.tri3d.x2, cmd->data.tri3d.y2, cmd->data.tri3d.z2,
                    cmd->data.tri3d.color);
                break;
        }
    }
}

void nx_device_submit(nx_device_t* dev, nx_command_buffer_t* cb) {
    if (!dev || !cb) return;
    execute_commands(dev, cb);
}

void nx_device_wait_idle(nx_device_t* dev) {
    (void)dev;
    /* Software renderer is synchronous - nothing to wait for */
}

void nx_device_present(nx_device_t* dev) {
    if (!dev) return;
    
    dev->frame_number++;
    dev->commands_this_frame = 0;
    dev->draw_calls_this_frame = 0;
    dev->triangles_this_frame = 0;
    dev->frame_start_time = get_time_us();
}

void nx_device_copy_framebuffer(nx_device_t* dev, void* dest, uint32_t dest_pitch) {
    if (!dev || !dest || !dev->sw_renderer) return;
    nx_software_renderer_present(dev->sw_renderer, dest, dest_pitch);
}

/* ============================================================================
 * Resource Management
 * ============================================================================ */

static uint32_t next_texture_id = 1;
static uint32_t next_buffer_id = 1;

nx_texture_t* nx_device_create_texture(nx_device_t* dev, nx_texture_desc_t desc) {
    if (!dev) return NULL;
    
    nx_texture_t* tex = calloc(1, sizeof(nx_texture_t));
    if (!tex) return NULL;
    
    tex->id = next_texture_id++;
    tex->width = desc.width;
    tex->height = desc.height;
    tex->device = dev;
    
    if (desc.data) {
        uint32_t size = desc.width * desc.height * sizeof(uint32_t);
        tex->data = malloc(size);
        if (tex->data) {
            memcpy(tex->data, desc.data, size);
        }
    }
    
    return tex;
}

void nx_texture_destroy(nx_texture_t* tex) {
    if (!tex) return;
    if (tex->data) free(tex->data);
    free(tex);
}

uint32_t nx_texture_width(nx_texture_t* tex) {
    return tex ? tex->width : 0;
}

uint32_t nx_texture_height(nx_texture_t* tex) {
    return tex ? tex->height : 0;
}

nx_buffer_t* nx_device_create_buffer(nx_device_t* dev, nx_buffer_desc_t desc) {
    if (!dev) return NULL;
    
    nx_buffer_t* buf = calloc(1, sizeof(nx_buffer_t));
    if (!buf) return NULL;
    
    buf->id = next_buffer_id++;
    buf->type = desc.type;
    buf->size = desc.size;
    buf->device = dev;
    
    buf->data = malloc(desc.size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    if (desc.data) {
        memcpy(buf->data, desc.data, desc.size);
    }
    
    return buf;
}

void nx_buffer_destroy(nx_buffer_t* buf) {
    if (!buf) return;
    if (buf->data) free(buf->data);
    free(buf);
}

void nx_buffer_update(nx_buffer_t* buf, const void* data, uint32_t size) {
    if (!buf || !data || !buf->data) return;
    uint32_t copy_size = size < buf->size ? size : buf->size;
    memcpy(buf->data, data, copy_size);
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

nx_frame_stats_t nx_device_frame_stats(nx_device_t* dev) {
    nx_frame_stats_t stats = {0};
    if (!dev) return stats;
    
    stats.frame_number = dev->frame_number;
    stats.frame_time_us = get_time_us() - dev->frame_start_time;
    stats.commands_executed = dev->commands_this_frame;
    stats.draw_calls = dev->draw_calls_this_frame;
    stats.triangles_drawn = dev->triangles_this_frame;
    
    return stats;
}
