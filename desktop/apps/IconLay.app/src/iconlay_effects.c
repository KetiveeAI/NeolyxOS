/*
 * IconLay - Effects Pipeline
 * Layer compositing and effect application
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Layer Rendering
 * ============================================================================ */

static void render_layer_to_buffer(iconlay_app_t* app, iconlay_layer_t* layer, 
                                   nx_effect_buffer_t* target, uint32_t size) {
    if (!layer || !layer->svg || !target) return;
    
    /* Clear layer cache if dirty */
    if (layer->dirty || !layer->cache) {
        if (!layer->cache) {
            layer->cache = nx_effect_buffer_create(size, size);
        }
        if (!layer->cache) return;
        
        nx_effect_buffer_clear(layer->cache, (nx_color_t){0, 0, 0, 0});
        
        /* Render SVG to temporary context */
        nx_context_t* temp_ctx = nxgfx_init(layer->cache->pixels, size, size, size * 4);
        if (temp_ctx) {
            float scale = layer->scale * (float)size / 512.0f;
            nx_point_t pos = {
                (int32_t)(layer->pos_x * size),
                (int32_t)(layer->pos_y * size)
            };
            nxsvg_render(temp_ctx, layer->svg, pos, scale);
            nxgfx_destroy(temp_ctx);
        }
        
        /* Apply layer effects */
        for (uint32_t i = 0; i < layer->effect_count; i++) {
            iconlay_effect_t* fx = &layer->effects[i];
            if (!fx->enabled) continue;
            
            switch (fx->type) {
                case ICONLAY_FX_BLUR:
                    nx_effect_blur(layer->cache, &fx->params.blur);
                    break;
                    
                case ICONLAY_FX_GLOW:
                    /* Create mask from alpha channel */
                    nx_effect_glow(layer->cache, layer->cache, &fx->params.glow);
                    break;
                    
                case ICONLAY_FX_COLOR_ADJUST:
                    nx_effect_color_adjust(layer->cache, &fx->params.color);
                    break;
                    
                default:
                    break;
            }
        }
        
        layer->dirty = false;
    }
    
    /* Composite to target with blend mode and opacity */
    float opacity = layer->opacity / 255.0f;
    nx_effect_composite(target, layer->cache, layer->blend_mode, opacity);
}

/* ============================================================================
 * Global Effects Application
 * ============================================================================ */

static void apply_global_effects(iconlay_app_t* app, nx_effect_buffer_t* buf) {
    if (!app || !buf) return;
    
    /* Apply global color adjustments */
    nx_color_adjust_t adjust = {
        .brightness = app->fx_brightness - 100.0f,
        .contrast = app->fx_contrast - 100.0f,
        .saturation = app->fx_saturation - 100.0f,
        .hue_shift = 0,
        .temperature = 0,
        .tint = 0
    };
    
    if (adjust.brightness != 0 || adjust.contrast != 0 || adjust.saturation != 0) {
        nx_effect_color_adjust(buf, &adjust);
    }
    
    /* Apply global blur if set */
    if (app->fx_blur > 0) {
        nx_effect_gaussian_blur(buf, app->fx_blur / 5.0f);
    }
    
    /* Apply global shadow if set */
    if (app->fx_shadow > 0) {
        nx_shadow_effect_t shadow = {
            .color = {0, 0, 0, 180},
            .offset_x = 0,
            .offset_y = app->fx_shadow / 10.0f,
            .blur = app->fx_shadow / 3.0f,
            .spread = 0,
            .opacity = app->fx_shadow / 100.0f,
            .inset = false
        };
        nx_effect_shadow(buf, buf, &shadow);
    }
    
    /* Apply global glow if enabled */
    if (app->fx_glow_enabled && app->fx_glow_amount > 0) {
        nx_glow_effect_t glow = nx_glow_preset_soft((nx_color_t){255, 255, 255, 200});
        glow.intensity = app->fx_glow_amount / 100.0f;
        glow.radius = app->fx_glow_amount / 5.0f;
        nx_effect_glow(buf, buf, &glow);
    }
}

/* ============================================================================
 * Material Effects
 * ============================================================================ */

static void apply_material_effects(iconlay_app_t* app, nx_effect_buffer_t* buf,
                                   nx_effect_buffer_t* background) {
    if (!app || !buf) return;
    
    nx_rect_t full_region = {0, 0, buf->width, buf->height};
    
    /* Glass effect */
    if (app->fx_glass_enabled) {
        nx_glass_effect_t glass = nx_glass_preset_default();
        glass.blur_radius = 15.0f + app->fx_depth * 0.2f;
        glass.opacity = 0.6f;
        nx_effect_glass(buf, background ? background : buf, &glass, full_region);
    }
    
    /* Frost effect */
    if (app->fx_frost_enabled) {
        nx_frost_effect_t frost = nx_frost_preset_default();
        frost.frost_intensity = 40.0f + app->fx_depth * 0.4f;
        frost.blur_radius = 10.0f;
        nx_effect_frost(buf, background ? background : buf, &frost, full_region);
    }
    
    /* Metal effect (simulate with orb lighting) */
    if (app->fx_metal_enabled) {
        /* Apply metallic shading using color adjustment */
        nx_color_adjust_t metal = {
            .brightness = 5.0f,
            .contrast = 15.0f,
            .saturation = -30.0f,
            .hue_shift = 0,
            .temperature = -10.0f,
            .tint = 0
        };
        nx_effect_color_adjust(buf, &metal);
    }
    
    /* Liquid effect (high specular + distortion) */
    if (app->fx_liquid_enabled) {
        nx_color_adjust_t liquid = {
            .brightness = 10.0f,
            .contrast = 20.0f,
            .saturation = 20.0f,
            .hue_shift = 0,
            .temperature = 0,
            .tint = 0
        };
        nx_effect_color_adjust(buf, &liquid);
        
        /* Slight blur for liquid smoothness */
        nx_effect_gaussian_blur(buf, 1.5f);
    }
}

/* ============================================================================
 * Composition Pipeline
 * ============================================================================ */

void iconlay_compose(iconlay_app_t* app) {
    if (!app || !app->composition) return;
    
    uint32_t size = app->preview_size;
    
    /* Resize composition buffer if needed */
    if (app->composition->width != size || app->composition->height != size) {
        nx_effect_buffer_destroy(app->composition);
        app->composition = nx_effect_buffer_create(size, size);
        if (!app->composition) return;
    }
    
    /* Clear composition buffer */
    nx_effect_buffer_clear(app->composition, (nx_color_t){0, 0, 0, 0});
    
    /* Render each layer from bottom to top */
    for (uint32_t i = 0; i < app->layer_count; i++) {
        iconlay_layer_t* layer = &app->layers[i];
        if (!layer->visible) continue;
        
        /* Resize layer cache if needed */
        if (layer->cache && (layer->cache->width != size || layer->cache->height != size)) {
            nx_effect_buffer_destroy(layer->cache);
            layer->cache = NULL;
            layer->dirty = true;
        }
        
        render_layer_to_buffer(app, layer, app->composition, size);
    }
    
    /* Apply material effects */
    apply_material_effects(app, app->composition, NULL);
    
    /* Apply global effects */
    apply_global_effects(app, app->composition);
    
    /* Update preview buffer */
    if (app->preview_buffer) {
        nx_effect_buffer_copy(app->preview_buffer, app->composition);
    }
}

/* ============================================================================
 * Preview Generation
 * ============================================================================ */

void iconlay_update_preview(iconlay_app_t* app, uint32_t size) {
    if (!app) return;
    
    app->preview_size = size;
    
    /* Invalidate all layers to force re-render at new size */
    for (uint32_t i = 0; i < app->layer_count; i++) {
        app->layers[i].dirty = true;
    }
    
    /* Recompose */
    iconlay_compose(app);
}

/* ============================================================================
 * Effect Management
 * ============================================================================ */

int iconlay_add_effect(iconlay_app_t* app, uint32_t layer_idx, iconlay_effect_t effect) {
    if (!app || layer_idx >= app->layer_count) return -1;
    
    iconlay_layer_t* layer = &app->layers[layer_idx];
    if (layer->effect_count >= ICONLAY_MAX_EFFECTS) return -1;
    
    layer->effects[layer->effect_count] = effect;
    layer->effect_count++;
    layer->dirty = true;
    
    return (int)layer->effect_count - 1;
}

void iconlay_remove_effect(iconlay_app_t* app, uint32_t layer_idx, uint32_t effect_idx) {
    if (!app || layer_idx >= app->layer_count) return;
    
    iconlay_layer_t* layer = &app->layers[layer_idx];
    if (effect_idx >= layer->effect_count) return;
    
    /* Shift effects down */
    for (uint32_t i = effect_idx; i < layer->effect_count - 1; i++) {
        layer->effects[i] = layer->effects[i + 1];
    }
    layer->effect_count--;
    layer->dirty = true;
}

void iconlay_update_effect(iconlay_app_t* app, uint32_t layer_idx, uint32_t effect_idx,
                           const iconlay_effect_t* params) {
    if (!app || layer_idx >= app->layer_count || !params) return;
    
    iconlay_layer_t* layer = &app->layers[layer_idx];
    if (effect_idx >= layer->effect_count) return;
    
    layer->effects[effect_idx] = *params;
    layer->dirty = true;
}

/* ============================================================================
 * Layer Invalidation
 * ============================================================================ */

void iconlay_invalidate_layer(iconlay_app_t* app, uint32_t index) {
    if (!app || index >= app->layer_count) return;
    app->layers[index].dirty = true;
}

void iconlay_invalidate_all(iconlay_app_t* app) {
    if (!app) return;
    for (uint32_t i = 0; i < app->layer_count; i++) {
        app->layers[i].dirty = true;
    }
}

/* ============================================================================
 * Effect Presets for IconLay
 * ============================================================================ */

iconlay_effect_t iconlay_effect_preset_glass(void) {
    iconlay_effect_t fx = {0};
    fx.type = ICONLAY_FX_GLASS;
    fx.enabled = true;
    fx.intensity = 70.0f;
    fx.params.glass = nx_glass_preset_default();
    return fx;
}

iconlay_effect_t iconlay_effect_preset_frost(void) {
    iconlay_effect_t fx = {0};
    fx.type = ICONLAY_FX_FROST;
    fx.enabled = true;
    fx.intensity = 60.0f;
    fx.params.frost = nx_frost_preset_default();
    return fx;
}

iconlay_effect_t iconlay_effect_preset_glow(nx_color_t color) {
    iconlay_effect_t fx = {0};
    fx.type = ICONLAY_FX_GLOW;
    fx.enabled = true;
    fx.intensity = 50.0f;
    fx.params.glow = nx_glow_preset_soft(color);
    return fx;
}

iconlay_effect_t iconlay_effect_preset_shadow(void) {
    iconlay_effect_t fx = {0};
    fx.type = ICONLAY_FX_SHADOW;
    fx.enabled = true;
    fx.intensity = 40.0f;
    fx.params.shadow = (nx_shadow_effect_t){
        .color = {0, 0, 0, 128},
        .offset_x = 0,
        .offset_y = 4,
        .blur = 8,
        .spread = 0,
        .opacity = 0.5f,
        .inset = false
    };
    return fx;
}
