/*
 * NeolyxOS - NXRender Container Widgets Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/container.h"
#include <stdlib.h>

/* ============================================================================
 * VBox Implementation
 * ============================================================================ */
static void vbox_render(nx_widget_t* self, nx_context_t* ctx);
static void vbox_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_size_t vbox_measure(nx_widget_t* self, nx_size_t available);
static void vbox_destroy(nx_widget_t* self);

static const nx_widget_vtable_t VBOX_VTABLE = {
    .render = vbox_render, .layout = vbox_layout,
    .measure = vbox_measure, .handle_event = NULL, .destroy = vbox_destroy
};

nx_vbox_t* nx_vbox_create(void) {
    nx_vbox_t* vbox = calloc(1, sizeof(nx_vbox_t));
    if (!vbox) return NULL;
    nx_widget_init(&vbox->base, &VBOX_VTABLE);
    vbox->layout = nx_flex_column();
    vbox->background = (nx_color_t){0, 0, 0, 0};
    vbox->padding = 0;
    return vbox;
}

void nx_vbox_set_gap(nx_vbox_t* vbox, uint32_t gap) { if (vbox) vbox->layout.gap = gap; }
void nx_vbox_set_padding(nx_vbox_t* vbox, uint32_t padding) { if (vbox) vbox->padding = padding; }
void nx_vbox_set_align(nx_vbox_t* vbox, nx_align_t align) { if (vbox) vbox->layout.align = align; }

static void vbox_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_vbox_t* vbox = (nx_vbox_t*)self;
    if (vbox->background.a > 0) nxgfx_fill_rect(ctx, self->bounds, vbox->background);
}

static void vbox_layout(nx_widget_t* self, nx_rect_t bounds) {
    nx_vbox_t* vbox = (nx_vbox_t*)self;
    self->bounds = bounds;
    nx_rect_t content = {
        bounds.x + vbox->padding, bounds.y + vbox->padding,
        bounds.width - 2*vbox->padding, bounds.height - 2*vbox->padding
    };
    nx_layout_flex(self, vbox->layout, content);
}

static nx_size_t vbox_measure(nx_widget_t* self, nx_size_t available) {
    nx_vbox_t* vbox = (nx_vbox_t*)self;
    uint32_t w = 0, h = 0;
    for (size_t i = 0; i < self->child_count; i++) {
        nx_size_t cs = nx_widget_measure(self->children[i], available);
        if (cs.width > w) w = cs.width;
        h += cs.height + vbox->layout.gap;
    }
    if (self->child_count > 0) h -= vbox->layout.gap;
    return (nx_size_t){w + 2*vbox->padding, h + 2*vbox->padding};
}

static void vbox_destroy(nx_widget_t* self) { free(self); }

/* ============================================================================
 * HBox Implementation
 * ============================================================================ */
static void hbox_render(nx_widget_t* self, nx_context_t* ctx);
static void hbox_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_size_t hbox_measure(nx_widget_t* self, nx_size_t available);
static void hbox_destroy(nx_widget_t* self);

static const nx_widget_vtable_t HBOX_VTABLE = {
    .render = hbox_render, .layout = hbox_layout,
    .measure = hbox_measure, .handle_event = NULL, .destroy = hbox_destroy
};

nx_hbox_t* nx_hbox_create(void) {
    nx_hbox_t* hbox = calloc(1, sizeof(nx_hbox_t));
    if (!hbox) return NULL;
    nx_widget_init(&hbox->base, &HBOX_VTABLE);
    hbox->layout = nx_flex_row();
    hbox->background = (nx_color_t){0, 0, 0, 0};
    hbox->padding = 0;
    return hbox;
}

void nx_hbox_set_gap(nx_hbox_t* hbox, uint32_t gap) { if (hbox) hbox->layout.gap = gap; }
void nx_hbox_set_padding(nx_hbox_t* hbox, uint32_t padding) { if (hbox) hbox->padding = padding; }
void nx_hbox_set_align(nx_hbox_t* hbox, nx_align_t align) { if (hbox) hbox->layout.align = align; }

static void hbox_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_hbox_t* hbox = (nx_hbox_t*)self;
    if (hbox->background.a > 0) nxgfx_fill_rect(ctx, self->bounds, hbox->background);
}

static void hbox_layout(nx_widget_t* self, nx_rect_t bounds) {
    nx_hbox_t* hbox = (nx_hbox_t*)self;
    self->bounds = bounds;
    nx_rect_t content = {
        bounds.x + hbox->padding, bounds.y + hbox->padding,
        bounds.width - 2*hbox->padding, bounds.height - 2*hbox->padding
    };
    nx_layout_flex(self, hbox->layout, content);
}

static nx_size_t hbox_measure(nx_widget_t* self, nx_size_t available) {
    nx_hbox_t* hbox = (nx_hbox_t*)self;
    uint32_t w = 0, h = 0;
    for (size_t i = 0; i < self->child_count; i++) {
        nx_size_t cs = nx_widget_measure(self->children[i], available);
        w += cs.width + hbox->layout.gap;
        if (cs.height > h) h = cs.height;
    }
    if (self->child_count > 0) w -= hbox->layout.gap;
    return (nx_size_t){w + 2*hbox->padding, h + 2*hbox->padding};
}

static void hbox_destroy(nx_widget_t* self) { free(self); }

/* ============================================================================
 * ScrollView Implementation
 * ============================================================================ */
static void scrollview_render(nx_widget_t* self, nx_context_t* ctx);
static void scrollview_layout(nx_widget_t* self, nx_rect_t bounds);
static nx_event_result_t scrollview_handle_event(nx_widget_t* self, nx_event_t* event);
static void scrollview_destroy(nx_widget_t* self);

static const nx_widget_vtable_t SCROLLVIEW_VTABLE = {
    .render = scrollview_render, .layout = scrollview_layout,
    .measure = NULL, .handle_event = scrollview_handle_event, .destroy = scrollview_destroy
};

nx_scrollview_t* nx_scrollview_create(void) {
    nx_scrollview_t* sv = calloc(1, sizeof(nx_scrollview_t));
    if (!sv) return NULL;
    nx_widget_init(&sv->base, &SCROLLVIEW_VTABLE);
    sv->scrollbar_color = nx_rgba(100, 100, 110, 200);
    sv->scrollbar_track = nx_rgba(50, 50, 55, 100);
    sv->scrollbar_width = 8;
    sv->show_scrollbar_v = true;
    return sv;
}

void nx_scrollview_set_content(nx_scrollview_t* sv, nx_widget_t* content) {
    if (!sv) return;
    if (sv->content) nx_widget_destroy(sv->content);
    sv->content = content;
    if (content) content->parent = &sv->base;
}

void nx_scrollview_scroll_to(nx_scrollview_t* sv, int32_t x, int32_t y) {
    if (!sv) return;
    sv->scroll_x = x; sv->scroll_y = y;
}

void nx_scrollview_scroll_by(nx_scrollview_t* sv, int32_t dx, int32_t dy) {
    if (!sv) return;
    sv->scroll_x += dx; sv->scroll_y += dy;
    if (sv->scroll_x < 0) sv->scroll_x = 0;
    if (sv->scroll_y < 0) sv->scroll_y = 0;
    int32_t max_x = sv->content_width - (int32_t)sv->base.bounds.width;
    int32_t max_y = sv->content_height - (int32_t)sv->base.bounds.height;
    if (sv->scroll_x > max_x) sv->scroll_x = max_x > 0 ? max_x : 0;
    if (sv->scroll_y > max_y) sv->scroll_y = max_y > 0 ? max_y : 0;
}

static void scrollview_render(nx_widget_t* self, nx_context_t* ctx) {
    nx_scrollview_t* sv = (nx_scrollview_t*)self;
    nxgfx_set_clip(ctx, self->bounds);
    if (sv->content) nx_widget_render(sv->content, ctx);
    nxgfx_clear_clip(ctx);
    /* Vertical scrollbar */
    if (sv->show_scrollbar_v && sv->content_height > (int32_t)self->bounds.height) {
        int32_t track_x = self->bounds.x + self->bounds.width - sv->scrollbar_width;
        nxgfx_fill_rect(ctx, (nx_rect_t){track_x, self->bounds.y, sv->scrollbar_width, self->bounds.height}, sv->scrollbar_track);
        float ratio = (float)self->bounds.height / sv->content_height;
        uint32_t thumb_h = (uint32_t)(self->bounds.height * ratio);
        if (thumb_h < 20) thumb_h = 20;
        int32_t thumb_y = self->bounds.y + (int32_t)(sv->scroll_y * ratio);
        nxgfx_fill_rounded_rect(ctx, (nx_rect_t){track_x, thumb_y, sv->scrollbar_width, thumb_h}, sv->scrollbar_color, 4);
    }
}

static void scrollview_layout(nx_widget_t* self, nx_rect_t bounds) {
    nx_scrollview_t* sv = (nx_scrollview_t*)self;
    self->bounds = bounds;
    if (sv->content) {
        nx_size_t cs = nx_widget_measure(sv->content, (nx_size_t){bounds.width, UINT32_MAX});
        sv->content_width = cs.width;
        sv->content_height = cs.height;
        nx_widget_layout(sv->content, (nx_rect_t){
            bounds.x - sv->scroll_x, bounds.y - sv->scroll_y, cs.width, cs.height
        });
    }
}

static nx_event_result_t scrollview_handle_event(nx_widget_t* self, nx_event_t* event) {
    nx_scrollview_t* sv = (nx_scrollview_t*)self;
    if (event->type == NX_EVENT_MOUSE_MOVE && event->modifiers.shift) {
        /* Simulate scroll wheel with shift+drag (placeholder) */
    }
    if (sv->content) return nx_widget_handle_event(sv->content, event);
    return NX_EVENT_IGNORED;
}

static void scrollview_destroy(nx_widget_t* self) {
    nx_scrollview_t* sv = (nx_scrollview_t*)self;
    if (sv->content) nx_widget_destroy(sv->content);
    free(sv);
}
