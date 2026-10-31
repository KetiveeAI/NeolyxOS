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
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxsyscall.h"

/* OSD State */
static theme_osd_state_t g_theme_osd = {0};
static int g_theme_osd_initialized = 0;

/* Configuration */
#define THEME_OSD_WIDTH       240
#define THEME_OSD_HEIGHT      56
#define THEME_OSD_DURATION    2000    /* 2 seconds */
#define THEME_OSD_FADE_TIME   500     /* 0.5 second fade */
#define THEME_OSD_RADIUS      25      /* Pill shape */

/* Colors */
#define THEME_BG_COLOR        0xD0252530
#define THEME_TEXT_COLOR      0xFFFFFFFF
#define THEME_MOON_COLOR      0xFF89B4FA  /* Blue moon for dark mode */
#define THEME_SUN_COLOR       0xFFF9E2AF  /* Yellow sun for light mode */

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

/* ============ Canvas renderer ============ */

/* Appearance HUD: sun or moon on ultrathin glass (cached). */
void theme_osd_render(nxc_canvas_t *c) {
    if (!c || !g_theme_osd.visible || g_theme_osd.opacity == 0) return;
    nxc_rect_t box = nxc_rect((float)g_theme_osd.x, (float)g_theme_osd.y, (float)g_theme_osd.width, (float)g_theme_osd.height);
    const nxglass_tokens_t *t = nxglass_tokens();
    nxglass_hud(c, box, g_theme_osd.dark_mode_on ? NXSYM_MOON : NXSYM_SUN, 1.0f, "Appearance",
                g_theme_osd.dark_mode_on ? "Dark" : "Light", -1.0f,
                g_theme_osd.dark_mode_on ? t->info : t->warning, g_theme_osd.opacity);
}
