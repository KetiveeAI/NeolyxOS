/*
 * Rivee - Image Tracing Engine
 * Converts bitmap images to vector paths
 * 
 * Algorithm:
 * 1. Color quantization (reduce colors)
 * 2. Edge detection (find boundaries)
 * 3. Path tracing (follow edges)
 * 4. Curve fitting (Bezier optimization)
 * 5. Path simplification (reduce nodes)
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../include/effects.h"
#include "../include/rivee.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============ Internal Structures ============ */

typedef struct {
    uint8_t r, g, b;
} rgb_t;

typedef struct {
    int x, y;
} point_i;

typedef struct edge_node {
    int x, y;
    int direction;  /* 0=right, 1=down, 2=left, 3=up */
    struct edge_node *next;
} edge_node_t;

/* ============ Color Operations ============ */

static inline int color_distance(rgb_t a, rgb_t b) {
    int dr = (int)a.r - (int)b.r;
    int dg = (int)a.g - (int)b.g;
    int db = (int)a.b - (int)b.b;
    return dr * dr + dg * dg + db * db;
}

static rgb_t get_pixel(const uint8_t *pixels, int width, int x, int y) {
    int idx = (y * width + x) * 4;
    return (rgb_t){pixels[idx], pixels[idx + 1], pixels[idx + 2]};
}

/* ============ Color Quantization ============ */

typedef struct {
    rgb_t colors[256];
    int count;
} palette_t;

static palette_t *quantize_colors(const uint8_t *pixels, int width, int height, int max_colors) {
    palette_t *palette = calloc(1, sizeof(palette_t));
    if (!palette) return NULL;
    
    /* Simple median cut quantization */
    /* For production, use octree or k-means */
    
    /* Sample colors */
    int sample_step = 4;
    int color_counts[256 * 256 * 256 / 8] = {0};
    
    for (int y = 0; y < height; y += sample_step) {
        for (int x = 0; x < width; x += sample_step) {
            rgb_t c = get_pixel(pixels, width, x, y);
            int idx = ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3);
            color_counts[idx]++;
        }
    }
    
    /* Find top colors */
    for (int i = 0; i < max_colors && palette->count < max_colors; i++) {
        int max_count = 0;
        int max_idx = -1;
        
        for (int j = 0; j < (256 * 256 * 256 / 8); j++) {
            if (color_counts[j] > max_count) {
                max_count = color_counts[j];
                max_idx = j;
            }
        }
        
        if (max_idx >= 0) {
            palette->colors[palette->count++] = (rgb_t){
                ((max_idx >> 10) & 31) << 3,
                ((max_idx >> 5) & 31) << 3,
                (max_idx & 31) << 3
            };
            color_counts[max_idx] = 0;
        }
    }
    
    return palette;
}

static int nearest_palette_color(const palette_t *palette, rgb_t color) {
    int best_idx = 0;
    int best_dist = color_distance(palette->colors[0], color);
    
    for (int i = 1; i < palette->count; i++) {
        int dist = color_distance(palette->colors[i], color);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    
    return best_idx;
}

/* ============ Edge Detection ============ */

static int *create_color_map(const uint8_t *pixels, int width, int height, 
                              const palette_t *palette) {
    int *map = malloc(width * height * sizeof(int));
    if (!map) return NULL;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            rgb_t c = get_pixel(pixels, width, x, y);
            map[y * width + x] = nearest_palette_color(palette, c);
        }
    }
    
    return map;
}

/* ============ Path Tracing ============ */

typedef struct {
    rivee_point_t *points;
    int count;
    int capacity;
} point_list_t;

static void add_point(point_list_t *list, float x, float y) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->points = realloc(list->points, list->capacity * sizeof(rivee_point_t));
    }
    list->points[list->count++] = (rivee_point_t){x, y};
}

static rivee_shape_t *trace_color_region(const int *color_map, int width, int height,
                                          int color_idx, bool *visited) {
    /* Find starting point for this color */
    int start_x = -1, start_y = -1;
    
    for (int y = 0; y < height && start_x < 0; y++) {
        for (int x = 0; x < width; x++) {
            if (color_map[y * width + x] == color_idx && !visited[y * width + x]) {
                /* Check if this is an edge pixel */
                bool is_edge = (x == 0 || color_map[y * width + x - 1] != color_idx) ||
                               (y == 0 || color_map[(y-1) * width + x] != color_idx) ||
                               (x == width-1 || color_map[y * width + x + 1] != color_idx) ||
                               (y == height-1 || color_map[(y+1) * width + x] != color_idx);
                if (is_edge) {
                    start_x = x;
                    start_y = y;
                    break;
                }
            }
        }
    }
    
    if (start_x < 0) return NULL;
    
    /* Trace the boundary */
    point_list_t contour = {0};
    
    int x = start_x, y = start_y;
    int dir = 0; /* Start going right */
    int steps = 0;
    int max_steps = width * height * 4;
    
    do {
        add_point(&contour, (float)x, (float)y);
        visited[y * width + x] = true;
        
        /* Try to turn right, then straight, then left, then back */
        static const int dx[] = {1, 0, -1, 0};
        static const int dy[] = {0, 1, 0, -1};
        
        bool found = false;
        for (int turn = -1; turn <= 2 && !found; turn++) {
            int new_dir = (dir + turn + 4) % 4;
            int nx = x + dx[new_dir];
            int ny = y + dy[new_dir];
            
            if (nx >= 0 && nx < width && ny >= 0 && ny < height &&
                color_map[ny * width + nx] == color_idx) {
                x = nx;
                y = ny;
                dir = new_dir;
                found = true;
            }
        }
        
        if (!found) break;
        steps++;
    } while ((x != start_x || y != start_y) && steps < max_steps);
    
    if (contour.count < 3) {
        free(contour.points);
        return NULL;
    }
    
    /* Convert to path */
    rivee_shape_t *shape = rivee_shape_create_path();
    if (!shape) {
        free(contour.points);
        return NULL;
    }
    
    rivee_path_move_to(&shape->data.path, contour.points[0].x, contour.points[0].y);
    
    for (int i = 1; i < contour.count; i++) {
        rivee_path_line_to(&shape->data.path, contour.points[i].x, contour.points[i].y);
    }
    
    rivee_path_close(&shape->data.path);
    free(contour.points);
    
    return shape;
}

/* ============ Public API ============ */

trace_options_t trace_options_default(trace_mode_t mode) {
    trace_options_t opts = {
        .mode = mode,
        .detail_level = 50,
        .smoothing = 50,
        .corner_smoothness = 50,
        .color_count = 16,
        .ignore_white = true,
        .merge_adjacent = true,
        .remove_overlap = false,
        .min_area = 4.0f
    };
    
    switch (mode) {
        case TRACE_OUTLINE:
            opts.color_count = 2;
            break;
        case TRACE_SILHOUETTE:
            opts.color_count = 1;
            break;
        case TRACE_DETAILED:
            opts.color_count = 32;
            opts.detail_level = 80;
            break;
        case TRACE_HIGHQUALITY:
            opts.color_count = 64;
            opts.detail_level = 90;
            opts.smoothing = 70;
            break;
        default:
            break;
    }
    
    return opts;
}

trace_result_t *image_trace(const uint8_t *pixels, int width, int height,
                            const trace_options_t *options) {
    if (!pixels || width <= 0 || height <= 0) return NULL;
    
    trace_result_t *result = calloc(1, sizeof(trace_result_t));
    if (!result) return NULL;
    
    /* Step 1: Color quantization */
    palette_t *palette = quantize_colors(pixels, width, height, options->color_count);
    if (!palette) {
        free(result);
        return NULL;
    }
    
    /* Step 2: Create color map */
    int *color_map = create_color_map(pixels, width, height, palette);
    if (!color_map) {
        free(palette);
        free(result);
        return NULL;
    }
    
    /* Step 3: Trace each color region */
    bool *visited = calloc(width * height, sizeof(bool));
    rivee_shape_t *shapes = NULL;
    rivee_shape_t *last_shape = NULL;
    
    for (int c = 0; c < palette->count; c++) {
        /* Skip white if requested */
        if (options->ignore_white) {
            rgb_t col = palette->colors[c];
            if (col.r > 240 && col.g > 240 && col.b > 240) continue;
        }
        
        /* Trace all regions of this color */
        memset(visited, 0, width * height * sizeof(bool));
        
        rivee_shape_t *shape;
        while ((shape = trace_color_region(color_map, width, height, c, visited)) != NULL) {
            /* Set fill color */
            rgb_t col = palette->colors[c];
            rivee_shape_set_fill_color(shape, RIVEE_COLOR_RGB(col.r, col.g, col.b));
            shape->stroke.width = 0; /* No stroke */
            
            /* Add to result list */
            if (!shapes) {
                shapes = shape;
            } else {
                last_shape->next = shape;
            }
            last_shape = shape;
            result->shape_count++;
            result->node_count += shape->data.path.command_count;
        }
    }
    
    result->shapes = shapes;
    result->color_count = palette->count;
    
    free(visited);
    free(color_map);
    free(palette);
    
    return result;
}

trace_result_t *image_trace_preview(const uint8_t *pixels, int width, int height,
                                     const trace_options_t *options) {
    /* For preview, use lower resolution */
    trace_options_t preview_opts = *options;
    preview_opts.detail_level = options->detail_level / 2;
    preview_opts.color_count = options->color_count / 2;
    if (preview_opts.color_count < 2) preview_opts.color_count = 2;
    
    return image_trace(pixels, width, height, &preview_opts);
}

void trace_result_free(trace_result_t *result) {
    if (!result) return;
    
    rivee_shape_t *shape = result->shapes;
    while (shape) {
        rivee_shape_t *next = shape->next;
        rivee_shape_free(shape);
        shape = next;
    }
    
    free(result);
}
