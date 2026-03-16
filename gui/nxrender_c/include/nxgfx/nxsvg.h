/*
 * NXSVG - Minimal SVG Parser for NXRender
 * 
 * Parses SVG path commands (M, L, C, Q, A, Z) and renders using NXGFX primitives.
 * Designed for icon rendering with anti-aliased output.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXSVG_H
#define NXSVG_H

#include "nxgfx/nxgfx.h"
#include <stdint.h>
#include <stdbool.h>

/* SVG path commands */
typedef enum {
    NXSVG_CMD_MOVE,       /* M x,y */
    NXSVG_CMD_LINE,       /* L x,y */
    NXSVG_CMD_CUBIC,      /* C x1,y1 x2,y2 x,y */
    NXSVG_CMD_QUAD,       /* Q x1,y1 x,y */
    NXSVG_CMD_ARC,        /* A rx,ry rotation large-arc sweep x,y */
    NXSVG_CMD_CLOSE       /* Z */
} nxsvg_cmd_type_t;

typedef struct {
    nxsvg_cmd_type_t type;
    float x, y;           /* End point */
    float x1, y1;         /* Control point 1 (cubic/quad) */
    float x2, y2;         /* Control point 2 (cubic only) */
    float rx, ry;         /* Arc radii */
    float rotation;       /* Arc rotation */
    bool large_arc;       /* Arc large-arc flag */
    bool sweep;           /* Arc sweep flag */
} nxsvg_cmd_t;

typedef struct {
    nxsvg_cmd_t* commands;
    uint32_t count;
    uint32_t capacity;
    nx_color_t fill;
    nx_color_t stroke;
    float stroke_width;
    float width, height;
    float viewbox_x, viewbox_y, viewbox_w, viewbox_h;
    int gradient_id;      /* Index into gradient table, -1 if solid */
    float transform[6];   /* Affine transform [a,b,c,d,e,f] */
    bool has_transform;
} nxsvg_path_t;

/* Gradient stop */
typedef struct {
    float offset;         /* 0.0-1.0 */
    nx_color_t color;
} nxsvg_gradient_stop_t;

/* Linear/Radial gradient */
typedef struct {
    char id[64];
    bool is_radial;
    /* Linear: line from (x1,y1) to (x2,y2) */
    float x1, y1, x2, y2;
    /* Radial: center (cx,cy), radius r, focal (fx,fy) */
    float cx, cy, r, fx, fy;
    nxsvg_gradient_stop_t stops[16];
    uint32_t stop_count;
} nxsvg_gradient_t;

typedef struct {
    nxsvg_path_t* paths;
    uint32_t path_count;
    float width, height;
    nxsvg_gradient_t* gradients;
    uint32_t gradient_count;
} nxsvg_image_t;

/* Parse SVG from file */
nxsvg_image_t* nxsvg_load(const char* path);

/* Parse SVG from memory */
nxsvg_image_t* nxsvg_parse(const char* svg_data, size_t len);

/* Render SVG to context */
void nxsvg_render(nx_context_t* ctx, const nxsvg_image_t* svg, 
                  nx_point_t pos, float scale);

/* Free SVG image */
void nxsvg_free(nxsvg_image_t* svg);

#endif /* NXSVG_H */
