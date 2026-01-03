/*
 * NeolyxOS - NXRender TextField Widget
 * Text input field with cursor and selection
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_TEXTFIELD_H
#define NXRENDER_TEXTFIELD_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*nx_textfield_callback_t)(const char* text, void* user_data);

typedef struct {
    nx_color_t background;
    nx_color_t background_focused;
    nx_color_t text_color;
    nx_color_t placeholder_color;
    nx_color_t cursor_color;
    nx_color_t border_color;
    nx_color_t border_focused;
    uint32_t corner_radius;
    uint32_t padding;
    uint32_t font_size;
    uint32_t border_width;
} nx_textfield_style_t;

typedef struct {
    nx_widget_t base;
    char* text;
    char* placeholder;
    size_t text_capacity;
    size_t cursor_pos;
    size_t selection_start;
    size_t selection_end;
    nx_textfield_style_t style;
    nx_textfield_callback_t on_change;
    nx_textfield_callback_t on_submit;
    void* callback_data;
    bool password_mode;
} nx_textfield_t;

/* Default style */
nx_textfield_style_t nx_textfield_default_style(void);

/* Create text field */
nx_textfield_t* nx_textfield_create(const char* placeholder);

/* Text operations */
void nx_textfield_set_text(nx_textfield_t* tf, const char* text);
const char* nx_textfield_get_text(nx_textfield_t* tf);
void nx_textfield_set_placeholder(nx_textfield_t* tf, const char* placeholder);

/* Callbacks */
void nx_textfield_set_on_change(nx_textfield_t* tf, nx_textfield_callback_t cb, void* data);
void nx_textfield_set_on_submit(nx_textfield_t* tf, nx_textfield_callback_t cb, void* data);

/* Options */
void nx_textfield_set_password_mode(nx_textfield_t* tf, bool enabled);
void nx_textfield_set_style(nx_textfield_t* tf, nx_textfield_style_t style);

#ifdef __cplusplus
}
#endif
#endif
