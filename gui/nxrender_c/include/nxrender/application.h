/*
 * NeolyxOS - NXRender Application Framework
 * High-level application lifecycle and main loop
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_APPLICATION_H
#define NXRENDER_APPLICATION_H

#include "nxgfx/nxgfx.h"
#include "nxrender/window.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_application nx_application_t;

/* Application callbacks */
typedef void (*nx_app_init_callback_t)(nx_application_t* app, void* user_data);
typedef void (*nx_app_update_callback_t)(nx_application_t* app, float dt, void* user_data);
typedef void (*nx_app_render_callback_t)(nx_application_t* app, void* user_data);
typedef void (*nx_app_cleanup_callback_t)(nx_application_t* app, void* user_data);

/* Application configuration */
typedef struct {
    const char* title;
    uint32_t width;
    uint32_t height;
    bool fullscreen;
    nx_app_init_callback_t on_init;
    nx_app_update_callback_t on_update;
    nx_app_render_callback_t on_render;
    nx_app_cleanup_callback_t on_cleanup;
    void* user_data;
} nx_app_config_t;

/* Application state */
struct nx_application {
    nx_app_config_t config;
    nx_context_t* ctx;
    nx_window_manager_t* wm;
    bool running;
    bool needs_redraw;
    uint64_t last_frame_time;
    float delta_time;
    nx_point_t mouse_pos;
    bool mouse_buttons[3];
};

/* Create default application config */
nx_app_config_t nx_app_default_config(void);

/* Application lifecycle */
nx_application_t* nx_app_create(nx_app_config_t config);
void nx_app_destroy(nx_application_t* app);

/* Main loop (call this with framebuffer from kernel) */
int nx_app_run(nx_application_t* app, void* framebuffer, uint32_t pitch);

/* Event injection (from kernel input drivers) */
void nx_app_inject_mouse_move(nx_application_t* app, int32_t x, int32_t y);
void nx_app_inject_mouse_button(nx_application_t* app, int button, bool pressed);
void nx_app_inject_key(nx_application_t* app, uint32_t keycode, bool pressed);
void nx_app_inject_text(nx_application_t* app, const char* text);

/* Control */
void nx_app_request_redraw(nx_application_t* app);
void nx_app_quit(nx_application_t* app);

/* Accessors */
nx_window_manager_t* nx_app_window_manager(nx_application_t* app);
nx_context_t* nx_app_context(nx_application_t* app);

#ifdef __cplusplus
}
#endif
#endif
