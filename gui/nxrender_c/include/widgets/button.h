/*
 * NeolyxOS - NXRender Button Widget
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_BUTTON_H
#define NXRENDER_BUTTON_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*nx_button_callback_t)(void* user_data);

typedef struct {
    nx_color_t background;
    nx_color_t background_hover;
    nx_color_t background_pressed;
    nx_color_t text_color;
    uint32_t corner_radius;
    uint32_t padding;
    uint32_t font_size;
} nx_button_style_t;

typedef struct {
    nx_widget_t base;
    char* label;
    nx_button_style_t style;
    nx_button_callback_t on_click;
    void* callback_data;
} nx_button_t;

/* Default button style */
nx_button_style_t nx_button_default_style(void);

/* Create button */
nx_button_t* nx_button_create(const char* label);

/* Set callback */
void nx_button_set_on_click(nx_button_t* btn, nx_button_callback_t cb, void* data);

/* Set style */
void nx_button_set_style(nx_button_t* btn, nx_button_style_t style);

/* Set label */
void nx_button_set_label(nx_button_t* btn, const char* label);

#ifdef __cplusplus
}
#endif
#endif
