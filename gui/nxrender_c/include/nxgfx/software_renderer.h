/*
 * NeolyxOS - NXGFX Software Renderer Backend
 * CPU-based fallback when GPU is not available
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * SOFTWARE RENDERING:
 * ===================
 * Executes GPU command buffer using CPU-based nxgfx primitives.
 * Used when:
 *   - No GPU driver available
 *   - GPU initialization failed
 *   - Debugging/development
 */

#ifndef NXGFX_SOFTWARE_RENDERER_H
#define NXGFX_SOFTWARE_RENDERER_H

#include "nxgfx/nxgfx.h"
#include "nxgfx/gpu_driver.h"
#include "nxgfx/nxgfx_effects.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Software renderer state */
typedef struct {
    nx_context_t* ctx;
    
    /* Framebuffer (owned by renderer) */
    uint32_t* framebuffer;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    bool owns_framebuffer;
    
    /* Scissor/clip region */
    nx_rect_t scissor;
    bool scissor_enabled;
    
    /* Blend mode */
    nx_blend_mode_t blend_mode;
    
    /* Texture cache (simple linear array) */
    struct {
        uint32_t id;
        uint32_t* data;
        uint32_t width;
        uint32_t height;
    } textures[64];
    uint32_t texture_count;
    
    /* Vertex buffers (simple linear array) */
    struct {
        uint32_t id;
        nx_vertex_t* data;
        uint32_t count;
    } vertex_buffers[64];
    uint32_t vb_count;
    
    /* Performance stats */
    uint64_t commands_executed;
    uint64_t last_frame_time_us;
} nx_software_renderer_t;

/* Lifecycle */
nx_software_renderer_t* nx_software_renderer_create(uint32_t width, uint32_t height);
nx_software_renderer_t* nx_software_renderer_from_framebuffer(void* fb, uint32_t w, uint32_t h, uint32_t pitch);
void nx_software_renderer_destroy(nx_software_renderer_t* sr);

/* Get underlying context for direct drawing */
nx_context_t* nx_software_renderer_context(nx_software_renderer_t* sr);

/* Execute a single GPU command */
void nx_software_renderer_execute_cmd(nx_software_renderer_t* sr, const nx_gpu_cmd_t* cmd);

/* Execute entire command buffer */
void nx_software_renderer_execute_buffer(nx_software_renderer_t* sr, 
                                          const nx_gpu_cmd_t* cmds, 
                                          uint32_t count);

/* Texture management */
uint32_t nx_software_renderer_upload_texture(nx_software_renderer_t* sr,
                                              const void* data,
                                              uint32_t width,
                                              uint32_t height);
void nx_software_renderer_delete_texture(nx_software_renderer_t* sr, uint32_t id);

/* Vertex buffer management */
uint32_t nx_software_renderer_upload_vertices(nx_software_renderer_t* sr,
                                               const nx_vertex_t* vertices,
                                               uint32_t count);
void nx_software_renderer_delete_vertices(nx_software_renderer_t* sr, uint32_t id);

/* Get framebuffer for display */
const uint32_t* nx_software_renderer_framebuffer(nx_software_renderer_t* sr);
uint32_t nx_software_renderer_width(nx_software_renderer_t* sr);
uint32_t nx_software_renderer_height(nx_software_renderer_t* sr);

/* Present (copy to destination if needed) */
void nx_software_renderer_present(nx_software_renderer_t* sr, void* dest_fb, uint32_t dest_pitch);

/* Stats */
uint64_t nx_software_renderer_commands_executed(nx_software_renderer_t* sr);

#ifdef __cplusplus
}
#endif
#endif /* NXGFX_SOFTWARE_RENDERER_H */
