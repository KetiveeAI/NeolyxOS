/*
 * NeolyxOS - NXRender Slider Widget
 * Horizontal/vertical slider with value range
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_SLIDER_H
#define NXRENDER_SLIDER_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NX_SLIDER_HORIZONTAL,
    NX_SLIDER_VERTICAL
} nx_slider_direction_t;

typedef struct {
    nx_widget_t base;
    float value;
    float min_value;
    float max_value;
    float step;
    nx_slider_direction_t direction;
    nx_color_t track_color;
    nx_color_t fill_color;
    nx_color_t thumb_color;
    uint32_t track_height;
    uint32_t thumb_radius;
    bool dragging;
    void (*on_change)(float value, void* user_data);
    void* callback_data;
} nx_slider_t;

nx_slider_t* nx_slider_create(float min, float max, float initial);
void nx_slider_set_value(nx_slider_t* slider, float value);
float nx_slider_get_value(nx_slider_t* slider);
void nx_slider_set_step(nx_slider_t* slider, float step);
void nx_slider_set_direction(nx_slider_t* slider, nx_slider_direction_t dir);
void nx_slider_set_on_change(nx_slider_t* slider, void (*callback)(float, void*), void* data);

#ifdef __cplusplus
}
#endif
#endif
