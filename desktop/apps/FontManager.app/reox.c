/*
 * ReoxUI Implementation
 * 
 * Reactive component rendering and layout engine.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "reox.h"

/* ============ Memory Management ============ */

#ifdef __KERNEL__
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
#define reox_alloc(s) kmalloc(s)
#define reox_dealloc(p) kfree(p)
#else
#include <stdlib.h>
#define reox_alloc(s) malloc(s)
#define reox_dealloc(p) free(p)
#endif

/* ============ String Helpers ============ */

static void reox_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int reox_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

/* ============ Theme ============ */

static reox_theme_t g_theme = {
    .bg_primary = 0xFF1E1E2E,
    .bg_secondary = 0xFF2D2D3D,
    .bg_tertiary = 0xFF3D3D4D,
    .fg_primary = 0xFFFFFFFF,
    .fg_secondary = 0xFFAAAAAA,
    .accent = 0xFF4D7FFF,
    .accent_hover = 0xFF5D8FFF,
    .error = 0xFFFF5555,
    .success = 0xFF55FF55,
    .warning = 0xFFFFCC00,
    .border = 0xFF4D4D5D,
};

const reox_theme_t* reox_get_theme(void) {
    return &g_theme;
}

void reox_set_theme(const reox_theme_t *theme) {
    if (theme) g_theme = *theme;
}

/* ============ Node Creation ============ */

static uint32_t g_next_id = 1;

static reox_node_t* create_node(reox_type_t type) {
    reox_node_t *node = (reox_node_t*)reox_alloc(sizeof(reox_node_t));
    if (!node) return NULL;
    
    /* Zero init */
    for (uint32_t i = 0; i < sizeof(reox_node_t); i++) {
        ((uint8_t*)node)[i] = 0;
    }
    
    node->type = type;
    node->id = g_next_id++;
    node->visible = 1;
    node->dirty = 1;
    
    return node;
}

reox_node_t* reox_container(void) {
    return create_node(REOX_CONTAINER);
}

reox_node_t* reox_hstack(uint8_t gap) {
    reox_node_t *node = create_node(REOX_HSTACK);
    if (node) node->style.margin_x = gap;
    return node;
}

reox_node_t* reox_vstack(uint8_t gap) {
    reox_node_t *node = create_node(REOX_VSTACK);
    if (node) node->style.margin_y = gap;
    return node;
}

reox_node_t* reox_text(const char *text, uint8_t font_size) {
    reox_node_t *node = create_node(REOX_TEXT);
    if (node) {
        reox_strcpy(node->data.text.text, text, REOX_MAX_TEXT);
        node->data.text.font_size = font_size ? font_size : 14;
        node->style.fg_color = g_theme.fg_primary;
    }
    return node;
}

reox_node_t* reox_button(const char *label, void (*on_click)(reox_node_t*, void*), void *ctx) {
    reox_node_t *node = create_node(REOX_BUTTON);
    if (node) {
        reox_strcpy(node->data.button.label, label, 32);
        node->data.button.on_click = on_click;
        node->data.button.click_ctx = ctx;
        node->style.bg_color = g_theme.accent;
        node->style.fg_color = g_theme.fg_primary;
        node->style.padding_x = 12;
        node->style.padding_y = 6;
        node->style.border_radius = 4;
    }
    return node;
}

reox_node_t* reox_icon(uint32_t icon_id, uint8_t size) {
    reox_node_t *node = create_node(REOX_ICON);
    if (node) {
        node->data.icon.icon_id = icon_id;
        node->data.icon.size = size ? size : 24;
        node->data.icon.tint = g_theme.fg_primary;
        node->width = size;
        node->height = size;
    }
    return node;
}

reox_node_t* reox_list(int32_t item_height) {
    reox_node_t *node = create_node(REOX_LIST);
    if (node) {
        node->data.list.item_height = item_height ? item_height : 32;
        node->data.list.selected_idx = -1;
        node->style.bg_color = g_theme.bg_primary;
    }
    return node;
}

reox_node_t* reox_list_item(const char *text) {
    reox_node_t *node = create_node(REOX_LIST_ITEM);
    if (node) {
        reox_strcpy(node->data.text.text, text, REOX_MAX_TEXT);
        node->style.padding_x = 8;
        node->style.padding_y = 4;
    }
    return node;
}

reox_node_t* reox_input(const char *placeholder) {
    reox_node_t *node = create_node(REOX_INPUT);
    if (node) {
        reox_strcpy(node->data.input.placeholder, placeholder, 32);
        node->style.bg_color = g_theme.bg_tertiary;
        node->style.border_color = g_theme.border;
        node->style.border_width = 1;
        node->style.padding_x = 8;
        node->style.padding_y = 4;
    }
    return node;
}

reox_node_t* reox_divider(void) {
    reox_node_t *node = create_node(REOX_DIVIDER);
    if (node) {
        node->height = 1;
        node->style.bg_color = g_theme.border;
    }
    return node;
}

reox_node_t* reox_spacer(void) {
    reox_node_t *node = create_node(REOX_SPACER);
    if (node) node->flex = 1;
    return node;
}

/* ============ Tree Building ============ */

void reox_add_child(reox_node_t *parent, reox_node_t *child) {
    if (!parent || !child) return;
    if (parent->child_count >= REOX_MAX_CHILDREN) return;
    
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    parent->dirty = 1;
}

void reox_remove_child(reox_node_t *parent, reox_node_t *child) {
    if (!parent || !child) return;
    
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children */
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            parent->dirty = 1;
            break;
        }
    }
}

/* ============ Styling ============ */

void reox_set_style(reox_node_t *node, reox_style_t style) {
    if (!node) return;
    node->style = style;
    node->dirty = 1;
}

void reox_set_size(reox_node_t *node, uint16_t w, uint16_t h) {
    if (!node) return;
    node->min_width = w;
    node->min_height = h;
    node->dirty = 1;
}

void reox_set_flex(reox_node_t *node, uint8_t flex) {
    if (!node) return;
    node->flex = flex;
    node->dirty = 1;
}

/* ============ Layout ============ */

void reox_layout(reox_node_t *root, int x, int y, int w, int h) {
    if (!root) return;
    
    root->x = x;
    root->y = y;
    root->width = w;
    root->height = h;
    
    int cx = x + root->style.padding_x;
    int cy = y + root->style.padding_y;
    int content_w = w - 2 * root->style.padding_x;
    int content_h = h - 2 * root->style.padding_y;
    
    if (root->type == REOX_HSTACK) {
        /* Horizontal layout */
        int total_flex = 0;
        int fixed_width = 0;
        
        for (int i = 0; i < root->child_count; i++) {
            if (root->children[i]->flex > 0) {
                total_flex += root->children[i]->flex;
            } else {
                fixed_width += root->children[i]->min_width + root->style.margin_x;
            }
        }
        
        int flex_space = content_w - fixed_width;
        
        for (int i = 0; i < root->child_count; i++) {
            reox_node_t *child = root->children[i];
            int child_w = child->min_width;
            
            if (child->flex > 0 && total_flex > 0) {
                child_w = (flex_space * child->flex) / total_flex;
            }
            
            reox_layout(child, cx, cy, child_w, content_h);
            cx += child_w + root->style.margin_x;
        }
    } else if (root->type == REOX_VSTACK) {
        /* Vertical layout */
        int total_flex = 0;
        int fixed_height = 0;
        
        for (int i = 0; i < root->child_count; i++) {
            if (root->children[i]->flex > 0) {
                total_flex += root->children[i]->flex;
            } else {
                fixed_height += root->children[i]->min_height + root->style.margin_y;
            }
        }
        
        int flex_space = content_h - fixed_height;
        
        for (int i = 0; i < root->child_count; i++) {
            reox_node_t *child = root->children[i];
            int child_h = child->min_height;
            
            if (child->flex > 0 && total_flex > 0) {
                child_h = (flex_space * child->flex) / total_flex;
            }
            
            reox_layout(child, cx, cy, content_w, child_h);
            cy += child_h + root->style.margin_y;
        }
    } else {
        /* Stack all children */
        for (int i = 0; i < root->child_count; i++) {
            reox_layout(root->children[i], cx, cy, content_w, content_h);
        }
    }
    
    root->dirty = 0;
}

/* ============ Rendering Helpers ============ */

#ifdef __KERNEL__
extern uint32_t fb_width, fb_height, fb_pitch;
extern volatile uint32_t *framebuffer;
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void nxi_render(const void *icon, int x, int y, int size,
                       volatile uint32_t *fb, uint32_t pitch,
                       uint32_t fb_width, uint32_t fb_height);
extern const void* nxi_get_icon(uint32_t id);

static void reox_fill_rect(int x, int y, int w, int h, uint32_t color,
                           volatile uint32_t *fb, uint32_t pitch,
                           uint32_t fbw, uint32_t fbh) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < (int)fbw && py >= 0 && py < (int)fbh) {
                fb[py * (pitch / 4) + px] = color;
            }
        }
    }
}

static void reox_draw_rect(int x, int y, int w, int h, uint32_t color,
                           volatile uint32_t *fb, uint32_t pitch,
                           uint32_t fbw, uint32_t fbh) {
    for (int i = 0; i < w; i++) {
        if (x + i >= 0 && x + i < (int)fbw) {
            if (y >= 0 && y < (int)fbh)
                fb[y * (pitch / 4) + x + i] = color;
            if (y + h - 1 >= 0 && y + h - 1 < (int)fbh)
                fb[(y + h - 1) * (pitch / 4) + x + i] = color;
        }
    }
    for (int j = 0; j < h; j++) {
        if (y + j >= 0 && y + j < (int)fbh) {
            if (x >= 0 && x < (int)fbw)
                fb[(y + j) * (pitch / 4) + x] = color;
            if (x + w - 1 >= 0 && x + w - 1 < (int)fbw)
                fb[(y + j) * (pitch / 4) + x + w - 1] = color;
        }
    }
}
#endif

/* ============ Rendering ============ */

void reox_render(reox_node_t *root, volatile uint32_t *fb, uint32_t pitch,
                 uint32_t fbw, uint32_t fbh) {
    if (!root || !root->visible || !fb) return;
    
#ifdef __KERNEL__
    /* Draw background */
    if ((root->style.bg_color & 0xFF000000) != 0) {
        reox_fill_rect(root->x, root->y, root->width, root->height,
                       root->style.bg_color, fb, pitch, fbw, fbh);
    }
    
    /* Draw border */
    if (root->style.border_width > 0) {
        reox_draw_rect(root->x, root->y, root->width, root->height,
                       root->style.border_color, fb, pitch, fbw, fbh);
    }
    
    /* Draw content based on type */
    switch (root->type) {
        case REOX_TEXT:
        case REOX_LIST_ITEM:
            desktop_draw_text(root->x + root->style.padding_x,
                             root->y + root->style.padding_y,
                             root->data.text.text,
                             root->style.fg_color);
            break;
            
        case REOX_BUTTON: {
            int tx = root->x + root->style.padding_x;
            int ty = root->y + root->style.padding_y;
            desktop_draw_text(tx, ty, root->data.button.label, root->style.fg_color);
            break;
        }
            
        case REOX_ICON: {
            const void *icon = nxi_get_icon(root->data.icon.icon_id);
            if (icon) {
                nxi_render(icon, root->x, root->y, root->data.icon.size,
                          fb, pitch, fbw, fbh);
            }
            break;
        }
            
        case REOX_DIVIDER:
            reox_fill_rect(root->x, root->y, root->width, 1,
                          root->style.bg_color, fb, pitch, fbw, fbh);
            break;
            
        default:
            break;
    }
    
    /* Render children */
    for (int i = 0; i < root->child_count; i++) {
        reox_render(root->children[i], fb, pitch, fbw, fbh);
    }
#endif
}

/* ============ Events ============ */

int reox_handle_click(reox_node_t *root, int x, int y) {
    if (!root || !root->visible) return 0;
    
    /* Check if click is within bounds */
    if (x < root->x || x >= root->x + root->width ||
        y < root->y || y >= root->y + root->height) {
        return 0;
    }
    
    /* Check children first (reverse order for z-order) */
    for (int i = root->child_count - 1; i >= 0; i--) {
        if (reox_handle_click(root->children[i], x, y)) {
            return 1;
        }
    }
    
    /* Handle this node */
    if (root->type == REOX_BUTTON && root->data.button.on_click) {
        root->data.button.on_click(root, root->data.button.click_ctx);
        return 1;
    }
    
    if (root->type == REOX_LIST_ITEM && root->parent) {
        /* Find index in parent list */
        for (int i = 0; i < root->parent->child_count; i++) {
            if (root->parent->children[i] == root) {
                root->parent->data.list.selected_idx = i;
                return 1;
            }
        }
    }
    
    return 0;
}

int reox_handle_key(reox_node_t *root, uint8_t key) {
    (void)root;
    (void)key;
    /* TODO: Handle keyboard input for focused input fields */
    return 0;
}

/* ============ Cleanup ============ */

void reox_free(reox_node_t *node) {
    if (!node) return;
    
    /* Free children first */
    for (int i = 0; i < node->child_count; i++) {
        reox_free(node->children[i]);
    }
    
    reox_dealloc(node);
}
