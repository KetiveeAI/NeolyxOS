/*
 * Reolab - Application Core
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#ifndef REOLAB_APP_H
#define REOLAB_APP_H

#include "../include/reolab.h"

/* Application state */
struct ReoLabApp {
    char title[256];
    int width;
    int height;
    bool running;
    
    /* Current state */
    ReoLabEditor* active_editor;
    ReoLabProject* current_project;
    
    /* UI state */
    void* window;           /* NXRender window handle */
    void* ui_root;          /* REOX UI root */
    
    /* File tabs */
    struct {
        ReoLabBuffer* buffer;
        char path[512];
        bool modified;
    } tabs[32];
    int tab_count;
    int active_tab;
};

/* Internal functions */
bool reolab_init_graphics(ReoLabApp* app);
bool reolab_init_ui(ReoLabApp* app);
void reolab_handle_input(ReoLabApp* app);
void reolab_render(ReoLabApp* app);

#endif /* REOLAB_APP_H */
