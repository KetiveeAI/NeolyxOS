/*
 * NeolyxOS - NXToolkit Layout Engine
 * CSS Flexbox Advanced Resolver
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/nx_layout_flex.h"
#include <stddef.h>

extern void* malloc(size_t size);
extern void* calloc(size_t n, size_t size);
extern void free(void* ptr);

void nx_layout_flex_set_config(nx_layout_node_t* node, nx_flex_config_t config) {
    if (!node) return;
    
    if (!node->layout_config) {
        node->layout_config = malloc(sizeof(nx_flex_config_t));
    }
    
    if (node->layout_config) {
        nx_flex_config_t* cfg = (nx_flex_config_t*)node->layout_config;
        *cfg = config;
    }
    
    node->type = NX_LAYOUT_TYPE_FLEX;
}

void nx_layout_flex_calculate(nx_layout_node_t* root, float available_width, float available_height) {
    if (!root || root->child_count == 0) return;
    if (!root->layout_config) return;
    
    nx_flex_config_t* cfg = (nx_flex_config_t*)root->layout_config;
    bool horizontal = (cfg->direction == NX_FLEX_ROW || cfg->direction == NX_FLEX_ROW_REVERSE);
    
    float padding_main_start = horizontal ? root->padding[3] : root->padding[0];
    float padding_main_end = horizontal ? root->padding[1] : root->padding[2];
    float padding_cross_start = horizontal ? root->padding[0] : root->padding[3];
    float padding_cross_end = horizontal ? root->padding[2] : root->padding[1];
    
    float avail_main = (horizontal ? available_width : available_height) - padding_main_start - padding_main_end;
    
    float current_main = padding_main_start;
    float current_cross = padding_cross_start;
    
    float max_cross_line = 0.0f;
    uint32_t line_child_count = 0;
    float line_main_total = 0.0f;
    uint32_t line_start_idx = 0;
    
    for (uint32_t i = 0; i < root->child_count; i++) {
        nx_layout_node_t* child = root->children[i];
        
        float child_main = horizontal ? child->intrinsic_width : child->intrinsic_height;
        float child_cross = horizontal ? child->intrinsic_height : child->intrinsic_width;
        
        if (child->flex_basis > 0) child_main = child->flex_basis;
        
        /* Flex Wrap Logic */
        if (cfg->wrap && line_child_count > 0 && 
            (line_main_total + child_main + cfg->gap > avail_main)) {
            
            /* Apply Justice Content to current line */
            float remaining = avail_main - line_main_total;
            float offset = padding_main_start;
            float spacing = cfg->gap;
            
            if (cfg->justify == NX_JUSTIFY_END) offset += remaining;
            else if (cfg->justify == NX_JUSTIFY_CENTER) offset += remaining / 2.0f;
            else if (cfg->justify == NX_JUSTIFY_SPACE_BETWEEN && line_child_count > 1) {
                spacing = remaining / (float)(line_child_count - 1);
            } else if (cfg->justify == NX_JUSTIFY_SPACE_EVENLY) {
                spacing = remaining / (float)(line_child_count + 1);
                offset += spacing;
            }
            
            /* Finalize node coordinates for the line */
            for (uint32_t j = line_start_idx; j < i; j++) {
                nx_layout_node_t* line_child = root->children[j];
                float c_main = horizontal ? line_child->computed_width : line_child->computed_height;
                float c_cross = horizontal ? line_child->computed_height : line_child->computed_width;
                
                /* Align items (Cross Axis) */
                float cross_offset = current_cross;
                if (cfg->align_items == NX_ALIGN_END) cross_offset += max_cross_line - c_cross;
                else if (cfg->align_items == NX_ALIGN_CENTER) cross_offset += (max_cross_line - c_cross) / 2.0f;
                else if (cfg->align_items == NX_ALIGN_STRETCH) {
                    c_cross = max_cross_line;
                    if (horizontal) line_child->computed_height = c_cross;
                    else line_child->computed_width = c_cross;
                }
                
                if (horizontal) {
                    line_child->computed_x = offset;
                    line_child->computed_y = cross_offset;
                } else {
                    line_child->computed_y = offset;
                    line_child->computed_x = cross_offset;
                }
                offset += c_main + spacing;
            }
            
            current_cross += max_cross_line + cfg->gap;
            max_cross_line = 0.0f;
            line_child_count = 0;
            line_main_total = 0.0f;
            line_start_idx = i;
            current_main = padding_main_start;
        }
        
        if (horizontal) {
            child->computed_width = child_main;
            child->computed_height = child_cross;
        } else {
            child->computed_height = child_main;
            child->computed_width = child_cross;
        }
        
        line_main_total += child_main;
        if (line_child_count > 0) line_main_total += cfg->gap;
        if (child_cross > max_cross_line) max_cross_line = child_cross;
        
        line_child_count++;
    }
    
    /* Flush final line */
    if (line_child_count > 0) {
        float remaining = avail_main - line_main_total;
        float offset = padding_main_start;
        float spacing = cfg->gap;
        
        if (cfg->justify == NX_JUSTIFY_END) offset += remaining;
        else if (cfg->justify == NX_JUSTIFY_CENTER) offset += remaining / 2.0f;
        else if (cfg->justify == NX_JUSTIFY_SPACE_BETWEEN && line_child_count > 1) {
            spacing = remaining / (float)(line_child_count - 1);
        } else if (cfg->justify == NX_JUSTIFY_SPACE_EVENLY) {
            spacing = remaining / (float)(line_child_count + 1);
            offset += spacing;
        }
        
        for (uint32_t j = line_start_idx; j < root->child_count; j++) {
            nx_layout_node_t* line_child = root->children[j];
            float c_main = horizontal ? line_child->computed_width : line_child->computed_height;
            float c_cross = horizontal ? line_child->computed_height : line_child->computed_width;
            
            float cross_offset = current_cross;
            if (cfg->align_items == NX_ALIGN_END) cross_offset += max_cross_line - c_cross;
            else if (cfg->align_items == NX_ALIGN_CENTER) cross_offset += (max_cross_line - c_cross) / 2.0f;
            else if (cfg->align_items == NX_ALIGN_STRETCH) {
                c_cross = max_cross_line;
                if (horizontal) line_child->computed_height = c_cross;
                else line_child->computed_width = c_cross;
            }
            
            if (horizontal) {
                line_child->computed_x = offset;
                line_child->computed_y = cross_offset;
            } else {
                line_child->computed_y = offset;
                line_child->computed_x = cross_offset;
            }
            offset += c_main + spacing;
        }
        current_cross += max_cross_line;
    }
    
    if (horizontal) {
        root->computed_height = current_cross + padding_cross_end;
        root->computed_width = available_width;
    } else {
        root->computed_width = current_cross + padding_cross_end;
        root->computed_height = available_height;
    }
}
