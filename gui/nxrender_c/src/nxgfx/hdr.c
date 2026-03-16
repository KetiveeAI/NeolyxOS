/*
 * NeolyxOS - NXGFX HDR Color System Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/hdr.h"
#include "nxgfx/nxgfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Gamma Correction
 * ============================================================================ */

float nx_srgb_to_linear(float v) {
    if (v <= 0.04045f) {
        return v / 12.92f;
    }
    return powf((v + 0.055f) / 1.055f, 2.4f);
}

float nx_linear_to_srgb(float v) {
    if (v <= 0.0031308f) {
        return v * 12.92f;
    }
    return 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

nx_colorf_t nx_color_srgb_to_linear(nx_colorf_t c) {
    return (nx_colorf_t){
        nx_srgb_to_linear(c.r),
        nx_srgb_to_linear(c.g),
        nx_srgb_to_linear(c.b),
        c.a
    };
}

nx_colorf_t nx_color_linear_to_srgb(nx_colorf_t c) {
    return (nx_colorf_t){
        nx_linear_to_srgb(c.r),
        nx_linear_to_srgb(c.g),
        nx_linear_to_srgb(c.b),
        c.a
    };
}

/* ============================================================================
 * Color Space Conversion Matrices (3x3)
 * ============================================================================ */

/* sRGB to XYZ D65 */
static const float SRGB_TO_XYZ[9] = {
    0.4124564f, 0.3575761f, 0.1804375f,
    0.2126729f, 0.7151522f, 0.0721750f,
    0.0193339f, 0.1191920f, 0.9503041f
};

/* XYZ D65 to sRGB */
static const float XYZ_TO_SRGB[9] = {
     3.2404542f, -1.5371385f, -0.4985314f,
    -0.9692660f,  1.8760108f,  0.0415560f,
     0.0556434f, -0.2040259f,  1.0572252f
};

/* Display P3 to XYZ D65 */
static const float P3_TO_XYZ[9] = {
    0.4865709f, 0.2656677f, 0.1982173f,
    0.2289746f, 0.6917385f, 0.0792869f,
    0.0000000f, 0.0451134f, 1.0439444f
};

/* XYZ D65 to Display P3 */
static const float XYZ_TO_P3[9] = {
     2.4934969f, -0.9313836f, -0.4027108f,
    -0.8294890f,  1.7626641f,  0.0236247f,
     0.0358458f, -0.0761724f,  0.9568845f
};

/* Rec.2020 to XYZ D65 */
static const float REC2020_TO_XYZ[9] = {
    0.6369580f, 0.1446169f, 0.1688810f,
    0.2627002f, 0.6779981f, 0.0593017f,
    0.0000000f, 0.0280727f, 1.0609851f
};

/* XYZ D65 to Rec.2020 */
static const float XYZ_TO_REC2020[9] = {
     1.7166512f, -0.3556708f, -0.2533663f,
    -0.6666844f,  1.6164812f,  0.0157685f,
     0.0176399f, -0.0427706f,  0.9421031f
};

/* Apply 3x3 matrix to RGB */
static nx_colorf_t apply_matrix(nx_colorf_t c, const float* m) {
    return (nx_colorf_t){
        m[0] * c.r + m[1] * c.g + m[2] * c.b,
        m[3] * c.r + m[4] * c.g + m[5] * c.b,
        m[6] * c.r + m[7] * c.g + m[8] * c.b,
        c.a
    };
}

nx_colorf_t nx_color_convert(nx_colorf_t c, nx_color_space_t from, nx_color_space_t to) {
    if (from == to) return c;
    
    /* Remove gamma if coming from gamma space */
    nx_colorf_t linear = c;
    if (from == NX_COLOR_SPACE_SRGB) {
        linear = nx_color_srgb_to_linear(c);
    }
    
    /* Convert to XYZ as intermediate */
    nx_colorf_t xyz;
    switch (from) {
        case NX_COLOR_SPACE_SRGB:
        case NX_COLOR_SPACE_LINEAR:
            xyz = apply_matrix(linear, SRGB_TO_XYZ);
            break;
        case NX_COLOR_SPACE_DISPLAY_P3:
        case NX_COLOR_SPACE_DCI_P3:
            xyz = apply_matrix(linear, P3_TO_XYZ);
            break;
        case NX_COLOR_SPACE_REC2020:
            xyz = apply_matrix(linear, REC2020_TO_XYZ);
            break;
        default:
            xyz = linear;
    }
    
    /* Convert from XYZ to target */
    nx_colorf_t result;
    switch (to) {
        case NX_COLOR_SPACE_SRGB:
            result = apply_matrix(xyz, XYZ_TO_SRGB);
            result = nx_color_linear_to_srgb(result);
            break;
        case NX_COLOR_SPACE_LINEAR:
            result = apply_matrix(xyz, XYZ_TO_SRGB);
            break;
        case NX_COLOR_SPACE_DISPLAY_P3:
        case NX_COLOR_SPACE_DCI_P3:
            result = apply_matrix(xyz, XYZ_TO_P3);
            break;
        case NX_COLOR_SPACE_REC2020:
            result = apply_matrix(xyz, XYZ_TO_REC2020);
            break;
        default:
            result = xyz;
    }
    
    result.a = c.a;
    return result;
}

/* ============================================================================
 * Tone Mapping
 * ============================================================================ */

static float reinhard(float x) {
    return x / (1.0f + x);
}

static float reinhard_extended(float x, float white) {
    return x * (1.0f + x / (white * white)) / (1.0f + x);
}

/* ACES filmic tone mapping */
static float aces_filmic(float x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

/* Uncharted 2 filmic curve */
static float uncharted2_curve(float x) {
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

nx_colorf_t nx_tonemap_color(nx_colorf_t hdr, nx_tonemap_t method, float exposure) {
    /* Apply exposure */
    hdr.r *= exposure;
    hdr.g *= exposure;
    hdr.b *= exposure;
    
    nx_colorf_t result;
    
    switch (method) {
        case NX_TONEMAP_CLAMP:
            result.r = hdr.r > 1.0f ? 1.0f : (hdr.r < 0 ? 0 : hdr.r);
            result.g = hdr.g > 1.0f ? 1.0f : (hdr.g < 0 ? 0 : hdr.g);
            result.b = hdr.b > 1.0f ? 1.0f : (hdr.b < 0 ? 0 : hdr.b);
            break;
            
        case NX_TONEMAP_REINHARD:
            result.r = reinhard(hdr.r);
            result.g = reinhard(hdr.g);
            result.b = reinhard(hdr.b);
            break;
            
        case NX_TONEMAP_REINHARD_EX:
            result.r = reinhard_extended(hdr.r, 4.0f);
            result.g = reinhard_extended(hdr.g, 4.0f);
            result.b = reinhard_extended(hdr.b, 4.0f);
            break;
            
        case NX_TONEMAP_ACES:
            result.r = aces_filmic(hdr.r);
            result.g = aces_filmic(hdr.g);
            result.b = aces_filmic(hdr.b);
            break;
            
        case NX_TONEMAP_FILMIC:
        case NX_TONEMAP_UNCHARTED2: {
            float white = 11.2f;
            float white_scale = 1.0f / uncharted2_curve(white);
            result.r = uncharted2_curve(hdr.r) * white_scale;
            result.g = uncharted2_curve(hdr.g) * white_scale;
            result.b = uncharted2_curve(hdr.b) * white_scale;
            break;
        }
    }
    
    result.a = hdr.a;
    return result;
}

void nx_tonemap_buffer(nx_colorf_t* buffer, uint32_t count, 
                        nx_tonemap_t method, float exposure) {
    if (!buffer) return;
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = nx_tonemap_color(buffer[i], method, exposure);
    }
}

/* ============================================================================
 * Color Blending
 * ============================================================================ */

nx_colorf_t nx_colorf_blend(nx_colorf_t dst, nx_colorf_t src) {
    if (src.a >= 1.0f) return src;
    if (src.a <= 0.0f) return dst;
    
    float out_a = src.a + dst.a * (1.0f - src.a);
    if (out_a < 0.0001f) return (nx_colorf_t){0, 0, 0, 0};
    
    float inv_a = 1.0f / out_a;
    return (nx_colorf_t){
        (src.r * src.a + dst.r * dst.a * (1.0f - src.a)) * inv_a,
        (src.g * src.a + dst.g * dst.a * (1.0f - src.a)) * inv_a,
        (src.b * src.a + dst.b * dst.a * (1.0f - src.a)) * inv_a,
        out_a
    };
}

/* ============================================================================
 * HDR Framebuffer
 * ============================================================================ */

struct nx_hdr_context {
    uint32_t width;
    uint32_t height;
    nx_color_format_t format;
    void* buffer;
    size_t buffer_size;
};

static size_t bytes_per_pixel(nx_color_format_t format) {
    switch (format) {
        case NX_COLOR_FORMAT_RGBA8:    return 4;
        case NX_COLOR_FORMAT_RGB10A2:  return 4;
        case NX_COLOR_FORMAT_RGBA16:   return 8;
        case NX_COLOR_FORMAT_RGBA16F:  return 8;
        case NX_COLOR_FORMAT_RGBA32F:  return 16;
        default: return 4;
    }
}

nx_hdr_context_t* nx_hdr_context_create(uint32_t width, uint32_t height,
                                         nx_color_format_t format) {
    nx_hdr_context_t* ctx = calloc(1, sizeof(nx_hdr_context_t));
    if (!ctx) return NULL;
    
    ctx->width = width;
    ctx->height = height;
    ctx->format = format;
    ctx->buffer_size = width * height * bytes_per_pixel(format);
    ctx->buffer = calloc(1, ctx->buffer_size);
    
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void nx_hdr_context_destroy(nx_hdr_context_t* ctx) {
    if (!ctx) return;
    if (ctx->buffer) free(ctx->buffer);
    free(ctx);
}

void* nx_hdr_buffer(nx_hdr_context_t* ctx) {
    return ctx ? ctx->buffer : NULL;
}

uint32_t nx_hdr_width(nx_hdr_context_t* ctx) {
    return ctx ? ctx->width : 0;
}

uint32_t nx_hdr_height(nx_hdr_context_t* ctx) {
    return ctx ? ctx->height : 0;
}

nx_color_format_t nx_hdr_format(nx_hdr_context_t* ctx) {
    return ctx ? ctx->format : NX_COLOR_FORMAT_RGBA8;
}

void nx_hdr_put_pixel(nx_hdr_context_t* ctx, int32_t x, int32_t y, nx_colorf_t c) {
    if (!ctx || x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height) return;
    
    size_t idx = (y * ctx->width + x);
    
    switch (ctx->format) {
        case NX_COLOR_FORMAT_RGBA8: {
            uint8_t* p = (uint8_t*)ctx->buffer + idx * 4;
            nx_color_t c8 = nx_float_to_color(c);
            p[0] = c8.r; p[1] = c8.g; p[2] = c8.b; p[3] = c8.a;
            break;
        }
        case NX_COLOR_FORMAT_RGB10A2: {
            nx_color10_t* p = (nx_color10_t*)ctx->buffer + idx;
            *p = nx_float_to_color10(c);
            break;
        }
        case NX_COLOR_FORMAT_RGBA16: {
            nx_color16_t* p = (nx_color16_t*)((uint8_t*)ctx->buffer + idx * 8);
            *p = nx_float_to_color16(c);
            break;
        }
        case NX_COLOR_FORMAT_RGBA16F:
        case NX_COLOR_FORMAT_RGBA32F: {
            nx_colorf_t* p = (nx_colorf_t*)ctx->buffer + idx;
            *p = c;
            break;
        }
    }
}

nx_colorf_t nx_hdr_get_pixel(nx_hdr_context_t* ctx, int32_t x, int32_t y) {
    if (!ctx || x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height) {
        return (nx_colorf_t){0, 0, 0, 0};
    }
    
    size_t idx = (y * ctx->width + x);
    
    switch (ctx->format) {
        case NX_COLOR_FORMAT_RGBA8: {
            uint8_t* p = (uint8_t*)ctx->buffer + idx * 4;
            return (nx_colorf_t){ p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f };
        }
        case NX_COLOR_FORMAT_RGB10A2: {
            nx_color10_t* p = (nx_color10_t*)ctx->buffer + idx;
            return nx_color10_to_float(*p);
        }
        case NX_COLOR_FORMAT_RGBA16: {
            nx_color16_t* p = (nx_color16_t*)((uint8_t*)ctx->buffer + idx * 8);
            return nx_color16_to_float(*p);
        }
        case NX_COLOR_FORMAT_RGBA16F:
        case NX_COLOR_FORMAT_RGBA32F: {
            nx_colorf_t* p = (nx_colorf_t*)ctx->buffer + idx;
            return *p;
        }
        default:
            return (nx_colorf_t){0, 0, 0, 0};
    }
}

void nx_hdr_clear(nx_hdr_context_t* ctx, nx_colorf_t c) {
    if (!ctx || !ctx->buffer) return;
    
    for (uint32_t y = 0; y < ctx->height; y++) {
        for (uint32_t x = 0; x < ctx->width; x++) {
            nx_hdr_put_pixel(ctx, x, y, c);
        }
    }
}

void nx_hdr_to_sdr(nx_hdr_context_t* hdr, nx_context_t* sdr, 
                    nx_tonemap_t tonemap, float exposure) {
    if (!hdr || !sdr) return;
    
    uint32_t w = hdr->width;
    uint32_t h = hdr->height;
    
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            nx_colorf_t c = nx_hdr_get_pixel(hdr, x, y);
            
            /* Tone map HDR to SDR range */
            c = nx_tonemap_color(c, tonemap, exposure);
            
            /* Apply sRGB gamma */
            c = nx_color_linear_to_srgb(c);
            
            /* Convert to 8-bit */
            nx_color_t c8 = nx_float_to_color(c);
            nxgfx_put_pixel(sdr, x, y, c8);
        }
    }
}
