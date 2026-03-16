/*
 * NeolyxOS - NXRender ListView Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/listview.h"
#include <stdlib.h>
#include <string.h>

static void listview_render(nx_widget_t* self, nx_context_t* ctx);
static void listview_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_size_t listview_measure(nx_widget_t* self, nx_size_t available);
static nx_event_result_t listview_handle_event(nx_widget_t* self, nx_event_t* event);
static void listview_destroy(nx_widget_t* self);

static const nx_widget_vtable_t LISTVIEW_VTABLE = {
    .render = listview_render,
    .layout = listview_layout,
    .measure = listview_measure,
    .handle_event = listview_handle_event,
    .destroy = listview_destroy
};

nx_listview_t* nx_listview_create(size_t item_count, uint32_t row_height) {
    nx_listview_t* lv = calloc(1, sizeof(nx_listview_t));
    if (!lv) return NULL;
    nx_widget_init(&lv->base, &LISTVIEW_VTABLE);
    lv->item_count = item_count;
    lv->row_height = row_height > 0 ? row_height : 32;
    lv->selected_index = (size_t)-1;
    lv->background = nx_rgba(30, 30, 35, 255);
    lv->selected_bg = nx_rgba(0, 100, 200, 255);
    lv->hover_bg = nx_rgba(60, 60, 70, 255);
    lv->separator_color = nx_rgba(50, 50, 55, 255);
    lv->show_separators = true;
    return lv;
}

void nx_listview_set_item_count(nx_listview_t* lv, size_t count) {
    if (lv) lv->item_count = count;
}

void nx_listview_set_cell_config(nx_listview_t* lv, nx_listview_cell_config_t config, void* data) {
    if (!lv) return;
    lv->cell_config = config;
    lv->callback_data = data;
}

void nx_listview_set_on_select(nx_listview_t* lv, nx_listview_select_callback_t callback, void* data) {
    if (!lv) return;
    lv->on_select = callback;
    lv->callback_data = data;
}

void nx_listview_select(nx_listview_t* lv, size_t index) {
    if (!lv || index >= lv->item_count) return;
    lv->selected_index = index;
}

void nx_listview_scroll_to(nx_listview_t* lv, size_t index) {
    if (!lv || index >= lv->item_count) return;
    int32_t target = (int32_t)(index * lv->row_height);
    int32_t view_h = lv->base.bounds.height;
    if (target < lv->scroll_offset) {
        lv->scroll_offset = target;
    } else if (target + (int32_t)lv->row_height > lv->scroll_offset + view_h) {
        lv->scroll_offset = target + lv->row_height - view_h;
    }
}

size_t nx_listview_get_selected(nx_listview_t* lv) {
    return lv ? lv->selected_index : (size_t)-1;
}

static void listview_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_listview_t* lv = (nx_listview_t*)self;
    nx_rect_t b = self->bounds;
    
    nxgfx_fill_rect(ctx, b, lv->background);
    nxgfx_set_clip(ctx, b);
    
    size_t first = lv->scroll_offset / lv->row_height;
    size_t visible = (b.height / lv->row_height) + 2;
    size_t last = first + visible;
    if (last > lv->item_count) last = lv->item_count;
    
    for (size_t i = first; i < last; i++) {
        int32_t row_y = b.y + (int32_t)(i * lv->row_height) - lv->scroll_offset;
        nx_rect_t row_rect = {b.x, row_y, b.width, lv->row_height};
        
        if (i == lv->selected_index) {
            nxgfx_fill_rect(ctx, row_rect, lv->selected_bg);
        }
        
        if (lv->show_separators && i < lv->item_count - 1) {
            nx_rect_t sep = {b.x + 8, row_y + lv->row_height - 1, b.width - 16, 1};
            nxgfx_fill_rect(ctx, sep, lv->separator_color);
        }
    }
    
    nxgfx_clear_clip(ctx);
}

static void listview_layout(nx_widget_t* self, nx_rect_t bounds) {
    self->bounds = bounds;
}

static nx_size_t listview_measure(nx_widget_t* self, nx_size_t available) {
    nx_listview_t* lv = (nx_listview_t*)self;
    uint32_t content_h = (uint32_t)(lv->item_count * lv->row_height);
    return (nx_size_t){available.width, content_h};
}

static nx_event_result_t listview_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_listview_t* lv = (nx_listview_t*)self;
    nx_rect_t b = self->bounds;
    
    switch (event->type) {
        case NX_EVENT_MOUSE_DOWN:
            if (event->pos.x >= b.x && event->pos.x < b.x + (int32_t)b.width &&
                event->pos.y >= b.y && event->pos.y < b.y + (int32_t)b.height) {
                int32_t rel_y = event->pos.y - b.y + lv->scroll_offset;
                size_t clicked = rel_y / lv->row_height;
                if (clicked < lv->item_count) {
                    lv->selected_index = clicked;
                    if (lv->on_select) {
                        lv->on_select(clicked, lv->callback_data);
                    }
                    return NX_EVENT_NEEDS_REDRAW;
                }
            }
            break;
        default:
            break;
    }
    return NX_EVENT_IGNORED;
}

static void listview_destroy(nx_widget_t* self) {
    nx_listview_t* lv = (nx_listview_t*)self;
    free(lv->cell_pool);
    free(lv);
}
