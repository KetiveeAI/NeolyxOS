/*
 * IconLay - Application Core
 * Initialization, layer management, and main loop
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __linux__
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#else
#include <SDL.h>
#include <SDL_image.h>
#endif

/* ============================================================================
 * Application Lifecycle
 * ============================================================================ */

iconlay_app_t* iconlay_create(void* framebuffer, uint32_t width, uint32_t height) {
    iconlay_app_t* app = calloc(1, sizeof(iconlay_app_t));
    if (!app) return NULL;
    
    /* Initialize graphics context */
    app->framebuffer = framebuffer;
    app->fb_width = width;
    app->fb_height = height;
    
    app->gfx = nxgfx_init(framebuffer, width, height, width * 4);
    if (!app->gfx) {
        free(app);
        return NULL;
    }
    
    /* Initialize composition buffers */
    app->preview_size = 512;
    app->composition = nx_effect_buffer_create(512, 512);
    app->preview_buffer = nx_effect_buffer_create(512, 512);
    
    if (!app->composition || !app->preview_buffer) {
        iconlay_destroy(app);
        return NULL;
    }
    
    /* Initialize state */
    app->layer_count = 0;
    app->selected_layer = -1;
    app->hovered_button = -1;
    
    /* Default export settings */
    app->export_16 = true;
    app->export_32 = true;
    app->export_64 = true;
    app->export_128 = true;
    app->export_256 = true;
    app->export_512 = true;
    app->export_1024 = true;
    app->export_preset = NXI_PRESET_ALL;
    
    /* Default effect values */
    app->fx_blur = 42;
    app->fx_brightness = 100;
    app->fx_contrast = 105;
    app->fx_saturation = 100;
    app->fx_shadow = 35;
    app->fx_glow_amount = 20;
    app->fx_depth = 15;
    
    /* Material effects off by default */
    app->fx_glass_enabled = true;
    app->fx_frost_enabled = false;
    app->fx_metal_enabled = false;
    app->fx_liquid_enabled = false;
    app->fx_glow_enabled = true;
    
    /* File state */
    strcpy(app->filename, "untitled_icon.nxi");
    app->unsaved_changes = false;
    
    app->running = true;
    
    printf("[IconLay] Initialized %ux%u nxgfx context\n", width, height);
    return app;
}

void iconlay_destroy(iconlay_app_t* app) {
    if (!app) return;
    
    /* Free layer resources */
    for (uint32_t i = 0; i < app->layer_count; i++) {
        if (app->layers[i].svg) {
            nxsvg_free(app->layers[i].svg);
        }
        if (app->layers[i].cache) {
            nx_effect_buffer_destroy(app->layers[i].cache);
        }
    }
    
    /* Free composition buffers */
    if (app->composition) nx_effect_buffer_destroy(app->composition);
    if (app->preview_buffer) nx_effect_buffer_destroy(app->preview_buffer);
    
    /* Free icon */
    if (app->icon) nxi_free(app->icon);
    
    /* Destroy graphics context */
    if (app->gfx) nxgfx_destroy(app->gfx);
    
    free(app);
    printf("[IconLay] Destroyed\n");
}

/* ============================================================================
 * Layer Management
 * ============================================================================ */

int iconlay_add_layer_svg(iconlay_app_t* app, const char* svg_path) {
    if (!app || !svg_path) return -1;
    if (app->layer_count >= ICONLAY_MAX_LAYERS) return -1;
    
    /* Load SVG */
    nxsvg_image_t* svg = nxsvg_load(svg_path);
    if (!svg) {
        printf("[IconLay] Failed to load SVG: %s\n", svg_path);
        return -1;
    }
    
    /* Initialize layer */
    iconlay_layer_t* layer = &app->layers[app->layer_count];
    memset(layer, 0, sizeof(iconlay_layer_t));
    
    /* Extract filename for layer name */
    const char* name = strrchr(svg_path, '/');
    name = name ? name + 1 : svg_path;
    strncpy(layer->name, name, sizeof(layer->name) - 1);
    
    layer->svg = svg;
    layer->pos_x = 0.0f;
    layer->pos_y = 0.0f;
    layer->scale = 1.0f;
    layer->rotation = 0.0f;
    layer->opacity = 255;
    layer->visible = true;
    layer->locked = false;
    layer->blend_mode = NX_BLEND_NORMAL;
    layer->dirty = true;
    
    app->layer_count++;
    app->unsaved_changes = true;
    
    printf("[IconLay] Added layer: %s (%ux%u)\n", layer->name, 
           (unsigned)svg->width, (unsigned)svg->height);
    
    return (int)app->layer_count - 1;
}

int iconlay_add_layer_png(iconlay_app_t* app, const char* png_path) {
    if (!app || !png_path) return -1;
    if (app->layer_count >= ICONLAY_MAX_LAYERS) return -1;
    
    /* Create placeholder layer (PNG loading via nxgfx_image_load) */
    iconlay_layer_t* layer = &app->layers[app->layer_count];
    memset(layer, 0, sizeof(iconlay_layer_t));
    
    const char* name = strrchr(png_path, '/');
    name = name ? name + 1 : png_path;
    strncpy(layer->name, name, sizeof(layer->name) - 1);
    
    layer->pos_x = 0.0f;
    layer->pos_y = 0.0f;
    layer->scale = 1.0f;
    layer->opacity = 255;
    layer->visible = true;
    layer->blend_mode = NX_BLEND_NORMAL;
    layer->dirty = true;
    
    app->layer_count++;
    app->unsaved_changes = true;
    
    printf("[IconLay] Added PNG layer: %s\n", layer->name);
    return (int)app->layer_count - 1;
}

void iconlay_remove_layer(iconlay_app_t* app, uint32_t index) {
    if (!app || index >= app->layer_count) return;
    
    /* Free layer resources */
    iconlay_layer_t* layer = &app->layers[index];
    if (layer->svg) nxsvg_free(layer->svg);
    if (layer->cache) nx_effect_buffer_destroy(layer->cache);
    
    /* Shift layers down */
    for (uint32_t i = index; i < app->layer_count - 1; i++) {
        app->layers[i] = app->layers[i + 1];
    }
    app->layer_count--;
    
    /* Adjust selection */
    if (app->selected_layer == (int32_t)index) {
        app->selected_layer = (app->layer_count > 0) ? 0 : -1;
    } else if (app->selected_layer > (int32_t)index) {
        app->selected_layer--;
    }
    
    app->unsaved_changes = true;
}

int iconlay_duplicate_layer(iconlay_app_t* app, uint32_t index) {
    if (!app || index >= app->layer_count) return -1;
    if (app->layer_count >= ICONLAY_MAX_LAYERS) return -1;
    
    iconlay_layer_t* src = &app->layers[index];
    iconlay_layer_t* dst = &app->layers[app->layer_count];
    
    *dst = *src;
    dst->cache = NULL;
    dst->dirty = true;
    
    /* Append " copy" to name */
    size_t len = strlen(dst->name);
    if (len < sizeof(dst->name) - 6) {
        strcat(dst->name, " copy");
    }
    
    /* Note: SVG is shared, not duplicated */
    
    app->layer_count++;
    app->unsaved_changes = true;
    
    return (int)app->layer_count - 1;
}

void iconlay_move_layer(iconlay_app_t* app, uint32_t from, uint32_t to) {
    if (!app || from >= app->layer_count || to >= app->layer_count || from == to) return;
    
    iconlay_layer_t temp = app->layers[from];
    
    if (from < to) {
        for (uint32_t i = from; i < to; i++) {
            app->layers[i] = app->layers[i + 1];
        }
    } else {
        for (uint32_t i = from; i > to; i--) {
            app->layers[i] = app->layers[i - 1];
        }
    }
    
    app->layers[to] = temp;
    app->unsaved_changes = true;
}

void iconlay_select_layer(iconlay_app_t* app, int32_t index) {
    if (!app) return;
    if (index < 0 || (uint32_t)index >= app->layer_count) {
        app->selected_layer = -1;
    } else {
        app->selected_layer = index;
    }
}

/* ============================================================================
 * Event Handling
 * ============================================================================ */

void iconlay_handle_event(iconlay_app_t* app, const iconlay_event_t* event) {
    if (!app || !event) return;
    
    switch (event->type) {
        case ICONLAY_EVENT_MOUSE_MOVE:
            app->mouse_x = event->x;
            app->mouse_y = event->y;
            
            if (app->dragging_layer && app->selected_layer >= 0) {
                iconlay_layer_t* layer = &app->layers[app->selected_layer];
                
                /* Convert screen to canvas coordinates */
                float cx, cy;
                iconlay_screen_to_canvas(app, event->x, event->y, &cx, &cy);
                
                layer->pos_x = cx - app->drag_offset_x;
                layer->pos_y = cy - app->drag_offset_y;
                layer->dirty = true;
            }
            break;
            
        case ICONLAY_EVENT_MOUSE_DOWN:
            if (event->button == 1) {
                /* Check for layer click on canvas */
                int32_t layer_idx = iconlay_layer_at(app, event->x, event->y);
                if (layer_idx >= 0) {
                    iconlay_select_layer(app, layer_idx);
                    app->dragging_layer = true;
                    
                    float cx, cy;
                    iconlay_screen_to_canvas(app, event->x, event->y, &cx, &cy);
                    app->drag_offset_x = cx - app->layers[layer_idx].pos_x;
                    app->drag_offset_y = cy - app->layers[layer_idx].pos_y;
                }
            }
            break;
            
        case ICONLAY_EVENT_MOUSE_UP:
            app->dragging_layer = false;
            break;
            
        case ICONLAY_EVENT_SCROLL:
            if (app->selected_layer >= 0) {
                iconlay_layer_t* layer = &app->layers[app->selected_layer];
                float delta = event->scroll_delta * 0.1f;
                layer->scale = layer->scale + delta;
                if (layer->scale < 0.1f) layer->scale = 0.1f;
                if (layer->scale > 5.0f) layer->scale = 5.0f;
                layer->dirty = true;
            }
            break;
            
        case ICONLAY_EVENT_KEY_DOWN:
            switch (event->keycode) {
                case 's':
                case 'S':
                    if (event->ctrl) {
                        iconlay_export_nxi(app, app->filename);
                        app->unsaved_changes = false;
                    }
                    break;
                    
                case 'i':
                case 'I':
                    if (event->ctrl) {
                        /* Trigger import dialog */
                    }
                    break;
                    
                case 127: /* Delete */
                    if (app->selected_layer >= 0) {
                        iconlay_remove_layer(app, app->selected_layer);
                    }
                    break;
            }
            break;
            
        case ICONLAY_EVENT_QUIT:
            app->running = false;
            break;
            
        default:
            break;
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int32_t iconlay_layer_at(iconlay_app_t* app, int32_t x, int32_t y) {
    if (!app) return -1;
    
    /* Convert screen to canvas coordinates */
    float cx, cy;
    iconlay_screen_to_canvas(app, x, y, &cx, &cy);
    
    /* Check layers from top to bottom */
    for (int32_t i = app->layer_count - 1; i >= 0; i--) {
        iconlay_layer_t* layer = &app->layers[i];
        if (!layer->visible) continue;
        
        /* Simple bounding box hit test */
        float lx = layer->pos_x;
        float ly = layer->pos_y;
        float lw = layer->scale;
        float lh = layer->scale;
        
        if (layer->svg) {
            lw *= layer->svg->width / 512.0f;
            lh *= layer->svg->height / 512.0f;
        }
        
        if (cx >= lx && cx <= lx + lw && cy >= ly && cy <= ly + lh) {
            return i;
        }
    }
    
    return -1;
}

void iconlay_screen_to_canvas(iconlay_app_t* app, int32_t sx, int32_t sy,
                               float* cx, float* cy) {
    if (!app || !cx || !cy) return;
    
    /* Calculate canvas position (centered between panels) */
    int canvas_x = ICONLAY_LEFT_PANEL_WIDTH + 
                   (app->fb_width - ICONLAY_LEFT_PANEL_WIDTH - ICONLAY_RIGHT_PANEL_WIDTH - 
                    ICONLAY_CANVAS_SIZE) / 2;
    int canvas_y = ICONLAY_TOOLBAR_HEIGHT + 80;
    
    *cx = (float)(sx - canvas_x) / ICONLAY_CANVAS_SIZE;
    *cy = (float)(sy - canvas_y) / ICONLAY_CANVAS_SIZE;
}

/* ============================================================================
 * Export Functions
 * ============================================================================ */

bool iconlay_export_nxi(iconlay_app_t* app, const char* path) {
    if (!app || !path) return false;
    
    /* Create NXI icon if not exists */
    if (!app->icon) {
        app->icon = nxi_create();
        if (!app->icon) return false;
    }
    
    /* Save NXI */
    int result = nxi_save(app->icon, path);
    printf("[IconLay] Exported: %s (result=%d)\n", path, result);
    
    return result == 0;
}

bool iconlay_export_png(iconlay_app_t* app, const char* path, uint32_t size) {
    if (!app || !path || size == 0) return false;
    
    /* Generate preview at specified size */
    iconlay_update_preview(app, size);
    
    /* Create SDL surface for PNG saving */
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, size, size, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surface) {
        printf("[IconLay] PNG export: failed to create surface\n");
        return false;
    }
    
    /* Render composition to buffer */
    if (app->preview_buffer && app->preview_buffer->data) {
        memcpy(surface->pixels, app->preview_buffer->data, size * size * 4);
    } else {
        /* Fallback: render layers directly to surface */
        memset(surface->pixels, 0, size * size * 4);
        if (app->icon) {
            layers_render_all(app->icon->layers, app->icon->layer_count,
                            surface->pixels, size);
        }
    }
    
    /* Save PNG using SDL_image */
    int result = IMG_SavePNG(surface, path);
    SDL_FreeSurface(surface);
    
    if (result == 0) {
        printf("[IconLay] Exported PNG: %s (%ux%u)\n", path, size, size);
        return true;
    } else {
        printf("[IconLay] PNG export failed: %s\n", IMG_GetError());
        return false;
    }
}

/* ============================================================================
 * Main Loop
 * ============================================================================ */

bool iconlay_update(iconlay_app_t* app) {
    if (!app) return false;
    
    /* Recompose if any layer is dirty */
    bool needs_compose = false;
    for (uint32_t i = 0; i < app->layer_count; i++) {
        if (app->layers[i].dirty) {
            needs_compose = true;
            break;
        }
    }
    
    if (needs_compose) {
        iconlay_compose(app);
    }
    
    return app->running;
}
