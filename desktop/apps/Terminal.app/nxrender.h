/*
 * NeolyxOS Terminal.app - NXRender Integration
 * 
 * Wrapper for NXRender graphics library to provide
 * window, text rendering, and event handling.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef TERMINAL_NXRENDER_H
#define TERMINAL_NXRENDER_H

#include <stdint.h>

/*
 * NXRender library integration
 * 
 * Terminal.app uses the NXRender C library (gui/nxrender_c/)
 * which translates to kernel-side nxrender_kdrv for GPU acceleration.
 * 
 *   Terminal.app
 *       ↓
 *   libnxrender.a (gui/nxrender_c/)
 *       ↓ syscalls
 *   nxrender_kdrv (kernel driver)
 *       ↓
 *   nxgfx_kdrv (GPU hardware)
 */

/* Include paths for nxrender */
/* #include "nxrender/window.h"     */
/* #include "nxrender/application.h" */
/* #include "nxrender/compositor.h" */

/* ============ Simplified Types for Terminal ============ */

typedef struct nx_context nx_context_t;
typedef struct nx_window nx_window_t;
typedef struct nx_font nx_font_t;

typedef struct {
    int32_t x, y;
} nx_point_t;

typedef struct {
    int32_t width, height;
} nx_size_t;

typedef struct {
    int32_t x, y, width, height;
} nx_rect_t;

typedef uint32_t nx_color_t;

/* Color helpers */
#define NX_RGB(r, g, b)     (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))
#define NX_RGBA(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* ============ Terminal Colors ============ */

#define TERM_COLOR_BG         NX_RGB(26, 26, 26)       /* Dark background */
#define TERM_COLOR_FG         NX_RGB(224, 224, 224)    /* Light text */
#define TERM_COLOR_CURSOR     NX_RGB(0, 255, 136)      /* Green cursor */
#define TERM_COLOR_SELECTION  NX_RGBA(64, 64, 128, 128) /* Selection highlight */
#define TERM_COLOR_TITLE_BAR  NX_RGB(45, 45, 45)       /* Title bar */
#define TERM_COLOR_TAB_ACTIVE NX_RGB(60, 60, 60)       /* Active tab */
#define TERM_COLOR_TAB        NX_RGB(40, 40, 40)       /* Inactive tab */

/* ============ NXRender Drawing API ============ */

/* Context management */
nx_context_t* nx_context_create(void);
void nx_context_destroy(nx_context_t *ctx);

/* Window management */
nx_window_t* nx_window_create(const char *title, nx_rect_t frame, uint32_t flags);
void nx_window_destroy(nx_window_t *win);
void nx_window_show(nx_window_t *win);
void nx_window_set_title(nx_window_t *win, const char *title);

/* Drawing primitives */
void nx_fill_rect(nx_context_t *ctx, nx_rect_t rect, nx_color_t color);
void nx_fill_rounded_rect(nx_context_t *ctx, nx_rect_t rect, nx_color_t color, int radius);
void nx_draw_rect(nx_context_t *ctx, nx_rect_t rect, nx_color_t color, int width);
void nx_draw_line(nx_context_t *ctx, nx_point_t start, nx_point_t end, nx_color_t color, int width);

/* Text rendering */
nx_font_t* nx_font_load(const char *name, int size);
void nx_font_destroy(nx_font_t *font);
void nx_draw_text(nx_context_t *ctx, const char *text, nx_point_t pos, nx_color_t color, nx_font_t *font);
nx_size_t nx_measure_text(const char *text, nx_font_t *font);
void nx_draw_char(nx_context_t *ctx, char ch, nx_point_t pos, nx_color_t fg, nx_color_t bg, nx_font_t *font);

/* Clipping */
void nx_set_clip(nx_context_t *ctx, nx_rect_t rect);
void nx_clear_clip(nx_context_t *ctx);

/* Presentation */
void nx_present(nx_context_t *ctx);

/* ============ Event Handling ============ */

typedef enum {
    NX_EVENT_NONE = 0,
    NX_EVENT_KEY_DOWN,
    NX_EVENT_KEY_UP,
    NX_EVENT_MOUSE_MOVE,
    NX_EVENT_MOUSE_DOWN,
    NX_EVENT_MOUSE_UP,
    NX_EVENT_SCROLL,
    NX_EVENT_FOCUS,
    NX_EVENT_BLUR,
    NX_EVENT_CLOSE
} nx_event_type_t;

typedef struct {
    nx_event_type_t type;
    union {
        struct {
            uint16_t keycode;
            uint16_t modifiers;
            char character;
        } key;
        struct {
            int32_t x, y;
            int32_t button;
        } mouse;
        struct {
            int32_t delta_x, delta_y;
        } scroll;
    } data;
} nx_event_t;

/* Event loop */
int nx_poll_event(nx_event_t *event);
int nx_wait_event(nx_event_t *event);

/* Keyboard modifiers */
#define NX_MOD_SHIFT   0x01
#define NX_MOD_CTRL    0x02
#define NX_MOD_ALT     0x04
#define NX_MOD_META    0x08

/* Mouse buttons */
#define NX_MOUSE_LEFT   1
#define NX_MOUSE_MIDDLE 2
#define NX_MOUSE_RIGHT  3

#endif /* TERMINAL_NXRENDER_H */
