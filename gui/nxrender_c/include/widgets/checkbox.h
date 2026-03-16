/*
 * NeolyxOS - NXRender Checkbox Widget
 * Toggle checkbox with label
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_CHECKBOX_H
#define NXRENDER_CHECKBOX_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_checkbox nx_checkbox_t;

/* Checkbox changed callback */
typedef void (*nx_checkbox_changed_fn)(nx_checkbox_t* checkbox, bool checked, void* user_data);

struct nx_checkbox {
    nx_widget_t base;
    char* label;
    bool checked;
    nx_color_t check_color;
    nx_color_t box_color;
    nx_color_t label_color;
    uint32_t box_size;
    nx_checkbox_changed_fn on_changed;
    void* callback_data;
};

/* Create/destroy */
nx_checkbox_t* nx_checkbox_create(const char* label);
void nx_checkbox_destroy(nx_checkbox_t* cb);

/* Properties */
void nx_checkbox_set_checked(nx_checkbox_t* cb, bool checked);
bool nx_checkbox_is_checked(nx_checkbox_t* cb);
void nx_checkbox_set_label(nx_checkbox_t* cb, const char* label);
const char* nx_checkbox_get_label(nx_checkbox_t* cb);

/* Styling */
void nx_checkbox_set_check_color(nx_checkbox_t* cb, nx_color_t color);
void nx_checkbox_set_box_color(nx_checkbox_t* cb, nx_color_t color);
void nx_checkbox_set_box_size(nx_checkbox_t* cb, uint32_t size);

/* Callback */
void nx_checkbox_set_on_changed(nx_checkbox_t* cb, nx_checkbox_changed_fn fn, void* data);

/* Toggle */
void nx_checkbox_toggle(nx_checkbox_t* cb);

#ifdef __cplusplus
}
#endif
#endif
