/*
 * Rivee - Draw your imagination
 * Document Management Implementation
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../../include/rivee.h"
#include <stdlib.h>
#include <string.h>

/* ============ Memory helpers ============ */

static void *rivee_alloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

#define rivee_free(ptr) do { if (ptr) { free(ptr); (ptr) = NULL; } } while(0)

/* ============ Document ============ */

rivee_document_t *rivee_document_new(float width, float height) {
    rivee_document_t *doc = rivee_alloc(sizeof(rivee_document_t));
    if (!doc) return NULL;
    
    snprintf(doc->name, sizeof(doc->name), "Untitled");
    doc->width = width;
    doc->height = height;
    doc->background = RIVEE_COLOR_WHITE;
    doc->layers = NULL;
    doc->layer_count = 0;
    doc->active_layer = NULL;
    doc->selection = NULL;
    doc->selection_count = 0;
    doc->modified = false;
    
    return doc;
}

void rivee_document_free(rivee_document_t *doc) {
    if (!doc) return;
    
    /* Free all layers */
    rivee_layer_t *layer = doc->layers;
    while (layer) {
        rivee_layer_t *next = layer->next;
        rivee_layer_delete(doc, layer);
        layer = next;
    }
    
    rivee_free(doc);
}

int rivee_document_save(rivee_document_t *doc, const char *path) {
    if (!doc || !path) return -1;
    
    /* TODO: Implement JSON/binary serialization */
    strncpy(doc->path, path, sizeof(doc->path) - 1);
    doc->modified = false;
    
    return 0;
}

rivee_document_t *rivee_document_open(const char *path) {
    if (!path) return NULL;
    
    /* TODO: Implement loading */
    (void)path;
    
    return NULL;
}

/* ============ Layers ============ */

static uint32_t next_layer_id = 1;

rivee_layer_t *rivee_layer_new(rivee_document_t *doc, const char *name) {
    if (!doc) return NULL;
    
    rivee_layer_t *layer = rivee_alloc(sizeof(rivee_layer_t));
    if (!layer) return NULL;
    
    layer->id = next_layer_id++;
    snprintf(layer->name, sizeof(layer->name), "%s", name ? name : "Layer");
    layer->visible = true;
    layer->locked = false;
    layer->opacity = 1.0f;
    layer->shapes = NULL;
    layer->shape_count = 0;
    layer->next = NULL;
    
    /* Add to document */
    if (!doc->layers) {
        doc->layers = layer;
    } else {
        rivee_layer_t *last = doc->layers;
        while (last->next) last = last->next;
        last->next = layer;
    }
    doc->layer_count++;
    
    /* Set as active if first layer */
    if (!doc->active_layer) {
        doc->active_layer = layer;
    }
    
    return layer;
}

void rivee_layer_delete(rivee_document_t *doc, rivee_layer_t *layer) {
    if (!doc || !layer) return;
    
    /* Free shapes */
    rivee_shape_t *shape = layer->shapes;
    while (shape) {
        rivee_shape_t *next = shape->next;
        rivee_shape_free(shape);
        shape = next;
    }
    
    /* Remove from list */
    if (doc->layers == layer) {
        doc->layers = layer->next;
    } else {
        rivee_layer_t *prev = doc->layers;
        while (prev && prev->next != layer) prev = prev->next;
        if (prev) prev->next = layer->next;
    }
    doc->layer_count--;
    
    /* Update active layer */
    if (doc->active_layer == layer) {
        doc->active_layer = doc->layers;
    }
    
    rivee_free(layer);
}

/* ============ Shapes ============ */

static uint32_t next_shape_id = 1;

static rivee_shape_t *shape_alloc(rivee_shape_type_t type) {
    rivee_shape_t *shape = rivee_alloc(sizeof(rivee_shape_t));
    if (!shape) return NULL;
    
    shape->id = next_shape_id++;
    shape->type = type;
    shape->fill.type = FILL_SOLID;
    shape->fill.solid = RIVEE_COLOR_WHITE;
    shape->stroke.color = RIVEE_COLOR_BLACK;
    shape->stroke.width = 1.0f;
    shape->stroke.cap = CAP_ROUND;
    shape->stroke.join = JOIN_ROUND;
    shape->transform = RIVEE_TRANSFORM_IDENTITY;
    shape->visible = true;
    shape->locked = false;
    shape->opacity = 1.0f;
    shape->next = NULL;
    
    return shape;
}

rivee_shape_t *rivee_shape_create_rect(float x, float y, float w, float h) {
    rivee_shape_t *shape = shape_alloc(SHAPE_RECTANGLE);
    if (!shape) return NULL;
    
    shape->data.rect.origin.x = x;
    shape->data.rect.origin.y = y;
    shape->data.rect.width = w;
    shape->data.rect.height = h;
    shape->data.rect.corner_radius = 0;
    snprintf(shape->name, sizeof(shape->name), "Rectangle");
    
    return shape;
}

rivee_shape_t *rivee_shape_create_ellipse(float cx, float cy, float rx, float ry) {
    rivee_shape_t *shape = shape_alloc(SHAPE_ELLIPSE);
    if (!shape) return NULL;
    
    shape->data.ellipse.center.x = cx;
    shape->data.ellipse.center.y = cy;
    shape->data.ellipse.rx = rx;
    shape->data.ellipse.ry = ry;
    snprintf(shape->name, sizeof(shape->name), "Ellipse");
    
    return shape;
}

rivee_shape_t *rivee_shape_create_line(float x1, float y1, float x2, float y2) {
    rivee_shape_t *shape = shape_alloc(SHAPE_LINE);
    if (!shape) return NULL;
    
    shape->data.line.start.x = x1;
    shape->data.line.start.y = y1;
    shape->data.line.end.x = x2;
    shape->data.line.end.y = y2;
    snprintf(shape->name, sizeof(shape->name), "Line");
    
    return shape;
}

rivee_shape_t *rivee_shape_create_path(void) {
    rivee_shape_t *shape = shape_alloc(SHAPE_PATH);
    if (!shape) return NULL;
    
    shape->data.path.commands = NULL;
    shape->data.path.command_count = 0;
    shape->data.path.command_capacity = 0;
    snprintf(shape->name, sizeof(shape->name), "Path");
    
    return shape;
}

rivee_shape_t *rivee_shape_create_text(float x, float y, const char *text) {
    rivee_shape_t *shape = shape_alloc(SHAPE_TEXT);
    if (!shape) return NULL;
    
    shape->data.text.origin.x = x;
    shape->data.text.origin.y = y;
    shape->data.text.text = text ? strdup(text) : NULL;
    snprintf(shape->data.text.font_family, sizeof(shape->data.text.font_family), "System");
    shape->data.text.font_size = 16.0f;
    shape->data.text.bold = false;
    shape->data.text.italic = false;
    snprintf(shape->name, sizeof(shape->name), "Text");
    
    return shape;
}

void rivee_shape_add(rivee_layer_t *layer, rivee_shape_t *shape) {
    if (!layer || !shape) return;
    
    if (!layer->shapes) {
        layer->shapes = shape;
    } else {
        rivee_shape_t *last = layer->shapes;
        while (last->next) last = last->next;
        last->next = shape;
    }
    layer->shape_count++;
}

void rivee_shape_remove(rivee_layer_t *layer, rivee_shape_t *shape) {
    if (!layer || !shape) return;
    
    if (layer->shapes == shape) {
        layer->shapes = shape->next;
    } else {
        rivee_shape_t *prev = layer->shapes;
        while (prev && prev->next != shape) prev = prev->next;
        if (prev) prev->next = shape->next;
    }
    layer->shape_count--;
    shape->next = NULL;
}

void rivee_shape_free(rivee_shape_t *shape) {
    if (!shape) return;
    
    if (shape->type == SHAPE_PATH) {
        rivee_free(shape->data.path.commands);
    } else if (shape->type == SHAPE_TEXT) {
        rivee_free(shape->data.text.text);
    }
    
    rivee_free(shape);
}

/* ============ Styling ============ */

void rivee_shape_set_fill_color(rivee_shape_t *shape, rivee_color_t color) {
    if (!shape) return;
    shape->fill.type = FILL_SOLID;
    shape->fill.solid = color;
}

void rivee_shape_set_fill_gradient(rivee_shape_t *shape, rivee_gradient_t *gradient) {
    if (!shape || !gradient) return;
    shape->fill.type = FILL_GRADIENT;
    shape->fill.gradient = *gradient;
}

void rivee_shape_set_stroke(rivee_shape_t *shape, rivee_color_t color, float width) {
    if (!shape) return;
    shape->stroke.color = color;
    shape->stroke.width = width;
}

/* ============ Path Operations ============ */

static void path_ensure_capacity(rivee_path_t *path, int needed) {
    if (path->command_count + needed <= path->command_capacity) return;
    
    int new_cap = path->command_capacity == 0 ? 16 : path->command_capacity * 2;
    while (new_cap < path->command_count + needed) new_cap *= 2;
    
    path->commands = realloc(path->commands, new_cap * sizeof(rivee_path_cmd_t));
    path->command_capacity = new_cap;
}

void rivee_path_move_to(rivee_path_t *path, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path, 1);
    
    rivee_path_cmd_t *cmd = &path->commands[path->command_count++];
    cmd->type = CMD_MOVE;
    cmd->points[0] = (rivee_point_t){x, y};
}

void rivee_path_line_to(rivee_path_t *path, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path, 1);
    
    rivee_path_cmd_t *cmd = &path->commands[path->command_count++];
    cmd->type = CMD_LINE;
    cmd->points[0] = (rivee_point_t){x, y};
}

void rivee_path_curve_to(rivee_path_t *path, float cx1, float cy1, float cx2, float cy2, float x, float y) {
    if (!path) return;
    path_ensure_capacity(path, 1);
    
    rivee_path_cmd_t *cmd = &path->commands[path->command_count++];
    cmd->type = CMD_CURVE;
    cmd->points[0] = (rivee_point_t){cx1, cy1};
    cmd->points[1] = (rivee_point_t){cx2, cy2};
    cmd->points[2] = (rivee_point_t){x, y};
}

void rivee_path_close(rivee_path_t *path) {
    if (!path) return;
    path_ensure_capacity(path, 1);
    
    rivee_path_cmd_t *cmd = &path->commands[path->command_count++];
    cmd->type = CMD_CLOSE;
}

/* ============ Selection ============ */

void rivee_select(rivee_document_t *doc, rivee_shape_t *shape) {
    if (!doc || !shape) return;
    
    /* For now, single selection */
    doc->selection = shape;
    doc->selection_count = 1;
}

void rivee_select_all(rivee_document_t *doc) {
    if (!doc || !doc->active_layer) return;
    
    doc->selection = doc->active_layer->shapes;
    doc->selection_count = doc->active_layer->shape_count;
}

void rivee_deselect_all(rivee_document_t *doc) {
    if (!doc) return;
    doc->selection = NULL;
    doc->selection_count = 0;
}

/* ============ Rendering (stub - uses NXRender) ============ */

void rivee_render_document(rivee_document_t *doc, void *surface) {
    if (!doc || !surface) return;
    
    /* Clear background - to be implemented by NXRender */
    (void)doc->background;
    
    /* Render each layer */
    for (rivee_layer_t *layer = doc->layers; layer; layer = layer->next) {
        if (!layer->visible) continue;
        
        for (rivee_shape_t *shape = layer->shapes; shape; shape = shape->next) {
            if (shape->visible) {
                rivee_render_shape(shape, surface);
            }
        }
    }
}

void rivee_render_shape(rivee_shape_t *shape, void *surface) {
    if (!shape || !surface) return;
    
    /* Rendering is delegated to NXRender based on shape type */
    /* This is a placeholder - real implementation calls nxrender_* functions */
    (void)shape->type;
}

/* ============ Export (stubs) ============ */

int rivee_export_nxi(rivee_document_t *doc, const char *path) {
    if (!doc || !path) return -1;
    
    /* TODO: Implement NXI vector export */
    return 0;
}

int rivee_export_png(rivee_document_t *doc, const char *path, int scale) {
    if (!doc || !path) return -1;
    
    /* TODO: Implement PNG raster export */
    (void)scale;
    return 0;
}

int rivee_export_svg(rivee_document_t *doc, const char *path) {
    if (!doc || !path) return -1;
    
    /* TODO: Implement SVG export */
    return 0;
}
