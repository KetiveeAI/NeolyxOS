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
#include "../include/nxsyscall.h"

/* OSD State */
static volume_osd_state_t g_osd = {0};
static int g_osd_initialized = 0;

/* OSD Configuration */
#define OSD_WIDTH       200
#define OSD_HEIGHT      60
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

/* External drawing functions from desktop_shell.c */
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);

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

/* Draw speaker icon */
static void draw_speaker_icon(int32_t x, int32_t y, int muted, uint8_t alpha) {
    uint32_t color = (alpha << 24) | (OSD_ICON_COLOR & 0x00FFFFFF);
    
    /* Speaker body */
    fill_rounded_rect(x, y + 6, 8, 12, 2, color);
    
    /* Speaker cone */
    fill_rounded_rect(x + 8, y + 4, 6, 16, 1, color);
    
    if (muted) {
        /* X mark for muted */
        uint32_t red = (alpha << 24) | 0x00FF5555;
        for (int i = 0; i < 8; i++) {
            fill_rounded_rect(x + 18 + i, y + 6 + i, 3, 3, 1, red);
            fill_rounded_rect(x + 26 - i, y + 6 + i, 3, 3, 1, red);
        }
    } else {
        /* Sound waves */
        for (int wave = 0; wave < 3; wave++) {
            int woff = 18 + wave * 5;
            int wh = 8 + wave * 4;
            uint32_t wcolor = ((alpha * (3 - wave) / 3) << 24) | (OSD_ICON_COLOR & 0x00FFFFFF);
            fill_rounded_rect(x + woff, y + 12 - wh/2, 2, wh, 1, wcolor);
        }
    }
}

/* Render volume OSD */
void volume_osd_render(uint32_t *fb, uint32_t pitch) {
    if (!g_osd.visible || g_osd.opacity == 0) return;
    
    (void)fb;
    (void)pitch;
    
    uint8_t a = g_osd.opacity;
    int32_t x = g_osd.x;
    int32_t y = g_osd.y;
    uint32_t w = g_osd.width;
    uint32_t h = g_osd.height;
    
    /* Background with current opacity */
    uint32_t bg = ((uint32_t)((a * 0xD0) / 255) << 24) | (OSD_BG_COLOR & 0x00FFFFFF);
    fill_rounded_rect(x, y, w, h, OSD_CORNER_RADIUS, bg);
    
    /* Speaker icon */
    draw_speaker_icon(x + 16, y + 18, g_osd.muted, a);
    
    /* Volume bar background */
    int32_t bar_x = x + 56;
    int32_t bar_y = y + 24;
    uint32_t bar_w = w - 72;
    uint32_t bar_h = 12;
    uint32_t bar_bg = (a << 24) | (OSD_BAR_BG & 0x00FFFFFF);
    fill_rounded_rect(bar_x, bar_y, bar_w, bar_h, 4, bar_bg);
    
    /* Volume bar fill */
    if (!g_osd.muted && g_osd.volume > 0) {
        uint32_t fill_w = (bar_w * g_osd.volume) / 100;
        if (fill_w < 8) fill_w = 8;
        uint32_t bar_fill = (a << 24) | (OSD_BAR_FILL & 0x00FFFFFF);
        fill_rounded_rect(bar_x, bar_y, fill_w, bar_h, 4, bar_fill);
    }
    
    /* Volume percentage text */
    char vol_str[8];
    int pos = 0;
    if (g_osd.muted) {
        vol_str[0] = 'M'; vol_str[1] = 'U'; vol_str[2] = 'T'; vol_str[3] = 'E';
        vol_str[4] = '\0';
    } else {
        int v = g_osd.volume;
        if (v >= 100) { vol_str[pos++] = '1'; vol_str[pos++] = '0'; vol_str[pos++] = '0'; }
        else if (v >= 10) { vol_str[pos++] = '0' + (v / 10); vol_str[pos++] = '0' + (v % 10); }
        else { vol_str[pos++] = '0' + v; }
        vol_str[pos++] = '%';
        vol_str[pos] = '\0';
    }
    
    uint32_t text_color = (a << 24) | (OSD_TEXT_COLOR & 0x00FFFFFF);
    desktop_draw_text(bar_x + (bar_w / 2) - 16, bar_y + 2, vol_str, text_color);
}

/* Get current OSD state */
volume_osd_state_t volume_osd_get_state(void) {
    return g_osd;
}
