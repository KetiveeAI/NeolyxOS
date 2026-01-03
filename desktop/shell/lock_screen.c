/*
 * NeolyxOS Lock Screen Implementation
 * 
 * Full-screen lock overlay with password entry.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "../include/lock_screen.h"

/* User authentication syscalls - connect to kernel */
extern int session_lock(void);
extern int session_unlock(const char *password);

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* Graphics primitives from desktop_shell.c */
extern void put_pixel(int32_t x, int32_t y, uint32_t color);
extern void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern void draw_text_large(int32_t x, int32_t y, const char *text, uint32_t color);

/* ============ Color Palette ============ */

#define LOCK_BG_COLOR       0xFF0F0F14
#define LOCK_OVERLAY        0xC0000000
#define LOCK_TEXT_WHITE     0xFFFFFFFF
#define LOCK_TEXT_DIM       0xFF888888
#define LOCK_FIELD_BG       0xFF1A1A25
#define LOCK_FIELD_BORDER   0xFF3A3A55
#define LOCK_ACCENT         0xFF667EEA
#define LOCK_ERROR          0xFFFF4444

/* ============ Static State ============ */

static struct {
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    
    lock_state_t state;
    
    char username[32];
    char fullname[64];
    char password_buffer[64];
    int password_len;
    
    int error_shown;
    uint64_t error_time;
    
    int mouse_x;
    int mouse_y;
} g_lock;

/* ============ Helper Functions ============ */

static void lock_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int lock_str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* ============ Drawing Helpers ============ */

static void draw_lock_background(void) {
    /* Dark gradient background */
    for (uint32_t y = 0; y < g_lock.height; y++) {
        uint32_t shade = 0x0F + (y * 10 / g_lock.height);
        uint32_t color = (0xFF << 24) | (shade << 16) | (shade << 8) | (shade + 5);
        for (uint32_t x = 0; x < g_lock.width; x++) {
            g_lock.framebuffer[y * g_lock.pitch / 4 + x] = color;
        }
    }
}

static void draw_lock_overlay(void) {
    /* Semi-transparent overlay */
    for (uint32_t y = 0; y < g_lock.height; y++) {
        for (uint32_t x = 0; x < g_lock.width; x++) {
            uint32_t *pixel = &g_lock.framebuffer[y * g_lock.pitch / 4 + x];
            uint32_t bg = *pixel;
            /* Darken existing */
            uint32_t r = ((bg >> 16) & 0xFF) / 2;
            uint32_t g = ((bg >> 8) & 0xFF) / 2;
            uint32_t b = (bg & 0xFF) / 2;
            *pixel = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_time(void) {
    uint64_t secs = pit_get_ticks() / 100;
    uint32_t hours = ((secs / 3600) + 11) % 24;
    uint32_t mins = (secs / 60) % 60;
    
    char time_str[8];
    time_str[0] = '0' + (hours / 10);
    time_str[1] = '0' + (hours % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mins / 10);
    time_str[4] = '0' + (mins % 10);
    time_str[5] = '\0';
    
    /* Large time display */
    int tx = (g_lock.width - 80) / 2;
    int ty = g_lock.height / 4;
    draw_text_large(tx, ty, time_str, LOCK_TEXT_WHITE);
}

static void draw_date(void) {
    /* Simplified date - in production, get from RTC */
    int dx = (g_lock.width - 160) / 2;
    int dy = g_lock.height / 4 + 60;
    desktop_draw_text(dx, dy, "Friday, December 27", LOCK_TEXT_DIM);
}

static void draw_user_avatar(void) {
    /* Profile circle */
    int cx = g_lock.width / 2;
    int cy = g_lock.height / 2 - 40;
    
    /* Avatar background circle */
    fill_circle(cx, cy, 50, LOCK_FIELD_BG);
    fill_circle(cx, cy, 48, LOCK_ACCENT);
    
    /* User initial */
    char initial[2] = {0, 0};
    if (g_lock.fullname[0]) {
        initial[0] = g_lock.fullname[0];
    } else if (g_lock.username[0]) {
        initial[0] = g_lock.username[0];
    } else {
        initial[0] = 'U';
    }
    /* Convert to uppercase */
    if (initial[0] >= 'a' && initial[0] <= 'z') {
        initial[0] -= 32;
    }
    draw_text_large(cx - 12, cy - 12, initial, LOCK_TEXT_WHITE);
}

static void draw_username(void) {
    const char *name = g_lock.fullname[0] ? g_lock.fullname : g_lock.username;
    int len = lock_str_len(name);
    int nx = (g_lock.width - len * 8) / 2;
    int ny = g_lock.height / 2 + 30;
    desktop_draw_text(nx, ny, name, LOCK_TEXT_WHITE);
}

static void draw_password_field(void) {
    int fw = 300;
    int fh = 40;
    int fx = (g_lock.width - fw) / 2;
    int fy = g_lock.height / 2 + 60;
    
    /* Field background */
    fill_rounded_rect(fx, fy, fw, fh, 8, LOCK_FIELD_BG);
    
    /* Password dots */
    int dot_x = fx + 20;
    for (int i = 0; i < g_lock.password_len && i < 20; i++) {
        fill_circle(dot_x + i * 12, fy + fh/2, 4, LOCK_TEXT_WHITE);
    }
    
    /* Placeholder if empty */
    if (g_lock.password_len == 0) {
        desktop_draw_text(fx + 20, fy + 12, "Enter Password", LOCK_TEXT_DIM);
    }
    
    /* Cursor */
    if ((pit_get_ticks() / 50) % 2 == 0) {
        desktop_fill_rect(dot_x + g_lock.password_len * 12, fy + 10, 2, 20, LOCK_TEXT_WHITE);
    }
}

static void draw_error_message(void) {
    if (!g_lock.error_shown) return;
    
    /* Check if error should expire */
    if (pit_get_ticks() - g_lock.error_time > 300) {
        g_lock.error_shown = 0;
        return;
    }
    
    int ex = (g_lock.width - 200) / 2;
    int ey = g_lock.height / 2 + 120;
    desktop_draw_text(ex, ey, "Incorrect password", LOCK_ERROR);
}

static void draw_hint(void) {
    int hx = (g_lock.width - 200) / 2;
    int hy = g_lock.height - 100;
    desktop_draw_text(hx, hy, "Press Enter to unlock", LOCK_TEXT_DIM);
}

/* ============ Public API ============ */

int lock_screen_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    g_lock.framebuffer = (uint32_t *)fb_addr;
    g_lock.width = width;
    g_lock.height = height;
    g_lock.pitch = pitch;
    g_lock.state = LOCK_STATE_HIDDEN;
    g_lock.password_len = 0;
    g_lock.error_shown = 0;
    g_lock.mouse_x = width / 2;
    g_lock.mouse_y = height / 2;
    
    lock_str_copy(g_lock.username, "admin", 32);
    lock_str_copy(g_lock.fullname, "Administrator", 64);
    
    serial_puts("[LOCK] Lock screen initialized\n");
    return 0;
}

void lock_screen_show(void) {
    if (g_lock.state != LOCK_STATE_HIDDEN) return;
    
    g_lock.state = LOCK_STATE_LOCKED;
    g_lock.password_len = 0;
    g_lock.password_buffer[0] = '\0';
    g_lock.error_shown = 0;
    
    /* Lock session */
    session_lock();
    
    serial_puts("[LOCK] Lock screen shown\n");
}

void lock_screen_hide(void) {
    g_lock.state = LOCK_STATE_HIDDEN;
    g_lock.password_len = 0;
    serial_puts("[LOCK] Lock screen hidden\n");
}

void lock_screen_render(void) {
    if (g_lock.state == LOCK_STATE_HIDDEN) return;
    
    draw_lock_background();
    draw_time();
    draw_date();
    draw_user_avatar();
    draw_username();
    draw_password_field();
    draw_error_message();
    draw_hint();
}

void lock_screen_handle_key(uint8_t scancode, int pressed) {
    if (!pressed) return;
    if (g_lock.state == LOCK_STATE_HIDDEN) return;
    
    /* Conversion from scancode to ASCII (simplified) */
    static const char scancode_to_ascii[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    
    if (scancode == 0x1C) {  /* Enter */
        /* Attempt unlock */
        g_lock.password_buffer[g_lock.password_len] = '\0';
        
        if (session_unlock(g_lock.password_buffer) == 0) {
            lock_screen_hide();
        } else {
            g_lock.error_shown = 1;
            g_lock.error_time = pit_get_ticks();
            g_lock.password_len = 0;
        }
        return;
    }
    
    if (scancode == 0x0E) {  /* Backspace */
        if (g_lock.password_len > 0) {
            g_lock.password_len--;
            g_lock.password_buffer[g_lock.password_len] = '\0';
        }
        return;
    }
    
    if (scancode < sizeof(scancode_to_ascii)) {
        char c = scancode_to_ascii[scancode];
        if (c && g_lock.password_len < 63) {
            g_lock.password_buffer[g_lock.password_len++] = c;
            g_lock.password_buffer[g_lock.password_len] = '\0';
        }
    }
}

void lock_screen_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    g_lock.mouse_x += dx;
    g_lock.mouse_y += dy;
    
    if (g_lock.mouse_x < 0) g_lock.mouse_x = 0;
    if (g_lock.mouse_y < 0) g_lock.mouse_y = 0;
    if (g_lock.mouse_x >= (int32_t)g_lock.width) g_lock.mouse_x = g_lock.width - 1;
    if (g_lock.mouse_y >= (int32_t)g_lock.height) g_lock.mouse_y = g_lock.height - 1;
    
    (void)buttons;
}

int lock_screen_is_active(void) {
    return g_lock.state != LOCK_STATE_HIDDEN;
}

void lock_screen_set_user(const char *username, const char *fullname) {
    if (username) lock_str_copy(g_lock.username, username, 32);
    if (fullname) lock_str_copy(g_lock.fullname, fullname, 64);
}
