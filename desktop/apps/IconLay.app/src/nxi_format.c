/*
 * NXI Format Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxi_format.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Device preset sizes */
static const uint32_t DESKTOP_SIZES[] = {16, 24, 32, 48, 64, 128, 256, 512};
static const uint32_t MOBILE_SIZES[] = {48, 72, 96, 120, 144, 192};
static const uint32_t WATCH_SIZES[] = {24, 38, 42, 48};
/* Combined all unique sizes for NXI_PRESET_ALL: 16,24,32,38,42,48,64,72,96,120,128,144,192,256,512 */
static const uint32_t ALL_SIZES[] = {16, 24, 32, 38, 42, 48, 64, 72, 96, 120, 128, 144, 192, 256, 512};

nxi_icon_t* nxi_create(void) {
    nxi_icon_t* icon = calloc(1, sizeof(nxi_icon_t));
    if (!icon) return NULL;
    
    icon->base_size = NXI_BASE_SIZE;
    icon->export_size = NXI_BASE_SIZE;
    icon->layer_capacity = 8;
    icon->layers = calloc(icon->layer_capacity, sizeof(nxi_layer_t*));
    
    return icon;
}

void nxi_free(nxi_icon_t* icon) {
    if (!icon) return;
    
    for (uint32_t i = 0; i < icon->layer_count; i++) {
        if (icon->layers[i]) {
            free(icon->layers[i]->paths);
            free(icon->layers[i]);
        }
    }
    free(icon->layers);
    free(icon);
}

nxi_layer_t* nxi_add_layer(nxi_icon_t* icon, const char* name) {
    if (!icon) return NULL;
    
    /* Expand if needed */
    if (icon->layer_count >= icon->layer_capacity) {
        icon->layer_capacity *= 2;
        icon->layers = realloc(icon->layers, 
                               icon->layer_capacity * sizeof(nxi_layer_t*));
    }
    
    nxi_layer_t* layer = calloc(1, sizeof(nxi_layer_t));
    if (!layer) return NULL;
    
    strncpy(layer->name, name ? name : "Layer", 31);
    layer->visible = true;
    layer->opacity = 255;
    layer->pos_x = 0.5f;
    layer->pos_y = 0.5f;
    layer->scale = 1.0f;
    layer->fill_color = 0xFFFFFFFF;  /* White by default */
    layer->path_capacity = 64;
    layer->paths = calloc(layer->path_capacity, sizeof(nxi_path_cmd_t));
    
    icon->layers[icon->layer_count++] = layer;
    return layer;
}

int nxi_remove_layer(nxi_icon_t* icon, uint32_t index) {
    if (!icon || index >= icon->layer_count) return -1;
    
    free(icon->layers[index]->paths);
    free(icon->layers[index]);
    
    /* Shift remaining layers */
    for (uint32_t i = index; i < icon->layer_count - 1; i++) {
        icon->layers[i] = icon->layers[i + 1];
    }
    icon->layer_count--;
    return 0;
}

int nxi_move_layer(nxi_icon_t* icon, uint32_t from, uint32_t to) {
    if (!icon || from >= icon->layer_count || to >= icon->layer_count)
        return -1;
    
    nxi_layer_t* temp = icon->layers[from];
    
    if (from < to) {
        for (uint32_t i = from; i < to; i++)
            icon->layers[i] = icon->layers[i + 1];
    } else {
        for (uint32_t i = from; i > to; i--)
            icon->layers[i] = icon->layers[i - 1];
    }
    
    icon->layers[to] = temp;
    return 0;
}

int nxi_layer_add_path(nxi_layer_t* layer, nxi_path_cmd_t cmd) {
    if (!layer) return -1;
    
    if (layer->path_count >= layer->path_capacity) {
        layer->path_capacity *= 2;
        layer->paths = realloc(layer->paths, 
                               layer->path_capacity * sizeof(nxi_path_cmd_t));
    }
    
    layer->paths[layer->path_count++] = cmd;
    return 0;
}

void nxi_layer_clear_paths(nxi_layer_t* layer) {
    if (layer) layer->path_count = 0;
}

int nxi_save(const nxi_icon_t* icon, const char* path) {
    if (!icon || !path) return -1;
    
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    
    /* Write header */
    nxi_header_t header = {
        .version = NXI_VERSION,
        .base_size = icon->base_size,
        .export_size = icon->export_size,
        .color_depth = NXI_DEPTH_RGBA32,
        .layer_count = icon->layer_count,
        .has_effects = icon->global_effects
    };
    memcpy(header.magic, NXI_MAGIC, 4);
    fwrite(&header, sizeof(header), 1, f);
    
    /* Write layers */
    for (uint32_t i = 0; i < icon->layer_count; i++) {
        nxi_layer_t* layer = icon->layers[i];
        
        nxi_layer_header_t lh = {
            .visible = layer->visible ? 1 : 0,
            .opacity = layer->opacity,
            .pos_x = layer->pos_x,
            .pos_y = layer->pos_y,
            .scale = layer->scale,
            .fill_color = layer->fill_color,
            .effect_flags = layer->effect_flags,
            .path_count = layer->path_count
        };
        strncpy(lh.name, layer->name, 31);
        fwrite(&lh, sizeof(lh), 1, f);
        
        /* Write path commands */
        fwrite(layer->paths, sizeof(nxi_path_cmd_t), layer->path_count, f);
    }
    
    fclose(f);
    return 0;
}

nxi_icon_t* nxi_load(const char* path) {
    if (!path) return NULL;
    
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    
    nxi_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return NULL;
    }
    
    /* Verify magic */
    if (memcmp(header.magic, NXI_MAGIC, 4) != 0) {
        fclose(f);
        return NULL;
    }
    
    nxi_icon_t* icon = nxi_create();
    if (!icon) {
        fclose(f);
        return NULL;
    }
    
    icon->base_size = header.base_size;
    icon->export_size = header.export_size;
    icon->global_effects = header.has_effects;
    
    /* Read layers */
    for (uint32_t i = 0; i < header.layer_count; i++) {
        nxi_layer_header_t lh;
        if (fread(&lh, sizeof(lh), 1, f) != 1) break;
        
        nxi_layer_t* layer = nxi_add_layer(icon, lh.name);
        if (!layer) break;
        
        layer->visible = lh.visible;
        layer->opacity = lh.opacity;
        layer->pos_x = lh.pos_x;
        layer->pos_y = lh.pos_y;
        layer->scale = lh.scale;
        layer->fill_color = lh.fill_color;
        layer->effect_flags = lh.effect_flags;
        
        /* Read path commands */
        if (lh.path_count > 0) {
            layer->path_capacity = lh.path_count;
            layer->paths = realloc(layer->paths, 
                                   lh.path_count * sizeof(nxi_path_cmd_t));
            fread(layer->paths, sizeof(nxi_path_cmd_t), lh.path_count, f);
            layer->path_count = lh.path_count;
        }
    }
    
    fclose(f);
    return icon;
}

const uint32_t* nxi_get_preset_sizes(nxi_preset_t preset, uint32_t* count) {
    switch (preset) {
        case NXI_PRESET_DESKTOP:
            *count = sizeof(DESKTOP_SIZES) / sizeof(DESKTOP_SIZES[0]);
            return DESKTOP_SIZES;
        case NXI_PRESET_MOBILE:
            *count = sizeof(MOBILE_SIZES) / sizeof(MOBILE_SIZES[0]);
            return MOBILE_SIZES;
        case NXI_PRESET_WATCH:
            *count = sizeof(WATCH_SIZES) / sizeof(WATCH_SIZES[0]);
            return WATCH_SIZES;
        case NXI_PRESET_ALL:
            *count = sizeof(ALL_SIZES) / sizeof(ALL_SIZES[0]);
            return ALL_SIZES;
        default:
            *count = 0;
            return NULL;
    }
}

int nxi_export_preset(const nxi_icon_t* icon, nxi_preset_t preset, 
                       const char* output_dir) {
    if (!icon || !output_dir) return -1;
    
    uint32_t count = 0;
    const uint32_t* sizes = nxi_get_preset_sizes(preset, &count);
    
    for (uint32_t i = 0; i < count; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/icon_%dx%d.nxi", 
                 output_dir, sizes[i], sizes[i]);
        
        /* Create export copy with target size */
        nxi_icon_t export_icon = *icon;
        export_icon.export_size = sizes[i];
        
        if (nxi_save(&export_icon, filename) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/* stb_image_write implementation - only include once */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Blend source pixel over destination (premultiplied alpha) */
static void blend_pixel(uint8_t* dst, const uint8_t* src) {
    float src_a = src[3] / 255.0f;
    float dst_a = dst[3] / 255.0f;
    float out_a = src_a + dst_a * (1.0f - src_a);
    
    if (out_a > 0.0001f) {
        dst[0] = (uint8_t)((src[0] * src_a + dst[0] * dst_a * (1.0f - src_a)) / out_a);
        dst[1] = (uint8_t)((src[1] * src_a + dst[1] * dst_a * (1.0f - src_a)) / out_a);
        dst[2] = (uint8_t)((src[2] * src_a + dst[2] * dst_a * (1.0f - src_a)) / out_a);
        dst[3] = (uint8_t)(out_a * 255);
    }
}

/* Draw filled circle to buffer */
static void draw_circle_to_buffer(uint8_t* buf, uint32_t size, 
                                   int cx, int cy, int r, uint32_t color) {
    uint8_t col[4] = {
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    };
    
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= (int)size) continue;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= (int)size) continue;
            int dx = x - cx;
            int dy = y - cy;
            if (dx*dx + dy*dy <= r*r) {
                int idx = (y * size + x) * 4;
                blend_pixel(&buf[idx], col);
            }
        }
    }
}

/* Draw filled rounded rect to buffer */
static void draw_rounded_rect_to_buffer(uint8_t* buf, uint32_t size,
                                         int x, int y, int w, int h, 
                                         int r, uint32_t color) {
    uint8_t col[4] = {
        (color >> 24) & 0xFF,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
    };
    
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= (int)size) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= (int)size) continue;
            
            /* Check if inside rounded corners */
            int inside = 1;
            if (r > 0) {
                /* Corner checks */
                if (px < x + r && py < y + r) {
                    int dx = px - (x + r);
                    int dy = py - (y + r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px >= x + w - r && py < y + r) {
                    int dx = px - (x + w - r);
                    int dy = py - (y + r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px < x + r && py >= y + h - r) {
                    int dx = px - (x + r);
                    int dy = py - (y + h - r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px >= x + w - r && py >= y + h - r) {
                    int dx = px - (x + w - r);
                    int dy = py - (y + h - r);
                    inside = (dx*dx + dy*dy <= r*r);
                }
            }
            
            if (inside) {
                int idx = (py * size + px) * 4;
                blend_pixel(&buf[idx], col);
            }
        }
    }
}

/* Interpolate between two colors */
static uint32_t lerp_color(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    
    uint8_t r1 = (c1 >> 24) & 0xFF;
    uint8_t g1 = (c1 >> 16) & 0xFF;
    uint8_t b1 = (c1 >> 8) & 0xFF;
    uint8_t a1 = c1 & 0xFF;
    
    uint8_t r2 = (c2 >> 24) & 0xFF;
    uint8_t g2 = (c2 >> 16) & 0xFF;
    uint8_t b2 = (c2 >> 8) & 0xFF;
    uint8_t a2 = c2 & 0xFF;
    
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
    uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);
    
    return (r << 24) | (g << 16) | (b << 8) | a;
}

/* Get color at gradient position */
static uint32_t get_gradient_color(const nxi_gradient_t* grad, float t) {
    if (!grad || grad->stop_count < 2) return 0xFFFFFFFF;
    
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    
    /* Find stops around t */
    for (uint8_t i = 0; i < grad->stop_count - 1; i++) {
        if (t >= grad->stops[i].position && t <= grad->stops[i + 1].position) {
            float range = grad->stops[i + 1].position - grad->stops[i].position;
            if (range < 0.0001f) return grad->stops[i].color;
            float local_t = (t - grad->stops[i].position) / range;
            return lerp_color(grad->stops[i].color, grad->stops[i + 1].color, local_t);
        }
    }
    
    return grad->stops[grad->stop_count - 1].color;
}

/* Compute gradient position for a pixel */
static float compute_gradient_t(const nxi_gradient_t* grad, 
                                 float px, float py, 
                                 float x, float y, float w, float h) {
    if (!grad || grad->type == NXI_GRADIENT_NONE) return 0.0f;
    
    float rel_x = (px - x) / w;  /* 0 to 1 across width */
    float rel_y = (py - y) / h;  /* 0 to 1 across height */
    
    if (grad->type == NXI_GRADIENT_LINEAR) {
        float rad = grad->angle * 3.14159265f / 180.0f;
        float dx = cosf(rad);
        float dy = sinf(rad);
        return (rel_x - 0.5f) * dx + (rel_y - 0.5f) * dy + 0.5f;
    } 
    else if (grad->type == NXI_GRADIENT_RADIAL) {
        float cx = grad->cx;
        float cy = grad->cy;
        float dist = sqrtf((rel_x - cx) * (rel_x - cx) + (rel_y - cy) * (rel_y - cy));
        float r = grad->radius > 0.0f ? grad->radius : 0.5f;
        return dist / r;
    }
    
    return 0.0f;
}

/* Draw gradient filled rounded rect to buffer */
static void draw_gradient_rounded_rect_to_buffer(uint8_t* buf, uint32_t buf_size,
                                                  int x, int y, int w, int h,
                                                  int r, const nxi_gradient_t* grad,
                                                  uint8_t opacity) {
    if (!grad || grad->type == NXI_GRADIENT_NONE || grad->stop_count < 2) return;
    
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= (int)buf_size) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= (int)buf_size) continue;
            
            /* Check if inside rounded corners */
            int inside = 1;
            if (r > 0) {
                if (px < x + r && py < y + r) {
                    int dx = px - (x + r);
                    int dy = py - (y + r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px >= x + w - r && py < y + r) {
                    int dx = px - (x + w - r);
                    int dy = py - (y + r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px < x + r && py >= y + h - r) {
                    int dx = px - (x + r);
                    int dy = py - (y + h - r);
                    inside = (dx*dx + dy*dy <= r*r);
                } else if (px >= x + w - r && py >= y + h - r) {
                    int dx = px - (x + w - r);
                    int dy = py - (y + h - r);
                    inside = (dx*dx + dy*dy <= r*r);
                }
            }
            
            if (inside) {
                float t = compute_gradient_t(grad, (float)px, (float)py, 
                                              (float)x, (float)y, (float)w, (float)h);
                uint32_t col = get_gradient_color(grad, t);
                
                /* Apply opacity */
                if (opacity < 255) {
                    uint8_t a = col & 0xFF;
                    a = (uint8_t)((a * opacity) / 255);
                    col = (col & 0xFFFFFF00) | a;
                }
                
                int idx = (py * buf_size + px) * 4;
                uint8_t src[4] = {
                    (col >> 24) & 0xFF,
                    (col >> 16) & 0xFF,
                    (col >> 8) & 0xFF,
                    col & 0xFF
                };
                blend_pixel(&buf[idx], src);
            }
        }
    }
}

/* Render layer path data to buffer */
static void render_layer_paths_to_buffer(uint8_t* buf, uint32_t buf_size,
                                          const nxi_layer_t* layer,
                                          float scale) {
    if (!layer || !layer->paths || layer->path_count == 0) return;
    
    /* Simple path rendering - fill bounded area */
    float min_x = 1.0f, min_y = 1.0f, max_x = 0.0f, max_y = 0.0f;
    
    for (uint32_t i = 0; i < layer->path_count; i++) {
        const nxi_path_cmd_t* cmd = &layer->paths[i];
        if (cmd->x < min_x) min_x = cmd->x;
        if (cmd->y < min_y) min_y = cmd->y;
        if (cmd->x > max_x) max_x = cmd->x;
        if (cmd->y > max_y) max_y = cmd->y;
    }
    
    /* Calculate pixel bounds */
    float layer_scale = layer->scale * scale;
    int cx = (int)(layer->pos_x * buf_size);
    int cy = (int)(layer->pos_y * buf_size);
    
    float path_w = (max_x - min_x) * layer_scale * buf_size;
    float path_h = (max_y - min_y) * layer_scale * buf_size;
    
    int x1 = cx - (int)(path_w / 2);
    int y1 = cy - (int)(path_h / 2);
    
    /* Check if gradient fill is enabled */
    if ((layer->effect_flags & NXI_EFFECT_GRADIENT) && 
        layer->effects.gradient.type != NXI_GRADIENT_NONE &&
        layer->effects.gradient.stop_count >= 2) {
        draw_gradient_rounded_rect_to_buffer(buf, buf_size, x1, y1, 
                                              (int)path_w, (int)path_h, 8,
                                              &layer->effects.gradient, 
                                              layer->opacity);
    } else {
        /* Solid fill */
        uint32_t col = layer->fill_color;
        if (layer->opacity < 255) {
            col = (col & 0xFFFFFF00) | layer->opacity;
        }
        draw_rounded_rect_to_buffer(buf, buf_size, x1, y1, 
                                     (int)path_w, (int)path_h, 8, col);
    }
}

uint8_t* nxi_render_to_buffer(const nxi_icon_t* icon, uint32_t size, 
                               void* render_ctx) {
    (void)render_ctx; /* Reserved for future use */
    
    if (!icon || size == 0) return NULL;
    
    /* Allocate RGBA buffer */
    uint8_t* buf = calloc(size * size * 4, 1);
    if (!buf) return NULL;
    
    /* Clear with transparent */
    memset(buf, 0, size * size * 4);
    
    float scale = (float)size / NXI_BASE_SIZE;
    
    /* Render each layer */
    for (uint32_t i = 0; i < icon->layer_count; i++) {
        nxi_layer_t* layer = icon->layers[i];
        if (!layer || !layer->visible || layer->opacity == 0) continue;
        
        /* Render layer content */
        render_layer_paths_to_buffer(buf, size, layer, scale);
        
        /* Apply effects */
        if (layer->effect_flags & NXI_EFFECT_GLOW) {
            /* Simple glow: render larger version with reduced opacity */
            float glow_scale = scale * 1.1f;
            uint32_t glow_col = (layer->fill_color & 0xFFFFFF00) | 0x40;
            render_layer_paths_to_buffer(buf, size, layer, glow_scale);
        }
    }
    
    return buf;
}

int nxi_export_png(const nxi_icon_t* icon, uint32_t size, 
                   const char* path, void* render_ctx) {
    if (!icon || !path || size == 0) return -1;
    
    uint8_t* buf = nxi_render_to_buffer(icon, size, render_ctx);
    if (!buf) return -1;
    
    /* Write PNG file */
    int result = stbi_write_png(path, size, size, 4, buf, size * 4);
    
    free(buf);
    return result ? 0 : -1;
}

int nxi_export_png_preset(const nxi_icon_t* icon, nxi_preset_t preset, 
                          const char* output_dir, void* render_ctx) {
    if (!icon || !output_dir) return -1;
    
    uint32_t count = 0;
    const uint32_t* sizes = nxi_get_preset_sizes(preset, &count);
    if (!sizes || count == 0) return -1;
    
    for (uint32_t i = 0; i < count; i++) {
        char filename[512];
        snprintf(filename, sizeof(filename), "%s/icon_%dx%d.png", 
                 output_dir, sizes[i], sizes[i]);
        
        if (nxi_export_png(icon, sizes[i], filename, render_ctx) != 0) {
            return -1;
        }
    }
    
    return 0;
}
