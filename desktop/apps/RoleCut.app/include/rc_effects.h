/*
 * RoleCut Effects Library
 * 
 * Built-in video effects for professional editing.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef RC_EFFECTS_H
#define RC_EFFECTS_H

#include "rolecut.h"
#include "rc_render.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Effect Categories
 * ============================================================================ */

typedef enum {
    RC_FX_CAT_COLOR = 0,        /* Color correction */
    RC_FX_CAT_STYLIZE,          /* Artistic effects */
    RC_FX_CAT_BLUR,             /* Blur variants */
    RC_FX_CAT_SHARPEN,          /* Sharpening */
    RC_FX_CAT_DISTORT,          /* Distortion */
    RC_FX_CAT_KEYING,           /* Green screen, etc */
    RC_FX_CAT_TIME,             /* Speed, reverse */
    RC_FX_CAT_GENERATE,         /* Generators */
    RC_FX_CAT_TRANSFORM,        /* Position, scale */
    RC_FX_CAT_COMPOSITE,        /* Blend modes */
} rc_fx_category_t;

/* ============================================================================
 * Built-in Effects Library
 * ============================================================================ */

typedef enum {
    /* Color Correction (Free) */
    RC_FX_BRIGHTNESS_CONTRAST,
    RC_FX_EXPOSURE,
    RC_FX_LEVELS,
    RC_FX_GAMMA,
    
    /* Color Correction (Pro) */
    RC_FX_CURVES,
    RC_FX_COLOR_WHEELS,
    RC_FX_HSL_SECONDARY,
    RC_FX_COLOR_MATCH,
    RC_FX_WHITE_BALANCE,
    RC_FX_VIBRANCE,
    RC_FX_TINT,
    RC_FX_CHANNEL_MIXER,
    RC_FX_SELECTIVE_COLOR,
    RC_FX_SHADOW_HIGHLIGHT,
    
    /* LUT Application (Pro) */
    RC_FX_LUT_3D,
    RC_FX_LUT_1D,
    
    /* Stylize */
    RC_FX_VIGNETTE,
    RC_FX_FILM_GRAIN,
    RC_FX_HALFTONE,
    RC_FX_POSTERIZE,
    RC_FX_THRESHOLD,
    RC_FX_SOLARIZE,
    RC_FX_EMBOSS,
    RC_FX_EDGE_DETECT,
    RC_FX_CARTOON,
    RC_FX_OIL_PAINT,
    RC_FX_SKETCH,
    RC_FX_VINTAGE,
    RC_FX_SEPIA,
    RC_FX_BLACK_WHITE,
    RC_FX_CROSS_PROCESS,
    RC_FX_BLEACH_BYPASS,
    RC_FX_CINEMATIC,
    RC_FX_DUOTONE,
    RC_FX_GLITCH,
    RC_FX_VHS,
    RC_FX_CRT,
    RC_FX_FILM_DAMAGE,
    
    /* Blur */
    RC_FX_GAUSSIAN_BLUR,
    RC_FX_BOX_BLUR,
    RC_FX_MOTION_BLUR,
    RC_FX_RADIAL_BLUR,
    RC_FX_ZOOM_BLUR,
    RC_FX_DIRECTIONAL_BLUR,
    RC_FX_LENS_BLUR,
    RC_FX_TILT_SHIFT,
    RC_FX_DEFOCUS,
    
    /* Sharpen */
    RC_FX_UNSHARP_MASK,
    RC_FX_SMART_SHARPEN,
    RC_FX_HIGH_PASS,
    
    /* Distort */
    RC_FX_LENS_DISTORTION,
    RC_FX_BARREL,
    RC_FX_PINCUSHION,
    RC_FX_SPHERIZE,
    RC_FX_WAVE,
    RC_FX_RIPPLE,
    RC_FX_TWIRL,
    RC_FX_MIRROR,
    RC_FX_KALEIDOSCOPE,
    RC_FX_PIXELATE,
    RC_FX_MOSAIC,
    
    /* Keying (Pro) */
    RC_FX_CHROMA_KEY,
    RC_FX_LUMA_KEY,
    RC_FX_DIFFERENCE_KEY,
    RC_FX_COLOR_KEY,
    RC_FX_SPILL_SUPPRESSOR,
    RC_FX_EDGE_FEATHER,
    RC_FX_MATTE_REFINE,
    
    /* Time (Pro) */
    RC_FX_SPEED_DURATION,
    RC_FX_SPEED_RAMP,
    RC_FX_REVERSE,
    RC_FX_FRAME_HOLD,
    RC_FX_TIME_REMAP,
    RC_FX_OPTICAL_FLOW,         /* Studio */
    
    /* Generators */
    RC_FX_SOLID_COLOR,
    RC_FX_GRADIENT,
    RC_FX_CHECKERBOARD,
    RC_FX_GRID,
    RC_FX_NOISE,
    RC_FX_BARS_TONE,
    
    /* Transform */
    RC_FX_POSITION,
    RC_FX_SCALE,
    RC_FX_ROTATION,
    RC_FX_ANCHOR_POINT,
    RC_FX_CROP,
    RC_FX_PAN_ZOOM,
    RC_FX_CORNER_PIN,
    RC_FX_STABILIZE,            /* Studio */
    
    RC_FX_COUNT
} rc_fx_id_t;

/* ============================================================================
 * Effect Definition
 * ============================================================================ */

typedef struct {
    rc_fx_id_t id;
    const char *name;
    const char *description;
    rc_fx_category_t category;
    bool requires_pro;
    bool requires_studio;
    bool supports_keyframes;
} rc_fx_def_t;

/**
 * Get built-in effect definitions.
 */
const rc_fx_def_t *rc_fx_get_definitions(int *count);

/**
 * Get effect definition by ID.
 */
const rc_fx_def_t *rc_fx_get_def(rc_fx_id_t id);

/* ============================================================================
 * Effect Instance
 * ============================================================================ */

typedef struct rc_fx_instance rc_fx_instance_t;

/**
 * Create effect instance.
 */
rc_fx_instance_t *rc_fx_create(rc_fx_id_t id);

/**
 * Destroy effect instance.
 */
void rc_fx_destroy(rc_fx_instance_t *fx);

/**
 * Clone effect instance.
 */
rc_fx_instance_t *rc_fx_clone(rc_fx_instance_t *fx);

/**
 * Enable/disable effect.
 */
void rc_fx_set_enabled(rc_fx_instance_t *fx, bool enabled);

/**
 * Check if effect is enabled.
 */
bool rc_fx_is_enabled(rc_fx_instance_t *fx);

/* ============================================================================
 * Effect Parameters
 * ============================================================================ */

typedef enum {
    RC_FX_PARAM_FLOAT,
    RC_FX_PARAM_INT,
    RC_FX_PARAM_BOOL,
    RC_FX_PARAM_COLOR,
    RC_FX_PARAM_POINT,
    RC_FX_PARAM_CHOICE,
    RC_FX_PARAM_ANGLE,
    RC_FX_PARAM_CURVE,
} rc_fx_param_type_t;

typedef struct {
    const char *name;
    const char *label;
    rc_fx_param_type_t type;
    float default_value;
    float min_value;
    float max_value;
    const char **choices;       /* For CHOICE type */
    int choice_count;
} rc_fx_param_def_t;

/**
 * Get effect parameter definitions.
 */
const rc_fx_param_def_t *rc_fx_get_params(rc_fx_instance_t *fx, int *count);

/**
 * Set parameter value.
 */
void rc_fx_set_float(rc_fx_instance_t *fx, const char *param, float value);
void rc_fx_set_int(rc_fx_instance_t *fx, const char *param, int value);
void rc_fx_set_bool(rc_fx_instance_t *fx, const char *param, bool value);
void rc_fx_set_color(rc_fx_instance_t *fx, const char *param, rc_color_t color);
void rc_fx_set_point(rc_fx_instance_t *fx, const char *param, float x, float y);

/**
 * Get parameter value.
 */
float rc_fx_get_float(rc_fx_instance_t *fx, const char *param);
int rc_fx_get_int(rc_fx_instance_t *fx, const char *param);
bool rc_fx_get_bool(rc_fx_instance_t *fx, const char *param);
rc_color_t rc_fx_get_color(rc_fx_instance_t *fx, const char *param);

/* ============================================================================
 * Effect Processing
 * ============================================================================ */

/**
 * Apply effect to frame buffer.
 */
void rc_fx_apply(rc_fx_instance_t *fx, 
                  rc_render_ctx_t *ctx,
                  rc_frame_buffer_t *dst,
                  rc_frame_buffer_t *src);

/**
 * Apply effect with keyframed parameters at time.
 */
void rc_fx_apply_at_time(rc_fx_instance_t *fx,
                          rc_render_ctx_t *ctx,
                          rc_frame_buffer_t *dst,
                          rc_frame_buffer_t *src,
                          rc_time_t time);

/* ============================================================================
 * Presets
 * ============================================================================ */

typedef struct {
    const char *name;
    const char *category;
    rc_fx_id_t effect_id;
    /* Preset values stored internally */
} rc_fx_preset_t;

/**
 * Get built-in presets for an effect.
 */
const rc_fx_preset_t *rc_fx_get_presets(rc_fx_id_t id, int *count);

/**
 * Apply preset to effect instance.
 */
void rc_fx_apply_preset(rc_fx_instance_t *fx, const rc_fx_preset_t *preset);

/**
 * Save current settings as user preset.
 */
bool rc_fx_save_preset(rc_fx_instance_t *fx, const char *name, const char *path);

/**
 * Load user preset.
 */
rc_fx_preset_t *rc_fx_load_preset(const char *path);

/* ============================================================================
 * LUT Support
 * ============================================================================ */

typedef struct rc_lut rc_lut_t;

/**
 * Load LUT file (.cube format).
 */
rc_lut_t *rc_lut_load(const char *path);

/**
 * Destroy LUT.
 */
void rc_lut_destroy(rc_lut_t *lut);

/**
 * Get LUT info.
 */
const char *rc_lut_get_name(rc_lut_t *lut);
int rc_lut_get_size(rc_lut_t *lut);

/**
 * Apply LUT to frame.
 */
void rc_lut_apply(rc_lut_t *lut, rc_render_ctx_t *ctx,
                   rc_frame_buffer_t *dst, rc_frame_buffer_t *src,
                   float intensity);

/* ============================================================================
 * Built-in LUTs (in resources/luts/)
 * ============================================================================ */

/**
 * Get list of built-in LUTs.
 */
const char **rc_lut_get_builtin(int *count);

/**
 * Load built-in LUT by name.
 */
rc_lut_t *rc_lut_load_builtin(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* RC_EFFECTS_H */
