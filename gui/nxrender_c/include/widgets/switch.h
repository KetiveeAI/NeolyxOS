/*
 * NeolyxOS - NXRender Switch Widget
 * iOS/macOS-style toggle switch
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_SWITCH_H
#define NXRENDER_SWITCH_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_switch nx_switch_t;

/* Switch toggled callback */
typedef void (*nx_switch_toggled_fn)(nx_switch_t* sw, bool is_on, void* user_data);

struct nx_switch {
    nx_widget_t base;
    bool is_on;
    nx_color_t on_color;
    nx_color_t off_color;
    nx_color_t thumb_color;
    uint32_t width;
    uint32_t height;
    float animation_progress;  /* 0.0 = off, 1.0 = on */
    nx_switch_toggled_fn on_toggled;
    void* callback_data;
};

/* Create/destroy */
nx_switch_t* nx_switch_create(void);
void nx_switch_destroy(nx_switch_t* sw);

/* State */
void nx_switch_set_on(nx_switch_t* sw, bool is_on);
bool nx_switch_is_on(nx_switch_t* sw);
void nx_switch_toggle(nx_switch_t* sw);

/* Styling */
void nx_switch_set_on_color(nx_switch_t* sw, nx_color_t color);
void nx_switch_set_off_color(nx_switch_t* sw, nx_color_t color);
void nx_switch_set_thumb_color(nx_switch_t* sw, nx_color_t color);
void nx_switch_set_size(nx_switch_t* sw, uint32_t width, uint32_t height);

/* Callback */
void nx_switch_set_on_toggled(nx_switch_t* sw, nx_switch_toggled_fn fn, void* data);

#ifdef __cplusplus
}
#endif
#endif
