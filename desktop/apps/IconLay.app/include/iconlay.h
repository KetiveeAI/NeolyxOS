/*
 * IconLay - NeolyxOS Icon Compositor
 * Native icon editor using Reox UI and nxgfx effects
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ICONLAY_H
#define ICONLAY_H

#include "nxi_format.h"
#include "layer.h"
#include <nxgfx/nxgfx.h>
#include <nxgfx/nxgfx_effects.h>
#include <nxgfx/nxsvg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * IconLay Configuration
 * ============================================================================ */

#define ICONLAY_VERSION_MAJOR 2
#define ICONLAY_VERSION_MINOR 0
#define ICONLAY_VERSION_PATCH 0

#define ICONLAY_WINDOW_WIDTH  1440
#define ICONLAY_WINDOW_HEIGHT 900
#define ICONLAY_LEFT_PANEL_WIDTH 320
#define ICONLAY_RIGHT_PANEL_WIDTH 384
#define ICONLAY_TOOLBAR_HEIGHT 56
#define ICONLAY_CANVAS_SIZE 400

#define ICONLAY_MAX_LAYERS 32
#define ICONLAY_MAX_EFFECTS 16

/* ============================================================================
 * Effect Configuration
 * ============================================================================ */

typedef enum {
    ICONLAY_FX_NONE = 0,
    ICONLAY_FX_GLASS,
    ICONLAY_FX_FROST,
    ICONLAY_FX_TILTSHIFT,
    ICONLAY_FX_ORB,
    ICONLAY_FX_GLOW,
    ICONLAY_FX_SHADOW,
    ICONLAY_FX_BLUR,
    ICONLAY_FX_COLOR_ADJUST
} iconlay_effect_type_t;

typedef struct {
    iconlay_effect_type_t type;
    bool enabled;
    float intensity;          /* 0-100 */
    
    union {
        nx_glass_effect_t glass;
        nx_frost_effect_t frost;
        nx_tiltshift_effect_t tiltshift;
        nx_orb_effect_t orb;
        nx_glow_effect_t glow;
        nx_shadow_effect_t shadow;
        nx_blur_params_t blur;
        nx_color_adjust_t color;
    } params;
} iconlay_effect_t;

/* ============================================================================
 * Layer Configuration
 * ============================================================================ */

typedef struct {
    char name[32];
    nxsvg_image_t* svg;       /* SVG source */
    nx_effect_buffer_t* cache; /* Rendered cache */
    
    float pos_x, pos_y;       /* Position (0-1 normalized) */
    float scale;              /* Scale factor */
    float rotation;           /* Rotation degrees */
    uint8_t opacity;          /* 0-255 */
    
    bool visible;
    bool locked;
    
    nx_blend_mode_t blend_mode;
    
    iconlay_effect_t effects[ICONLAY_MAX_EFFECTS];
    uint32_t effect_count;
    
    bool dirty;               /* Needs re-render */
} iconlay_layer_t;

/* ============================================================================
 * Application State
 * ============================================================================ */

typedef struct {
    /* Graphics context */
    nx_context_t* gfx;
    uint32_t* framebuffer;
    uint32_t fb_width, fb_height;
    
    /* Icon data */
    nxi_icon_t* icon;
    iconlay_layer_t layers[ICONLAY_MAX_LAYERS];
    uint32_t layer_count;
    int32_t selected_layer;
    
    /* Composition buffer */
    nx_effect_buffer_t* composition;
    nx_effect_buffer_t* preview_buffer;
    
    /* Global effects */
    iconlay_effect_t global_effects[ICONLAY_MAX_EFFECTS];
    uint32_t global_effect_count;
    
    /* Material effects state */
    bool fx_glass_enabled;
    bool fx_frost_enabled;
    bool fx_metal_enabled;
    bool fx_liquid_enabled;
    bool fx_glow_enabled;
    
    /* Effect parameters */
    float fx_blur;
    float fx_brightness;
    float fx_contrast;
    float fx_saturation;
    float fx_shadow;
    float fx_glow_amount;
    float fx_depth;
    
    /* Preview settings */
    uint32_t preview_size;    /* 16, 32, 64, 128, 256, 512 */
    
    /* Export settings */
    bool export_16, export_32, export_64;
    bool export_128, export_256, export_512, export_1024;
    nxi_preset_t export_preset;
    
    /* UI state */
    int32_t mouse_x, mouse_y;
    int32_t hovered_button;
    bool dragging_layer;
    float drag_offset_x, drag_offset_y;
    
    /* Color picker */
    bool color_picker_active;
    float picker_hue, picker_sat, picker_val;
    
    /* File state */
    char filename[256];
    bool unsaved_changes;
    
    /* Reox UI widgets */
    void* effect_sliders[7];       /* iconlay_slider_t* */
    void* fx_toggles[5];           /* iconlay_toggle_t* for Glass, Frost, Metal, Liquid, Glow */
    void* layer_cards[32];         /* iconlay_layer_card_t* */
    void* color_picker;            /* iconlay_color_picker_t* */
    
    /* Application state */
    bool running;
} iconlay_app_t;

/* ============================================================================
 * Application Lifecycle
 * ============================================================================ */

/* Initialize application */
iconlay_app_t* iconlay_create(void* framebuffer, uint32_t width, uint32_t height);

/* Destroy application */
void iconlay_destroy(iconlay_app_t* app);

/* Main loop iteration */
bool iconlay_update(iconlay_app_t* app);

/* Render UI */
void iconlay_render(iconlay_app_t* app);

/* ============================================================================
 * Layer Management
 * ============================================================================ */

/* Add layer from SVG file */
int iconlay_add_layer_svg(iconlay_app_t* app, const char* svg_path);

/* Add layer from PNG file */
int iconlay_add_layer_png(iconlay_app_t* app, const char* png_path);

/* Remove layer */
void iconlay_remove_layer(iconlay_app_t* app, uint32_t index);

/* Duplicate layer */
int iconlay_duplicate_layer(iconlay_app_t* app, uint32_t index);

/* Move layer in stack */
void iconlay_move_layer(iconlay_app_t* app, uint32_t from, uint32_t to);

/* Select layer */
void iconlay_select_layer(iconlay_app_t* app, int32_t index);

/* ============================================================================
 * Effects API
 * ============================================================================ */

/* Add effect to layer */
int iconlay_add_effect(iconlay_app_t* app, uint32_t layer_idx, iconlay_effect_t effect);

/* Remove effect from layer */
void iconlay_remove_effect(iconlay_app_t* app, uint32_t layer_idx, uint32_t effect_idx);

/* Update effect parameters */
void iconlay_update_effect(iconlay_app_t* app, uint32_t layer_idx, uint32_t effect_idx, 
                           const iconlay_effect_t* params);

/* Apply all effects and compose */
void iconlay_compose(iconlay_app_t* app);

/* ============================================================================
 * Preview & Export
 * ============================================================================ */

/* Update preview at specified size */
void iconlay_update_preview(iconlay_app_t* app, uint32_t size);

/* Export to NXI format */
bool iconlay_export_nxi(iconlay_app_t* app, const char* path);

/* Export to PNG at specific size */
bool iconlay_export_png(iconlay_app_t* app, const char* path, uint32_t size);

/* ============================================================================
 * Event Handling
 * ============================================================================ */

typedef enum {
    ICONLAY_EVENT_NONE,
    ICONLAY_EVENT_MOUSE_MOVE,
    ICONLAY_EVENT_MOUSE_DOWN,
    ICONLAY_EVENT_MOUSE_UP,
    ICONLAY_EVENT_SCROLL,
    ICONLAY_EVENT_KEY_DOWN,
    ICONLAY_EVENT_KEY_UP,
    ICONLAY_EVENT_QUIT
} iconlay_event_type_t;

typedef struct {
    iconlay_event_type_t type;
    int32_t x, y;
    int32_t button;
    int32_t scroll_delta;
    uint32_t keycode;
    bool shift, ctrl, alt;
} iconlay_event_t;

/* Process input event */
void iconlay_handle_event(iconlay_app_t* app, const iconlay_event_t* event);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Get layer at screen position */
int32_t iconlay_layer_at(iconlay_app_t* app, int32_t x, int32_t y);

/* Screen to canvas coordinates */
void iconlay_screen_to_canvas(iconlay_app_t* app, int32_t sx, int32_t sy, 
                               float* cx, float* cy);

/* Mark layer as dirty (needs re-render) */
void iconlay_invalidate_layer(iconlay_app_t* app, uint32_t index);

/* Invalidate all layers */
void iconlay_invalidate_all(iconlay_app_t* app);

#ifdef __cplusplus
}
#endif

#endif /* ICONLAY_H */
