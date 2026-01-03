/*
 * NeolyxOS - NXRender Widget Base Implementation
 * Copyright (c) 2025 KetiveeAI
 */

#include "widgets/widget.h"
#include <stdlib.h>
#include <string.h>

static nx_widget_id_t g_next_id = 1;

nx_widget_id_t nx_widget_next_id(void) {
    return g_next_id++;
}

void nx_widget_init(nx_widget_t* w, const nx_widget_vtable_t* vtable) {
    if (!w) return;
    memset(w, 0, sizeof(nx_widget_t));
    w->vtable = vtable;
    w->id = nx_widget_next_id();
    w->visible = true;
    w->enabled = true;
    w->state = NX_WIDGET_NORMAL;
}

void nx_widget_destroy(nx_widget_t* w) {
    if (!w) return;
    for (size_t i = 0; i < w->child_count; i++) {
        if (w->children[i] && w->children[i]->vtable && w->children[i]->vtable->destroy) {
            w->children[i]->vtable->destroy(w->children[i]);
        }
    }
    free(w->children);
    w->children = NULL;
    w->child_count = 0;
    if (w->vtable && w->vtable->destroy) {
        w->vtable->destroy(w);
    }
}

void nx_widget_add_child(nx_widget_t* parent, nx_widget_t* child) {
    if (!parent || !child) return;
    if (parent->child_count >= parent->child_capacity) {
        size_t new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        nx_widget_t** new_arr = realloc(parent->children, new_cap * sizeof(nx_widget_t*));
        if (!new_arr) return;
        parent->children = new_arr;
        parent->child_capacity = new_cap;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

void nx_widget_remove_child(nx_widget_t* parent, nx_widget_t* child) {
    if (!parent || !child) return;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            memmove(&parent->children[i], &parent->children[i+1], 
                   (parent->child_count - i - 1) * sizeof(nx_widget_t*));
            parent->child_count--;
            child->parent = NULL;
            break;
        }
    }
}

void nx_widget_set_state(nx_widget_t* w, nx_widget_state_t state) {
    if (w) w->state |= state;
}

void nx_widget_clear_state(nx_widget_t* w, nx_widget_state_t state) {
    if (w) w->state &= ~state;
}

bool nx_widget_has_state(nx_widget_t* w, nx_widget_state_t state) {
    return w && (w->state & state);
}

void nx_widget_render(nx_widget_t* w, nx_context_t* ctx) {
    if (!w || !w->visible || !ctx) return;
    if (w->vtable && w->vtable->render) {
        w->vtable->render(w, ctx);
    }
    nx_widget_render_children(w, ctx);
}

void nx_widget_render_children(nx_widget_t* w, nx_context_t* ctx) {
    if (!w || !ctx) return;
    for (size_t i = 0; i < w->child_count; i++) {
        nx_widget_render(w->children[i], ctx);
    }
}

void nx_widget_layout(nx_widget_t* w, nx_rect_t bounds) {
    if (!w) return;
    w->bounds = bounds;
    if (w->vtable && w->vtable->layout) {
        w->vtable->layout(w, bounds);
    }
}

nx_size_t nx_widget_measure(nx_widget_t* w, nx_size_t available) {
    if (!w || !w->vtable || !w->vtable->measure) {
        return (nx_size_t){0, 0};
    }
    return w->vtable->measure(w, available);
}

nx_event_result_t nx_widget_handle_event(nx_widget_t* w, nx_event_t* event) {
    if (!w || !w->enabled || !event) return NX_EVENT_IGNORED;
    for (size_t i = w->child_count; i > 0; i--) {
        nx_widget_t* child = w->children[i-1];
        if (child && nx_rect_contains(child->bounds, event->pos)) {
            nx_event_result_t result = nx_widget_handle_event(child, event);
            if (result != NX_EVENT_IGNORED) return result;
        }
    }
    if (w->vtable && w->vtable->handle_event) {
        return w->vtable->handle_event(w, event);
    }
    return NX_EVENT_IGNORED;
}
