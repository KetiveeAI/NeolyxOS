/*
 * NeolyxOS Files.app - NXRender Graphics Wrapper
 * 
 * Provides a thin wrapper to the NXRender graphics system.
 * This allows Files.app to be compiled standalone for testing.
 * 
 * Copyright (c) 2025 KetiveeAI
 * KetiveeAI is a registered trademark of KetiveeAI.
 */

#ifndef FILES_NXRENDER_H
#define FILES_NXRENDER_H

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

#endif /* NXRENDER_REAL */

/* ============ Drawing Functions ============ */

#ifdef NXRENDER_REAL

/* Use real NXRender drawing */
#define nx_fill_rect    nxrender_fill_rect
#define nx_draw_rect    nxrender_draw_rect
#define nx_draw_text    nxrender_draw_text
#define nx_draw_line    nxrender_draw_line

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

static inline void nx_draw_line(void *ctx, int x1, int y1, int x2, int y2, uint32_t color) {
    (void)ctx; (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

#endif /* NXRENDER_REAL */

/* ============ Utility Functions ============ */

static inline uint32_t nx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t nx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

#endif /* FILES_NXRENDER_H */
