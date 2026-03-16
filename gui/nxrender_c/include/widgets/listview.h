/*
 * NeolyxOS - NXRender ListView Widget
 * Virtual scrolling list with cell reuse
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_LISTVIEW_H
#define NXRENDER_LISTVIEW_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cell configuration callback */
typedef void (*nx_listview_cell_config_t)(nx_widget_t* cell, size_t index, void* user_data);
typedef void (*nx_listview_select_callback_t)(size_t index, void* user_data);

typedef struct {
    nx_widget_t base;
    size_t item_count;
    uint32_t row_height;
    int32_t scroll_offset;
    size_t selected_index;
    bool multi_select;
    nx_color_t background;
    nx_color_t selected_bg;
    nx_color_t hover_bg;
    nx_color_t separator_color;
    bool show_separators;
    /* Cell reuse pool */
    nx_widget_t** cell_pool;
    size_t pool_capacity;
    /* Callbacks */
    nx_listview_cell_config_t cell_config;
    nx_listview_select_callback_t on_select;
    void* callback_data;
} nx_listview_t;

nx_listview_t* nx_listview_create(size_t item_count, uint32_t row_height);
void nx_listview_set_item_count(nx_listview_t* lv, size_t count);
void nx_listview_set_cell_config(nx_listview_t* lv, nx_listview_cell_config_t config, void* data);
void nx_listview_set_on_select(nx_listview_t* lv, nx_listview_select_callback_t callback, void* data);
void nx_listview_select(nx_listview_t* lv, size_t index);
void nx_listview_scroll_to(nx_listview_t* lv, size_t index);
size_t nx_listview_get_selected(nx_listview_t* lv);

#ifdef __cplusplus
}
#endif
#endif
