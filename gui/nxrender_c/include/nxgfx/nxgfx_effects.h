/*
 * NeolyxOS - NXGFX Effects System
 * Material effects for modern UI compositing
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * Effects:
 *   - Glass (frosted glass with blur and tint)
 *   - Frost (procedural frost pattern)
 *   - Tilt-Shift (selective focus blur)
 *   - Orb/Sphere (3D lit sphere, uses lighting.h)
 *   - Glow (outer/inner glow)
 *   - Blur (gaussian, box, motion)
 */

#ifndef NXGFX_EFFECTS_H
#define NXGFX_EFFECTS_H

#include "nxgfx/nxgfx.h"
#include "nxgfx/lighting.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Blur Types
 * ============================================================================ */

typedef enum {
    NX_BLUR_GAUSSIAN,       /* Standard gaussian blur */
    NX_BLUR_BOX,            /* Fast box blur (approximation) */
    NX_BLUR_MOTION,         /* Directional motion blur */
    NX_BLUR_RADIAL          /* Radial/zoom blur from center */
} nx_blur_type_t;

typedef struct {
    nx_blur_type_t type;
    float radius;           /* Blur radius in pixels (0-50) */
    float angle;            /* For motion blur: direction (0-360) */
    nx_point_t center;      /* For radial blur: center point */
    float strength;         /* Blur intensity (0-1) */
} nx_blur_params_t;

/* ============================================================================
 * Glass Effect (Glassmorphism)
 * ============================================================================ */

typedef struct {
    float blur_radius;      /* Background blur amount (0-50) */
    float opacity;          /* Glass opacity (0.0-1.0) */
    float refraction;       /* Light refraction intensity (0-1) */
    nx_color_t tint_color;  /* Glass tint (RGBA) */
    float border_opacity;   /* Border/edge visibility (0-1) */
    float border_width;     /* Border thickness */
    bool specular_highlight; /* Add specular reflection */
    float highlight_intensity; /* Specular intensity (0-1) */
} nx_glass_effect_t;

/* ============================================================================
 * Frost Effect
 * ============================================================================ */

typedef struct {
    float frost_intensity;  /* Frost pattern strength (0-100) */
    float blur_radius;      /* Background blur (0-30) */
    float grain_size;       /* Noise/grain detail (0.1-5.0) */
    float opacity;          /* Overall opacity (0-1) */
    nx_color_t frost_color; /* Frost tint color */
    uint32_t seed;          /* Random seed for pattern */
} nx_frost_effect_t;

/* ============================================================================
 * Tilt-Shift Effect
 * ============================================================================ */

typedef struct {
    nx_point_t focus_point; /* Center of sharp focus (x, y) */
    float focus_radius;     /* Sharp region size in pixels */
    float blur_strength;    /* Out-of-focus blur amount (0-30) */
    float falloff;          /* Gradient falloff rate (0.1-5.0) */
    float angle;            /* Tilt angle (0-360 degrees) */
    bool elliptical;        /* Use elliptical focus region */
    float aspect_ratio;     /* Ellipse aspect ratio */
} nx_tiltshift_effect_t;

/* ============================================================================
 * Orb/Sphere Effect (uses lighting system)
 * ============================================================================ */

typedef struct {
    nx_vec3_t light_position;   /* 3D light source position */
    float specularity;          /* Specular highlight strength (0-1) */
    float metalness;            /* Metallic surface property (0-1) */
    float roughness;            /* Surface roughness (0=smooth, 1=rough) */
    nx_color_t base_color;      /* Surface base color */
    float ambient_occlusion;    /* Cavity darkening (0-1) */
    float rim_light;            /* Edge lighting intensity (0-1) */
    bool environment_map;       /* Use environment reflection */
} nx_orb_effect_t;

/* ============================================================================
 * Glow Effect
 * ============================================================================ */

typedef enum {
    NX_GLOW_OUTER,          /* Glow outside object */
    NX_GLOW_INNER,          /* Glow inside object */
    NX_GLOW_BOTH            /* Both inner and outer */
} nx_glow_type_t;

typedef struct {
    nx_glow_type_t type;
    nx_color_t color;       /* Glow color */
    float radius;           /* Glow spread (0-50) */
    float intensity;        /* Glow brightness (0-2) */
    float falloff;          /* How quickly glow fades (0.5-3) */
    bool knockout;          /* Remove original shape from glow */
} nx_glow_effect_t;

/* ============================================================================
 * Shadow Effect (enhanced drop shadow)
 * ============================================================================ */

typedef struct {
    nx_color_t color;       /* Shadow color */
    float offset_x;         /* Horizontal offset */
    float offset_y;         /* Vertical offset */
    float blur;             /* Shadow blur/softness (0-50) */
    float spread;           /* Shadow size expansion */
    float opacity;          /* Shadow opacity (0-1) */
    bool inset;             /* Inner shadow */
} nx_shadow_effect_t;

/* ============================================================================
 * Chromatic Aberration
 * ============================================================================ */

typedef struct {
    float intensity;        /* Aberration strength (0-20) */
    nx_point_t center;      /* Center point for radial aberration */
    bool radial;            /* Radial (lens-like) vs linear */
    float angle;            /* Direction for linear aberration */
} nx_chromatic_aberration_t;

/* ============================================================================
 * Color Adjustment
 * ============================================================================ */

typedef struct {
    float brightness;       /* -100 to 100 */
    float contrast;         /* -100 to 100 */
    float saturation;       /* -100 to 100 */
    float hue_shift;        /* -180 to 180 degrees */
    float temperature;      /* -100 (cool) to 100 (warm) */
    float tint;             /* -100 (green) to 100 (magenta) */
} nx_color_adjust_t;

/* ============================================================================
 * Effect Buffer (for layer compositing)
 * ============================================================================ */

typedef struct {
    uint32_t* pixels;       /* RGBA pixel data */
    uint32_t width;
    uint32_t height;
    uint32_t stride;        /* Bytes per row */
    bool owns_memory;       /* Should free on destroy */
} nx_effect_buffer_t;

/* Buffer management */
nx_effect_buffer_t* nx_effect_buffer_create(uint32_t width, uint32_t height);
nx_effect_buffer_t* nx_effect_buffer_from_context(nx_context_t* ctx);
void nx_effect_buffer_destroy(nx_effect_buffer_t* buf);
void nx_effect_buffer_clear(nx_effect_buffer_t* buf, nx_color_t color);
void nx_effect_buffer_copy(nx_effect_buffer_t* dst, const nx_effect_buffer_t* src);
void nx_effect_buffer_blit(nx_context_t* ctx, const nx_effect_buffer_t* buf, nx_point_t pos);

/* ============================================================================
 * Blur API
 * ============================================================================ */

/* Apply blur to buffer */
void nx_effect_blur(nx_effect_buffer_t* buf, const nx_blur_params_t* params);

/* Optimized gaussian blur (separable, 2-pass) */
void nx_effect_gaussian_blur(nx_effect_buffer_t* buf, float radius);

/* Fast box blur (constant time, any radius) */
void nx_effect_box_blur(nx_effect_buffer_t* buf, uint32_t radius);

/* ============================================================================
 * Material Effects API
 * ============================================================================ */

/* Glass effect (blur background + tint + highlight) */
void nx_effect_glass(nx_effect_buffer_t* buf, 
                     const nx_effect_buffer_t* background,
                     const nx_glass_effect_t* params,
                     nx_rect_t region);

/* Frost effect (procedural noise + blur) */
void nx_effect_frost(nx_effect_buffer_t* buf,
                     const nx_effect_buffer_t* background,
                     const nx_frost_effect_t* params,
                     nx_rect_t region);

/* Tilt-shift effect (selective focus) */
void nx_effect_tiltshift(nx_effect_buffer_t* buf, 
                         const nx_tiltshift_effect_t* params);

/* Orb/Sphere effect (3D lit sphere) */
void nx_effect_orb(nx_effect_buffer_t* buf,
                   const nx_orb_effect_t* params,
                   nx_point_t center,
                   uint32_t radius);

/* ============================================================================
 * Glow & Shadow API
 * ============================================================================ */

/* Apply glow effect */
void nx_effect_glow(nx_effect_buffer_t* buf, 
                    const nx_effect_buffer_t* mask,
                    const nx_glow_effect_t* params);

/* Apply shadow effect */
void nx_effect_shadow(nx_effect_buffer_t* buf,
                      const nx_effect_buffer_t* mask,
                      const nx_shadow_effect_t* params);

/* ============================================================================
 * Color Adjustment API
 * ============================================================================ */

/* Apply color adjustments */
void nx_effect_color_adjust(nx_effect_buffer_t* buf, 
                            const nx_color_adjust_t* params);

/* Apply chromatic aberration */
void nx_effect_chromatic_aberration(nx_effect_buffer_t* buf,
                                    const nx_chromatic_aberration_t* params);

/* ============================================================================
 * Blend Modes
 * ============================================================================ */

#ifndef NX_BLEND_MODE_DEFINED
#define NX_BLEND_MODE_DEFINED
typedef enum {
    NX_BLEND_NORMAL,
    NX_BLEND_MULTIPLY,
    NX_BLEND_SCREEN,
    NX_BLEND_OVERLAY,
    NX_BLEND_DARKEN,
    NX_BLEND_LIGHTEN,
    NX_BLEND_COLOR_DODGE,
    NX_BLEND_COLOR_BURN,
    NX_BLEND_HARD_LIGHT,
    NX_BLEND_SOFT_LIGHT,
    NX_BLEND_DIFFERENCE,
    NX_BLEND_EXCLUSION,
    NX_BLEND_HUE,
    NX_BLEND_SATURATION,
    NX_BLEND_COLOR,
    NX_BLEND_LUMINOSITY,
    NX_BLEND_ADD,
    NX_BLEND_SUBTRACT
} nx_blend_mode_t;
#endif

/* Composite two buffers with blend mode */
void nx_effect_composite(nx_effect_buffer_t* dst,
                         const nx_effect_buffer_t* src,
                         nx_blend_mode_t mode,
                         float opacity);

/* ============================================================================
 * Effect Presets
 * ============================================================================ */

/* Get default glass effect (macOS-style) */
nx_glass_effect_t nx_glass_preset_default(void);
nx_glass_effect_t nx_glass_preset_dark(void);
nx_glass_effect_t nx_glass_preset_light(void);

/* Get default frost effect */
nx_frost_effect_t nx_frost_preset_default(void);
nx_frost_effect_t nx_frost_preset_heavy(void);

/* Get default orb effect */
nx_orb_effect_t nx_orb_preset_glossy(nx_color_t color);
nx_orb_effect_t nx_orb_preset_matte(nx_color_t color);
nx_orb_effect_t nx_orb_preset_metallic(nx_color_t color);

/* Get default glow effect */
nx_glow_effect_t nx_glow_preset_soft(nx_color_t color);
nx_glow_effect_t nx_glow_preset_neon(nx_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* NXGFX_EFFECTS_H */
