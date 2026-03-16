/*
 * NeolyxOS Notification Center Implementation
 * 
 * Slide-in notification panel from top-right.
 * Displays system and app notifications with dismiss.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/notification_center.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ Static State ============ */

static notification_center_state_t g_nc;
static int g_nc_initialized = 0;
static uint32_t g_next_notification_id = 1;

/* ============ String Helpers ============ */

static void nc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Drawing Primitives ============ */

static inline void nc_put_pixel_alpha(uint32_t *fb, uint32_t pitch,
                                       int32_t x, int32_t y, uint32_t color,
                                       uint32_t sw, uint32_t sh) {
    if (x < 0 || x >= (int32_t)sw || y < 0 || y >= (int32_t)sh) return;
    
    uint8_t a = (color >> 24) & 0xFF;
    if (a == 0xFF) {
        fb[y * (pitch / 4) + x] = color;
        return;
    }
    if (a == 0x00) return;
    
    uint32_t bg = fb[y * (pitch / 4) + x];
    uint8_t br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint8_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fbb = color & 0xFF;
    
    uint8_t r = (fr * a + br * (255 - a)) / 255;
    uint8_t g = (fg * a + bg_ * (255 - a)) / 255;
    uint8_t b = (fbb * a + bb * (255 - a)) / 255;
    
    fb[y * (pitch / 4) + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void nc_fill_rect(uint32_t *fb, uint32_t pitch,
                          int32_t x, int32_t y, uint32_t w, uint32_t h, 
                          uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            nc_put_pixel_alpha(fb, pitch, x + i, y + j, color,
                               g_nc.screen_width, g_nc.screen_height);
        }
    }
}

static void nc_fill_circle(uint32_t *fb, uint32_t pitch,
                            int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                nc_put_pixel_alpha(fb, pitch, cx + dx, cy + dy, color,
                                   g_nc.screen_width, g_nc.screen_height);
            }
        }
    }
}

static void nc_fill_rounded_rect(uint32_t *fb, uint32_t pitch,
                                  int32_t x, int32_t y, uint32_t w, uint32_t h,
                                  int32_t r, uint32_t color) {
    if (r > (int32_t)(h / 2)) r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    
    nc_fill_rect(fb, pitch, x + r, y, w - 2*r, h, color);
    nc_fill_rect(fb, pitch, x, y + r, r, h - 2*r, color);
    nc_fill_rect(fb, pitch, x + w - r, y + r, r, h - 2*r, color);
    
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t dist_sq = (r - dx - 1) * (r - dx - 1) + (r - dy - 1) * (r - dy - 1);
            if (dist_sq <= r * r) {
                nc_put_pixel_alpha(fb, pitch, x + dx, y + dy, color, g_nc.screen_width, g_nc.screen_height);
                nc_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + dy, color, g_nc.screen_width, g_nc.screen_height);
                nc_put_pixel_alpha(fb, pitch, x + dx, y + h - 1 - dy, color, g_nc.screen_width, g_nc.screen_height);
                nc_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + h - 1 - dy, color, g_nc.screen_width, g_nc.screen_height);
            }
        }
    }
}

extern void desktop_draw_text(int x, int y, const char *text, uint32_t color);

/* ============ Initialization ============ */

void notification_center_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_nc_initialized) return;
    
    g_nc.screen_width = screen_w;
    g_nc.screen_height = screen_h;
    g_nc.visible = 0;
    g_nc.count = 0;
    g_nc.hover_idx = -1;
    g_nc.scroll_offset = 0;
    g_nc.slide_progress = 0;
    
    /* Position: top-right, left of Control Center */
    g_nc.width = NC_WIDTH;
    g_nc.height = NC_HEIGHT;
    g_nc.x = screen_w - NC_WIDTH - NC_MARGIN - 360;  /* Left of CC */
    g_nc.y = 32;
    
    /* Add sample notification */
    notification_push("System", "Welcome to NeolyxOS", "Your desktop is ready.", 0xFF007AFF);
    
    g_nc_initialized = 1;
}

/* ============ Toggle ============ */

void notification_center_toggle(void) {
    g_nc.visible = !g_nc.visible;
    if (g_nc.visible) g_nc.slide_progress = 0;
}

void notification_center_show(void) {
    g_nc.visible = 1;
    g_nc.slide_progress = 0;
}

void notification_center_hide(void) {
    g_nc.visible = 0;
}

int notification_center_is_visible(void) {
    return g_nc.visible;
}

/* ============ Notification Management ============ */

uint32_t notification_push(const char *app_name, const char *title, const char *message, uint32_t icon_color) {
    if (g_nc.count >= NC_MAX_NOTIFICATIONS) {
        /* Remove oldest */
        for (int i = 0; i < g_nc.count - 1; i++) {
            g_nc.notifications[i] = g_nc.notifications[i + 1];
        }
        g_nc.count--;
    }
    
    notification_t *notif = &g_nc.notifications[g_nc.count];
    notif->id = g_next_notification_id++;
    nc_strcpy(notif->app_name, app_name, 32);
    nc_strcpy(notif->title, title, 64);
    nc_strcpy(notif->message, message, 128);
    notif->icon_color = icon_color ? icon_color : NC_ACCENT;
    notif->timestamp = nx_gettime();
    notif->read = 0;
    notif->dismissed = 0;
    
    g_nc.count++;
    return notif->id;
}

void notification_dismiss(uint32_t id) {
    for (int i = 0; i < g_nc.count; i++) {
        if (g_nc.notifications[i].id == id) {
            /* Shift remaining */
            for (int j = i; j < g_nc.count - 1; j++) {
                g_nc.notifications[j] = g_nc.notifications[j + 1];
            }
            g_nc.count--;
            return;
        }
    }
}

void notification_clear_all(void) {
    g_nc.count = 0;
}

int notification_get_count(void) {
    return g_nc.count;
}

/* ============ Rendering ============ */

void notification_center_render(uint32_t *fb, uint32_t pitch) {
    if (!g_nc.visible || !g_nc_initialized) return;
    
    int px = g_nc.x;
    int py = g_nc.y;
    int pw = g_nc.width;
    int ph = g_nc.height;
    
    /* Main panel background */
    nc_fill_rounded_rect(fb, pitch, px, py, pw, ph, NC_CORNER_R, NC_BG_PRIMARY);
    
    /* Header */
    desktop_draw_text(px + 16, py + 12, "NOTIFICATIONS", NC_TEXT_PRIMARY);
    
    /* Clear all button */
    if (g_nc.count > 0) {
        desktop_draw_text(px + pw - 70, py + 12, "Clear", NC_TEXT_SECONDARY);
    }
    
    int content_y = py + 40;
    int content_h = ph - 50;
    
    if (g_nc.count == 0) {
        desktop_draw_text(px + pw/2 - 60, py + ph/2 - 8, "No notifications", NC_TEXT_DIM);
        return;
    }
    
    /* Render notifications */
    int y_offset = content_y - g_nc.scroll_offset;
    
    for (int i = 0; i < g_nc.count; i++) {
        notification_t *notif = &g_nc.notifications[i];
        
        int notif_y = y_offset + i * (NC_NOTIF_HEIGHT + NC_NOTIF_GAP);
        
        /* Skip if outside visible area */
        if (notif_y + NC_NOTIF_HEIGHT < content_y) continue;
        if (notif_y > py + ph) break;
        
        /* Background */
        uint32_t bg = (i == g_nc.hover_idx) ? NC_BG_HOVER : NC_BG_NOTIFICATION;
        nc_fill_rounded_rect(fb, pitch, px + 12, notif_y, pw - 24, NC_NOTIF_HEIGHT, 8, bg);
        
        /* Icon */
        nc_fill_circle(fb, pitch, px + 32, notif_y + NC_NOTIF_HEIGHT/2, 14, notif->icon_color);
        
        /* App name */
        desktop_draw_text(px + 54, notif_y + 8, notif->app_name, NC_TEXT_DIM);
        
        /* Title */
        desktop_draw_text(px + 54, notif_y + 24, notif->title, NC_TEXT_PRIMARY);
        
        /* Message (truncated) */
        desktop_draw_text(px + 54, notif_y + 40, notif->message, NC_TEXT_SECONDARY);
        
        /* Dismiss X */
        desktop_draw_text(px + pw - 36, notif_y + 8, "x", NC_TEXT_DIM);
    }
}

/* ============ Animation ============ */

void notification_center_tick(void) {
    if (!g_nc_initialized) return;
    
    if (g_nc.visible && g_nc.slide_progress < 100) {
        g_nc.slide_progress += 10;
        if (g_nc.slide_progress > 100) g_nc.slide_progress = 100;
    }
}

/* ============ Input Handling ============ */

int notification_center_handle_input(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_nc.visible || !g_nc_initialized) return 0;
    
    /* Check if outside panel */
    if (mouse_x < g_nc.x || mouse_x > g_nc.x + g_nc.width ||
        mouse_y < g_nc.y || mouse_y > g_nc.y + g_nc.height) {
        if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
            notification_center_hide();
            return 1;
        }
        return 0;
    }
    
    int rel_x = mouse_x - g_nc.x;
    int rel_y = mouse_y - g_nc.y;
    
    /* Update hover */
    g_nc.hover_idx = -1;
    if (rel_y >= 40 && g_nc.count > 0) {
        int idx = (rel_y - 40 + g_nc.scroll_offset) / (NC_NOTIF_HEIGHT + NC_NOTIF_GAP);
        if (idx >= 0 && idx < g_nc.count) {
            g_nc.hover_idx = idx;
        }
    }
    
    /* Click handling */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Clear all button */
        if (rel_y < 40 && rel_x > g_nc.width - 80) {
            notification_clear_all();
            return 1;
        }
        
        /* Dismiss notification (X button) */
        if (g_nc.hover_idx >= 0 && rel_x > g_nc.width - 48) {
            notification_dismiss(g_nc.notifications[g_nc.hover_idx].id);
            return 1;
        }
        
        /* Mark as read on click */
        if (g_nc.hover_idx >= 0) {
            g_nc.notifications[g_nc.hover_idx].read = 1;
            return 1;
        }
    }
    
    return 1;
}

notification_center_state_t* notification_center_get_state(void) {
    return &g_nc;
}
