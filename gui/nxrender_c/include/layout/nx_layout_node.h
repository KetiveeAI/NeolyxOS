/*
 * NeolyxOS - NXToolkit Layout Engine
 * Abstract Node Shadow Tree
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_LAYOUT_NODE_H
#define NX_LAYOUT_NODE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NX_LAYOUT_TYPE_FLEX,
    NX_LAYOUT_TYPE_GRID,
    NX_LAYOUT_TYPE_ABSOLUTE,
    NX_LAYOUT_TYPE_TEXT
} nx_layout_type_t;

typedef struct nx_layout_node {
    nx_layout_type_t type;
    
    /* Intrinsic sizes (e.g. text bounds) */
    float intrinsic_width;
    float intrinsic_height;
    
    /* Configured layout pointers dynamically sized (eliminating stubs) */
    void* layout_config;
    
    /* Constrictions inputs */
    float margin[4];  /* top, right, bottom, left */
    float padding[4];
    float border[4];
    
    /* Configured styles */
    float flex_grow;
    float flex_shrink;
    float flex_basis;
    
    /* Output constraints */
    float computed_x;
    float computed_y;
    float computed_width;
    float computed_height;
    
    /* Tree links */
    struct nx_layout_node* parent;
    struct nx_layout_node** children;
    uint32_t child_count;
    uint32_t child_capacity;
    
    /* Dirtiness Tracking */
    bool is_dirty;
    
    void* context_widget; /* Points back to nx_widget_t */
} nx_layout_node_t;

/* Node Construction */
nx_layout_node_t* nx_layout_node_create(nx_layout_type_t type);
void nx_layout_node_destroy(nx_layout_node_t* node);

/* Tree Management */
void nx_layout_node_add_child(nx_layout_node_t* parent, nx_layout_node_t* child);
void nx_layout_node_remove_child(nx_layout_node_t* parent, nx_layout_node_t* child);
void nx_layout_node_mark_dirty(nx_layout_node_t* node);

/* Evaluation (Deep pass algorithms) */
void nx_layout_node_calculate(nx_layout_node_t* root, float available_width, float available_height);

#ifdef __cplusplus
}
#endif
#endif
