/*
 * NeolyxOS Theme OSD (On-Screen Display)
 * 
 * Floating indicator for theme/appearance changes.
 * Shows "Dark Mode On/Off" with moon/sun icon.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/theme_osd.h"
#include "../include/nxsyscall.h"

/* OSD State */
static theme_osd_state_t g_theme_osd = {0};
static int g_theme_osd_initialized = 0;

/* Configuration */
#define THEME_OSD_WIDTH       200
#define THEME_OSD_HEIGHT      50
#define THEME_OSD_DURATION    2000    /* 2 seconds */
#define THEME_OSD_FADE_TIME   500     /* 0.5 second fade */
#define THEME_OSD_RADIUS      25      /* Pill shape */

/* Colors */
#define THEME_BG_COLOR        0xD0252530
#define THEME_TEXT_COLOR      0xFFFFFFFF
#define THEME_MOON_COLOR      0xFF89B4FA  /* Blue moon for dark mode */
#define THEME_SUN_COLOR       0xFFF9E2AF  /* Yellow sun for light mode */

/* External drawing functions */
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);

/* Initialize */
void theme_osd_init(uint32_t screen_w, uint32_t screen_h) {
    g_theme_osd.visible = 0;
    g_theme_osd.dark_mode_on = 1;
    g_theme_osd.show_time = 0;
    g_theme_osd.opacity = 255;
    
    /* Center horizontally, upper third vertically */
    g_theme_osd.width = THEME_OSD_WIDTH;
    g_theme_osd.height = THEME_OSD_HEIGHT;
    g_theme_osd.x = (screen_w - THEME_OSD_WIDTH) / 2;
    g_theme_osd.y = screen_h / 4;
    
    g_theme_osd_initialized = 1;
}

/* Show dark mode indicator */
void theme_osd_show_dark_mode(int enabled) {
    if (!g_theme_osd_initialized) return;
    
    g_theme_osd.dark_mode_on = enabled;
    g_theme_osd.visible = 1;
    g_theme_osd.show_time = nx_gettime();
    g_theme_osd.opacity = 255;
}

/* Hide */
void theme_osd_hide(void) {
    g_theme_osd.visible = 0;
    g_theme_osd.opacity = 0;
}

/* Check visibility */
int theme_osd_is_visible(void) {
    return g_theme_osd.visible;
}

/* Update fade animation */
void theme_osd_tick(void) {
    if (!g_theme_osd.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_theme_osd.show_time;
    
    if (elapsed > THEME_OSD_DURATION) {
        uint64_t fade_elapsed = elapsed - THEME_OSD_DURATION;
        
        if (fade_elapsed >= THEME_OSD_FADE_TIME) {
            g_theme_osd.visible = 0;
            g_theme_osd.opacity = 0;
        } else {
            g_theme_osd.opacity = 255 - (uint8_t)((fade_elapsed * 255) / THEME_OSD_FADE_TIME);
        }
    }
}

/* Draw moon icon (crescent) */
static void draw_moon_icon(int32_t cx, int32_t cy, uint8_t alpha) {
    uint32_t color = ((uint32_t)alpha << 24) | (THEME_MOON_COLOR & 0x00FFFFFF);
    uint32_t bg = ((uint32_t)(alpha * 0xD0 / 255) << 24) | (THEME_BG_COLOR & 0x00FFFFFF);
    
    /* Full moon circle */
    fill_circle(cx, cy, 10, color);
    /* Cutout circle offset to create crescent */
    fill_circle(cx + 5, cy - 3, 8, bg);
}

/* Draw sun icon (circle with rays) */
static void draw_sun_icon(int32_t cx, int32_t cy, uint8_t alpha) {
    uint32_t color = ((uint32_t)alpha << 24) | (THEME_SUN_COLOR & 0x00FFFFFF);
    
    /* Center circle */
    fill_circle(cx, cy, 8, color);
    
    /* Rays (small circles around the center) */
    for (int i = 0; i < 8; i++) {
        int angle = i * 45;
        int dx = 0, dy = 0;
        switch (i) {
            case 0: dx = 0;  dy = -12; break;  /* N */
            case 1: dx = 8;  dy = -8;  break;  /* NE */
            case 2: dx = 12; dy = 0;   break;  /* E */
            case 3: dx = 8;  dy = 8;   break;  /* SE */
            case 4: dx = 0;  dy = 12;  break;  /* S */
            case 5: dx = -8; dy = 8;   break;  /* SW */
            case 6: dx = -12; dy = 0;  break;  /* W */
            case 7: dx = -8; dy = -8;  break;  /* NW */
        }
        (void)angle;
        fill_circle(cx + dx, cy + dy, 2, color);
    }
}

/* Render */
void theme_osd_render(uint32_t *fb, uint32_t pitch) {
    if (!g_theme_osd.visible || g_theme_osd.opacity == 0) return;
    
    (void)fb;
    (void)pitch;
    
    uint8_t a = g_theme_osd.opacity;
    int32_t x = g_theme_osd.x;
    int32_t y = g_theme_osd.y;
    uint32_t w = g_theme_osd.width;
    uint32_t h = g_theme_osd.height;
    
    /* Background pill */
    uint32_t bg = ((uint32_t)((a * 0xD0) / 255) << 24) | (THEME_BG_COLOR & 0x00FFFFFF);
    fill_rounded_rect(x, y, w, h, THEME_OSD_RADIUS, bg);
    
    /* Icon */
    int icon_cx = x + 30;
    int icon_cy = y + h / 2;
    
    if (g_theme_osd.dark_mode_on) {
        draw_moon_icon(icon_cx, icon_cy, a);
    } else {
        draw_sun_icon(icon_cx, icon_cy, a);
    }
    
    /* Text */
    uint32_t text_color = ((uint32_t)a << 24) | (THEME_TEXT_COLOR & 0x00FFFFFF);
    const char *text = g_theme_osd.dark_mode_on ? "Dark Mode On" : "Dark Mode Off";
    desktop_draw_text(x + 52, y + 18, text, text_color);
}
