/*
 * Reolab - Application Implementation
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ReoLabApp* reolab_app_create(const char* title, int width, int height) {
    ReoLabApp* app = (ReoLabApp*)calloc(1, sizeof(ReoLabApp));
    if (!app) return NULL;
    
    strncpy(app->title, title, sizeof(app->title) - 1);
    app->width = width;
    app->height = height;
    app->running = false;
    app->tab_count = 0;
    app->active_tab = -1;
    
    /* Initialize graphics backend */
    if (!reolab_init_graphics(app)) {
        free(app);
        return NULL;
    }
    
    /* Initialize REOX UI */
    if (!reolab_init_ui(app)) {
        free(app);
        return NULL;
    }
    
    printf("[Reolab] Application created: %dx%d\n", width, height);
    return app;
}

void reolab_app_destroy(ReoLabApp* app) {
    if (!app) return;
    
    /* Close all tabs */
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].buffer) {
            reolab_buffer_destroy(app->tabs[i].buffer);
        }
    }
    
    /* TODO: Cleanup graphics and UI */
    
    free(app);
    printf("[Reolab] Application destroyed\n");
}

int reolab_app_run(ReoLabApp* app) {
    if (!app) return -1;
    
    app->running = true;
    printf("[Reolab] Entering main loop\n");
    
    while (app->running) {
        reolab_handle_input(app);
        reolab_render(app);
    }
    
    return 0;
}

void reolab_app_quit(ReoLabApp* app) {
    if (app) app->running = false;
}

bool reolab_app_open_file(ReoLabApp* app, const char* path) {
    if (!app || !path) return false;
    if (app->tab_count >= 32) return false;
    
    /* Read file content */
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[Reolab] Cannot open file: %s\n", path);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = (char*)malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    /* Create buffer and tab */
    ReoLabBuffer* buffer = reolab_buffer_create();
    reolab_buffer_insert(buffer, 0, content);
    free(content);
    
    int idx = app->tab_count++;
    app->tabs[idx].buffer = buffer;
    strncpy(app->tabs[idx].path, path, sizeof(app->tabs[idx].path) - 1);
    app->tabs[idx].modified = false;
    app->active_tab = idx;
    
    printf("[Reolab] Opened file: %s\n", path);
    return true;
}

bool reolab_app_save_file(ReoLabApp* app) {
    if (!app || app->active_tab < 0) return false;
    
    const char* path = app->tabs[app->active_tab].path;
    const char* text = reolab_buffer_get_text(app->tabs[app->active_tab].buffer);
    
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    
    app->tabs[app->active_tab].modified = false;
    printf("[Reolab] Saved file: %s\n", path);
    return true;
}

bool reolab_app_save_file_as(ReoLabApp* app, const char* path) {
    if (!app || app->active_tab < 0 || !path) return false;
    
    strncpy(app->tabs[app->active_tab].path, path, 
            sizeof(app->tabs[app->active_tab].path) - 1);
    return reolab_app_save_file(app);
}

bool reolab_app_close_file(ReoLabApp* app) {
    if (!app || app->active_tab < 0) return false;
    
    /* TODO: Check for unsaved changes */
    
    reolab_buffer_destroy(app->tabs[app->active_tab].buffer);
    
    /* Shift remaining tabs */
    for (int i = app->active_tab; i < app->tab_count - 1; i++) {
        app->tabs[i] = app->tabs[i + 1];
    }
    app->tab_count--;
    
    if (app->active_tab >= app->tab_count) {
        app->active_tab = app->tab_count - 1;
    }
    
    return true;
}

/* Real graphics implementations using SDL2/NXRender */
#include "../graphics/graphics.h"
#include "../ui/ui.h"

static RLGraphics* g_gfx = NULL;

bool reolab_init_graphics(ReoLabApp* app) {
    g_gfx = rl_graphics_create(app->title, app->width, app->height);
    if (!g_gfx) {
        fprintf(stderr, "[Reolab] Failed to initialize graphics\n");
        return false;
    }
    printf("[Reolab] Graphics initialized\n");
    return true;
}

bool reolab_init_ui(ReoLabApp* app) {
    (void)app;
    printf("[Reolab] UI initialized\n");
    return true;
}

void reolab_handle_input(ReoLabApp* app) {
    if (!g_gfx) {
        app->running = false;
        return;
    }
    
    if (!rl_graphics_poll_events(g_gfx)) {
        app->running = false;
    }
}

void reolab_render(ReoLabApp* app) {
    if (!g_gfx) return;
    
    rl_graphics_begin_frame(g_gfx);
    rl_ui_render(g_gfx, app);
    rl_graphics_end_frame(g_gfx);
}

void reolab_cleanup_graphics(void) {
    if (g_gfx) {
        rl_graphics_destroy(g_gfx);
        g_gfx = NULL;
    }
}

