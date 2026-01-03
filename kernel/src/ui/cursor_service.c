/*
 * NeolyxOS Universal Cursor Service - Implementation
 * 
 * Handles cursor movement with configurable speed/acceleration,
 * theme loading, and animation.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/ui/cursor_service.h"

/* Kernel dependencies */
extern void serial_puts(const char *s);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ Global State ============ */

static cursor_state_t  g_cursor_state;
static cursor_config_t g_cursor_config;
static cursor_theme_t  g_cursor_theme;
static bool g_initialized = false;

/* ============ Built-in Cursor Bitmaps ============ */

/*
 * NeolyxOS Default Cursor (from cursor.svg)
 * Y-shaped spread design, 14x16 pixels
 * 
 * Pixel representation (1 = filled, 0 = transparent):
 * 
 * Row 0:   X . . . . . . . . . . . . .    <- top point
 * Row 1:   X X . . . . . . . . . . X .    <- spreads out
 * Row 2:   X X . . . . . . . . X X . .
 * Row 3:   X X X . . . . . X X X . . .
 * Row 4:   . X X . . . . X X . . . . .
 * Row 5:   . X X . . . X X . . . . . .
 * Row 6:   . . X X . X X . . . . . . .
 * Row 7:   . . X X X X . . . . . . . .    <- middle merge
 * Row 8:   . . . X X . . . . . . . . .
 * Row 9:   . . . X X . . . . . . . . .
 * Row 10:  . . X X X X . . . . . . . .
 * Row 11:  . . X X . X X . . . . . . .    <- tail split
 * Row 12:  . X X . . . X X . . . . . .
 * Row 13:  . X X . . . . . . . . . . .
 * Row 14:  X X X . . . . . . . . . . .
 * Row 15:  X X . . . . . . . . . . . .
 */

/* Left side of cursor (darker purple #B0AAE3) */
static const uint32_t cursor_arrow_left[16] = {
    0x80000000,  /* 1000 0000 0000 0000 */
    0xC0000000,  /* 1100 0000 0000 0000 */
    0xC0000000,  /* 1100 0000 0000 0000 */
    0xE0000000,  /* 1110 0000 0000 0000 */
    0x60000000,  /* 0110 0000 0000 0000 */
    0x60000000,  /* 0110 0000 0000 0000 */
    0x30000000,  /* 0011 0000 0000 0000 */
    0x30000000,  /* 0011 0000 0000 0000 */
    0x18000000,  /* 0001 1000 0000 0000 */
    0x18000000,  /* 0001 1000 0000 0000 */
    0x30000000,  /* 0011 0000 0000 0000 */
    0x30000000,  /* 0011 0000 0000 0000 */
    0x60000000,  /* 0110 0000 0000 0000 */
    0x60000000,  /* 0110 0000 0000 0000 */
    0xE0000000,  /* 1110 0000 0000 0000 */
    0xC0000000   /* 1100 0000 0000 0000 */
};

/* Right side of cursor (lighter purple #CDBBE6) */
static const uint32_t cursor_arrow_right[16] = {
    0x00000000,  /* 0000 0000 0000 0000 */
    0x00040000,  /* 0000 0000 0000 0100 */
    0x00180000,  /* 0000 0000 0001 1000 */
    0x00700000,  /* 0000 0000 0111 0000 */
    0x01C00000,  /* 0000 0001 1100 0000 */
    0x07000000,  /* 0000 0111 0000 0000 */
    0x0C000000,  /* 0000 1100 0000 0000 */
    0x10000000,  /* 0001 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000 */
    0x0C000000,  /* 0000 1100 0000 0000 */
    0x06000000,  /* 0000 0110 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000 */
    0x00000000   /* 0000 0000 0000 0000 */
};

/* Outline version (black #373435) for contrast */
static const uint32_t cursor_arrow_outline[16] = {
    0xC0000000,  /* 1100 0000 0000 0000 */
    0xE0040000,  /* 1110 0000 0000 0100 */
    0xF0180000,  /* 1111 0000 0001 1000 */
    0xF8700000,  /* 1111 1000 0111 0000 */
    0x7DC00000,  /* 0111 1101 1100 0000 */
    0x7F000000,  /* 0111 1111 0000 0000 */
    0x3FC00000,  /* 0011 1111 1100 0000 */
    0x3C000000,  /* 0011 1100 0000 0000 */
    0x1C000000,  /* 0001 1100 0000 0000 */
    0x1C000000,  /* 0001 1100 0000 0000 */
    0x3C000000,  /* 0011 1100 0000 0000 */
    0x3FC00000,  /* 0011 1111 1100 0000 */
    0x7F000000,  /* 0111 1111 0000 0000 */
    0x7C000000,  /* 0111 1100 0000 0000 */
    0xFC000000,  /* 1111 1100 0000 0000 */
    0xE0000000   /* 1110 0000 0000 0000 */
};

/* ============ Speed/Acceleration Tables ============ */

/* Speed multipliers: 1=0.25x, 5=1.0x, 10=4.0x */
static const float speed_table[11] = {
    0.0f,   /* unused */
    0.25f,  /* 1 - very slow */
    0.45f,  /* 2 */
    0.65f,  /* 3 */
    0.85f,  /* 4 */
    1.00f,  /* 5 - normal (default) */
    1.40f,  /* 6 */
    1.80f,  /* 7 */
    2.50f,  /* 8 */
    3.20f,  /* 9 */
    4.00f   /* 10 - very fast */
};

/* Acceleration curves */
static float apply_acceleration(float delta, uint8_t level) {
    float abs_delta = delta < 0 ? -delta : delta;
    float sign = delta < 0 ? -1.0f : 1.0f;
    
    switch (level) {
        case 0:  /* Off - linear */
            return delta;
        case 1:  /* Low */
            if (abs_delta > 2.0f) {
                return sign * (2.0f + (abs_delta - 2.0f) * 1.3f);
            }
            return delta;
        case 2:  /* Medium */
            if (abs_delta > 3.0f) {
                return sign * (3.0f + (abs_delta - 3.0f) * 1.8f);
            } else if (abs_delta > 1.5f) {
                return sign * (1.5f + (abs_delta - 1.5f) * 1.4f);
            }
            return delta;
        case 3:  /* High */
            if (abs_delta > 5.0f) {
                return sign * (5.0f + (abs_delta - 5.0f) * 3.0f);
            } else if (abs_delta > 2.0f) {
                return sign * (2.0f + (abs_delta - 2.0f) * 2.0f);
            }
            return delta * 1.2f;
        default:
            return delta;
    }
}

/* ============ Initialization ============ */

void cursor_service_init(uint16_t width, uint16_t height) {
    /* Initialize state */
    g_cursor_state.x = width / 2.0f;
    g_cursor_state.y = height / 2.0f;
    g_cursor_state.screen_x = width / 2;
    g_cursor_state.screen_y = height / 2;
    g_cursor_state.dx_accum = 0.0f;
    g_cursor_state.dy_accum = 0.0f;
    g_cursor_state.current_type = NX_CURSOR_ARROW;
    g_cursor_state.requested_type = NX_CURSOR_ARROW;
    g_cursor_state.buttons = 0;
    g_cursor_state.last_click_time = 0;
    g_cursor_state.visible = true;
    g_cursor_state.locked = false;
    g_cursor_state.screen_width = width;
    g_cursor_state.screen_height = height;
    
    /* Initialize config with defaults */
    g_cursor_config.speed = 5;
    g_cursor_config.acceleration = 2;  /* Medium */
    g_cursor_config.multiplier = speed_table[5];
    g_cursor_config.size = 1;  /* Normal */
    g_cursor_config.scale = 1.0f;
    g_cursor_config.double_click_ms = 400;
    g_cursor_config.natural_scrolling = false;
    g_cursor_config.smooth_tracking = true;
    
    /* Load built-in theme */
    cursor_set_builtin_theme(false);  /* Light variant for dark backgrounds */
    
    g_initialized = true;
    serial_puts("[CURSOR] Service initialized\n");
}

/* ============ Movement Update ============ */

/*
 * Movement processing order (industry standard):
 * 1. Clamp raw input (prevent hardware spikes)
 * 2. Apply acceleration (on raw deltas)
 * 3. Apply speed multiplier (user preference)
 * 
 * Timestamp units: milliseconds (monotonic)
 */
void cursor_service_update(int16_t dx, int16_t dy, uint8_t buttons, uint32_t timestamp) {
    if (!g_initialized || g_cursor_state.locked) return;
    
    /* 1. Clamp raw input to prevent hardware spikes */
    if (dx > 50) dx = 50;
    if (dx < -50) dx = -50;
    if (dy > 50) dy = 50;
    if (dy < -50) dy = -50;
    
    /* 2. Apply acceleration FIRST (on raw deltas, not speed-adjusted) */
    float fdx = apply_acceleration((float)dx, g_cursor_config.acceleration);
    float fdy = apply_acceleration((float)dy, g_cursor_config.acceleration);
    
    /* 3. Apply speed multiplier AFTER acceleration */
    fdx *= g_cursor_config.multiplier;
    fdy *= g_cursor_config.multiplier;
    
    /* Update sub-pixel position */
    g_cursor_state.x += fdx;
    g_cursor_state.y += fdy;
    
    /* Clamp to screen bounds */
    if (g_cursor_state.x < 0) g_cursor_state.x = 0;
    if (g_cursor_state.y < 0) g_cursor_state.y = 0;
    if (g_cursor_state.x >= g_cursor_state.screen_width) 
        g_cursor_state.x = g_cursor_state.screen_width - 1;
    if (g_cursor_state.y >= g_cursor_state.screen_height) 
        g_cursor_state.y = g_cursor_state.screen_height - 1;
    
    /* Update integer position */
    g_cursor_state.screen_x = (int32_t)g_cursor_state.x;
    g_cursor_state.screen_y = (int32_t)g_cursor_state.y;
    
    /* Handle button state */
    uint8_t prev_buttons = g_cursor_state.buttons;
    g_cursor_state.buttons = buttons;
    
    /* Detect double-click */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Left button pressed */
        if (timestamp - g_cursor_state.last_click_time < g_cursor_config.double_click_ms) {
            /* Double click detected */
            int dx_click = g_cursor_state.screen_x - g_cursor_state.last_click_x;
            int dy_click = g_cursor_state.screen_y - g_cursor_state.last_click_y;
            if (dx_click < 0) dx_click = -dx_click;
            if (dy_click < 0) dy_click = -dy_click;
            
            if (dx_click < 5 && dy_click < 5) {
                /* Valid double-click (within 5px) */
                /* TODO: Emit double-click event */
            }
        }
        g_cursor_state.last_click_time = timestamp;
        g_cursor_state.last_click_x = g_cursor_state.screen_x;
        g_cursor_state.last_click_y = g_cursor_state.screen_y;
    }
}

/* ============ Animation ============ */

void cursor_service_animate(uint32_t timestamp) {
    if (!g_initialized) return;
    
    nx_cursor_type_t type = g_cursor_state.current_type;
    cursor_animation_t *anim = &g_cursor_theme.animations[type];
    
    if (!anim->is_animated || anim->frame_count <= 1) return;
    
    /* Check if it's time for next frame */
    if (timestamp - anim->last_tick >= anim->frame_delay_ms) {
        anim->current_frame++;
        if (anim->current_frame >= anim->frame_count) {
            anim->current_frame = 0;
        }
        anim->last_tick = timestamp;
    }
}

/* ============ Rendering ============ */

/* Draw cursor at current position */
void cursor_draw(volatile uint32_t *fb, uint32_t pitch) {
    if (!g_initialized || !g_cursor_state.visible || !fb) return;
    
    nx_cursor_type_t type = g_cursor_state.current_type;
    cursor_image_t *img = &g_cursor_theme.images[type];
    
    int cx = g_cursor_state.screen_x - img->hotspot_x;
    int cy = g_cursor_state.screen_y - img->hotspot_y;
    int width = img->width;
    int height = img->height;
    
    /* Render using bitmap data (1-bit with colors) */
    if (img->bitmap) {
        for (int y = 0; y < height; y++) {
            int py = cy + y;
            if (py < 0 || py >= g_cursor_state.screen_height) continue;
            
            uint32_t row_left = cursor_arrow_left[y];
            uint32_t row_right = cursor_arrow_right[y];
            
            for (int x = 0; x < width; x++) {
                int px = cx + x;
                if (px < 0 || px >= g_cursor_state.screen_width) continue;
                
                uint32_t mask = 0x80000000 >> x;
                uint32_t idx = py * (pitch / 4) + px;
                
                /* Check left side (darker purple) */
                if (row_left & mask) {
                    fb[idx] = CURSOR_COLOR_LEFT;
                }
                /* Check right side (lighter purple) */
                else if (row_right & mask) {
                    fb[idx] = CURSOR_COLOR_RIGHT;
                }
            }
        }
    }
    /* Render using pixel data (ARGB) */
    else if (img->pixels) {
        for (int y = 0; y < height; y++) {
            int py = cy + y;
            if (py < 0 || py >= g_cursor_state.screen_height) continue;
            
            for (int x = 0; x < width; x++) {
                int px = cx + x;
                if (px < 0 || px >= g_cursor_state.screen_width) continue;
                
                uint32_t pixel = img->pixels[y * width + x];
                uint8_t alpha = (pixel >> 24) & 0xFF;
                
                if (alpha == 0) continue;  /* Fully transparent */
                
                uint32_t idx = py * (pitch / 4) + px;
                
                if (alpha == 0xFF) {
                    /* Fully opaque */
                    fb[idx] = pixel;
                } else {
                    /* Alpha blend */
                    uint32_t bg = fb[idx];
                    uint8_t bg_r = (bg >> 16) & 0xFF;
                    uint8_t bg_g = (bg >> 8) & 0xFF;
                    uint8_t bg_b = bg & 0xFF;
                    uint8_t fg_r = (pixel >> 16) & 0xFF;
                    uint8_t fg_g = (pixel >> 8) & 0xFF;
                    uint8_t fg_b = pixel & 0xFF;
                    
                    uint8_t out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                    uint8_t out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                    uint8_t out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                    
                    fb[idx] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
                }
            }
        }
    }
}

/* ============ Visibility ============ */

void cursor_hide(void) {
    g_cursor_state.visible = false;
}

void cursor_show(void) {
    g_cursor_state.visible = true;
}

/* ============ Configuration ============ */

void cursor_set_config(const cursor_config_t *config) {
    if (!config) return;
    
    g_cursor_config = *config;
    
    /* Validate and compute multiplier */
    if (g_cursor_config.speed < 1) g_cursor_config.speed = 1;
    if (g_cursor_config.speed > 10) g_cursor_config.speed = 10;
    g_cursor_config.multiplier = speed_table[g_cursor_config.speed];
    
    /* Validate acceleration */
    if (g_cursor_config.acceleration > 3) g_cursor_config.acceleration = 3;
    
    /* Compute scale from size */
    switch (g_cursor_config.size) {
        case 0: g_cursor_config.scale = 0.75f; break;
        case 1: g_cursor_config.scale = 1.00f; break;
        case 2: g_cursor_config.scale = 1.50f; break;
        default: g_cursor_config.scale = 1.00f; break;
    }
}

const cursor_config_t* cursor_get_config(void) {
    return &g_cursor_config;
}

void cursor_set_speed(uint8_t speed) {
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;
    g_cursor_config.speed = speed;
    g_cursor_config.multiplier = speed_table[speed];
}

void cursor_set_acceleration(uint8_t level) {
    if (level > 3) level = 3;
    g_cursor_config.acceleration = level;
}

/* ============ Theme Loading ============ */

void cursor_set_builtin_theme(bool dark) {
    /* Set theme metadata */
    const char *name = dark ? "NeolyxOS Dark" : "NeolyxOS Light";
    int i = 0;
    while (name[i] && i < 31) { g_cursor_theme.name[i] = name[i]; i++; }
    g_cursor_theme.name[i] = '\0';
    
    const char *author = "KetiveeAI";
    i = 0;
    while (author[i] && i < 63) { g_cursor_theme.author[i] = author[i]; i++; }
    g_cursor_theme.author[i] = '\0';
    
    g_cursor_theme.version = 1;
    g_cursor_theme.primary_color = dark ? CURSOR_COLOR_OUTLINE : CURSOR_COLOR_LEFT;
    g_cursor_theme.outline_color = dark ? 0xFFFFFFFF : CURSOR_COLOR_OUTLINE;
    
    /* Set up arrow cursor */
    cursor_image_t *arrow = &g_cursor_theme.images[NX_CURSOR_ARROW];
    arrow->width = CURSOR_DEFAULT_WIDTH;
    arrow->height = CURSOR_DEFAULT_HEIGHT;
    arrow->hotspot_x = CURSOR_DEFAULT_HOTSPOT_X;
    arrow->hotspot_y = CURSOR_DEFAULT_HOTSPOT_Y;
    arrow->pixels = NULL;
    arrow->bitmap = dark ? cursor_arrow_outline : cursor_arrow_left;
    arrow->is_loaded = true;
    
    /* Copy arrow as default for other cursor types until they're implemented */
    for (int t = 1; t < NX_CURSOR_COUNT; t++) {
        g_cursor_theme.images[t] = *arrow;
        g_cursor_theme.animations[t].is_animated = false;
        g_cursor_theme.animations[t].frame_count = 1;
    }
    
    g_cursor_theme.is_loaded = true;
    serial_puts("[CURSOR] Built-in theme loaded: ");
    serial_puts(g_cursor_theme.name);
    serial_puts("\n");
}

int cursor_load_theme(const char *path) {
    /* TODO: Load theme from .nxcursor package */
    /* For now, use built-in theme */
    (void)path;
    cursor_set_builtin_theme(false);
    return 0;
}

const cursor_theme_t* cursor_get_theme(void) {
    return &g_cursor_theme;
}

/* ============ Cursor Type ============ */

void cursor_set_type(nx_cursor_type_t type) {
    if (type < NX_CURSOR_COUNT) {
        g_cursor_state.requested_type = type;
        g_cursor_state.current_type = type;
    }
}

nx_cursor_type_t cursor_get_type(void) {
    return g_cursor_state.current_type;
}

/* ============ Position ============ */

void cursor_get_position(int32_t *x, int32_t *y) {
    if (x) *x = g_cursor_state.screen_x;
    if (y) *y = g_cursor_state.screen_y;
}

void cursor_set_position(int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= g_cursor_state.screen_width) x = g_cursor_state.screen_width - 1;
    if (y >= g_cursor_state.screen_height) y = g_cursor_state.screen_height - 1;
    
    g_cursor_state.x = (float)x;
    g_cursor_state.y = (float)y;
    g_cursor_state.screen_x = x;
    g_cursor_state.screen_y = y;
}

const cursor_state_t* cursor_get_state(void) {
    return &g_cursor_state;
}

void cursor_lock(void) {
    g_cursor_state.locked = true;
}

void cursor_unlock(void) {
    g_cursor_state.locked = false;
}

/* ============ Convenience Functions ============ */

bool cursor_is_initialized(void) {
    return g_initialized;
}

void cursor_render(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!g_initialized || !fb) return;
    
    /* Update screen dimensions if changed */
    if (width != g_cursor_state.screen_width || height != g_cursor_state.screen_height) {
        g_cursor_state.screen_width = width;
        g_cursor_state.screen_height = height;
        
        /* Clamp cursor position to new bounds */
        if (g_cursor_state.screen_x >= (int32_t)width) 
            g_cursor_state.screen_x = width - 1;
        if (g_cursor_state.screen_y >= (int32_t)height) 
            g_cursor_state.screen_y = height - 1;
    }
    
    /* Delegate to cursor_draw */
    cursor_draw((volatile uint32_t*)fb, pitch);
}
