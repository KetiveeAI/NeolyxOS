/*
 * REOX Color System
 * Advanced color management and manipulation for NeolyxOS UI
 * 
 * Features:
 * - Multiple color spaces (RGB, HSL, HSV, LAB, LCH, XYZ)
 * - Blend modes (overlay, multiply, screen, etc.)
 * - Color palette generation
 * - Gradient system
 * - Color accessibility checking
 */

#ifndef REOX_COLOR_SYSTEM_H
#define REOX_COLOR_SYSTEM_H

#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Color Space Types
 * ============================================================================ */

/* HSL Color (Hue, Saturation, Lightness) */
typedef struct rx_hsl {
    float h;    /* 0-360 degrees */
    float s;    /* 0-1 */
    float l;    /* 0-1 */
    float a;    /* 0-1 alpha */
} rx_hsl;

/* HSV Color (Hue, Saturation, Value) */
typedef struct rx_hsv {
    float h;    /* 0-360 degrees */
    float s;    /* 0-1 */
    float v;    /* 0-1 */
    float a;    /* 0-1 alpha */
} rx_hsv;

/* CIE LAB Color (Perceptually uniform) */
typedef struct rx_lab {
    float l;    /* 0-100 lightness */
    float a;    /* -128 to 128 green-red */
    float b;    /* -128 to 128 blue-yellow */
    float alpha;
} rx_lab;

/* CIE LCH Color (LAB in polar coordinates) */
typedef struct rx_lch {
    float l;    /* 0-100 lightness */
    float c;    /* 0-150 chroma */
    float h;    /* 0-360 hue */
    float alpha;
} rx_lch;

/* CIE XYZ Color (Device-independent) */
typedef struct rx_xyz {
    float x;
    float y;
    float z;
    float alpha;
} rx_xyz;

/* ============================================================================
 * Color Space Conversions
 * ============================================================================ */

/* RGB conversions */
extern rx_hsl rx_rgb_to_hsl(rx_color rgb);
extern rx_hsv rx_rgb_to_hsv(rx_color rgb);
extern rx_lab rx_rgb_to_lab(rx_color rgb);
extern rx_lch rx_rgb_to_lch(rx_color rgb);
extern rx_xyz rx_rgb_to_xyz(rx_color rgb);

/* To RGB conversions */
extern rx_color rx_hsl_to_rgb(rx_hsl hsl);
extern rx_color rx_hsv_to_rgb(rx_hsv hsv);
extern rx_color rx_lab_to_rgb(rx_lab lab);
extern rx_color rx_lch_to_rgb(rx_lch lch);
extern rx_color rx_xyz_to_rgb(rx_xyz xyz);

/* Cross-space conversions */
extern rx_lab rx_xyz_to_lab(rx_xyz xyz);
extern rx_xyz rx_lab_to_xyz(rx_lab lab);
extern rx_lch rx_lab_to_lch(rx_lab lab);
extern rx_lab rx_lch_to_lab(rx_lch lch);

/* ============================================================================
 * Color Manipulation
 * ============================================================================ */

/* Adjust color properties */
extern rx_color rx_color_adjust_hue(rx_color c, float degrees);
extern rx_color rx_color_adjust_saturation(rx_color c, float amount);
extern rx_color rx_color_adjust_lightness(rx_color c, float amount);
extern rx_color rx_color_adjust_brightness(rx_color c, float amount);
extern rx_color rx_color_adjust_contrast(rx_color c, float amount);

/* Color mixing */
extern rx_color rx_color_mix(rx_color a, rx_color b, float t);
extern rx_color rx_color_mix_lab(rx_color a, rx_color b, float t);  /* Perceptually uniform */
extern rx_color rx_color_mix_lch(rx_color a, rx_color b, float t);  /* Hue-preserving */

/* Color transforms */
extern rx_color rx_color_invert(rx_color c);
extern rx_color rx_color_grayscale(rx_color c);
extern rx_color rx_color_sepia(rx_color c);
extern rx_color rx_color_complement(rx_color c);
extern rx_color rx_color_desaturate(rx_color c, float amount);
extern rx_color rx_color_saturate(rx_color c, float amount);

/* ============================================================================
 * Blend Modes
 * ============================================================================ */

typedef enum rx_blend_mode {
    RX_BLEND_NORMAL,
    RX_BLEND_MULTIPLY,
    RX_BLEND_SCREEN,
    RX_BLEND_OVERLAY,
    RX_BLEND_DARKEN,
    RX_BLEND_LIGHTEN,
    RX_BLEND_COLOR_DODGE,
    RX_BLEND_COLOR_BURN,
    RX_BLEND_HARD_LIGHT,
    RX_BLEND_SOFT_LIGHT,
    RX_BLEND_DIFFERENCE,
    RX_BLEND_EXCLUSION,
    RX_BLEND_HUE,
    RX_BLEND_SATURATION,
    RX_BLEND_COLOR,
    RX_BLEND_LUMINOSITY,
    RX_BLEND_PLUS_LIGHTER,    /* Additive */
    RX_BLEND_PLUS_DARKER,
} rx_blend_mode;

extern rx_color rx_color_blend(rx_color base, rx_color blend, rx_blend_mode mode);
extern rx_color rx_color_blend_alpha(rx_color base, rx_color blend, rx_blend_mode mode, float alpha);

/* ============================================================================
 * Gradient System
 * ============================================================================ */

typedef enum rx_gradient_type {
    RX_GRADIENT_LINEAR,
    RX_GRADIENT_RADIAL,
    RX_GRADIENT_ANGULAR,
    RX_GRADIENT_DIAMOND,
    RX_GRADIENT_CONIC,
} rx_gradient_type;

typedef struct rx_gradient_stop {
    rx_color color;
    float position;           /* 0.0 - 1.0 */
} rx_gradient_stop;

typedef struct rx_gradient {
    rx_gradient_type type;
    rx_gradient_stop* stops;
    size_t stop_count;
    
    /* Linear gradient */
    float angle;              /* Degrees */
    
    /* Radial gradient */
    rx_point center;
    float radius;
    float focal_x;
    float focal_y;
    
    /* Color space for interpolation */
    bool use_oklch;           /* Modern perceptual interpolation */
    
} rx_gradient;

extern rx_gradient* rx_gradient_linear(float angle, rx_gradient_stop* stops, size_t count);
extern rx_gradient* rx_gradient_radial(rx_point center, float radius, rx_gradient_stop* stops, size_t count);
extern rx_gradient* rx_gradient_angular(rx_point center, float start_angle, rx_gradient_stop* stops, size_t count);
extern void rx_gradient_add_stop(rx_gradient* grad, rx_color color, float position);
extern rx_color rx_gradient_color_at(rx_gradient* grad, float position);
extern rx_color rx_gradient_sample(rx_gradient* grad, rx_point pos, rx_rect bounds);
extern void rx_gradient_destroy(rx_gradient* grad);

/* Preset gradients */
extern rx_gradient* rx_gradient_sunset(void);
extern rx_gradient* rx_gradient_ocean(void);
extern rx_gradient* rx_gradient_forest(void);
extern rx_gradient* rx_gradient_neon(void);
extern rx_gradient* rx_gradient_gold(void);
extern rx_gradient* rx_gradient_northern_lights(void);

/* ============================================================================
 * Color Palette Generation
 * ============================================================================ */

typedef enum rx_palette_type {
    RX_PALETTE_COMPLEMENTARY,      /* Opposite on wheel */
    RX_PALETTE_ANALOGOUS,          /* Adjacent colors */
    RX_PALETTE_TRIADIC,            /* 3 evenly spaced */
    RX_PALETTE_SPLIT_COMP,         /* Split complementary */
    RX_PALETTE_TETRADIC,           /* 4 evenly spaced */
    RX_PALETTE_MONOCHROMATIC,      /* Single hue variations */
    RX_PALETTE_SQUARE,             /* 4 colors, 90° apart */
} rx_palette_type;

typedef struct rx_palette {
    rx_color* colors;
    size_t count;
    rx_color base;
} rx_palette;

extern rx_palette* rx_palette_generate(rx_color base, rx_palette_type type, size_t count);
extern rx_palette* rx_palette_from_image(void* image_data, int width, int height, size_t max_colors);
extern rx_palette* rx_palette_harmonize(rx_color* colors, size_t count);
extern rx_color rx_palette_find_accent(rx_palette* pal);
extern void rx_palette_sort_by_lightness(rx_palette* pal);
extern void rx_palette_sort_by_saturation(rx_palette* pal);
extern void rx_palette_destroy(rx_palette* pal);

/* ============================================================================
 * Color Accessibility
 * ============================================================================ */

typedef enum rx_wcag_level {
    RX_WCAG_AA,
    RX_WCAG_AA_LARGE,
    RX_WCAG_AAA,
    RX_WCAG_AAA_LARGE,
} rx_wcag_level;

/* Calculate relative luminance (0-1) */
extern float rx_color_luminance(rx_color c);

/* Calculate contrast ratio (1-21) */
extern float rx_color_contrast_ratio(rx_color c1, rx_color c2);

/* Check WCAG compliance */
extern bool rx_color_is_accessible(rx_color foreground, rx_color background, rx_wcag_level level);

/* Find accessible color */
extern rx_color rx_color_make_accessible(rx_color color, rx_color background, rx_wcag_level level);

/* Color blindness simulation */
typedef enum rx_color_blindness {
    RX_VISION_NORMAL,
    RX_VISION_PROTANOPIA,     /* Red-blind */
    RX_VISION_DEUTERANOPIA,   /* Green-blind */
    RX_VISION_TRITANOPIA,     /* Blue-blind */
    RX_VISION_ACHROMATOPSIA,  /* Total color blindness */
} rx_color_blindness;

extern rx_color rx_color_simulate_blindness(rx_color c, rx_color_blindness type);

/* ============================================================================
 * Color Quantization
 * ============================================================================ */

extern rx_palette* rx_color_quantize(void* pixels, int width, int height, int max_colors);
extern rx_color rx_color_average(rx_color* colors, size_t count);
extern rx_color rx_color_dominant(void* pixels, int width, int height);

/* ============================================================================
 * Color Space Profiles (Display P3, sRGB, etc.)
 * ============================================================================ */

typedef enum rx_color_space {
    RX_SPACE_SRGB,
    RX_SPACE_DISPLAY_P3,
    RX_SPACE_ADOBE_RGB,
    RX_SPACE_REC2020,
    RX_SPACE_LINEAR_SRGB,
} rx_color_space;

typedef struct rx_color_profile {
    rx_color_space space;
    float gamma;
    float white_point[3];
    float primaries[6];       /* r_x, r_y, g_x, g_y, b_x, b_y */
} rx_color_profile;

extern rx_color_profile* rx_profile_srgb(void);
extern rx_color_profile* rx_profile_display_p3(void);
extern rx_color rx_color_convert_profile(rx_color c, rx_color_profile* from, rx_color_profile* to);

#ifdef __cplusplus
}
#endif

#endif /* REOX_COLOR_SYSTEM_H */
