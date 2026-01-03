/*
 * NeolyxOS FontManager.app - NXRender API Header
 * 
 * Stub for NeolyxOS native rendering functions.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: LicenseRef-KetiveeAI-Proprietary
 */

#ifndef NXRENDER_H
#define NXRENDER_H

#include <stdint.h>
#include <stddef.h>

/* ============ Types ============ */

typedef struct nx_event {
    int type;
    union {
        struct { uint16_t code; uint16_t mods; } key;
        struct { int x, y, button, action; } mouse;
        struct { int dx, dy; } scroll;
        struct { int width, height; } resize;
    };
} nx_event_t;

/* Event types */
#define NX_EVENT_KEY         1
#define NX_EVENT_MOUSE_MOVE  2
#define NX_EVENT_MOUSE_BUTTON 3
#define NX_EVENT_SCROLL      4
#define NX_EVENT_CLOSE       5
#define NX_EVENT_RESIZE      6

/* ============ Drawing Functions ============ */

extern void nx_fill_rect(void *ctx, int x, int y, int w, int h, uint32_t color);
extern void nx_draw_rect(void *ctx, int x, int y, int w, int h, uint32_t color);
extern void nx_draw_text(void *ctx, const char *text, int x, int y, uint32_t color);
extern void nx_draw_nxi(void *ctx, const char *path, int x, int y, int w, int h);

/* ============ Window Functions ============ */

extern void* nx_window_create(const char *title, int width, int height);
extern void nx_window_destroy(void *window);
extern int nx_poll_event(void *window, nx_event_t *event);
extern void nx_present(void *window);
extern void nx_sleep(int ms);

/* ============ Input Functions ============ */

extern int nx_get_modifiers(void);

/* ============ Process Functions ============ */

extern int nx_exec(const char *path, const char *arg);

#endif /* NXRENDER_H */
