/*
 * ReoxUI - Reactive Component UI for NeolyxOS
 * 
 * Redux-inspired reactive UI framework with component composition.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef REOX_H
#define REOX_H

#include <stdint.h>

/* ============ Component Types ============ */

typedef enum {
    REOX_CONTAINER,    /* Generic container */
    REOX_HSTACK,       /* Horizontal stack */
    REOX_VSTACK,       /* Vertical stack */
    REOX_TEXT,         /* Text label */
    REOX_BUTTON,       /* Clickable button */
    REOX_ICON,         /* NXI icon */
    REOX_IMAGE,        /* Bitmap image */
    REOX_INPUT,        /* Text input */
    REOX_LIST,         /* Scrollable list */
    REOX_LIST_ITEM,    /* List item */
    REOX_DIVIDER,      /* Horizontal/vertical divider */
    REOX_SPACER,       /* Flexible spacer */
} reox_type_t;

/* ============ Layout Properties ============ */

typedef enum {
    REOX_ALIGN_START,
    REOX_ALIGN_CENTER,
    REOX_ALIGN_END,
    REOX_ALIGN_STRETCH,
} reox_align_t;

typedef enum {
    REOX_JUSTIFY_START,
    REOX_JUSTIFY_CENTER,
    REOX_JUSTIFY_END,
    REOX_JUSTIFY_BETWEEN,
    REOX_JUSTIFY_AROUND,
} reox_justify_t;

/* ============ Styling ============ */

typedef struct {
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t border_color;
    uint8_t  border_width;
    uint8_t  border_radius;
    uint8_t  padding_x;
    uint8_t  padding_y;
    uint8_t  margin_x;
    uint8_t  margin_y;
} reox_style_t;

/* Default styles */
#define REOX_STYLE_DEFAULT { \
    .bg_color = 0x00000000, \
    .fg_color = 0xFFFFFFFF, \
    .border_color = 0xFF4D4D5D, \
    .border_width = 0, \
    .border_radius = 0, \
    .padding_x = 0, \
    .padding_y = 0, \
    .margin_x = 0, \
    .margin_y = 0, \
}

#define REOX_STYLE_BUTTON { \
    .bg_color = 0xFF4D7FFF, \
    .fg_color = 0xFFFFFFFF, \
    .border_color = 0xFF3D6FEF, \
    .border_width = 1, \
    .border_radius = 4, \
    .padding_x = 12, \
    .padding_y = 6, \
    .margin_x = 0, \
    .margin_y = 0, \
}

#define REOX_STYLE_CARD { \
    .bg_color = 0xFF2D2D3D, \
    .fg_color = 0xFFFFFFFF, \
    .border_color = 0xFF4D4D5D, \
    .border_width = 1, \
    .border_radius = 8, \
    .padding_x = 16, \
    .padding_y = 12, \
    .margin_x = 8, \
    .margin_y = 8, \
}

/* ============ Component Structure ============ */

#define REOX_MAX_CHILDREN 32
#define REOX_MAX_TEXT 128

typedef struct reox_node {
    reox_type_t type;
    uint32_t id;
    
    /* Layout */
    int16_t x, y;           /* Computed position */
    uint16_t width, height; /* Computed size */
    uint16_t min_width, min_height;
    uint16_t max_width, max_height;
    uint8_t flex;           /* Flex grow factor */
    reox_align_t align;
    reox_justify_t justify;
    
    /* Styling */
    reox_style_t style;
    
    /* Content (based on type) */
    union {
        /* REOX_TEXT */
        struct {
            char text[REOX_MAX_TEXT];
            uint8_t font_size;
        } text;
        
        /* REOX_BUTTON */
        struct {
            char label[32];
            void (*on_click)(struct reox_node *node, void *ctx);
            void *click_ctx;
            uint8_t pressed;
            uint8_t hovered;
        } button;
        
        /* REOX_ICON */
        struct {
            uint32_t icon_id;
            uint8_t size;
            uint32_t tint;
        } icon;
        
        /* REOX_LIST */
        struct {
            int32_t scroll_offset;
            int32_t item_height;
            int32_t selected_idx;
        } list;
        
        /* REOX_INPUT */
        struct {
            char value[REOX_MAX_TEXT];
            char placeholder[32];
            uint8_t cursor_pos;
            uint8_t focused;
        } input;
    } data;
    
    /* Children */
    struct reox_node *children[REOX_MAX_CHILDREN];
    uint8_t child_count;
    
    /* Parent reference */
    struct reox_node *parent;
    
    /* Flags */
    uint8_t visible;
    uint8_t dirty;    /* Needs re-render */
} reox_node_t;

/* ============ API Functions ============ */

/* Create nodes */
reox_node_t* reox_container(void);
reox_node_t* reox_hstack(uint8_t gap);
reox_node_t* reox_vstack(uint8_t gap);
reox_node_t* reox_text(const char *text, uint8_t font_size);
reox_node_t* reox_button(const char *label, void (*on_click)(reox_node_t*, void*), void *ctx);
reox_node_t* reox_icon(uint32_t icon_id, uint8_t size);
reox_node_t* reox_list(int32_t item_height);
reox_node_t* reox_list_item(const char *text);
reox_node_t* reox_input(const char *placeholder);
reox_node_t* reox_divider(void);
reox_node_t* reox_spacer(void);

/* Build tree */
void reox_add_child(reox_node_t *parent, reox_node_t *child);
void reox_remove_child(reox_node_t *parent, reox_node_t *child);

/* Styling */
void reox_set_style(reox_node_t *node, reox_style_t style);
void reox_set_size(reox_node_t *node, uint16_t w, uint16_t h);
void reox_set_flex(reox_node_t *node, uint8_t flex);

/* Layout */
void reox_layout(reox_node_t *root, int x, int y, int w, int h);

/* Rendering */
void reox_render(reox_node_t *root, volatile uint32_t *fb, uint32_t pitch,
                 uint32_t fb_width, uint32_t fb_height);

/* Events */
int reox_handle_click(reox_node_t *root, int x, int y);
int reox_handle_key(reox_node_t *root, uint8_t key);

/* Cleanup */
void reox_free(reox_node_t *node);

/* ============ Global State ============ */

/* Theme colors */
typedef struct {
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t fg_primary;
    uint32_t fg_secondary;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t error;
    uint32_t success;
    uint32_t warning;
    uint32_t border;
} reox_theme_t;

/* Get current theme */
const reox_theme_t* reox_get_theme(void);

/* Set theme */
void reox_set_theme(const reox_theme_t *theme);

#endif /* REOX_H */
