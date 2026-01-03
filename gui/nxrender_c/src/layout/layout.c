/*
 * NeolyxOS - NXRender Layout Engine Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "layout/layout.h"
#include <stdlib.h>

nx_flex_layout_t nx_flex_row(void) {
    return (nx_flex_layout_t){
        .direction = NX_FLEX_ROW,
        .justify = NX_JUSTIFY_START,
        .align = NX_ALIGN_CENTER,
        .gap = 8,
        .wrap = false
    };
}

nx_flex_layout_t nx_flex_column(void) {
    return (nx_flex_layout_t){
        .direction = NX_FLEX_COLUMN,
        .justify = NX_JUSTIFY_START,
        .align = NX_ALIGN_STRETCH,
        .gap = 8,
        .wrap = false
    };
}

nx_stack_layout_t nx_stack_center(void) {
    return (nx_stack_layout_t){
        .horizontal = NX_ALIGN_CENTER,
        .vertical = NX_ALIGN_CENTER
    };
}

nx_constraints_t nx_constraints_tight(uint32_t w, uint32_t h) {
    return (nx_constraints_t){ w, w, h, h };
}

nx_constraints_t nx_constraints_loose(uint32_t max_w, uint32_t max_h) {
    return (nx_constraints_t){ 0, max_w, 0, max_h };
}

nx_constraints_t nx_constraints_expand(void) {
    return (nx_constraints_t){ 0, UINT32_MAX, 0, UINT32_MAX };
}

void nx_layout_flex(nx_widget_t* container, nx_flex_layout_t layout, nx_rect_t bounds) {
    if (!container || container->child_count == 0) return;
    
    bool horizontal = (layout.direction == NX_FLEX_ROW || layout.direction == NX_FLEX_ROW_REVERSE);
    bool reverse = (layout.direction == NX_FLEX_ROW_REVERSE || layout.direction == NX_FLEX_COLUMN_REVERSE);
    
    /* Measure children */
    uint32_t total_main = 0;
    nx_size_t* sizes = malloc(container->child_count * sizeof(nx_size_t));
    for (size_t i = 0; i < container->child_count; i++) {
        sizes[i] = nx_widget_measure(container->children[i], (nx_size_t){bounds.width, bounds.height});
        total_main += horizontal ? sizes[i].width : sizes[i].height;
    }
    total_main += layout.gap * (container->child_count - 1);
    
    /* Calculate starting position and spacing */
    uint32_t available = horizontal ? bounds.width : bounds.height;
    int32_t offset = 0;
    int32_t spacing = layout.gap;
    
    switch (layout.justify) {
        case NX_JUSTIFY_START: offset = 0; break;
        case NX_JUSTIFY_END: offset = available - total_main; break;
        case NX_JUSTIFY_CENTER: offset = (available - total_main) / 2; break;
        case NX_JUSTIFY_SPACE_BETWEEN:
            if (container->child_count > 1)
                spacing = (available - total_main + layout.gap * (container->child_count - 1)) / (container->child_count - 1);
            break;
        case NX_JUSTIFY_SPACE_AROUND:
            spacing = (available - total_main + layout.gap * (container->child_count - 1)) / (container->child_count * 2);
            offset = spacing;
            break;
        case NX_JUSTIFY_SPACE_EVENLY:
            spacing = (available - total_main + layout.gap * (container->child_count - 1)) / (container->child_count + 1);
            offset = spacing;
            break;
    }
    
    if (reverse) offset = available - offset;
    
    /* Position children */
    for (size_t i = 0; i < container->child_count; i++) {
        size_t idx = reverse ? (container->child_count - 1 - i) : i;
        nx_widget_t* child = container->children[idx];
        nx_size_t sz = sizes[idx];
        
        int32_t cross_offset = 0;
        uint32_t cross_size = horizontal ? sz.height : sz.width;
        uint32_t cross_available = horizontal ? bounds.height : bounds.width;
        
        switch (layout.align) {
            case NX_ALIGN_START: cross_offset = 0; break;
            case NX_ALIGN_END: cross_offset = cross_available - cross_size; break;
            case NX_ALIGN_CENTER: cross_offset = (cross_available - cross_size) / 2; break;
            case NX_ALIGN_STRETCH: cross_size = cross_available; break;
        }
        
        nx_rect_t child_bounds;
        if (horizontal) {
            child_bounds = (nx_rect_t){ bounds.x + offset, bounds.y + cross_offset, sz.width, cross_size };
            offset += reverse ? -(int32_t)(sz.width + spacing) : (int32_t)(sz.width + spacing);
        } else {
            child_bounds = (nx_rect_t){ bounds.x + cross_offset, bounds.y + offset, cross_size, sz.height };
            offset += reverse ? -(int32_t)(sz.height + spacing) : (int32_t)(sz.height + spacing);
        }
        
        nx_widget_layout(child, child_bounds);
    }
    
    free(sizes);
}

void nx_layout_stack(nx_widget_t* container, nx_stack_layout_t layout, nx_rect_t bounds) {
    if (!container) return;
    
    for (size_t i = 0; i < container->child_count; i++) {
        nx_widget_t* child = container->children[i];
        nx_size_t sz = nx_widget_measure(child, (nx_size_t){bounds.width, bounds.height});
        
        int32_t x = bounds.x, y = bounds.y;
        
        switch (layout.horizontal) {
            case NX_ALIGN_START: x = bounds.x; break;
            case NX_ALIGN_END: x = bounds.x + bounds.width - sz.width; break;
            case NX_ALIGN_CENTER: x = bounds.x + (bounds.width - sz.width) / 2; break;
            case NX_ALIGN_STRETCH: sz.width = bounds.width; break;
        }
        
        switch (layout.vertical) {
            case NX_ALIGN_START: y = bounds.y; break;
            case NX_ALIGN_END: y = bounds.y + bounds.height - sz.height; break;
            case NX_ALIGN_CENTER: y = bounds.y + (bounds.height - sz.height) / 2; break;
            case NX_ALIGN_STRETCH: sz.height = bounds.height; break;
        }
        
        nx_widget_layout(child, (nx_rect_t){x, y, sz.width, sz.height});
    }
}
