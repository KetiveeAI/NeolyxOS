/*
 * NeolyxOS Kernel - Text Console
 * 
 * Framebuffer text console with proper font rendering
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "font.h"          /* Legacy 8x16 font */
#include "nx_sans_embedded.h"  /* NX Sans 9x18 font */

/* Font selection - use NX Sans by default */
#define USE_NX_SANS 1

/* Console state */
static volatile uint32_t *console_fb = 0;
static uint32_t console_width = 0;
static uint32_t console_height = 0;
static uint32_t console_pitch = 0;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t console_cols = 0;
static uint32_t console_rows = 0;

static uint32_t fg_color = 0x00FFFFFF;  /* White */
static uint32_t bg_color = 0x00151530;  /* Dark blue */

/* Margin for text area */
#define CONSOLE_MARGIN_X 16
#define CONSOLE_MARGIN_Y 16

/* ============ Low-level Framebuffer ============ */

static void console_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (console_fb && x < console_width && y < console_height) {
        console_fb[y * (console_pitch / 4) + x] = color;
    }
}

static void console_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h && (y + j) < console_height; j++) {
        for (uint32_t i = 0; i < w && (x + i) < console_width; i++) {
            console_put_pixel(x + i, y + j, color);
        }
    }
}

/* ============ Character Drawing ============ */

static void console_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
#if USE_NX_SANS
    /* NX Sans 9x18 font - modern, clean look */
    const uint16_t *glyph = nx_font_get_glyph(c);
    
    for (int row = 0; row < NX_FONT_HEIGHT; row++) {
        uint16_t bits = glyph[row];
        for (int col = 0; col < NX_FONT_WIDTH; col++) {
            uint32_t color = (bits & (0x8000 >> col)) ? fg : bg;
            console_put_pixel(x + col, y + row, color);
        }
    }
#else
    /* Legacy 8x16 BIOS font */
    const uint8_t *glyph = font_get_glyph(c);
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            console_put_pixel(x + col, y + row, color);
        }
    }
#endif
}

/* ============ Scrolling ============ */

/* Font dimension macros for current font */
#if USE_NX_SANS
    #define CURRENT_FONT_WIDTH  NX_FONT_WIDTH
    #define CURRENT_FONT_HEIGHT NX_FONT_HEIGHT
#else
    #define CURRENT_FONT_WIDTH  FONT_WIDTH
    #define CURRENT_FONT_HEIGHT FONT_HEIGHT
#endif

static void console_scroll(void) {
    if (!console_fb) return;
    
    uint32_t text_y = CONSOLE_MARGIN_Y;
    uint32_t line_height = CURRENT_FONT_HEIGHT;
    uint32_t text_area_height = console_height - (CONSOLE_MARGIN_Y * 2);
    
    /* Move all lines up by one line height */
    for (uint32_t y = text_y; y < text_y + text_area_height - line_height; y++) {
        for (uint32_t x = CONSOLE_MARGIN_X; x < console_width - CONSOLE_MARGIN_X; x++) {
            console_fb[y * (console_pitch / 4) + x] = 
                console_fb[(y + line_height) * (console_pitch / 4) + x];
        }
    }
    
    /* Clear last line */
    console_fill_rect(CONSOLE_MARGIN_X, text_y + text_area_height - line_height,
                      console_width - (CONSOLE_MARGIN_X * 2), line_height, bg_color);
    
    cursor_y--;
}

/* ============ Public API ============ */

void console_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    console_fb = (volatile uint32_t*)fb_addr;
    console_width = width;
    console_height = height;
    console_pitch = pitch;
    
    /* Calculate text grid dimensions using current font */
    console_cols = (width - (CONSOLE_MARGIN_X * 2)) / CURRENT_FONT_WIDTH;
    console_rows = (height - (CONSOLE_MARGIN_Y * 2)) / CURRENT_FONT_HEIGHT;
    
    cursor_x = 0;
    cursor_y = 0;
    
    /* Clear screen */
    console_fill_rect(0, 0, width, height, bg_color);
}

void console_clear(void) {
    if (!console_fb) return;
    console_fill_rect(0, 0, console_width, console_height, bg_color);
    cursor_x = 0;
    cursor_y = 0;
}

void console_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void console_putchar(char c) {
    if (!console_fb) return;
    
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3;  /* Align to 4-space tabs */
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            /* Clear the character */
            uint32_t px = CONSOLE_MARGIN_X + cursor_x * CURRENT_FONT_WIDTH;
            uint32_t py = CONSOLE_MARGIN_Y + cursor_y * CURRENT_FONT_HEIGHT;
            console_fill_rect(px, py, CURRENT_FONT_WIDTH, CURRENT_FONT_HEIGHT, bg_color);
        }
    } else if (c >= 32 && c <= 126) {
        uint32_t px = CONSOLE_MARGIN_X + cursor_x * CURRENT_FONT_WIDTH;
        uint32_t py = CONSOLE_MARGIN_Y + cursor_y * CURRENT_FONT_HEIGHT;
        console_draw_char(px, py, c, fg_color, bg_color);
        cursor_x++;
    }
    
    /* Handle line wrap */
    if (cursor_x >= console_cols) {
        cursor_x = 0;
        cursor_y++;
    }
    
    /* Handle scroll */
    while (cursor_y >= console_rows) {
        console_scroll();
    }
}

void console_puts(const char *str) {
    while (*str) {
        console_putchar(*str++);
    }
}

/* Simple printf implementation */
static void console_print_int(int64_t val, int base, int min_width, char pad) {
    char buf[32];
    int i = 0;
    int negative = 0;
    
    if (val < 0 && base == 10) {
        negative = 1;
        val = -val;
    }
    
    uint64_t uval = (uint64_t)val;
    
    do {
        int digit = uval % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        uval /= base;
    } while (uval > 0);
    
    if (negative) buf[i++] = '-';
    
    /* Padding */
    while (i < min_width) buf[i++] = pad;
    
    /* Print in reverse */
    while (i > 0) {
        console_putchar(buf[--i]);
    }
}

void console_printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt != '%') {
            console_putchar(*fmt++);
            continue;
        }
        
        fmt++;
        
        /* Parse width */
        int width = 0;
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        switch (*fmt) {
            case 'd':
            case 'i':
                console_print_int(__builtin_va_arg(args, int), 10, width, pad);
                break;
            case 'u':
                console_print_int(__builtin_va_arg(args, unsigned int), 10, width, pad);
                break;
            case 'x':
            case 'X':
                console_print_int(__builtin_va_arg(args, unsigned int), 16, width, pad);
                break;
            case 'l':
                fmt++;
                if (*fmt == 'x' || *fmt == 'X') {
                    console_print_int(__builtin_va_arg(args, uint64_t), 16, width, pad);
                } else if (*fmt == 'd') {
                    console_print_int(__builtin_va_arg(args, int64_t), 10, width, pad);
                } else if (*fmt == 'u') {
                    console_print_int(__builtin_va_arg(args, uint64_t), 10, width, pad);
                }
                break;
            case 'p':
                console_puts("0x");
                console_print_int((uint64_t)__builtin_va_arg(args, void*), 16, 16, '0');
                break;
            case 's': {
                const char *s = __builtin_va_arg(args, const char*);
                if (s) console_puts(s);
                else console_puts("(null)");
                break;
            }
            case 'c':
                console_putchar((char)__builtin_va_arg(args, int));
                break;
            case '%':
                console_putchar('%');
                break;
            default:
                console_putchar('%');
                console_putchar(*fmt);
                break;
        }
        fmt++;
    }
    
    __builtin_va_end(args);
}

/* ============ Special Drawing Functions ============ */

void console_draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    /* Top and bottom borders */
    console_fill_rect(x, y, w, 2, color);
    console_fill_rect(x, y + h - 2, w, 2, color);
    /* Left and right borders */
    console_fill_rect(x, y, 2, h, color);
    console_fill_rect(x + w - 2, y, 2, h, color);
}

void console_draw_filled_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                             uint32_t fill_color, uint32_t border_color) {
    console_fill_rect(x, y, w, h, fill_color);
    console_draw_box(x, y, w, h, border_color);
}

/* Get console dimensions for external use */
uint32_t console_get_cols(void) { return console_cols; }
uint32_t console_get_rows(void) { return console_rows; }
uint32_t console_get_width(void) { return console_width; }
uint32_t console_get_height(void) { return console_height; }
