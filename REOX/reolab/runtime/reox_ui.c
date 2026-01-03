/*
 * REOX UI Runtime Library - Implementation
 * Native UI system for NeolyxOS
 */

#include "reox_ui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Color Functions
 * ============================================================================ */

rx_color color_lighten(rx_color c, float amount) {
    if (amount < 0) amount = 0;
    if (amount > 1) amount = 1;
    
    uint8_t r = c.r + (uint8_t)((255 - c.r) * amount);
    uint8_t g = c.g + (uint8_t)((255 - c.g) * amount);
    uint8_t b = c.b + (uint8_t)((255 - c.b) * amount);
    
    return (rx_color){ r, g, b, c.a };
}

rx_color color_darken(rx_color c, float amount) {
    if (amount < 0) amount = 0;
    if (amount > 1) amount = 1;
    
    uint8_t r = (uint8_t)(c.r * (1 - amount));
    uint8_t g = (uint8_t)(c.g * (1 - amount));
    uint8_t b = (uint8_t)(c.b * (1 - amount));
    
    return (rx_color){ r, g, b, c.a };
}

rx_color color_blend(rx_color a, rx_color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    
    return (rx_color){
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
        (uint8_t)(a.a + (b.a - a.a) * t)
    };
}

rx_color color_with_alpha(rx_color c, uint8_t alpha) {
    return (rx_color){ c.r, c.g, c.b, alpha };
}

/* ============================================================================
 * Box Functions
 * ============================================================================ */

rx_box box_new(void) {
    rx_box b = {0};
    b.width = -1;  /* Auto */
    b.height = -1;
    b.min_width = 0;
    b.min_height = 0;
    b.max_width = INFINITY;
    b.max_height = INFINITY;
    b.background = RX_COLOR_CLEAR;
    return b;
}

rx_box box_with_size(float width, float height) {
    rx_box b = box_new();
    b.width = width;
    b.height = height;
    return b;
}

void box_set_background(rx_box* box, rx_color color) {
    if (box) box->background = color;
}

void box_set_padding(rx_box* box, rx_edge_insets padding) {
    if (box) box->padding = padding;
}

void box_set_margin(rx_box* box, rx_edge_insets margin) {
    if (box) box->margin = margin;
}

void box_set_border(rx_box* box, rx_border b) {
    if (box) box->border = box_border_all(b);
}

void box_set_corner_radius(rx_box* box, float radius) {
    if (box) box->corner_radius = corners_all(radius);
}

void box_set_shadow(rx_box* box, rx_shadow s) {
    if (box) box->shadow = s;
}

/* ============================================================================
 * View Functions
 * ============================================================================ */

rx_view* view_new(rx_view_kind kind) {
    rx_view* v = (rx_view*)calloc(1, sizeof(rx_view));
    if (!v) return NULL;
    
    v->kind = kind;
    v->box = box_new();
    v->layout = vstack(0);
    v->visible = true;
    v->enabled = true;
    v->child_capacity = 4;
    v->children = (rx_view**)malloc(sizeof(rx_view*) * v->child_capacity);
    
    return v;
}

void view_free(rx_view* view) {
    if (!view) return;
    
    /* Free children recursively */
    for (size_t i = 0; i < view->child_count; i++) {
        view_free(view->children[i]);
    }
    free(view->children);
    
    /* Call custom destructor if present */
    if (view->vtable && view->vtable->destroy) {
        view->vtable->destroy(view);
    }
    
    free(view);
}

void view_add_child(rx_view* parent, rx_view* child) {
    if (!parent || !child) return;
    
    /* Grow array if needed */
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = (rx_view**)realloc(
            parent->children, 
            sizeof(rx_view*) * parent->child_capacity
        );
    }
    
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

void view_remove_child(rx_view* parent, rx_view* child) {
    if (!parent || !child) return;
    
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children */
            for (size_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            break;
        }
    }
}

/* Layout algorithm (simplified flexbox) */
void view_layout(rx_view* view, rx_size available) {
    if (!view || !view->visible) return;
    
    /* Calculate content size */
    float content_width = view->box.width >= 0 ? view->box.width : available.width;
    float content_height = view->box.height >= 0 ? view->box.height : available.height;
    
    /* Apply constraints */
    if (content_width < view->box.min_width) content_width = view->box.min_width;
    if (content_height < view->box.min_height) content_height = view->box.min_height;
    if (content_width > view->box.max_width) content_width = view->box.max_width;
    if (content_height > view->box.max_height) content_height = view->box.max_height;
    
    /* Set frame */
    view->box.frame.width = content_width;
    view->box.frame.height = content_height;
    
    /* Layout children */
    if (view->child_count == 0) return;
    
    rx_edge_insets pad = view->box.padding;
    float inner_width = content_width - pad.left - pad.right;
    float inner_height = content_height - pad.top - pad.bottom;
    
    float offset_x = pad.left;
    float offset_y = pad.top;
    
    for (size_t i = 0; i < view->child_count; i++) {
        rx_view* child = view->children[i];
        
        rx_size child_available = {
            view->layout.direction == RX_LAYOUT_HORIZONTAL 
                ? inner_width / view->child_count 
                : inner_width,
            view->layout.direction == RX_LAYOUT_VERTICAL 
                ? inner_height / view->child_count 
                : inner_height
        };
        
        view_layout(child, child_available);
        
        /* Position child */
        child->box.frame.x = offset_x + child->box.margin.left;
        child->box.frame.y = offset_y + child->box.margin.top;
        
        /* Advance offset */
        if (view->layout.direction == RX_LAYOUT_HORIZONTAL) {
            offset_x += child->box.frame.width + 
                        child->box.margin.left + child->box.margin.right +
                        view->layout.gap;
        } else if (view->layout.direction == RX_LAYOUT_VERTICAL) {
            offset_y += child->box.frame.height + 
                        child->box.margin.top + child->box.margin.bottom +
                        view->layout.gap;
        }
        /* Stack: all children at same position */
    }
    
    /* Custom layout if provided */
    if (view->vtable && view->vtable->layout) {
        view->vtable->layout(view, available);
    }
}

void view_render(rx_view* view, void* context) {
    if (!view || !view->visible) return;
    
    /* Custom render if provided */
    if (view->vtable && view->vtable->render) {
        view->vtable->render(view, context);
    }
    
    /* Render children */
    for (size_t i = 0; i < view->child_count; i++) {
        view_render(view->children[i], context);
    }
}

/* ============================================================================
 * Text View Functions
 * ============================================================================ */

rx_text_view* text_view_new(const char* text) {
    rx_text_view* v = (rx_text_view*)calloc(1, sizeof(rx_text_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_TEXT;
    v->base.box = box_new();
    v->base.visible = true;
    v->base.enabled = true;
    
    if (text) {
        v->text = strdup(text);
    }
    
    v->color = RX_COLOR_TEXT;
    v->font_size = 16.0f;
    v->font_weight = 400;
    v->font_family = "System";
    
    return v;
}

void text_view_set_text(rx_text_view* view, const char* text) {
    if (!view) return;
    free(view->text);
    view->text = text ? strdup(text) : NULL;
}

void text_view_set_color(rx_text_view* view, rx_color color) {
    if (view) view->color = color;
}

void text_view_set_font_size(rx_text_view* view, float size) {
    if (view) view->font_size = size;
}

/* ============================================================================
 * Button View Functions
 * ============================================================================ */

rx_button_view* button_view_new(const char* label) {
    rx_button_view* v = (rx_button_view*)calloc(1, sizeof(rx_button_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_BUTTON;
    v->base.box = box_new();
    v->base.box.padding = insets_symmetric(8, 16);
    v->base.box.corner_radius = corners_all(8);
    v->base.visible = true;
    v->base.enabled = true;
    
    if (label) {
        v->label = strdup(label);
    }
    
    v->normal_color = RX_COLOR_PRIMARY;
    v->hover_color = color_lighten(RX_COLOR_PRIMARY, 0.1f);
    v->pressed_color = color_darken(RX_COLOR_PRIMARY, 0.1f);
    v->base.box.background = v->normal_color;
    
    return v;
}

void button_view_set_callback(rx_button_view* view, rx_button_callback cb, void* data) {
    if (!view) return;
    view->on_click = cb;
    view->callback_data = data;
}

/* ============================================================================
 * Input View Functions
 * ============================================================================ */

rx_input_view* input_view_new(const char* placeholder) {
    rx_input_view* v = (rx_input_view*)calloc(1, sizeof(rx_input_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_INPUT;
    v->base.box = box_new();
    v->base.box.padding = insets_all(8);
    v->base.box.background = RX_COLOR_SURFACE;
    v->base.box.corner_radius = corners_all(4);
    v->base.box.border = box_border_all(border(1, RX_COLOR_GRAY));
    v->base.visible = true;
    v->base.enabled = true;
    
    if (placeholder) {
        v->placeholder = strdup(placeholder);
    }
    v->value = strdup("");
    v->max_length = 256;
    v->password = false;
    
    return v;
}

void input_view_set_value(rx_input_view* view, const char* value) {
    if (!view) return;
    free(view->value);
    view->value = value ? strdup(value) : strdup("");
}

const char* input_view_get_value(rx_input_view* view) {
    return view ? view->value : "";
}

/* ============================================================================
 * Image View Functions
 * ============================================================================ */

rx_image_view* image_view_new(const char* source) {
    rx_image_view* v = (rx_image_view*)calloc(1, sizeof(rx_image_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_IMAGE;
    v->base.box = box_new();
    v->base.visible = true;
    v->base.enabled = true;
    
    if (source) {
        v->source = strdup(source);
    }
    v->aspect_fit = true;
    
    return v;
}

/* ============================================================================
 * Application Functions
 * ============================================================================ */

rx_app* app_new(const char* name) {
    rx_app* app = (rx_app*)calloc(1, sizeof(rx_app));
    if (!app) return NULL;
    
    app->name = name ? strdup(name) : "REOX App";
    app->running = false;
    
    return app;
}

rx_window* app_create_window(rx_app* app, const char* title, int width, int height) {
    if (!app) return NULL;
    
    rx_window* win = (rx_window*)calloc(1, sizeof(rx_window));
    if (!win) return NULL;
    
    win->title = title ? strdup(title) : "Window";
    win->size = size((float)width, (float)height);
    win->position = point(100, 100);
    win->resizable = true;
    win->fullscreen = false;
    
    /* Add to app's window list */
    app->windows = (rx_window**)realloc(
        app->windows,
        sizeof(rx_window*) * (app->window_count + 1)
    );
    app->windows[app->window_count++] = win;
    
    return win;
}

void app_run(rx_app* app) {
    if (!app) return;
    app->running = true;
    
    /* Main loop would be implemented per-platform */
    /* For now, just process one frame */
    for (size_t i = 0; i < app->window_count; i++) {
        rx_window* win = app->windows[i];
        if (win->root_view) {
            view_layout(win->root_view, win->size);
            view_render(win->root_view, NULL);
        }
    }
}

void app_quit(rx_app* app) {
    if (app) app->running = false;
}

/* ============================================================================
 * Switch View Implementation
 * ============================================================================ */

rx_switch_view* switch_view_new(bool initial_value) {
    rx_switch_view* v = (rx_switch_view*)calloc(1, sizeof(rx_switch_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_CUSTOM;
    v->base.box = box_new();
    v->base.box.width = 50;
    v->base.box.height = 26;
    v->base.visible = true;
    v->base.enabled = true;
    
    v->value = initial_value;
    v->on_color = RX_COLOR_PRIMARY;
    v->off_color = color_hex(0x3A3A3C);
    v->thumb_color = RX_COLOR_WHITE;
    
    return v;
}

void switch_view_set_value(rx_switch_view* view, bool value) {
    if (view) view->value = value;
}

void switch_view_set_callback(rx_switch_view* view, rx_switch_callback cb, void* data) {
    if (!view) return;
    view->on_change = cb;
    view->callback_data = data;
}

/* ============================================================================
 * Progress View Implementation
 * ============================================================================ */

rx_progress_view* progress_view_new(float initial_value) {
    rx_progress_view* v = (rx_progress_view*)calloc(1, sizeof(rx_progress_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_CUSTOM;
    v->base.box = box_new();
    v->base.box.height = 4;
    v->base.visible = true;
    v->base.enabled = true;
    
    v->value = initial_value < 0 ? 0 : (initial_value > 1 ? 1 : initial_value);
    v->indeterminate = false;
    v->track_color = color_hex(0x3A3A3C);
    v->fill_color = RX_COLOR_PRIMARY;
    v->height = 4;
    
    return v;
}

void progress_view_set_value(rx_progress_view* view, float value) {
    if (!view) return;
    view->value = value < 0 ? 0 : (value > 1 ? 1 : value);
    view->indeterminate = false;
}

void progress_view_set_indeterminate(rx_progress_view* view, bool indeterminate) {
    if (view) view->indeterminate = indeterminate;
}

/* ============================================================================
 * List View Implementation
 * ============================================================================ */

rx_list_view* list_view_new(int item_count, float item_height) {
    rx_list_view* v = (rx_list_view*)calloc(1, sizeof(rx_list_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_LIST;
    v->base.box = box_new();
    v->base.visible = true;
    v->base.enabled = true;
    
    v->item_count = item_count;
    v->item_height = item_height;
    v->scroll_offset = 0;
    v->selected_index = -1;
    v->scrollable = true;
    
    return v;
}

void list_view_set_builder(rx_list_view* view, rx_list_item_builder builder, void* data) {
    if (!view) return;
    view->item_builder = builder;
    view->user_data = data;
}

void list_view_set_callback(rx_list_view* view, rx_list_callback cb, void* data) {
    if (!view) return;
    view->on_select = cb;
    if (!view->user_data) view->user_data = data;
}

void list_view_scroll_to(rx_list_view* view, int index) {
    if (!view || index < 0 || index >= view->item_count) return;
    view->scroll_offset = index * view->item_height;
}

void list_view_reload(rx_list_view* view) {
    if (!view) return;
    /* Mark for redraw - would trigger rebuild of visible items */
}

/* ============================================================================
 * Tab View Implementation
 * ============================================================================ */

rx_tab_view* tab_view_new(void) {
    rx_tab_view* v = (rx_tab_view*)calloc(1, sizeof(rx_tab_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_CUSTOM;
    v->base.box = box_new();
    v->base.visible = true;
    v->base.enabled = true;
    
    v->selected_index = 0;
    v->show_icons = false;
    v->scroll_tabs = false;
    
    return v;
}

int tab_view_add_tab(rx_tab_view* view, const char* title, rx_view* content) {
    if (!view) return -1;
    
    view->tabs = (rx_tab_item*)realloc(
        view->tabs,
        sizeof(rx_tab_item) * (view->tab_count + 1)
    );
    
    rx_tab_item* tab = &view->tabs[view->tab_count];
    tab->title = title ? strdup(title) : NULL;
    tab->content = content;
    tab->icon_name = NULL;
    
    if (content) {
        view_add_child(&view->base, content);
    }
    
    return (int)view->tab_count++;
}

void tab_view_set_selected(rx_tab_view* view, int index) {
    if (!view || index < 0 || (size_t)index >= view->tab_count) return;
    view->selected_index = index;
    
    /* Hide all tabs except selected */
    for (size_t i = 0; i < view->tab_count; i++) {
        if (view->tabs[i].content) {
            view->tabs[i].content->visible = (i == (size_t)index);
        }
    }
    
    if (view->on_change) {
        view->on_change(index, view->callback_data);
    }
}

void tab_view_set_callback(rx_tab_view* view, rx_tab_callback cb, void* data) {
    if (!view) return;
    view->on_change = cb;
    view->callback_data = data;
}

void tab_view_set_tab_icon(rx_tab_view* view, int index, const char* icon_name) {
    if (!view || index < 0 || (size_t)index >= view->tab_count) return;
    view->tabs[index].icon_name = icon_name;
    view->show_icons = true;
}

/* ============================================================================
 * Menu View Implementation
 * ============================================================================ */

rx_menu_view* menu_view_new(void) {
    rx_menu_view* v = (rx_menu_view*)calloc(1, sizeof(rx_menu_view));
    if (!v) return NULL;
    
    v->base.kind = RX_VIEW_CUSTOM;
    v->base.box = box_new();
    v->base.box.background = RX_COLOR_SURFACE;
    v->base.box.corner_radius = corners_all(8);
    v->base.box.padding = insets_symmetric(8, 0);
    v->base.visible = false;  /* Hidden by default */
    v->base.enabled = true;
    
    v->hovered_index = -1;
    v->visible = false;
    
    return v;
}

int menu_view_add_item(rx_menu_view* menu, const char* title, const char* shortcut) {
    if (!menu) return -1;
    
    menu->items = (rx_menu_item*)realloc(
        menu->items,
        sizeof(rx_menu_item) * (menu->item_count + 1)
    );
    
    rx_menu_item* item = &menu->items[menu->item_count];
    item->title = title ? strdup(title) : NULL;
    item->shortcut = shortcut ? strdup(shortcut) : NULL;
    item->enabled = true;
    item->separator = false;
    item->submenu = NULL;
    
    return (int)menu->item_count++;
}

int menu_view_add_separator(rx_menu_view* menu) {
    if (!menu) return -1;
    
    menu->items = (rx_menu_item*)realloc(
        menu->items,
        sizeof(rx_menu_item) * (menu->item_count + 1)
    );
    
    rx_menu_item* item = &menu->items[menu->item_count];
    item->title = NULL;
    item->shortcut = NULL;
    item->enabled = false;
    item->separator = true;
    item->submenu = NULL;
    
    return (int)menu->item_count++;
}

void menu_view_add_submenu(rx_menu_view* menu, int index, rx_menu_view* submenu) {
    if (!menu || index < 0 || (size_t)index >= menu->item_count) return;
    menu->items[index].submenu = submenu;
}

void menu_view_show(rx_menu_view* menu, rx_point position) {
    if (!menu) return;
    menu->anchor = position;
    menu->base.box.frame.x = position.x;
    menu->base.box.frame.y = position.y;
    menu->visible = true;
    menu->base.visible = true;
}

void menu_view_hide(rx_menu_view* menu) {
    if (!menu) return;
    menu->visible = false;
    menu->base.visible = false;
    menu->hovered_index = -1;
}

void menu_view_set_callback(rx_menu_view* menu, rx_menu_callback cb, void* data) {
    if (!menu) return;
    menu->on_select = cb;
    menu->callback_data = data;
}

/* ============================================================================
 * Container Helper Implementations
 * ============================================================================ */

rx_view* hstack_new(float gap) {
    rx_view* v = view_new(RX_VIEW_BOX);
    if (!v) return NULL;
    v->layout = (rx_layout){ RX_LAYOUT_HORIZONTAL, RX_ALIGN_START, RX_ALIGN_CENTER, gap, {0,0,0,0} };
    return v;
}

rx_view* vstack_new(float gap) {
    rx_view* v = view_new(RX_VIEW_BOX);
    if (!v) return NULL;
    v->layout = (rx_layout){ RX_LAYOUT_VERTICAL, RX_ALIGN_START, RX_ALIGN_START, gap, {0,0,0,0} };
    return v;
}

rx_view* zstack_new(void) {
    rx_view* v = view_new(RX_VIEW_BOX);
    if (!v) return NULL;
    v->layout = (rx_layout){ RX_LAYOUT_STACK, RX_ALIGN_CENTER, RX_ALIGN_CENTER, 0, {0,0,0,0} };
    return v;
}

rx_view* spacer_new(void) {
    rx_view* v = view_new(RX_VIEW_BOX);
    if (!v) return NULL;
    /* Spacer takes available space - flex: 1 equivalent */
    v->box.min_width = 0;
    v->box.min_height = 0;
    return v;
}

rx_view* divider_new(bool horizontal) {
    rx_view* v = view_new(RX_VIEW_BOX);
    if (!v) return NULL;
    
    if (horizontal) {
        v->box.height = 1;
        v->box.width = -1;  /* Auto */
    } else {
        v->box.width = 1;
        v->box.height = -1;  /* Auto */
    }
    v->box.background = color_hex(0x3A3A3C);
    
    return v;
}

