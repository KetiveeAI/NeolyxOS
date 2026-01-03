/*
 * NeolyxOS - NXRender Container Widgets
 * VBox, HBox, ScrollView containers
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXRENDER_CONTAINER_H
#define NXRENDER_CONTAINER_H

#include "widgets/widget.h"
#include "layout/layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VBox - Vertical container (Column)
 * ============================================================================ */
typedef struct {
    nx_widget_t base;
    nx_flex_layout_t layout;
    nx_color_t background;
    uint32_t padding;
} nx_vbox_t;

nx_vbox_t* nx_vbox_create(void);
void nx_vbox_set_gap(nx_vbox_t* vbox, uint32_t gap);
void nx_vbox_set_padding(nx_vbox_t* vbox, uint32_t padding);
void nx_vbox_set_align(nx_vbox_t* vbox, nx_align_t align);

/* ============================================================================
 * HBox - Horizontal container (Row)
 * ============================================================================ */
typedef struct {
    nx_widget_t base;
    nx_flex_layout_t layout;
    nx_color_t background;
    uint32_t padding;
} nx_hbox_t;

nx_hbox_t* nx_hbox_create(void);
void nx_hbox_set_gap(nx_hbox_t* hbox, uint32_t gap);
void nx_hbox_set_padding(nx_hbox_t* hbox, uint32_t padding);
void nx_hbox_set_align(nx_hbox_t* hbox, nx_align_t align);

/* ============================================================================
 * ScrollView - Scrollable container
 * ============================================================================ */
typedef struct {
    nx_widget_t base;
    nx_widget_t* content;
    int32_t scroll_x;
    int32_t scroll_y;
    int32_t content_width;
    int32_t content_height;
    bool show_scrollbar_h;
    bool show_scrollbar_v;
    nx_color_t scrollbar_color;
    nx_color_t scrollbar_track;
    uint32_t scrollbar_width;
} nx_scrollview_t;

nx_scrollview_t* nx_scrollview_create(void);
void nx_scrollview_set_content(nx_scrollview_t* sv, nx_widget_t* content);
void nx_scrollview_scroll_to(nx_scrollview_t* sv, int32_t x, int32_t y);
void nx_scrollview_scroll_by(nx_scrollview_t* sv, int32_t dx, int32_t dy);

#ifdef __cplusplus
}
#endif
#endif
