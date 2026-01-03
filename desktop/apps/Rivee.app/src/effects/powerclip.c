/*
 * Rivee - PowerClip Implementation
 * Clips content inside container shapes
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../include/effects.h"
#include "../include/rivee.h"
#include <stdlib.h>
#include <string.h>

/* ============ PowerClip ============ */

powerclip_t *powerclip_create(rivee_shape_t *container) {
    if (!container) return NULL;
    
    powerclip_t *clip = calloc(1, sizeof(powerclip_t));
    if (!clip) return NULL;
    
    clip->container = container;
    clip->contents = NULL;
    clip->content_count = 0;
    clip->edit_mode = false;
    
    return clip;
}

void powerclip_add_content(powerclip_t *clip, rivee_shape_t *content) {
    if (!clip || !content) return;
    
    /* Add to front of list */
    content->next = clip->contents;
    clip->contents = content;
    clip->content_count++;
}

void powerclip_remove_content(powerclip_t *clip, rivee_shape_t *content) {
    if (!clip || !content) return;
    
    if (clip->contents == content) {
        clip->contents = content->next;
        clip->content_count--;
        content->next = NULL;
        return;
    }
    
    rivee_shape_t *prev = clip->contents;
    while (prev && prev->next != content) {
        prev = prev->next;
    }
    
    if (prev) {
        prev->next = content->next;
        clip->content_count--;
        content->next = NULL;
    }
}

void powerclip_extract_all(powerclip_t *clip, rivee_layer_t *target) {
    if (!clip || !target) return;
    
    while (clip->contents) {
        rivee_shape_t *content = clip->contents;
        powerclip_remove_content(clip, content);
        rivee_shape_add(target, content);
    }
}

void powerclip_enter_edit(powerclip_t *clip) {
    if (clip) clip->edit_mode = true;
}

void powerclip_exit_edit(powerclip_t *clip) {
    if (clip) clip->edit_mode = false;
}

void powerclip_free(powerclip_t *clip) {
    if (!clip) return;
    
    /* Free contents */
    rivee_shape_t *content = clip->contents;
    while (content) {
        rivee_shape_t *next = content->next;
        rivee_shape_free(content);
        content = next;
    }
    
    /* Note: container is NOT freed (owned by document) */
    free(clip);
}

/* ============ Magic Fill ============ */

/*
 * Flood fill algorithm to find connected area,
 * then traces boundary to create vector shape.
 */

typedef struct {
    int x, y;
} stack_point_t;

static bool colors_similar(rivee_color_t a, rivee_color_t b, float tolerance) {
    float dr = ((float)a.r - (float)b.r) / 255.0f;
    float dg = ((float)a.g - (float)b.g) / 255.0f;
    float db = ((float)a.b - (float)b.b) / 255.0f;
    float dist = dr*dr + dg*dg + db*db;
    return dist <= tolerance * tolerance * 3;
}

rivee_shape_t *magic_fill(rivee_document_t *doc, float click_x, float click_y,
                          const magic_fill_options_t *options) {
    if (!doc || !options) return NULL;
    
    /* Get pixel buffer from rendered document */
    /* (This would be from the actual renderer) */
    int width = (int)doc->width;
    int height = (int)doc->height;
    
    /* Allocate visited bitmap */
    bool *visited = calloc(width * height, sizeof(bool));
    if (!visited) return NULL;
    
    /* Get target color at click point */
    int click_ix = (int)click_x;
    int click_iy = (int)click_y;
    if (click_ix < 0 || click_ix >= width || click_iy < 0 || click_iy >= height) {
        free(visited);
        return NULL;
    }
    
    /* Would get actual pixel color - using placeholder */
    rivee_color_t target_color = RIVEE_COLOR_WHITE;
    
    /* Flood fill to find region */
    stack_point_t *stack = malloc(width * height * sizeof(stack_point_t));
    int stack_size = 0;
    
    stack[stack_size++] = (stack_point_t){click_ix, click_iy};
    visited[click_iy * width + click_ix] = true;
    
    /* Boundary points */
    rivee_point_t *boundary = malloc(width * height * sizeof(rivee_point_t));
    int boundary_count = 0;
    
    while (stack_size > 0) {
        stack_point_t p = stack[--stack_size];
        
        /* Check if this is a boundary point */
        bool is_boundary = false;
        static const int dx[] = {1, 0, -1, 0};
        static const int dy[] = {0, 1, 0, -1};
        
        for (int d = 0; d < 4; d++) {
            int nx = p.x + dx[d];
            int ny = p.y + dy[d];
            
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                is_boundary = true;
                continue;
            }
            
            int ni = ny * width + nx;
            
            /* Would check actual pixel color here */
            bool similar = true; /* Placeholder */
            (void)target_color;
            (void)options->tolerance;
            
            if (similar && !visited[ni]) {
                visited[ni] = true;
                stack[stack_size++] = (stack_point_t){nx, ny};
            } else if (!similar) {
                is_boundary = true;
            }
        }
        
        if (is_boundary && boundary_count < width * height) {
            boundary[boundary_count++] = (rivee_point_t){(float)p.x, (float)p.y};
        }
    }
    
    free(stack);
    free(visited);
    
    if (boundary_count < 3) {
        free(boundary);
        return NULL;
    }
    
    /* Sort boundary points to form contour */
    /* (Simplified - would use proper contour tracing) */
    
    /* Create shape from boundary */
    rivee_shape_t *shape = rivee_shape_create_path();
    if (!shape) {
        free(boundary);
        return NULL;
    }
    
    rivee_path_move_to(&shape->data.path, boundary[0].x, boundary[0].y);
    for (int i = 1; i < boundary_count; i++) {
        rivee_path_line_to(&shape->data.path, boundary[i].x, boundary[i].y);
    }
    rivee_path_close(&shape->data.path);
    
    /* Apply fill */
    shape->fill = options->fill;
    
    free(boundary);
    return shape;
}

/* ============ LiveSketch ============ */

rivee_shape_t *livesketch_process(rivee_point_t *points, int point_count,
                                   const livesketch_options_t *options) {
    if (!points || point_count < 2) return NULL;
    
    rivee_shape_t *shape = rivee_shape_create_path();
    if (!shape) return NULL;
    
    /* Check for shape recognition */
    if (options->shape_recognition) {
        /* Detect basic shapes: line, rectangle, ellipse, triangle */
        
        /* Calculate bounding box */
        float min_x = points[0].x, max_x = points[0].x;
        float min_y = points[0].y, max_y = points[0].y;
        
        for (int i = 1; i < point_count; i++) {
            if (points[i].x < min_x) min_x = points[i].x;
            if (points[i].x > max_x) max_x = points[i].x;
            if (points[i].y < min_y) min_y = points[i].y;
            if (points[i].y > max_y) max_y = points[i].y;
        }
        
        float width = max_x - min_x;
        float height = max_y - min_y;
        
        /* Check if it's a closed shape */
        float close_dist = (points[0].x - points[point_count-1].x) * 
                           (points[0].x - points[point_count-1].x) +
                           (points[0].y - points[point_count-1].y) * 
                           (points[0].y - points[point_count-1].y);
        
        bool is_closed = close_dist < (width * width + height * height) * 0.01f;
        
        if (is_closed && width > 10 && height > 10) {
            /* Check for rectangle (corners near bounding box corners) */
            /* Check for ellipse (points near ellipse formula) */
            /* For now, just smooth the path */
        }
    }
    
    /* Apply smoothing using Catmull-Rom spline */
    rivee_path_move_to(&shape->data.path, points[0].x, points[0].y);
    
    for (int i = 1; i < point_count; i++) {
        /* Simplified - just line segments with smoothing factor */
        float t = options->smoothing;
        
        if (i > 0 && i < point_count - 1) {
            /* Calculate control points for smooth curve */
            float cx = points[i].x + t * (points[i].x - points[i-1].x);
            float cy = points[i].y + t * (points[i].y - points[i-1].y);
            
            rivee_path_curve_to(&shape->data.path,
                                points[i-1].x, points[i-1].y,
                                cx, cy,
                                points[i].x, points[i].y);
        } else {
            rivee_path_line_to(&shape->data.path, points[i].x, points[i].y);
        }
    }
    
    return shape;
}

/* ============ Smart Carve ============ */

void smart_carve_apply(rivee_shape_t *shape, rivee_point_t *stroke, int point_count,
                       const smart_carve_options_t *options) {
    if (!shape || !stroke || point_count < 2 || shape->type != SHAPE_PATH) return;
    
    rivee_path_t *path = &shape->data.path;
    
    /* For each path point, check if it's near the carve stroke */
    for (int i = 0; i < path->command_count; i++) {
        rivee_path_cmd_t *cmd = &path->commands[i];
        if (cmd->type == CMD_CLOSE) continue;
        
        rivee_point_t *pt = &cmd->points[cmd->type == CMD_CURVE ? 2 : 0];
        
        /* Find nearest point on stroke */
        float min_dist = 1e10f;
        rivee_point_t nearest = stroke[0];
        
        for (int j = 0; j < point_count; j++) {
            float dx = pt->x - stroke[j].x;
            float dy = pt->y - stroke[j].y;
            float dist = dx * dx + dy * dy;
            
            if (dist < min_dist) {
                min_dist = dist;
                nearest = stroke[j];
            }
        }
        
        min_dist = sqrtf(min_dist);
        
        /* Apply carve if within radius */
        if (min_dist < options->radius) {
            float factor = 1.0f - (min_dist / options->radius);
            factor *= options->strength;
            
            /* Push point toward stroke */
            pt->x += (nearest.x - pt->x) * factor;
            pt->y += (nearest.y - pt->y) * factor;
        }
    }
}
