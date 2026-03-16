/*
 * IconLay Color Picker - Header
 * Uses rx_color_picker_view from Reox UI framework
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ICONLAY_COLORPICKER_H
#define ICONLAY_COLORPICKER_H

#include <stdbool.h>
#include <stdint.h>
#include "reox_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct iconlay_color_picker iconlay_color_picker_t;
typedef struct nx_context nx_context_t;

/* Create color picker with initial color */
iconlay_color_picker_t* iconlay_color_picker_create(rx_color initial_color);

/* Destroy color picker */
void iconlay_color_picker_destroy(iconlay_color_picker_t* cp);

/* Show/hide */
void iconlay_color_picker_set_visible(iconlay_color_picker_t* cp, bool visible);
bool iconlay_color_picker_is_visible(iconlay_color_picker_t* cp);

/* Position and size */
void iconlay_color_picker_set_position(iconlay_color_picker_t* cp, float x, float y);
void iconlay_color_picker_set_size(iconlay_color_picker_t* cp, float w, float h);

/* Get/set color */
rx_color iconlay_color_picker_get_color(iconlay_color_picker_t* cp);
void iconlay_color_picker_set_color(iconlay_color_picker_t* cp, rx_color color);

/* Set selection callback */
void iconlay_color_picker_set_callback(iconlay_color_picker_t* cp,
                                        void (*callback)(rx_color, void*),
                                        void* user_data);

/* Render to nxgfx context */
void iconlay_color_picker_render(iconlay_color_picker_t* cp, nx_context_t* ctx);

/* Handle mouse click - returns true if event was consumed */
bool iconlay_color_picker_handle_click(iconlay_color_picker_t* cp, 
                                        float x, float y, bool pressed);

/* Utility functions */
uint32_t iconlay_color_to_argb(rx_color color);
rx_color iconlay_argb_to_color(uint32_t argb);
void iconlay_color_to_hex_string(rx_color color, char* buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ICONLAY_COLORPICKER_H */
