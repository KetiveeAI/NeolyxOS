/*
 * NeolyxOS - NXRender Application Framework Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include "nxrender/application.h"
#include <stdlib.h>
#include <string.h>

nx_app_config_t nx_app_default_config(void) {
    return (nx_app_config_t){
        .title = "NXRender Application",
        .width = 1280,
        .height = 720,
        .fullscreen = false,
        .on_init = NULL,
        .on_update = NULL,
        .on_render = NULL,
        .on_cleanup = NULL,
        .user_data = NULL
    };
}

nx_application_t* nx_app_create(nx_app_config_t config) {
    nx_application_t* app = calloc(1, sizeof(nx_application_t));
    if (!app) return NULL;
    app->config = config;
    app->running = false;
    app->needs_redraw = true;
    return app;
}

void nx_app_destroy(nx_application_t* app) {
    if (!app) return;
    if (app->config.on_cleanup) app->config.on_cleanup(app, app->config.user_data);
    if (app->wm) nx_wm_destroy(app->wm);
    if (app->ctx) nxgfx_destroy(app->ctx);
    free(app);
}

int nx_app_run(nx_application_t* app, void* framebuffer, uint32_t pitch) {
    if (!app || !framebuffer) return -1;
    app->ctx = nxgfx_init(framebuffer, app->config.width, app->config.height, pitch);
    if (!app->ctx) return -1;
    app->wm = nx_wm_create(app->ctx);
    if (!app->wm) { nxgfx_destroy(app->ctx); return -1; }
    if (app->config.on_init) app->config.on_init(app, app->config.user_data);
    app->running = true;
    while (app->running) {
        if (app->config.on_update) app->config.on_update(app, app->delta_time, app->config.user_data);
        if (app->needs_redraw) {
            nxgfx_clear(app->ctx, nx_rgba(20, 20, 25, 255));
            nx_wm_render(app->wm);
            if (app->config.on_render) app->config.on_render(app, app->config.user_data);
            nxgfx_present(app->ctx);
            app->needs_redraw = false;
        }
    }
    return 0;
}

void nx_app_inject_mouse_move(nx_application_t* app, int32_t x, int32_t y) {
    if (!app) return;
    app->mouse_pos = (nx_point_t){x, y};
    nx_event_t event = { .type = NX_EVENT_MOUSE_MOVE, .pos = {x, y} };
    if (nx_wm_handle_event(app->wm, &event)) app->needs_redraw = true;
}

void nx_app_inject_mouse_button(nx_application_t* app, int button, bool pressed) {
    if (!app || button < 0 || button > 2) return;
    app->mouse_buttons[button] = pressed;
    nx_event_t event = {
        .type = pressed ? NX_EVENT_MOUSE_DOWN : NX_EVENT_MOUSE_UP,
        .pos = app->mouse_pos,
        .button = (nx_mouse_button_t)button
    };
    if (nx_wm_handle_event(app->wm, &event)) app->needs_redraw = true;
}

void nx_app_inject_key(nx_application_t* app, uint32_t keycode, bool pressed) {
    if (!app || !app->wm || !app->wm->focused) return;
    nx_event_t event = {
        .type = pressed ? NX_EVENT_KEY_DOWN : NX_EVENT_KEY_UP,
        .keycode = keycode
    };
    nx_window_t* win = app->wm->focused;
    if (win->root_widget && nx_widget_handle_event(win->root_widget, &event) != NX_EVENT_IGNORED) {
        app->needs_redraw = true;
    }
}

void nx_app_inject_text(nx_application_t* app, const char* text) {
    if (!app || !text || !app->wm || !app->wm->focused) return;
    nx_event_t event = { .type = NX_EVENT_TEXT_INPUT };
    strncpy(event.text, text, sizeof(event.text) - 1);
    nx_window_t* win = app->wm->focused;
    if (win->root_widget && nx_widget_handle_event(win->root_widget, &event) != NX_EVENT_IGNORED) {
        app->needs_redraw = true;
    }
}

void nx_app_request_redraw(nx_application_t* app) { if (app) app->needs_redraw = true; }
void nx_app_quit(nx_application_t* app) { if (app) app->running = false; }
nx_window_manager_t* nx_app_window_manager(nx_application_t* app) { return app ? app->wm : NULL; }
nx_context_t* nx_app_context(nx_application_t* app) { return app ? app->ctx : NULL; }
