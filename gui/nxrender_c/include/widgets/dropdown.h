/*
 * NeolyxOS - NXRender Dropdown Widget
 * Select menu with options
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_DROPDOWN_H
#define NXRENDER_DROPDOWN_H

#include "widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_dropdown nx_dropdown_t;
typedef struct nx_dropdown_option nx_dropdown_option_t;

/* Selection changed callback */
typedef void (*nx_dropdown_changed_fn)(nx_dropdown_t* dd, int index, void* user_data);

struct nx_dropdown_option {
    char* label;
    void* data;
};

struct nx_dropdown {
    nx_widget_t base;
    nx_dropdown_option_t* options;
    size_t option_count;
    size_t option_capacity;
    int selected_index;
    bool is_open;
    nx_color_t bg_color;
    nx_color_t text_color;
    nx_color_t border_color;
    nx_color_t hover_color;
    uint32_t item_height;
    uint32_t max_visible;
    nx_dropdown_changed_fn on_changed;
    void* callback_data;
};

/* Create/destroy */
nx_dropdown_t* nx_dropdown_create(void);
void nx_dropdown_destroy(nx_dropdown_t* dd);

/* Options */
void nx_dropdown_add_option(nx_dropdown_t* dd, const char* label, void* data);
void nx_dropdown_remove_option(nx_dropdown_t* dd, int index);
void nx_dropdown_clear_options(nx_dropdown_t* dd);
int nx_dropdown_option_count(nx_dropdown_t* dd);

/* Selection */
void nx_dropdown_set_selected(nx_dropdown_t* dd, int index);
int nx_dropdown_get_selected(nx_dropdown_t* dd);
const char* nx_dropdown_get_selected_label(nx_dropdown_t* dd);
void* nx_dropdown_get_selected_data(nx_dropdown_t* dd);

/* State */
void nx_dropdown_open(nx_dropdown_t* dd);
void nx_dropdown_close(nx_dropdown_t* dd);
bool nx_dropdown_is_open(nx_dropdown_t* dd);

/* Styling */
void nx_dropdown_set_bg_color(nx_dropdown_t* dd, nx_color_t color);
void nx_dropdown_set_text_color(nx_dropdown_t* dd, nx_color_t color);
void nx_dropdown_set_item_height(nx_dropdown_t* dd, uint32_t height);
void nx_dropdown_set_max_visible(nx_dropdown_t* dd, uint32_t max);

/* Callback */
void nx_dropdown_set_on_changed(nx_dropdown_t* dd, nx_dropdown_changed_fn fn, void* data);

#ifdef __cplusplus
}
#endif
#endif
