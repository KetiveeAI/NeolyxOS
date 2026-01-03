/*
 * NeolyxOS Desktop Input Test
 * 
 * Minimal desktop for testing mouse/keyboard input.
 * Shows input events visually on screen for debugging.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* ============ Syscall Numbers ============ */
#define SYS_EXIT            1
#define SYS_YIELD           4
#define SYS_INPUT_POLL      123
#define SYS_INPUT_REGISTER  124

/* Input event types */
#define INPUT_TYPE_KEYBOARD     0
#define INPUT_TYPE_MOUSE_MOVE   1
#define INPUT_TYPE_MOUSE_BUTTON 2

/* ============ Structures ============ */

typedef struct {
    uint32_t type;
    uint32_t timestamp;
    union {
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t pressed;
            uint8_t modifiers;
        } keyboard;
        struct {
            int16_t dx;
            int16_t dy;
            uint8_t buttons;
            uint8_t _pad[3];
        } mouse;
    };
} input_event_t;

/* ============ Syscall Wrappers ============ */

static inline int64_t syscall0(int64_t num) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    while(1) { }
}

static void sys_yield(void) {
    syscall0(SYS_YIELD);
}

static int sys_input_poll(input_event_t *events, int max) {
    return (int)syscall2(SYS_INPUT_POLL, (int64_t)events, max);
}

/* ============ Framebuffer ============ */

/* Pre-mapped by kernel at known address */
#define FB_ADDR     0xC0000000UL
static uint32_t *g_fb = (uint32_t *)FB_ADDR;
static uint32_t g_width = 1280;
static uint32_t g_height = 800;
static uint32_t g_pitch = 1280 * 4;

/* ============ Drawing Functions ============ */

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t stride = g_pitch / 4;
    for (int py = y; py < y + h && py < (int)g_height; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < (int)g_width; px++) {
            if (px < 0) continue;
            g_fb[py * stride + px] = color;
        }
    }
}

static void fill_screen(uint32_t color) {
    draw_rect(0, 0, g_width, g_height, color);
}

/* Simple cursor - just a white square */
static void draw_cursor(int x, int y) {
    draw_rect(x, y, 12, 12, 0xFFFFFFFF);      /* White fill */
    draw_rect(x + 2, y + 2, 8, 8, 0xFF000000); /* Black center */
}

/* Draw a status indicator */
static void draw_status(int event_count, int mouse_events, int kbd_events) {
    /* Status bar at top */
    draw_rect(0, 0, g_width, 30, 0xFF333333);
    
    /* Event count indicators - colored boxes */
    /* Green box = total events received */
    if (event_count > 0) {
        for (int i = 0; i < event_count && i < 50; i++) {
            draw_rect(10 + i * 14, 5, 12, 20, 0xFF00FF00);
        }
    }
    
    /* Blue boxes = mouse events */
    if (mouse_events > 0) {
        draw_rect(g_width - 200, 5, mouse_events * 5, 20, 0xFF0088FF);
    }
    
    /* Red boxes = keyboard events */
    if (kbd_events > 0) {
        draw_rect(g_width - 100, 5, kbd_events * 10, 20, 0xFFFF0000);
    }
}

/* ============ Main ============ */

/* Debug counters */
static int total_events = 0;
static int mouse_events = 0;
static int kbd_events = 0;

/* Mouse position */
static int mouse_x = 640;
static int mouse_y = 400;

void _start(void) {
    /* Clear screen with dark blue background */
    fill_screen(0xFF1A1A2E);
    
    /* Draw initial dock placeholder */
    int dock_width = 400;
    int dock_x = (g_width - dock_width) / 2;
    int dock_y = g_height - 70;
    draw_rect(dock_x, dock_y, dock_width, 60, 0x80000000);
    
    /* 5 dock icons */
    for (int i = 0; i < 5; i++) {
        uint32_t colors[] = {0xFF4A90D9, 0xFFFF6B9D, 0xFF9B59B6, 0xFF2ECC71, 0xFFF39C12};
        draw_rect(dock_x + 20 + i * 75, dock_y + 10, 40, 40, colors[i]);
    }
    
    /* Initial cursor */
    draw_cursor(mouse_x, mouse_y);
    
    /* Draw initial status */
    draw_status(0, 0, 0);
    
    /* Main loop */
    input_event_t events[8];
    int loop_count = 0;
    
    while (1) {
        /* Poll for input events */
        int n = sys_input_poll(events, 8);
        
        if (n > 0) {
            total_events += n;
            
            for (int i = 0; i < n; i++) {
                input_event_t *ev = &events[i];
                
                if (ev->type == INPUT_TYPE_MOUSE_MOVE) {
                    mouse_events++;
                    
                    /* Erase old cursor by redrawing background */
                    draw_rect(mouse_x, mouse_y, 12, 12, 0xFF1A1A2E);
                    
                    /* Update position */
                    mouse_x += ev->mouse.dx;
                    mouse_y += ev->mouse.dy;
                    
                    /* Clamp */
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x >= (int)g_width) mouse_x = g_width - 1;
                    if (mouse_y >= (int)g_height) mouse_y = g_height - 1;
                    
                    /* Draw new cursor */
                    draw_cursor(mouse_x, mouse_y);
                }
                else if (ev->type == INPUT_TYPE_MOUSE_BUTTON) {
                    mouse_events++;
                    
                    /* Flash a box on click */
                    if (ev->mouse.buttons & 0x01) {
                        draw_rect(mouse_x - 20, mouse_y - 20, 40, 40, 0xFFFF0000);
                    }
                }
                else if (ev->type == INPUT_TYPE_KEYBOARD) {
                    kbd_events++;
                    
                    /* ESC to exit */
                    if (ev->keyboard.pressed && ev->keyboard.scancode == 0x01) {
                        sys_exit(0);
                    }
                    
                    /* Flash a key indicator */
                    draw_rect(100, 100, 50, 50, 0xFF00FF00);
                }
            }
            
            /* Update status bar */
            draw_status(total_events, mouse_events, kbd_events);
        }
        
        /* Every 1000 loops, flash a heartbeat indicator to show we're alive */
        loop_count++;
        if (loop_count % 1000 == 0) {
            /* Alternate heartbeat color */
            uint32_t color = (loop_count / 1000) % 2 ? 0xFF00FF00 : 0xFF1A1A2E;
            draw_rect(g_width - 30, g_height - 30, 20, 20, color);
        }
        
        sys_yield();
    }
    
    sys_exit(0);
}
