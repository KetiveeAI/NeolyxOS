/*
 * Frame - Main Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx_minimal.h"
#include "app.h"
#include "ui/main_window.h"

static frame_app_t g_app;

void on_init(nx_application_t* app, void* user_data) {
    (void)user_data;
    frame_app_init(&g_app);
    
    // Initialize Reox UI
    ui_main_window_create(nx_app_window_manager(app)->windows);
}

void on_render(nx_application_t* app, void* user_data) {
    (void)user_data;
    frame_app_render(&g_app, app->ctx);
}

int main(int argc, char** argv) {
    nx_app_config_t config = nx_app_default_config();
    config.title = "Frame";
    config.width = 800;
    config.height = 600;
    config.on_init = on_init;
    config.on_render = on_render;
    
    nx_application_t* app = nx_app_create(config);
    if (app) {
        if (argc > 1) {
            frame_app_load_image(&g_app, argv[1]);
        }
        nx_app_run(app, NULL, 0);
        nx_app_destroy(app);
    }
    return 0;
}
