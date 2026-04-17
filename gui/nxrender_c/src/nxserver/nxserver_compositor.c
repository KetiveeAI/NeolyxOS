/*
 * NeolyxOS - NXServer Compositor Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxserver/nxserver_compositor.h"
#include <stddef.h>

/* External allocator definitions */
extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);
extern void* memcpy(void* dest, const void* src, size_t n);

/* =======================================================
 * Damage Region Tracking
 * ======================================================= */

nx_region_t* nx_region_create(void) {
    nx_region_t* reg = (nx_region_t*)calloc(1, sizeof(nx_region_t));
    if (reg) {
        reg->capacity = 16;
        reg->rects = (nx_rect_node_t*)malloc(sizeof(nx_rect_node_t) * reg->capacity);
    }
    return reg;
}

void nx_region_destroy(nx_region_t* region) {
    if (!region) return;
    if (region->rects) free(region->rects);
    free(region);
}

void nx_region_add_rect(nx_region_t* region, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!region || w == 0 || h == 0) return;
    
    if (region->count >= region->capacity) {
        /* Simple reallocation mock */
        uint32_t new_cap = region->capacity * 2;
        nx_rect_node_t* new_rects = (nx_rect_node_t*)malloc(sizeof(nx_rect_node_t) * new_cap);
        if (new_rects) {
            for (uint32_t i = 0; i < region->count; i++) {
                new_rects[i] = region->rects[i];
            }
            free(region->rects);
            region->rects = new_rects;
            region->capacity = new_cap;
        } else {
            return;
        }
    }
    
    region->rects[region->count].x = x;
    region->rects[region->count].y = y;
    region->rects[region->count].width = w;
    region->rects[region->count].height = h;
    region->count++;
}

void nx_region_clear(nx_region_t* region) {
    if (region) region->count = 0;
}

static inline int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

bool nx_region_intersects(const nx_region_t* region, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!region) return false;
    
    int32_t r1_right = x + w;
    int32_t r1_bottom = y + h;
    
    for (uint32_t i = 0; i < region->count; i++) {
        nx_rect_node_t* rect = &region->rects[i];
        int32_t r2_right = rect->x + rect->width;
        int32_t r2_bottom = rect->y + rect->height;
        
        if (!(rect->x >= r1_right || r2_right <= x ||
              rect->y >= r1_bottom || r2_bottom <= y)) {
            return true;
        }
    }
    return false;
}

/* =======================================================
 * Window Management
 * ======================================================= */

static uint32_t g_next_win_id = 1;

nx_compositor_t* nx_compositor_create(uint32_t w, uint32_t h, void* fb_ptr, uint32_t pitch) {
    nx_compositor_t* comp = (nx_compositor_t*)calloc(1, sizeof(nx_compositor_t));
    if (!comp) return NULL;
    
    comp->screen_width = w;
    comp->screen_height = h;
    comp->backbuffer = fb_ptr;
    comp->pitch = pitch;
    comp->damage_region = nx_region_create();
    
    /* Initially, entire screen is damaged */
    nx_compositor_damage_rect(comp, 0, 0, w, h);
    return comp;
}

void nx_compositor_destroy(nx_compositor_t* comp) {
    if (!comp) return;
    
    nx_window_t* curr = comp->head;
    while (curr) {
        nx_window_t* next = curr->next;
        free(curr);
        curr = next;
    }
    
    nx_region_destroy(comp->damage_region);
    free(comp);
}

nx_window_t* nx_compositor_create_window(nx_compositor_t* comp, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t flags) {
    if (!comp) return NULL;
    
    nx_window_t* win = (nx_window_t*)calloc(1, sizeof(nx_window_t));
    if (!win) return NULL;
    
    win->id = g_next_win_id++;
    win->x = x; win->y = y;
    win->width = w; win->height = h;
    win->flags = flags;
    win->opacity = 255;
    
    /* Insert at the top of the Z-order (head) */
    if (!comp->head) {
        comp->head = comp->tail = win;
    } else {
        win->next = comp->head;
        comp->head->prev = win;
        comp->head = win;
    }
    
    nx_compositor_damage_rect(comp, x, y, w, h);
    return win;
}

void nx_compositor_destroy_window(nx_compositor_t* comp, nx_window_t* win) {
    if (!comp || !win) return;
    
    nx_compositor_damage_rect(comp, win->x, win->y, win->width, win->height);
    
    if (win->prev) win->prev->next = win->next;
    else comp->head = win->next;
    
    if (win->next) win->next->prev = win->prev;
    else comp->tail = win->prev;
    
    free(win);
}

void nx_compositor_set_window_pos(nx_compositor_t* comp, nx_window_t* win, int32_t x, int32_t y) {
    if (!comp || !win) return;
    
    /* Damage old rect */
    nx_compositor_damage_rect(comp, win->x, win->y, win->width, win->height);
    
    win->x = x;
    win->y = y;
    
    /* Damage new rect */
    nx_compositor_damage_rect(comp, win->x, win->y, win->width, win->height);
}

void nx_compositor_raise_window(nx_compositor_t* comp, nx_window_t* win) {
    if (!comp || !win || comp->head == win) return;
    
    if (win->prev) win->prev->next = win->next;
    if (win->next) win->next->prev = win->prev;
    else comp->tail = win->prev;
    
    win->prev = NULL;
    win->next = comp->head;
    comp->head->prev = win;
    comp->head = win;
    
    nx_compositor_damage_rect(comp, win->x, win->y, win->width, win->height);
}

void nx_compositor_damage_rect(nx_compositor_t* comp, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!comp) return;
    nx_region_add_rect(comp->damage_region, x, y, w, h);
}

/* =======================================================
 * Rendering Pipeline
 * ======================================================= */

static void blend_pixel(uint32_t* dest, uint32_t src, uint8_t global_alpha) {
    /* Fixed 32-bit ARGB blend */
    uint8_t sa = (src >> 24) & 0xFF;
    if (sa == 0) return;
    
    if (global_alpha != 255) {
        sa = (sa * global_alpha) / 255;
    }
    
    if (sa == 255) {
        *dest = src;
        return;
    }
    
    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    
    uint32_t d = *dest;
    uint8_t dr = (d >> 16) & 0xFF;
    uint8_t dg = (d >> 8) & 0xFF;
    uint8_t db = d & 0xFF;
    
    uint8_t inv_sa = 255 - sa;
    
    *dest = 0xFF000000 | 
            (((sr * sa + dr * inv_sa) >> 8) << 16) |
            (((sg * sa + dg * inv_sa) >> 8) << 8) |
            ((sb * sa + db * inv_sa) >> 8);
}

void nx_compositor_render_frame(nx_compositor_t* comp) {
    if (!comp || !comp->backbuffer || comp->damage_region->count == 0) return;
    
    /* Start from the bottom-most window and paint upwards (Painter's Algorithm) */
    nx_window_t* win = comp->tail;
    
    while (win) {
        if ((win->flags & NX_WIN_FLAG_VISIBLE) && win->shm_buffer) {
            
            /* Only redraw if intersecting a damage region */
            if (nx_region_intersects(comp->damage_region, win->x, win->y, win->width, win->height)) {
                
                int32_t start_y = max_i32(0, win->y);
                int32_t end_y = min_i32(comp->screen_height, win->y + win->height);
                int32_t start_x = max_i32(0, win->x);
                int32_t end_x = min_i32(comp->screen_width, win->x + win->width);
                
                uint8_t* dst_ptr = (uint8_t*)comp->backbuffer;
                uint8_t* src_ptr = (uint8_t*)win->shm_buffer;
                
                for (int32_t y = start_y; y < end_y; y++) {
                    uint32_t* dst_row = (uint32_t*)(dst_ptr + (y * comp->pitch));
                    uint32_t* src_row = (uint32_t*)(src_ptr + ((y - win->y) * win->pitch));
                    
                    for (int32_t x = start_x; x < end_x; x++) {
                        if (win->flags & NX_WIN_FLAG_TRANSPARENT) {
                            blend_pixel(&dst_row[x], src_row[x - win->x], win->opacity);
                        } else {
                            dst_row[x] = src_row[x - win->x];
                        }
                    }
                }
            }
        }
        win = win->prev;
    }
    
    /* Clear damages for next frame */
    nx_region_clear(comp->damage_region);
}
