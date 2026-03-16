/*
 * IconLay Reox Widgets Implementation
 * Custom widgets for IconLay using Reox framework
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay_widgets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Color Utilities
 * ============================================================================ */

static inline nx_color_t to_nx_color(iconlay_color_t c) {
    return (nx_color_t){c.r, c.g, c.b, c.a};
}

static inline float clampf(float v, float min, float max) {
    return v < min ? min : (v > max ? max : v);
}

/* HSV to RGB conversion */
iconlay_color_t iconlay_hsv_to_rgb(float h, float s, float v, uint8_t a) {
    float r, g, b;
    
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    s = clampf(s, 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);
    
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    
    return (iconlay_color_t){
        (uint8_t)((r + m) * 255.0f),
        (uint8_t)((g + m) * 255.0f),
        (uint8_t)((b + m) * 255.0f),
        a
    };
}

/* RGB to HSV conversion */
void iconlay_rgb_to_hsv(iconlay_color_t c, float* h, float* s, float* v) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;
    
    float max = fmaxf(fmaxf(r, g), b);
    float min = fminf(fminf(r, g), b);
    float delta = max - min;
    
    *v = max;
    *s = (max == 0) ? 0 : delta / max;
    
    if (delta == 0) {
        *h = 0;
    } else if (max == r) {
        *h = 60.0f * fmodf((g - b) / delta, 6.0f);
    } else if (max == g) {
        *h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        *h = 60.0f * ((r - g) / delta + 4.0f);
    }
    
    if (*h < 0) *h += 360.0f;
}

void iconlay_color_to_hex(iconlay_color_t c, char* buf, size_t size) {
    if (size >= 8) {
        snprintf(buf, size, "#%02X%02X%02X", c.r, c.g, c.b);
    }
}

iconlay_color_t iconlay_hex_to_color(const char* hex) {
    if (!hex) return ICONLAY_COLOR_BLACK;
    if (hex[0] == '#') hex++;
    
    unsigned int r, g, b;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        return (iconlay_color_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, 255};
    }
    return ICONLAY_COLOR_BLACK;
}

/* ============================================================================
 * Slider Implementation
 * ============================================================================ */

iconlay_slider_t* iconlay_slider_create(const char* label, float min, float max, float initial) {
    iconlay_slider_t* s = calloc(1, sizeof(iconlay_slider_t));
    if (!s) return NULL;
    
    strncpy(s->label, label ? label : "", 31);
    s->min_value = min;
    s->max_value = max;
    s->value = clampf(initial, min, max);
    s->step = 0;
    
    s->width = 200;
    s->height = 32;
    s->corner_radius = 4;
    s->thumb_size = 16;
    
    s->track_color = (iconlay_color_t){50, 50, 56, 255};
    s->fill_color = ICONLAY_COLOR_ACCENT;
    s->thumb_color = ICONLAY_COLOR_WHITE;
    
    s->show_value = true;
    strcpy(s->value_format, "%.0f");
    s->enabled = true;
    
    return s;
}

void iconlay_slider_destroy(iconlay_slider_t* s) {
    if (s) free(s);
}

void iconlay_slider_set_bounds(iconlay_slider_t* s, float x, float y, float w, float h) {
    if (!s) return;
    s->x = x; s->y = y; s->width = w; s->height = h;
}

void iconlay_slider_set_step(iconlay_slider_t* s, float step) {
    if (s) s->step = step;
}

void iconlay_slider_set_format(iconlay_slider_t* s, const char* fmt) {
    if (s && fmt) strncpy(s->value_format, fmt, 15);
}

void iconlay_slider_set_colors(iconlay_slider_t* s, iconlay_color_t track, iconlay_color_t fill) {
    if (!s) return;
    s->track_color = track;
    s->fill_color = fill;
}

void iconlay_slider_set_callback(iconlay_slider_t* s, iconlay_slider_callback_t cb, void* data) {
    if (!s) return;
    s->on_change = cb;
    s->callback_data = data;
}

float iconlay_slider_get_value(iconlay_slider_t* s) {
    return s ? s->value : 0;
}

void iconlay_slider_set_value(iconlay_slider_t* s, float value) {
    if (!s) return;
    s->value = clampf(value, s->min_value, s->max_value);
}

void iconlay_slider_render(iconlay_slider_t* s, nx_context_t* ctx) {
    if (!s || !ctx) return;
    
    float label_width = 80;
    float value_width = 40;
    float track_x = s->x + label_width;
    float track_w = s->width - label_width - value_width - 8;
    float track_h = 6;
    float track_y = s->y + (s->height - track_h) / 2;
    
    /* Draw label */
    nx_font_t* font = nxgfx_font_default(ctx, 12);
    if (font && s->label[0]) {
        nx_color_t text_col = {200, 200, 210, 255};
        nxgfx_draw_text(ctx, s->label, (nx_point_t){(int32_t)s->x, (int32_t)(s->y + 8)}, font, text_col);
    }
    
    /* Draw track background */
    nx_rect_t track = {(int32_t)track_x, (int32_t)track_y, (uint32_t)track_w, (uint32_t)track_h};
    nxgfx_fill_rounded_rect(ctx, track, to_nx_color(s->track_color), 3);
    
    /* Draw fill */
    float t = (s->value - s->min_value) / (s->max_value - s->min_value);
    float fill_w = track_w * t;
    if (fill_w > 0) {
        nx_rect_t fill = {(int32_t)track_x, (int32_t)track_y, (uint32_t)fill_w, (uint32_t)track_h};
        nxgfx_fill_rounded_rect(ctx, fill, to_nx_color(s->fill_color), 3);
    }
    
    /* Draw thumb */
    float thumb_x = track_x + fill_w;
    float thumb_y = s->y + s->height / 2;
    nxgfx_fill_circle(ctx, (nx_point_t){(int32_t)thumb_x, (int32_t)thumb_y}, 
                      (uint32_t)(s->thumb_size / 2), to_nx_color(s->thumb_color));
    
    /* Draw value */
    if (s->show_value && font) {
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), s->value_format, s->value);
        nx_color_t val_col = {160, 160, 170, 255};
        nxgfx_draw_text(ctx, val_buf, 
                        (nx_point_t){(int32_t)(s->x + s->width - value_width), (int32_t)(s->y + 8)}, 
                        font, val_col);
    }
    
    if (font) nxgfx_font_destroy(font);
}

bool iconlay_slider_handle_event(iconlay_slider_t* s, int32_t x, int32_t y, bool pressed, bool released) {
    if (!s || !s->enabled) return false;
    
    float label_width = 80;
    float value_width = 40;
    float track_x = s->x + label_width;
    float track_w = s->width - label_width - value_width - 8;
    
    /* Check if within bounds */
    bool in_bounds = x >= s->x && x < s->x + s->width &&
                     y >= s->y && y < s->y + s->height;
    
    s->hovered = in_bounds;
    
    if (pressed && in_bounds) {
        s->dragging = true;
    }
    
    if (released) {
        s->dragging = false;
    }
    
    if (s->dragging) {
        float t = clampf((x - track_x) / track_w, 0.0f, 1.0f);
        float new_val = s->min_value + t * (s->max_value - s->min_value);
        
        /* Apply step */
        if (s->step > 0) {
            new_val = roundf(new_val / s->step) * s->step;
        }
        
        new_val = clampf(new_val, s->min_value, s->max_value);
        
        if (new_val != s->value) {
            s->value = new_val;
            if (s->on_change) {
                s->on_change(s->value, s->callback_data);
            }
        }
        return true;
    }
    
    return in_bounds;
}

/* ============================================================================
 * Toggle Implementation
 * ============================================================================ */

iconlay_toggle_t* iconlay_toggle_create(const char* label, bool initial) {
    iconlay_toggle_t* t = calloc(1, sizeof(iconlay_toggle_t));
    if (!t) return NULL;
    
    strncpy(t->label, label ? label : "", 31);
    t->value = initial;
    t->anim_progress = initial ? 1.0f : 0.0f;
    
    t->width = 200;
    t->height = 32;
    
    t->on_color = ICONLAY_COLOR_ACCENT;
    t->off_color = (iconlay_color_t){70, 70, 78, 255};
    t->thumb_color = ICONLAY_COLOR_WHITE;
    t->label_color = (iconlay_color_t){200, 200, 210, 255};
    
    t->anim_duration_ms = 150.0f;
    
    return t;
}

void iconlay_toggle_destroy(iconlay_toggle_t* t) {
    if (t) free(t);
}

void iconlay_toggle_set_bounds(iconlay_toggle_t* t, float x, float y, float w, float h) {
    if (!t) return;
    t->x = x; t->y = y; t->width = w; t->height = h;
}

void iconlay_toggle_set_colors(iconlay_toggle_t* t, iconlay_color_t on, iconlay_color_t off) {
    if (!t) return;
    t->on_color = on;
    t->off_color = off;
}

void iconlay_toggle_set_callback(iconlay_toggle_t* t, iconlay_toggle_callback_t cb, void* data) {
    if (!t) return;
    t->on_change = cb;
    t->callback_data = data;
}

bool iconlay_toggle_get_value(iconlay_toggle_t* t) {
    return t ? t->value : false;
}

void iconlay_toggle_set_value(iconlay_toggle_t* t, bool value) {
    if (!t) return;
    t->value = value;
    t->anim_progress = value ? 1.0f : 0.0f;
}

void iconlay_toggle_render(iconlay_toggle_t* t, nx_context_t* ctx) {
    if (!t || !ctx) return;
    
    float switch_w = 44;
    float switch_h = 24;
    float switch_x = t->x + t->width - switch_w;
    float switch_y = t->y + (t->height - switch_h) / 2;
    
    /* Draw label */
    nx_font_t* font = nxgfx_font_default(ctx, 12);
    if (font && t->label[0]) {
        nxgfx_draw_text(ctx, t->label, 
                        (nx_point_t){(int32_t)t->x, (int32_t)(t->y + 8)}, 
                        font, to_nx_color(t->label_color));
        nxgfx_font_destroy(font);
    }
    
    /* Interpolate colors */
    float p = t->anim_progress;
    iconlay_color_t bg = {
        (uint8_t)(t->off_color.r + p * (t->on_color.r - t->off_color.r)),
        (uint8_t)(t->off_color.g + p * (t->on_color.g - t->off_color.g)),
        (uint8_t)(t->off_color.b + p * (t->on_color.b - t->off_color.b)),
        255
    };
    
    /* Draw switch track */
    nx_rect_t track = {(int32_t)switch_x, (int32_t)switch_y, (uint32_t)switch_w, (uint32_t)switch_h};
    nxgfx_fill_rounded_rect(ctx, track, to_nx_color(bg), 12);
    
    /* Draw thumb */
    float thumb_r = (switch_h - 6) / 2;
    float thumb_x = switch_x + 3 + thumb_r + p * (switch_w - 6 - thumb_r * 2);
    float thumb_y = switch_y + switch_h / 2;
    nxgfx_fill_circle(ctx, (nx_point_t){(int32_t)thumb_x, (int32_t)thumb_y}, 
                      (uint32_t)thumb_r, to_nx_color(t->thumb_color));
}

bool iconlay_toggle_handle_click(iconlay_toggle_t* t, int32_t x, int32_t y) {
    if (!t) return false;
    
    bool in_bounds = x >= t->x && x < t->x + t->width &&
                     y >= t->y && y < t->y + t->height;
    
    if (in_bounds) {
        t->value = !t->value;
        t->animating = true;
        
        if (t->on_change) {
            t->on_change(t->value, t->callback_data);
        }
        return true;
    }
    return false;
}

void iconlay_toggle_update(iconlay_toggle_t* t, float dt_ms) {
    if (!t || !t->animating) return;
    
    float target = t->value ? 1.0f : 0.0f;
    float step = dt_ms / t->anim_duration_ms;
    
    if (t->anim_progress < target) {
        t->anim_progress += step;
        if (t->anim_progress >= target) {
            t->anim_progress = target;
            t->animating = false;
        }
    } else if (t->anim_progress > target) {
        t->anim_progress -= step;
        if (t->anim_progress <= target) {
            t->anim_progress = target;
            t->animating = false;
        }
    }
}

/* ============================================================================
 * Layer Card Implementation
 * ============================================================================ */

iconlay_layer_card_t* iconlay_layer_card_create(const char* name, int index) {
    iconlay_layer_card_t* c = calloc(1, sizeof(iconlay_layer_card_t));
    if (!c) return NULL;
    
    strncpy(c->name, name ? name : "Layer", 31);
    strcpy(c->type_label, "SVG");
    c->opacity = 255;
    c->visible = true;
    c->locked = false;
    c->layer_index = index;
    
    c->width = 280;
    c->height = 72;
    c->corner_radius = 8;
    
    c->bg_color = (iconlay_color_t){39, 39, 42, 255};
    c->selected_color = (iconlay_color_t){59, 130, 246, 50};
    c->text_color = (iconlay_color_t){250, 250, 250, 255};
    c->hovered_button = -1;
    
    return c;
}

void iconlay_layer_card_destroy(iconlay_layer_card_t* c) {
    if (c) free(c);
}

void iconlay_layer_card_set_bounds(iconlay_layer_card_t* c, float x, float y, float w, float h) {
    if (!c) return;
    c->x = x; c->y = y; c->width = w; c->height = h;
}

void iconlay_layer_card_set_info(iconlay_layer_card_t* c, const char* name, const char* type,
                                  uint8_t opacity, bool visible, bool locked) {
    if (!c) return;
    if (name) strncpy(c->name, name, 31);
    if (type) strncpy(c->type_label, type, 7);
    c->opacity = opacity;
    c->visible = visible;
    c->locked = locked;
}

void iconlay_layer_card_set_thumbnail(iconlay_layer_card_t* c, nx_effect_buffer_t* thumb) {
    if (c) c->thumbnail = thumb;
}

void iconlay_layer_card_set_selected(iconlay_layer_card_t* c, bool selected) {
    if (c) c->selected = selected;
}

void iconlay_layer_card_set_callback(iconlay_layer_card_t* c, iconlay_layer_card_callback_t cb, void* data) {
    if (!c) return;
    c->on_action = cb;
    c->callback_data = data;
}

void iconlay_layer_card_render(iconlay_layer_card_t* c, nx_context_t* ctx) {
    if (!c || !ctx) return;
    
    nx_rect_t bg = {(int32_t)c->x, (int32_t)c->y, (uint32_t)c->width, (uint32_t)c->height};
    
    /* Draw background */
    if (c->selected) {
        nxgfx_fill_rounded_rect(ctx, bg, to_nx_color(c->selected_color), (uint32_t)c->corner_radius);
        /* Selection border */
        nx_color_t border = {59, 130, 246, 128};
        nxgfx_draw_rect(ctx, bg, border, 1);
    } else if (c->hovered) {
        iconlay_color_t hover = {50, 50, 56, 255};
        nxgfx_fill_rounded_rect(ctx, bg, to_nx_color(hover), (uint32_t)c->corner_radius);
    }
    
    /* Type badge */
    nx_rect_t badge = {(int32_t)(c->x + 12), (int32_t)(c->y + 16), 40, 40};
    nx_color_t badge_bg = {9, 9, 11, 255};
    nxgfx_fill_rounded_rect(ctx, badge, badge_bg, 4);
    nxgfx_draw_rect(ctx, badge, (nx_color_t){63, 63, 70, 255}, 1);
    
    nx_font_t* font_small = nxgfx_font_default(ctx, 10);
    if (font_small) {
        nx_color_t dim = {161, 161, 170, 255};
        nxgfx_draw_text(ctx, c->type_label, 
                        (nx_point_t){badge.x + 8, badge.y + 14}, font_small, dim);
        nxgfx_font_destroy(font_small);
    }
    
    /* Layer name */
    nx_font_t* font = nxgfx_font_default(ctx, 12);
    if (font) {
        nxgfx_draw_text(ctx, c->name, 
                        (nx_point_t){(int32_t)(c->x + 64), (int32_t)(c->y + 18)}, 
                        font, to_nx_color(c->text_color));
        
        /* Blend mode and opacity info */
        char info[32];
        snprintf(info, sizeof(info), "Normal • %d%%", (c->opacity * 100) / 255);
        nx_color_t dim = {161, 161, 170, 255};
        nxgfx_draw_text(ctx, info, 
                        (nx_point_t){(int32_t)(c->x + 64), (int32_t)(c->y + 38)}, 
                        font, dim);
        nxgfx_font_destroy(font);
    }
    
    /* Visibility toggle */
    float btn_x = c->x + c->width - 40;
    float btn_y = c->y + 24;
    nx_rect_t vis_btn = {(int32_t)btn_x, (int32_t)btn_y, 24, 24};
    iconlay_color_t vis_color = c->visible ? ICONLAY_COLOR_ACCENT : c->bg_color;
    nxgfx_fill_rounded_rect(ctx, vis_btn, to_nx_color(vis_color), 4);
}

bool iconlay_layer_card_handle_click(iconlay_layer_card_t* c, int32_t x, int32_t y) {
    if (!c) return false;
    
    if (!iconlay_layer_card_hit_test(c, x, y)) return false;
    
    /* Check visibility button */
    float btn_x = c->x + c->width - 40;
    float btn_y = c->y + 24;
    if (x >= btn_x && x < btn_x + 24 && y >= btn_y && y < btn_y + 24) {
        c->visible = !c->visible;
        if (c->on_action) {
            c->on_action(ICONLAY_LAYER_ACTION_TOGGLE_VIS, c->callback_data);
        }
        return true;
    }
    
    /* Otherwise select layer */
    if (c->on_action) {
        c->on_action(ICONLAY_LAYER_ACTION_SELECT, c->callback_data);
    }
    return true;
}

bool iconlay_layer_card_hit_test(iconlay_layer_card_t* c, int32_t x, int32_t y) {
    if (!c) return false;
    return x >= c->x && x < c->x + c->width &&
           y >= c->y && y < c->y + c->height;
}

/* ============================================================================
 * Color Picker Implementation
 * ============================================================================ */

static void init_palettes(iconlay_color_picker_t* p) {
    float hues[] = {220, 270, 330, 0, 25, 50, 140, 180, 0};
    const char* names[] = {"Blues", "Purples", "Pinks", "Reds", "Oranges", "Yellows", "Greens", "Teals", "Grays"};
    (void)names;
    
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 10; j++) {
            float s = 0.95f - j * 0.05f;
            float v = 0.95f - j * 0.08f;
            
            if (i == 8) {
                /* Grays */
                float gray = 0.95f - j * 0.09f;
                p->palettes[i][j] = (iconlay_color_t){
                    (uint8_t)(gray * 255), (uint8_t)(gray * 255), (uint8_t)(gray * 255), 255
                };
            } else {
                p->palettes[i][j] = iconlay_hsv_to_rgb(hues[i], s, v, 255);
            }
        }
    }
    p->palette_count = 9;
}

iconlay_color_picker_t* iconlay_color_picker_create(iconlay_color_t initial) {
    iconlay_color_picker_t* p = calloc(1, sizeof(iconlay_color_picker_t));
    if (!p) return NULL;
    
    iconlay_rgb_to_hsv(initial, &p->hue, &p->saturation, &p->brightness);
    p->alpha = initial.a;
    
    p->width = 600;
    p->height = 700;
    p->swatch_size = 48;
    p->swatch_gap = 8;
    p->margin = 16;
    
    p->bg_color = (iconlay_color_t){28, 28, 32, 240};
    p->selected_palette = -1;
    p->selected_swatch = -1;
    
    init_palettes(p);
    
    return p;
}

void iconlay_color_picker_destroy(iconlay_color_picker_t* p) {
    if (p) free(p);
}

void iconlay_color_picker_set_bounds(iconlay_color_picker_t* p, float x, float y, float w, float h) {
    if (!p) return;
    p->x = x; p->y = y; p->width = w; p->height = h;
}

void iconlay_color_picker_set_visible(iconlay_color_picker_t* p, bool visible) {
    if (p) p->visible = visible;
}

void iconlay_color_picker_set_callback(iconlay_color_picker_t* p, iconlay_color_callback_t cb, void* data) {
    if (!p) return;
    p->on_change = cb;
    p->callback_data = data;
}

iconlay_color_t iconlay_color_picker_get_color(iconlay_color_picker_t* p) {
    if (!p) return ICONLAY_COLOR_BLACK;
    return iconlay_hsv_to_rgb(p->hue, p->saturation, p->brightness, p->alpha);
}

void iconlay_color_picker_set_color(iconlay_color_picker_t* p, iconlay_color_t color) {
    if (!p) return;
    iconlay_rgb_to_hsv(color, &p->hue, &p->saturation, &p->brightness);
    p->alpha = color.a;
}

void iconlay_color_picker_set_hue(iconlay_color_picker_t* p, float hue) {
    if (p) p->hue = fmodf(hue, 360.0f);
}

void iconlay_color_picker_render(iconlay_color_picker_t* p, nx_context_t* ctx) {
    if (!p || !p->visible || !ctx) return;
    
    nx_rect_t bg = {(int32_t)p->x, (int32_t)p->y, (uint32_t)p->width, (uint32_t)p->height};
    nxgfx_fill_rounded_rect(ctx, bg, to_nx_color(p->bg_color), 12);
    nxgfx_draw_rect(ctx, bg, (nx_color_t){60, 60, 68, 255}, 1);
    
    float y = p->y + p->margin + 40;
    
    /* Hue slider */
    float slider_h = 24;
    for (int i = 0; i < (int)(p->width - p->margin * 2); i++) {
        float h = (i / (p->width - p->margin * 2)) * 360.0f;
        iconlay_color_t c = iconlay_hsv_to_rgb(h, 1.0f, 1.0f, 255);
        nx_rect_t seg = {(int32_t)(p->x + p->margin + i), (int32_t)y, 1, (uint32_t)slider_h};
        nxgfx_fill_rect(ctx, seg, to_nx_color(c));
    }
    
    /* Hue thumb */
    float thumb_x = p->x + p->margin + (p->hue / 360.0f) * (p->width - p->margin * 2);
    nx_rect_t thumb = {(int32_t)(thumb_x - 4), (int32_t)(y - 2), 8, (uint32_t)(slider_h + 4)};
    nxgfx_fill_rounded_rect(ctx, thumb, (nx_color_t){255, 255, 255, 255}, 4);
    
    y += slider_h + p->margin;
    
    /* Custom palette from current hue */
    for (int i = 0; i < 10; i++) {
        float s = 0.95f - i * 0.05f;
        float v = 0.95f - i * 0.08f;
        iconlay_color_t c = iconlay_hsv_to_rgb(p->hue, s, v, 255);
        
        float sx = p->x + p->margin + i * (p->swatch_size + p->swatch_gap);
        nx_rect_t swatch = {(int32_t)sx, (int32_t)y, (uint32_t)p->swatch_size, (uint32_t)p->swatch_size};
        nxgfx_fill_rounded_rect(ctx, swatch, to_nx_color(c), 6);
    }
    
    y += p->swatch_size + p->margin + 8;
    
    /* Predefined palettes */
    nx_font_t* font = nxgfx_font_default(ctx, 10);
    for (int pi = 0; pi < p->palette_count; pi++) {
        /* Palette label */
        if (font) {
            const char* names[] = {"Blues", "Purples", "Pinks", "Reds", "Oranges", "Yellows", "Greens", "Teals", "Grays"};
            nx_color_t dim = {120, 120, 130, 255};
            nxgfx_draw_text(ctx, names[pi], (nx_point_t){(int32_t)(p->x + p->margin), (int32_t)(y - 16)}, font, dim);
        }
        
        for (int i = 0; i < 10; i++) {
            float sx = p->x + p->margin + i * (p->swatch_size + p->swatch_gap);
            nx_rect_t swatch = {(int32_t)sx, (int32_t)y, (uint32_t)p->swatch_size, (uint32_t)p->swatch_size};
            nxgfx_fill_rounded_rect(ctx, swatch, to_nx_color(p->palettes[pi][i]), 6);
            
            /* Selection indicator */
            if (pi == p->selected_palette && i == p->selected_swatch) {
                nxgfx_draw_rect(ctx, swatch, (nx_color_t){59, 130, 246, 255}, 2);
            }
        }
        
        y += p->swatch_size + p->margin;
    }
    
    if (font) nxgfx_font_destroy(font);
    
    /* Current color preview */
    iconlay_color_t current = iconlay_color_picker_get_color(p);
    nx_rect_t preview = {(int32_t)(p->x + p->width - 120), (int32_t)(p->y + p->margin + 40), 100, 100};
    nxgfx_fill_rounded_rect(ctx, preview, to_nx_color(current), 8);
    
    /* Hex value */
    char hex[8];
    iconlay_color_to_hex(current, hex, sizeof(hex));
    nx_font_t* font2 = nxgfx_font_default(ctx, 12);
    if (font2) {
        nxgfx_draw_text(ctx, hex, (nx_point_t){preview.x, preview.y + 110}, font2, (nx_color_t){180, 180, 190, 255});
        nxgfx_font_destroy(font2);
    }
}

bool iconlay_color_picker_handle_event(iconlay_color_picker_t* p, int32_t x, int32_t y,
                                        bool pressed, bool released, bool dragging) {
    if (!p || !p->visible) return false;
    
    /* Check bounds */
    if (x < p->x || x >= p->x + p->width || y < p->y || y >= p->y + p->height) {
        return false;
    }
    
    float slider_y = p->y + p->margin + 40;
    float slider_h = 24;
    
    /* Hue slider interaction */
    if (y >= slider_y && y < slider_y + slider_h) {
        if (pressed || dragging) {
            p->dragging_hue = true;
            float t = clampf((x - p->x - p->margin) / (p->width - p->margin * 2), 0.0f, 1.0f);
            p->hue = t * 360.0f;
            
            if (p->on_change) {
                p->on_change(iconlay_color_picker_get_color(p), p->callback_data);
            }
            return true;
        }
    }
    
    if (released) {
        p->dragging_hue = false;
    }
    
    /* Palette swatch clicks */
    if (pressed) {
        float start_y = slider_y + slider_h + p->margin + p->swatch_size + p->margin + 8;
        
        for (int pi = 0; pi < p->palette_count; pi++) {
            float row_y = start_y + pi * (p->swatch_size + p->margin);
            
            if (y >= row_y && y < row_y + p->swatch_size) {
                int si = (int)((x - p->x - p->margin) / (p->swatch_size + p->swatch_gap));
                if (si >= 0 && si < 10) {
                    p->selected_palette = pi;
                    p->selected_swatch = si;
                    
                    iconlay_color_t c = p->palettes[pi][si];
                    iconlay_rgb_to_hsv(c, &p->hue, &p->saturation, &p->brightness);
                    p->alpha = c.a;
                    
                    if (p->on_change) {
                        p->on_change(c, p->callback_data);
                    }
                    return true;
                }
            }
        }
    }
    
    return true;
}
