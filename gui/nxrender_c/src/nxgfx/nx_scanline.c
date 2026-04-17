/*
 * NeolyxOS - NXGFX Active Edge Table Scanline Rasterizer
 * Copyright (c) 2025 KetiveeAI
 */

#include "nxgfx/nxgfx_path.h"
#include <stddef.h>

extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);

/* Simplified memory allocation placeholders to avoid dependencies.
   In a production NeolyxOS build, these will map to kmalloc or pool allocators. */
extern void* malloc(size_t size);
extern void free(void* ptr);

/* Helper: Swap pointers */
static void swap_edges(nx_edge_t** a, nx_edge_t** b) {
    nx_edge_t* temp = *a;
    *a = *b;
    *b = temp;
}

/* Helper: Sort edge list by current X */
static void sort_active_edges(nx_edge_t** head) {
    if (!*head || !(*head)->next) return;
    
    bool swapped;
    do {
        swapped = false;
        nx_edge_t** ptr = head;
        while (*ptr && (*ptr)->next) {
            if ((*ptr)->x > (*ptr)->next->x) {
                nx_edge_t* temp = (*ptr)->next;
                (*ptr)->next = temp->next;
                temp->next = *ptr;
                *ptr = temp;
                swapped = true;
            }
            ptr = &((*ptr)->next);
        }
    } while (swapped);
}

void nx_scanline_rasterize(const nx_vec2_t* points, uint32_t count, 
                           const nx_bounds_t* clip, nx_winding_rule_t rule,
                           nx_scanline_span_cb_t span_cb, void* user_data) {
    if (!span_cb || count < 3) return;

    /* 1. Find min and max Y to establish bounds */
    float min_y = points[0].y;
    float max_y = points[0].y;
    
    for (uint32_t i = 1; i < count; i++) {
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    
    int32_t start_y = (int32_t)min_y;
    int32_t end_y = (int32_t)max_y;
    
    /* Apply optional clipping */
    if (clip) {
        if (start_y < (int32_t)clip->min.y) start_y = (int32_t)clip->min.y;
        if (end_y >= (int32_t)clip->max.y) end_y = (int32_t)clip->max.y - 1;
    }
    
    if (start_y > end_y) return;
    
    uint32_t table_size = end_y - start_y + 1;
    nx_edge_t** edge_table = (nx_edge_t**)calloc(table_size, sizeof(nx_edge_t*));
    if (!edge_table) return;
    
    /* 3. Build edges */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t next = (i + 1) % count;
        nx_vec2_t p1 = points[i];
        nx_vec2_t p2 = points[next];
        
        if ((int32_t)p1.y == (int32_t)p2.y) continue;
        
        int32_t dir = 1;
        if (p1.y > p2.y) {
            nx_vec2_t temp = p1; p1 = p2; p2 = temp;
            dir = -1;
        }
        
        int32_t y1 = (int32_t)p1.y;
        int32_t y2 = (int32_t)p2.y;
        
        if (clip) {
            if (y2 < (int32_t)clip->min.y || y1 >= (int32_t)clip->max.y) continue;
        }
        
        nx_edge_t* edge = (nx_edge_t*)malloc(sizeof(nx_edge_t));
        if (!edge) continue;
        
        edge->y_max = y2;
        edge->x = p1.x;
        edge->dx_dy = (p2.x - p1.x) / (p2.y - p1.y);
        edge->winding_dir = dir;
        
        if (y1 >= start_y && y1 <= end_y) {
            uint32_t bucket = y1 - start_y;
            edge->next = edge_table[bucket];
            edge_table[bucket] = edge;
        } else {
            free(edge);
        }
    }
    
    nx_edge_t* active_edges = NULL;
    
    for (int32_t y = start_y; y <= end_y; y++) {
        uint32_t bucket = y - start_y;
        
        /* Insert new edges */
        nx_edge_t* curr = edge_table[bucket];
        while (curr) {
            nx_edge_t* next = curr->next;
            curr->next = active_edges;
            active_edges = curr;
            curr = next;
        }
        
        /* Remove expired edges */
        nx_edge_t** ptr = &active_edges;
        while (*ptr) {
            if ((*ptr)->y_max <= y) {
                nx_edge_t* del = *ptr;
                *ptr = (*ptr)->next;
                free(del);
            } else {
                ptr = &((*ptr)->next);
            }
        }
        
        /* Sorted AET */
        sort_active_edges(&active_edges);
        
        /* Winding rule processing to emit horizontal spans */
        int winding_count = 0;
        bool in_shape = false;
        int32_t span_x_start = 0;
        
        curr = active_edges;
        while (curr) {
            if (rule == NX_WINDING_EVEN_ODD) {
                in_shape = !in_shape;
            } else {
                winding_count += curr->winding_dir;
                in_shape = (winding_count != 0);
            }
            
            if (in_shape && span_x_start == 0) {
                span_x_start = (int32_t)curr->x;
            } else if (!in_shape && span_x_start != 0) {
                int32_t span_x_end = (int32_t)curr->x;
                
                /* Subpixel clip processing block bounds */
                if (clip) {
                    if (span_x_start < (int32_t)clip->min.x) span_x_start = (int32_t)clip->min.x;
                    if (span_x_end >= (int32_t)clip->max.x) span_x_end = (int32_t)clip->max.x - 1;
                }
                
                if (span_x_start <= span_x_end) {
                    span_cb(user_data, y, span_x_start, span_x_end);
                }
                span_x_start = 0;
            }
            
            curr->x += curr->dx_dy;
            curr = curr->next;
        }
    }
    
    while (active_edges) {
        nx_edge_t* next = active_edges->next;
        free(active_edges);
        active_edges = next;
    }
    
    free(edge_table);
}
