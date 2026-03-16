/*
 * NeolyxOS - NXRender Dropdown Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/dropdown.h"
#include <stdlib.h>
#include <string.h>

static void dropdown_render(nx_widget_t* self, nx_context_t* ctx);
static nx_event_result_t dropdown_handle_event(nx_widget_t* self, nx_event_t* event);
static void dropdown_destroy(nx_widget_t* self);

static const nx_widget_vtable_t dropdown_vtable = {
    .render = dropdown_render,
    .layout = NULL,
    .measure = NULL,
    .handle_event = dropdown_handle_event,
    .destroy = dropdown_destroy
};

nx_dropdown_t* nx_dropdown_create(void) {
    nx_dropdown_t* dd = calloc(1, sizeof(nx_dropdown_t));
    if (!dd) return NULL;
    
    nx_widget_init(&dd->base, &dropdown_vtable);
    dd->selected_index = -1;
    dd->is_open = false;
    dd->bg_color = nx_rgb(45, 45, 50);
    dd->text_color = nx_rgb(240, 240, 245);
    dd->border_color = nx_rgb(70, 70, 75);
    dd->hover_color = nx_rgb(60, 60, 70);
    dd->item_height = 32;
    dd->max_visible = 5;
    
    return dd;
}

void nx_dropdown_destroy(nx_dropdown_t* dd) {
    if (!dd) return;
    nx_dropdown_clear_options(dd);
    free(dd->options);
    nx_widget_destroy(&dd->base);
    free(dd);
}

static void dropdown_destroy(nx_widget_t* self) {
    nx_dropdown_t* dd = (nx_dropdown_t*)self;
    nx_dropdown_clear_options(dd);
    free(dd->options);
    dd->options = NULL;
}

void nx_dropdown_add_option(nx_dropdown_t* dd, const char* label, void* data) {
    if (!dd) return;
    
    if (dd->option_count >= dd->option_capacity) {
        size_t new_cap = dd->option_capacity == 0 ? 8 : dd->option_capacity * 2;
        nx_dropdown_option_t* new_opts = realloc(dd->options, new_cap * sizeof(nx_dropdown_option_t));
        if (!new_opts) return;
        dd->options = new_opts;
        dd->option_capacity = new_cap;
    }
    
    dd->options[dd->option_count].label = label ? strdup(label) : NULL;
    dd->options[dd->option_count].data = data;
    dd->option_count++;
}

void nx_dropdown_remove_option(nx_dropdown_t* dd, int index) {
    if (!dd || index < 0 || (size_t)index >= dd->option_count) return;
    
    free(dd->options[index].label);
    for (size_t i = index; i < dd->option_count - 1; i++) {
        dd->options[i] = dd->options[i + 1];
    }
    dd->option_count--;
    
    if (dd->selected_index == index) {
        dd->selected_index = -1;
    } else if (dd->selected_index > index) {
        dd->selected_index--;
    }
}

void nx_dropdown_clear_options(nx_dropdown_t* dd) {
    if (!dd) return;
    for (size_t i = 0; i < dd->option_count; i++) {
        free(dd->options[i].label);
    }
    dd->option_count = 0;
    dd->selected_index = -1;
}

int nx_dropdown_option_count(nx_dropdown_t* dd) {
    return dd ? (int)dd->option_count : 0;
}

void nx_dropdown_set_selected(nx_dropdown_t* dd, int index) {
    if (!dd) return;
    if (index < -1 || (size_t)index >= dd->option_count) return;
    
    if (dd->selected_index != index) {
        dd->selected_index = index;
        if (dd->on_changed) {
            dd->on_changed(dd, index, dd->callback_data);
        }
    }
}

int nx_dropdown_get_selected(nx_dropdown_t* dd) {
    return dd ? dd->selected_index : -1;
}

const char* nx_dropdown_get_selected_label(nx_dropdown_t* dd) {
    if (!dd || dd->selected_index < 0) return NULL;
    return dd->options[dd->selected_index].label;
}

void* nx_dropdown_get_selected_data(nx_dropdown_t* dd) {
    if (!dd || dd->selected_index < 0) return NULL;
    return dd->options[dd->selected_index].data;
}

void nx_dropdown_open(nx_dropdown_t* dd) {
    if (dd) dd->is_open = true;
}

void nx_dropdown_close(nx_dropdown_t* dd) {
    if (dd) dd->is_open = false;
}

bool nx_dropdown_is_open(nx_dropdown_t* dd) {
    return dd ? dd->is_open : false;
}

void nx_dropdown_set_bg_color(nx_dropdown_t* dd, nx_color_t color) {
    if (dd) dd->bg_color = color;
}

void nx_dropdown_set_text_color(nx_dropdown_t* dd, nx_color_t color) {
    if (dd) dd->text_color = color;
}

void nx_dropdown_set_item_height(nx_dropdown_t* dd, uint32_t height) {
    if (dd) dd->item_height = height;
}

void nx_dropdown_set_max_visible(nx_dropdown_t* dd, uint32_t max) {
    if (dd) dd->max_visible = max;
}

void nx_dropdown_set_on_changed(nx_dropdown_t* dd, nx_dropdown_changed_fn fn, void* data) {
    if (!dd) return;
    dd->on_changed = fn;
    dd->callback_data = data;
}

static void dropdown_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_dropdown_t* dd = (nx_dropdown_t*)self;
    if (!self->visible) return;
    
    nx_rect_t bounds = self->bounds;
    
    /* Main button */
    nxgfx_fill_rounded_rect(ctx, bounds, dd->bg_color, 6);
    nxgfx_draw_rect(ctx, bounds, dd->border_color, 1);
    
    /* Selected text */
    const char* label = nx_dropdown_get_selected_label(dd);
    if (label) {
        nx_point_t pos = { bounds.x + 12, bounds.y + bounds.height - 10 };
        nxgfx_draw_text(ctx, label, pos, NULL, dd->text_color);
    }
    
    /* Arrow */
    int32_t arrow_x = bounds.x + bounds.width - 20;
    int32_t arrow_y = bounds.y + bounds.height / 2;
    nx_point_t p1 = { arrow_x, arrow_y - 3 };
    nx_point_t p2 = { arrow_x + 6, arrow_y - 3 };
    nx_point_t p3 = { arrow_x + 3, arrow_y + 3 };
    nxgfx_draw_line(ctx, p1, p3, dd->text_color, 1);
    nxgfx_draw_line(ctx, p2, p3, dd->text_color, 1);
    
    /* Dropdown list when open */
    if (dd->is_open && dd->option_count > 0) {
        size_t visible = dd->option_count;
        if (visible > dd->max_visible) visible = dd->max_visible;
        
        uint32_t list_height = visible * dd->item_height;
        nx_rect_t list = { bounds.x, bounds.y + bounds.height + 2, bounds.width, list_height };
        nxgfx_fill_rounded_rect(ctx, list, dd->bg_color, 6);
        nxgfx_draw_rect(ctx, list, dd->border_color, 1);
        
        for (size_t i = 0; i < visible; i++) {
            int32_t item_y = list.y + i * dd->item_height;
            nx_point_t pos = { list.x + 12, item_y + dd->item_height - 10 };
            nxgfx_draw_text(ctx, dd->options[i].label, pos, NULL, dd->text_color);
        }
    }
}

static nx_event_result_t dropdown_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_dropdown_t* dd = (nx_dropdown_t*)self;
    nx_rect_t bounds = self->bounds;
    
    if (event->type == NX_EVENT_MOUSE_DOWN) {
        if (nx_rect_contains(bounds, event->pos)) {
            dd->is_open = !dd->is_open;
            return NX_EVENT_NEEDS_REDRAW;
        }
        
        /* Check list items if open */
        if (dd->is_open) {
            size_t visible = dd->option_count;
            if (visible > dd->max_visible) visible = dd->max_visible;
            
            nx_rect_t list = { bounds.x, bounds.y + bounds.height + 2, bounds.width, visible * dd->item_height };
            if (nx_rect_contains(list, event->pos)) {
                int item = (event->pos.y - list.y) / dd->item_height;
                if (item >= 0 && (size_t)item < dd->option_count) {
                    nx_dropdown_set_selected(dd, item);
                    dd->is_open = false;
                    return NX_EVENT_NEEDS_REDRAW;
                }
            }
            
            /* Click outside closes */
            dd->is_open = false;
            return NX_EVENT_NEEDS_REDRAW;
        }
    }
    
    return NX_EVENT_IGNORED;
}
