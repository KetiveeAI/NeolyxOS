/*
 * NeolyxOS Recovery Menu
 * 
 * Recovery options menu triggered by F2 key during boot
 * Options: Recover, Disk Utility, Help, Fresh Install
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/core/recovery.h"

/* External keyboard functions */
extern uint8_t kbd_getchar(void);
extern int kbd_check_key(void);

/* Colors */
#define RECOVERY_BG          0x00101020
#define RECOVERY_TITLE_BG    0x00202050
#define RECOVERY_ACCENT      0x0000B4D8
#define RECOVERY_TEXT        0x00FFFFFF
#define RECOVERY_SELECTED_BG 0x00304060
#define RECOVERY_SELECTED_FG 0x0000D4F8
#define RECOVERY_BORDER      0x00404080

/* Key codes (from keyboard driver) */
#define KEY_UP      0x48
#define KEY_DOWN    0x50
#define KEY_ENTER   0x1C
#define KEY_ESC     0x01
#define KEY_F2      0x3C

/* Menu option labels */
static const char *recovery_labels[] = {
    "Continue Boot",
    "Recover NeolyxOS",
    "Disk Utility",
    "Help",
    "Fresh Install"
};

/* Menu option descriptions */
static const char *recovery_descriptions[] = {
    "Continue booting NeolyxOS normally",
    "Attempt to repair system files and configuration",
    "Manage disk partitions and format drives",
    "View help documentation and troubleshooting guide",
    "Reinstall NeolyxOS (WARNING: erases data)"
};

/* ============ Framebuffer Drawing ============ */

static volatile uint32_t *rec_fb = 0;
static uint32_t rec_width = 0;
static uint32_t rec_height = 0;
static uint32_t rec_pitch = 0;

static void rec_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (rec_fb && x < rec_width && y < rec_height) {
        rec_fb[y * (rec_pitch / 4) + x] = color;
    }
}

static void rec_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h && (y + j) < rec_height; j++) {
        for (uint32_t i = 0; i < w && (x + i) < rec_width; i++) {
            rec_put_pixel(x + i, y + j, color);
        }
    }
}

/* Include font for menu drawing */
#include "../video/font.h"
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

static void rec_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font_bitmap[c - 32];
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (bits & (0x80 >> col)) {
                rec_put_pixel(x + col, y + row, fg);
            }
        }
    }
}

static void rec_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        rec_draw_char(x, y, *s, color);
        x += FONT_WIDTH + 1;
        s++;
    }
}

static void rec_draw_string_centered(uint32_t y, const char *s, uint32_t color) {
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    uint32_t x = (rec_width - len * (FONT_WIDTH + 1)) / 2;
    rec_draw_string(x, y, s, color);
}

/* ============ Public API ============ */

void recovery_init(RecoveryMenu *menu, uint64_t fb_addr, 
                   uint32_t width, uint32_t height, uint32_t pitch) {
    menu->selected = 0;
    menu->triggered = 0;
    menu->f2_press_count = 0;
    menu->fb_addr = fb_addr;
    menu->fb_width = width;
    menu->fb_height = height;
    menu->fb_pitch = pitch;
    
    rec_fb = (volatile uint32_t*)fb_addr;
    rec_width = width;
    rec_height = height;
    rec_pitch = pitch;
}

int recovery_check_f2(RecoveryMenu *menu) {
    /* Check keyboard for F2 press */
    if (kbd_check_key()) {
        uint8_t key = kbd_getchar();
        if (key == KEY_F2) {
            menu->f2_press_count++;
            menu->triggered = 1;
            return 1;
        }
    }
    return 0;
}

int recovery_should_show(RecoveryMenu *menu) {
    return menu->triggered;
}

void recovery_draw(RecoveryMenu *menu) {
    if (!rec_fb) return;
    
    /* Clear screen */
    rec_fill_rect(0, 0, rec_width, rec_height, RECOVERY_BG);
    
    /* Title bar */
    rec_fill_rect(0, 0, rec_width, 50, RECOVERY_TITLE_BG);
    rec_fill_rect(0, 48, rec_width, 2, RECOVERY_ACCENT);
    rec_draw_string_centered(16, "NeolyxOS Recovery Menu", RECOVERY_TEXT);
    
    /* Menu box */
    uint32_t box_w = 500;
    uint32_t box_h = 40 + RECOVERY_OPTION_COUNT * 50;
    uint32_t box_x = (rec_width - box_w) / 2;
    uint32_t box_y = 80;
    
    /* Box background */
    rec_fill_rect(box_x, box_y, box_w, box_h, 0x00181830);
    
    /* Box border */
    rec_fill_rect(box_x, box_y, box_w, 2, RECOVERY_BORDER);
    rec_fill_rect(box_x, box_y + box_h - 2, box_w, 2, RECOVERY_BORDER);
    rec_fill_rect(box_x, box_y, 2, box_h, RECOVERY_BORDER);
    rec_fill_rect(box_x + box_w - 2, box_y, 2, box_h, RECOVERY_BORDER);
    
    /* Menu options */
    for (int i = 0; i < RECOVERY_OPTION_COUNT; i++) {
        uint32_t item_y = box_y + 20 + i * 50;
        
        /* Highlight selected */
        if (i == menu->selected) {
            rec_fill_rect(box_x + 10, item_y, box_w - 20, 44, RECOVERY_SELECTED_BG);
            rec_fill_rect(box_x + 10, item_y, 4, 44, RECOVERY_ACCENT);
        }
        
        /* Option text */
        uint32_t text_color = (i == menu->selected) ? RECOVERY_SELECTED_FG : RECOVERY_TEXT;
        rec_draw_string(box_x + 24, item_y + 6, recovery_labels[i], text_color);
        
        /* Description (smaller, dimmer) */
        uint32_t desc_color = (i == menu->selected) ? 0x0080A0B0 : 0x00606080;
        rec_draw_string(box_x + 24, item_y + 24, recovery_descriptions[i], desc_color);
    }
    
    /* Footer instructions */
    uint32_t footer_y = box_y + box_h + 20;
    rec_draw_string_centered(footer_y, "[UP/DOWN] Select   [ENTER] Confirm   [ESC] Cancel", 0x00808080);
    
    /* Warning for Fresh Install */
    if (menu->selected == RECOVERY_OPTION_FRESH_INSTALL) {
        rec_fill_rect(box_x, footer_y + 30, box_w, 24, 0x00802020);
        rec_draw_string_centered(footer_y + 34, "WARNING: Fresh Install will erase all data!", 0x00FFFF00);
    }
}

int recovery_run(RecoveryMenu *menu) {
    recovery_draw(menu);
    
    while (1) {
        /* Wait for key */
        while (!kbd_check_key()) {
            /* Small delay */
            for (volatile int i = 0; i < 10000; i++);
        }
        
        uint8_t key = kbd_getchar();
        
        switch (key) {
            case KEY_UP:
                if (menu->selected > 0) {
                    menu->selected--;
                    recovery_draw(menu);
                }
                break;
                
            case KEY_DOWN:
                if (menu->selected < RECOVERY_OPTION_COUNT - 1) {
                    menu->selected++;
                    recovery_draw(menu);
                }
                break;
                
            case KEY_ENTER:
                return menu->selected;
                
            case KEY_ESC:
                return RECOVERY_OPTION_CONTINUE;
        }
    }
}
