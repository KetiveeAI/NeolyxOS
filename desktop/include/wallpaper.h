/*
 * NeolyxOS Wallpaper Module Header
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXWALLPAPER_H
#define NXWALLPAPER_H

#include <stdint.h>

/* Initialize wallpaper system */
int wallpaper_init(uint32_t screen_w, uint32_t screen_h);

/* Shutdown and free wallpaper resources */
void wallpaper_shutdown(void);

/* Render wallpaper to framebuffer */
void wallpaper_render(uint32_t *fb, uint32_t pitch, uint32_t width, uint32_t height);

/* Check if wallpaper is loaded */
int wallpaper_is_loaded(void);

/* Reload wallpaper after settings change */
int wallpaper_reload(void);

#endif /* NXWALLPAPER_H */
