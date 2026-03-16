/*
 * IconLay - Main Entry Point (Native nxgfx build)
 * For testing on host system and NeolyxOS deployment
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "iconlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* For host testing, we need a minimal host window */
#ifdef NXGFX_NATIVE

/*
 * On NeolyxOS, this would connect to the desktop compositor.
 * For host testing, we create a simple framebuffer.
 */

static uint32_t* g_framebuffer = NULL;
static iconlay_app_t* g_app = NULL;

int main(int argc, char* argv[]) {
    printf("IconLay v%d.%d.%d - NeolyxOS Icon Compositor\n",
           ICONLAY_VERSION_MAJOR, ICONLAY_VERSION_MINOR, ICONLAY_VERSION_PATCH);
    printf("Using pure nxgfx backend (no SDL)\n\n");
    
    /* Allocate framebuffer */
    uint32_t width = ICONLAY_WINDOW_WIDTH;
    uint32_t height = ICONLAY_WINDOW_HEIGHT;
    
    g_framebuffer = calloc(width * height, sizeof(uint32_t));
    if (!g_framebuffer) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        return 1;
    }
    
    /* Create application */
    g_app = iconlay_create(g_framebuffer, width, height);
    if (!g_app) {
        fprintf(stderr, "Failed to create IconLay app\n");
        free(g_framebuffer);
        return 1;
    }
    
    /* Load SVG if provided */
    if (argc > 1) {
        printf("Loading: %s\n", argv[1]);
        int layer_idx = iconlay_add_layer_svg(g_app, argv[1]);
        if (layer_idx >= 0) {
            iconlay_select_layer(g_app, layer_idx);
        }
    }
    
    /* Initial compose and render */
    printf("Composing layers...\n");
    iconlay_compose(g_app);
    
    printf("Rendering UI...\n");
    iconlay_render(g_app);
    
    /* In native mode, we'd run an event loop here */
    /* For now, just do a single frame render */
    printf("\nNative build complete. Frame rendered to buffer.\n");
    printf("To see output, integrate with NeolyxOS desktop or add SDL host.\n");
    
    /* Export if layers present */
    if (g_app->layer_count > 0) {
        printf("\nExporting to test_icon.nxi...\n");
        if (iconlay_export_nxi(g_app, "test_icon.nxi")) {
            printf("Export successful!\n");
        }
    }
    
    /* Cleanup */
    iconlay_destroy(g_app);
    free(g_framebuffer);
    
    printf("\nIconLay exited cleanly.\n");
    return 0;
}

#endif /* NXGFX_NATIVE */
