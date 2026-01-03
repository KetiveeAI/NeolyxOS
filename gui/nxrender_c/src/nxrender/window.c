/*
 * NeolyxOS - NXRender Window Manager Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include "nxrender/window.h"
#include <stdlib.h>
#include <string.h>

nx_window_style_t nx_window_default_style(void) {
    return (nx_window_style_t){
        .title_bar_color = nx_rgba(40, 40, 45, 255),
        .title_text_color = NX_COLOR_WHITE,
        .border_color = nx_rgba(60, 60, 65, 255),
        .background_color = nx_rgba(30, 30, 35, 255),
        .title_bar_height = 32,
        .border_width = 1,
        .corner_radius = 8
    };
}

nx_window_manager_t* nx_wm_create(nx_context_t* ctx) {
    nx_window_manager_t* wm = calloc(1, sizeof(nx_window_manager_t));
    if (!wm) return NULL;
    wm->ctx = ctx;
    wm->screen_width = nxgfx_width(ctx);
    wm->screen_height = nxgfx_height(ctx);
    return wm;
}

void nx_wm_destroy(nx_window_manager_t* wm) {
    if (!wm) return;
    nx_window_t* w = wm->windows;
    while (w) {
        nx_window_t* next = w->next;
        nx_window_destroy(w);
        w = next;
    }
    free(wm);
}

nx_window_t* nx_window_create(const char* title, nx_rect_t frame, nx_window_flags_t flags) {
    nx_window_t* win = calloc(1, sizeof(nx_window_t));
    if (!win) return NULL;
    win->title = title ? strdup(title) : NULL;
    win->frame = frame;
    win->flags = flags;
    win->style = nx_window_default_style();
    win->visible = true;
    win->dirty = true;
    uint32_t tb = (flags & NX_WINDOW_BORDERLESS) ? 0 : win->style.title_bar_height;
    uint32_t bw = (flags & NX_WINDOW_BORDERLESS) ? 0 : win->style.border_width;
    win->content_rect = (nx_rect_t){
        frame.x + bw, frame.y + tb,
        frame.width - 2*bw, frame.height - tb - bw
    };
    return win;
}

void nx_window_destroy(nx_window_t* win) {
    if (!win) return;
    if (win->root_widget) nx_widget_destroy(win->root_widget);
    free(win->title);
    free(win);
}

void nx_wm_add_window(nx_window_manager_t* wm, nx_window_t* win) {
    if (!wm || !win) return;
    int32_t max_z = 0;
    for (nx_window_t* w = wm->windows; w; w = w->next) {
        if (w->z_order > max_z) max_z = w->z_order;
    }
    win->z_order = max_z + 1;
    win->next = wm->windows;
    wm->windows = win;
    nx_wm_focus_window(wm, win);
}

void nx_wm_remove_window(nx_window_manager_t* wm, nx_window_t* win) {
    if (!wm || !win) return;
    if (wm->windows == win) {
        wm->windows = win->next;
    } else {
        for (nx_window_t* w = wm->windows; w; w = w->next) {
            if (w->next == win) { w->next = win->next; break; }
        }
    }
    if (wm->focused == win) wm->focused = wm->windows;
    win->next = NULL;
}

void nx_wm_focus_window(nx_window_manager_t* wm, nx_window_t* win) {
    if (!wm || !win) return;
    if (wm->focused) wm->focused->focused = false;
    wm->focused = win;
    win->focused = true;
    int32_t max_z = 0;
    for (nx_window_t* w = wm->windows; w; w = w->next) {
        if (w->z_order > max_z) max_z = w->z_order;
    }
    win->z_order = max_z + 1;
}

nx_window_t* nx_wm_window_at(nx_window_manager_t* wm, nx_point_t pos) {
    if (!wm) return NULL;
    nx_window_t* top = NULL;
    int32_t top_z = -1;
    for (nx_window_t* w = wm->windows; w; w = w->next) {
        if (!w->visible) continue;
        if (nx_rect_contains(w->frame, pos) && w->z_order > top_z) {
            top = w; top_z = w->z_order;
        }
    }
    return top;
}

void nx_window_set_title(nx_window_t* win, const char* title) {
    if (!win) return;
    free(win->title);
    win->title = title ? strdup(title) : NULL;
    win->dirty = true;
}

void nx_window_set_root_widget(nx_window_t* win, nx_widget_t* widget) {
    if (!win) return;
    if (win->root_widget) nx_widget_destroy(win->root_widget);
    win->root_widget = widget;
    if (widget) nx_widget_layout(widget, win->content_rect);
    win->dirty = true;
}

void nx_window_move(nx_window_t* win, nx_point_t pos) {
    if (!win) return;
    int32_t dx = pos.x - win->frame.x, dy = pos.y - win->frame.y;
    win->frame.x = pos.x; win->frame.y = pos.y;
    win->content_rect.x += dx; win->content_rect.y += dy;
    if (win->root_widget) nx_widget_layout(win->root_widget, win->content_rect);
    win->dirty = true;
}

void nx_window_resize(nx_window_t* win, nx_size_t size) {
    if (!win) return;
    win->frame.width = size.width; win->frame.height = size.height;
    uint32_t tb = (win->flags & NX_WINDOW_BORDERLESS) ? 0 : win->style.title_bar_height;
    uint32_t bw = (win->flags & NX_WINDOW_BORDERLESS) ? 0 : win->style.border_width;
    win->content_rect.width = size.width - 2*bw;
    win->content_rect.height = size.height - tb - bw;
    if (win->root_widget) nx_widget_layout(win->root_widget, win->content_rect);
    win->dirty = true;
}

void nx_window_show(nx_window_t* win) { if (win) { win->visible = true; win->dirty = true; } }
void nx_window_hide(nx_window_t* win) { if (win) { win->visible = false; } }

void nx_window_render(nx_window_t* win, nx_context_t* ctx) {
    if (!win || !win->visible || !ctx) return;
    nx_window_style_t* s = &win->style;
    if (!(win->flags & NX_WINDOW_BORDERLESS)) {
        nxgfx_fill_rounded_rect(ctx, win->frame, s->background_color, s->corner_radius);
        nx_rect_t title_bar = { win->frame.x, win->frame.y, win->frame.width, s->title_bar_height };
        nxgfx_fill_rounded_rect(ctx, title_bar, s->title_bar_color, s->corner_radius);
        if (win->title) {
            nx_font_t* font = nxgfx_font_default(ctx, 14);
            nx_point_t tp = { win->frame.x + 12, win->frame.y + 8 };
            nxgfx_draw_text(ctx, win->title, tp, font, s->title_text_color);
            nxgfx_font_destroy(font);
        }
        nx_color_t close_color = nx_rgba(255, 95, 87, 255);
        nx_point_t close_center = { win->frame.x + win->frame.width - 20, win->frame.y + 16 };
        nxgfx_fill_circle(ctx, close_center, 6, close_color);
    }
    nxgfx_fill_rect(ctx, win->content_rect, s->background_color);
    if (win->root_widget) nx_widget_render(win->root_widget, ctx);
    win->dirty = false;
}

void nx_wm_render(nx_window_manager_t* wm) {
    if (!wm) return;
    nx_window_t* sorted[64]; int count = 0;
    for (nx_window_t* w = wm->windows; w && count < 64; w = w->next) sorted[count++] = w;
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[i]->z_order > sorted[j]->z_order) {
                nx_window_t* tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
            }
        }
    }
    for (int i = 0; i < count; i++) nx_window_render(sorted[i], wm->ctx);
}

bool nx_wm_handle_event(nx_window_manager_t* wm, nx_event_t* event) {
    if (!wm || !event) return false;
    if (event->type == NX_EVENT_MOUSE_DOWN) {
        nx_window_t* win = nx_wm_window_at(wm, event->pos);
        if (win) {
            nx_wm_focus_window(wm, win);
            if (!(win->flags & NX_WINDOW_BORDERLESS) &&
                event->pos.y < win->frame.y + (int32_t)win->style.title_bar_height) {
                wm->dragging = win;
                wm->drag_offset = (nx_point_t){ event->pos.x - win->frame.x, event->pos.y - win->frame.y };
                return true;
            }
            if (win->root_widget) {
                nx_event_t local = *event;
                local.pos.x -= win->content_rect.x;
                local.pos.y -= win->content_rect.y;
                return nx_widget_handle_event(win->root_widget, &local) != NX_EVENT_IGNORED;
            }
        }
    } else if (event->type == NX_EVENT_MOUSE_UP) {
        if (wm->dragging) { wm->dragging = NULL; return true; }
    } else if (event->type == NX_EVENT_MOUSE_MOVE) {
        if (wm->dragging) {
            nx_window_move(wm->dragging, (nx_point_t){
                event->pos.x - wm->drag_offset.x, event->pos.y - wm->drag_offset.y });
            return true;
        }
    }
    return false;
}
