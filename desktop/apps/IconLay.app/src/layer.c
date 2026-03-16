/*
 * Layer System Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "layer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool layer_hit_test(const nxi_layer_t* layer, float x, float y) {
    if (!layer || !layer->visible) return false;
    
    layer_bounds_t b = layer_get_bounds(layer);
    return (x >= b.x && x <= b.x + b.width &&
            y >= b.y && y <= b.y + b.height);
}

layer_bounds_t layer_get_bounds(const nxi_layer_t* layer) {
    layer_bounds_t b = {0.5f, 0.5f, 0.0f, 0.0f};
    if (!layer || layer->path_count == 0) return b;
    
    float min_x = 1e9f, min_y = 1e9f;
    float max_x = -1e9f, max_y = -1e9f;
    
    for (uint32_t i = 0; i < layer->path_count; i++) {
        nxi_path_cmd_t* cmd = &layer->paths[i];
        if (cmd->x < min_x) min_x = cmd->x;
        if (cmd->y < min_y) min_y = cmd->y;
        if (cmd->x > max_x) max_x = cmd->x;
        if (cmd->y > max_y) max_y = cmd->y;
    }
    
    float w = (max_x - min_x) * layer->scale;
    float h = (max_y - min_y) * layer->scale;
    
    b.x = layer->pos_x - w / 2.0f;
    b.y = layer->pos_y - h / 2.0f;
    b.width = w;
    b.height = h;
    
    return b;
}

void layer_set_position(nxi_layer_t* layer, float x, float y) {
    if (layer) {
        layer->pos_x = x;
        layer->pos_y = y;
    }
}

void layer_set_scale(nxi_layer_t* layer, float scale) {
    if (layer && scale > 0.0f) {
        layer->scale = scale;
    }
}

void layer_set_color(nxi_layer_t* layer, uint32_t color) {
    if (layer) layer->fill_color = color;
}

void layer_set_opacity(nxi_layer_t* layer, uint8_t opacity) {
    if (layer) layer->opacity = opacity;
}

void layer_set_visible(nxi_layer_t* layer, bool visible) {
    if (layer) layer->visible = visible;
}

nxi_layer_t* layer_duplicate(const nxi_layer_t* layer) {
    if (!layer) return NULL;
    
    nxi_layer_t* dup = calloc(1, sizeof(nxi_layer_t));
    if (!dup) return NULL;
    
    memcpy(dup, layer, sizeof(nxi_layer_t));
    
    if (layer->path_count > 0) {
        dup->path_capacity = layer->path_count;
        dup->paths = malloc(layer->path_count * sizeof(nxi_path_cmd_t));
        memcpy(dup->paths, layer->paths, 
               layer->path_count * sizeof(nxi_path_cmd_t));
    }
    
    return dup;
}

/* Blend RGBA color onto buffer */
static void blend_pixel(uint8_t* buf, uint32_t idx, 
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0) return;
    
    if (a == 255) {
        buf[idx] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
        buf[idx + 3] = 255;
        return;
    }
    
    float alpha = a / 255.0f;
    buf[idx] = (uint8_t)(buf[idx] * (1 - alpha) + r * alpha);
    buf[idx + 1] = (uint8_t)(buf[idx + 1] * (1 - alpha) + g * alpha);
    buf[idx + 2] = (uint8_t)(buf[idx + 2] * (1 - alpha) + b * alpha);
    buf[idx + 3] = 255;
}

/* Flatten cubic Bezier curve to line segments */
typedef struct {
    float* x;
    float* y;
    uint32_t count;
    uint32_t capacity;
} point_list_t;

static void point_list_add(point_list_t* list, float x, float y) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 256;
        list->x = realloc(list->x, list->capacity * sizeof(float));
        list->y = realloc(list->y, list->capacity * sizeof(float));
    }
    list->x[list->count] = x;
    list->y[list->count] = y;
    list->count++;
}

/* Recursive Bezier flattening with adaptive subdivision */
static void flatten_cubic(point_list_t* pts,
                          float x0, float y0, float x1, float y1,
                          float x2, float y2, float x3, float y3,
                          int depth) {
    if (depth > 20) {
        point_list_add(pts, x3, y3);
        return;
    }
    
    /* Check if flat enough */
    float dx = x3 - x0;
    float dy = y3 - y0;
    float d1 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d2 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    
    float tol = 0.02f;  /* Tighter tolerance for smooth curves */
    if ((d1 + d2) * (d1 + d2) < tol * tol * (dx * dx + dy * dy)) {
        point_list_add(pts, x3, y3);
        return;
    }
    
    /* Subdivide at midpoint */
    float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float x0123 = (x012 + x123) * 0.5f, y0123 = (y012 + y123) * 0.5f;
    
    flatten_cubic(pts, x0, y0, x01, y01, x012, y012, x0123, y0123, depth + 1);
    flatten_cubic(pts, x0123, y0123, x123, y123, x23, y23, x3, y3, depth + 1);
}

/* Flatten quadratic Bezier */
static void flatten_quad(point_list_t* pts,
                         float x0, float y0, float x1, float y1,
                         float x2, float y2, int depth) {
    if (depth > 12) {
        point_list_add(pts, x2, y2);
        return;
    }
    
    float dx = x2 - x0;
    float dy = y2 - y0;
    float d = fabsf((x1 - x2) * dy - (y1 - y2) * dx);
    
    float tol = 0.05f;  /* Tighter tolerance for quadratics */
    if (d * d < tol * tol * (dx * dx + dy * dy)) {
        point_list_add(pts, x2, y2);
        return;
    }
    
    float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    
    flatten_quad(pts, x0, y0, x01, y01, x012, y012, depth + 1);
    flatten_quad(pts, x012, y012, x12, y12, x2, y2, depth + 1);
}

/* Subpath structure for even-odd fill */
typedef struct {
    float* x;
    float* y;
    uint32_t count;
} subpath_t;

/* Flatten path commands to polygon points, splitting at MOVE commands */
static void flatten_paths(const nxi_path_cmd_t* paths, uint32_t path_count,
                          float ox, float oy, float scale, float size,
                          subpath_t** out_subpaths, uint32_t* out_subpath_count) {
    subpath_t* subpaths = NULL;
    uint32_t subpath_count = 0;
    uint32_t subpath_capacity = 0;
    
    point_list_t current = {0};
    float cx = 0, cy = 0;
    
    for (uint32_t i = 0; i < path_count; i++) {
        const nxi_path_cmd_t* cmd = &paths[i];
        float px = (ox + cmd->x * scale) * size;
        float py = (oy + cmd->y * scale) * size;
        
        switch (cmd->type) {
            case NXI_PATH_MOVE:
                /* Start new subpath */
                if (current.count > 2) {
                    if (subpath_count >= subpath_capacity) {
                        subpath_capacity = subpath_capacity ? subpath_capacity * 2 : 8;
                        subpaths = realloc(subpaths, subpath_capacity * sizeof(subpath_t));
                    }
                    subpaths[subpath_count].x = current.x;
                    subpaths[subpath_count].y = current.y;
                    subpaths[subpath_count].count = current.count;
                    subpath_count++;
                    current = (point_list_t){0};
                } else {
                    free(current.x);
                    free(current.y);
                    current = (point_list_t){0};
                }
                point_list_add(&current, px, py);
                cx = px; cy = py;
                break;
                
            case NXI_PATH_LINE:
                point_list_add(&current, px, py);
                cx = px; cy = py;
                break;
                
            case NXI_PATH_CUBIC: {
                float x1 = (ox + cmd->x1 * scale) * size;
                float y1 = (oy + cmd->y1 * scale) * size;
                float x2 = (ox + cmd->x2 * scale) * size;
                float y2 = (oy + cmd->y2 * scale) * size;
                flatten_cubic(&current, cx, cy, x1, y1, x2, y2, px, py, 0);
                cx = px; cy = py;
                break;
            }
            
            case NXI_PATH_QUAD: {
                float x1 = (ox + cmd->x1 * scale) * size;
                float y1 = (oy + cmd->y1 * scale) * size;
                flatten_quad(&current, cx, cy, x1, y1, px, py, 0);
                cx = px; cy = py;
                break;
            }
            
            case NXI_PATH_CLOSE:
                if (current.count > 0) {
                    point_list_add(&current, current.x[0], current.y[0]);
                }
                cx = current.count > 0 ? current.x[0] : 0;
                cy = current.count > 0 ? current.y[0] : 0;
                break;
        }
    }
    
    /* Add final subpath */
    if (current.count > 2) {
        if (subpath_count >= subpath_capacity) {
            subpath_capacity = subpath_capacity ? subpath_capacity * 2 : 8;
            subpaths = realloc(subpaths, subpath_capacity * sizeof(subpath_t));
        }
        subpaths[subpath_count].x = current.x;
        subpaths[subpath_count].y = current.y;
        subpaths[subpath_count].count = current.count;
        subpath_count++;
    } else {
        free(current.x);
        free(current.y);
    }
    
    *out_subpaths = subpaths;
    *out_subpath_count = subpath_count;
}

/* Scanline rasterizer with 16-sample anti-aliasing */
static void rasterize_subpaths(uint8_t* buf, uint32_t size,
                               subpath_t* subpaths, uint32_t subpath_count,
                               uint32_t color, uint8_t opacity) {
    if (subpath_count == 0) return;
    
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = ((color & 0xFF) * opacity) / 255;
    
    /* Find bounding box */
    float min_y = 1e9f, max_y = -1e9f;
    for (uint32_t s = 0; s < subpath_count; s++) {
        for (uint32_t i = 0; i < subpaths[s].count; i++) {
            if (subpaths[s].y[i] < min_y) min_y = subpaths[s].y[i];
            if (subpaths[s].y[i] > max_y) max_y = subpaths[s].y[i];
        }
    }
    
    int y0 = (int)floorf(min_y) - 1;
    int y1 = (int)ceilf(max_y) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= (int)size) y1 = size - 1;
    
    /* Count total edges */
    uint32_t total_edges = 0;
    for (uint32_t s = 0; s < subpath_count; s++) {
        total_edges += subpaths[s].count;
    }
    float* crossings = malloc((total_edges + 1) * sizeof(float));
    /* 24-sample anti-aliasing for smoother edges */
    #define AA_SAMPLES 24
    
    float* coverage = calloc(size, sizeof(float));
    
    for (int y = y0; y <= y1; y++) {
        memset(coverage, 0, size * sizeof(float));
        
        for (int sample = 0; sample < AA_SAMPLES; sample++) {
            float yf = y + (sample + 0.5f) / AA_SAMPLES;
            uint32_t cross_count = 0;
            
            /* Collect crossings */
            for (uint32_t s = 0; s < subpath_count; s++) {
                subpath_t* sp = &subpaths[s];
                for (uint32_t i = 0; i < sp->count; i++) {
                    uint32_t j = (i + 1) % sp->count;
                    float y0f = sp->y[i], y1f = sp->y[j];
                    float x0f = sp->x[i], x1f = sp->x[j];
                    
                    if ((y0f <= yf && y1f > yf) || (y1f <= yf && y0f > yf)) {
                        float t = (yf - y0f) / (y1f - y0f);
                        crossings[cross_count++] = x0f + t * (x1f - x0f);
                    }
                }
            }
            
            if (cross_count < 2) continue;
            
            /* Sort */
            for (uint32_t i = 0; i < cross_count - 1; i++) {
                for (uint32_t k = i + 1; k < cross_count; k++) {
                    if (crossings[k] < crossings[i]) {
                        float tmp = crossings[i];
                        crossings[i] = crossings[k];
                        crossings[k] = tmp;
                    }
                }
            }
            
            /* Fill spans */
            for (uint32_t ci = 0; ci + 1 < cross_count; ci += 2) {
                float x_start = crossings[ci];
                float x_end = crossings[ci + 1];
                
                int ix0 = (int)floorf(x_start);
                int ix1 = (int)floorf(x_end);
                
                if (ix0 < 0) ix0 = 0;
                if (ix1 >= (int)size) ix1 = size - 1;
                
                /* Left edge */
                if (ix0 >= 0 && ix0 < (int)size) {
                    float left_cov = fminf(1.0f, (ix0 + 1) - x_start);
                    if (x_end <= ix0 + 1) left_cov = x_end - x_start;
                    coverage[ix0] += left_cov / AA_SAMPLES;
                }
                
                /* Interior */
                for (int x = ix0 + 1; x < ix1; x++) {
                    if (x >= 0 && x < (int)size) {
                        coverage[x] += 1.0f / AA_SAMPLES;
                    }
                }
                
                /* Right edge */
                if (ix1 > ix0 && ix1 >= 0 && ix1 < (int)size) {
                    float right_cov = x_end - ix1;
                    coverage[ix1] += right_cov / AA_SAMPLES;
                }
            }
        }
        
        /* Apply coverage */
        for (int x = 0; x < (int)size; x++) {
            float cov = coverage[x];
            if (cov > 0.0f) {
                if (cov > 1.0f) cov = 1.0f;
                uint8_t aa = (uint8_t)(a * cov);
                if (aa > 0) {
                    blend_pixel(buf, (y * size + x) * 4, r, g, b, aa);
                }
            }
        }
    }
    
    free(coverage);
    free(crossings);
    #undef AA_SAMPLES
}

int layer_render(const nxi_layer_t* layer, uint8_t* buf, uint32_t size) {
    if (!layer || !buf || size == 0) return -1;
    if (!layer->visible || layer->opacity == 0) return 0;
    if (layer->path_count < 3) return 0;
    
    /* Calculate offset to center layer */
    layer_bounds_t b = layer_get_bounds(layer);
    float ox = b.x;
    float oy = b.y;
    
    /* Flatten paths to subpaths with proper Bezier handling */
    subpath_t* subpaths = NULL;
    uint32_t subpath_count = 0;
    flatten_paths(layer->paths, layer->path_count, 
                  ox, oy, layer->scale, size,
                  &subpaths, &subpath_count);
    
    /* Rasterize with even-odd fill */
    rasterize_subpaths(buf, size, subpaths, subpath_count,
                       layer->fill_color, layer->opacity);
    
    /* Cleanup */
    for (uint32_t i = 0; i < subpath_count; i++) {
        free(subpaths[i].x);
        free(subpaths[i].y);
    }
    free(subpaths);
    
    return 0;
}

/* Downsample 4x for anti-aliasing */
/* Downsample 8x with weighted box filter for ultra-smooth edges */
static void downsample_8x(const uint8_t* src, uint32_t src_size,
                          uint8_t* dst, uint32_t dst_size) {
    for (uint32_t y = 0; y < dst_size; y++) {
        for (uint32_t x = 0; x < dst_size; x++) {
            uint32_t sx = x * 8;
            uint32_t sy = y * 8;
            
            /* Average 8x8 block of pixels */
            uint32_t r = 0, g = 0, b = 0, a = 0;
            for (int dy = 0; dy < 8; dy++) {
                for (int dx = 0; dx < 8; dx++) {
                    uint32_t si = ((sy + dy) * src_size + (sx + dx)) * 4;
                    r += src[si];
                    g += src[si + 1];
                    b += src[si + 2];
                    a += src[si + 3];
                }
            }
            
            uint32_t di = (y * dst_size + x) * 4;
            dst[di] = r / 64;
            dst[di + 1] = g / 64;
            dst[di + 2] = b / 64;
            dst[di + 3] = a / 64;
        }
    }
}

int layers_render_all(nxi_layer_t** layers, uint32_t count,
                      uint8_t* buf, uint32_t size) {
    if (!layers || !buf || size == 0) return -1;
    
    /* Render at 8x resolution for ultra-smooth anti-aliasing */
    uint32_t hi_size = size * 8;
    
    /* Cap at 8192 for memory limits */
    if (hi_size > 8192) hi_size = 8192;
    
    uint8_t* hi_buf = calloc(hi_size * hi_size * 4, 1);
    if (!hi_buf) {
        /* Fallback to direct render */
        memset(buf, 0, size * size * 4);
        for (uint32_t i = 0; i < count; i++) {
            if (layers[i]) layer_render(layers[i], buf, size);
        }
        return 0;
    }
    
    /* Render all layers to high-res buffer */
    for (uint32_t i = 0; i < count; i++) {
        if (layers[i]) {
            layer_render(layers[i], hi_buf, hi_size);
        }
    }
    
    /* Downsample to target buffer */
    downsample_8x(hi_buf, hi_size, buf, size);
    free(hi_buf);
    
    return 0;
}

/* Box blur - fast approximation of Gaussian (3-pass for quality) */
static void box_blur(uint8_t* buf, uint32_t size, int radius) {
    if (radius < 1) return;
    
    uint8_t* tmp = malloc(size * size * 4);
    if (!tmp) return;
    
    /* Horizontal pass */
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            int r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int dx = -radius; dx <= radius; dx++) {
                int nx = (int)x + dx;
                if (nx >= 0 && nx < (int)size) {
                    uint32_t idx = (y * size + nx) * 4;
                    r += buf[idx]; g += buf[idx+1]; b += buf[idx+2]; a += buf[idx+3];
                    count++;
                }
            }
            uint32_t idx = (y * size + x) * 4;
            tmp[idx] = r / count; tmp[idx+1] = g / count;
            tmp[idx+2] = b / count; tmp[idx+3] = a / count;
        }
    }
    
    /* Vertical pass */
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            int r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                int ny = (int)y + dy;
                if (ny >= 0 && ny < (int)size) {
                    uint32_t idx = (ny * size + x) * 4;
                    r += tmp[idx]; g += tmp[idx+1]; b += tmp[idx+2]; a += tmp[idx+3];
                    count++;
                }
            }
            uint32_t idx = (y * size + x) * 4;
            buf[idx] = r / count; buf[idx+1] = g / count;
            buf[idx+2] = b / count; buf[idx+3] = a / count;
        }
    }
    
    free(tmp);
}

/* Add glow effect (expand and blur with color) */
static void apply_glow(uint8_t* buf, uint32_t size, uint32_t glow_color, float radius) {
    uint8_t* glow_buf = calloc(size * size * 4, 1);
    if (!glow_buf) return;
    
    uint8_t gr = (glow_color >> 24) & 0xFF;
    uint8_t gg = (glow_color >> 16) & 0xFF;
    uint8_t gb = (glow_color >> 8) & 0xFF;
    
    /* Copy alpha channel and expand */
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            uint32_t idx = (y * size + x) * 4;
            if (buf[idx + 3] > 128) {
                glow_buf[idx] = gr; glow_buf[idx+1] = gg;
                glow_buf[idx+2] = gb; glow_buf[idx+3] = 255;
            }
        }
    }
    
    /* Blur the glow */
    box_blur(glow_buf, size, (int)(radius * 4));
    
    /* Composite glow under original */
    for (uint32_t i = 0; i < size * size * 4; i += 4) {
        if (glow_buf[i + 3] > 0 && buf[i + 3] < 255) {
            float ga = glow_buf[i + 3] / 255.0f * 0.5f;
            float ba = buf[i + 3] / 255.0f;
            buf[i] = (uint8_t)(buf[i] * ba + glow_buf[i] * ga * (1 - ba));
            buf[i+1] = (uint8_t)(buf[i+1] * ba + glow_buf[i+1] * ga * (1 - ba));
            buf[i+2] = (uint8_t)(buf[i+2] * ba + glow_buf[i+2] * ga * (1 - ba));
            if (buf[i + 3] < 255) buf[i + 3] = (uint8_t)(buf[i+3] + glow_buf[i+3] * 0.3f);
        }
    }
    
    free(glow_buf);
}

/* Glass effect - blur background + add highlight */
static void apply_glass(uint8_t* buf, uint32_t size, float intensity) {
    /* Blur for frosted glass look */
    box_blur(buf, size, (int)(intensity * 6));
    
    /* Add subtle white highlight at top */
    for (uint32_t y = 0; y < size / 4; y++) {
        float fade = 1.0f - (float)y / (size / 4);
        for (uint32_t x = 0; x < size; x++) {
            uint32_t idx = (y * size + x) * 4;
            if (buf[idx + 3] > 0) {
                int r = buf[idx] + (int)(fade * 30 * intensity);
                int g = buf[idx+1] + (int)(fade * 30 * intensity);
                int b = buf[idx+2] + (int)(fade * 30 * intensity);
                buf[idx] = r > 255 ? 255 : r;
                buf[idx+1] = g > 255 ? 255 : g;
                buf[idx+2] = b > 255 ? 255 : b;
            }
        }
    }
}

/* Frost effect - blur + noise texture */
static void apply_frost(uint8_t* buf, uint32_t size, float intensity) {
    box_blur(buf, size, (int)(intensity * 8));
    
    /* Add subtle noise */
    for (uint32_t i = 0; i < size * size * 4; i += 4) {
        if (buf[i + 3] > 0) {
            int noise = ((i * 17 + 31) % 21) - 10;
            noise = (int)(noise * intensity * 0.3f);
            int r = buf[i] + noise;
            int g = buf[i+1] + noise;
            int b = buf[i+2] + noise;
            buf[i] = r < 0 ? 0 : (r > 255 ? 255 : r);
            buf[i+1] = g < 0 ? 0 : (g > 255 ? 255 : g);
            buf[i+2] = b < 0 ? 0 : (b > 255 ? 255 : b);
        }
    }
}

/* Edge lighting effect */
static void apply_edge_light(uint8_t* buf, uint32_t size, float intensity) {
    for (uint32_t y = 1; y < size - 1; y++) {
        for (uint32_t x = 1; x < size - 1; x++) {
            uint32_t idx = (y * size + x) * 4;
            if (buf[idx + 3] > 0) {
                /* Sobel-like edge detection */
                int left_a = buf[((y) * size + x - 1) * 4 + 3];
                int right_a = buf[((y) * size + x + 1) * 4 + 3];
                int top_a = buf[((y - 1) * size + x) * 4 + 3];
                
                int edge = (left_a - right_a) + (top_a - buf[idx + 3]);
                int highlight = (int)(edge * intensity * 0.3f);
                
                int r = buf[idx] + highlight;
                int g = buf[idx+1] + highlight;
                int b = buf[idx+2] + highlight;
                buf[idx] = r < 0 ? 0 : (r > 255 ? 255 : r);
                buf[idx+1] = g < 0 ? 0 : (g > 255 ? 255 : g);
                buf[idx+2] = b < 0 ? 0 : (b > 255 ? 255 : b);
            }
        }
    }
}

void layer_apply_effects(uint8_t* buf, uint32_t size,
                         const nxi_layer_t* layer) {
    if (!buf || !layer || layer->effect_flags == 0) return;
    
    float intensity = layer->effects.glow_radius;  /* Reuse for general intensity */
    if (intensity < 0.1f) intensity = 1.0f;
    
    if (layer->effect_flags & NXI_EFFECT_BLUR) {
        box_blur(buf, size, (int)(intensity * 4));
    }
    
    if (layer->effect_flags & NXI_EFFECT_GLASS) {
        apply_glass(buf, size, intensity);
    }
    
    if (layer->effect_flags & NXI_EFFECT_FROST) {
        apply_frost(buf, size, intensity);
    }
    
    if (layer->effect_flags & NXI_EFFECT_GLOW) {
        apply_glow(buf, size, layer->effects.glow_color, layer->effects.glow_radius);
    }
    
    if (layer->effect_flags & NXI_EFFECT_EDGE) {
        apply_edge_light(buf, size, intensity);
    }
}
