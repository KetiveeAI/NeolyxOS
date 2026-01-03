/*
 * NeolyxOS Calculator.app - NXRender Graphics Wrapper
 * 
 * Provides a thin wrapper to the NXRender graphics system.
 * This allows Calculator.app to be compiled standalone for testing.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * Proprietary and confidential. Unauthorized copying prohibited.
 */

#ifndef CALCULATOR_NXRENDER_H
#define CALCULATOR_NXRENDER_H

#include <stdint.h>
#include <stddef.h>

/* ============ Types ============ */

typedef struct nx_window nx_window_t;
typedef struct nx_context nx_context_t;
typedef struct nx_event nx_event_t;

/* Event types */
typedef enum {
    NX_EVENT_NONE = 0,
    NX_EVENT_KEY_DOWN,
    NX_EVENT_KEY_UP,
    NX_EVENT_MOUSE_MOVE,
    NX_EVENT_MOUSE_DOWN,
    NX_EVENT_MOUSE_UP,
    NX_EVENT_MOUSE_SCROLL,
    NX_EVENT_RESIZE,
    NX_EVENT_CLOSE
} nx_event_type_t;

/* Event structure */
struct nx_event {
    nx_event_type_t type;
    union {
        struct { uint16_t keycode; uint16_t mods; } key;
        struct { int x; int y; int button; } mouse;
        struct { int width; int height; } resize;
        struct { int dx; int dy; } scroll;
    };
};

/* Key codes */
#define NX_KEY_0        0x30
#define NX_KEY_1        0x31
#define NX_KEY_2        0x32
#define NX_KEY_3        0x33
#define NX_KEY_4        0x34
#define NX_KEY_5        0x35
#define NX_KEY_6        0x36
#define NX_KEY_7        0x37
#define NX_KEY_8        0x38
#define NX_KEY_9        0x39
#define NX_KEY_PLUS     0x2B
#define NX_KEY_MINUS    0x2D
#define NX_KEY_STAR     0x2A
#define NX_KEY_SLASH    0x2F
#define NX_KEY_DOT      0x2E
#define NX_KEY_ENTER    0x0D
#define NX_KEY_ESCAPE   0x1B
#define NX_KEY_BACKSPACE 0x08
#define NX_KEY_C        0x43
#define NX_KEY_PERCENT  0x25

/* ============ Window Functions ============ */

#ifdef NXRENDER_REAL

/* Real NXRender - include from library */
#include <nxrender/window.h>
#include <nxrender/draw.h>

#else

/* Stub implementation for standalone build */

static inline nx_window_t* nx_window_create(const char *title, int w, int h, uint32_t flags) {
    (void)title; (void)w; (void)h; (void)flags;
    return (nx_window_t*)1;  /* Non-null dummy */
}

static inline void nx_window_destroy(nx_window_t *win) {
    (void)win;
}

static inline nx_context_t* nx_window_context(nx_window_t *win) {
    (void)win;
    return (nx_context_t*)1;
}

static inline int nx_poll_event(nx_window_t *win, nx_event_t *event) {
    (void)win;
    event->type = NX_EVENT_NONE;
    return 0;
}

static inline void nx_present(nx_context_t *ctx) {
    (void)ctx;
}

static inline void nx_sleep(int ms) {
    (void)ms;
}

#endif /* NXRENDER_REAL */

/* ============ Drawing Functions ============ */

#ifdef NXRENDER_REAL

/* Use real NXRender drawing */
#define nx_fill_rect        nxrender_fill_rect
#define nx_draw_rect        nxrender_draw_rect
#define nx_draw_text        nxrender_draw_text
#define nx_draw_text_large  nxrender_draw_text_large
#define nx_fill_rounded     nxrender_fill_rounded_rect
#define nx_text_width       nxrender_text_width

#else

/* Stub drawing for testing */

static inline void nx_fill_rect(void *ctx, int x, int y, int w, int h, uint32_t color) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h; (void)color;
}

static inline void nx_draw_rect(void *ctx, int x, int y, int w, int h, uint32_t color) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h; (void)color;
}

static inline void nx_draw_text(void *ctx, const char *text, int x, int y, uint32_t color) {
    (void)ctx; (void)text; (void)x; (void)y; (void)color;
}

static inline void nx_draw_text_large(void *ctx, const char *text, int x, int y, uint32_t color, int size) {
    (void)ctx; (void)text; (void)x; (void)y; (void)color; (void)size;
}

static inline void nx_fill_rounded(void *ctx, int x, int y, int w, int h, int r, uint32_t color) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h; (void)r; (void)color;
}

static inline int nx_text_width(const char *text, int size) {
    (void)size;
    int len = 0;
    while (text[len]) len++;
    return len * 8;
}

#endif /* NXRENDER_REAL */

/* ============ Utility Functions ============ */

static inline uint32_t nx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t nx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

#endif /* CALCULATOR_NXRENDER_H */
