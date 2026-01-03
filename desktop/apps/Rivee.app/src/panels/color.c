/*
 * Rivee - Color Picker Panel
 * Advanced color selection with gamut checking
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "../include/color.h"
#include "../include/rivee.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============ Color Picker ============ */

void rivee_color_picker_init(rivee_color_picker_t *picker) {
    if (!picker) return;
    
    memset(picker, 0, sizeof(rivee_color_picker_t));
    picker->mode = PICKER_MODE_RGB;
    picker->r = 255;
    picker->g = 255;
    picker->b = 255;
    picker->a = 255;
    picker->brightness = 100;
    picker->target_gamut = COLORSPACE_SRGB;
    strcpy(picker->hex, "#FFFFFF");
}

static void sync_from_rgb(rivee_color_picker_t *picker) {
    float r = picker->r / 255.0f;
    float g = picker->g / 255.0f;
    float b = picker->b / 255.0f;
    
    /* RGB to HSB */
    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;
    
    picker->brightness = max * 100;
    picker->saturation = (max == 0) ? 0 : (delta / max) * 100;
    
    if (delta == 0) {
        picker->hue = 0;
    } else if (max == r) {
        picker->hue = 60 * fmodf((g - b) / delta, 6);
    } else if (max == g) {
        picker->hue = 60 * ((b - r) / delta + 2);
    } else {
        picker->hue = 60 * ((r - g) / delta + 4);
    }
    if (picker->hue < 0) picker->hue += 360;
    
    /* RGB to CMYK */
    float k = 1 - max;
    if (k < 1) {
        picker->c = (1 - r - k) / (1 - k) * 100;
        picker->m = (1 - g - k) / (1 - k) * 100;
        picker->y = (1 - b - k) / (1 - k) * 100;
    } else {
        picker->c = picker->m = picker->y = 0;
    }
    picker->k = k * 100;
    
    /* RGB to Lab (via XYZ) */
    nxcolor_float_t rgb = {r, g, b, 1.0f};
    nxcolor_lab_t lab = nxcolor_rgb_to_lab(rgb);
    picker->lab_l = lab.l;
    picker->lab_a = lab.a;
    picker->lab_b = lab.b;
    
    /* Update hex */
    snprintf(picker->hex, sizeof(picker->hex), "#%02X%02X%02X", 
             picker->r, picker->g, picker->b);
    
    /* Update rivee color */
    picker->color = RIVEE_COLOR_RGBA(picker->r, picker->g, picker->b, picker->a);
}

void rivee_color_picker_set_color(rivee_color_picker_t *picker, rivee_color_t color) {
    if (!picker) return;
    
    picker->r = color.r;
    picker->g = color.g;
    picker->b = color.b;
    picker->a = color.a;
    
    sync_from_rgb(picker);
}

rivee_color_t rivee_color_picker_get_color(const rivee_color_picker_t *picker) {
    if (!picker) return RIVEE_COLOR_BLACK;
    return picker->color;
}

void rivee_color_picker_set_rgb(rivee_color_picker_t *picker, uint8_t r, uint8_t g, uint8_t b) {
    if (!picker) return;
    
    picker->r = r;
    picker->g = g;
    picker->b = b;
    
    sync_from_rgb(picker);
}

void rivee_color_picker_set_hsb(rivee_color_picker_t *picker, float h, float s, float b) {
    if (!picker) return;
    
    picker->hue = h;
    picker->saturation = s;
    picker->brightness = b;
    
    /* HSB to RGB */
    float hs = s / 100.0f;
    float hb = b / 100.0f;
    float c = hb * hs;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = hb - c;
    
    float r, g, bl;
    if (h < 60) { r = c; g = x; bl = 0; }
    else if (h < 120) { r = x; g = c; bl = 0; }
    else if (h < 180) { r = 0; g = c; bl = x; }
    else if (h < 240) { r = 0; g = x; bl = c; }
    else if (h < 300) { r = x; g = 0; bl = c; }
    else { r = c; g = 0; bl = x; }
    
    picker->r = (uint8_t)((r + m) * 255);
    picker->g = (uint8_t)((g + m) * 255);
    picker->b = (uint8_t)((bl + m) * 255);
    
    sync_from_rgb(picker);
}

void rivee_color_picker_set_cmyk(rivee_color_picker_t *picker, float c, float m, float y, float k) {
    if (!picker) return;
    
    picker->c = c;
    picker->m = m;
    picker->y = y;
    picker->k = k;
    
    /* CMYK to RGB */
    float kk = k / 100.0f;
    picker->r = (uint8_t)(255 * (1 - c/100) * (1 - kk));
    picker->g = (uint8_t)(255 * (1 - m/100) * (1 - kk));
    picker->b = (uint8_t)(255 * (1 - y/100) * (1 - kk));
    
    sync_from_rgb(picker);
}

void rivee_color_picker_set_hex(rivee_color_picker_t *picker, const char *hex) {
    if (!picker || !hex) return;
    
    strncpy(picker->hex, hex, sizeof(picker->hex) - 1);
    
    /* Parse hex */
    const char *p = hex;
    if (*p == '#') p++;
    
    unsigned int r, g, b;
    if (sscanf(p, "%02x%02x%02x", &r, &g, &b) == 3) {
        picker->r = (uint8_t)r;
        picker->g = (uint8_t)g;
        picker->b = (uint8_t)b;
        sync_from_rgb(picker);
    }
}

void rivee_color_picker_check_gamut(rivee_color_picker_t *picker, nxcolor_space_t space) {
    if (!picker) return;
    
    picker->target_gamut = space;
    
    nxcolor_float_t color = {
        picker->r / 255.0f,
        picker->g / 255.0f,
        picker->b / 255.0f,
        1.0f
    };
    
    picker->out_of_gamut = !nxcolor_in_gamut(color, space);
}

/* ============ Color Harmonies ============ */

int rivee_color_harmony_generate(rivee_color_t base, color_harmony_t type,
                                  rivee_color_t *out, int max) {
    if (!out || max < 1) return 0;
    
    /* Convert to HSB */
    rivee_color_picker_t picker;
    rivee_color_picker_init(&picker);
    rivee_color_picker_set_color(&picker, base);
    
    float h = picker.hue;
    float s = picker.saturation;
    float b = picker.brightness;
    
    int count = 0;
    out[count++] = base;
    
    switch (type) {
        case HARMONY_COMPLEMENTARY:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 180, 360), s, b);
                out[count++] = picker.color;
            }
            break;
            
        case HARMONY_ANALOGOUS:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 30, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h - 30 + 360, 360), s, b);
                out[count++] = picker.color;
            }
            break;
            
        case HARMONY_TRIADIC:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 120, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 240, 360), s, b);
                out[count++] = picker.color;
            }
            break;
            
        case HARMONY_SPLIT_COMP:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 150, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 210, 360), s, b);
                out[count++] = picker.color;
            }
            break;
            
        case HARMONY_TETRADIC:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 90, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 180, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 270, 360), s, b);
                out[count++] = picker.color;
            }
            break;
            
        case HARMONY_SQUARE:
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 90, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 180, 360), s, b);
                out[count++] = picker.color;
            }
            if (count < max) {
                rivee_color_picker_set_hsb(&picker, fmodf(h + 270, 360), s, b);
                out[count++] = picker.color;
            }
            break;
    }
    
    return count;
}

/* ============ Eyedropper ============ */

rivee_color_t rivee_eyedropper_sample(rivee_document_t *doc, float x, float y,
                                       const eyedropper_options_t *options) {
    if (!doc) return RIVEE_COLOR_BLACK;
    
    int sample_size = options ? options->sample_size : 1;
    if (sample_size < 1) sample_size = 1;
    
    /* Would sample from rendered canvas */
    /* For now return placeholder */
    (void)x;
    (void)y;
    (void)sample_size;
    
    return RIVEE_COLOR_WHITE;
}
