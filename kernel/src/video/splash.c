/*
 * NeolyxOS Kernel - Boot Splash Screen
 * 
 * Professional boot screen with logo and progress indicator
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* External console functions */
extern void console_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void console_draw_filled_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                                    uint32_t fill_color, uint32_t border_color);
extern uint32_t console_get_width(void);
extern uint32_t console_get_height(void);

/* Colors */
#define SPLASH_BG        0x00101020   /* Very dark blue */
#define SPLASH_ACCENT    0x0000B4D8   /* Cyan accent */
#define SPLASH_TEXT      0x00FFFFFF   /* White */
#define SPLASH_PROGRESS  0x0000D4A8   /* Teal progress */
#define SPLASH_BOX_BG    0x00202040   /* Box background */
#define SPLASH_BOX_BORDER 0x00404080  /* Box border */

/* State */
static volatile uint32_t *splash_fb = 0;
static uint32_t splash_width = 0;
static uint32_t splash_height = 0;
static uint32_t splash_pitch = 0;
static uint32_t current_progress = 0;

/* Forward declarations for font (from font.h) */
#define FONT_WIDTH  8
#define FONT_HEIGHT 16

/* Put pixel directly to framebuffer */
static void splash_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (splash_fb && x < splash_width && y < splash_height) {
        splash_fb[y * (splash_pitch / 4) + x] = color;
    }
}

static void splash_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            splash_put_pixel(x + i, y + j, color);
        }
    }
}

/* Draw NeolyxOS text logo with simple pixel art */
static void splash_draw_logo_text(uint32_t center_x, uint32_t y) {
    /* "NEOLYXOS" in large block letters (simplified) */
    /* Each letter is approximately 20 pixels wide, 24 pixels tall */
    
    uint32_t logo_width = 200;  /* Approximate total width */
    uint32_t x = center_x - logo_width / 2;
    uint32_t letter_w = 20;
    uint32_t letter_h = 28;
    uint32_t spacing = 4;
    
    uint32_t color = SPLASH_ACCENT;
    
    /* N */
    splash_fill_rect(x, y, 4, letter_h, color);
    splash_fill_rect(x + 16, y, 4, letter_h, color);
    for (int i = 0; i < 6; i++) {
        splash_fill_rect(x + 4 + i * 2, y + 4 + i * 4, 4, 4, color);
    }
    x += letter_w + spacing;
    
    /* E */
    splash_fill_rect(x, y, 4, letter_h, color);
    splash_fill_rect(x, y, 16, 4, color);
    splash_fill_rect(x, y + 12, 12, 4, color);
    splash_fill_rect(x, y + letter_h - 4, 16, 4, color);
    x += letter_w + spacing;
    
    /* O */
    splash_fill_rect(x + 4, y, 12, 4, color);
    splash_fill_rect(x + 4, y + letter_h - 4, 12, 4, color);
    splash_fill_rect(x, y + 4, 4, letter_h - 8, color);
    splash_fill_rect(x + 16, y + 4, 4, letter_h - 8, color);
    x += letter_w + spacing;
    
    /* L */
    splash_fill_rect(x, y, 4, letter_h, color);
    splash_fill_rect(x, y + letter_h - 4, 16, 4, color);
    x += letter_w + spacing;
    
    /* Y */
    splash_fill_rect(x, y, 4, 12, color);
    splash_fill_rect(x + 16, y, 4, 12, color);
    splash_fill_rect(x + 8, y + 10, 4, letter_h - 10, color);
    splash_fill_rect(x + 4, y + 8, 12, 4, color);
    x += letter_w + spacing;
    
    /* X */
    for (int i = 0; i < 7; i++) {
        splash_fill_rect(x + i * 2, y + i * 4, 4, 4, color);
        splash_fill_rect(x + 16 - i * 2, y + i * 4, 4, 4, color);
    }
    x += letter_w + spacing;
    
    /* O */
    splash_fill_rect(x + 4, y, 12, 4, color);
    splash_fill_rect(x + 4, y + letter_h - 4, 12, 4, color);
    splash_fill_rect(x, y + 4, 4, letter_h - 8, color);
    splash_fill_rect(x + 16, y + 4, 4, letter_h - 8, color);
    x += letter_w + spacing;
    
    /* S */
    splash_fill_rect(x + 4, y, 12, 4, color);
    splash_fill_rect(x, y + 4, 4, 8, color);
    splash_fill_rect(x + 4, y + 12, 12, 4, color);
    splash_fill_rect(x + 16, y + 16, 4, 8, color);
    splash_fill_rect(x, y + letter_h - 4, 12, 4, color);
}

/* Draw progress bar */
static void splash_draw_progress(uint32_t percent) {
    uint32_t bar_width = 300;
    uint32_t bar_height = 12;
    uint32_t x = (splash_width - bar_width) / 2;
    uint32_t y = splash_height / 2 + 80;
    
    /* Background */
    splash_fill_rect(x - 2, y - 2, bar_width + 4, bar_height + 4, SPLASH_BOX_BORDER);
    splash_fill_rect(x, y, bar_width, bar_height, SPLASH_BOX_BG);
    
    /* Progress */
    uint32_t progress_width = (bar_width * percent) / 100;
    if (progress_width > 0) {
        splash_fill_rect(x, y, progress_width, bar_height, SPLASH_PROGRESS);
    }
    
    current_progress = percent;
}

/* ============ Public API ============ */

void splash_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    splash_fb = (volatile uint32_t*)fb_addr;
    splash_width = width;
    splash_height = height;
    splash_pitch = pitch;
    current_progress = 0;
}

void splash_show(void) {
    if (!splash_fb) return;
    
    /* Clear to background color */
    splash_fill_rect(0, 0, splash_width, splash_height, SPLASH_BG);
    
    /* Draw decorative top bar */
    splash_fill_rect(0, 0, splash_width, 3, SPLASH_ACCENT);
    
    /* Draw logo in center */
    uint32_t logo_y = splash_height / 2 - 50;
    splash_draw_logo_text(splash_width / 2, logo_y);
    
    /* Draw "Loading..." text area below logo */
    uint32_t status_box_w = 320;
    uint32_t status_box_h = 28;
    uint32_t status_x = (splash_width - status_box_w) / 2;
    uint32_t status_y = splash_height / 2 + 30;
    
    splash_fill_rect(status_x, status_y, status_box_w, status_box_h, SPLASH_BOX_BG);
    splash_fill_rect(status_x, status_y, status_box_w, 2, SPLASH_BOX_BORDER);
    splash_fill_rect(status_x, status_y + status_box_h - 2, status_box_w, 2, SPLASH_BOX_BORDER);
    
    /* Draw initial progress bar at 0% */
    splash_draw_progress(0);
}

void splash_update(uint32_t percent, const char *status) {
    if (!splash_fb) return;
    
    /* Update progress bar */
    splash_draw_progress(percent);
    
    /* Clear status area */
    uint32_t status_box_w = 320;
    uint32_t status_x = (splash_width - status_box_w) / 2;
    uint32_t status_y = splash_height / 2 + 30;
    splash_fill_rect(status_x + 8, status_y + 6, status_box_w - 16, 16, SPLASH_BOX_BG);
    
    /* Note: Status text would need font rendering, which is in console.c
     * For now, just update the progress bar visually */
    (void)status;  /* Suppress unused warning */
}

void splash_hide(void) {
    if (!splash_fb) return;
    
    /* Clear screen */
    splash_fill_rect(0, 0, splash_width, splash_height, 0x00101020);
}

uint32_t splash_get_progress(void) {
    return current_progress;
}
