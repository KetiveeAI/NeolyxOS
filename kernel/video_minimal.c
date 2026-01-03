// kernel/video_minimal.c
// Minimal VGA text mode driver (no dependencies)
// NeolyxOS - Copyright (c) 2024 Ketivee Organization

#include <stdint.h>

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ATTR   0x0F  // White on black

static volatile uint16_t* video = (volatile uint16_t*)VGA_BUFFER;
static int cursor_x = 0;
static int cursor_y = 0;

void video_init(void) {
    cursor_x = 0;
    cursor_y = 0;
}

void video_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video[i] = ' ' | (VGA_ATTR << 8);
    }
    cursor_x = 0;
    cursor_y = 0;
}

void video_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        int pos = cursor_y * VGA_WIDTH + cursor_x;
        video[pos] = (uint16_t)c | (VGA_ATTR << 8);
        cursor_x++;
    }
    
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= VGA_HEIGHT) {
        // Simple scroll - just wrap to top
        cursor_y = 0;
    }
}

void video_print(const char* s) {
    while (*s) {
        video_putchar(*s++);
    }
}
