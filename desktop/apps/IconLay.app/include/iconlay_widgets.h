/*
 * IconLay Reox UI Widgets
 * Custom widgets for IconLay using Reox framework
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ICONLAY_REOX_WIDGETS_H
#define ICONLAY_REOX_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>
#include "nxgfx/nxgfx.h"
#include "nxgfx/nxgfx_effects.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct iconlay_color_picker iconlay_color_picker_t;
typedef struct iconlay_slider iconlay_slider_t;
typedef struct iconlay_toggle iconlay_toggle_t;
typedef struct iconlay_layer_card iconlay_layer_card_t;

/* ============================================================================
 * Color Type (matches Reox rx_color)
 * ============================================================================ */

typedef struct {
    uint8_t r, g, b, a;
} iconlay_color_t;

#define ICONLAY_COLOR_WHITE  ((iconlay_color_t){255, 255, 255, 255})
#define ICONLAY_COLOR_BLACK  ((iconlay_color_t){0, 0, 0, 255})
#define ICONLAY_COLOR_ACCENT ((iconlay_color_t){59, 130, 246, 255})

/* ============================================================================
 * Slider Widget
 * ============================================================================ */

typedef void (*iconlay_slider_callback_t)(float value, void* user_data);

typedef struct iconlay_slider {
    /* Style */
    float x, y, width, height;
    float corner_radius;
    iconlay_color_t track_color;
    iconlay_color_t fill_color;
    iconlay_color_t thumb_color;
    float thumb_size;
    
    /* Value */
    float min_value;
    float max_value;
    float value;
    float step;            /* 0 = continuous */
    
    /* Label */
    char label[32];
    bool show_value;
    char value_format[16]; /* e.g., "%d%%" or "%.1f" */
    
    /* State */
    bool dragging;
    bool hovered;
    bool enabled;
    
    /* Callback */
    iconlay_slider_callback_t on_change;
    void* callback_data;
} iconlay_slider_t;

/* Create/destroy */
iconlay_slider_t* iconlay_slider_create(const char* label, float min, float max, float initial);
void iconlay_slider_destroy(iconlay_slider_t* slider);

/* Configuration */
void iconlay_slider_set_bounds(iconlay_slider_t* s, float x, float y, float w, float h);
void iconlay_slider_set_step(iconlay_slider_t* s, float step);
void iconlay_slider_set_format(iconlay_slider_t* s, const char* fmt);
void iconlay_slider_set_colors(iconlay_slider_t* s, iconlay_color_t track, iconlay_color_t fill);
void iconlay_slider_set_callback(iconlay_slider_t* s, iconlay_slider_callback_t cb, void* data);

/* Value access */
float iconlay_slider_get_value(iconlay_slider_t* s);
void iconlay_slider_set_value(iconlay_slider_t* s, float value);

/* Rendering and input */
void iconlay_slider_render(iconlay_slider_t* s, nx_context_t* ctx);
bool iconlay_slider_handle_event(iconlay_slider_t* s, int32_t x, int32_t y, bool pressed, bool released);

/* ============================================================================
 * Toggle Switch Widget
 * ============================================================================ */

typedef void (*iconlay_toggle_callback_t)(bool value, void* user_data);

typedef struct iconlay_toggle {
    /* Style */
    float x, y, width, height;
    iconlay_color_t on_color;
    iconlay_color_t off_color;
    iconlay_color_t thumb_color;
    
    /* Label */
    char label[32];
    iconlay_color_t label_color;
    
    /* State */
    bool value;
    bool hovered;
    bool animating;
    float anim_progress; /* 0.0 to 1.0 */
    
    /* Animation */
    float anim_duration_ms;
    
    /* Callback */
    iconlay_toggle_callback_t on_change;
    void* callback_data;
} iconlay_toggle_t;

/* Create/destroy */
iconlay_toggle_t* iconlay_toggle_create(const char* label, bool initial);
void iconlay_toggle_destroy(iconlay_toggle_t* toggle);

/* Configuration */
void iconlay_toggle_set_bounds(iconlay_toggle_t* t, float x, float y, float w, float h);
void iconlay_toggle_set_colors(iconlay_toggle_t* t, iconlay_color_t on, iconlay_color_t off);
void iconlay_toggle_set_callback(iconlay_toggle_t* t, iconlay_toggle_callback_t cb, void* data);

/* Value access */
bool iconlay_toggle_get_value(iconlay_toggle_t* t);
void iconlay_toggle_set_value(iconlay_toggle_t* t, bool value);

/* Rendering and input */
void iconlay_toggle_render(iconlay_toggle_t* t, nx_context_t* ctx);
bool iconlay_toggle_handle_click(iconlay_toggle_t* t, int32_t x, int32_t y);
void iconlay_toggle_update(iconlay_toggle_t* t, float dt_ms);

/* ============================================================================
 * Layer Card Widget
 * ============================================================================ */

typedef void (*iconlay_layer_card_callback_t)(int action, void* user_data);

/* Layer card actions */
#define ICONLAY_LAYER_ACTION_SELECT     0
#define ICONLAY_LAYER_ACTION_TOGGLE_VIS 1
#define ICONLAY_LAYER_ACTION_TOGGLE_LOCK 2
#define ICONLAY_LAYER_ACTION_DELETE     3
#define ICONLAY_LAYER_ACTION_DUPLICATE  4
#define ICONLAY_LAYER_ACTION_MOVE_UP    5
#define ICONLAY_LAYER_ACTION_MOVE_DOWN  6

typedef struct iconlay_layer_card {
    /* Position */
    float x, y, width, height;
    
    /* Layer info */
    char name[32];
    char type_label[8];   /* "SVG" or "PNG" */
    uint8_t opacity;
    bool visible;
    bool locked;
    
    /* State */
    bool selected;
    bool hovered;
    int hovered_button;   /* -1 = none, 0 = vis, 1 = lock */
    
    /* Preview thumbnail (if any) */
    nx_effect_buffer_t* thumbnail;
    
    /* Style */
    iconlay_color_t bg_color;
    iconlay_color_t selected_color;
    iconlay_color_t text_color;
    float corner_radius;
    
    /* Callback */
    iconlay_layer_card_callback_t on_action;
    void* callback_data;
    int layer_index;
} iconlay_layer_card_t;

/* Create/destroy */
iconlay_layer_card_t* iconlay_layer_card_create(const char* name, int index);
void iconlay_layer_card_destroy(iconlay_layer_card_t* card);

/* Configuration */
void iconlay_layer_card_set_bounds(iconlay_layer_card_t* c, float x, float y, float w, float h);
void iconlay_layer_card_set_info(iconlay_layer_card_t* c, const char* name, const char* type, 
                                  uint8_t opacity, bool visible, bool locked);
void iconlay_layer_card_set_thumbnail(iconlay_layer_card_t* c, nx_effect_buffer_t* thumb);
void iconlay_layer_card_set_selected(iconlay_layer_card_t* c, bool selected);
void iconlay_layer_card_set_callback(iconlay_layer_card_t* c, iconlay_layer_card_callback_t cb, void* data);

/* Rendering and input */
void iconlay_layer_card_render(iconlay_layer_card_t* c, nx_context_t* ctx);
bool iconlay_layer_card_handle_click(iconlay_layer_card_t* c, int32_t x, int32_t y);
bool iconlay_layer_card_hit_test(iconlay_layer_card_t* c, int32_t x, int32_t y);

/* ============================================================================
 * Color Picker Widget
 * ============================================================================ */

typedef void (*iconlay_color_callback_t)(iconlay_color_t color, void* user_data);

typedef struct iconlay_color_picker {
    /* Position */
    float x, y, width, height;
    
    /* Current color (HSV) */
    float hue;        /* 0-360 */
    float saturation; /* 0-1 */
    float brightness; /* 0-1 */
    uint8_t alpha;    /* 0-255 */
    
    /* Predefined palettes */
    iconlay_color_t palettes[9][10];
    int palette_count;
    
    /* UI State */
    bool visible;
    bool dragging_hue;
    bool dragging_sv;
    int selected_palette;
    int selected_swatch;
    
    /* Style */
    float swatch_size;
    float swatch_gap;
    float margin;
    iconlay_color_t bg_color;
    
    /* Callback */
    iconlay_color_callback_t on_change;
    void* callback_data;
} iconlay_color_picker_t;

/* Create/destroy */
iconlay_color_picker_t* iconlay_color_picker_create(iconlay_color_t initial);
void iconlay_color_picker_destroy(iconlay_color_picker_t* picker);

/* Configuration */
void iconlay_color_picker_set_bounds(iconlay_color_picker_t* p, float x, float y, float w, float h);
void iconlay_color_picker_set_visible(iconlay_color_picker_t* p, bool visible);
void iconlay_color_picker_set_callback(iconlay_color_picker_t* p, iconlay_color_callback_t cb, void* data);

/* Color access */
iconlay_color_t iconlay_color_picker_get_color(iconlay_color_picker_t* p);
void iconlay_color_picker_set_color(iconlay_color_picker_t* p, iconlay_color_t color);
void iconlay_color_picker_set_hue(iconlay_color_picker_t* p, float hue);

/* Rendering and input */
void iconlay_color_picker_render(iconlay_color_picker_t* p, nx_context_t* ctx);
bool iconlay_color_picker_handle_event(iconlay_color_picker_t* p, int32_t x, int32_t y, 
                                        bool pressed, bool released, bool dragging);

/* Utilities */
iconlay_color_t iconlay_hsv_to_rgb(float h, float s, float v, uint8_t a);
void iconlay_rgb_to_hsv(iconlay_color_t c, float* h, float* s, float* v);
void iconlay_color_to_hex(iconlay_color_t c, char* buf, size_t size);
iconlay_color_t iconlay_hex_to_color(const char* hex);

#ifdef __cplusplus
}
#endif

#endif /* ICONLAY_REOX_WIDGETS_H */
