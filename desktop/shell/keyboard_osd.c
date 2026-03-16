/*
 * NeolyxOS Keyboard OSD (On-Screen Display)
 * 
 * Shows keyboard lock indicators (Caps Lock, Num Lock)
 * and keyboard shortcut hints.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/keyboard_osd.h"
#include "../include/nxsyscall.h"

/* OSD State */
static keyboard_osd_state_t g_kbd_osd = {0};
static int g_kbd_osd_initialized = 0;

/* Configuration */
#define KBD_OSD_WIDTH       180
#define KBD_OSD_HEIGHT      50
#define KBD_OSD_MARGIN      20
#define KBD_OSD_DURATION    2000    /* 2 seconds */
#define KBD_OSD_FADE_TIME   500     /* 0.5 second fade */
#define KBD_OSD_RADIUS      10

/* Colors */
#define KBD_BG_COLOR        0xD0252530
#define KBD_TEXT_COLOR      0xFFFFFFFF
#define KBD_ICON_ON         0xFF4AE04A  /* Green for ON */
#define KBD_ICON_OFF        0xFF505050  /* Gray for OFF */

/* External drawing functions */
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);

/* Safe string copy */
static void kbd_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Initialize keyboard OSD */
void keyboard_osd_init(uint32_t screen_w, uint32_t screen_h) {
    g_kbd_osd.visible = 0;
    g_kbd_osd.type = KBD_OSD_NONE;
    g_kbd_osd.text[0] = '\0';
    g_kbd_osd.show_time = 0;
    g_kbd_osd.opacity = 255;
    
    /* Position at top right */
    g_kbd_osd.x = screen_w - KBD_OSD_WIDTH - KBD_OSD_MARGIN;
    g_kbd_osd.y = 40 + KBD_OSD_MARGIN;  /* Below nav bar */
    g_kbd_osd.width = KBD_OSD_WIDTH;
    g_kbd_osd.height = KBD_OSD_HEIGHT;
    
    g_kbd_osd_initialized = 1;
}

/* Show Caps Lock indicator */
void keyboard_osd_caps_lock(int on) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_CAPS_LOCK;
    kbd_strcpy(g_kbd_osd.text, on ? "CAPS LOCK ON" : "CAPS LOCK OFF", 64);
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Show Num Lock indicator */
void keyboard_osd_num_lock(int on) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_NUM_LOCK;
    kbd_strcpy(g_kbd_osd.text, on ? "NUM LOCK ON" : "NUM LOCK OFF", 64);
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Show keyboard shortcut hint */
void keyboard_osd_shortcut(const char *keys, const char *action) {
    if (!g_kbd_osd_initialized) return;
    
    g_kbd_osd.visible = 1;
    g_kbd_osd.type = KBD_OSD_SHORTCUT;
    
    /* Build shortcut string */
    char *dst = g_kbd_osd.text;
    int pos = 0;
    
    /* Keys */
    if (keys) {
        while (keys[pos] && pos < 30) {
            dst[pos] = keys[pos];
            pos++;
        }
    }
    
    /* Separator */
    if (action && pos < 60) {
        dst[pos++] = ' ';
        dst[pos++] = '-';
        dst[pos++] = ' ';
        
        /* Action */
        int j = 0;
        while (action[j] && pos < 63) {
            dst[pos++] = action[j++];
        }
    }
    dst[pos] = '\0';
    
    g_kbd_osd.show_time = nx_gettime();
    g_kbd_osd.opacity = 255;
}

/* Hide keyboard OSD */
void keyboard_osd_hide(void) {
    g_kbd_osd.visible = 0;
    g_kbd_osd.opacity = 0;
}

/* Check if visible */
int keyboard_osd_is_visible(void) {
    return g_kbd_osd.visible;
}

/* Update state (call each frame) */
void keyboard_osd_tick(void) {
    if (!g_kbd_osd.visible) return;
    
    uint64_t now = nx_gettime();
    uint64_t elapsed = now - g_kbd_osd.show_time;
    
    if (elapsed > KBD_OSD_DURATION) {
        uint64_t fade_elapsed = elapsed - KBD_OSD_DURATION;
        
        if (fade_elapsed >= KBD_OSD_FADE_TIME) {
            g_kbd_osd.visible = 0;
            g_kbd_osd.opacity = 0;
        } else {
            g_kbd_osd.opacity = 255 - (uint8_t)((fade_elapsed * 255) / KBD_OSD_FADE_TIME);
        }
    }
}

/* Render keyboard OSD */
void keyboard_osd_render(uint32_t *fb, uint32_t pitch) {
    if (!g_kbd_osd.visible || g_kbd_osd.opacity == 0) return;
    
    (void)fb;
    (void)pitch;
    
    uint8_t a = g_kbd_osd.opacity;
    int32_t x = g_kbd_osd.x;
    int32_t y = g_kbd_osd.y;
    uint32_t w = g_kbd_osd.width;
    uint32_t h = g_kbd_osd.height;
    
    /* Background */
    uint32_t bg = ((uint32_t)((a * 0xD0) / 255) << 24) | (KBD_BG_COLOR & 0x00FFFFFF);
    fill_rounded_rect(x, y, w, h, KBD_OSD_RADIUS, bg);
    
    /* Icon based on type */
    int icon_x = x + 16;
    int icon_y = y + 17;
    
    if (g_kbd_osd.type == KBD_OSD_CAPS_LOCK) {
        /* Caps Lock icon - A with up arrow */
        uint32_t icon_color;
        if (g_kbd_osd.text[10] == 'N') {  /* "ON" */
            icon_color = (a << 24) | (KBD_ICON_ON & 0x00FFFFFF);
        } else {
            icon_color = (a << 24) | (KBD_ICON_OFF & 0x00FFFFFF);
        }
        fill_rounded_rect(icon_x, icon_y, 16, 16, 3, icon_color);
        fill_rounded_rect(icon_x + 4, icon_y - 4, 8, 8, 2, icon_color);
    } else if (g_kbd_osd.type == KBD_OSD_NUM_LOCK) {
        /* Num Lock icon - # symbol */
        uint32_t icon_color;
        if (g_kbd_osd.text[9] == 'N') {  /* "ON" */
            icon_color = (a << 24) | (KBD_ICON_ON & 0x00FFFFFF);
        } else {
            icon_color = (a << 24) | (KBD_ICON_OFF & 0x00FFFFFF);
        }
        fill_rounded_rect(icon_x + 2, icon_y, 12, 16, 2, icon_color);
    } else {
        /* Shortcut - keyboard icon */
        uint32_t kbd_color = (a << 24) | 0x00808090;
        fill_rounded_rect(icon_x, icon_y + 2, 18, 12, 2, kbd_color);
        /* Keys */
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 3; c++) {
                fill_rounded_rect(icon_x + 2 + c * 5, icon_y + 4 + r * 4, 4, 3, 1, 
                                 (a << 24) | 0x00FFFFFF);
            }
        }
    }
    
    /* Text */
    uint32_t text_color = (a << 24) | (KBD_TEXT_COLOR & 0x00FFFFFF);
    desktop_draw_text(x + 40, y + 18, g_kbd_osd.text, text_color);
}

/* ============ Help Overlay (Full-Screen Shortcut Help) ============ */

/* Help overlay state */
static int g_help_visible = 0;
static uint32_t g_help_screen_w = 0;
static uint32_t g_help_screen_h = 0;

/* Shortcut definitions */
typedef struct {
    const char *keys;
    const char *action;
} shortcut_entry_t;

static const shortcut_entry_t g_shortcuts[] = {
    {"NX", "App Drawer"},
    {"NX + Space", "Anveshan Search"},
    {"NX + Tab", "Switch Windows"},
    {"Caps Lock", "Toggle Caps Lock"},
    {"Num Lock", "Toggle Num Lock"},
    {"Ctrl + C", "Copy"},
    {"Ctrl + V", "Paste"},
    {"Ctrl + X", "Cut"},
    {"Ctrl + Z", "Undo"},
    {"Ctrl + S", "Save"},
    {"Ctrl + A", "Select All"},
    {"Alt + F4", "Close Window"},
    {NULL, NULL}
};

void keyboard_osd_help_show(void) {
    g_help_visible = 1;
    g_help_screen_w = g_kbd_osd.width > 100 ? 1920 : 1920;  /* Use stored screen size */
    g_help_screen_h = 1080;
}

void keyboard_osd_help_hide(void) {
    g_help_visible = 0;
}

int keyboard_osd_help_visible(void) {
    return g_help_visible;
}

/* Render help overlay - called from desktop render loop */
void keyboard_osd_help_render(uint32_t *fb, uint32_t pitch, uint32_t sw, uint32_t sh) {
    if (!g_help_visible) return;
    
    /* Semi-transparent full-screen background */
    uint32_t bg_color = 0xD0101020;
    for (uint32_t y = 0; y < sh; y++) {
        for (uint32_t x = 0; x < sw; x++) {
            uint32_t idx = y * (pitch / 4) + x;
            uint32_t bg = fb[idx];
            /* Blend */
            uint8_t a = (bg_color >> 24) & 0xFF;
            uint8_t r = ((((bg_color >> 16) & 0xFF) * a) + (((bg >> 16) & 0xFF) * (255 - a))) / 255;
            uint8_t g = ((((bg_color >> 8) & 0xFF) * a) + (((bg >> 8) & 0xFF) * (255 - a))) / 255;
            uint8_t b = (((bg_color & 0xFF) * a) + ((bg & 0xFF) * (255 - a))) / 255;
            fb[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    
    /* Panel in center */
    int panel_w = 400;
    int panel_h = 360;
    int panel_x = (sw - panel_w) / 2;
    int panel_y = (sh - panel_h) / 2;
    
    fill_rounded_rect(panel_x, panel_y, panel_w, panel_h, 16, 0xE8252535);
    
    /* Title */
    desktop_draw_text(panel_x + 16, panel_y + 16, "Keyboard Shortcuts", 0xFFFFFFFF);
    
    /* Shortcut list */
    int row_y = panel_y + 50;
    int row_h = 24;
    int key_x = panel_x + 20;
    int action_x = panel_x + 180;
    
    for (int i = 0; g_shortcuts[i].keys != NULL; i++) {
        /* Key combination */
        fill_rounded_rect(key_x, row_y, 140, 20, 4, 0x60404050);
        desktop_draw_text(key_x + 8, row_y + 4, g_shortcuts[i].keys, 0xFF89B4FA);
        
        /* Action description */
        desktop_draw_text(action_x, row_y + 4, g_shortcuts[i].action, 0xFFCDD6F4);
        
        row_y += row_h;
        if (row_y > panel_y + panel_h - 40) break;  /* Don't overflow panel */
    }
    
    /* Footer hint */
    desktop_draw_text(panel_x + 16, panel_y + panel_h - 24, "Release NX to close", 0x80FFFFFF);
}
