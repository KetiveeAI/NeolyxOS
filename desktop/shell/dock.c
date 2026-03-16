/*
 * NeolyxOS Dock Module Implementation
 * 
 * Extracted from desktop_shell.c for modularity.
 * Uses nxappearance settings for icon size, magnification, position.
 * Subscribes to NX_EVENT_DOCK_* events for live updates.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/dock.h"
#include "../include/nxconfig.h"
#include "../include/nxsyscall.h"
#include "../include/nxi_render.h"
#include <stddef.h>


/* ============ Static State ============ */

static dock_state_t g_dock;
static int g_dock_initialized = 0;

/* ============ String Helpers ============ */

static void dock_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Drawing Primitives ============ */

/* These would normally come from a shared drawing module */
/* For now, inline implementations matching desktop_shell.c */

static inline void dock_put_pixel(uint32_t *fb, uint32_t pitch, 
                                   int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= (int32_t)g_dock.screen_width || 
        y < 0 || y >= (int32_t)g_dock.screen_height) return;
    fb[y * (pitch / 4) + x] = color;
}

static void dock_put_pixel_alpha(uint32_t *fb, uint32_t pitch,
                                  int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= (int32_t)g_dock.screen_width || 
        y < 0 || y >= (int32_t)g_dock.screen_height) return;
    
    uint8_t a = (color >> 24) & 0xFF;
    uint32_t *row = fb + y * (pitch >> 2);  /* Optimized: compute row once */
    
    if (a == 0xFF) {
        row[x] = color;
        return;
    }
    if (a == 0x00) return;
    
    uint32_t bg = row[x];
    uint8_t br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint8_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fbb = color & 0xFF;
    
    uint8_t r = (fr * a + br * (255 - a)) / 255;
    uint8_t g = (fg * a + bg_ * (255 - a)) / 255;
    uint8_t b = (fbb * a + bb * (255 - a)) / 255;
    
    row[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void dock_fill_rect_alpha(uint32_t *fb, uint32_t pitch,
                                  int32_t x, int32_t y, uint32_t w, uint32_t h, 
                                  uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            dock_put_pixel_alpha(fb, pitch, x + i, y + j, color);
        }
    }
}

static void dock_fill_circle(uint32_t *fb, uint32_t pitch,
                              int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                dock_put_pixel_alpha(fb, pitch, cx + dx, cy + dy, color);
            }
        }
    }
}

static void dock_draw_circle(uint32_t *fb, uint32_t pitch,
                              int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    int32_t x = 0, y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        dock_put_pixel_alpha(fb, pitch, cx + x, cy + y, color);
        dock_put_pixel_alpha(fb, pitch, cx - x, cy + y, color);
        dock_put_pixel_alpha(fb, pitch, cx + x, cy - y, color);
        dock_put_pixel_alpha(fb, pitch, cx - x, cy - y, color);
        dock_put_pixel_alpha(fb, pitch, cx + y, cy + x, color);
        dock_put_pixel_alpha(fb, pitch, cx - y, cy + x, color);
        dock_put_pixel_alpha(fb, pitch, cx + y, cy - x, color);
        dock_put_pixel_alpha(fb, pitch, cx - y, cy - x, color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

static void dock_fill_rounded_rect(uint32_t *fb, uint32_t pitch,
                                    int32_t x, int32_t y, uint32_t w, uint32_t h,
                                    int32_t r, uint32_t color) {
    if (r > (int32_t)(h / 2)) r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    
    dock_fill_rect_alpha(fb, pitch, x + r, y, w - 2*r, h, color);
    dock_fill_rect_alpha(fb, pitch, x, y + r, r, h - 2*r, color);
    dock_fill_rect_alpha(fb, pitch, x + w - r, y + r, r, h - 2*r, color);
    
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t dist_sq = (r - dx - 1) * (r - dx - 1) + (r - dy - 1) * (r - dy - 1);
            if (dist_sq <= r * r) {
                dock_put_pixel_alpha(fb, pitch, x + dx, y + dy, color);
                dock_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + dy, color);
                dock_put_pixel_alpha(fb, pitch, x + dx, y + h - 1 - dy, color);
                dock_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + h - 1 - dy, color);
            }
        }
    }
}

static void dock_fill_pill(uint32_t *fb, uint32_t pitch,
                            int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    int32_t r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    dock_fill_rounded_rect(fb, pitch, x, y, w, h, r, color);
}

static void dock_draw_pill_border(uint32_t *fb, uint32_t pitch,
                                   int32_t x, int32_t y, uint32_t w, uint32_t h, 
                                   uint32_t color) {
    int32_t r = h / 2;
    for (uint32_t i = r; i < w - r; i++) {
        dock_put_pixel_alpha(fb, pitch, x + i, y, color);
        dock_put_pixel_alpha(fb, pitch, x + i, y + h - 1, color);
    }
    dock_draw_circle(fb, pitch, x + r, y + r, r, color);
    dock_draw_circle(fb, pitch, x + w - r - 1, y + r, r, color);
}

/* ============================================================================
 * ICON LOADING FROM NXI FILES
 * ============================================================================
 * 
 * !!! DO NOT ADD HARDCODED ICON DRAWING HERE !!!
 * 
 * All icons MUST be loaded from .nxi files stored in:
 *   /System/Icons/  - System icons
 *   /Applications/[App].app/resources/icons/  - App icons
 * 
 * Use nxi_draw_icon_fallback() or nxi_render_icon_by_name() to render.
 * If an icon is missing, the system will show a placeholder.
 * To add a new icon, create the .nxi file using IconLay.app.
 * 
 * Icon ID is resolved ONCE at add-time via dock_resolve_icon_id(),
 * NOT per-frame. This is a performance and architecture decision.
 * 
 * See: desktop/include/nxi_render.h for available icon IDs
 * ============================================================================ */

/* Draw dock icon using NXI renderer with pre-resolved icon_id */
static void dock_draw_app_icon(uint32_t *fb, uint32_t pitch,
                                int cx, int cy, int size, 
                                dock_icon_t *icon) {
    if (!icon) return;
    
    /* Use pre-resolved icon_id (set at add-time, NOT per-frame label lookup) */
    nxi_draw_icon_fallback(fb, pitch, icon->icon_id, 
                           cx - size/2, cy - size/2, size,
                           0xFFFFFFFF);  /* White tint for visibility */
}


void dock_init(uint32_t screen_w, uint32_t screen_h) {
    extern void nx_debug_print(const char *msg);
    nx_debug_print("[DOCK] dock_init() entered\n");
    
    if (g_dock_initialized) return;
    
    g_dock.screen_width = screen_w;
    g_dock.screen_height = screen_h;
    g_dock.icon_count = 0;
    g_dock.hover_idx = -1;
    g_dock.pressed_idx = -1;
    g_dock.minimize_anim_idx = -1;
    g_dock.minimize_progress = 0;
    g_dock.visible = 1;
    g_dock.autohide_timer = 0;
    g_dock.position = DOCK_POS_BOTTOM;
    
    nx_debug_print("[DOCK] Getting appearance settings...\n");
    /* Get settings from nxappearance */
    g_dock.settings = nxappearance_get();
    
    nx_debug_print("[DOCK] Adding default icons...\n");
    /* Add default icons (from dock_icon_colors) */
    static const char *default_labels[] = {
        "Files", "Safari", "Messages", "Notes", 
        "Photos", "Music", "Mail", "Settings"
    };
    for (int i = 0; i < DOCK_ICON_COLOR_COUNT && i < 8; i++) {
        dock_add_icon(i + 1, default_labels[i], dock_icon_colors[i]);
    }
    
    /* Mark first app as active */
    if (g_dock.icon_count > 0) {
        g_dock.icons[0].active = 1;
    }
    
    nx_debug_print("[DOCK] Subscribing to events...\n");
    /* Subscribe to dock events */
    nxevent_subscribe_range(NX_EVENT_DOCK_RESIZE, NX_EVENT_DOCK_ICON_REMOVED,
                            dock_event_handler, NULL);
    
    g_dock_initialized = 1;
    nx_debug_print("[DOCK] dock_init() complete\n");
}

/* ============ Icon Management ============ */

/* Resolve NXI icon ID from app label (called once at add-time, NOT per-frame) */
static uint32_t dock_resolve_icon_id(const char *label) {
    if (!label || !label[0]) return NXI_ICON_APP;
    
    /* Match app name to pre-defined icon ID */
    switch (label[0]) {
        case 'F':
            if (label[1] == 'i') return NXI_ICON_FOLDER;  /* Files */
            break;
        case 'S':
            if (label[1] == 'a') return NXI_ICON_SEARCH;   /* Safari */
            if (label[1] == 'e') return NXI_ICON_SETTINGS; /* Settings */
            break;
        case 'M':
            if (label[1] == 'u') return NXI_ICON_PLAY;     /* Music */
            break;
        case 'N':
            return NXI_ICON_FILE;  /* Notes */
        case 'T':
            return NXI_ICON_TERMINAL;
        default:
            break;
    }
    
    return NXI_ICON_APP;  /* Default app icon */
}

int dock_add_icon(uint32_t app_id, const char *label, uint32_t color) {
    if (g_dock.icon_count >= DOCK_MAX_ICONS) return -1;
    
    dock_icon_t *icon = &g_dock.icons[g_dock.icon_count];
    
    /* CRITICAL: Zero entire struct first to prevent undefined behavior */
    /* As struct grows, this ensures all new fields are initialized */
    for (int i = 0; i < (int)sizeof(dock_icon_t); i++) {
        ((char*)icon)[i] = 0;
    }
    
    /* Set known values */
    icon->app_id = app_id;
    icon->color = color;
    icon->icon_id = dock_resolve_icon_id(label);  /* Resolve ONCE, not per-frame */
    dock_strcpy(icon->label, label, 32);
    
    return g_dock.icon_count++;
}

void dock_remove_icon(uint32_t app_id) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            /* Shift remaining icons */
            for (int j = i; j < g_dock.icon_count - 1; j++) {
                g_dock.icons[j] = g_dock.icons[j + 1];
            }
            g_dock.icon_count--;
            return;
        }
    }
}

void dock_set_running(uint32_t app_id, int running) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            g_dock.icons[i].running = running;
            return;
        }
    }
}

void dock_start_bounce(uint32_t app_id) {
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].app_id == app_id) {
            g_dock.icons[i].bouncing = 1;
            g_dock.icons[i].bounce_frame = 0;
            return;
        }
    }
}

/* ============ Rendering ============ */

void dock_render(uint32_t *fb, uint32_t pitch, int mouse_x, int mouse_y) {
    extern void nx_debug_print(const char *msg);
    static int dock_debug_count = 0;
    
    if (!g_dock.visible || !g_dock_initialized) {
        if (dock_debug_count < 3) {
            dock_debug_count++;
            if (!g_dock.visible) nx_debug_print("[DOCK] Not visible\n");
            if (!g_dock_initialized) nx_debug_print("[DOCK] Not initialized\n");
        }
        return;
    }
    
    appearance_settings_t *app = g_dock.settings;
    if (!app) app = nxappearance_get();
    
    int base_size = app->dock_icon_size;
    int icon_count = g_dock.icon_count;
    if (icon_count < 2) icon_count = 2;  /* Minimum width */
    
    int dock_w = (base_size + DOCK_ICON_GAP) * icon_count + DOCK_PADDING * 2;
    int dock_h = base_size + 18;
    int dock_x = (g_dock.screen_width - dock_w) / 2;
    int dock_y = g_dock.screen_height - dock_h - DOCK_MARGIN_BOTTOM;
    
    /* Store computed geometry */
    g_dock.x = dock_x;
    g_dock.y = dock_y;
    g_dock.width = dock_w;
    g_dock.height = dock_h;
    
    /* Detect hover */
    g_dock.hover_idx = -1;
    if (mouse_y >= dock_y && mouse_y <= dock_y + dock_h) {
        int mx = mouse_x - dock_x - DOCK_PADDING;
        if (mx >= 0 && mx < g_dock.icon_count * (base_size + DOCK_ICON_GAP)) {
            g_dock.hover_idx = mx / (base_size + DOCK_ICON_GAP);
            if (g_dock.hover_idx >= g_dock.icon_count) {
                g_dock.hover_idx = -1;
            }
        }
    }
    
    /* Frosted glass background */
    uint32_t glass_color = GLASS_COLOR(0x303050, app);
    dock_fill_pill(fb, pitch, dock_x, dock_y, dock_w, dock_h, glass_color);
    dock_draw_pill_border(fb, pitch, dock_x, dock_y, dock_w, dock_h, 0x40FFFFFF);
    
    /* Render icons */
    int ix = dock_x + DOCK_PADDING;
    int iy = dock_y + (dock_h - base_size) / 2;
    
    for (int i = 0; i < g_dock.icon_count; i++) {
        dock_icon_t *icon = &g_dock.icons[i];
        
        /* Calculate size with magnification */
        int size = base_size;
        int y_offset = 0;
        
        if (app->dock_magnification && g_dock.hover_idx >= 0) {
            int dist = i - g_dock.hover_idx;
            if (dist < 0) dist = -dist;
            
            float mag = app->dock_magnification_amount;
            if (mag < 1.0f) mag = 1.5f;
            
            if (dist == 0) {
                size = (int)(base_size * mag);
                y_offset = -(size - base_size) / 2;
            } else if (dist == 1) {
                size = base_size + (int)((mag - 1.0f) * base_size * 0.5f);
                y_offset = -(size - base_size) / 2;
            } else if (dist == 2) {
                size = base_size + (int)((mag - 1.0f) * base_size * 0.2f);
                y_offset = -(size - base_size) / 2;
            }
        }
        
        /* Bounce animation */
        if (icon->bouncing) {
            int bounce_height = 0;
            int phase = icon->bounce_frame % 30;
            if (phase < 15) {
                bounce_height = phase;
            } else {
                bounce_height = 30 - phase;
            }
            y_offset -= bounce_height;
        }
        
        /* Minimize animation */
        if (i == g_dock.minimize_anim_idx && g_dock.minimize_progress > 0) {
            int bounce = (g_dock.minimize_progress < 50) ? 
                          g_dock.minimize_progress : (100 - g_dock.minimize_progress);
            y_offset = -bounce / 5;
        }
        
        int cx = ix + size / 2;
        int cy = iy + y_offset + size / 2;
        
        /* Drop shadow (offset, lower opacity) */
        dock_fill_circle(fb, pitch, cx + 2, cy + 2, size / 2 - 2, 0x40000000);
        
        /* Icon background circle */
        dock_fill_circle(fb, pitch, cx, cy, size / 2 - 2, icon->color);
        
        /* Icon from NXI file (file-based icons, NOT hardcoded shapes) */
        dock_draw_app_icon(fb, pitch, cx, cy, size, icon);
        
        /* Subtle top highlight for glass effect */
        dock_fill_circle(fb, pitch, cx - size/6, cy - size/6, size/6, 0x20FFFFFF);
        
        /* Hover highlight ring */
        if (i == g_dock.hover_idx) {
            dock_draw_circle(fb, pitch, cx, cy, size / 2 - 1, 0x80FFFFFF);
        }
        
        /* Running indicator dot */
        if (icon->active || icon->running) {
            dock_fill_circle(fb, pitch, cx, dock_y + dock_h - 5, 2, 0xFFFFFFFF);
        }
        
        ix += base_size + DOCK_ICON_GAP;
    }
}

/* ============ Input Handling ============ */

int dock_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_dock.visible || !g_dock_initialized) return 0;
    
    /* Check if mouse is in dock area */
    if (mouse_y < g_dock.y || mouse_y > g_dock.y + g_dock.height ||
        mouse_x < g_dock.x || mouse_x > g_dock.x + g_dock.width) {
        return 0;
    }
    
    /* Click detection */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Left click - check for icon hit */
        if (g_dock.hover_idx >= 0 && g_dock.hover_idx < g_dock.icon_count) {
            g_dock.pressed_idx = g_dock.hover_idx;
            return 1;
        }
    }
    
    /* Release detection */
    if (!(buttons & 0x01) && (prev_buttons & 0x01)) {
        if (g_dock.pressed_idx >= 0 && g_dock.pressed_idx == g_dock.hover_idx) {
            /* Icon was clicked - launch app or focus window */
            dock_icon_t *icon = &g_dock.icons[g_dock.pressed_idx];
            
            /* Publish app launch event */
            nxevent_publish_app(NX_EVENT_APP_LAUNCHED, icon->app_id, 
                               icon->label, "/Applications/");
            
            /* Start bounce animation */
            dock_start_bounce(icon->app_id);
        }
        g_dock.pressed_idx = -1;
        return 1;
    }
    
    return 1;  /* Consume event if in dock area */
}

/* ============ State Access ============ */

dock_state_t* dock_get_state(void) {
    return &g_dock;
}

void dock_update_settings(void) {
    g_dock.settings = nxappearance_get();
}

/* ============ Animation ============ */

void dock_tick(void) {
    if (!g_dock_initialized) return;
    
    /* Update bounce animations */
    for (int i = 0; i < g_dock.icon_count; i++) {
        if (g_dock.icons[i].bouncing) {
            g_dock.icons[i].bounce_frame++;
            if (g_dock.icons[i].bounce_frame >= 60) {
                g_dock.icons[i].bouncing = 0;
                g_dock.icons[i].bounce_frame = 0;
            }
        }
    }
    
    /* Update minimize animation */
    if (g_dock.minimize_progress > 0) {
        g_dock.minimize_progress += 5;
        if (g_dock.minimize_progress >= 100) {
            g_dock.minimize_anim_idx = -1;
            g_dock.minimize_progress = 0;
        }
    }
    
    /* Autohide logic */
    if (g_dock.settings && g_dock.settings->dock_auto_hide) {
        /* TODO: Implement autohide timer */
    }
}

/* ============ Event Handler ============ */

void dock_event_handler(const nx_event_t *event, void *userdata) {
    (void)userdata;
    if (!event) return;
    
    switch (event->type) {
        case NX_EVENT_DOCK_RESIZE: {
            /* Icon size or position changed - reload settings */
            dock_update_settings();
            break;
        }
        
        case NX_EVENT_DOCK_POSITION: {
            /* Dock moved to different edge - not yet implemented */
            break;
        }
        
        case NX_EVENT_DOCK_AUTOHIDE: {
            /* Autohide toggled */
            dock_update_settings();
            break;
        }
        
        case NX_EVENT_DOCK_ICON_ADDED: {
            /* New app pinned to dock */
            if (event->data) {
                nx_app_data_t *app_data = (nx_app_data_t*)event->data;
                dock_add_icon(app_data->app_id, app_data->app_name, 0xFF007AFF);
            }
            break;
        }
        
        case NX_EVENT_DOCK_ICON_REMOVED: {
            /* App unpinned from dock */
            if (event->data) {
                nx_app_data_t *app_data = (nx_app_data_t*)event->data;
                dock_remove_icon(app_data->app_id);
            }
            break;
        }
        
        default:
            break;
    }
}
