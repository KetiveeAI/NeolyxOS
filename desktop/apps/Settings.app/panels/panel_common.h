/*
 * NeolyxOS Settings Panels - Common Utilities
 * 
 * Shared macros and helpers for all panel implementations.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef PANEL_COMMON_H
#define PANEL_COMMON_H

#include "../settings.h"

/* Standard padding values */
#define PANEL_PADDING       24.0f
#define CARD_PADDING        16.0f
#define ITEM_GAP            12.0f
#define SECTION_GAP         20.0f

/* Create a standard setting row: label on left, control on right */
static inline rx_view* setting_row(const char* label_text, rx_view* control) {
    rx_view* row = hstack_new(16.0f);
    if (!row) return NULL;
    
    rx_text_view* label = text_view_new(label_text);
    if (label) {
        label->color = NX_COLOR_TEXT;
        view_add_child(row, (rx_view*)label);
    }
    
    /* Spacer pushes control to right */
    rx_view* sp = spacer_new();
    if (sp) {
        view_add_child(row, sp);
    }
    
    if (control) {
        view_add_child(row, control);
    }
    
    return row;
}

/* Create a toggle switch for boolean settings */
static inline rx_view* setting_toggle(const char* label_text, bool initial_value) {
    rx_button_view* toggle_btn = button_view_new(initial_value ? "ON" : "OFF");
    if (toggle_btn) {
        if (initial_value) {
            toggle_btn->normal_color = NX_COLOR_PRIMARY;
        } else {
            toggle_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        }
    }
    return setting_row(label_text, (rx_view*)toggle_btn);
}

/* Create a divider line between sections */
static inline rx_view* section_divider(void) {
    return divider_new(true);
}

/* Panel header with title */
static inline rx_text_view* panel_header(const char* title) {
    rx_text_view* header = text_view_new(title);
    if (header) {
        text_view_set_font_size(header, 28.0f);
        header->font_weight = 700;
        header->color = NX_COLOR_TEXT;
    }
    return header;
}

#endif /* PANEL_COMMON_H */
