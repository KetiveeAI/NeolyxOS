/*
 * NeolyxOS Battery Warning Popup
 * 
 * Modal warning when battery falls below thresholds.
 * Shows at 20% (yellow), 10% (orange), 5% (red).
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/battery_warning.h"
#include "../include/nxsyscall.h"

/* Warning State */
static battery_warning_state_t g_bw = {0};
static int g_bw_initialized = 0;

/* Configuration */
#define BW_WIDTH        320
#define BW_HEIGHT       120
#define BW_DURATION     8000    /* 8 seconds */
#define BW_FADE_TIME    500     /* 0.5 second fade */
#define BW_RADIUS       16

/* Colors */
#define BW_BG_COLOR     0xE8181820
#define BW_TEXT_COLOR   0xFFFFFFFF
#define BW_YELLOW       0xFFF9E2AF  /* 20% warning */
#define BW_ORANGE       0xFFFFA500  /* 10% warning */
#define BW_RED          0xFFFF453A  /* 5% urgent */

/* External drawing functions */
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);

/* Initialize */
void battery_warning_init(uint32_t screen_w, uint32_t screen_h) {
    g_bw.visible = 0;
    g_bw.battery_percent = 100;
    g_bw.is_charging = 0;
    g_bw.show_time = 0;
    g_bw.opacity = 255;
    
    /* Center on screen */
    g_bw.width = BW_WIDTH;
    g_bw.height = BW_HEIGHT;
    g_bw.x = (screen_w - BW_WIDTH) / 2;
    g_bw.y = (screen_h - BW_HEIGHT) / 2;
    
    g_bw.warned_20 = 0;
    g_bw.warned_10 = 0;
    g_bw.warned_5 = 0;
    
    g_bw_initialized = 1;
}

/* Show warning popup */
static void show_warning(int percent) {
    g_bw.battery_percent = percent;
    g_bw.visible = 1;
    g_bw.show_time = nx_gettime();
    g_bw.opacity = 255;
}

/* Check battery and trigger warnings at thresholds */
void battery_warning_check(int battery_percent, int is_charging) {
    if (!g_bw_initialized) return;
    
    g_bw.is_charging = is_charging;
    
    /* Reset warnings when charging */
    if (is_charging) {
        g_bw.warned_20 = 0;
        g_bw.warned_10 = 0;
        g_bw.warned_5 = 0;
        return;
    }
    
    /* Check thresholds */
    if (battery_percent <= BATTERY_URGENT_THRESHOLD && !g_bw.warned_5) {
        show_warning(battery_percent);
        g_bw.warned_5 = 1;
    } else if (battery_percent <= BATTERY_CRITICAL_THRESHOLD && !g_bw.warned_10) {
        show_warning(battery_percent);
        g_bw.warned_10 = 1;
    } else if (battery_percent <= BATTERY_LOW_THRESHOLD && !g_bw.warned_20) {
        show_warning(battery_percent);
        g_bw.warned_20 = 1;
    }
}

/* Dismiss popup */
void battery_warning_dismiss(void) {
    g_bw.visible = 0;
    g_bw.opacity = 0;
}

/* Check visibility */
int battery_warning_is_visible(void) {
    return g_bw.visible;
}

/* Reset warning flags */
void battery_warning_reset(void) {
    g_bw.warned_20 = 0;
    g_bw.warned_10 = 0;
    g_bw.warned_5 = 0;
}

/* Update fade animation */
void battery_warning_tick(void) {
    if (!g_bw.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_bw.show_time;
    
    if (elapsed > BW_DURATION) {
        uint64_t fade_elapsed = elapsed - BW_DURATION;
        
        if (fade_elapsed >= BW_FADE_TIME) {
            g_bw.visible = 0;
            g_bw.opacity = 0;
        } else {
            g_bw.opacity = 255 - (uint8_t)((fade_elapsed * 255) / BW_FADE_TIME);
        }
    }
}

/* Draw battery icon */
static void draw_battery_icon(int32_t x, int32_t y, uint8_t alpha, int percent) {
    uint32_t outline = ((uint32_t)alpha << 24) | 0x00FFFFFF;
    
    /* Battery body outline */
    fill_rounded_rect(x, y, 40, 20, 4, outline);
    fill_rounded_rect(x + 2, y + 2, 36, 16, 3, (alpha << 24) | 0x00181820);
    
    /* Battery tip */
    fill_rounded_rect(x + 40, y + 6, 4, 8, 2, outline);
    
    /* Fill based on percent */
    uint32_t fill_color;
    if (percent <= BATTERY_URGENT_THRESHOLD) {
        fill_color = BW_RED;
    } else if (percent <= BATTERY_CRITICAL_THRESHOLD) {
        fill_color = BW_ORANGE;
    } else {
        fill_color = BW_YELLOW;
    }
    fill_color = ((uint32_t)alpha << 24) | (fill_color & 0x00FFFFFF);
    
    int fill_w = (32 * percent) / 100;
    if (fill_w < 4) fill_w = 4;
    fill_rounded_rect(x + 4, y + 4, fill_w, 12, 2, fill_color);
}

/* Render warning popup */
void battery_warning_render(uint32_t *fb, uint32_t pitch) {
    if (!g_bw.visible || g_bw.opacity == 0) return;
    
    (void)fb;
    (void)pitch;
    
    uint8_t a = g_bw.opacity;
    int32_t x = g_bw.x;
    int32_t y = g_bw.y;
    uint32_t w = g_bw.width;
    uint32_t h = g_bw.height;
    
    /* Background */
    uint32_t bg = ((uint32_t)((a * 0xE8) / 255) << 24) | (BW_BG_COLOR & 0x00FFFFFF);
    fill_rounded_rect(x, y, w, h, BW_RADIUS, bg);
    
    /* Warning color based on level */
    uint32_t accent;
    const char *title;
    if (g_bw.battery_percent <= BATTERY_URGENT_THRESHOLD) {
        accent = BW_RED;
        title = "Battery Critically Low";
    } else if (g_bw.battery_percent <= BATTERY_CRITICAL_THRESHOLD) {
        accent = BW_ORANGE;
        title = "Battery Very Low";
    } else {
        accent = BW_YELLOW;
        title = "Low Battery";
    }
    
    /* Accent bar at top */
    uint32_t bar_color = ((uint32_t)a << 24) | (accent & 0x00FFFFFF);
    fill_rounded_rect(x, y, w, 6, BW_RADIUS, bar_color);
    fill_rounded_rect(x, y + 3, w, 3, 0, bar_color);
    
    /* Battery icon */
    draw_battery_icon(x + 20, y + 30, a, g_bw.battery_percent);
    
    /* Title text */
    uint32_t text_color = ((uint32_t)a << 24) | (BW_TEXT_COLOR & 0x00FFFFFF);
    desktop_draw_text(x + 72, y + 32, title, text_color);
    
    /* Percentage and message */
    char pct_str[16];
    int pos = 0;
    int pct = g_bw.battery_percent;
    if (pct >= 10) { pct_str[pos++] = '0' + (pct / 10); }
    pct_str[pos++] = '0' + (pct % 10);
    pct_str[pos++] = '%';
    pct_str[pos++] = ' ';
    pct_str[pos++] = 'r';
    pct_str[pos++] = 'e';
    pct_str[pos++] = 'm';
    pct_str[pos++] = 'a';
    pct_str[pos++] = 'i';
    pct_str[pos++] = 'n';
    pct_str[pos++] = 'i';
    pct_str[pos++] = 'n';
    pct_str[pos++] = 'g';
    pct_str[pos] = '\0';
    
    uint32_t dim_color = ((uint32_t)(a * 0x99 / 255) << 24) | 0x00FFFFFF;
    desktop_draw_text(x + 72, y + 52, pct_str, dim_color);
    
    /* Hint */
    desktop_draw_text(x + 20, y + 88, "Connect to power soon", dim_color);
}
