/*
 * NeolyxOS - NXGFX Software Renderer Implementation
 * CPU-based fallback renderer
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#define _POSIX_C_SOURCE 199309L
#include "nxgfx/software_renderer.h"
#include "nxgfx/nxgfx.h"
#include "nxgfx/nxgfx_effects.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static nx_color_t unpack_color(uint32_t c) {
    return (nx_color_t){
        .r = (c >> 16) & 0xFF,
        .g = (c >> 8) & 0xFF,
        .b = c & 0xFF,
        .a = (c >> 24) & 0xFF
    };
}

nx_software_renderer_t* nx_software_renderer_create(uint32_t width, uint32_t height) {
    nx_software_renderer_t* sr = calloc(1, sizeof(nx_software_renderer_t));
    if (!sr) return NULL;
    
    sr->fb_width = width;
    sr->fb_height = height;
    sr->fb_pitch = width * sizeof(uint32_t);
    sr->owns_framebuffer = true;
    
    sr->framebuffer = calloc(width * height, sizeof(uint32_t));
    if (!sr->framebuffer) {
        free(sr);
        return NULL;
    }
    
    sr->ctx = nxgfx_init(sr->framebuffer, width, height, sr->fb_pitch);
    if (!sr->ctx) {
        free(sr->framebuffer);
        free(sr);
        return NULL;
    }
    
    sr->scissor = (nx_rect_t){0, 0, width, height};
    sr->scissor_enabled = false;
    sr->blend_mode = NX_BLEND_NORMAL;
    
    return sr;
}

nx_software_renderer_t* nx_software_renderer_from_framebuffer(void* fb, uint32_t w, uint32_t h, uint32_t pitch) {
    if (!fb || w == 0 || h == 0) return NULL;
    
    nx_software_renderer_t* sr = calloc(1, sizeof(nx_software_renderer_t));
    if (!sr) return NULL;
    
    sr->framebuffer = (uint32_t*)fb;
    sr->fb_width = w;
    sr->fb_height = h;
    sr->fb_pitch = pitch;
    sr->owns_framebuffer = false;
    
    sr->ctx = nxgfx_init(fb, w, h, pitch);
    if (!sr->ctx) {
        free(sr);
        return NULL;
    }
    
    sr->scissor = (nx_rect_t){0, 0, w, h};
    sr->scissor_enabled = false;
    sr->blend_mode = NX_BLEND_NORMAL;
    
    return sr;
}

void nx_software_renderer_destroy(nx_software_renderer_t* sr) {
    if (!sr) return;
    
    if (sr->ctx) nxgfx_destroy(sr->ctx);
    
    for (uint32_t i = 0; i < sr->texture_count; i++) {
        if (sr->textures[i].data) free(sr->textures[i].data);
    }
    
    for (uint32_t i = 0; i < sr->vb_count; i++) {
        if (sr->vertex_buffers[i].data) free(sr->vertex_buffers[i].data);
    }
    
    if (sr->owns_framebuffer && sr->framebuffer) {
        free(sr->framebuffer);
    }
    
    free(sr);
}

nx_context_t* nx_software_renderer_context(nx_software_renderer_t* sr) {
    return sr ? sr->ctx : NULL;
}

void nx_software_renderer_execute_cmd(nx_software_renderer_t* sr, const nx_gpu_cmd_t* cmd) {
    if (!sr || !sr->ctx || !cmd) return;
    
    sr->commands_executed++;
    
    switch (cmd->type) {
        case NX_GPU_CMD_NOP:
            break;
            
        case NX_GPU_CMD_CLEAR: {
            nx_color_t c = unpack_color(cmd->data.clear.color);
            nxgfx_clear(sr->ctx, c);
            break;
        }
        
        case NX_GPU_CMD_DRAW_RECT: {
            nx_rect_t r = {cmd->data.rect.x, cmd->data.rect.y, 
                           (uint32_t)cmd->data.rect.w, (uint32_t)cmd->data.rect.h};
            nx_color_t c = unpack_color(cmd->data.rect.color);
            if (cmd->data.rect.radius > 0) {
                nxgfx_fill_rounded_rect(sr->ctx, r, c, cmd->data.rect.radius);
            } else {
                nxgfx_fill_rect(sr->ctx, r, c);
            }
            break;
        }
        
        case NX_GPU_CMD_DRAW_CIRCLE: {
            nx_point_t center = {cmd->data.circle.cx, cmd->data.circle.cy};
            nx_color_t c = unpack_color(cmd->data.circle.color);
            nxgfx_fill_circle(sr->ctx, center, cmd->data.circle.r, c);
            break;
        }
        
        case NX_GPU_CMD_DRAW_LINE: {
            nx_point_t p1 = {cmd->data.line.x1, cmd->data.line.y1};
            nx_point_t p2 = {cmd->data.line.x2, cmd->data.line.y2};
            nx_color_t c = unpack_color(cmd->data.line.color);
            nxgfx_draw_line(sr->ctx, p1, p2, c, cmd->data.line.width);
            break;
        }
        
        case NX_GPU_CMD_DRAW_TRIANGLE: {
            /* Triangle data would be in vertex buffer - simplified for now */
            break;
        }
        
        case NX_GPU_CMD_DRAW_MESH: {
            /* Find vertex buffer */
            nx_vertex_t* verts = NULL;
            for (uint32_t i = 0; i < sr->vb_count; i++) {
                if (sr->vertex_buffers[i].id == cmd->data.mesh.vertex_buffer) {
                    verts = sr->vertex_buffers[i].data;
                    break;
                }
            }
            if (!verts) break;
            
            /* Draw triangles from vertex buffer */
            uint32_t tri_count = cmd->data.mesh.count / 3;
            for (uint32_t t = 0; t < tri_count; t++) {
                nx_vertex_t* v0 = &verts[t * 3];
                nx_vertex_t* v1 = &verts[t * 3 + 1];
                nx_vertex_t* v2 = &verts[t * 3 + 2];
                
                /* Use first vertex color for triangle */
                nx_color_t c = unpack_color(v0->color);
                
                nxgfx_fill_triangle_3d(sr->ctx,
                    v0->position[0], v0->position[1], v0->position[2],
                    v1->position[0], v1->position[1], v1->position[2],
                    v2->position[0], v2->position[1], v2->position[2],
                    c);
            }
            break;
        }
        
        case NX_GPU_CMD_BLIT_TEXTURE: {
            /* Find texture */
            uint32_t* tex_data = NULL;
            uint32_t tex_w = 0, tex_h = 0;
            for (uint32_t i = 0; i < sr->texture_count; i++) {
                if (sr->textures[i].id == cmd->data.blit.texture_id) {
                    tex_data = sr->textures[i].data;
                    tex_w = sr->textures[i].width;
                    tex_h = sr->textures[i].height;
                    break;
                }
            }
            if (!tex_data) break;
            
            /* Create temporary image and draw */
            nx_image_t* img = nxgfx_image_create(sr->ctx, tex_data, tex_w, tex_h);
            if (img) {
                if (cmd->data.blit.w != (int32_t)tex_w || cmd->data.blit.h != (int32_t)tex_h) {
                    nx_rect_t dest = {cmd->data.blit.x, cmd->data.blit.y,
                                      (uint32_t)cmd->data.blit.w, (uint32_t)cmd->data.blit.h};
                    nxgfx_draw_image_scaled(sr->ctx, img, dest);
                } else {
                    nxgfx_draw_image(sr->ctx, img, (nx_point_t){cmd->data.blit.x, cmd->data.blit.y});
                }
                nxgfx_image_destroy(img);
            }
            break;
        }
        
        case NX_GPU_CMD_SET_SCISSOR: {
            sr->scissor = (nx_rect_t){
                cmd->data.scissor.x1,
                cmd->data.scissor.y1,
                (uint32_t)(cmd->data.scissor.x2 - cmd->data.scissor.x1),
                (uint32_t)(cmd->data.scissor.y2 - cmd->data.scissor.y1)
            };
            sr->scissor_enabled = true;
            nxgfx_set_clip(sr->ctx, sr->scissor);
            break;
        }
        
        case NX_GPU_CMD_SET_BLEND: {
            sr->blend_mode = (nx_blend_mode_t)cmd->data.blend.mode;
            break;
        }
        
        case NX_GPU_CMD_PRESENT: {
            /* Mark frame complete */
            sr->last_frame_time_us = get_time_us();
            break;
        }
        
        case NX_GPU_CMD_FENCE:
        case NX_GPU_CMD_SET_SHADER:
        case NX_GPU_CMD_UPLOAD_TEXTURE:
        case NX_GPU_CMD_UPLOAD_VERTICES:
        case NX_GPU_CMD_DRAW_TEXT:
        default:
            break;
    }
}

void nx_software_renderer_execute_buffer(nx_software_renderer_t* sr,
                                          const nx_gpu_cmd_t* cmds,
                                          uint32_t count) {
    if (!sr || !cmds) return;
    for (uint32_t i = 0; i < count; i++) {
        nx_software_renderer_execute_cmd(sr, &cmds[i]);
    }
}

uint32_t nx_software_renderer_upload_texture(nx_software_renderer_t* sr,
                                              const void* data,
                                              uint32_t width,
                                              uint32_t height) {
    if (!sr || !data || sr->texture_count >= 64) return 0;
    
    uint32_t size = width * height * sizeof(uint32_t);
    uint32_t* copy = malloc(size);
    if (!copy) return 0;
    memcpy(copy, data, size);
    
    uint32_t id = sr->texture_count + 1;
    sr->textures[sr->texture_count].id = id;
    sr->textures[sr->texture_count].data = copy;
    sr->textures[sr->texture_count].width = width;
    sr->textures[sr->texture_count].height = height;
    sr->texture_count++;
    
    return id;
}

void nx_software_renderer_delete_texture(nx_software_renderer_t* sr, uint32_t id) {
    if (!sr) return;
    for (uint32_t i = 0; i < sr->texture_count; i++) {
        if (sr->textures[i].id == id) {
            free(sr->textures[i].data);
            sr->textures[i].data = NULL;
            sr->textures[i].id = 0;
            return;
        }
    }
}

uint32_t nx_software_renderer_upload_vertices(nx_software_renderer_t* sr,
                                               const nx_vertex_t* vertices,
                                               uint32_t count) {
    if (!sr || !vertices || sr->vb_count >= 64) return 0;
    
    uint32_t size = count * sizeof(nx_vertex_t);
    nx_vertex_t* copy = malloc(size);
    if (!copy) return 0;
    memcpy(copy, vertices, size);
    
    uint32_t id = sr->vb_count + 1;
    sr->vertex_buffers[sr->vb_count].id = id;
    sr->vertex_buffers[sr->vb_count].data = copy;
    sr->vertex_buffers[sr->vb_count].count = count;
    sr->vb_count++;
    
    return id;
}

void nx_software_renderer_delete_vertices(nx_software_renderer_t* sr, uint32_t id) {
    if (!sr) return;
    for (uint32_t i = 0; i < sr->vb_count; i++) {
        if (sr->vertex_buffers[i].id == id) {
            free(sr->vertex_buffers[i].data);
            sr->vertex_buffers[i].data = NULL;
            sr->vertex_buffers[i].id = 0;
            return;
        }
    }
}

const uint32_t* nx_software_renderer_framebuffer(nx_software_renderer_t* sr) {
    return sr ? sr->framebuffer : NULL;
}

uint32_t nx_software_renderer_width(nx_software_renderer_t* sr) {
    return sr ? sr->fb_width : 0;
}

uint32_t nx_software_renderer_height(nx_software_renderer_t* sr) {
    return sr ? sr->fb_height : 0;
}

void nx_software_renderer_present(nx_software_renderer_t* sr, void* dest_fb, uint32_t dest_pitch) {
    if (!sr || !dest_fb) return;
    
    uint32_t* src = sr->framebuffer;
    uint8_t* dst = (uint8_t*)dest_fb;
    uint32_t copy_bytes = sr->fb_width * sizeof(uint32_t);
    
    for (uint32_t y = 0; y < sr->fb_height; y++) {
        memcpy(dst + y * dest_pitch, src + y * sr->fb_width, copy_bytes);
    }
    
    sr->last_frame_time_us = get_time_us();
}

uint64_t nx_software_renderer_commands_executed(nx_software_renderer_t* sr) {
    return sr ? sr->commands_executed : 0;
}
