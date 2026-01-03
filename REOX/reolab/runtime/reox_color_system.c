/*
 * REOX Color System - Implementation
 * Advanced color management and manipulation
 */

#include "reox_color_system.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Helper macros */
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))
#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))

/* ============================================================================
 * Color Space Conversions
 * ============================================================================ */

rx_hsl rx_rgb_to_hsl(rx_color rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    
    float max = MAX3(r, g, b);
    float min = MIN3(r, g, b);
    float l = (max + min) / 2.0f;
    float h = 0, s = 0;
    
    if (max != min) {
        float d = max - min;
        s = l > 0.5f ? d / (2.0f - max - min) : d / (max + min);
        
        if (max == r) {
            h = (g - b) / d + (g < b ? 6.0f : 0);
        } else if (max == g) {
            h = (b - r) / d + 2.0f;
        } else {
            h = (r - g) / d + 4.0f;
        }
        h *= 60.0f;
    }
    
    return (rx_hsl){ h, s, l, rgb.a / 255.0f };
}

static float hue_to_rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6) return p + (q - p) * 6 * t;
    if (t < 1.0f/2) return q;
    if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
    return p;
}

rx_color rx_hsl_to_rgb(rx_hsl hsl) {
    float r, g, b;
    
    if (hsl.s == 0) {
        r = g = b = hsl.l;
    } else {
        float h = hsl.h / 360.0f;
        float q = hsl.l < 0.5f ? hsl.l * (1 + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
        float p = 2 * hsl.l - q;
        r = hue_to_rgb(p, q, h + 1.0f/3);
        g = hue_to_rgb(p, q, h);
        b = hue_to_rgb(p, q, h - 1.0f/3);
    }
    
    return (rx_color){
        (uint8_t)(r * 255),
        (uint8_t)(g * 255),
        (uint8_t)(b * 255),
        (uint8_t)(hsl.a * 255)
    };
}

rx_hsv rx_rgb_to_hsv(rx_color rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    
    float max = MAX3(r, g, b);
    float min = MIN3(r, g, b);
    float v = max;
    float h = 0, s = 0;
    
    float d = max - min;
    s = max == 0 ? 0 : d / max;
    
    if (max != min) {
        if (max == r) {
            h = (g - b) / d + (g < b ? 6 : 0);
        } else if (max == g) {
            h = (b - r) / d + 2;
        } else {
            h = (r - g) / d + 4;
        }
        h *= 60;
    }
    
    return (rx_hsv){ h, s, v, rgb.a / 255.0f };
}

rx_color rx_hsv_to_rgb(rx_hsv hsv) {
    float r, g, b;
    
    int i = (int)(hsv.h / 60) % 6;
    float f = hsv.h / 60 - i;
    float p = hsv.v * (1 - hsv.s);
    float q = hsv.v * (1 - f * hsv.s);
    float t = hsv.v * (1 - (1 - f) * hsv.s);
    
    switch (i) {
        case 0: r = hsv.v; g = t; b = p; break;
        case 1: r = q; g = hsv.v; b = p; break;
        case 2: r = p; g = hsv.v; b = t; break;
        case 3: r = p; g = q; b = hsv.v; break;
        case 4: r = t; g = p; b = hsv.v; break;
        default: r = hsv.v; g = p; b = q; break;
    }
    
    return (rx_color){
        (uint8_t)(r * 255),
        (uint8_t)(g * 255),
        (uint8_t)(b * 255),
        (uint8_t)(hsv.a * 255)
    };
}

rx_xyz rx_rgb_to_xyz(rx_color rgb) {
    /* sRGB to linear */
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    
    r = r > 0.04045f ? powf((r + 0.055f) / 1.055f, 2.4f) : r / 12.92f;
    g = g > 0.04045f ? powf((g + 0.055f) / 1.055f, 2.4f) : g / 12.92f;
    b = b > 0.04045f ? powf((b + 0.055f) / 1.055f, 2.4f) : b / 12.92f;
    
    r *= 100; g *= 100; b *= 100;
    
    return (rx_xyz){
        r * 0.4124564f + g * 0.3575761f + b * 0.1804375f,
        r * 0.2126729f + g * 0.7151522f + b * 0.0721750f,
        r * 0.0193339f + g * 0.1191920f + b * 0.9503041f,
        rgb.a / 255.0f
    };
}

rx_color rx_xyz_to_rgb(rx_xyz xyz) {
    float x = xyz.x / 100;
    float y = xyz.y / 100;
    float z = xyz.z / 100;
    
    float r = x *  3.2404542f + y * -1.5371385f + z * -0.4985314f;
    float g = x * -0.9692660f + y *  1.8760108f + z *  0.0415560f;
    float b = x *  0.0556434f + y * -0.2040259f + z *  1.0572252f;
    
    r = r > 0.0031308f ? 1.055f * powf(r, 1/2.4f) - 0.055f : 12.92f * r;
    g = g > 0.0031308f ? 1.055f * powf(g, 1/2.4f) - 0.055f : 12.92f * g;
    b = b > 0.0031308f ? 1.055f * powf(b, 1/2.4f) - 0.055f : 12.92f * b;
    
    return (rx_color){
        (uint8_t)CLAMP(r * 255, 0, 255),
        (uint8_t)CLAMP(g * 255, 0, 255),
        (uint8_t)CLAMP(b * 255, 0, 255),
        (uint8_t)(xyz.alpha * 255)
    };
}

rx_lab rx_xyz_to_lab(rx_xyz xyz) {
    /* D65 reference white */
    float x = xyz.x / 95.047f;
    float y = xyz.y / 100.0f;
    float z = xyz.z / 108.883f;
    
    x = x > 0.008856f ? powf(x, 1.0f/3) : (7.787f * x) + 16.0f/116;
    y = y > 0.008856f ? powf(y, 1.0f/3) : (7.787f * y) + 16.0f/116;
    z = z > 0.008856f ? powf(z, 1.0f/3) : (7.787f * z) + 16.0f/116;
    
    return (rx_lab){
        (116 * y) - 16,
        500 * (x - y),
        200 * (y - z),
        xyz.alpha
    };
}

rx_xyz rx_lab_to_xyz(rx_lab lab) {
    float y = (lab.l + 16) / 116;
    float x = lab.a / 500 + y;
    float z = y - lab.b / 200;
    
    float x3 = x * x * x;
    float y3 = y * y * y;
    float z3 = z * z * z;
    
    x = x3 > 0.008856f ? x3 : (x - 16.0f/116) / 7.787f;
    y = y3 > 0.008856f ? y3 : (y - 16.0f/116) / 7.787f;
    z = z3 > 0.008856f ? z3 : (z - 16.0f/116) / 7.787f;
    
    return (rx_xyz){
        x * 95.047f,
        y * 100.0f,
        z * 108.883f,
        lab.alpha
    };
}

rx_lab rx_rgb_to_lab(rx_color rgb) {
    return rx_xyz_to_lab(rx_rgb_to_xyz(rgb));
}

rx_color rx_lab_to_rgb(rx_lab lab) {
    return rx_xyz_to_rgb(rx_lab_to_xyz(lab));
}

rx_lch rx_lab_to_lch(rx_lab lab) {
    float c = sqrtf(lab.a * lab.a + lab.b * lab.b);
    float h = atan2f(lab.b, lab.a) * 180 / M_PI;
    if (h < 0) h += 360;
    
    return (rx_lch){ lab.l, c, h, lab.alpha };
}

rx_lab rx_lch_to_lab(rx_lch lch) {
    float h_rad = lch.h * M_PI / 180;
    return (rx_lab){
        lch.l,
        lch.c * cosf(h_rad),
        lch.c * sinf(h_rad),
        lch.alpha
    };
}

rx_lch rx_rgb_to_lch(rx_color rgb) {
    return rx_lab_to_lch(rx_rgb_to_lab(rgb));
}

rx_color rx_lch_to_rgb(rx_lch lch) {
    return rx_lab_to_rgb(rx_lch_to_lab(lch));
}

/* ============================================================================
 * Color Manipulation
 * ============================================================================ */

rx_color color_lighten(rx_color c, float amount) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.l = CLAMP(hsl.l + amount, 0, 1);
    return rx_hsl_to_rgb(hsl);
}

rx_color color_darken(rx_color c, float amount) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.l = CLAMP(hsl.l - amount, 0, 1);
    return rx_hsl_to_rgb(hsl);
}

rx_color color_blend(rx_color a, rx_color b, float t) {
    return (rx_color){
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
        (uint8_t)(a.a + (b.a - a.a) * t)
    };
}

rx_color color_with_alpha(rx_color c, uint8_t alpha) {
    return (rx_color){ c.r, c.g, c.b, alpha };
}

rx_color rx_color_adjust_hue(rx_color c, float degrees) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.h = fmodf(hsl.h + degrees, 360.0f);
    if (hsl.h < 0) hsl.h += 360;
    return rx_hsl_to_rgb(hsl);
}

rx_color rx_color_adjust_saturation(rx_color c, float amount) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.s = CLAMP(hsl.s + amount, 0, 1);
    return rx_hsl_to_rgb(hsl);
}

rx_color rx_color_adjust_lightness(rx_color c, float amount) {
    return color_lighten(c, amount);
}

rx_color rx_color_adjust_brightness(rx_color c, float amount) {
    rx_hsv hsv = rx_rgb_to_hsv(c);
    hsv.v = CLAMP(hsv.v + amount, 0, 1);
    return rx_hsv_to_rgb(hsv);
}

rx_color rx_color_adjust_contrast(rx_color c, float amount) {
    float factor = (259 * (amount * 255 + 255)) / (255 * (259 - amount * 255));
    return (rx_color){
        (uint8_t)CLAMP(factor * (c.r - 128) + 128, 0, 255),
        (uint8_t)CLAMP(factor * (c.g - 128) + 128, 0, 255),
        (uint8_t)CLAMP(factor * (c.b - 128) + 128, 0, 255),
        c.a
    };
}

rx_color rx_color_mix(rx_color a, rx_color b, float t) {
    return color_blend(a, b, t);
}

rx_color rx_color_mix_lab(rx_color a, rx_color b, float t) {
    rx_lab lab_a = rx_rgb_to_lab(a);
    rx_lab lab_b = rx_rgb_to_lab(b);
    
    rx_lab result = {
        lab_a.l + (lab_b.l - lab_a.l) * t,
        lab_a.a + (lab_b.a - lab_a.a) * t,
        lab_a.b + (lab_b.b - lab_a.b) * t,
        lab_a.alpha + (lab_b.alpha - lab_a.alpha) * t
    };
    
    return rx_lab_to_rgb(result);
}

rx_color rx_color_mix_lch(rx_color a, rx_color b, float t) {
    rx_lch lch_a = rx_rgb_to_lch(a);
    rx_lch lch_b = rx_rgb_to_lch(b);
    
    /* Handle hue wrapping */
    float h_diff = lch_b.h - lch_a.h;
    if (h_diff > 180) h_diff -= 360;
    if (h_diff < -180) h_diff += 360;
    
    rx_lch result = {
        lch_a.l + (lch_b.l - lch_a.l) * t,
        lch_a.c + (lch_b.c - lch_a.c) * t,
        fmodf(lch_a.h + h_diff * t + 360, 360),
        lch_a.alpha + (lch_b.alpha - lch_a.alpha) * t
    };
    
    return rx_lch_to_rgb(result);
}

rx_color rx_color_invert(rx_color c) {
    return (rx_color){ 255 - c.r, 255 - c.g, 255 - c.b, c.a };
}

rx_color rx_color_grayscale(rx_color c) {
    /* Luminance-based grayscale */
    uint8_t gray = (uint8_t)(0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
    return (rx_color){ gray, gray, gray, c.a };
}

rx_color rx_color_sepia(rx_color c) {
    uint8_t r = (uint8_t)CLAMP(0.393f * c.r + 0.769f * c.g + 0.189f * c.b, 0, 255);
    uint8_t g = (uint8_t)CLAMP(0.349f * c.r + 0.686f * c.g + 0.168f * c.b, 0, 255);
    uint8_t b = (uint8_t)CLAMP(0.272f * c.r + 0.534f * c.g + 0.131f * c.b, 0, 255);
    return (rx_color){ r, g, b, c.a };
}

rx_color rx_color_complement(rx_color c) {
    return rx_color_adjust_hue(c, 180);
}

rx_color rx_color_desaturate(rx_color c, float amount) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.s = CLAMP(hsl.s * (1 - amount), 0, 1);
    return rx_hsl_to_rgb(hsl);
}

rx_color rx_color_saturate(rx_color c, float amount) {
    rx_hsl hsl = rx_rgb_to_hsl(c);
    hsl.s = CLAMP(hsl.s * (1 + amount), 0, 1);
    return rx_hsl_to_rgb(hsl);
}

/* ============================================================================
 * Blend Modes
 * ============================================================================ */

static uint8_t blend_multiply(uint8_t base, uint8_t blend) {
    return (base * blend) / 255;
}

static uint8_t blend_screen(uint8_t base, uint8_t blend) {
    return 255 - ((255 - base) * (255 - blend)) / 255;
}

static uint8_t blend_overlay(uint8_t base, uint8_t blend) {
    return base < 128 
        ? (2 * base * blend) / 255 
        : 255 - (2 * (255 - base) * (255 - blend)) / 255;
}

static uint8_t blend_soft_light(uint8_t base, uint8_t blend) {
    float b = base / 255.0f;
    float l = blend / 255.0f;
    float result;
    
    if (l <= 0.5f) {
        result = b - (1 - 2*l) * b * (1 - b);
    } else {
        float d = b <= 0.25f ? ((16*b - 12)*b + 4)*b : sqrtf(b);
        result = b + (2*l - 1) * (d - b);
    }
    
    return (uint8_t)(result * 255);
}

static uint8_t blend_hard_light(uint8_t base, uint8_t blend) {
    return blend < 128 
        ? (2 * base * blend) / 255 
        : 255 - (2 * (255 - base) * (255 - blend)) / 255;
}

rx_color rx_color_blend(rx_color base, rx_color blend, rx_blend_mode mode) {
    rx_color result;
    result.a = base.a;
    
    switch (mode) {
        case RX_BLEND_NORMAL:
            result = blend;
            break;
            
        case RX_BLEND_MULTIPLY:
            result.r = blend_multiply(base.r, blend.r);
            result.g = blend_multiply(base.g, blend.g);
            result.b = blend_multiply(base.b, blend.b);
            break;
            
        case RX_BLEND_SCREEN:
            result.r = blend_screen(base.r, blend.r);
            result.g = blend_screen(base.g, blend.g);
            result.b = blend_screen(base.b, blend.b);
            break;
            
        case RX_BLEND_OVERLAY:
            result.r = blend_overlay(base.r, blend.r);
            result.g = blend_overlay(base.g, blend.g);
            result.b = blend_overlay(base.b, blend.b);
            break;
            
        case RX_BLEND_DARKEN:
            result.r = base.r < blend.r ? base.r : blend.r;
            result.g = base.g < blend.g ? base.g : blend.g;
            result.b = base.b < blend.b ? base.b : blend.b;
            break;
            
        case RX_BLEND_LIGHTEN:
            result.r = base.r > blend.r ? base.r : blend.r;
            result.g = base.g > blend.g ? base.g : blend.g;
            result.b = base.b > blend.b ? base.b : blend.b;
            break;
            
        case RX_BLEND_COLOR_DODGE:
            result.r = blend.r == 255 ? 255 : (uint8_t)CLAMP(base.r * 255 / (255 - blend.r), 0, 255);
            result.g = blend.g == 255 ? 255 : (uint8_t)CLAMP(base.g * 255 / (255 - blend.g), 0, 255);
            result.b = blend.b == 255 ? 255 : (uint8_t)CLAMP(base.b * 255 / (255 - blend.b), 0, 255);
            break;
            
        case RX_BLEND_COLOR_BURN:
            result.r = blend.r == 0 ? 0 : (uint8_t)CLAMP(255 - (255 - base.r) * 255 / blend.r, 0, 255);
            result.g = blend.g == 0 ? 0 : (uint8_t)CLAMP(255 - (255 - base.g) * 255 / blend.g, 0, 255);
            result.b = blend.b == 0 ? 0 : (uint8_t)CLAMP(255 - (255 - base.b) * 255 / blend.b, 0, 255);
            break;
            
        case RX_BLEND_HARD_LIGHT:
            result.r = blend_hard_light(base.r, blend.r);
            result.g = blend_hard_light(base.g, blend.g);
            result.b = blend_hard_light(base.b, blend.b);
            break;
            
        case RX_BLEND_SOFT_LIGHT:
            result.r = blend_soft_light(base.r, blend.r);
            result.g = blend_soft_light(base.g, blend.g);
            result.b = blend_soft_light(base.b, blend.b);
            break;
            
        case RX_BLEND_DIFFERENCE:
            result.r = base.r > blend.r ? base.r - blend.r : blend.r - base.r;
            result.g = base.g > blend.g ? base.g - blend.g : blend.g - base.g;
            result.b = base.b > blend.b ? base.b - blend.b : blend.b - base.b;
            break;
            
        case RX_BLEND_EXCLUSION:
            result.r = base.r + blend.r - 2 * base.r * blend.r / 255;
            result.g = base.g + blend.g - 2 * base.g * blend.g / 255;
            result.b = base.b + blend.b - 2 * base.b * blend.b / 255;
            break;
            
        case RX_BLEND_PLUS_LIGHTER:
            result.r = (uint8_t)CLAMP(base.r + blend.r, 0, 255);
            result.g = (uint8_t)CLAMP(base.g + blend.g, 0, 255);
            result.b = (uint8_t)CLAMP(base.b + blend.b, 0, 255);
            break;
            
        default:
            result = blend;
            break;
    }
    
    return result;
}

rx_color rx_color_blend_alpha(rx_color base, rx_color blend, rx_blend_mode mode, float alpha) {
    rx_color blended = rx_color_blend(base, blend, mode);
    return rx_color_mix(base, blended, alpha);
}

/* ============================================================================
 * Gradient System
 * ============================================================================ */

static int compare_stops(const void* a, const void* b) {
    const rx_gradient_stop* sa = (const rx_gradient_stop*)a;
    const rx_gradient_stop* sb = (const rx_gradient_stop*)b;
    if (sa->position < sb->position) return -1;
    if (sa->position > sb->position) return 1;
    return 0;
}

rx_gradient* rx_gradient_linear(float angle, rx_gradient_stop* stops, size_t count) {
    rx_gradient* grad = (rx_gradient*)calloc(1, sizeof(rx_gradient));
    if (!grad) return NULL;
    
    grad->type = RX_GRADIENT_LINEAR;
    grad->angle = angle;
    grad->stop_count = count;
    grad->stops = (rx_gradient_stop*)malloc(sizeof(rx_gradient_stop) * count);
    memcpy(grad->stops, stops, sizeof(rx_gradient_stop) * count);
    
    /* Sort stops by position */
    qsort(grad->stops, count, sizeof(rx_gradient_stop), compare_stops);
    
    return grad;
}

rx_gradient* rx_gradient_radial(rx_point center, float radius, rx_gradient_stop* stops, size_t count) {
    rx_gradient* grad = (rx_gradient*)calloc(1, sizeof(rx_gradient));
    if (!grad) return NULL;
    
    grad->type = RX_GRADIENT_RADIAL;
    grad->center = center;
    grad->radius = radius;
    grad->stop_count = count;
    grad->stops = (rx_gradient_stop*)malloc(sizeof(rx_gradient_stop) * count);
    memcpy(grad->stops, stops, sizeof(rx_gradient_stop) * count);
    
    qsort(grad->stops, count, sizeof(rx_gradient_stop), compare_stops);
    
    return grad;
}

void rx_gradient_add_stop(rx_gradient* grad, rx_color color, float position) {
    if (!grad) return;
    
    grad->stops = (rx_gradient_stop*)realloc(grad->stops, 
        sizeof(rx_gradient_stop) * (grad->stop_count + 1));
    
    grad->stops[grad->stop_count].color = color;
    grad->stops[grad->stop_count].position = position;
    grad->stop_count++;
    
    qsort(grad->stops, grad->stop_count, sizeof(rx_gradient_stop), compare_stops);
}

rx_color rx_gradient_color_at(rx_gradient* grad, float position) {
    if (!grad || grad->stop_count == 0) {
        return (rx_color){0, 0, 0, 255};
    }
    
    if (grad->stop_count == 1) {
        return grad->stops[0].color;
    }
    
    position = CLAMP(position, 0, 1);
    
    /* Find surrounding stops */
    rx_gradient_stop* prev = &grad->stops[0];
    rx_gradient_stop* next = &grad->stops[grad->stop_count - 1];
    
    for (size_t i = 0; i < grad->stop_count - 1; i++) {
        if (position >= grad->stops[i].position && position <= grad->stops[i + 1].position) {
            prev = &grad->stops[i];
            next = &grad->stops[i + 1];
            break;
        }
    }
    
    /* Interpolate */
    float range = next->position - prev->position;
    if (range == 0) return prev->color;
    
    float t = (position - prev->position) / range;
    
    if (grad->use_oklch) {
        return rx_color_mix_lch(prev->color, next->color, t);
    }
    
    return rx_color_mix(prev->color, next->color, t);
}

rx_color rx_gradient_sample(rx_gradient* grad, rx_point pos, rx_rect bounds) {
    if (!grad) return (rx_color){0, 0, 0, 255};
    
    float position = 0;
    
    switch (grad->type) {
        case RX_GRADIENT_LINEAR: {
            float angle_rad = grad->angle * M_PI / 180;
            float dx = cosf(angle_rad);
            float dy = sinf(angle_rad);
            
            float normalized_x = (pos.x - bounds.x) / bounds.width;
            float normalized_y = (pos.y - bounds.y) / bounds.height;
            
            position = normalized_x * dx + normalized_y * dy;
            position = (position + 1) / 2;  /* Normalize to 0-1 */
            break;
        }
        
        case RX_GRADIENT_RADIAL: {
            float dx = pos.x - (bounds.x + grad->center.x * bounds.width);
            float dy = pos.y - (bounds.y + grad->center.y * bounds.height);
            float distance = sqrtf(dx * dx + dy * dy);
            position = distance / (grad->radius * bounds.width);
            break;
        }
        
        case RX_GRADIENT_ANGULAR:
        case RX_GRADIENT_CONIC: {
            float dx = pos.x - (bounds.x + grad->center.x * bounds.width);
            float dy = pos.y - (bounds.y + grad->center.y * bounds.height);
            position = (atan2f(dy, dx) + M_PI) / (2 * M_PI);
            break;
        }
        
        default:
            position = 0;
            break;
    }
    
    return rx_gradient_color_at(grad, position);
}

void rx_gradient_destroy(rx_gradient* grad) {
    if (grad) {
        free(grad->stops);
        free(grad);
    }
}

/* ============================================================================
 * Preset Gradients
 * ============================================================================ */

rx_gradient* rx_gradient_sunset(void) {
    rx_gradient_stop stops[] = {
        {{255, 94, 77, 255}, 0.0f},
        {{255, 154, 0, 255}, 0.5f},
        {{128, 0, 128, 255}, 1.0f}
    };
    return rx_gradient_linear(135, stops, 3);
}

rx_gradient* rx_gradient_ocean(void) {
    rx_gradient_stop stops[] = {
        {{0, 119, 182, 255}, 0.0f},
        {{0, 180, 216, 255}, 0.5f},
        {{144, 224, 239, 255}, 1.0f}
    };
    return rx_gradient_linear(180, stops, 3);
}

rx_gradient* rx_gradient_neon(void) {
    rx_gradient_stop stops[] = {
        {{255, 0, 128, 255}, 0.0f},
        {{128, 0, 255, 255}, 0.5f},
        {{0, 255, 255, 255}, 1.0f}
    };
    return rx_gradient_linear(90, stops, 3);
}

/* ============================================================================
 * Color Palette Generation
 * ============================================================================ */

rx_palette* rx_palette_generate(rx_color base, rx_palette_type type, size_t count) {
    rx_palette* pal = (rx_palette*)calloc(1, sizeof(rx_palette));
    if (!pal) return NULL;
    
    pal->base = base;
    rx_hsl hsl = rx_rgb_to_hsl(base);
    
    switch (type) {
        case RX_PALETTE_COMPLEMENTARY:
            pal->count = 2;
            pal->colors = (rx_color*)malloc(sizeof(rx_color) * 2);
            pal->colors[0] = base;
            pal->colors[1] = rx_color_adjust_hue(base, 180);
            break;
            
        case RX_PALETTE_ANALOGOUS:
            pal->count = count > 0 ? count : 3;
            pal->colors = (rx_color*)malloc(sizeof(rx_color) * pal->count);
            for (size_t i = 0; i < pal->count; i++) {
                float offset = (float)(i - pal->count/2) * 30;
                pal->colors[i] = rx_color_adjust_hue(base, offset);
            }
            break;
            
        case RX_PALETTE_TRIADIC:
            pal->count = 3;
            pal->colors = (rx_color*)malloc(sizeof(rx_color) * 3);
            pal->colors[0] = base;
            pal->colors[1] = rx_color_adjust_hue(base, 120);
            pal->colors[2] = rx_color_adjust_hue(base, 240);
            break;
            
        case RX_PALETTE_TETRADIC:
            pal->count = 4;
            pal->colors = (rx_color*)malloc(sizeof(rx_color) * 4);
            pal->colors[0] = base;
            pal->colors[1] = rx_color_adjust_hue(base, 90);
            pal->colors[2] = rx_color_adjust_hue(base, 180);
            pal->colors[3] = rx_color_adjust_hue(base, 270);
            break;
            
        case RX_PALETTE_MONOCHROMATIC:
            pal->count = count > 0 ? count : 5;
            pal->colors = (rx_color*)malloc(sizeof(rx_color) * pal->count);
            for (size_t i = 0; i < pal->count; i++) {
                float l_offset = (float)i / (pal->count - 1) - 0.5f;
                hsl.l = CLAMP(hsl.l + l_offset * 0.4f, 0.1f, 0.9f);
                pal->colors[i] = rx_hsl_to_rgb(hsl);
            }
            break;
            
        default:
            pal->count = 1;
            pal->colors = (rx_color*)malloc(sizeof(rx_color));
            pal->colors[0] = base;
            break;
    }
    
    return pal;
}

void rx_palette_destroy(rx_palette* pal) {
    if (pal) {
        free(pal->colors);
        free(pal);
    }
}

/* ============================================================================
 * Color Accessibility
 * ============================================================================ */

float rx_color_luminance(rx_color c) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;
    
    r = r <= 0.03928f ? r / 12.92f : powf((r + 0.055f) / 1.055f, 2.4f);
    g = g <= 0.03928f ? g / 12.92f : powf((g + 0.055f) / 1.055f, 2.4f);
    b = b <= 0.03928f ? b / 12.92f : powf((b + 0.055f) / 1.055f, 2.4f);
    
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float rx_color_contrast_ratio(rx_color c1, rx_color c2) {
    float l1 = rx_color_luminance(c1);
    float l2 = rx_color_luminance(c2);
    
    float lighter = l1 > l2 ? l1 : l2;
    float darker = l1 > l2 ? l2 : l1;
    
    return (lighter + 0.05f) / (darker + 0.05f);
}

bool rx_color_is_accessible(rx_color foreground, rx_color background, rx_wcag_level level) {
    float ratio = rx_color_contrast_ratio(foreground, background);
    
    switch (level) {
        case RX_WCAG_AA:       return ratio >= 4.5f;
        case RX_WCAG_AA_LARGE: return ratio >= 3.0f;
        case RX_WCAG_AAA:      return ratio >= 7.0f;
        case RX_WCAG_AAA_LARGE: return ratio >= 4.5f;
        default:               return ratio >= 4.5f;
    }
}

rx_color rx_color_make_accessible(rx_color color, rx_color background, rx_wcag_level level) {
    if (rx_color_is_accessible(color, background, level)) {
        return color;
    }
    
    float bg_lum = rx_color_luminance(background);
    rx_hsl hsl = rx_rgb_to_hsl(color);
    
    /* Determine direction to adjust lightness */
    float target_ratio = level == RX_WCAG_AAA ? 7.0f : 4.5f;
    bool lighten = bg_lum < 0.5f;
    
    /* Binary search for accessible lightness */
    float lo = lighten ? hsl.l : 0;
    float hi = lighten ? 1 : hsl.l;
    
    for (int i = 0; i < 20; i++) {
        float mid = (lo + hi) / 2;
        hsl.l = mid;
        rx_color test = rx_hsl_to_rgb(hsl);
        float ratio = rx_color_contrast_ratio(test, background);
        
        if (ratio >= target_ratio) {
            if (lighten) {
                hi = mid;
            } else {
                lo = mid;
            }
        } else {
            if (lighten) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
    }
    
    return rx_hsl_to_rgb(hsl);
}

/* ============================================================================
 * Color Blindness Simulation
 * ============================================================================ */

rx_color rx_color_simulate_blindness(rx_color c, rx_color_blindness type) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;
    
    float rr, gg, bb;
    
    switch (type) {
        case RX_VISION_PROTANOPIA:
            rr = 0.567f * r + 0.433f * g;
            gg = 0.558f * r + 0.442f * g;
            bb = 0.242f * g + 0.758f * b;
            break;
            
        case RX_VISION_DEUTERANOPIA:
            rr = 0.625f * r + 0.375f * g;
            gg = 0.700f * r + 0.300f * g;
            bb = 0.300f * g + 0.700f * b;
            break;
            
        case RX_VISION_TRITANOPIA:
            rr = 0.950f * r + 0.050f * g;
            gg = 0.433f * g + 0.567f * b;
            bb = 0.475f * g + 0.525f * b;
            break;
            
        case RX_VISION_ACHROMATOPSIA:
            rr = gg = bb = 0.299f * r + 0.587f * g + 0.114f * b;
            break;
            
        default:
            return c;
    }
    
    return (rx_color){
        (uint8_t)CLAMP(rr * 255, 0, 255),
        (uint8_t)CLAMP(gg * 255, 0, 255),
        (uint8_t)CLAMP(bb * 255, 0, 255),
        c.a
    };
}
