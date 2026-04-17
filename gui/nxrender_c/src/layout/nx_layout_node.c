/*
 * NeolyxOS - NXToolkit Layout Engine
 * Node Shadow Tree Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/nx_layout_node.h"
#include <stddef.h>

extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);

nx_layout_node_t* nx_layout_node_create(nx_layout_type_t type) {
    nx_layout_node_t* node = (nx_layout_node_t*)calloc(1, sizeof(nx_layout_node_t));
    if (!node) return NULL;
    
    node->type = type;
    node->is_dirty = true;
    
    node->child_capacity = 4;
    node->children = (nx_layout_node_t**)malloc(sizeof(nx_layout_node_t*) * node->child_capacity);
    
    return node;
}

void nx_layout_node_destroy(nx_layout_node_t* node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->child_count; i++) {
        nx_layout_node_destroy(node->children[i]);
    }
    if (node->children) free(node->children);
    free(node);
}

void nx_layout_node_add_child(nx_layout_node_t* parent, nx_layout_node_t* child) {
    if (!parent || !child) return;
    
    if (parent->child_count >= parent->child_capacity) {
        uint32_t new_cap = parent->child_capacity * 2;
        nx_layout_node_t** new_buf = (nx_layout_node_t**)malloc(sizeof(nx_layout_node_t*) * new_cap);
        for (uint32_t i = 0; i < parent->child_count; i++) {
            new_buf[i] = parent->children[i];
        }
        free(parent->children);
        parent->children = new_buf;
        parent->child_capacity = new_cap;
    }
    
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    
    nx_layout_node_mark_dirty(parent);
}

void nx_layout_node_remove_child(nx_layout_node_t* parent, nx_layout_node_t* child) {
    if (!parent || !child) return;
    
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            for (uint32_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j+1];
            }
            parent->child_count--;
            child->parent = NULL;
            nx_layout_node_mark_dirty(parent);
            return;
        }
    }
}

void nx_layout_node_mark_dirty(nx_layout_node_t* node) {
    while (node && !node->is_dirty) {
        node->is_dirty = true;
        node = node->parent;
    }
}

/* Base stub handler - will interlink with nx_layout_flex/grid algorithms */
void nx_layout_node_calculate(nx_layout_node_t* root, float available_width, float available_height) {
    if (!root || !root->is_dirty) return;
    
    root->computed_width = available_width;
    root->computed_height = available_height;
    
    if (root->type == NX_LAYOUT_TYPE_FLEX) {
        /* Route to nx_layout_flex_calculate(root) in the flex module */
    } else if (root->type == NX_LAYOUT_TYPE_GRID) {
        /* Route to nx_layout_grid_calculate(root) */
    }
    
    root->is_dirty = false;
}
