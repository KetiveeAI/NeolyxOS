/*
 * NeolyxOS Desktop Shell
 * 
 * macOS-inspired desktop shell with:
 *   - Menu bar at top
 *   - Dock at bottom
 *   - Window compositor
 *   - Mouse cursor
 * 
 * Uses nxgfx_kdrv for framebuffer access (as a CLIENT).
 * Theme/appearance settings from nxappearance.h
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_DESKTOP_SHELL_H
#define NEOLYX_DESKTOP_SHELL_H

#include <stdint.h>
#include "nxappearance.h"

/* ============ Colors ============ */
/* NOTE: Theme-dependent colors should use nxappearance_get() at runtime */

/* Modern glassmorphic color palette */
#define DESKTOP_BG_TOP      0xFF0A0B1A  /* Deep navy top */
#define DESKTOP_BG_BOT      0xFF1A0F20  /* Dark purple-pink bottom */
#define DESKTOP_BG_COLOR    0xFF0A0B1A  /* Fallback solid */

/* Glassmorphism effects - use GLASS_COLOR() macro with nxappearance_get() for dynamic opacity */
#define GLASS_DOCK          0x40303050  /* Frosted dock 25% */
#define GLASS_WIDGET        0x70404060  /* Widget glass 44% */
#define GLASS_SEARCH        0x40354050  /* Search bar 25% */
#define GLASS_BORDER        0x30FFFFFF  /* Subtle white border */

/* Text colors */
#define COLOR_TEXT_WHITE    0xFFFFFFFF  /* Pure white */
#define COLOR_TEXT_DIM      0xFF888888  /* Dimmed gray */
#define COLOR_TEXT_BRIGHT   0xFFCCCCCC  /* Bright gray */

/* Window colors (kept for compatibility) */
#define MENUBAR_BG_COLOR    0xE0202030
#define MENUBAR_TEXT_COLOR  0xFFFFFFFF
#define DOCK_BG_COLOR       GLASS_DOCK
#define DOCK_ITEM_BG        0x60505070
#define CURSOR_COLOR        0xFFFFFFFF
#define WINDOW_BG_COLOR     0xFF252535
#define WINDOW_TITLE_COLOR  0xFF303045
#define WINDOW_BORDER_COLOR 0xFF404055
#define WINDOW_TEXT_COLOR   0xFFE0E0E0

/* ============ Layout Constants ============ */
/* NOTE: Dock settings (icon size, height) come from nxappearance_get()->dock_* */

#define MENUBAR_HEIGHT      24
#define DOCK_HEIGHT         58        /* Base height, adjusted by icon size */
#define DOCK_ICON_SIZE      48        /* Default, use nxappearance_get()->dock_icon_size */
#define DOCK_PADDING        6
#define CURSOR_SIZE         16
#define WIDGET_RADIUS       15

/* ============ Desktop State ============ */

typedef struct {
    /* Framebuffer info */
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    
    /* Mouse state */
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    
    /* Desktop elements */
    uint8_t menubar_visible;
    uint8_t dock_visible;
    
    /* Active window count */
    uint32_t window_count;
    
    /* Is running */
    uint8_t running;
} desktop_state_t;

/* ============ Window Structure ============ */

#define MAX_WINDOWS 16
#define WINDOW_TITLE_MAX 64

typedef struct {
    uint32_t id;
    char title[WINDOW_TITLE_MAX];
    
    /* Bounds */
    int32_t x, y;
    uint32_t width, height;
    
    /* State */
    uint8_t visible;
    uint8_t focused;
    uint8_t minimized;
    uint8_t dragging;
    
    /* Z-order (higher = on top) */
    uint8_t z_order;
    
    /* Content render callback */
    void (*render_content)(void *win, void *ctx);
    void *render_ctx;
    
    /* Keyboard handler callback (optional) */
    void (*key_handler)(void *win, uint8_t scancode, uint8_t pressed, void *ctx);
    void *key_ctx;
} desktop_window_t;

/* ============ Desktop API ============ */

/**
 * desktop_init - Initialize the desktop shell
 * 
 * @fb_addr: Framebuffer physical address
 * @width: Screen width
 * @height: Screen height  
 * @pitch: Framebuffer pitch in bytes
 * 
 * Returns: 0 on success, -1 on error
 */
int desktop_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);

/**
 * desktop_run - Run the desktop event loop
 * 
 * Does not return until desktop is terminated.
 */
void desktop_run(void);

/**
 * desktop_shutdown - Shutdown the desktop shell
 */
void desktop_shutdown(void);

/* ============ Window Management ============ */

/**
 * desktop_create_window - Create a new window
 * 
 * @title: Window title
 * @x, y: Initial position
 * @width, height: Window size
 * 
 * Returns: Window ID, or 0 on error
 */
uint32_t desktop_create_window(const char *title, int32_t x, int32_t y, 
                               uint32_t width, uint32_t height);

/**
 * desktop_destroy_window - Destroy a window
 */
int desktop_destroy_window(uint32_t window_id);

/**
 * desktop_focus_window - Bring window to front
 */
void desktop_focus_window(uint32_t window_id);

/**
 * desktop_set_window_render - Set window content render callback
 */
void desktop_set_window_render(uint32_t window_id, 
                               void (*callback)(desktop_window_t *win, void *ctx),
                               void *ctx);

/**
 * desktop_set_window_key_handler - Set keyboard handler for a window
 */
void desktop_set_window_key_handler(uint32_t window_id,
                                    void (*handler)(desktop_window_t *win, 
                                                    uint8_t scancode, 
                                                    uint8_t pressed, 
                                                    void *ctx),
                                    void *ctx);

/* ============ Drawing Helpers ============ */

/**
 * desktop_fill_rect - Fill a rectangle
 */
void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);

/**
 * desktop_draw_rect - Draw rectangle outline
 */
void desktop_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);

/**
 * desktop_draw_text - Draw text at position
 */
void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);

/**
 * desktop_draw_cursor - Draw mouse cursor
 */
void desktop_draw_cursor(int32_t x, int32_t y);

/* ============ Input Handling ============ */

/**
 * desktop_handle_mouse - Handle mouse input event
 */
void desktop_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);

/**
 * desktop_handle_key - Handle keyboard input event
 */
void desktop_handle_key(uint8_t scancode, uint8_t pressed);

#endif /* NEOLYX_DESKTOP_SHELL_H */
