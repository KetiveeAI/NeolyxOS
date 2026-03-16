/*
 * Reolab - REOX Development Environment
 * NeolyxOS Application
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 * 
 * This file is part of NeolyxOS and may not be copied,
 * modified, or distributed without express permission.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "include/reolab.h"
#include "src/app/app.h"

int main(int argc, char* argv[]) {
    printf("[Reolab] Starting REOX Development Environment...\n");
    
    ReoLabApp* app = reolab_app_create("Reolab", 1280, 800);
    if (!app) {
        fprintf(stderr, "[Reolab] Failed to create application\n");
        return 1;
    }
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            reolab_app_open_file(app, argv[i]);
        }
    }
    
    /* Run main loop */
    int result = reolab_app_run(app);
    
    /* Cleanup */
    reolab_app_destroy(app);
    
    printf("[Reolab] Exited with code %d\n", result);
    return result;
}
