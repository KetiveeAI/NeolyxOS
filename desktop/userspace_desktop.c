/*
 * NeolyxOS Userspace Desktop - Interactive Desktop with Mouse Input
 * 
 * This is the userspace desktop that runs in Ring 3 with full mouse support.
 * Uses syscalls for framebuffer and input access.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* ============ Syscall Numbers (MUST MATCH kernel/include/core/syscall.h) ============ */
#define SYS_EXIT            1
#define SYS_SLEEP           2
#define SYS_TIME            3
#define SYS_YIELD           4

/* Framebuffer syscalls */
#define SYS_FB_MAP          120
#define SYS_FB_INFO         121
#define SYS_FB_FLIP         122

/* Input syscalls */
#define SYS_INPUT_POLL      123
#define SYS_INPUT_REGISTER  124

/* Input event types */
#define INPUT_TYPE_KEYBOARD     0
#define INPUT_TYPE_MOUSE_MOVE   1
#define INPUT_TYPE_MOUSE_BUTTON 2

/* ============ Structures ============ */

typedef struct {
    uint64_t base_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint64_t size;
} fb_info_t;

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

/* API wrappers */
static void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    while(1) { }
}

static void sys_yield(void) {
    syscall0(SYS_YIELD);
}

static uint64_t sys_time(void) {
    return (uint64_t)syscall0(SYS_TIME);
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

/* ============ Mouse State ============ */

static int32_t mouse_x = 640;
static int32_t mouse_y = 400;
static uint8_t mouse_buttons = 0;

/* ============ Drawing Functions ============ */

static void draw_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < (int)g_width && y >= 0 && y < (int)g_height) {
        g_fb[y * (g_pitch / 4) + x] = color;
    }
}

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

/* Alpha blending for cursor */
static uint32_t blend_color(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;
    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;
    
    uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
    
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ============ Cursor ============ */

#define CURSOR_SIZE 16

/* Simple arrow cursor bitmap */
static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0},
    {1,2,2,2,1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,1,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,1,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,1,2,1,0,0,0,0,0,0,0},
    {1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0},
};

/* Saved background under cursor */
static uint32_t cursor_save[CURSOR_SIZE][CURSOR_SIZE];
static int cursor_save_x = -1;
static int cursor_save_y = -1;

static void save_cursor_bg(int x, int y) {
    for (int cy = 0; cy < CURSOR_SIZE; cy++) {
        for (int cx = 0; cx < CURSOR_SIZE; cx++) {
            int px = x + cx;
            int py = y + cy;
            if (px >= 0 && px < (int)g_width && py >= 0 && py < (int)g_height) {
                cursor_save[cy][cx] = g_fb[py * (g_pitch/4) + px];
            }
        }
    }
    cursor_save_x = x;
    cursor_save_y = y;
}

static void restore_cursor_bg(void) {
    if (cursor_save_x < 0) return;
    for (int cy = 0; cy < CURSOR_SIZE; cy++) {
        for (int cx = 0; cx < CURSOR_SIZE; cx++) {
            int px = cursor_save_x + cx;
            int py = cursor_save_y + cy;
            if (px >= 0 && px < (int)g_width && py >= 0 && py < (int)g_height) {
                g_fb[py * (g_pitch/4) + px] = cursor_save[cy][cx];
            }
        }
    }
}

static void draw_cursor(int x, int y) {
    /* Save background first */
    save_cursor_bg(x, y);
    
    /* Draw cursor */
    for (int cy = 0; cy < CURSOR_SIZE; cy++) {
        for (int cx = 0; cx < CURSOR_SIZE; cx++) {
            uint8_t pixel = cursor_bitmap[cy][cx];
            if (pixel == 0) continue;
            
            int px = x + cx;
            int py = y + cy;
            if (px < 0 || px >= (int)g_width || py < 0 || py >= (int)g_height) continue;
            
            uint32_t color;
            if (pixel == 1) {
                color = 0xFF000000;  /* Black border */
            } else {
                color = 0xFFFFFFFF;  /* White fill */
            }
            g_fb[py * (g_pitch/4) + px] = color;
        }
    }
}

/* ============ Desktop UI Colors ============ */

#define COLOR_DESKTOP_BG    0xFF1E1E2E  /* Dark background */
#define COLOR_MENUBAR_BG    0xFF2D2D3F  /* Menu bar */
#define COLOR_SUCCESS_GREEN 0xFF00AA00  /* Success indicator */
#define COLOR_DOCK_BG       0x80000000  /* Semi-transparent dock */
#define COLOR_DOCK_ICON     0xFF4A90D9  /* Blue icons */
#define COLOR_DOCK_HOVER    0xFF6BB5FF  /* Hover highlight */
#define COLOR_TEXT          0xFFFFFFFF  /* White text */

/* ============ Dock Data ============ */

#define DOCK_ICON_COUNT 6
#define DOCK_ICON_SIZE  48
#define DOCK_ICON_GAP   12
#define DOCK_HEIGHT     68
#define DOCK_PADDING    16

static int dock_hover_index = -1;

typedef struct {
    int x, y;
    int size;
    uint32_t color;
    const char *name;
} dock_icon_t;

static dock_icon_t dock_icons[DOCK_ICON_COUNT];

static int get_dock_x(void) {
    int dock_width = DOCK_ICON_COUNT * DOCK_ICON_SIZE + (DOCK_ICON_COUNT - 1) * DOCK_ICON_GAP + DOCK_PADDING * 2;
    return (g_width - dock_width) / 2;
}

static int get_dock_y(void) {
    return g_height - DOCK_HEIGHT - 10;
}

static void init_dock_icons(void) {
    int dock_x = get_dock_x();
    int dock_y = get_dock_y();
    
    uint32_t colors[DOCK_ICON_COUNT] = {
        0xFF4A90D9,  /* Finder - Blue */
        0xFFFF6B6B,  /* Safari - Red */
        0xFF45B7D1,  /* Mail - Cyan */
        0xFF96CEB4,  /* Calendar - Green */
        0xFFFFA07A,  /* Settings - Orange */
        0xFF9B59B6,  /* Terminal - Purple */
    };
    
    for (int i = 0; i < DOCK_ICON_COUNT; i++) {
        dock_icons[i].x = dock_x + DOCK_PADDING + i * (DOCK_ICON_SIZE + DOCK_ICON_GAP);
        dock_icons[i].y = dock_y + (DOCK_HEIGHT - DOCK_ICON_SIZE) / 2;
        dock_icons[i].size = DOCK_ICON_SIZE;
        dock_icons[i].color = colors[i];
        dock_icons[i].name = 0;  /* Set later */
    }
}

/* ============ Desktop Drawing ============ */

static void draw_menubar(void) {
    /* Menu bar at top */
    draw_rect(0, 0, g_width, 28, COLOR_MENUBAR_BG);
    
    /* NeolyxOS logo/apple icon */
    draw_rect(10, 6, 16, 16, 0xFF00AA00);
    
    /* Time indicator on right */
    draw_rect(g_width - 80, 6, 70, 16, 0x40FFFFFF);
}

static void draw_dock(void) {
    int dock_x = get_dock_x();
    int dock_y = get_dock_y();
    int dock_width = DOCK_ICON_COUNT * DOCK_ICON_SIZE + (DOCK_ICON_COUNT - 1) * DOCK_ICON_GAP + DOCK_PADDING * 2;
    
    /* Dock background with rounded look */
    draw_rect(dock_x, dock_y, dock_width, DOCK_HEIGHT, 0x80303050);
    draw_rect(dock_x + 2, dock_y + 2, dock_width - 4, DOCK_HEIGHT - 4, 0xA0202030);
    
    /* Draw icons */
    for (int i = 0; i < DOCK_ICON_COUNT; i++) {
        int ix = dock_icons[i].x;
        int iy = dock_icons[i].y;
        int size = dock_icons[i].size;
        uint32_t color = dock_icons[i].color;
        
        /* Hover effect - draw larger and brighter */
        if (i == dock_hover_index) {
            /* Glow effect */
            draw_rect(ix - 4, iy - 4, size + 8, size + 8, 0x40FFFFFF);
            draw_rect(ix - 2, iy - 6, size + 4, size + 4, 0x60FFFFFF);
        }
        
        /* Icon with rounded corners effect */
        draw_rect(ix + 2, iy, size - 4, size, color);
        draw_rect(ix, iy + 2, size, size - 4, color);
        draw_rect(ix + 1, iy + 1, size - 2, size - 2, color);
        
        /* Inner highlight */
        draw_rect(ix + 4, iy + 4, size - 8, 4, blend_color(color, 0xFFFFFFFF, 100));
    }
}

static void draw_desktop_background(void) {
    /* Gradient-ish background */
    for (int y = 0; y < (int)g_height; y++) {
        /* Subtle vertical gradient */
        uint8_t shade = 30 + (y * 20) / g_height;
        uint32_t color = (0xFF << 24) | (shade << 16) | (shade << 8) | (shade + 20);
        for (int x = 0; x < (int)g_width; x++) {
            g_fb[y * (g_pitch/4) + x] = color;
        }
    }
}

static void draw_status_text(void) {
    /* Status area showing mouse position */
    draw_rect(10, 40, 200, 24, 0x80000000);
    
    /* Simple position indicator using rectangles */
    int x_pos = 20 + (mouse_x * 150) / g_width;
    int y_pos = 44 + (mouse_y * 16) / g_height;
    draw_rect(x_pos, y_pos, 4, 4, 0xFF00FF00);
}

static void draw_desktop_full(void) {
    draw_desktop_background();
    draw_menubar();
    draw_dock();
    draw_status_text();
}

/* ============ Input Handling ============ */

static int check_dock_hover(int mx, int my) {
    for (int i = 0; i < DOCK_ICON_COUNT; i++) {
        int ix = dock_icons[i].x;
        int iy = dock_icons[i].y;
        int size = dock_icons[i].size;
        
        if (mx >= ix && mx < ix + size && my >= iy && my < iy + size) {
            return i;
        }
    }
    return -1;
}

static void handle_input(void) {
    input_event_t events[16];
    int count = sys_input_poll(events, 16);
    
    int needs_redraw = 0;
    
    for (int i = 0; i < count; i++) {
        input_event_t *ev = &events[i];
        
        if (ev->type == INPUT_TYPE_MOUSE_MOVE) {
            /* Restore old cursor position */
            restore_cursor_bg();
            
            /* Update position */
            mouse_x += ev->mouse.dx;
            mouse_y += ev->mouse.dy;
            
            /* Clamp to screen */
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)g_width) mouse_x = g_width - 1;
            if (mouse_y >= (int)g_height) mouse_y = g_height - 1;
            
            /* Check dock hover */
            int new_hover = check_dock_hover(mouse_x, mouse_y);
            if (new_hover != dock_hover_index) {
                dock_hover_index = new_hover;
                needs_redraw = 1;
            }
            
            /* Draw cursor at new position */
            draw_cursor(mouse_x, mouse_y);
        }
        else if (ev->type == INPUT_TYPE_MOUSE_BUTTON) {
            mouse_buttons = ev->mouse.buttons;
            
            /* Handle click on dock */
            if (mouse_buttons & 0x01) {  /* Left click */
                int clicked_icon = check_dock_hover(mouse_x, mouse_y);
                if (clicked_icon >= 0) {
                    /* Visual feedback - flash the icon */
                    draw_rect(dock_icons[clicked_icon].x, dock_icons[clicked_icon].y,
                              dock_icons[clicked_icon].size, dock_icons[clicked_icon].size,
                              0xFFFFFFFF);
                    needs_redraw = 1;
                }
            }
        }
        else if (ev->type == INPUT_TYPE_KEYBOARD) {
            /* Handle keyboard - Escape to exit */
            if (ev->keyboard.pressed && ev->keyboard.scancode == 0x01) {
                sys_exit(0);
            }
        }
    }
    
    if (needs_redraw) {
        /* Redraw dock area */
        int dock_y = get_dock_y();
        draw_rect(get_dock_x() - 10, dock_y - 10, 
                  g_width - 2 * (get_dock_x() - 10), DOCK_HEIGHT + 20, COLOR_DESKTOP_BG);
        draw_dock();
        draw_cursor(mouse_x, mouse_y);
    }
}

/* ============ Main Loop ============ */

void _start(void) {
    /* Initialize dock icons */
    init_dock_icons();
    
    /* Initial draw */
    draw_desktop_full();
    
    /* Draw initial cursor */
    draw_cursor(mouse_x, mouse_y);
    
    /* Main event loop */
    while (1) {
        handle_input();
        sys_yield();
    }
    
    sys_exit(0);
}
