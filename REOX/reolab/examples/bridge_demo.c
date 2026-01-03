/*
 * REOX + NXRender Demo
 * First system app: Settings panel
 * 
 * Tests: Window, VStack, Button, Checkbox, Slider
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * NXRender FFI Stubs (for testing without Rust library)
 * In production, link against libnxrender_ffi.a
 * ============================================================================ */

#define NXRENDER_STUB_IMPL

/* Types */
typedef void* NxGpuContext;
typedef void* NxTheme;
typedef void* NxMouseState;
typedef void* NxKeyboardState;

typedef struct { uint8_t r, g, b, a; } NxColor;
typedef struct { float x, y, width, height; } NxRect;
typedef struct { float x, y; } NxPoint;

typedef enum { NX_MOUSE_LEFT = 0, NX_MOUSE_RIGHT = 1, NX_MOUSE_MIDDLE = 2 } NxMouseButton;

/* GPU */
NxGpuContext nx_gpu_create(void) { printf("[GPU] Created\n"); return (void*)0x1; }
NxGpuContext nx_gpu_create_with_size(uint32_t w, uint32_t h) { 
    printf("[GPU] Created %ux%u\n", w, h); 
    return (void*)0x1; 
}
void nx_gpu_destroy(NxGpuContext ctx) { (void)ctx; }
void nx_gpu_present(NxGpuContext ctx) { (void)ctx; printf("[GPU] Present\n"); }
void nx_gpu_resize(NxGpuContext ctx, uint32_t w, uint32_t h) { (void)ctx; (void)w; (void)h; }

void nx_gpu_fill_rect(NxGpuContext ctx, NxRect r, NxColor c) {
    (void)ctx;
    if (c.a > 0) printf("[GPU] Rect(%.0f,%.0f %.0fx%.0f) #%02x%02x%02x\n", r.x, r.y, r.width, r.height, c.r, c.g, c.b);
}
void nx_gpu_fill_rounded_rect(NxGpuContext ctx, NxRect r, NxColor c, float rad) {
    (void)ctx;
    if (c.a > 0) printf("[GPU] RoundRect(%.0f,%.0f %.0fx%.0f r=%.0f) #%02x%02x%02x\n", r.x, r.y, r.width, r.height, rad, c.r, c.g, c.b);
}
void nx_gpu_fill_circle(NxGpuContext ctx, float x, float y, float r, NxColor c) {
    (void)ctx;
    printf("[GPU] Circle(%.0f,%.0f r=%.0f) #%02x%02x%02x\n", x, y, r, c.r, c.g, c.b);
}
void nx_gpu_draw_text(NxGpuContext ctx, const char* text, float x, float y, NxColor c) {
    (void)ctx; (void)c;
    printf("[GPU] Text(%.0f,%.0f): '%s'\n", x, y, text);
}
void nx_gpu_clear(NxGpuContext ctx, NxColor c) {
    (void)ctx;
    printf("[GPU] Clear #%02x%02x%02x\n", c.r, c.g, c.b);
}

/* Theme */
NxTheme nx_theme_light(void) { return (void*)0x2; }
NxTheme nx_theme_dark(void) { return (void*)0x2; }
void nx_theme_destroy(NxTheme t) { (void)t; }
NxColor nx_theme_get_primary_color(NxTheme t) { (void)t; return (NxColor){0, 122, 255, 255}; }
NxColor nx_theme_get_background_color(NxTheme t) { (void)t; return (NxColor){28, 28, 30, 255}; }
NxColor nx_theme_get_surface_color(NxTheme t) { (void)t; return (NxColor){44, 44, 46, 255}; }
NxColor nx_theme_get_text_color(NxTheme t) { (void)t; return (NxColor){255, 255, 255, 255}; }

/* Mouse */
NxMouseState nx_mouse_create(void) { return (void*)0x3; }
void nx_mouse_destroy(NxMouseState m) { (void)m; }
void nx_mouse_get_position(NxMouseState m, float* x, float* y) { (void)m; *x = 0; *y = 0; }
bool nx_mouse_is_button_down(NxMouseState m, NxMouseButton b) { (void)m; (void)b; return false; }
void nx_mouse_move(NxMouseState m, float x, float y) { (void)m; (void)x; (void)y; }
void nx_mouse_button_down(NxMouseState m, float x, float y, NxMouseButton b) { (void)m; (void)x; (void)y; (void)b; }
void nx_mouse_button_up(NxMouseState m, float x, float y, NxMouseButton b) { (void)m; (void)x; (void)y; (void)b; }

/* Keyboard */
NxKeyboardState nx_keyboard_create(void) { return (void*)0x4; }
void nx_keyboard_destroy(NxKeyboardState k) { (void)k; }
bool nx_keyboard_is_ctrl(NxKeyboardState k) { (void)k; return false; }
bool nx_keyboard_is_shift(NxKeyboardState k) { (void)k; return false; }
bool nx_keyboard_is_alt(NxKeyboardState k) { (void)k; return false; }

const char* nx_version(void) { return "NXRENDER 1.0.0 (stub)"; }

/* ============================================================================
 * REOX Bridge - Inline implementation since we defined stubs above
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
    RX_STATE_DIRTY = 1 << 5,
} RxNodeState;

/* UI Node */
typedef struct RxNode {
    uint64_t id;
    RxNodeType type;
    uint32_t state;
    float x, y, width, height;
    NxColor background;
    NxColor foreground;
    float corner_radius;
    float padding;
    float gap;
    char* text;
    float value;
    struct RxNode* parent;
    struct RxNode* first_child;
    struct RxNode* next_sibling;
    void (*on_click)(struct RxNode* node);
    void (*on_change)(struct RxNode* node, float value);
} RxNode;

/* Bridge state */
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

static RxBridge* rx_bridge = NULL;

static inline void rx_bridge_init(uint32_t width, uint32_t height) {
    rx_bridge = (RxBridge*)calloc(1, sizeof(RxBridge));
    rx_bridge->gpu = nx_gpu_create_with_size(width, height);
    rx_bridge->theme = nx_theme_dark();
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
    RxNode* child = node->first_child;
    while (child) {
        RxNode* next = child->next_sibling;
        rx_node_destroy(child);
        child = next;
    }
    if (node->text) free(node->text);
    free(node);
}

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

/* Layout */
static inline void rx_layout_node(RxNode* node, float available_w, float available_h) {
    if (!node) return;
    
    float padding = node->padding;
    float inner_w = available_w - padding * 2;
    float inner_h = available_h - padding * 2;
    
    int child_count = 0;
    RxNode* child = node->first_child;
    while (child) {
        if (!(child->state & RX_STATE_HIDDEN)) child_count++;
        child = child->next_sibling;
    }
    
    if (child_count == 0) return;
    
    float gap = node->gap;
    float total_gap = gap * (child_count - 1);
    
    if (node->type == RX_NODE_VSTACK) {
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
    }
}

/* Rendering */
static inline void rx_render_node(RxNode* node) {
    if (!node || !rx_bridge || (node->state & RX_STATE_HIDDEN)) return;
    
    NxRect rect = { node->x, node->y, node->width, node->height };
    
    if (node->background.a > 0) {
        if (node->corner_radius > 0) {
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, rect, node->background, node->corner_radius);
        } else {
            nx_gpu_fill_rect(rx_bridge->gpu, rect, node->background);
        }
    }
    
    switch (node->type) {
        case RX_NODE_TEXT:
            if (node->text) {
                float tx = node->x + 4;
                float ty = node->y + node->height / 2 + 5;
                nx_gpu_draw_text(rx_bridge->gpu, node->text, tx, ty, node->foreground);
            }
            break;
            
        case RX_NODE_BUTTON:
            if (node->text) {
                NxColor btn_bg = nx_theme_get_primary_color(rx_bridge->theme);
                nx_gpu_fill_rounded_rect(rx_bridge->gpu, rect, btn_bg, 6.0f);
                float tx = node->x + node->width / 2 - 4 * strlen(node->text);
                float ty = node->y + node->height / 2 + 5;
                nx_gpu_draw_text(rx_bridge->gpu, node->text, tx, ty, (NxColor){255,255,255,255});
            }
            break;
            
        case RX_NODE_CHECKBOX: {
            NxRect box = { node->x + 4, node->y + (node->height - 20) / 2, 20, 20 };
            NxColor box_color = node->value > 0.5 ? 
                nx_theme_get_primary_color(rx_bridge->theme) : (NxColor){60, 60, 62, 255};
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, box, box_color, 4);
            if (node->text) {
                nx_gpu_draw_text(rx_bridge->gpu, node->text, node->x + 32, node->y + node->height / 2 + 5, node->foreground);
            }
            break;
        }
        
        case RX_NODE_SLIDER: {
            NxRect track = { node->x + 8, node->y + node->height / 2 - 2, node->width - 16, 4 };
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, track, (NxColor){60, 60, 62, 255}, 2);
            float fill_w = (node->width - 16) * node->value;
            NxRect fill = { node->x + 8, node->y + node->height / 2 - 2, fill_w, 4 };
            nx_gpu_fill_rounded_rect(rx_bridge->gpu, fill, nx_theme_get_primary_color(rx_bridge->theme), 2);
            nx_gpu_fill_circle(rx_bridge->gpu, node->x + 8 + fill_w, node->y + node->height / 2, 8, (NxColor){255, 255, 255, 255});
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
    
    RxNode* child = node->first_child;
    while (child) {
        rx_render_node(child);
        child = child->next_sibling;
    }
}

/* Hit testing */
static inline RxNode* rx_hit_test(RxNode* node, float x, float y) {
    if (!node || (node->state & RX_STATE_HIDDEN)) return NULL;
    
    bool inside = x >= node->x && x < node->x + node->width &&
                  y >= node->y && y < node->y + node->height;
    
    if (!inside) return NULL;
    
    RxNode* hit = NULL;
    RxNode* child = node->first_child;
    while (child) {
        RxNode* child_hit = rx_hit_test(child, x, y);
        if (child_hit) hit = child_hit;
        child = child->next_sibling;
    }
    
    if (hit) return hit;
    
    if (node->type == RX_NODE_BUTTON || node->type == RX_NODE_CHECKBOX ||
        node->type == RX_NODE_SLIDER || node->type == RX_NODE_TEXTFIELD) {
        return node;
    }
    
    return NULL;
}

static inline void rx_handle_mouse_up(float x, float y) {
    if (!rx_bridge || !rx_bridge->root) return;
    
    RxNode* hit = rx_hit_test(rx_bridge->root, x, y);
    
    if (rx_bridge->focused && (rx_bridge->focused->state & RX_STATE_PRESSED)) {
        rx_bridge->focused->state &= ~RX_STATE_PRESSED;
        
        if (hit == rx_bridge->focused) {
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

static inline void rx_handle_mouse_down(float x, float y) {
    if (!rx_bridge || !rx_bridge->root) return;
    
    RxNode* hit = rx_hit_test(rx_bridge->root, x, y);
    if (hit) {
        hit->state |= RX_STATE_PRESSED;
        rx_bridge->focused = hit;
        rx_bridge->needs_redraw = true;
    }
}

static inline void rx_frame(void) {
    if (!rx_bridge) return;
    
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
 * Settings Panel Demo
 * ============================================================================ */

static int dark_mode = 1;
static float volume = 0.75;
static float brightness = 0.50;

void on_dark_mode_toggle(RxNode* node, float value) {
    dark_mode = value > 0.5 ? 1 : 0;
    printf("\n[APP] Dark mode: %s\n\n", dark_mode ? "ON" : "OFF");
    (void)node;
}

void on_apply_click(RxNode* node) {
    (void)node;
    printf("\n[APP] Settings applied!\n");
    printf("  Dark mode: %s\n", dark_mode ? "ON" : "OFF");
    printf("  Volume: %.0f%%\n", volume * 100);
    printf("  Brightness: %.0f%%\n\n", brightness * 100);
}

int main(void) {
    printf("=== REOX + NXRender Demo ===\n");
    printf("First system app: Settings Panel\n\n");
    
    rx_bridge_init(400, 300);
    
    RxNode* root = rx_node_create(RX_NODE_VSTACK);
    root->x = 0;
    root->y = 0;
    root->width = 400;
    root->height = 300;
    root->padding = 20;
    root->gap = 12;
    root->background = nx_theme_get_background_color(rx_bridge->theme);
    rx_bridge->root = root;
    
    RxNode* title = rx_node_create(RX_NODE_TEXT);
    title->text = strdup("Settings");
    title->height = 30;
    rx_node_add_child(root, title);
    
    RxNode* div1 = rx_node_create(RX_NODE_DIVIDER);
    div1->height = 1;
    rx_node_add_child(root, div1);
    
    RxNode* dark_check = rx_node_create(RX_NODE_CHECKBOX);
    dark_check->text = strdup("Dark Mode");
    dark_check->value = 1.0;
    dark_check->height = 40;
    dark_check->on_change = on_dark_mode_toggle;
    rx_node_add_child(root, dark_check);
    
    RxNode* vol_label = rx_node_create(RX_NODE_TEXT);
    vol_label->text = strdup("Volume");
    vol_label->height = 20;
    rx_node_add_child(root, vol_label);
    
    RxNode* vol_slider = rx_node_create(RX_NODE_SLIDER);
    vol_slider->value = 0.75;
    vol_slider->height = 30;
    rx_node_add_child(root, vol_slider);
    
    RxNode* bright_label = rx_node_create(RX_NODE_TEXT);
    bright_label->text = strdup("Brightness");
    bright_label->height = 20;
    rx_node_add_child(root, bright_label);
    
    RxNode* bright_slider = rx_node_create(RX_NODE_SLIDER);
    bright_slider->value = 0.50;
    bright_slider->height = 30;
    rx_node_add_child(root, bright_slider);
    
    RxNode* apply_btn = rx_node_create(RX_NODE_BUTTON);
    apply_btn->text = strdup("Apply Settings");
    apply_btn->height = 44;
    apply_btn->on_click = on_apply_click;
    rx_node_add_child(root, apply_btn);
    
    printf("--- Layout ---\n");
    rx_layout_node(root, 400, 300);
    
    printf("\n--- Render Frame 1 ---\n");
    rx_frame();
    
    printf("\n--- Simulating click on Apply button ---\n");
    float btn_x = apply_btn->x + apply_btn->width / 2;
    float btn_y = apply_btn->y + apply_btn->height / 2;
    rx_handle_mouse_down(btn_x, btn_y);
    rx_handle_mouse_up(btn_x, btn_y);
    
    printf("\n--- Render Frame 2 ---\n");
    rx_bridge->needs_redraw = true;
    rx_frame();
    
    rx_node_destroy(root);
    rx_bridge_destroy();
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}
