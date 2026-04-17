/*
 * NeolyxOS - NXToolkit Layout Engine
 * Advanced Node-based Grid Execution
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/nx_layout_grid.h"
#include "layout/nx_layout_math.h"
#include <stddef.h>

extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);

void nx_layout_grid_set_config(nx_layout_node_t* node, nx_grid_config_t config) {
    if (!node) return;
    
    if (!node->layout_config) {
        node->layout_config = malloc(sizeof(nx_grid_config_t));
    }
    
    if (node->layout_config) {
        nx_grid_config_t* cfg = (nx_grid_config_t*)node->layout_config;
        *cfg = config;
    }
    
    node->type = NX_LAYOUT_TYPE_GRID;
}

void nx_layout_grid_calculate(nx_layout_node_t* root, float available_width, float available_height) {
    if (!root || root->child_count == 0) return;
    if (!root->layout_config) return;
    
    nx_grid_config_t* cfg = (nx_grid_config_t*)root->layout_config;
    
    uint32_t cols = cfg->cols;
    if (cols == 0) cols = 1; 
    
    uint32_t rows = cfg->rows;
    if (rows == 0) rows = (root->child_count + cols - 1) / cols;
    
    float total_col_gap = (cols - 1) * cfg->col_gap;
    float total_row_gap = (rows - 1) * cfg->row_gap;
    
    float avail_w_for_cells = available_width - root->padding[1] - root->padding[3];
    float cell_width = nx_layout_math_clamp((avail_w_for_cells - total_col_gap) / (float)cols, 0.0f, NX_FLOAT_MAX);
    float cell_height = 0.0f;
    
    if (available_height > 0.0f && available_height != NX_FLOAT_MAX) {
        float avail_h_for_cells = available_height - root->padding[0] - root->padding[2];
        cell_height = nx_layout_math_clamp((avail_h_for_cells - total_row_gap) / (float)rows, 0.0f, NX_FLOAT_MAX);
    }
    
    for (uint32_t i = 0; i < root->child_count; i++) {
        uint32_t c = i % cols;
        uint32_t r = i / cols;
        
        nx_layout_node_t* child = root->children[i];
        
        float ch = cell_height > 0.0f ? cell_height : child->intrinsic_height;
        
        float x_offset = nx_layout_math_round_pixel(root->padding[3] + c * (cell_width + cfg->col_gap), 1.0f);
        float y_offset = nx_layout_math_round_pixel(root->padding[0] + r * (ch + cfg->row_gap), 1.0f);
        
        child->computed_x = x_offset;
        child->computed_y = y_offset;
        child->computed_width = nx_layout_math_round_pixel(cell_width, 1.0f);
        child->computed_height = nx_layout_math_round_pixel(ch, 1.0f);
    }
    
    float total_h = root->padding[0] + root->padding[2] + total_row_gap;
    for (uint32_t r = 0; r < rows; r++) {
        if (cell_height > 0.0f) {
            total_h += cell_height;
        } else {
            float max_h_in_row = 0.0f;
            for (uint32_t c = 0; c < cols; c++) {
                uint32_t idx = r * cols + c;
                if (idx < root->child_count) {
                    if (root->children[idx]->intrinsic_height > max_h_in_row)
                        max_h_in_row = root->children[idx]->intrinsic_height;
                }
            }
            total_h += max_h_in_row;
        }
    }
    
    root->computed_width = available_width;
    root->computed_height = nx_layout_math_round_pixel(total_h, 1.0f);
}
