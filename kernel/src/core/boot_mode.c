/*
 * NeolyxOS Boot Mode Manager Implementation
 * 
 * Handles boot mode selection and environment startup.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/boot_mode.h"
#include "ui/desktop.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height;
extern volatile uint32_t *framebuffer;
extern uint8_t kbd_wait_key(void);

/* ============ State ============ */

static boot_mode_t current_mode = BOOT_MODE_INSTALLER;
static int first_boot = 1;

/* ============ Drawing Helpers ============ */

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h && j < (int)fb_height; j++) {
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0 && j >= 0) {
                framebuffer[j * fb_width + i] = color;
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint32_t color) {
    /* Simple 8x8 font rendering - placeholder */
    int cx = x;
    while (*text) {
        /* Draw character placeholder */
        if (*text != ' ') {
            draw_rect(cx, y, 6, 8, color);
        }
        cx += 8;
        text++;
    }
}

static void draw_centered_text(int y, const char *text, uint32_t color) {
    int len = 0;
    const char *p = text;
    while (*p++) len++;
    int x = (fb_width - len * 8) / 2;
    draw_text(x, y, text, color);
}

/* ============ Colors ============ */

#define COLOR_BG        0x1A1A2E
#define COLOR_PRIMARY   0x0F3460
#define COLOR_ACCENT    0xE94560
#define COLOR_TEXT      0xFFFFFF
#define COLOR_SELECTED  0x16213E
#define COLOR_BORDER    0x0F3460

/* ============ Boot Mode Selection Screen ============ */

boot_mode_t boot_mode_selector(void) {
    serial_puts("[BOOT] Showing mode selection screen...\n");
    
    int selected = 0;  /* 0 = Desktop, 1 = Server */
    int done = 0;
    
    while (!done) {
        /* Clear screen */
        draw_rect(0, 0, fb_width, fb_height, COLOR_BG);
        
        /* Title */
        int title_y = fb_height / 4;
        draw_centered_text(title_y, "NeolyxOS Setup", COLOR_TEXT);
        draw_centered_text(title_y + 20, "Select Installation Mode", COLOR_ACCENT);
        
        /* Options */
        int opt_y = fb_height / 2 - 40;
        int opt_w = 300;
        int opt_h = 60;
        int opt_x = (fb_width - opt_w) / 2;
        
        /* Desktop Mode */
        uint32_t desk_bg = (selected == 0) ? COLOR_ACCENT : COLOR_PRIMARY;
        draw_rect(opt_x, opt_y, opt_w, opt_h, desk_bg);
        draw_rect(opt_x + 2, opt_y + 2, opt_w - 4, opt_h - 4, COLOR_SELECTED);
        draw_centered_text(opt_y + 20, "Desktop Mode", COLOR_TEXT);
        draw_centered_text(opt_y + 36, "(Recommended)", COLOR_ACCENT);
        
        /* Server Mode */
        uint32_t serv_bg = (selected == 1) ? COLOR_ACCENT : COLOR_PRIMARY;
        draw_rect(opt_x, opt_y + 80, opt_w, opt_h, serv_bg);
        draw_rect(opt_x + 2, opt_y + 82, opt_w - 4, opt_h - 4, COLOR_SELECTED);
        draw_centered_text(opt_y + 100, "Server Mode", COLOR_TEXT);
        draw_centered_text(opt_y + 116, "(Shell Only)", COLOR_PRIMARY);
        
        /* Instructions */
        draw_centered_text(fb_height - 60, "Use UP/DOWN to select, ENTER to confirm", COLOR_TEXT);
        
        /* Get input */
        uint8_t key = kbd_wait_key();
        
        switch (key) {
            case 0x48:  /* Up */
                selected = 0;
                break;
            case 0x50:  /* Down */
                selected = 1;
                break;
            case 0x1C:  /* Enter */
                done = 1;
                break;
        }
    }
    
    return (selected == 0) ? BOOT_MODE_DESKTOP : BOOT_MODE_SERVER;
}

/* Forward declaration - actual implementation in installer.c */
extern int installer_check_needed(void);

int installer_needed(void) {
    /* Use the implementation from installer.c that checks VFS */
    return installer_check_needed();
}

void installer_run(void) {
    serial_puts("[INSTALLER] Starting NeolyxOS installation wizard...\n");
    
    /* Clear screen */
    draw_rect(0, 0, fb_width, fb_height, COLOR_BG);
    
    /* Welcome screen */
    int y = fb_height / 3;
    draw_centered_text(y, "Welcome to NeolyxOS", COLOR_TEXT);
    draw_centered_text(y + 30, "The Independent Operating System", COLOR_ACCENT);
    draw_centered_text(y + 80, "Press any key to continue...", COLOR_TEXT);
    
    kbd_wait_key();
    
    /* Select boot mode */
    current_mode = boot_mode_selector();
    
    /* Installation complete */
    draw_rect(0, 0, fb_width, fb_height, COLOR_BG);
    y = fb_height / 2 - 20;
    draw_centered_text(y, "Installation Complete!", COLOR_TEXT);
    
    const char *mode_str = (current_mode == BOOT_MODE_DESKTOP) ? 
                           "Desktop Mode" : "Server Mode";
    draw_centered_text(y + 30, mode_str, COLOR_ACCENT);
    draw_centered_text(y + 80, "Starting NeolyxOS...", COLOR_TEXT);
    
    /* Wait a moment */
    for (volatile int i = 0; i < 100000000; i++);
    
    first_boot = 0;
    serial_puts("[INSTALLER] Installation complete.\n");
}

/* ============ Boot Mode API ============ */

boot_mode_t boot_get_mode(void) {
    return current_mode;
}

int boot_set_mode(boot_mode_t mode) {
    current_mode = mode;
    /* TODO: Save to /etc/neolyx.conf */
    return 0;
}

void boot_mode_init(void) {
    serial_puts("[BOOT] Initializing boot mode manager...\n");
    
    if (installer_needed()) {
        installer_run();
    }
    
    /* Run first boot wizard if needed (after install, before normal boot) */
    extern int first_boot_needed(void);
    extern int first_boot_run_wizard(void);
    
    if (first_boot_needed()) {
        serial_puts("[BOOT] Running first boot wizard...\n");
        first_boot_run_wizard();
    }
    
    serial_puts("[BOOT] Mode: ");
    switch (current_mode) {
        case BOOT_MODE_DESKTOP:
            serial_puts("Desktop\n");
            break;
        case BOOT_MODE_SERVER:
            serial_puts("Server\n");
            break;
        default:
            serial_puts("Unknown\n");
            break;
    }
}

 
