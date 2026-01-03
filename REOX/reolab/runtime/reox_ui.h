/*
 * REOX UI Runtime Library - Header
 * Native UI system for NeolyxOS
 * SwiftUI-like declarative UI in C
 */

#ifndef REOX_UI_H
#define REOX_UI_H

#include "reox_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Color System
 * ============================================================================ */

/* RGBA Color (0-255 per channel) */
typedef struct rx_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} rx_color;

/* Create color from RGB */
RX_INLINE RX_CONST rx_color color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (rx_color){ r, g, b, 255 };
}

/* Create color from RGBA */
RX_INLINE RX_CONST rx_color color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (rx_color){ r, g, b, a };
}

/* Create color from hex (0xRRGGBB) */
RX_INLINE RX_CONST rx_color color_hex(uint32_t hex) {
    return (rx_color){
        (hex >> 16) & 0xFF,
        (hex >> 8) & 0xFF,
        hex & 0xFF,
        255
    };
}

/* Create color from hex with alpha (0xRRGGBBAA) */
RX_INLINE RX_CONST rx_color color_hex_alpha(uint32_t hex) {
    return (rx_color){
        (hex >> 24) & 0xFF,
        (hex >> 16) & 0xFF,
        (hex >> 8) & 0xFF,
        hex & 0xFF
    };
}

/* Predefined colors */
#define RX_COLOR_BLACK      ((rx_color){ 0, 0, 0, 255 })
#define RX_COLOR_WHITE      ((rx_color){ 255, 255, 255, 255 })
#define RX_COLOR_RED        ((rx_color){ 255, 0, 0, 255 })
#define RX_COLOR_GREEN      ((rx_color){ 0, 255, 0, 255 })
#define RX_COLOR_BLUE       ((rx_color){ 0, 0, 255, 255 })
#define RX_COLOR_YELLOW     ((rx_color){ 255, 255, 0, 255 })
#define RX_COLOR_CYAN       ((rx_color){ 0, 255, 255, 255 })
#define RX_COLOR_MAGENTA    ((rx_color){ 255, 0, 255, 255 })
#define RX_COLOR_ORANGE     ((rx_color){ 255, 165, 0, 255 })
#define RX_COLOR_PURPLE     ((rx_color){ 128, 0, 128, 255 })
#define RX_COLOR_GRAY       ((rx_color){ 128, 128, 128, 255 })
#define RX_COLOR_CLEAR      ((rx_color){ 0, 0, 0, 0 })

/* NeolyxOS System Colors */
#define RX_COLOR_PRIMARY    ((rx_color){ 0, 122, 255, 255 })
#define RX_COLOR_SECONDARY  ((rx_color){ 88, 86, 214, 255 })
#define RX_COLOR_ACCENT     ((rx_color){ 255, 69, 58, 255 })
#define RX_COLOR_BACKGROUND ((rx_color){ 28, 28, 30, 255 })
#define RX_COLOR_SURFACE    ((rx_color){ 44, 44, 46, 255 })
#define RX_COLOR_TEXT       ((rx_color){ 255, 255, 255, 255 })
#define RX_COLOR_TEXT_DIM   ((rx_color){ 174, 174, 178, 255 })

/* Color manipulation */
extern rx_color color_lighten(rx_color c, float amount);
extern rx_color color_darken(rx_color c, float amount);
extern rx_color color_blend(rx_color a, rx_color b, float t);
extern rx_color color_with_alpha(rx_color c, uint8_t alpha);

/* ============================================================================
 * Geometry Types
 * ============================================================================ */

/* 2D Point */
typedef struct rx_point {
    float x;
    float y;
} rx_point;

/* 2D Size */
typedef struct rx_size {
    float width;
    float height;
} rx_size;

/* Rectangle */
typedef struct rx_rect {
    float x;
    float y;
    float width;
    float height;
} rx_rect;

/* Edge Insets (padding/margin) */
typedef struct rx_edge_insets {
    float top;
    float right;
    float bottom;
    float left;
} rx_edge_insets;

/* Corner Radii */
typedef struct rx_corner_radii {
    float top_left;
    float top_right;
    float bottom_right;
    float bottom_left;
} rx_corner_radii;

/* Constructors */
RX_INLINE rx_point point(float x, float y) {
    return (rx_point){ x, y };
}

RX_INLINE rx_size size(float w, float h) {
    return (rx_size){ w, h };
}

RX_INLINE rx_rect rect(float x, float y, float w, float h) {
    return (rx_rect){ x, y, w, h };
}

RX_INLINE rx_edge_insets insets(float top, float right, float bottom, float left) {
    return (rx_edge_insets){ top, right, bottom, left };
}

RX_INLINE rx_edge_insets insets_all(float value) {
    return (rx_edge_insets){ value, value, value, value };
}

RX_INLINE rx_edge_insets insets_symmetric(float vertical, float horizontal) {
    return (rx_edge_insets){ vertical, horizontal, vertical, horizontal };
}

RX_INLINE rx_corner_radii corners(float tl, float tr, float br, float bl) {
    return (rx_corner_radii){ tl, tr, br, bl };
}

RX_INLINE rx_corner_radii corners_all(float value) {
    return (rx_corner_radii){ value, value, value, value };
}

/* ============================================================================
 * Border System
 * ============================================================================ */

/* Border style */
typedef enum rx_border_style {
    RX_BORDER_NONE,
    RX_BORDER_SOLID,
    RX_BORDER_DASHED,
    RX_BORDER_DOTTED,
} rx_border_style;

/* Border definition */
typedef struct rx_border {
    float width;
    rx_color color;
    rx_border_style style;
} rx_border;

/* Box border (each side) */
typedef struct rx_box_border {
    rx_border top;
    rx_border right;
    rx_border bottom;
    rx_border left;
} rx_box_border;

RX_INLINE rx_border border(float width, rx_color color) {
    return (rx_border){ width, color, RX_BORDER_SOLID };
}

RX_INLINE rx_border border_styled(float width, rx_color color, rx_border_style style) {
    return (rx_border){ width, color, style };
}

RX_INLINE rx_box_border box_border_all(rx_border b) {
    return (rx_box_border){ b, b, b, b };
}

/* ============================================================================
 * Shadow System
 * ============================================================================ */

typedef struct rx_shadow {
    float offset_x;
    float offset_y;
    float blur_radius;
    float spread_radius;
    rx_color color;
} rx_shadow;

RX_INLINE rx_shadow shadow(float x, float y, float blur, rx_color color) {
    return (rx_shadow){ x, y, blur, 0, color };
}

/* ============================================================================
 * Box Model (CSS-like)
 * ============================================================================ */

typedef struct rx_box {
    /* Content size (auto = -1) */
    float width;
    float height;
    float min_width;
    float min_height;
    float max_width;
    float max_height;
    
    /* Spacing */
    rx_edge_insets padding;
    rx_edge_insets margin;
    
    /* Appearance */
    rx_color background;
    rx_box_border border;
    rx_corner_radii corner_radius;
    rx_shadow shadow;
    
    /* Computed bounds */
    rx_rect frame;
} rx_box;

/* Box builder */
extern rx_box box_new(void);
extern rx_box box_with_size(float width, float height);
extern void box_set_background(rx_box* box, rx_color color);
extern void box_set_padding(rx_box* box, rx_edge_insets padding);
extern void box_set_margin(rx_box* box, rx_edge_insets margin);
extern void box_set_border(rx_box* box, rx_border b);
extern void box_set_corner_radius(rx_box* box, float radius);
extern void box_set_shadow(rx_box* box, rx_shadow s);

/* ============================================================================
 * Layout System
 * ============================================================================ */

/* Alignment */
typedef enum rx_alignment {
    RX_ALIGN_START,
    RX_ALIGN_CENTER,
    RX_ALIGN_END,
    RX_ALIGN_STRETCH,
    RX_ALIGN_SPACE_BETWEEN,
    RX_ALIGN_SPACE_AROUND,
    RX_ALIGN_SPACE_EVENLY,
} rx_alignment;

/* Layout direction */
typedef enum rx_layout_direction {
    RX_LAYOUT_HORIZONTAL,
    RX_LAYOUT_VERTICAL,
    RX_LAYOUT_STACK,  /* Z-axis stacking */
} rx_layout_direction;

/* Layout container */
typedef struct rx_layout {
    rx_layout_direction direction;
    rx_alignment main_axis;
    rx_alignment cross_axis;
    float gap;
    rx_edge_insets padding;
} rx_layout;

RX_INLINE rx_layout hstack(float gap) {
    return (rx_layout){ RX_LAYOUT_HORIZONTAL, RX_ALIGN_START, RX_ALIGN_CENTER, gap, {0,0,0,0} };
}

RX_INLINE rx_layout vstack(float gap) {
    return (rx_layout){ RX_LAYOUT_VERTICAL, RX_ALIGN_START, RX_ALIGN_START, gap, {0,0,0,0} };
}

RX_INLINE rx_layout zstack(void) {
    return (rx_layout){ RX_LAYOUT_STACK, RX_ALIGN_CENTER, RX_ALIGN_CENTER, 0, {0,0,0,0} };
}

/* ============================================================================
 * View Types
 * ============================================================================ */

/* View kind */
typedef enum rx_view_kind {
    RX_VIEW_BOX,
    RX_VIEW_TEXT,
    RX_VIEW_IMAGE,
    RX_VIEW_BUTTON,
    RX_VIEW_INPUT,
    RX_VIEW_SCROLL,
    RX_VIEW_LIST,
    RX_VIEW_CUSTOM,
} rx_view_kind;

/* Forward declaration */
struct rx_view;

/* View vtable for custom views */
typedef struct rx_view_vtable {
    void (*render)(struct rx_view* view, void* context);
    void (*layout)(struct rx_view* view, rx_size available);
    void (*on_click)(struct rx_view* view, rx_point pos);
    void (*on_hover)(struct rx_view* view, bool hovered);
    void (*destroy)(struct rx_view* view);
} rx_view_vtable;

/* Base view structure */
typedef struct rx_view {
    rx_view_kind kind;
    rx_box box;
    rx_layout layout;
    
    /* Children */
    struct rx_view** children;
    size_t child_count;
    size_t child_capacity;
    
    /* Parent */
    struct rx_view* parent;
    
    /* State */
    bool visible;
    bool enabled;
    bool hovered;
    bool focused;
    
    /* Custom data */
    void* user_data;
    rx_view_vtable* vtable;
} rx_view;

/* View lifecycle */
extern rx_view* view_new(rx_view_kind kind);
extern void view_free(rx_view* view);
extern void view_add_child(rx_view* parent, rx_view* child);
extern void view_remove_child(rx_view* parent, rx_view* child);
extern void view_layout(rx_view* view, rx_size available);
extern void view_render(rx_view* view, void* context);

/* ============================================================================
 * Text View
 * ============================================================================ */

typedef struct rx_text_view {
    rx_view base;
    char* text;
    rx_color color;
    float font_size;
    int font_weight;  /* 100-900 */
    const char* font_family;
} rx_text_view;

extern rx_text_view* text_view_new(const char* text);
extern void text_view_set_text(rx_text_view* view, const char* text);
extern void text_view_set_color(rx_text_view* view, rx_color color);
extern void text_view_set_font_size(rx_text_view* view, float size);

/* ============================================================================
 * Button View
 * ============================================================================ */

typedef void (*rx_button_callback)(void* user_data);

typedef struct rx_button_view {
    rx_view base;
    char* label;
    rx_button_callback on_click;
    void* callback_data;
    rx_color normal_color;
    rx_color hover_color;
    rx_color pressed_color;
} rx_button_view;

extern rx_button_view* button_view_new(const char* label);
extern void button_view_set_callback(rx_button_view* view, rx_button_callback cb, void* data);

/* ============================================================================
 * Input View
 * ============================================================================ */

typedef void (*rx_input_callback)(const char* value, void* user_data);

typedef struct rx_input_view {
    rx_view base;
    char* value;
    char* placeholder;
    rx_input_callback on_change;
    void* callback_data;
    size_t max_length;
    bool password;
} rx_input_view;

extern rx_input_view* input_view_new(const char* placeholder);
extern void input_view_set_value(rx_input_view* view, const char* value);
extern const char* input_view_get_value(rx_input_view* view);

/* ============================================================================
 * Image View
 * ============================================================================ */

typedef struct rx_image_view {
    rx_view base;
    const char* source;
    void* texture_handle;
    rx_size natural_size;
    bool aspect_fit;
} rx_image_view;

extern rx_image_view* image_view_new(const char* source);

/* ============================================================================
 * Switch View (Toggle)
 * ============================================================================ */

typedef void (*rx_switch_callback)(bool value, void* user_data);

typedef struct rx_switch_view {
    rx_view base;
    bool value;
    rx_switch_callback on_change;
    void* callback_data;
    rx_color on_color;
    rx_color off_color;
    rx_color thumb_color;
} rx_switch_view;

extern rx_switch_view* switch_view_new(bool initial_value);
extern void switch_view_set_value(rx_switch_view* view, bool value);
extern void switch_view_set_callback(rx_switch_view* view, rx_switch_callback cb, void* data);

/* ============================================================================
 * Progress View (Progress Bar)
 * ============================================================================ */

typedef struct rx_progress_view {
    rx_view base;
    float value;           /* 0.0 to 1.0 */
    bool indeterminate;    /* Animated loading state */
    rx_color track_color;
    rx_color fill_color;
    float height;
} rx_progress_view;

extern rx_progress_view* progress_view_new(float initial_value);
extern void progress_view_set_value(rx_progress_view* view, float value);
extern void progress_view_set_indeterminate(rx_progress_view* view, bool indeterminate);

/* ============================================================================
 * List View (Scrollable List)
 * ============================================================================ */

typedef void (*rx_list_callback)(int index, void* user_data);
typedef rx_view* (*rx_list_item_builder)(int index, void* user_data);

typedef struct rx_list_view {
    rx_view base;
    int item_count;
    float item_height;
    float scroll_offset;
    int selected_index;
    rx_list_callback on_select;
    rx_list_item_builder item_builder;
    void* user_data;
    bool scrollable;
} rx_list_view;

extern rx_list_view* list_view_new(int item_count, float item_height);
extern void list_view_set_builder(rx_list_view* view, rx_list_item_builder builder, void* data);
extern void list_view_set_callback(rx_list_view* view, rx_list_callback cb, void* data);
extern void list_view_scroll_to(rx_list_view* view, int index);
extern void list_view_reload(rx_list_view* view);

/* ============================================================================
 * Tab View (Tab Navigation)
 * ============================================================================ */

typedef void (*rx_tab_callback)(int index, void* user_data);

typedef struct rx_tab_item {
    char* title;
    rx_view* content;
    const char* icon_name;  /* Optional icon */
} rx_tab_item;

typedef struct rx_tab_view {
    rx_view base;
    rx_tab_item* tabs;
    size_t tab_count;
    int selected_index;
    rx_tab_callback on_change;
    void* callback_data;
    bool show_icons;
    bool scroll_tabs;       /* For many tabs */
} rx_tab_view;

extern rx_tab_view* tab_view_new(void);
extern int tab_view_add_tab(rx_tab_view* view, const char* title, rx_view* content);
extern void tab_view_set_selected(rx_tab_view* view, int index);
extern void tab_view_set_callback(rx_tab_view* view, rx_tab_callback cb, void* data);
extern void tab_view_set_tab_icon(rx_tab_view* view, int index, const char* icon_name);

/* ============================================================================
 * Menu View (Dropdown/Context Menu)
 * ============================================================================ */

typedef void (*rx_menu_callback)(int index, void* user_data);

typedef struct rx_menu_item {
    char* title;
    char* shortcut;         /* e.g., "Ctrl+S" */
    bool enabled;
    bool separator;         /* If true, render as separator line */
    struct rx_menu_view* submenu;  /* Optional nested menu */
} rx_menu_item;

typedef struct rx_menu_view {
    rx_view base;
    rx_menu_item* items;
    size_t item_count;
    int hovered_index;
    rx_menu_callback on_select;
    void* callback_data;
    bool visible;
    rx_point anchor;        /* Where menu appears */
} rx_menu_view;

extern rx_menu_view* menu_view_new(void);
extern int menu_view_add_item(rx_menu_view* menu, const char* title, const char* shortcut);
extern int menu_view_add_separator(rx_menu_view* menu);
extern void menu_view_add_submenu(rx_menu_view* menu, int index, rx_menu_view* submenu);
extern void menu_view_show(rx_menu_view* menu, rx_point position);
extern void menu_view_hide(rx_menu_view* menu);
extern void menu_view_set_callback(rx_menu_view* menu, rx_menu_callback cb, void* data);

/* ============================================================================
 * Container Helpers
 * ============================================================================ */

/* Create horizontal stack container */
extern rx_view* hstack_new(float gap);

/* Create vertical stack container */
extern rx_view* vstack_new(float gap);

/* Create z-stack (overlay) container */
extern rx_view* zstack_new(void);

/* Spacer view - flexible empty space */
extern rx_view* spacer_new(void);

/* Divider view - horizontal/vertical line */
extern rx_view* divider_new(bool horizontal);

/* ============================================================================
 * Application & Window
 * ============================================================================ */

typedef struct rx_window {
    const char* title;
    rx_size size;
    rx_point position;
    rx_view* root_view;
    void* native_handle;
    bool resizable;
    bool fullscreen;
} rx_window;

typedef struct rx_app {
    const char* name;
    rx_window** windows;
    size_t window_count;
    bool running;
} rx_app;

extern rx_app* app_new(const char* name);
extern rx_window* app_create_window(rx_app* app, const char* title, int width, int height);
extern void app_run(rx_app* app);
extern void app_quit(rx_app* app);

#ifdef __cplusplus
}
#endif

#endif /* REOX_UI_H */
