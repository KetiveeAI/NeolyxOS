/*
 * NeolyxOS - NXGFX Vector Engine (Native 2D Math & Path Core)
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXGFX_PATH_H
#define NXGFX_PATH_H

#include <stdint.h>
#include <stdbool.h>
#include "nxgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================
 * Fundamental Geometry & Vector Math
 * ======================================================= */

typedef struct { float x; float y; } nx_vec2_t;
typedef struct { nx_vec2_t min; nx_vec2_t max; } nx_bounds_t;
typedef struct { nx_vec2_t origin; nx_vec2_t direction; } nx_ray_t;
typedef struct { nx_vec2_t p0; nx_vec2_t p1; } nx_line_segment_t;

/* Analytic Bezier API */
void nx_bezier_flatten_cubic(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, nx_vec2_t p3,
                             float tolerance, nx_vec2_t* out_points, uint32_t* inout_count, uint32_t capacity);
void nx_bezier_flatten_quad(nx_vec2_t p0, nx_vec2_t p1, nx_vec2_t p2, 
                            float tolerance, nx_vec2_t* out_points, uint32_t* inout_count, uint32_t capacity);

/* Standard math operations. External linkage to avoid standard headers. */
float nx_vec2_mag(nx_vec2_t v);
float nx_vec2_mag_sq(nx_vec2_t v);
nx_vec2_t nx_vec2_normalize(nx_vec2_t v);
float nx_vec2_dot(nx_vec2_t a, nx_vec2_t b);
float nx_vec2_cross(nx_vec2_t a, nx_vec2_t b);
float nx_dist_sq(nx_vec2_t a, nx_vec2_t b);
nx_bounds_t nx_bounds_merge(nx_bounds_t a, nx_bounds_t b);

/* =======================================================
 * Affine Transformations (3x3 Matrix)
 * ======================================================= */

typedef struct {
    float m[6]; /* [a, b, c, d, tx, ty] matches SVG standard */
} nx_transform_t;

nx_transform_t nx_transform_identity(void);
nx_transform_t nx_transform_multiply(nx_transform_t a, nx_transform_t b);
nx_transform_t nx_transform_translate(nx_transform_t t, float tx, float ty);
nx_transform_t nx_transform_scale(nx_transform_t t, float sx, float sy);
nx_transform_t nx_transform_rotate(nx_transform_t t, float radians);
nx_vec2_t nx_transform_point(nx_transform_t t, nx_vec2_t p);

/* =======================================================
 * Stroke & Styling Configurations
 * ======================================================= */

typedef enum {
    NX_LINE_CAP_BUTT = 0,
    NX_LINE_CAP_ROUND,
    NX_LINE_CAP_SQUARE
} nx_line_cap_t;

typedef enum {
    NX_LINE_JOIN_MITER = 0,
    NX_LINE_JOIN_ROUND,
    NX_LINE_JOIN_BEVEL
} nx_line_join_t;

typedef enum {
    NX_WINDING_NON_ZERO = 0,
    NX_WINDING_EVEN_ODD
} nx_winding_rule_t;

typedef struct {
    float width;
    nx_line_cap_t cap;
    nx_line_join_t join;
    float miter_limit;
    float* dash_array;
    uint32_t dash_count;
    float dash_offset;
} nx_stroke_style_t;

/* =======================================================
 * Complex Path Generation
 * ======================================================= */

#define NX_PATH_MOVE   0
#define NX_PATH_LINE   1
#define NX_PATH_QUAD   2
#define NX_PATH_CUBIC  3
#define NX_PATH_CLOSE  4

typedef struct {
    uint8_t type;
    nx_vec2_t p[3]; /* Maximum 3 points for cubic (c1, c2, end) */
} nx_path_cmd_t;

struct nx_path {
    nx_path_cmd_t* commands;
    uint32_t count;
    uint32_t capacity;
    nx_bounds_t bounds;
    bool is_closed;
};

nx_path_t* nx_path_create(void);
void nx_path_destroy(nx_path_t* path);
void nx_path_move_to(nx_path_t* path, nx_vec2_t p);
void nx_path_line_to(nx_path_t* path, nx_vec2_t p);
void nx_path_quad_to(nx_path_t* path, nx_vec2_t control, nx_vec2_t p);
void nx_path_cubic_to(nx_path_t* path, nx_vec2_t c1, nx_vec2_t c2, nx_vec2_t p);
void nx_path_arc_to(nx_path_t* path, nx_vec2_t center, float radius, float angle1, float angle2);
void nx_path_svg_arc_to(nx_path_t* path, float rx, float ry, float x_axis_rot, 
                        bool large_arc, bool sweep, nx_vec2_t end_p);
void nx_path_close(nx_path_t* path);
void nx_path_clear(nx_path_t* path);
void nx_path_transform(nx_path_t* path, nx_transform_t t);
nx_bounds_t nx_path_compute_bounds(const nx_path_t* path);

/* Flattens paths with curves into pure line segments. Returns array of points. */
nx_vec2_t* nx_path_flatten(const nx_path_t* path, float tolerance, uint32_t* out_count);

/* Converts a vector path with stroke into a fillable polygon path using stroke expansion. */
nx_path_t* nx_path_stroke_to_fill(const nx_path_t* path, const nx_stroke_style_t* style, float tolerance);

/* =======================================================
 * Active Edge Table (AET) Scanline Rasterizer
 * ======================================================= */

typedef struct nx_edge {
    int32_t y_max;
    float x;
    float dx_dy;
    int32_t winding_dir;
    struct nx_edge* next;
} nx_edge_t;

typedef struct {
    nx_edge_t** edges;
    int32_t y_min;
    int32_t y_max;
    uint32_t height;
} nx_edge_table_t;

/* The AET callback definition for platform-agnostic scanline rendering */
typedef void (*nx_scanline_span_cb_t)(void* user_data, int32_t y, int32_t x_start, int32_t x_end);

void nx_scanline_rasterize(const nx_vec2_t* points, uint32_t count, 
                           const nx_bounds_t* clip, nx_winding_rule_t rule,
                           nx_scanline_span_cb_t span_cb, void* user_data);

#ifdef __cplusplus
}
#endif
#endif
