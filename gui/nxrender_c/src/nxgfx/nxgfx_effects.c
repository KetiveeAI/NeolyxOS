/*
 * NeolyxOS - NXGFX Effects Implementation
 * Material effects for modern UI compositing
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx_effects.h"
#include "nxgfx/lighting.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

nx_effect_buffer_t* nx_effect_buffer_create(uint32_t width, uint32_t height) {
    nx_effect_buffer_t* buf = calloc(1, sizeof(nx_effect_buffer_t));
    if (!buf) return NULL;
    
    buf->width = width;
    buf->height = height;
    buf->stride = width * 4;
    buf->pixels = calloc(width * height, sizeof(uint32_t));
    buf->owns_memory = true;
    
    if (!buf->pixels) {
        free(buf);
        return NULL;
    }
    return buf;
}

void nx_effect_buffer_destroy(nx_effect_buffer_t* buf) {
    if (!buf) return;
    if (buf->owns_memory && buf->pixels) {
        free(buf->pixels);
    }
    free(buf);
}

void nx_effect_buffer_clear(nx_effect_buffer_t* buf, nx_color_t color) {
    if (!buf || !buf->pixels) return;
    uint32_t packed = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
    for (uint32_t i = 0; i < buf->width * buf->height; i++) {
        buf->pixels[i] = packed;
    }
}

void nx_effect_buffer_copy(nx_effect_buffer_t* dst, const nx_effect_buffer_t* src) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    uint32_t copy_w = (dst->width < src->width) ? dst->width : src->width;
    uint32_t copy_h = (dst->height < src->height) ? dst->height : src->height;
    
    for (uint32_t y = 0; y < copy_h; y++) {
        memcpy(&dst->pixels[y * dst->width], 
               &src->pixels[y * src->width], 
               copy_w * sizeof(uint32_t));
    }
}

/* ============================================================================
 * Color Utilities
 * ============================================================================ */

static inline uint8_t clamp_u8(int v) {
    return (v < 0) ? 0 : (v > 255) ? 255 : (uint8_t)v;
}

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline void unpack_rgba(uint32_t c, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    *a = (c >> 24) & 0xFF;
    *r = (c >> 16) & 0xFF;
    *g = (c >> 8) & 0xFF;
    *b = c & 0xFF;
}

static inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ============================================================================
 * Gaussian Blur (Separable, 2-pass)
 * ============================================================================ */

static float* generate_gaussian_kernel(float sigma, int* kernel_size) {
    int size = (int)(sigma * 6.0f) | 1;
    if (size < 3) size = 3;
    if (size > 101) size = 101;
    *kernel_size = size;
    
    float* kernel = malloc(size * sizeof(float));
    if (!kernel) return NULL;
    
    int half = size / 2;
    float sum = 0.0f;
    float s2 = 2.0f * sigma * sigma;
    
    for (int i = 0; i < size; i++) {
        int x = i - half;
        kernel[i] = expf(-(float)(x * x) / s2);
        sum += kernel[i];
    }
    
    for (int i = 0; i < size; i++) {
        kernel[i] /= sum;
    }
    return kernel;
}

void nx_effect_gaussian_blur(nx_effect_buffer_t* buf, float radius) {
    if (!buf || !buf->pixels || radius <= 0) return;
    
    int kernel_size;
    float* kernel = generate_gaussian_kernel(radius / 2.0f, &kernel_size);
    if (!kernel) return;
    
    uint32_t w = buf->width;
    uint32_t h = buf->height;
    int half = kernel_size / 2;
    
    uint32_t* temp = malloc(w * h * sizeof(uint32_t));
    if (!temp) { free(kernel); return; }
    
    /* Horizontal pass */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < kernel_size; k++) {
                int sx = (int)x + k - half;
                if (sx < 0) sx = 0;
                if (sx >= (int)w) sx = w - 1;
                
                uint32_t px = buf->pixels[y * w + sx];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                
                float weight = kernel[k];
                r += pr * weight;
                g += pg * weight;
                b += pb * weight;
                a += pa * weight;
            }
            temp[y * w + x] = pack_rgba(clamp_u8((int)r), clamp_u8((int)g), 
                                        clamp_u8((int)b), clamp_u8((int)a));
        }
    }
    
    /* Vertical pass */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < kernel_size; k++) {
                int sy = (int)y + k - half;
                if (sy < 0) sy = 0;
                if (sy >= (int)h) sy = h - 1;
                
                uint32_t px = temp[sy * w + x];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                
                float weight = kernel[k];
                r += pr * weight;
                g += pg * weight;
                b += pb * weight;
                a += pa * weight;
            }
            buf->pixels[y * w + x] = pack_rgba(clamp_u8((int)r), clamp_u8((int)g), 
                                               clamp_u8((int)b), clamp_u8((int)a));
        }
    }
    
    free(temp);
    free(kernel);
}

/* Box blur - fast constant-time blur using integral image approach */
void nx_effect_box_blur(nx_effect_buffer_t* buf, uint32_t radius) {
    if (!buf || !buf->pixels || radius == 0) return;
    
    uint32_t w = buf->width;
    uint32_t h = buf->height;
    int r = (int)radius;
    
    uint32_t* temp = malloc(w * h * sizeof(uint32_t));
    if (!temp) return;
    
    /* Horizontal pass */
    for (uint32_t y = 0; y < h; y++) {
        int rsum = 0, gsum = 0, bsum = 0, asum = 0;
        int count = 0;
        
        for (int x = -r; x <= r; x++) {
            int sx = (x < 0) ? 0 : x;
            uint32_t px = buf->pixels[y * w + sx];
            uint8_t pr, pg, pb, pa;
            unpack_rgba(px, &pr, &pg, &pb, &pa);
            rsum += pr; gsum += pg; bsum += pb; asum += pa;
            count++;
        }
        
        for (uint32_t x = 0; x < w; x++) {
            temp[y * w + x] = pack_rgba(rsum / count, gsum / count, 
                                        bsum / count, asum / count);
            
            int left = (int)x - r;
            int right = (int)x + r + 1;
            
            if (left >= 0) {
                uint32_t px = buf->pixels[y * w + left];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                rsum -= pr; gsum -= pg; bsum -= pb; asum -= pa;
                count--;
            }
            if (right < (int)w) {
                uint32_t px = buf->pixels[y * w + right];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                rsum += pr; gsum += pg; bsum += pb; asum += pa;
                count++;
            }
        }
    }
    
    /* Vertical pass */
    for (uint32_t x = 0; x < w; x++) {
        int rsum = 0, gsum = 0, bsum = 0, asum = 0;
        int count = 0;
        
        for (int y = -r; y <= r; y++) {
            int sy = (y < 0) ? 0 : y;
            uint32_t px = temp[sy * w + x];
            uint8_t pr, pg, pb, pa;
            unpack_rgba(px, &pr, &pg, &pb, &pa);
            rsum += pr; gsum += pg; bsum += pb; asum += pa;
            count++;
        }
        
        for (uint32_t y = 0; y < h; y++) {
            buf->pixels[y * w + x] = pack_rgba(rsum / count, gsum / count, 
                                               bsum / count, asum / count);
            
            int top = (int)y - r;
            int bottom = (int)y + r + 1;
            
            if (top >= 0) {
                uint32_t px = temp[top * w + x];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                rsum -= pr; gsum -= pg; bsum -= pb; asum -= pa;
                count--;
            }
            if (bottom < (int)h) {
                uint32_t px = temp[bottom * w + x];
                uint8_t pr, pg, pb, pa;
                unpack_rgba(px, &pr, &pg, &pb, &pa);
                rsum += pr; gsum += pg; bsum += pb; asum += pa;
                count++;
            }
        }
    }
    
    free(temp);
}

void nx_effect_blur(nx_effect_buffer_t* buf, const nx_blur_params_t* params) {
    if (!buf || !params) return;
    
    switch (params->type) {
        case NX_BLUR_GAUSSIAN:
            nx_effect_gaussian_blur(buf, params->radius);
            break;
        case NX_BLUR_BOX:
            nx_effect_box_blur(buf, (uint32_t)params->radius);
            break;
        case NX_BLUR_MOTION:
        case NX_BLUR_RADIAL:
            nx_effect_gaussian_blur(buf, params->radius);
            break;
    }
}

/* ============================================================================
 * Glass Effect
 * ============================================================================ */

void nx_effect_glass(nx_effect_buffer_t* buf, 
                     const nx_effect_buffer_t* background,
                     const nx_glass_effect_t* params,
                     nx_rect_t region) {
    if (!buf || !background || !params) return;
    
    nx_effect_buffer_t* blurred = nx_effect_buffer_create(background->width, background->height);
    if (!blurred) return;
    
    nx_effect_buffer_copy(blurred, background);
    nx_effect_gaussian_blur(blurred, params->blur_radius);
    
    uint32_t x0 = (region.x < 0) ? 0 : (uint32_t)region.x;
    uint32_t y0 = (region.y < 0) ? 0 : (uint32_t)region.y;
    uint32_t x1 = x0 + region.width;
    uint32_t y1 = y0 + region.height;
    if (x1 > buf->width) x1 = buf->width;
    if (y1 > buf->height) y1 = buf->height;
    
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            if (x >= blurred->width || y >= blurred->height) continue;
            
            uint32_t bg_px = blurred->pixels[y * blurred->width + x];
            uint8_t br, bg, bb, ba;
            unpack_rgba(bg_px, &br, &bg, &bb, &ba);
            
            float t = params->opacity;
            int r = (int)(br * (1.0f - t) + params->tint_color.r * t);
            int g = (int)(bg * (1.0f - t) + params->tint_color.g * t);
            int b = (int)(bb * (1.0f - t) + params->tint_color.b * t);
            int a = (int)(ba * (1.0f - t) + params->tint_color.a * t);
            
            if (params->specular_highlight) {
                float nx = (float)(x - x0) / (float)(x1 - x0) - 0.5f;
                float ny = (float)(y - y0) / (float)(y1 - y0) - 0.3f;
                float dist = sqrtf(nx * nx + ny * ny);
                float spec = expf(-dist * dist * 8.0f) * params->highlight_intensity * 255.0f;
                r = clamp_u8(r + (int)spec);
                g = clamp_u8(g + (int)spec);
                b = clamp_u8(b + (int)spec);
            }
            
            buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(r), clamp_u8(g), 
                                                        clamp_u8(b), clamp_u8(a));
        }
    }
    
    nx_effect_buffer_destroy(blurred);
}

/* ============================================================================
 * Frost Effect
 * ============================================================================ */

static float noise2d(float x, float y, uint32_t seed) {
    uint32_t n = (uint32_t)(x * 57.0f + y * 13.0f + seed);
    n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f;
}

void nx_effect_frost(nx_effect_buffer_t* buf,
                     const nx_effect_buffer_t* background,
                     const nx_frost_effect_t* params,
                     nx_rect_t region) {
    if (!buf || !background || !params) return;
    
    nx_effect_buffer_t* blurred = nx_effect_buffer_create(background->width, background->height);
    if (!blurred) return;
    
    nx_effect_buffer_copy(blurred, background);
    nx_effect_gaussian_blur(blurred, params->blur_radius);
    
    uint32_t x0 = (region.x < 0) ? 0 : (uint32_t)region.x;
    uint32_t y0 = (region.y < 0) ? 0 : (uint32_t)region.y;
    uint32_t x1 = x0 + region.width;
    uint32_t y1 = y0 + region.height;
    if (x1 > buf->width) x1 = buf->width;
    if (y1 > buf->height) y1 = buf->height;
    
    float grain = params->grain_size > 0 ? params->grain_size : 1.0f;
    float intensity = params->frost_intensity / 100.0f;
    
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            if (x >= blurred->width || y >= blurred->height) continue;
            
            uint32_t bg_px = blurred->pixels[y * blurred->width + x];
            uint8_t br, bg, bb, ba;
            unpack_rgba(bg_px, &br, &bg, &bb, &ba);
            
            float n = noise2d(x / grain, y / grain, params->seed);
            n = (n + 1.0f) * 0.5f;
            
            float frost = n * intensity;
            float t = params->opacity;
            
            int r = (int)(br * (1.0f - t) + (br + frost * 80) * t);
            int g = (int)(bg * (1.0f - t) + (bg + frost * 80) * t);
            int b = (int)(bb * (1.0f - t) + (bb + frost * 80) * t);
            
            r = (int)(r * (1.0f - t * 0.3f) + params->frost_color.r * t * 0.3f);
            g = (int)(g * (1.0f - t * 0.3f) + params->frost_color.g * t * 0.3f);
            b = (int)(b * (1.0f - t * 0.3f) + params->frost_color.b * t * 0.3f);
            
            buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(r), clamp_u8(g), 
                                                        clamp_u8(b), clamp_u8((int)(ba * t)));
        }
    }
    
    nx_effect_buffer_destroy(blurred);
}

/* ============================================================================
 * Tilt-Shift Effect
 * ============================================================================ */

void nx_effect_tiltshift(nx_effect_buffer_t* buf, const nx_tiltshift_effect_t* params) {
    if (!buf || !params) return;
    
    nx_effect_buffer_t* blurred = nx_effect_buffer_create(buf->width, buf->height);
    if (!blurred) return;
    
    nx_effect_buffer_copy(blurred, buf);
    nx_effect_gaussian_blur(blurred, params->blur_strength);
    
    float cx = params->focus_point.x;
    float cy = params->focus_point.y;
    float focus_r = params->focus_radius;
    float falloff = params->falloff > 0 ? params->falloff : 1.0f;
    float angle_rad = params->angle * (float)M_PI / 180.0f;
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    for (uint32_t y = 0; y < buf->height; y++) {
        for (uint32_t x = 0; x < buf->width; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            
            float rotated_y = -dx * sin_a + dy * cos_a;
            float dist = fabsf(rotated_y);
            
            float blur_amount = 0.0f;
            if (dist > focus_r) {
                blur_amount = powf((dist - focus_r) / (focus_r * falloff), 2.0f);
                if (blur_amount > 1.0f) blur_amount = 1.0f;
            }
            
            uint32_t orig_px = buf->pixels[y * buf->width + x];
            uint32_t blur_px = blurred->pixels[y * blurred->width + x];
            
            uint8_t or, og, ob, oa;
            uint8_t br, bg, bb, ba;
            unpack_rgba(orig_px, &or, &og, &ob, &oa);
            unpack_rgba(blur_px, &br, &bg, &bb, &ba);
            
            float t = blur_amount;
            int r = (int)(or * (1.0f - t) + br * t);
            int g = (int)(og * (1.0f - t) + bg * t);
            int b = (int)(ob * (1.0f - t) + bb * t);
            int a = (int)(oa * (1.0f - t) + ba * t);
            
            buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(r), clamp_u8(g), 
                                                        clamp_u8(b), clamp_u8(a));
        }
    }
    
    nx_effect_buffer_destroy(blurred);
}

/* ============================================================================
 * Orb/Sphere Effect (using lighting system)
 * ============================================================================ */

void nx_effect_orb(nx_effect_buffer_t* buf,
                   const nx_orb_effect_t* params,
                   nx_point_t center,
                   uint32_t radius) {
    if (!buf || !params || radius == 0) return;
    
    float r = (float)radius;
    float cx = (float)center.x;
    float cy = (float)center.y;
    
    nx_vec3_t light_dir = nx_vec3_normalize(params->light_position);
    nx_vec3_t view_dir = nx_vec3(0, 0, 1);
    
    for (uint32_t y = 0; y < buf->height; y++) {
        for (uint32_t x = 0; x < buf->width; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float d2 = dx * dx + dy * dy;
            
            if (d2 > r * r) continue;
            
            float z = sqrtf(r * r - d2);
            nx_vec3_t normal = nx_vec3_normalize(nx_vec3(dx, dy, z));
            
            float ndotl = nx_vec3_dot(normal, light_dir);
            if (ndotl < 0) ndotl = 0;
            
            float diffuse = ndotl * (1.0f - params->metalness * 0.5f);
            
            nx_vec3_t half_vec = nx_vec3_normalize(nx_vec3_add(light_dir, view_dir));
            float ndoth = nx_vec3_dot(normal, half_vec);
            if (ndoth < 0) ndoth = 0;
            float shininess = (1.0f - params->roughness) * 128.0f + 8.0f;
            float specular = powf(ndoth, shininess) * params->specularity;
            
            float ao = 1.0f - params->ambient_occlusion * (1.0f - ndotl) * 0.5f;
            
            float rim = 1.0f - nx_vec3_dot(normal, view_dir);
            rim = powf(rim, 3.0f) * params->rim_light;
            
            float shade = (diffuse + specular * (1.0f - params->roughness) + rim) * ao;
            shade = clampf(shade, 0.0f, 1.5f);
            
            int pr = (int)(params->base_color.r * shade);
            int pg = (int)(params->base_color.g * shade);
            int pb = (int)(params->base_color.b * shade);
            int pa = params->base_color.a;
            
            if (specular > 0.5f) {
                float spec_add = (specular - 0.5f) * 2.0f * 255.0f;
                pr = clamp_u8(pr + (int)spec_add);
                pg = clamp_u8(pg + (int)spec_add);
                pb = clamp_u8(pb + (int)spec_add);
            }
            
            buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(pr), clamp_u8(pg), 
                                                        clamp_u8(pb), clamp_u8(pa));
        }
    }
}

/* ============================================================================
 * Glow Effect
 * ============================================================================ */

void nx_effect_glow(nx_effect_buffer_t* buf, 
                    const nx_effect_buffer_t* mask,
                    const nx_glow_effect_t* params) {
    if (!buf || !mask || !params) return;
    
    nx_effect_buffer_t* glow = nx_effect_buffer_create(mask->width, mask->height);
    if (!glow) return;
    
    for (uint32_t y = 0; y < mask->height; y++) {
        for (uint32_t x = 0; x < mask->width; x++) {
            uint32_t px = mask->pixels[y * mask->width + x];
            uint8_t a = (px >> 24) & 0xFF;
            if (a > 0) {
                glow->pixels[y * glow->width + x] = pack_rgba(
                    params->color.r, params->color.g, params->color.b, a);
            }
        }
    }
    
    nx_effect_gaussian_blur(glow, params->radius);
    
    for (uint32_t y = 0; y < buf->height && y < glow->height; y++) {
        for (uint32_t x = 0; x < buf->width && x < glow->width; x++) {
            uint32_t glow_px = glow->pixels[y * glow->width + x];
            uint32_t buf_px = buf->pixels[y * buf->width + x];
            
            uint8_t gr, gg, gb, ga;
            uint8_t br, bg, bb, ba;
            unpack_rgba(glow_px, &gr, &gg, &gb, &ga);
            unpack_rgba(buf_px, &br, &bg, &bb, &ba);
            
            float t = (ga / 255.0f) * params->intensity;
            if (t > 1.0f) t = 1.0f;
            
            int r = (int)(br + gr * t);
            int g = (int)(bg + gg * t);
            int b = (int)(bb + gb * t);
            
            buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(r), clamp_u8(g), 
                                                        clamp_u8(b), ba);
        }
    }
    
    nx_effect_buffer_destroy(glow);
}

/* ============================================================================
 * Shadow Effect
 * ============================================================================ */

void nx_effect_shadow(nx_effect_buffer_t* buf,
                      const nx_effect_buffer_t* mask,
                      const nx_shadow_effect_t* params) {
    if (!buf || !mask || !params) return;
    
    nx_effect_buffer_t* shadow = nx_effect_buffer_create(mask->width, mask->height);
    if (!shadow) return;
    
    /* Create shadow from mask alpha channel */
    for (uint32_t y = 0; y < mask->height; y++) {
        for (uint32_t x = 0; x < mask->width; x++) {
            uint32_t px = mask->pixels[y * mask->width + x];
            uint8_t a = (px >> 24) & 0xFF;
            if (a > 0) {
                uint8_t sa = (uint8_t)(a * params->opacity);
                shadow->pixels[y * shadow->width + x] = pack_rgba(
                    params->color.r, params->color.g, params->color.b, sa);
            }
        }
    }
    
    /* Apply blur to shadow */
    if (params->blur > 0) {
        nx_effect_gaussian_blur(shadow, params->blur);
    }
    
    /* Apply spread (expand/contract shadow) - simplified as additional blur */
    if (params->spread > 0) {
        nx_effect_gaussian_blur(shadow, params->spread * 0.5f);
    }
    
    /* Composite shadow with offset */
    int32_t off_x = (int32_t)params->offset_x;
    int32_t off_y = (int32_t)params->offset_y;
    
    for (uint32_t y = 0; y < buf->height; y++) {
        for (uint32_t x = 0; x < buf->width; x++) {
            int32_t sx = (int32_t)x - off_x;
            int32_t sy = (int32_t)y - off_y;
            
            if (sx >= 0 && sy >= 0 && (uint32_t)sx < shadow->width && (uint32_t)sy < shadow->height) {
                uint32_t shadow_px = shadow->pixels[sy * shadow->width + sx];
                uint32_t buf_px = buf->pixels[y * buf->width + x];
                
                uint8_t sr, sg, sb, sa;
                uint8_t br, bg, bb, ba;
                unpack_rgba(shadow_px, &sr, &sg, &sb, &sa);
                unpack_rgba(buf_px, &br, &bg, &bb, &ba);
                
                /* Alpha composite shadow under the buffer content */
                float t = sa / 255.0f;
                int r = (int)(br * (1.0f - t) + sr * t);
                int g = (int)(bg * (1.0f - t) + sg * t);
                int b = (int)(bb * (1.0f - t) + sb * t);
                
                /* Only apply shadow where original is transparent */
                if (ba < 128) {
                    buf->pixels[y * buf->width + x] = pack_rgba(clamp_u8(r), clamp_u8(g), 
                                                                clamp_u8(b), clamp_u8((int)(ba + sa * 0.5f)));
                }
            }
        }
    }
    
    nx_effect_buffer_destroy(shadow);
}

/* ============================================================================
 * Color Adjustment
 * ============================================================================ */

void nx_effect_color_adjust(nx_effect_buffer_t* buf, const nx_color_adjust_t* params) {
    if (!buf || !params) return;
    
    float brightness = params->brightness / 100.0f;
    float contrast = (params->contrast + 100.0f) / 100.0f;
    float saturation = (params->saturation + 100.0f) / 100.0f;
    
    for (uint32_t i = 0; i < buf->width * buf->height; i++) {
        uint8_t r, g, b, a;
        unpack_rgba(buf->pixels[i], &r, &g, &b, &a);
        
        float fr = r / 255.0f;
        float fg = g / 255.0f;
        float fb = b / 255.0f;
        
        fr += brightness;
        fg += brightness;
        fb += brightness;
        
        fr = (fr - 0.5f) * contrast + 0.5f;
        fg = (fg - 0.5f) * contrast + 0.5f;
        fb = (fb - 0.5f) * contrast + 0.5f;
        
        float gray = fr * 0.299f + fg * 0.587f + fb * 0.114f;
        fr = gray + (fr - gray) * saturation;
        fg = gray + (fg - gray) * saturation;
        fb = gray + (fb - gray) * saturation;
        
        buf->pixels[i] = pack_rgba(clamp_u8((int)(fr * 255)), 
                                   clamp_u8((int)(fg * 255)), 
                                   clamp_u8((int)(fb * 255)), a);
    }
}

/* ============================================================================
 * Blend Modes
 * ============================================================================ */

static inline float blend_multiply(float a, float b) { return a * b; }
static inline float blend_screen(float a, float b) { return 1.0f - (1.0f - a) * (1.0f - b); }
static inline float blend_overlay(float a, float b) { 
    return (a < 0.5f) ? (2.0f * a * b) : (1.0f - 2.0f * (1.0f - a) * (1.0f - b)); 
}

void nx_effect_composite(nx_effect_buffer_t* dst,
                         const nx_effect_buffer_t* src,
                         nx_blend_mode_t mode,
                         float opacity) {
    if (!dst || !src) return;
    
    uint32_t w = (dst->width < src->width) ? dst->width : src->width;
    uint32_t h = (dst->height < src->height) ? dst->height : src->height;
    
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t dr, dg, db, da;
            uint8_t sr, sg, sb, sa;
            unpack_rgba(dst->pixels[y * dst->width + x], &dr, &dg, &db, &da);
            unpack_rgba(src->pixels[y * src->width + x], &sr, &sg, &sb, &sa);
            
            float d_r = dr / 255.0f, d_g = dg / 255.0f, d_b = db / 255.0f;
            float s_r = sr / 255.0f, s_g = sg / 255.0f, s_b = sb / 255.0f;
            float r, g, b;
            
            switch (mode) {
                case NX_BLEND_MULTIPLY:
                    r = blend_multiply(d_r, s_r);
                    g = blend_multiply(d_g, s_g);
                    b = blend_multiply(d_b, s_b);
                    break;
                case NX_BLEND_SCREEN:
                    r = blend_screen(d_r, s_r);
                    g = blend_screen(d_g, s_g);
                    b = blend_screen(d_b, s_b);
                    break;
                case NX_BLEND_OVERLAY:
                    r = blend_overlay(d_r, s_r);
                    g = blend_overlay(d_g, s_g);
                    b = blend_overlay(d_b, s_b);
                    break;
                case NX_BLEND_ADD:
                    r = d_r + s_r;
                    g = d_g + s_g;
                    b = d_b + s_b;
                    break;
                default:
                    r = s_r; g = s_g; b = s_b;
                    break;
            }
            
            float t = (sa / 255.0f) * opacity;
            r = d_r * (1.0f - t) + r * t;
            g = d_g * (1.0f - t) + g * t;
            b = d_b * (1.0f - t) + b * t;
            
            dst->pixels[y * dst->width + x] = pack_rgba(
                clamp_u8((int)(r * 255)), clamp_u8((int)(g * 255)), 
                clamp_u8((int)(b * 255)), da);
        }
    }
}

/* ============================================================================
 * Effect Presets
 * ============================================================================ */

nx_glass_effect_t nx_glass_preset_default(void) {
    return (nx_glass_effect_t){
        .blur_radius = 20.0f,
        .opacity = 0.7f,
        .refraction = 0.1f,
        .tint_color = {255, 255, 255, 200},
        .border_opacity = 0.3f,
        .border_width = 1.0f,
        .specular_highlight = true,
        .highlight_intensity = 0.4f
    };
}

nx_glass_effect_t nx_glass_preset_dark(void) {
    return (nx_glass_effect_t){
        .blur_radius = 25.0f,
        .opacity = 0.85f,
        .refraction = 0.15f,
        .tint_color = {20, 20, 25, 220},
        .border_opacity = 0.2f,
        .border_width = 1.0f,
        .specular_highlight = true,
        .highlight_intensity = 0.3f
    };
}

nx_frost_effect_t nx_frost_preset_default(void) {
    return (nx_frost_effect_t){
        .frost_intensity = 50.0f,
        .blur_radius = 15.0f,
        .grain_size = 2.0f,
        .opacity = 0.8f,
        .frost_color = {230, 240, 255, 255},
        .seed = 12345
    };
}

nx_orb_effect_t nx_orb_preset_glossy(nx_color_t color) {
    return (nx_orb_effect_t){
        .light_position = {-0.5f, -0.7f, 1.0f},
        .specularity = 0.9f,
        .metalness = 0.0f,
        .roughness = 0.1f,
        .base_color = color,
        .ambient_occlusion = 0.3f,
        .rim_light = 0.4f,
        .environment_map = false
    };
}

nx_orb_effect_t nx_orb_preset_metallic(nx_color_t color) {
    return (nx_orb_effect_t){
        .light_position = {-0.4f, -0.6f, 1.0f},
        .specularity = 0.8f,
        .metalness = 0.9f,
        .roughness = 0.3f,
        .base_color = color,
        .ambient_occlusion = 0.4f,
        .rim_light = 0.6f,
        .environment_map = true
    };
}

nx_glow_effect_t nx_glow_preset_soft(nx_color_t color) {
    return (nx_glow_effect_t){
        .type = NX_GLOW_OUTER,
        .color = color,
        .radius = 15.0f,
        .intensity = 0.6f,
        .falloff = 1.5f,
        .knockout = false
    };
}

nx_glow_effect_t nx_glow_preset_neon(nx_color_t color) {
    return (nx_glow_effect_t){
        .type = NX_GLOW_BOTH,
        .color = color,
        .radius = 25.0f,
        .intensity = 1.2f,
        .falloff = 2.0f,
        .knockout = false
    };
}
