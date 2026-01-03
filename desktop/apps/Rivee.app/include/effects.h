/*
 * Rivee - Effects API
 * PowerClip, Image Trace, Magic Fill, and more
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef RIVEE_EFFECTS_H
#define RIVEE_EFFECTS_H

#include "rivee.h"

/* ============ PowerClip ============ */
/* 
 * PowerClip places content inside a container shape.
 * Like CorelDRAW's PowerClip feature.
 */

typedef struct {
    rivee_shape_t *container;    /* The shape that clips */
    rivee_shape_t *contents;     /* Content inside (linked list) */
    int content_count;
    bool edit_mode;              /* If true, editing contents */
} powerclip_t;

/* Create PowerClip - clips contents inside container */
powerclip_t *powerclip_create(rivee_shape_t *container);
void powerclip_add_content(powerclip_t *clip, rivee_shape_t *content);
void powerclip_remove_content(powerclip_t *clip, rivee_shape_t *content);
void powerclip_extract_all(powerclip_t *clip, rivee_layer_t *target);
void powerclip_enter_edit(powerclip_t *clip);
void powerclip_exit_edit(powerclip_t *clip);
void powerclip_free(powerclip_t *clip);

/* ============ Image Trace ============ */
/*
 * Converts bitmap images to vector paths.
 * Like CorelDRAW's Trace Bitmap feature.
 */

typedef enum {
    TRACE_OUTLINE,       /* Single color outline */
    TRACE_CENTERLINE,    /* Centerline for line art */
    TRACE_SILHOUETTE,    /* Black/white silhouette */
    TRACE_DETAILED,      /* Detailed logo trace */
    TRACE_CLIPART,       /* Simple clipart */
    TRACE_LINEART,       /* Technical line art */
    TRACE_LOWQUALITY,    /* Low quality image */
    TRACE_HIGHQUALITY,   /* High quality photo */
} trace_mode_t;

typedef struct {
    trace_mode_t mode;
    int detail_level;        /* 1-100, higher = more detail */
    int smoothing;           /* 1-100, higher = smoother curves */
    int corner_smoothness;   /* 1-100, corner detection */
    int color_count;         /* Max colors for multi-color trace */
    bool ignore_white;       /* Ignore white background */
    bool merge_adjacent;     /* Merge adjacent colors */
    bool remove_overlap;     /* Remove overlapping shapes */
    float min_area;          /* Minimum area threshold */
} trace_options_t;

typedef struct {
    rivee_shape_t *shapes;   /* Resulting vector shapes */
    int shape_count;
    int color_count;
    int node_count;
    int curve_count;
} trace_result_t;

/* Default trace options */
trace_options_t trace_options_default(trace_mode_t mode);

/* Trace bitmap to vector */
trace_result_t *image_trace(const uint8_t *pixels, int width, int height,
                            const trace_options_t *options);
void trace_result_free(trace_result_t *result);

/* Preview trace (faster, lower quality) */
trace_result_t *image_trace_preview(const uint8_t *pixels, int width, int height,
                                     const trace_options_t *options);

/* ============ Magic Fill ============ */
/*
 * Intelligent fill that creates shapes from areas.
 * Click to fill any enclosed area with a new shape.
 */

typedef struct {
    float tolerance;         /* Color tolerance 0-1 */
    bool use_contour;        /* Create contour path */
    bool smooth_edges;       /* Smooth the resulting path */
    int expansion;           /* Pixels to expand selection */
    rivee_fill_t fill;       /* Fill to apply */
} magic_fill_options_t;

/* Create shape from clicked area */
rivee_shape_t *magic_fill(rivee_document_t *doc, float click_x, float click_y,
                          const magic_fill_options_t *options);

/* ============ LiveSketch ============ */
/*
 * Converts freehand strokes to smooth curves.
 */

typedef struct {
    float smoothing;         /* 0-1, curve smoothing */
    float straightness;      /* 0-1, line detection */
    bool shape_recognition;  /* Recognize basic shapes */
    float delay_ms;          /* Time before finalizing */
} livesketch_options_t;

/* Process freehand stroke */
rivee_shape_t *livesketch_process(rivee_point_t *points, int point_count,
                                   const livesketch_options_t *options);

/* ============ Smart Carve ============ */
/*
 * Interactive shape edge refinement.
 */

typedef struct {
    float radius;            /* Brush radius */
    float strength;          /* Carve strength 0-1 */
    bool smooth;             /* Smooth while carving */
} smart_carve_options_t;

void smart_carve_apply(rivee_shape_t *shape, rivee_point_t *stroke, int point_count,
                       const smart_carve_options_t *options);

/* ============ Boolean Operations ============ */

typedef enum {
    BOOL_UNION,          /* A + B */
    BOOL_INTERSECT,      /* A & B */
    BOOL_SUBTRACT,       /* A - B */
    BOOL_EXCLUDE,        /* A ^ B (XOR) */
    BOOL_DIVIDE,         /* Split A by B */
} bool_operation_t;

/* Perform boolean operation on shapes */
rivee_shape_t *shape_boolean(rivee_shape_t *a, rivee_shape_t *b, bool_operation_t op);

/* Quick operations */
rivee_shape_t *shape_combine(rivee_shape_t *shapes[], int count);
rivee_shape_t *shape_weld(rivee_shape_t *shapes[], int count);
rivee_shape_t *shape_trim(rivee_shape_t *target, rivee_shape_t *cutter);

/* ============ Blend & Contour ============ */

typedef struct {
    int steps;               /* Number of intermediate shapes */
    bool full_path;          /* Blend along entire path */
    bool rotate;             /* Rotate along path */
    bool scale;              /* Scale along path */
    float acceleration;      /* 0 = linear, <0 = decelerate, >0 = accelerate */
} blend_options_t;

/* Create blend between two shapes */
rivee_shape_t *shape_blend(rivee_shape_t *from, rivee_shape_t *to,
                           const blend_options_t *options);

typedef struct {
    float offset;            /* Contour distance */
    int steps;               /* Number of contour lines */
    bool inside;             /* Inside contour */
    bool outside;            /* Outside contour */
    rivee_color_t start_color;
    rivee_color_t end_color;
} contour_options_t;

/* Create contour around shape */
rivee_shape_t *shape_contour(rivee_shape_t *shape, const contour_options_t *options);

/* ============ Envelope & Perspective ============ */

typedef enum {
    ENVELOPE_STRAIGHT,
    ENVELOPE_SINGLE_ARC,
    ENVELOPE_DOUBLE_ARC,
    ENVELOPE_UNCONSTRAINED
} envelope_mode_t;

typedef struct {
    envelope_mode_t mode;
    rivee_point_t corners[4];    /* Corner control points */
    rivee_point_t handles[8];    /* Bezier handles (2 per corner) */
} envelope_t;

/* Apply envelope distortion */
void shape_apply_envelope(rivee_shape_t *shape, const envelope_t *envelope);

/* Apply perspective */
void shape_apply_perspective(rivee_shape_t *shape, rivee_point_t vanishing_point,
                             float depth);

/* ============ Drop Shadow & Effects ============ */

typedef struct {
    float offset_x;
    float offset_y;
    float blur;
    float opacity;
    rivee_color_t color;
    bool inner;              /* Inner shadow */
} shadow_options_t;

typedef struct {
    rivee_color_t color;
    float radius;
    float intensity;
} glow_options_t;

typedef struct {
    float radius;
    float intensity;
    float angle;
    int steps;
} bevel_options_t;

/* Apply effects (non-destructive) */
void shape_add_shadow(rivee_shape_t *shape, const shadow_options_t *shadow);
void shape_add_glow(rivee_shape_t *shape, const glow_options_t *glow);
void shape_add_bevel(rivee_shape_t *shape, const bevel_options_t *bevel);
void shape_clear_effects(rivee_shape_t *shape);

/* ============ Art Brushes ============ */

typedef struct {
    char name[64];
    rivee_shape_t *brush_shape;   /* Shape used for stroke */
    float spacing;                /* Spacing along path */
    bool scale_with_pressure;
    bool rotate_along_path;
    bool stretch_to_fit;
} art_brush_t;

/* Apply art brush to path */
rivee_shape_t *path_apply_brush(rivee_path_t *path, const art_brush_t *brush);

/* ============ Pattern Fill ============ */

typedef struct {
    char name[64];
    char file_path[256];
    float scale;
    float rotation;
    float offset_x;
    float offset_y;
    bool tile;
} pattern_t;

/* Load pattern from file */
pattern_t *pattern_load(const char *path);
void pattern_free(pattern_t *pattern);

/* Apply pattern to shape */
void shape_set_pattern_fill(rivee_shape_t *shape, const pattern_t *pattern);

#endif /* RIVEE_EFFECTS_H */
