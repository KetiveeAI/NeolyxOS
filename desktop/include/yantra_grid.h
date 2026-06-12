/*
 * NeolyxOS - Yantra Grid Overlay
 *
 * Subtle geometric mesh drawn over the desktop wallpaper layer.
 * 48px diamond+crosshair grid that aligns with dock icon spacing
 * and snap zone boundaries. Purely decorative — zero CPU after init.
 *
 * Copyright (c) 2026 KetiveeAI
 */

#ifndef NEOLYX_YANTRA_GRID_H
#define NEOLYX_YANTRA_GRID_H

#include <stdint.h>

/* Initialize grid geometry for the given screen dimensions.
 * Call once at desktop startup. */
void yantra_grid_init(uint32_t screen_w, uint32_t screen_h);

/* Render grid overlay to the framebuffer.
 * Call after wallpaper, before windows.
 * @fb: framebuffer pixel array (ARGB32)
 * @fb_pitch: bytes per row
 * @opacity: 0-255, typically 13 (~5%) for dark theme, 10 (~4%) for light */
void yantra_grid_draw(volatile uint32_t *fb, uint32_t fb_pitch,
                       uint32_t fb_w, uint32_t fb_h, uint8_t opacity);

/* Change opacity at runtime (e.g. from Settings or night mode). */
void yantra_grid_set_opacity(uint8_t opacity);

/* Enable/disable without losing opacity setting. */
void yantra_grid_set_enabled(int enabled);

/* Query current state. */
int yantra_grid_is_enabled(void);

#endif /* NEOLYX_YANTRA_GRID_H */
