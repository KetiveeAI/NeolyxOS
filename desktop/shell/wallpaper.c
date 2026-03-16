/*
 * NeolyxOS Wallpaper Module
 * 
 * Handles loading and rendering of desktop wallpaper.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/nxappearance.h"
#include "../include/nxsyscall.h"

/* Wallpaper state */
static int g_wallpaper_loaded = 0;
static uint32_t *g_wallpaper_pixels = NULL;
static uint32_t g_wallpaper_width = 0;
static uint32_t g_wallpaper_height = 0;
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;

/* Initialize wallpaper system */
int wallpaper_init(uint32_t screen_w, uint32_t screen_h) {
    g_screen_width = screen_w;
    g_screen_height = screen_h;
    g_wallpaper_loaded = 0;
    g_wallpaper_pixels = NULL;
    
    /*
     * Wallpaper loading via libnximage is not yet ready.
     * TODO: Port libnximage to use NeolyxOS syscalls:
     *   - nx_open/nx_read/nx_close for file I/O
     *   - nx_alloc/nx_free for memory
     *   - Provide memcmp/memcpy stubs
     * 
     * For now, the system uses render_background() gradient fallback.
     */
    
    return 0;
}

/* Free wallpaper resources */
void wallpaper_shutdown(void) {
    if (g_wallpaper_pixels) {
        nx_free(g_wallpaper_pixels);
        g_wallpaper_pixels = NULL;
    }
    g_wallpaper_loaded = 0;
}

/* Render wallpaper to framebuffer */
void wallpaper_render(uint32_t *fb, uint32_t pitch, uint32_t width, uint32_t height) {
    uint32_t stride = pitch / 4;
    (void)width;
    (void)height;
    
    if (g_wallpaper_loaded && g_wallpaper_pixels) {
        /* Blit wallpaper image, scale if needed */
        uint32_t img_w = g_wallpaper_width;
        uint32_t img_h = g_wallpaper_height;
        
        for (uint32_t y = 0; y < g_screen_height; y++) {
            /* Calculate source Y with scaling */
            uint32_t src_y = (y * img_h) / g_screen_height;
            if (src_y >= img_h) src_y = img_h - 1;
            
            for (uint32_t x = 0; x < g_screen_width; x++) {
                /* Calculate source X with scaling */
                uint32_t src_x = (x * img_w) / g_screen_width;
                if (src_x >= img_w) src_x = img_w - 1;
                
                uint32_t color = g_wallpaper_pixels[src_y * img_w + src_x];
                fb[y * stride + x] = color;
            }
        }
    }
    /* Note: If wallpaper not loaded, render_background() in desktop_shell.c 
     * handles the gradient fallback */
}

/* Check if wallpaper is loaded */
int wallpaper_is_loaded(void) {
    return g_wallpaper_loaded;
}

/* Reload wallpaper (after settings change) */
int wallpaper_reload(void) {
    wallpaper_shutdown();
    return wallpaper_init(g_screen_width, g_screen_height);
}
