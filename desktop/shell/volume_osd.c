/*
 * NeolyxOS Volume OSD (On-Screen Display)
 * 
 * Floating volume indicator that appears when volume changes.
 * Displays for 3 seconds then fades out.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/volume_osd.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxsyscall.h"

/* OSD State */
static volume_osd_state_t g_osd = {0};
static int g_osd_initialized = 0;

/* OSD Configuration */
#define OSD_WIDTH       240
#define OSD_HEIGHT      64
#define OSD_MARGIN      40
#define OSD_FADE_START  2500    /* Start fading after 2.5s */
#define OSD_FADE_END    3000    /* Fully hidden after 3s */
#define OSD_CORNER_RADIUS 12

/* Colors */
#define OSD_BG_COLOR    0xD0202030  /* Dark with 80% opacity */
#define OSD_BAR_BG      0xFF404050  /* Darker bar background */
#define OSD_BAR_FILL    0xFF4A90D9  /* Blue accent */
#define OSD_TEXT_COLOR  0xFFFFFFFF  /* White */
#define OSD_ICON_COLOR  0xFFCCCCCC  /* Light gray */

/* Initialize volume OSD */
void volume_osd_init(uint32_t screen_w, uint32_t screen_h) {
    g_osd.visible = 0;
    g_osd.volume = 50;
    g_osd.muted = 0;
    g_osd.show_time = 0;
    g_osd.opacity = 255;
    
    /* Position at bottom center */
    g_osd.x = (screen_w - OSD_WIDTH) / 2;
    g_osd.y = screen_h - OSD_HEIGHT - OSD_MARGIN - 80; /* Above dock */
    g_osd.width = OSD_WIDTH;
    g_osd.height = OSD_HEIGHT;
    
    g_osd_initialized = 1;
}

/* Show volume OSD with current volume level */
void volume_osd_show(int volume, int muted) {
    if (!g_osd_initialized) return;
    
    /* Clamp volume 0-100 */
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    g_osd.volume = volume;
    g_osd.muted = muted;
    g_osd.visible = 1;
    g_osd.show_time = nx_gettime();
    g_osd.opacity = 255;
}

/* Hide volume OSD immediately */
void volume_osd_hide(void) {
    g_osd.visible = 0;
    g_osd.opacity = 0;
}

/* Check if OSD is visible */
int volume_osd_is_visible(void) {
    return g_osd.visible;
}

/* Update OSD state (call each frame) */
void volume_osd_tick(void) {
    if (!g_osd.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_osd.show_time;
    
    /* Start fading after 2.5 seconds */
    if (elapsed > OSD_FADE_START) {
        uint64_t fade_elapsed = elapsed - OSD_FADE_START;
        uint64_t fade_duration = OSD_FADE_END - OSD_FADE_START;
        
        if (fade_elapsed >= fade_duration) {
            /* Fully faded, hide */
            g_osd.visible = 0;
            g_osd.opacity = 0;
        } else {
            /* Calculate fade opacity (255 -> 0) */
            g_osd.opacity = 255 - (uint8_t)((fade_elapsed * 255) / fade_duration);
        }
    }
}

/* Get current OSD state */
volume_osd_state_t volume_osd_get_state(void) {
    return g_osd;
}

/* ============ Canvas renderer ============ */

/* Volume HUD above the dock: ultrathin glass (cached), level bar. */
void volume_osd_render(nxc_canvas_t *c) {
    if (!c || !g_osd.visible || g_osd.opacity == 0) return;
    nxc_rect_t box = nxc_rect((float)g_osd.x, (float)g_osd.y, (float)g_osd.width, (float)g_osd.height);
    int v = g_osd.volume < 0 ? 0 : (g_osd.volume > 100 ? 100 : g_osd.volume);
    char pct[8];
    int n = 0;
    if (v >= 100) pct[n++] = '1';
    if (v >= 10) pct[n++] = (char)('0' + (v / 10) % 10);
    pct[n++] = (char)('0' + v % 10);
    pct[n++] = '%';
    pct[n] = '\0';
    nxglass_hud(c, box, g_osd.muted ? NXSYM_VOLUME_MUTE : NXSYM_VOLUME, (float)v / 100.0f,
                g_osd.muted ? "Muted" : "Volume", g_osd.muted ? 0 : pct,
                g_osd.muted ? 0.0f : (float)v / 100.0f, 0, g_osd.opacity);
}
