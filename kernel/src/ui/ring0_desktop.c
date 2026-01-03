/*
 * NeolyxOS Ring 0 Desktop with Mouse Support
 * 
 * Runs in kernel mode and directly accesses framebuffer globals
 * and kernel input driver. Includes mouse event loop for cursor.
 * 
 * This file is LINKED with the kernel (not loaded as ELF).
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* Import kernel framebuffer globals */
extern uint64_t g_framebuffer_addr;
extern uint32_t g_framebuffer_width;
extern uint32_t g_framebuffer_height;
extern uint32_t g_framebuffer_pitch;

/* Import kernel mouse driver functions */
extern int nxmouse_get_delta(int16_t *dx, int16_t *dy, uint8_t *buttons);
extern void serial_puts(const char *s);

/* Framebuffer pointer */
static uint32_t *g_fb = 0;
static uint32_t g_width = 0;
static uint32_t g_height = 0;
static uint32_t g_pitch = 0;

/* Mouse state */
static int32_t g_mouse_x = 0;
static int32_t g_mouse_y = 0;
static uint8_t g_mouse_buttons = 0;

/* Saved pixels under cursor */
#define CURSOR_SIZE 16
static uint32_t g_cursor_save[CURSOR_SIZE * CURSOR_SIZE];
static int32_t g_cursor_saved_x = -1;
static int32_t g_cursor_saved_y = -1;

/* ============ Drawing Functions ============ */

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_fb) return;
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

/* ============ Mouse Cursor ============ */

static void save_cursor_area(int x, int y) {
    if (!g_fb) return;
    uint32_t stride = g_pitch / 4;
    
    for (int py = 0; py < CURSOR_SIZE; py++) {
        for (int px = 0; px < CURSOR_SIZE; px++) {
            int sx = x + px;
            int sy = y + py;
            if (sx >= 0 && sx < (int)g_width && sy >= 0 && sy < (int)g_height) {
                g_cursor_save[py * CURSOR_SIZE + px] = g_fb[sy * stride + sx];
            }
        }
    }
    g_cursor_saved_x = x;
    g_cursor_saved_y = y;
}

static void restore_cursor_area(void) {
    if (!g_fb || g_cursor_saved_x < 0) return;
    uint32_t stride = g_pitch / 4;
    
    for (int py = 0; py < CURSOR_SIZE; py++) {
        for (int px = 0; px < CURSOR_SIZE; px++) {
            int sx = g_cursor_saved_x + px;
            int sy = g_cursor_saved_y + py;
            if (sx >= 0 && sx < (int)g_width && sy >= 0 && sy < (int)g_height) {
                g_fb[sy * stride + sx] = g_cursor_save[py * CURSOR_SIZE + px];
            }
        }
    }
}

static void draw_cursor(int x, int y) {
    if (!g_fb) return;
    uint32_t stride = g_pitch / 4;
    
    /* Arrow cursor pattern (16x16) */
    static const uint8_t cursor[16][16] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,1,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,1,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,1,2,1,0,0,0,0,0,0,0,0,0},
        {1,0,0,0,0,1,2,1,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,2,1,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0},
    };
    
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int sx = x + px;
            int sy = y + py;
            if (sx >= 0 && sx < (int)g_width && sy >= 0 && sy < (int)g_height) {
                if (cursor[py][px] == 1) {
                    g_fb[sy * stride + sx] = 0xFF000000;  /* Black outline */
                } else if (cursor[py][px] == 2) {
                    g_fb[sy * stride + sx] = 0xFFFFFFFF;  /* White fill */
                }
            }
        }
    }
}

/* ============ Desktop UI ============ */

/* Desktop colors */
#define COLOR_DESKTOP_BG    0xFF1E1E2E  /* Dark background */
#define COLOR_MENUBAR_BG    0xFF2D2D3F  /* Menu bar */
#define COLOR_DOCK_BG       0xFF333344  
#define COLOR_DOCK_ICON     0xFF4A90D9  /* Blue icons */
#define COLOR_SUCCESS_GREEN 0xFF00AA00  /* Success indicator */

static void draw_menubar(void) {
    /* Menu bar at top */
    draw_rect(0, 0, g_width, 28, COLOR_MENUBAR_BG);
    
    /* NeolyxOS "logo" - small green square */
    draw_rect(8, 6, 16, 16, COLOR_SUCCESS_GREEN);
}

static void draw_dock(void) {
    /* Dock at bottom center */
    int dock_width = 400;
    int dock_height = 60;
    int dock_x = (g_width - dock_width) / 2;
    int dock_y = g_height - dock_height - 10;
    
    /* Dock background */
    draw_rect(dock_x, dock_y, dock_width, dock_height, COLOR_DOCK_BG);
    
    /* Dock icons (simple squares for now) */
    for (int i = 0; i < 5; i++) {
        int icon_x = dock_x + 20 + i * 70;
        int icon_y = dock_y + 10;
        draw_rect(icon_x, icon_y, 40, 40, COLOR_DOCK_ICON);
    }
}

static void desktop_render_static(void) {
    /* Clear screen */
    fill_screen(COLOR_DESKTOP_BG);
    
    /* Draw UI elements */
    draw_menubar();
    draw_dock();
}

/* ============ Mouse Event Loop ============ */

static void handle_mouse_input(void) {
    int16_t dx = 0, dy = 0;
    uint8_t buttons = 0;
    
    /* Poll mouse - nxmouse_get_delta returns 0 on success with new data */
    if (nxmouse_get_delta(&dx, &dy, &buttons) == 0) {
        if (dx != 0 || dy != 0 || buttons != g_mouse_buttons) {
            /* Restore old cursor position */
            restore_cursor_area();
            
            /* Update position */
            g_mouse_x += dx;
            g_mouse_y += dy;
            
            /* Clamp to screen */
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_x >= (int32_t)g_width) g_mouse_x = g_width - 1;
            if (g_mouse_y >= (int32_t)g_height) g_mouse_y = g_height - 1;
            
            /* Save new cursor area and draw */
            save_cursor_area(g_mouse_x, g_mouse_y);
            draw_cursor(g_mouse_x, g_mouse_y);
            
            g_mouse_buttons = buttons;
        }
    }
}

/* ============ Public Entry Point ============ */

/*
 * ring0_desktop_main - Called directly by kernel main.c
 * 
 * This function runs the Ring 0 desktop with mouse support.
 * It never returns (runs event loop forever).
 */
void ring0_desktop_main(void) {
    /* Get framebuffer from kernel globals */
    g_fb = (uint32_t *)g_framebuffer_addr;
    g_width = g_framebuffer_width;
    g_height = g_framebuffer_height;
    g_pitch = g_framebuffer_pitch;
    
    /* Check if framebuffer is valid */
    if (!g_fb || g_width == 0 || g_height == 0) {
        serial_puts("[R0DESKTOP] Error: No valid framebuffer!\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    
    /* Initialize mouse position to center */
    g_mouse_x = g_width / 2;
    g_mouse_y = g_height / 2;
    
    /* Render desktop once */
    desktop_render_static();
    
    serial_puts("[R0DESKTOP] Desktop rendered, starting event loop\n");
    
    /* Initial cursor save and draw */
    save_cursor_area(g_mouse_x, g_mouse_y);
    draw_cursor(g_mouse_x, g_mouse_y);
    
    /* Event loop - poll mouse and update cursor */
    while (1) {
        handle_mouse_input();
        
        /* Small delay to reduce CPU usage */
        __asm__ volatile("hlt");
    }
}
