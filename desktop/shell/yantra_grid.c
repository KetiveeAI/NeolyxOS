/*
 * NeolyxOS - Yantra Grid Overlay Implementation
 *
 * Renders a barely-visible geometric mesh on the desktop background.
 * Grid uses 48px cells with diamond nodes at even intersections and
 * crosshair dots at odd intersections. All geometry is single-pixel
 * lines — no allocations, no blur, no GPU cost after startup.
 *
 * Copyright (c) 2026 KetiveeAI
 */

#include "../include/yantra_grid.h"

/* ============ State ============ */

static uint32_t g_grid_w = 0;
static uint32_t g_grid_h = 0;
static uint8_t  g_grid_opacity = 13;    /* ~5% of 255 */
static int      g_grid_enabled = 1;
static int      g_grid_initialized = 0;

#define GRID_CELL 48  /* px — aligns with DOCK_ICON_SIZE */

/* ============ Init ============ */

void yantra_grid_init(uint32_t screen_w, uint32_t screen_h) {
    g_grid_w = screen_w;
    g_grid_h = screen_h;
    g_grid_initialized = 1;
}

/* ============ Runtime Controls ============ */

void yantra_grid_set_opacity(uint8_t opacity) {
    g_grid_opacity = opacity;
}

void yantra_grid_set_enabled(int enabled) {
    g_grid_enabled = enabled;
}

int yantra_grid_is_enabled(void) {
    return g_grid_enabled && g_grid_initialized;
}

/* ============ Alpha Pixel Helper ============ */

static inline void grid_put_alpha(volatile uint32_t *fb, uint32_t fb_pitch,
                                   int32_t x, int32_t y,
                                   uint32_t fb_w, uint32_t fb_h,
                                   uint8_t alpha) {
    if (x < 0 || x >= (int32_t)fb_w || y < 0 || y >= (int32_t)fb_h) return;

    uint32_t bg = fb[y * (fb_pitch / 4) + x];
    uint8_t br = (bg >> 16) & 0xFF;
    uint8_t bg_ = (bg >> 8) & 0xFF;
    uint8_t bb = bg & 0xFF;

    /* Blend white at given alpha over background */
    uint8_t r = (uint8_t)((0xFF * alpha + br * (255 - alpha)) / 255);
    uint8_t g = (uint8_t)((0xFF * alpha + bg_ * (255 - alpha)) / 255);
    uint8_t b = (uint8_t)((0xFF * alpha + bb * (255 - alpha)) / 255);

    fb[y * (fb_pitch / 4) + x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ============ Draw ============ */

void yantra_grid_draw(volatile uint32_t *fb, uint32_t fb_pitch,
                       uint32_t fb_w, uint32_t fb_h, uint8_t opacity) {
    if (!g_grid_enabled || !g_grid_initialized || opacity == 0) return;

    uint32_t cols = fb_w / GRID_CELL;
    uint32_t rows = fb_h / GRID_CELL;

    /* Horizontal grid lines */
    for (uint32_t row = 1; row < rows; row++) {
        int32_t py = (int32_t)(row * GRID_CELL);
        for (uint32_t x = 0; x < fb_w; x++) {
            grid_put_alpha(fb, fb_pitch, (int32_t)x, py, fb_w, fb_h, opacity);
        }
    }

    /* Vertical grid lines */
    for (uint32_t col = 1; col < cols; col++) {
        int32_t px = (int32_t)(col * GRID_CELL);
        for (uint32_t y = 0; y < fb_h; y++) {
            grid_put_alpha(fb, fb_pitch, px, (int32_t)y, fb_w, fb_h, opacity);
        }
    }

    /* Node decorations at intersections */
    for (uint32_t row = 1; row < rows; row++) {
        for (uint32_t col = 1; col < cols; col++) {
            int32_t cx = (int32_t)(col * GRID_CELL);
            int32_t cy = (int32_t)(row * GRID_CELL);

            uint8_t node_alpha = (uint8_t)(opacity * 2 > 255 ? 255 : opacity * 2);

            if ((row % 2 == 0) && (col % 2 == 0)) {
                /* Diamond node at even-even intersections (4px diamond) */
                grid_put_alpha(fb, fb_pitch, cx,     cy - 2, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx - 1, cy - 1, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx + 1, cy - 1, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx - 2, cy,     fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx + 2, cy,     fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx - 1, cy + 1, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx + 1, cy + 1, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx,     cy + 2, fb_w, fb_h, node_alpha);
            } else if ((row % 2 == 1) && (col % 2 == 1)) {
                /* Crosshair dot at odd-odd intersections (3px cross) */
                grid_put_alpha(fb, fb_pitch, cx,     cy - 1, fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx - 1, cy,     fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx,     cy,     fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx + 1, cy,     fb_w, fb_h, node_alpha);
                grid_put_alpha(fb, fb_pitch, cx,     cy + 1, fb_w, fb_h, node_alpha);
            }
        }
    }
}
