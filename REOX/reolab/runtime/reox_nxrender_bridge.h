/*
 * REOX to NXRender Bridge
 * 
 * PURPOSE: Connects REOX-generated C code to NXRender Rust rendering
 * 
 * ARCHITECTURE:
 * ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 * │   REOX      │────▶│   Bridge    │────▶│  NXRender   │
 * │  (C code)   │     │  (C header) │     │  (Rust FFI) │
 * └─────────────┘     └─────────────┘     └─────────────┘
 * 
 * SCOPE: Layout + Input + Redraw (no animations yet)
 */

#ifndef REOX_NXRENDER_BRIDGE_H
#define REOX_NXRENDER_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * NXRender FFI imports (from nxrender-ffi Rust library)
 * ============================================================================ */

/* GPU Context */
typedef void* NxGpuContext;
NxGpuContext nx_gpu_create(void);
NxGpuContext nx_gpu_create_with_size(uint32_t width, uint32_t height);
void nx_gpu_destroy(NxGpuContext ctx);
void nx_gpu_present(NxGpuContext ctx);
void nx_gpu_resize(NxGpuContext ctx, uint32_t width, uint32_t height);

/* NxColor - matches nxrender-ffi */
typedef struct { uint8_t r, g, b, a; } NxColor;
typedef struct { float x, y, width, height; } NxRect;
typedef struct { float x, y; } NxPoint;

/* GPU Drawing */
void nx_gpu_fill_rect(NxGpuContext ctx, NxRect rect, NxColor color);
void nx_gpu_fill_rounded_rect(NxGpuContext ctx, NxRect rect, NxColor color, float radius);
void nx_gpu_fill_circle(NxGpuContext ctx, float x, float y, float radius, NxColor color);
void nx_gpu_draw_text(NxGpuContext ctx, const char* text, float x, float y, NxColor color);
void nx_gpu_clear(NxGpuContext ctx, NxColor color);

/* Theme */
typedef void* NxTheme;
NxTheme nx_theme_light(void);
NxTheme nx_theme_dark(void);
void nx_theme_destroy(NxTheme theme);
NxColor nx_theme_get_primary_color(NxTheme theme);
NxColor nx_theme_get_background_color(NxTheme theme);
NxColor nx_theme_get_surface_color(NxTheme theme);
NxColor nx_theme_get_text_color(NxTheme theme);

/* Mouse */
typedef void* NxMouseState;
typedef enum { NX_MOUSE_LEFT = 0, NX_MOUSE_RIGHT = 1, NX_MOUSE_MIDDLE = 2 } NxMouseButton;
NxMouseState nx_mouse_create(void);
void nx_mouse_destroy(NxMouseState mouse);
void nx_mouse_get_position(NxMouseState mouse, float* x, float* y);
bool nx_mouse_is_button_down(NxMouseState mouse, NxMouseButton button);
void nx_mouse_move(NxMouseState mouse, float x, float y);
void nx_mouse_button_down(NxMouseState mouse, float x, float y, NxMouseButton button);
void nx_mouse_button_up(NxMouseState mouse, float x, float y, NxMouseButton button);

/* Keyboard */
typedef void* NxKeyboardState;
NxKeyboardState nx_keyboard_create(void);
void nx_keyboard_destroy(NxKeyboardState kb);
bool nx_keyboard_is_ctrl(NxKeyboardState kb);
bool nx_keyboard_is_shift(NxKeyboardState kb);
bool nx_keyboard_is_alt(NxKeyboardState kb);

/* ============================================================================
 * REOX UI Node System
 * Minimal ABI for UI tree management
 * ============================================================================ */

/* Node types matching REOX View hierarchy */
typedef enum {
    RX_NODE_VSTACK = 0,
    RX_NODE_HSTACK = 1,
    RX_NODE_ZSTACK = 2,
    RX_NODE_TEXT = 3,
    RX_NODE_BUTTON = 4,
    RX_NODE_TEXTFIELD = 5,
    RX_NODE_CHECKBOX = 6,
    RX_NODE_SLIDER = 7,
    RX_NODE_IMAGE = 8,
    RX_NODE_SPACER = 9,
    RX_NODE_DIVIDER = 10,
    RX_NODE_SCROLL = 11,
    RX_NODE_GRID = 12,
} RxNodeType;

/* Node state flags */
typedef enum {
    RX_STATE_NONE = 0,
    RX_STATE_HOVERED = 1 << 0,
    RX_STATE_PRESSED = 1 << 1,
    RX_STATE_FOCUSED = 1 << 2,
    RX_STATE_DISABLED = 1 << 3,
    RX_STATE_HIDDEN = 1 << 4,
    RX_STATE_DIRTY = 1 << 5,    /* Needs redraw */
} RxNodeState;

/* UI Node - the core building block */
typedef struct RxNode {
    uint64_t id;                /* Unique node ID */
    RxNodeType type;            /* Node type */
    uint32_t state;             /* State flags */
    
    /* Bounds (computed by layout) */
    float x, y, width, height;
    
    /* Style */
    NxColor background;
    NxColor foreground;
    float corner_radius;
    float padding;
    float gap;                  /* For stacks */
    
    /* Content (type-specific) */
    char* text;                 /* For TEXT, BUTTON, TEXTFIELD */
    float value;                /* For SLIDER, CHECKBOX */
    
    /* Tree structure */
    struct RxNode* parent;
    struct RxNode* first_child;
    struct RxNode* next_sibling;
    
    /* Callbacks (as function pointers) */
    void (*on_click)(struct RxNode* node);
    void (*on_change)(struct RxNode* node, float value);
} RxNode;

/* ============================================================================
 * REOX Bridge API
 * These functions connect REOX code to NXRender
 * ============================================================================ */

/* Global bridge state */
typedef struct {
    NxGpuContext gpu;
    NxTheme theme;
    NxMouseState mouse;
    NxKeyboardState keyboard;
    RxNode* root;
    RxNode* focused;
    RxNode* hovered;
    uint64_t next_node_id;
    bool needs_redraw;
} RxBridge;

/* Initialize bridge */
static RxBridge* rx_bridge = NULL;

static inline void rx_bridge_init(uint32_t width, uint32_t height) {
    rx_bridge = (RxBridge*)calloc(1, sizeof(RxBridge));
    rx_bridge->gpu = nx_gpu_create_with_size(width, height);
    rx_bridge->theme = nx_theme_dark();  /* NeolyxOS default */
    rx_bridge->mouse = nx_mouse_create();
    rx_bridge->keyboard = nx_keyboard_create();
    rx_bridge->next_node_id = 1;
    rx_bridge->needs_redraw = true;
}

static inline void rx_bridge_destroy(void) {
    if (!rx_bridge) return;
    nx_gpu_destroy(rx_bridge->gpu);
    nx_theme_destroy(rx_bridge->theme);
    nx_mouse_destroy(rx_bridge->mouse);
    nx_keyboard_destroy(rx_bridge->keyboard);
    free(rx_bridge);
    rx_bridge = NULL;
}

/* ============================================================================
 * Node Creation
 * ============================================================================ */

static inline RxNode* rx_node_create(RxNodeType type) {
    RxNode* node = (RxNode*)calloc(1, sizeof(RxNode));
    node->id = rx_bridge->next_node_id++;
    node->type = type;
    node->state = RX_STATE_DIRTY;
    node->background = (NxColor){0, 0, 0, 0};
    node->foreground = nx_theme_get_text_color(rx_bridge->theme);
    return node;
}

static inline void rx_node_destroy(RxNode* node) {
    if (!node) return;
    
    /* Destroy children first */
    RxNode* child = node->first_child;
    while (child) {
        RxNode* next = child->next_sibling;
        rx_node_destroy(child);
        child = next;
    }
    
    if (node->text) free(node->text);
    free(node);
}

/* Node hierarchy */
static inline void rx_node_add_child(RxNode* parent, RxNode* child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    child->next_sibling = NULL;
    
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        RxNode* last = parent->first_child;
        while (last->next_sibling) last = last->next_sibling;
        last->next_sibling = child;
    }
}

/* ============================================================================
 * Layout Engine (Flexbox-style)
 * ============================================================================ */

static inline void rx_layout_node(RxNode* node, float available_w, float available_h) {
    if (!node) return;
    
    float padding = node->padding;
    float inner_w = available_w - padding * 2;
    float inner_h = available_h - padding * 2;
    
    /* Count children */
    int child_count = 0;
    RxNode* child = node->first_child;
    while (child) {
        if (!(child->state & RX_STATE_HIDDEN)) child_count++;
        child = child->next_sibling;
    }
    
    if (child_count == 0) return;
    
    float gap = node->gap;
    float total_gap = gap * (child_count - 1);
    
    switch (node->type) {
        case RX_NODE_VSTACK: {
            float child_h = (inner_h - total_gap) / child_count;
            float y = node->y + padding;
            
            child = node->first_child;
            while (child) {
                if (!(child->state & RX_STATE_HIDDEN)) {
                    child->x = node->x + padding;
                    child->y = y;
                    child->width = inner_w;
                    child->height = child_h;
                    rx_layout_node(child, child->width, child->height);
                    y += child_h + gap;
                }
                child = child->next_sibling;
            }
            break;
        }
        case RX_NODE_HSTACK: {
            float child_w = (inner_w - total_gap) / child_count;
            float x = node->x + padding;
            
            child = node->first_child;
            while (child) {
                if (!(child->state & RX_STATE_HIDDEN)) {
                    child->x = x;
                    child->y = node->y + padding;
                    child->width = child_w;
                    child->height = inner_h;
                    rx_layout_node(child, child->width, child->height);
                    x += child_w + gap;
                }
                child = child->next_sibling;
            }
            break;
        }
        case RX_NODE_ZSTACK: {
            /* All children take full space */
            child = node->first_child;
            while (child) {
                if (!(child->state & RX_STATE_HIDDEN)) {
                    child->x = node->x + padding;
                    child->y = node->y + padding;
                    child->width = inner_w;
                    child->height = inner_h;
                    rx_layout_node(child, child->width, child->height);
                }
                child = child->next_sibling;
            }
            break;
        }
        default:
            /* Leaf nodes - layout children generically */
            child = node->first_child;
            while (child) {
                rx_layout_node(child, child->width, child->height);
                child = child->next_sibling;
            }
            break;
    }
}

/* ============================================================================
 * Rendering
 * ============================================================================ */

static inline void rx_render_node(RxNode* node) {
    if (!node || !rx_bridge || (node->state & RX_STATE_HIDDEN)) return;
    
    NxRect rect = { node->x, node->y, node->width, node->height };
    
    /* Draw background */
    if (node->background.a > 0) {
        if (node->corner_radius > 0) {
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, rect, node->background, node->corner_radius);
        } else {
            nx_gpu_fill_rect(rx_bridge->gpu, rect, node->background);
        }
    }
    
    /* Type-specific rendering */
    switch (node->type) {
        case RX_NODE_TEXT:
        case RX_NODE_BUTTON:
            if (node->text) {
                NxColor text_color = node->foreground;
                
                /* Button hover/press states */
                if (node->type == RX_NODE_BUTTON) {
                    NxColor btn_bg = nx_theme_get_primary_color(rx_bridge->theme);
                    if (node->state & RX_STATE_PRESSED) {
                        btn_bg.r = btn_bg.r * 0.8;
                        btn_bg.g = btn_bg.g * 0.8;
                        btn_bg.b = btn_bg.b * 0.8;
                    } else if (node->state & RX_STATE_HOVERED) {
                        btn_bg.r = btn_bg.r < 230 ? btn_bg.r + 25 : 255;
                        btn_bg.g = btn_bg.g < 230 ? btn_bg.g + 25 : 255;
                        btn_bg.b = btn_bg.b < 230 ? btn_bg.b + 25 : 255;
                    }
                    nx_gpu_fill_rounded_rect(rx_bridge->gpu, rect, btn_bg, 6.0f);
                    text_color = (NxColor){255, 255, 255, 255};
                }
                
                /* Center text (approximate) */
                float tx = node->x + node->width / 2 - 4 * strlen(node->text);
                float ty = node->y + node->height / 2 + 5;
                nx_gpu_draw_text(rx_bridge->gpu, node->text, tx, ty, text_color);
            }
            break;
            
        case RX_NODE_CHECKBOX: {
            NxRect box = { node->x + 4, node->y + (node->height - 20) / 2, 20, 20 };
            NxColor box_color = node->value > 0.5 ? 
                nx_theme_get_primary_color(rx_bridge->theme) : 
                (NxColor){60, 60, 62, 255};
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, box, box_color, 4);
            
            if (node->text) {
                nx_gpu_draw_text(rx_bridge->gpu, node->text, 
                    node->x + 32, node->y + node->height / 2 + 5, node->foreground);
            }
            break;
        }
        
        case RX_NODE_SLIDER: {
            /* Track */
            NxRect track = { node->x + 8, node->y + node->height / 2 - 2, node->width - 16, 4 };
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, track, (NxColor){60, 60, 62, 255}, 2);
            
            /* Fill */
            float fill_w = (node->width - 16) * node->value;
            NxRect fill = { node->x + 8, node->y + node->height / 2 - 2, fill_w, 4 };
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, fill, nx_theme_get_primary_color(rx_bridge->theme), 2);
            
            /* Thumb */
            float thumb_x = node->x + 8 + fill_w;
            nx_gpu_fill_circle(rx_bridge->gpu, thumb_x, node->y + node->height / 2, 8, (NxColor){255, 255, 255, 255});
            break;
        }
        
        case RX_NODE_DIVIDER: {
            NxRect line = { node->x, node->y + node->height / 2, node->width, 1 };
            nx_gpu_fill_rect(rx_bridge->gpu, line, (NxColor){60, 60, 62, 255});
            break;
        }
        
        default:
            break;
    }
    
    /* Render children */
    RxNode* child = node->first_child;
    while (child) {
        rx_render_node(child);
        child = child->next_sibling;
    }
}

/* ============================================================================
 * Input Handling
 * ============================================================================ */

static inline RxNode* rx_hit_test(RxNode* node, float x, float y) {
    if (!node || (node->state & RX_STATE_HIDDEN)) return NULL;
    
    /* Check if point is inside node */
    bool inside = x >= node->x && x < node->x + node->width &&
                  y >= node->y && y < node->y + node->height;
    
    if (!inside) return NULL;
    
    /* Check children (reverse order for z-order) */
    RxNode* hit = NULL;
    RxNode* child = node->first_child;
    while (child) {
        RxNode* child_hit = rx_hit_test(child, x, y);
        if (child_hit) hit = child_hit;
        child = child->next_sibling;
    }
    
    /* Return deepest hit, or this node if interactive */
    if (hit) return hit;
    
    /* Only return this node if it's interactive */
    if (node->type == RX_NODE_BUTTON || 
        node->type == RX_NODE_CHECKBOX ||
        node->type == RX_NODE_SLIDER ||
        node->type == RX_NODE_TEXTFIELD) {
        return node;
    }
    
    return NULL;
}

static inline void rx_handle_mouse_move(float x, float y) {
    if (!rx_bridge || !rx_bridge->root) return;
    
    nx_mouse_move(rx_bridge->mouse, x, y);
    
    RxNode* hit = rx_hit_test(rx_bridge->root, x, y);
    
    /* Update hover state */
    if (rx_bridge->hovered != hit) {
        if (rx_bridge->hovered) {
            rx_bridge->hovered->state &= ~RX_STATE_HOVERED;
            rx_bridge->hovered->state |= RX_STATE_DIRTY;
        }
        if (hit) {
            hit->state |= RX_STATE_HOVERED | RX_STATE_DIRTY;
        }
        rx_bridge->hovered = hit;
        rx_bridge->needs_redraw = true;
    }
}

static inline void rx_handle_mouse_down(float x, float y) {
    if (!rx_bridge || !rx_bridge->root) return;
    
    nx_mouse_button_down(rx_bridge->mouse, x, y, NX_MOUSE_LEFT);
    
    RxNode* hit = rx_hit_test(rx_bridge->root, x, y);
    if (hit) {
        hit->state |= RX_STATE_PRESSED | RX_STATE_DIRTY;
        rx_bridge->focused = hit;
        rx_bridge->needs_redraw = true;
    }
}

static inline void rx_handle_mouse_up(float x, float y) {
    if (!rx_bridge || !rx_bridge->root) return;
    
    nx_mouse_button_up(rx_bridge->mouse, x, y, NX_MOUSE_LEFT);
    
    RxNode* hit = rx_hit_test(rx_bridge->root, x, y);
    
    /* Trigger click if released on same node that was pressed */
    if (rx_bridge->focused && (rx_bridge->focused->state & RX_STATE_PRESSED)) {
        rx_bridge->focused->state &= ~RX_STATE_PRESSED;
        rx_bridge->focused->state |= RX_STATE_DIRTY;
        
        if (hit == rx_bridge->focused) {
            /* Click! */
            if (rx_bridge->focused->type == RX_NODE_CHECKBOX) {
                rx_bridge->focused->value = rx_bridge->focused->value > 0.5 ? 0.0 : 1.0;
                if (rx_bridge->focused->on_change) {
                    rx_bridge->focused->on_change(rx_bridge->focused, rx_bridge->focused->value);
                }
            }
            if (rx_bridge->focused->on_click) {
                rx_bridge->focused->on_click(rx_bridge->focused);
            }
        }
        
        rx_bridge->needs_redraw = true;
    }
}

/* ============================================================================
 * Main Frame Loop
 * ============================================================================ */

static inline void rx_frame(void) {
    if (!rx_bridge) return;
    
    /* Layout if needed */
    if (rx_bridge->root && (rx_bridge->root->state & RX_STATE_DIRTY)) {
        rx_layout_node(rx_bridge->root, rx_bridge->root->width, rx_bridge->root->height);
    }
    
    /* Render if needed */
    if (rx_bridge->needs_redraw) {
        NxColor bg = nx_theme_get_background_color(rx_bridge->theme);
        nx_gpu_clear(rx_bridge->gpu, bg);
        
        if (rx_bridge->root) {
            rx_render_node(rx_bridge->root);
        }
        
        nx_gpu_present(rx_bridge->gpu);
        rx_bridge->needs_redraw = false;
    }
}

/* ============================================================================
 * State Invalidation
 * Mark nodes dirty when state changes
 * ============================================================================ */

static inline void rx_invalidate(RxNode* node) {
    if (!node) return;
    node->state |= RX_STATE_DIRTY;
    if (rx_bridge) rx_bridge->needs_redraw = true;
}

static inline void rx_invalidate_tree(RxNode* node) {
    if (!node) return;
    rx_invalidate(node);
    RxNode* child = node->first_child;
    while (child) {
        rx_invalidate_tree(child);
        child = child->next_sibling;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* REOX_NXRENDER_BRIDGE_H */
