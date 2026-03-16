/*
 * Frame - Image Viewer
 * Application Logic
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "app.h"
#include "ui/main_window.h"
#include <stdio.h>
#include <string.h>

void frame_app_init(frame_app_t* app) {
    memset(app, 0, sizeof(frame_app_t));
}

void frame_app_load_image(frame_app_t* app, const char* path) {
    if (app->current_image.pixels) {
        nxgfx_image_destroy(app->gfx_image);
        nxi_free(&app->current_image);
    }
    
    if (nxi_load(path, &app->current_image) == NXI_OK) {
        app->has_image = true;
        
        // Update Reox Bindings
        ui_main_window_update_binding("image_path", (void*)path);
        bool has_img = true;
        ui_main_window_update_binding("has_image", &has_img);
        
        printf("[Frame] Loaded %s\n", path);
    } else {
        printf("[Frame] Failed to load %s\n", path);
    }
}

void frame_app_render(frame_app_t* app, nx_context_t* ctx) {
    if (app->has_image && !app->gfx_image) {
        // Create texture on first render
        app->gfx_image = nxgfx_image_create(ctx, app->current_image.pixels,
                                            app->current_image.width, 
                                            app->current_image.height);
    }
    
    if (app->gfx_image) {
        // Render image centered
        // Use current_image for dimensions as gfx_image is opaque
        int x = (nxgfx_width(ctx) - app->current_image.width) / 2;
        int y = (nxgfx_height(ctx) - app->current_image.height) / 2;
        nxgfx_draw_image(ctx, app->gfx_image, (nx_point_t){x, y});
    }
}
