/*
 * Rivee - Draw your imagination
 * Professional Vector Graphics Editor for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef RIVEE_H
#define RIVEE_H

#include <stdint.h>
#include <stdbool.h>

/* ============ Rivee Core Types ============ */

/* 2D Point */
typedef struct {
    float x;
    float y;
} rivee_point_t;

/* Color (RGBA) */
typedef struct {
    uint8_t r, g, b, a;
} rivee_color_t;

/* Gradient stop */
typedef struct {
    float position;     /* 0.0 to 1.0 */
    rivee_color_t color;
} rivee_gradient_stop_t;

/* Gradient */
typedef struct {
    enum { GRADIENT_LINEAR, GRADIENT_RADIAL } type;
    rivee_point_t start;
    rivee_point_t end;
    rivee_gradient_stop_t stops[8];
    int stop_count;
} rivee_gradient_t;

/* Fill style */
typedef struct {
    enum { FILL_NONE, FILL_SOLID, FILL_GRADIENT } type;
    union {
        rivee_color_t solid;
        rivee_gradient_t gradient;
    };
} rivee_fill_t;

/* Stroke style */
typedef struct {
    rivee_color_t color;
    float width;
    enum { CAP_BUTT, CAP_ROUND, CAP_SQUARE } cap;
    enum { JOIN_MITER, JOIN_ROUND, JOIN_BEVEL } join;
    float dash_array[8];
    int dash_count;
} rivee_stroke_t;

/* Transform matrix (2D affine) */
typedef struct {
    float a, b, c, d, tx, ty;
} rivee_transform_t;

/* ============ Shape Types ============ */

typedef enum {
    SHAPE_RECTANGLE,
    SHAPE_ELLIPSE,
    SHAPE_LINE,
    SHAPE_PATH,
    SHAPE_TEXT,
    SHAPE_GROUP
} rivee_shape_type_t;

/* Path command */
typedef struct {
    enum { CMD_MOVE, CMD_LINE, CMD_CURVE, CMD_CLOSE } type;
    rivee_point_t points[3];  /* Max 3 control points */
} rivee_path_cmd_t;

/* Path data */
typedef struct {
    rivee_path_cmd_t *commands;
    int command_count;
    int command_capacity;
} rivee_path_t;

/* Rectangle shape */
typedef struct {
    rivee_point_t origin;
    float width;
    float height;
    float corner_radius;
} rivee_rect_t;

/* Ellipse shape */
typedef struct {
    rivee_point_t center;
    float rx;
    float ry;
} rivee_ellipse_t;

/* Line shape */
typedef struct {
    rivee_point_t start;
    rivee_point_t end;
} rivee_line_t;

/* Text shape */
typedef struct {
    rivee_point_t origin;
    char *text;
    char font_family[64];
    float font_size;
    bool bold;
    bool italic;
} rivee_text_t;

/* Generic shape */
typedef struct rivee_shape {
    uint32_t id;
    char name[64];
    rivee_shape_type_t type;
    
    union {
        rivee_rect_t rect;
        rivee_ellipse_t ellipse;
        rivee_line_t line;
        rivee_path_t path;
        rivee_text_t text;
    } data;
    
    rivee_fill_t fill;
    rivee_stroke_t stroke;
    rivee_transform_t transform;
    
    bool visible;
    bool locked;
    float opacity;
    
    struct rivee_shape *next;
} rivee_shape_t;

/* ============ Layer ============ */

typedef struct rivee_layer {
    uint32_t id;
    char name[64];
    bool visible;
    bool locked;
    float opacity;
    
    rivee_shape_t *shapes;
    int shape_count;
    
    struct rivee_layer *next;
} rivee_layer_t;

/* ============ Document ============ */

typedef struct {
    char name[128];
    char path[256];
    
    float width;
    float height;
    rivee_color_t background;
    
    rivee_layer_t *layers;
    int layer_count;
    
    rivee_layer_t *active_layer;
    rivee_shape_t *selection;
    int selection_count;
    
    bool modified;
} rivee_document_t;

/* ============ Tools ============ */

typedef enum {
    TOOL_SELECT,
    TOOL_RECTANGLE,
    TOOL_ELLIPSE,
    TOOL_LINE,
    TOOL_PEN,
    TOOL_TEXT,
    TOOL_ZOOM,
    TOOL_PAN,
    TOOL_EYEDROPPER
} rivee_tool_t;

/* ============ Rivee Engine API ============ */

/* Document management */
rivee_document_t *rivee_document_new(float width, float height);
rivee_document_t *rivee_document_open(const char *path);
int rivee_document_save(rivee_document_t *doc, const char *path);
void rivee_document_free(rivee_document_t *doc);

/* Layer operations */
rivee_layer_t *rivee_layer_new(rivee_document_t *doc, const char *name);
void rivee_layer_delete(rivee_document_t *doc, rivee_layer_t *layer);
void rivee_layer_move(rivee_document_t *doc, rivee_layer_t *layer, int new_index);

/* Shape operations */
rivee_shape_t *rivee_shape_create_rect(float x, float y, float w, float h);
rivee_shape_t *rivee_shape_create_ellipse(float cx, float cy, float rx, float ry);
rivee_shape_t *rivee_shape_create_line(float x1, float y1, float x2, float y2);
rivee_shape_t *rivee_shape_create_path(void);
rivee_shape_t *rivee_shape_create_text(float x, float y, const char *text);

void rivee_shape_add(rivee_layer_t *layer, rivee_shape_t *shape);
void rivee_shape_remove(rivee_layer_t *layer, rivee_shape_t *shape);
void rivee_shape_free(rivee_shape_t *shape);

/* Path building */
void rivee_path_move_to(rivee_path_t *path, float x, float y);
void rivee_path_line_to(rivee_path_t *path, float x, float y);
void rivee_path_curve_to(rivee_path_t *path, float cx1, float cy1, float cx2, float cy2, float x, float y);
void rivee_path_close(rivee_path_t *path);

/* Styling */
void rivee_shape_set_fill_color(rivee_shape_t *shape, rivee_color_t color);
void rivee_shape_set_fill_gradient(rivee_shape_t *shape, rivee_gradient_t *gradient);
void rivee_shape_set_stroke(rivee_shape_t *shape, rivee_color_t color, float width);

/* Transform */
void rivee_shape_translate(rivee_shape_t *shape, float dx, float dy);
void rivee_shape_scale(rivee_shape_t *shape, float sx, float sy);
void rivee_shape_rotate(rivee_shape_t *shape, float angle);

/* Selection */
void rivee_select(rivee_document_t *doc, rivee_shape_t *shape);
void rivee_select_all(rivee_document_t *doc);
void rivee_deselect_all(rivee_document_t *doc);

/* Rendering (uses NXRender) */
void rivee_render_document(rivee_document_t *doc, void *nxrender_surface);
void rivee_render_shape(rivee_shape_t *shape, void *nxrender_surface);

/* Export */
int rivee_export_nxi(rivee_document_t *doc, const char *path);
int rivee_export_png(rivee_document_t *doc, const char *path, int scale);
int rivee_export_svg(rivee_document_t *doc, const char *path);

/* ============ Colors ============ */

#define RIVEE_COLOR_BLACK   ((rivee_color_t){0, 0, 0, 255})
#define RIVEE_COLOR_WHITE   ((rivee_color_t){255, 255, 255, 255})
#define RIVEE_COLOR_RED     ((rivee_color_t){255, 0, 0, 255})
#define RIVEE_COLOR_GREEN   ((rivee_color_t){0, 255, 0, 255})
#define RIVEE_COLOR_BLUE    ((rivee_color_t){0, 0, 255, 255})
#define RIVEE_COLOR_NONE    ((rivee_color_t){0, 0, 0, 0})

#define RIVEE_COLOR_RGB(r, g, b)     ((rivee_color_t){r, g, b, 255})
#define RIVEE_COLOR_RGBA(r, g, b, a) ((rivee_color_t){r, g, b, a})

/* ============ Transform helpers ============ */

#define RIVEE_TRANSFORM_IDENTITY ((rivee_transform_t){1, 0, 0, 1, 0, 0})

#endif /* RIVEE_H */
