/*
 * NeolyxOS - NXServer Compositor Layer
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXSERVER_COMPOSITOR_H
#define NXSERVER_COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================
 * Damage Region Tracking
 * ======================================================= */

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} nx_rect_node_t;

typedef struct {
    nx_rect_node_t* rects;
    uint32_t count;
    uint32_t capacity;
} nx_region_t;

/* Region operations for computing dirty intersections */
nx_region_t* nx_region_create(void);
void nx_region_destroy(nx_region_t* region);
void nx_region_add_rect(nx_region_t* region, int32_t x, int32_t y, uint32_t w, uint32_t h);
void nx_region_subtract_rect(nx_region_t* region, int32_t x, int32_t y, uint32_t w, uint32_t h);
bool nx_region_intersects(const nx_region_t* region, int32_t x, int32_t y, uint32_t w, uint32_t h);
void nx_region_clear(nx_region_t* region);

/* =======================================================
 * Window Management
 * ======================================================= */

#define NX_WIN_FLAG_VISIBLE    (1 << 0)
#define NX_WIN_FLAG_FOCUSED    (1 << 1)
#define NX_WIN_FLAG_ALWAYS_TOP (1 << 2)
#define NX_WIN_FLAG_NO_INPUT   (1 << 3)
#define NX_WIN_FLAG_TRANSPARENT (1 << 4)

typedef struct nx_window {
    uint32_t id;
    int32_t x, y;
    uint32_t width, height;
    
    uint32_t flags;
    uint8_t opacity; /* 0-255 global alpha */
    
    /* SHM Backing store mapped by IPC client */
    void* shm_buffer;
    uint32_t shm_key;
    uint32_t pitch;
    
    /* Z-Order tree connections */
    struct nx_window* prev; /* Window above this one */
    struct nx_window* next; /* Window below this one */
} nx_window_t;

typedef struct {
    nx_window_t* head; /* Top-most window */
    nx_window_t* tail; /* Bottom-most window */
    
    uint32_t screen_width;
    uint32_t screen_height;
    
    nx_region_t* damage_region; /* Aggregated dirty rects for the current frame */
    
    /* Compositor backbuffer */
    void* backbuffer; 
    uint32_t pitch;
} nx_compositor_t;

nx_compositor_t* nx_compositor_create(uint32_t w, uint32_t h, void* fb_ptr, uint32_t pitch);
void nx_compositor_destroy(nx_compositor_t* comp);

nx_window_t* nx_compositor_create_window(nx_compositor_t* comp, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t flags);
void nx_compositor_destroy_window(nx_compositor_t* comp, nx_window_t* win);

void nx_compositor_set_window_pos(nx_compositor_t* comp, nx_window_t* win, int32_t x, int32_t y);
void nx_compositor_raise_window(nx_compositor_t* comp, nx_window_t* win);
void nx_compositor_lower_window(nx_compositor_t* comp, nx_window_t* win);

/* Damage & Rendering */
void nx_compositor_damage_rect(nx_compositor_t* comp, int32_t x, int32_t y, uint32_t w, uint32_t h);
void nx_compositor_render_frame(nx_compositor_t* comp);

#ifdef __cplusplus
}
#endif
#endif
