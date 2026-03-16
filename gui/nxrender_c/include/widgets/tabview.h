/*
 * NeolyxOS - NXRender TabView Widget
 * Tabbed container with tab bar
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_TABVIEW_H
#define NXRENDER_TABVIEW_H

#include "widgets/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NX_TAB_MAX_TABS 32
#define NX_TAB_MAX_TITLE 64

typedef struct {
    char title[NX_TAB_MAX_TITLE];
    nx_widget_t* content;
    bool closable;
} nx_tab_t;

typedef void (*nx_tabview_change_callback_t)(size_t index, void* user_data);

typedef struct {
    nx_widget_t base;
    nx_tab_t tabs[NX_TAB_MAX_TABS];
    size_t tab_count;
    size_t active_tab;
    uint32_t tab_height;
    nx_color_t tab_bg;
    nx_color_t tab_active_bg;
    nx_color_t tab_hover_bg;
    nx_color_t tab_text;
    nx_color_t content_bg;
    int32_t hovered_tab;
    /* Callbacks */
    nx_tabview_change_callback_t on_change;
    void* callback_data;
} nx_tabview_t;

nx_tabview_t* nx_tabview_create(void);
size_t nx_tabview_add_tab(nx_tabview_t* tv, const char* title, nx_widget_t* content);
void nx_tabview_remove_tab(nx_tabview_t* tv, size_t index);
void nx_tabview_select(nx_tabview_t* tv, size_t index);
size_t nx_tabview_get_active(nx_tabview_t* tv);
void nx_tabview_set_closable(nx_tabview_t* tv, size_t index, bool closable);
void nx_tabview_set_on_change(nx_tabview_t* tv, nx_tabview_change_callback_t callback, void* data);

#ifdef __cplusplus
}
#endif
#endif
