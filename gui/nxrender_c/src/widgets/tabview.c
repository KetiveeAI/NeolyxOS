/*
 * NeolyxOS - NXRender TabView Widget Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/tabview.h"
#include <stdlib.h>
#include <string.h>

static void tabview_render(nx_widget_t* self, nx_context_t* ctx);
static void tabview_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_event_result_t tabview_handle_event(nx_widget_t* self, nx_event_t* event);
static void tabview_destroy(nx_widget_t* self);

static const nx_widget_vtable_t TABVIEW_VTABLE = {
    .render = tabview_render,
    .layout = tabview_layout,
    .measure = NULL,
    .handle_event = tabview_handle_event,
    .destroy = tabview_destroy
};

nx_tabview_t* nx_tabview_create(void) {
    nx_tabview_t* tv = calloc(1, sizeof(nx_tabview_t));
    if (!tv) return NULL;
    nx_widget_init(&tv->base, &TABVIEW_VTABLE);
    tv->tab_height = 36;
    tv->tab_bg = nx_rgba(40, 40, 45, 255);
    tv->tab_active_bg = nx_rgba(30, 30, 35, 255);
    tv->tab_hover_bg = nx_rgba(55, 55, 60, 255);
    tv->tab_text = nx_rgba(200, 200, 205, 255);
    tv->content_bg = nx_rgba(30, 30, 35, 255);
    tv->hovered_tab = -1;
    return tv;
}

size_t nx_tabview_add_tab(nx_tabview_t* tv, const char* title, nx_widget_t* content) {
    if (!tv || tv->tab_count >= NX_TAB_MAX_TABS) return (size_t)-1;
    size_t idx = tv->tab_count++;
    strncpy(tv->tabs[idx].title, title, NX_TAB_MAX_TITLE - 1);
    tv->tabs[idx].title[NX_TAB_MAX_TITLE - 1] = '\0';
    tv->tabs[idx].content = content;
    tv->tabs[idx].closable = false;
    if (content) content->parent = &tv->base;
    return idx;
}

void nx_tabview_remove_tab(nx_tabview_t* tv, size_t index) {
    if (!tv || index >= tv->tab_count) return;
    if (tv->tabs[index].content) {
        nx_widget_destroy(tv->tabs[index].content);
    }
    for (size_t i = index; i < tv->tab_count - 1; i++) {
        tv->tabs[i] = tv->tabs[i + 1];
    }
    tv->tab_count--;
    if (tv->active_tab >= tv->tab_count && tv->tab_count > 0) {
        tv->active_tab = tv->tab_count - 1;
    }
}

void nx_tabview_select(nx_tabview_t* tv, size_t index) {
    if (!tv || index >= tv->tab_count) return;
    if (tv->active_tab != index) {
        tv->active_tab = index;
        if (tv->on_change) {
            tv->on_change(index, tv->callback_data);
        }
    }
}

size_t nx_tabview_get_active(nx_tabview_t* tv) {
    return tv ? tv->active_tab : 0;
}

void nx_tabview_set_closable(nx_tabview_t* tv, size_t index, bool closable) {
    if (tv && index < tv->tab_count) {
        tv->tabs[index].closable = closable;
    }
}

void nx_tabview_set_on_change(nx_tabview_t* tv, nx_tabview_change_callback_t callback, void* data) {
    if (!tv) return;
    tv->on_change = callback;
    tv->callback_data = data;
}

static void tabview_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_tabview_t* tv = (nx_tabview_t*)self;
    nx_rect_t b = self->bounds;
    
    nx_rect_t tab_bar = {b.x, b.y, b.width, tv->tab_height};
    nxgfx_fill_rect(ctx, tab_bar, tv->tab_bg);
    
    if (tv->tab_count > 0) {
        uint32_t tab_w = b.width / tv->tab_count;
        if (tab_w > 150) tab_w = 150;
        
        for (size_t i = 0; i < tv->tab_count; i++) {
            int32_t tx = b.x + (int32_t)(i * tab_w);
            nx_rect_t tr = {tx, b.y, tab_w, tv->tab_height};
            
            nx_color_t bg = tv->tab_bg;
            if (i == tv->active_tab) {
                bg = tv->tab_active_bg;
            } else if ((int32_t)i == tv->hovered_tab) {
                bg = tv->tab_hover_bg;
            }
            nxgfx_fill_rect(ctx, tr, bg);
            
            if (i == tv->active_tab) {
                nx_rect_t indicator = {tx, b.y + tv->tab_height - 2, tab_w, 2};
                nxgfx_fill_rect(ctx, indicator, nx_rgba(0, 122, 255, 255));
            }
            
            nxgfx_draw_text(ctx, tv->tabs[i].title, 
                           (nx_point_t){tx + 12, b.y + (tv->tab_height - 14) / 2}, 
                           NULL, tv->tab_text);
        }
    }
    
    nx_rect_t content_area = {b.x, b.y + tv->tab_height, b.width, b.height - tv->tab_height};
    nxgfx_fill_rect(ctx, content_area, tv->content_bg);
    
    if (tv->tab_count > 0 && tv->tabs[tv->active_tab].content) {
        nx_widget_render(tv->tabs[tv->active_tab].content, ctx);
    }
}

static void tabview_layout(nx_widget_t* self, nx_rect_t bounds) {
    nx_tabview_t* tv = (nx_tabview_t*)self;
    self->bounds = bounds;
    
    nx_rect_t content_area = {bounds.x, bounds.y + tv->tab_height, 
                              bounds.width, bounds.height - tv->tab_height};
    if (tv->tab_count > 0 && tv->tabs[tv->active_tab].content) {
        nx_widget_layout(tv->tabs[tv->active_tab].content, content_area);
    }
}

static nx_event_result_t tabview_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_tabview_t* tv = (nx_tabview_t*)self;
    nx_rect_t b = self->bounds;
    
    switch (event->type) {
        case NX_EVENT_MOUSE_MOVE: {
            int32_t old = tv->hovered_tab;
            tv->hovered_tab = -1;
            if (event->pos.y >= b.y && event->pos.y < b.y + (int32_t)tv->tab_height && 
                tv->tab_count > 0) {
                uint32_t tab_w = b.width / tv->tab_count;
                if (tab_w > 150) tab_w = 150;
                int32_t rel_x = event->pos.x - b.x;
                if (rel_x >= 0 && (size_t)(rel_x / tab_w) < tv->tab_count) {
                    tv->hovered_tab = rel_x / tab_w;
                }
            }
            return old != tv->hovered_tab ? NX_EVENT_NEEDS_REDRAW : NX_EVENT_IGNORED;
        }
        case NX_EVENT_MOUSE_DOWN:
            if (event->pos.y >= b.y && event->pos.y < b.y + (int32_t)tv->tab_height && 
                tv->tab_count > 0) {
                uint32_t tab_w = b.width / tv->tab_count;
                if (tab_w > 150) tab_w = 150;
                int32_t rel_x = event->pos.x - b.x;
                if (rel_x >= 0) {
                    size_t clicked = rel_x / tab_w;
                    if (clicked < tv->tab_count && clicked != tv->active_tab) {
                        nx_tabview_select(tv, clicked);
                        return NX_EVENT_NEEDS_REDRAW;
                    }
                }
            }
            break;
        case NX_EVENT_MOUSE_LEAVE:
            if (tv->hovered_tab >= 0) {
                tv->hovered_tab = -1;
                return NX_EVENT_NEEDS_REDRAW;
            }
            break;
        default:
            break;
    }
    
    if (tv->tab_count > 0 && tv->tabs[tv->active_tab].content) {
        return nx_widget_handle_event(tv->tabs[tv->active_tab].content, event);
    }
    return NX_EVENT_IGNORED;
}

static void tabview_destroy(nx_widget_t* self) {
    nx_tabview_t* tv = (nx_tabview_t*)self;
    for (size_t i = 0; i < tv->tab_count; i++) {
        if (tv->tabs[i].content) {
            nx_widget_destroy(tv->tabs[i].content);
        }
    }
    free(tv);
}
