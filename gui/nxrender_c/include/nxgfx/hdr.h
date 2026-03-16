/*
 * NeolyxOS - NXGFX HDR Color System
 * Wide color gamut and high dynamic range support
 * 
 * Copyright (c) 2025 KetiveeAI
 *
 * COLOR FORMATS:
 * ==============
 * SDR 8-bit:   RGBA8888  (0-255 per channel, sRGB)
 * 10-bit:      RGB10A2   (0-1023 RGB, 0-3 alpha, Display P3/Rec2020)
 * 16-bit:      RGBA16F   (half-float, linear HDR)
 * 32-bit:      RGBA32F   (full float, linear HDR)
 *
 * COLOR SPACES:
 * =============
 * - sRGB:      Standard web/SDR content (gamma ~2.2)
 * - Display P3: Wide color gamut (Apple displays, modern monitors)
 * - Rec.2020:  Ultra-wide gamut (HDR TV, cinema)
 * - Linear:    No gamma, used for compositing and lighting
 *
 * TONE MAPPING:
 * =============
 * HDR content must be tone-mapped to display capabilities.
 * Algorithms: Reinhard, ACES, Filmic
 */

#ifndef NXGFX_HDR_H
#define NXGFX_HDR_H

#include "nxgfx/nxgfx.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Color Formats
 * ============================================================================ */
typedef enum {
    NX_COLOR_FORMAT_RGBA8,      /* 8-bit per channel (32-bit total) */
    NX_COLOR_FORMAT_RGB10A2,    /* 10-bit RGB, 2-bit alpha (32-bit) */
    NX_COLOR_FORMAT_RGBA16,     /* 16-bit per channel (64-bit) */
    NX_COLOR_FORMAT_RGBA16F,    /* 16-bit float per channel (64-bit) */
    NX_COLOR_FORMAT_RGBA32F     /* 32-bit float per channel (128-bit) */
} nx_color_format_t;

/* Color spaces */
typedef enum {
    NX_COLOR_SPACE_SRGB,        /* Standard RGB (gamma ~2.2) */
    NX_COLOR_SPACE_LINEAR,      /* Linear RGB (no gamma) */
    NX_COLOR_SPACE_DISPLAY_P3,  /* Apple Display P3 */
    NX_COLOR_SPACE_REC2020,     /* BT.2020 (HDR TV) */
    NX_COLOR_SPACE_DCI_P3       /* DCI-P3 (Cinema) */
} nx_color_space_t;

/* ============================================================================
 * Wide Color Types
 * ============================================================================ */

/* 10-bit RGB + 2-bit Alpha (packed 32-bit) */
typedef struct {
    uint32_t packed;    /* R: 10 | G: 10 | B: 10 | A: 2 */
} nx_color10_t;

/* 16-bit per channel */
typedef struct {
    uint16_t r, g, b, a;
} nx_color16_t;

/* 32-bit float per channel (HDR range: 0.0 to unbounded) */
typedef struct {
    float r, g, b, a;
} nx_colorf_t;

/* ============================================================================
 * Color Constructors
 * ============================================================================ */

/* 10-bit color (0-1023 range) */
static inline nx_color10_t nx_color10(uint16_t r, uint16_t g, uint16_t b, uint8_t a) {
    nx_color10_t c;
    c.packed = ((uint32_t)(r & 0x3FF) << 22) |
               ((uint32_t)(g & 0x3FF) << 12) |
               ((uint32_t)(b & 0x3FF) << 2) |
               (a & 0x3);
    return c;
}

/* Extract 10-bit components */
static inline void nx_color10_unpack(nx_color10_t c, uint16_t* r, uint16_t* g, uint16_t* b, uint8_t* a) {
    *r = (c.packed >> 22) & 0x3FF;
    *g = (c.packed >> 12) & 0x3FF;
    *b = (c.packed >> 2) & 0x3FF;
    *a = c.packed & 0x3;
}

/* 16-bit color (0-65535 range) */
static inline nx_color16_t nx_color16(uint16_t r, uint16_t g, uint16_t b, uint16_t a) {
    return (nx_color16_t){ r, g, b, a };
}

/* Float color (0.0 - 1.0+ for HDR) */
static inline nx_colorf_t nx_colorf(float r, float g, float b, float a) {
    return (nx_colorf_t){ r, g, b, a };
}

/* ============================================================================
 * Color Conversion
 * ============================================================================ */

/* 8-bit to float */
static inline nx_colorf_t nx_color_to_float(nx_color_t c) {
    return (nx_colorf_t){
        c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f
    };
}

/* Float to 8-bit (clamps to 0-1) */
static inline nx_color_t nx_float_to_color(nx_colorf_t c) {
    float r = c.r < 0 ? 0 : (c.r > 1 ? 1 : c.r);
    float g = c.g < 0 ? 0 : (c.g > 1 ? 1 : c.g);
    float b = c.b < 0 ? 0 : (c.b > 1 ? 1 : c.b);
    float a = c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a);
    return (nx_color_t){ (uint8_t)(r * 255), (uint8_t)(g * 255), 
                         (uint8_t)(b * 255), (uint8_t)(a * 255) };
}

/* 16-bit to float */
static inline nx_colorf_t nx_color16_to_float(nx_color16_t c) {
    return (nx_colorf_t){
        c.r / 65535.0f, c.g / 65535.0f, c.b / 65535.0f, c.a / 65535.0f
    };
}

/* Float to 16-bit */
static inline nx_color16_t nx_float_to_color16(nx_colorf_t c) {
    float r = c.r < 0 ? 0 : (c.r > 1 ? 1 : c.r);
    float g = c.g < 0 ? 0 : (c.g > 1 ? 1 : c.g);
    float b = c.b < 0 ? 0 : (c.b > 1 ? 1 : c.b);
    float a = c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a);
    return (nx_color16_t){ (uint16_t)(r * 65535), (uint16_t)(g * 65535), 
                           (uint16_t)(b * 65535), (uint16_t)(a * 65535) };
}

/* 10-bit to float */
static inline nx_colorf_t nx_color10_to_float(nx_color10_t c) {
    uint16_t r, g, b;
    uint8_t a;
    nx_color10_unpack(c, &r, &g, &b, &a);
    return (nx_colorf_t){ r / 1023.0f, g / 1023.0f, b / 1023.0f, a / 3.0f };
}

/* Float to 10-bit */
static inline nx_color10_t nx_float_to_color10(nx_colorf_t c) {
    float r = c.r < 0 ? 0 : (c.r > 1 ? 1 : c.r);
    float g = c.g < 0 ? 0 : (c.g > 1 ? 1 : c.g);
    float b = c.b < 0 ? 0 : (c.b > 1 ? 1 : c.b);
    float a = c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a);
    return nx_color10((uint16_t)(r * 1023), (uint16_t)(g * 1023), 
                      (uint16_t)(b * 1023), (uint8_t)(a * 3));
}

/* 8-bit to 16-bit (upscale) */
static inline nx_color16_t nx_color_upscale_16(nx_color_t c) {
    return (nx_color16_t){
        (uint16_t)c.r << 8 | c.r,
        (uint16_t)c.g << 8 | c.g,
        (uint16_t)c.b << 8 | c.b,
        (uint16_t)c.a << 8 | c.a
    };
}

/* 16-bit to 8-bit (downscale with dithering) */
static inline nx_color_t nx_color_downscale_8(nx_color16_t c) {
    return (nx_color_t){ c.r >> 8, c.g >> 8, c.b >> 8, c.a >> 8 };
}

/* ============================================================================
 * Gamma Correction
 * ============================================================================ */

/* sRGB gamma to linear */
float nx_srgb_to_linear(float v);

/* Linear to sRGB gamma */
float nx_linear_to_srgb(float v);

/* Full color gamma conversion */
nx_colorf_t nx_color_srgb_to_linear(nx_colorf_t c);
nx_colorf_t nx_color_linear_to_srgb(nx_colorf_t c);

/* ============================================================================
 * Color Space Conversion
 * ============================================================================ */

/* Convert between color spaces */
nx_colorf_t nx_color_convert(nx_colorf_t c, nx_color_space_t from, nx_color_space_t to);

/* ============================================================================
 * HDR Tone Mapping
 * ============================================================================ */
typedef enum {
    NX_TONEMAP_CLAMP,       /* Simple clamp (lossy) */
    NX_TONEMAP_REINHARD,    /* Reinhard global operator */
    NX_TONEMAP_REINHARD_EX, /* Extended Reinhard with white point */
    NX_TONEMAP_ACES,        /* Academy Color Encoding System */
    NX_TONEMAP_FILMIC,      /* Cinematic filmic curve */
    NX_TONEMAP_UNCHARTED2   /* Uncharted 2 filmic */
} nx_tonemap_t;

/* Tone map single color */
nx_colorf_t nx_tonemap_color(nx_colorf_t hdr, nx_tonemap_t method, float exposure);

/* Tone map buffer (in-place) */
void nx_tonemap_buffer(nx_colorf_t* buffer, uint32_t count, 
                        nx_tonemap_t method, float exposure);

/* ============================================================================
 * Color Math (in linear space)
 * ============================================================================ */

/* Blend colors (premultiplied alpha) */
nx_colorf_t nx_colorf_blend(nx_colorf_t dst, nx_colorf_t src);

/* Multiply color by scalar */
static inline nx_colorf_t nx_colorf_mul(nx_colorf_t c, float s) {
    return (nx_colorf_t){ c.r * s, c.g * s, c.b * s, c.a * s };
}

/* Add colors */
static inline nx_colorf_t nx_colorf_add(nx_colorf_t a, nx_colorf_t b) {
    return (nx_colorf_t){ a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a };
}

/* Lerp colors */
static inline nx_colorf_t nx_colorf_lerp(nx_colorf_t a, nx_colorf_t b, float t) {
    return (nx_colorf_t){
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

/* Luminance (Rec. 709) */
static inline float nx_colorf_luminance(nx_colorf_t c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

/* ============================================================================
 * HDR Framebuffer
 * ============================================================================ */

/* HDR context for high-precision rendering */
typedef struct nx_hdr_context nx_hdr_context_t;

/* Create HDR context */
nx_hdr_context_t* nx_hdr_context_create(uint32_t width, uint32_t height, 
                                         nx_color_format_t format);
void nx_hdr_context_destroy(nx_hdr_context_t* ctx);

/* Get buffer pointer (cast based on format) */
void* nx_hdr_buffer(nx_hdr_context_t* ctx);
uint32_t nx_hdr_width(nx_hdr_context_t* ctx);
uint32_t nx_hdr_height(nx_hdr_context_t* ctx);
nx_color_format_t nx_hdr_format(nx_hdr_context_t* ctx);

/* Put pixel (any format) */
void nx_hdr_put_pixel(nx_hdr_context_t* ctx, int32_t x, int32_t y, nx_colorf_t c);
nx_colorf_t nx_hdr_get_pixel(nx_hdr_context_t* ctx, int32_t x, int32_t y);

/* Clear */
void nx_hdr_clear(nx_hdr_context_t* ctx, nx_colorf_t c);

/* Convert HDR context to SDR for display */
void nx_hdr_to_sdr(nx_hdr_context_t* hdr, nx_context_t* sdr, 
                    nx_tonemap_t tonemap, float exposure);

#ifdef __cplusplus
}
#endif
#endif /* NXGFX_HDR_H */
