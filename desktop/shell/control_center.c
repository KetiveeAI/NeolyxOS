/*
 * NeolyxOS Control Center Implementation
 * 
 * Modern glassmorphic quick settings panel with sections:
 * - Network Grid (WiFi, Bluetooth, AirDrop, Ethernet)
 * - Quick Actions (Focus, Dark Mode, Screen Mirror, Keyboard)
 * - Now Playing media controls
 * - Brightness and Volume sliders
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/control_center.h"
#include "../include/nxui.h"
/* Layout (relative to the panel) shared with control_center_handle_input */
#define CCL_PAD        16
#define CCL_TILE_H     48
#define CCL_NET_Y      16
#define CCL_SEC_Y      (CCL_NET_Y + 2 * (CCL_TILE_H + CC_GRID_GAP) + 8)       /* 140 */
#define CCL_SEC_H      72
#define CCL_MEDIA_Y    (CCL_SEC_Y + CCL_SEC_H + CC_SECTION_GAP + 8)             /* 232 */
#define CCL_MEDIA_H    90
#define CCL_BRIGHT_Y   (CCL_MEDIA_Y + CCL_MEDIA_H + CC_SECTION_GAP)             /* 334 */
#define CCL_VOL_Y      (CCL_BRIGHT_Y + 44)                                      /* 378 */
#define CCL_QA_Y       (CCL_VOL_Y + 44)                                         /* 422 */
#define CCL_QA_H       40

#include "../include/nxsyscall.h"
#include "../include/vm_detect.h"
#include "../include/nxi_render.h"
#include "../include/volume_osd.h"
#include "../include/theme_osd.h"
#include "../include/notification_center.h"
#include <stddef.h>

/* Config for security settings */
#include "nx_config.h"

extern void serial_puts(const char *s);

/* ============ External Desktop API ============ */

extern int desktop_launch_app(const char *name);

/* Settings panel IDs (must match settings.h) */
#define CC_PANEL_NETWORK     1
#define CC_PANEL_BLUETOOTH   2
#define CC_PANEL_DISPLAY     3
#define CC_PANEL_SOUND       4
#define CC_PANEL_APPEARANCE  9
#define CC_PANEL_SECURITY   15
#define CC_PANEL_KEYBOARD   19

/* Requested Settings panel to open (0 = none, set before launch) */
static int g_cc_pending_panel = 0;

/* Open Settings app with specific panel */
static void cc_open_settings_panel(int panel_id) {
    g_cc_pending_panel = panel_id;
    desktop_launch_app("Settings");
    control_center_hide();
}

/* Get pending panel (for Settings app to query) */
int control_center_get_pending_panel(void) {
    int p = g_cc_pending_panel;
    g_cc_pending_panel = 0;
    return p;
}

/* ============ Static State ============ */

static control_center_state_t g_cc;
static int g_cc_initialized = 0;

/* ============ String Helpers ============ */

static void cc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int cc_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

/* ============ Drawing Primitives ============ */

static inline void cc_put_pixel_alpha(uint32_t *fb, uint32_t pitch,
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

static void cc_fill_rect(uint32_t *fb, uint32_t pitch,
                          int32_t x, int32_t y, uint32_t w, uint32_t h, 
                          uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            cc_put_pixel_alpha(fb, pitch, x + i, y + j, color,
                               g_cc.screen_width, g_cc.screen_height);
        }
    }
}

static void cc_fill_circle(uint32_t *fb, uint32_t pitch,
                            int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                cc_put_pixel_alpha(fb, pitch, cx + dx, cy + dy, color,
                                   g_cc.screen_width, g_cc.screen_height);
            }
        }
    }
}

static void cc_fill_rounded_rect(uint32_t *fb, uint32_t pitch,
                                  int32_t x, int32_t y, uint32_t w, uint32_t h,
                                  int32_t r, uint32_t color) {
    if (r > (int32_t)(h / 2)) r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    
    cc_fill_rect(fb, pitch, x + r, y, w - 2*r, h, color);
    cc_fill_rect(fb, pitch, x, y + r, r, h - 2*r, color);
    cc_fill_rect(fb, pitch, x + w - r, y + r, r, h - 2*r, color);
    
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t dist_sq = (r - dx - 1) * (r - dx - 1) + (r - dy - 1) * (r - dy - 1);
            if (dist_sq <= r * r) {
                cc_put_pixel_alpha(fb, pitch, x + dx, y + dy, color, g_cc.screen_width, g_cc.screen_height);
                cc_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + dy, color, g_cc.screen_width, g_cc.screen_height);
                cc_put_pixel_alpha(fb, pitch, x + dx, y + h - 1 - dy, color, g_cc.screen_width, g_cc.screen_height);
                cc_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + h - 1 - dy, color, g_cc.screen_width, g_cc.screen_height);
            }
        }
    }
}

/* Simple text rendering (uses desktop_draw_text externally) */
extern void desktop_draw_text(int x, int y, const char *text, uint32_t color);

/* ============ Initialization ============ */

void control_center_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_cc_initialized) return;
    
    g_cc.screen_width = screen_w;
    g_cc.screen_height = screen_h;
    g_cc.visible = 0;
    g_cc.hover_section = -1;
    g_cc.hover_item = -1;
    g_cc.slide_progress = 0;
    
    /* Network toggles - default states */
    g_cc.wifi.enabled = 1;
    cc_strcpy(g_cc.wifi.label, "Wi-Fi", 24);
    cc_strcpy(g_cc.wifi.sublabel, "Connected", 32);
    g_cc.wifi.icon_color = CC_ACTIVE_GREEN;
    
    g_cc.bluetooth.enabled = 1;
    cc_strcpy(g_cc.bluetooth.label, "Bluetooth", 24);
    cc_strcpy(g_cc.bluetooth.sublabel, "On", 32);
    g_cc.bluetooth.icon_color = CC_ACTIVE_BLUE;
    
    g_cc.airdrop.enabled = 0;
    cc_strcpy(g_cc.airdrop.label, "NX Share", 24);
    cc_strcpy(g_cc.airdrop.sublabel, "Off", 32);
    g_cc.airdrop.icon_color = CC_ACTIVE_BLUE;
    
    g_cc.ethernet.enabled = 1;
    cc_strcpy(g_cc.ethernet.label, "Ethernet", 24);
    cc_strcpy(g_cc.ethernet.sublabel, "Connected", 32);
    g_cc.ethernet.icon_color = CC_ACTIVE_GREEN;
    
    /* Security toggle - firewall (read from config) */
    g_cc.firewall.enabled = g_nx_config.security.firewall_enabled;
    cc_strcpy(g_cc.firewall.label, "Firewall", 24);
    cc_strcpy(g_cc.firewall.sublabel, g_cc.firewall.enabled ? "Active" : "Off", 32);
    g_cc.firewall.icon_color = CC_ACTIVE_GREEN;
    g_cc.blocked_count = 0;
    g_cc.active_connections = 0;
    
    /* In VM mode: WiFi/Bluetooth not available, mark as N/A */
    if (vm_is_virtual()) {
        g_cc.wifi.enabled = 0;
        cc_strcpy(g_cc.wifi.sublabel, "N/A (VM)", 32);
        g_cc.bluetooth.enabled = 0;
        cc_strcpy(g_cc.bluetooth.sublabel, "N/A (VM)", 32);
    }
    
    /* Quick actions */
    g_cc.focus_mode.enabled = 0;
    cc_strcpy(g_cc.focus_mode.label, "Focus", 24);
    cc_strcpy(g_cc.focus_mode.sublabel, "", 32);
    g_cc.focus_mode.icon_color = CC_ACTIVE_ORANGE;
    
    g_cc.dark_mode.enabled = 1;
    cc_strcpy(g_cc.dark_mode.label, "Dark Mode", 24);
    cc_strcpy(g_cc.dark_mode.sublabel, "", 32);
    g_cc.dark_mode.icon_color = CC_ACTIVE_BLUE;
    
    g_cc.screen_mirror.enabled = 0;
    cc_strcpy(g_cc.screen_mirror.label, "Mirror", 24);
    cc_strcpy(g_cc.screen_mirror.sublabel, "", 32);
    g_cc.screen_mirror.icon_color = CC_ACTIVE_PURPLE;
    
    g_cc.keyboard_brightness.enabled = 0;
    cc_strcpy(g_cc.keyboard_brightness.label, "Keyboard", 24);
    cc_strcpy(g_cc.keyboard_brightness.sublabel, "", 32);
    g_cc.keyboard_brightness.icon_color = CC_ACTIVE_BLUE;
    
    /* Media */
    g_cc.media_playing = 0;
    cc_strcpy(g_cc.media_title, "Not Playing", 64);
    cc_strcpy(g_cc.media_artist, "", 32);
    g_cc.media_artwork_color = 0xFF505070;
    
    /* Sliders */
    g_cc.brightness = 80;
    g_cc.volume = 75;
    g_cc.mic_enabled = 1;
    cc_strcpy(g_cc.output_device, "Speakers", 32);
    
    /* Position: top-right corner */
    g_cc.width = CC_WIDTH;
    g_cc.height = CC_HEIGHT;
    g_cc.x = screen_w - CC_WIDTH - CC_MARGIN;
    g_cc.y = 32;  /* Below menu bar */
    
    g_cc_initialized = 1;
}

/* ============ Toggle ============ */

void control_center_toggle(void) {
    g_cc.visible = !g_cc.visible;
    if (g_cc.visible) g_cc.slide_progress = 0;
    serial_puts("[CC] Toggle: visible=");
    serial_puts(g_cc.visible ? "1\n" : "0\n");
}

void control_center_show(void) {
    g_cc.visible = 1;
    g_cc.slide_progress = 0;
}

void control_center_hide(void) {
    g_cc.visible = 0;
}

int control_center_is_visible(void) {
    return g_cc.visible;
}

/* Draw WiFi icon - signal waves */
static void draw_wifi_icon(uint32_t *fb, uint32_t pitch, int cx, int cy, uint32_t color) {
    /* Three arcs from bottom */
    for (int arc = 0; arc < 3; arc++) {
        int r = 4 + arc * 4;
        for (int a = -45; a <= 45; a += 15) {
            int px = cx + (r * a) / 60;  /* Approximation */
            int py = cy - (r * (45 - (a < 0 ? -a : a))) / 60;
            cc_fill_circle(fb, pitch, px, py, 1, color);
        }
    }
    /* Center dot */
    cc_fill_circle(fb, pitch, cx, cy + 4, 2, color);
}

/* Draw Bluetooth icon - B shape with runes */
static void draw_bluetooth_icon(uint32_t *fb, uint32_t pitch, int cx, int cy, uint32_t color) {
    /* Vertical line */
    cc_fill_rect(fb, pitch, cx - 1, cy - 8, 2, 16, color);
    /* Top right triangle */
    for (int i = 0; i < 6; i++) {
        cc_fill_rect(fb, pitch, cx + 1 + i, cy - 7 + i, 2, 1, color);
    }
    /* Bottom right triangle */
    for (int i = 0; i < 6; i++) {
        cc_fill_rect(fb, pitch, cx + 1 + i, cy + 7 - i, 2, 1, color);
    }
    /* Top left partial */
    for (int i = 0; i < 3; i++) {
        cc_fill_rect(fb, pitch, cx - 3 - i, cy - 4 + i, 2, 1, color);
    }
    /* Bottom left partial */
    for (int i = 0; i < 3; i++) {
        cc_fill_rect(fb, pitch, cx - 3 - i, cy + 4 - i, 2, 1, color);
    }
}

/* Draw Share icon - three circles connected */
static void draw_share_icon(uint32_t *fb, uint32_t pitch, int cx, int cy, uint32_t color) {
    /* Three nodes */
    cc_fill_circle(fb, pitch, cx, cy - 6, 3, color);      /* Top */
    cc_fill_circle(fb, pitch, cx - 6, cy + 4, 3, color);  /* Bottom left */
    cc_fill_circle(fb, pitch, cx + 6, cy + 4, 3, color);  /* Bottom right */
    /* Connecting lines (approximated with dots) */
    for (int i = 1; i < 6; i++) {
        cc_fill_circle(fb, pitch, cx - i, cy - 6 + i * 10 / 6, 1, color);      /* Top to BL */
        cc_fill_circle(fb, pitch, cx + i, cy - 6 + i * 10 / 6, 1, color);      /* Top to BR */
    }
}

/* Draw Ethernet icon - network plug */
static void draw_ethernet_icon(uint32_t *fb, uint32_t pitch, int cx, int cy, uint32_t color) {
    /* Main body rectangle */
    cc_fill_rounded_rect(fb, pitch, cx - 6, cy - 4, 12, 10, 2, color);
    /* Connector prongs */
    cc_fill_rect(fb, pitch, cx - 4, cy + 6, 2, 3, color);
    cc_fill_rect(fb, pitch, cx + 2, cy + 6, 2, 3, color);
    /* Inner cutout (dark) */
    cc_fill_rect(fb, pitch, cx - 4, cy - 2, 8, 6, 0x80000000);
}

/* Draw Shield icon - firewall security */
static void draw_shield_icon(uint32_t *fb, uint32_t pitch, int cx, int cy, uint32_t color) {
    /* Shield outline - rounded top, pointed bottom */
    cc_fill_rounded_rect(fb, pitch, cx - 7, cy - 8, 14, 10, 3, color);
    /* Bottom triangle */
    for (int row = 0; row < 6; row++) {
        int w = 14 - row * 2;
        if (w > 0) {
            cc_fill_rect(fb, pitch, cx - w/2, cy + 2 + row, w, 1, color);
        }
    }
    /* Checkmark cutout (dark) */
    cc_fill_rect(fb, pitch, cx - 3, cy - 1, 2, 4, 0x80000000);
    cc_fill_rect(fb, pitch, cx - 1, cy + 1, 2, 2, 0x80000000);
    cc_fill_rect(fb, pitch, cx + 1, cy - 2, 2, 4, 0x80000000);
}

/* Toggle tile with icon and label */
static void render_toggle_tile(uint32_t *fb, uint32_t pitch, 
                                int x, int y, int w, int h,
                                cc_toggle_t *toggle, int hovered, int icon_type) {
    /* Background card */
    uint32_t bg = hovered ? CC_BG_SECTION_HOVER : CC_BG_SECTION;
    cc_fill_rounded_rect(fb, pitch, x, y, w, h, CC_TOGGLE_R, bg);
    
    /* Icon circle background */
    int icon_cx = x + 24;
    int icon_cy = y + h / 2;
    uint32_t icon_bg = toggle->enabled ? toggle->icon_color : CC_TOGGLE_OFF;
    cc_fill_circle(fb, pitch, icon_cx, icon_cy, 14, icon_bg);
    
    /* Draw specific icon symbol */
    uint32_t symbol_color = 0xFFFFFFFF;  /* White symbol on colored bg */
    switch (icon_type) {
        case 0: draw_wifi_icon(fb, pitch, icon_cx, icon_cy, symbol_color); break;
        case 1: draw_bluetooth_icon(fb, pitch, icon_cx, icon_cy, symbol_color); break;
        case 2: draw_share_icon(fb, pitch, icon_cx, icon_cy, symbol_color); break;
        case 3: draw_ethernet_icon(fb, pitch, icon_cx, icon_cy, symbol_color); break;
        case 4: draw_shield_icon(fb, pitch, icon_cx, icon_cy, symbol_color); break; /* Firewall */
        default: break;
    }
    
    /* Label */
    desktop_draw_text(x + 46, y + 10, toggle->label, CC_TEXT_PRIMARY);
    
    /* Sublabel */
    if (toggle->sublabel[0]) {
        desktop_draw_text(x + 46, y + 24, toggle->sublabel, CC_TEXT_SECONDARY);
    }
    
    /* Chevron ">" to open settings (right side of tile) */
    int chevron_x = x + w - 16;
    int chevron_cy = y + h / 2;
    /* Draw small chevron arrow using lines */
    for (int i = 0; i < 4; i++) {
        cc_fill_rect(fb, pitch, chevron_x + i, chevron_cy - 4 + i, 2, 2, CC_TEXT_DIM);
        cc_fill_rect(fb, pitch, chevron_x + i, chevron_cy + 4 - i, 2, 2, CC_TEXT_DIM);
    }
}

/* Small quick action tile (icon only) */
static void render_quick_action(uint32_t *fb, uint32_t pitch,
                                 int x, int y, int size,
                                 cc_toggle_t *toggle, const char *icon_char, int hovered) {
    uint32_t bg = hovered ? CC_BG_SECTION_HOVER : CC_BG_SECTION;
    cc_fill_rounded_rect(fb, pitch, x, y, size, size, 8, bg);
    
    int cx = x + size / 2;
    int cy = y + size / 2 - 8;
    uint32_t icon_color = toggle->enabled ? toggle->icon_color : CC_TOGGLE_OFF;
    cc_fill_circle(fb, pitch, cx, cy, 12, icon_color);
    
    /* Label below icon */
    int label_x = x + (size - cc_strlen(toggle->label) * 4) / 2;
    desktop_draw_text(label_x, y + size - 16, toggle->label, CC_TEXT_SECONDARY);
}

/* Slider with icon - icon_type: 0 = brightness (sun), 1 = volume (speaker) */
static void render_slider(uint32_t *fb, uint32_t pitch, 
                           int x, int y, int w, int value,
                           int icon_type, int hovered) {
    (void)hovered;  /* Unused for now */
    
    /* Background track */
    cc_fill_rounded_rect(fb, pitch, x, y + 2, w, 20, 10, CC_SLIDER_BG);
    
    /* Fill */
    int fill_w = (w * value) / 100;
    if (fill_w > 8) {
        cc_fill_rounded_rect(fb, pitch, x, y + 2, fill_w, 20, 10, CC_SLIDER_FILL);
    }
    
    /* Draw icon at left using NXI icon system */
    int icon_x = x - 28;
    int icon_y = y;
    int icon_size = 20;
    uint32_t icon_id = (icon_type == 0) ? NXI_ICON_BRIGHTNESS : NXI_ICON_VOLUME;
    nxi_draw_icon_fallback(fb, pitch, icon_id, icon_x, icon_y, icon_size, CC_TEXT_PRIMARY);
}

/* ============ Main Render ============ */

static void control_center_render_legacy(uint32_t *fb, uint32_t pitch) {
    if (!g_cc.visible || !g_cc_initialized) return;
    
    int px = g_cc.x;
    int py = g_cc.y;
    int pw = g_cc.width;
    int ph = g_cc.height;
    
    /* Main glassmorphic panel */
    cc_fill_rounded_rect(fb, pitch, px, py, pw, ph, CC_CORNER_R, CC_BG_PRIMARY);
    
    /* Subtle border */
    /* (Top edge) */
    cc_fill_rect(fb, pitch, px + CC_CORNER_R, py, pw - 2*CC_CORNER_R, 1, CC_BORDER);
    
    int section_y = py + 16;
    int tile_w = (pw - 40 - CC_GRID_GAP) / 2;  /* Two columns */
    int tile_h = 48;
    
    /* === Network Grid (2x2) === */
    int net_x1 = px + 16;
    int net_x2 = net_x1 + tile_w + CC_GRID_GAP;
    
    /* Row 1: WiFi, Bluetooth */
    render_toggle_tile(fb, pitch, net_x1, section_y, tile_w, tile_h, 
                       &g_cc.wifi, g_cc.hover_item == 0, 0);  /* icon_type 0 = WiFi */
    render_toggle_tile(fb, pitch, net_x2, section_y, tile_w, tile_h, 
                       &g_cc.bluetooth, g_cc.hover_item == 1, 1);  /* icon_type 1 = Bluetooth */
    
    section_y += tile_h + CC_GRID_GAP;
    
    /* Row 2: NX Share, Ethernet */
    render_toggle_tile(fb, pitch, net_x1, section_y, tile_w, tile_h, 
                       &g_cc.airdrop, g_cc.hover_item == 2, 2);  /* icon_type 2 = Share */
    render_toggle_tile(fb, pitch, net_x2, section_y, tile_w, tile_h, 
                       &g_cc.ethernet, g_cc.hover_item == 3, 3);  /* icon_type 3 = Ethernet */
    
    section_y += tile_h + CC_GRID_GAP;
    
    /* Row 3: Firewall Security (full width) */
    render_toggle_tile(fb, pitch, net_x1, section_y, pw - 32, tile_h, 
                       &g_cc.firewall, g_cc.hover_item == 8, 4);  /* icon_type 4 = Shield */
    /* Show blocked count if any */
    if (g_cc.blocked_count > 0) {
        char blocked_str[16];
        int bc = g_cc.blocked_count;
        int pos = 0;
        if (bc >= 1000) {
            blocked_str[pos++] = '0' + (bc / 1000) % 10;
        }
        if (bc >= 100) {
            blocked_str[pos++] = '0' + (bc / 100) % 10;
        }
        if (bc >= 10) {
            blocked_str[pos++] = '0' + (bc / 10) % 10;
        }
        blocked_str[pos++] = '0' + bc % 10;
        blocked_str[pos] = '\0';
        desktop_draw_text(net_x1 + pw - 80, section_y + 18, blocked_str, CC_TEXT_SECONDARY);
    }
    
    section_y += tile_h + CC_SECTION_GAP + 8;
    
    /* === Quick Actions (2x2 smaller tiles) === */
    int qa_size = 72;
    int qa_gap = 8;
    int qa_x = px + (pw - (qa_size * 4 + qa_gap * 3)) / 2;
    
    render_quick_action(fb, pitch, qa_x, section_y, qa_size, 
                        &g_cc.focus_mode, "F", g_cc.hover_item == 4);
    render_quick_action(fb, pitch, qa_x + qa_size + qa_gap, section_y, qa_size, 
                        &g_cc.dark_mode, "D", g_cc.hover_item == 5);
    render_quick_action(fb, pitch, qa_x + (qa_size + qa_gap) * 2, section_y, qa_size, 
                        &g_cc.screen_mirror, "M", g_cc.hover_item == 6);
    render_quick_action(fb, pitch, qa_x + (qa_size + qa_gap) * 3, section_y, qa_size, 
                        &g_cc.keyboard_brightness, "K", g_cc.hover_item == 7);
    
    section_y += qa_size + CC_SECTION_GAP + 8;
    
    /* === Now Playing Section === */
    cc_fill_rounded_rect(fb, pitch, px + 16, section_y, pw - 32, 80, CC_TOGGLE_R, CC_BG_SECTION);
    
    /* Album art placeholder */
    cc_fill_rounded_rect(fb, pitch, px + 24, section_y + 8, 64, 64, 6, g_cc.media_artwork_color);
    
    /* Track info */
    desktop_draw_text(px + 100, section_y + 16, g_cc.media_title, CC_TEXT_PRIMARY);
    desktop_draw_text(px + 100, section_y + 32, g_cc.media_artist, CC_TEXT_SECONDARY);
    
    /* Play/Pause controls */
    int ctrl_y = section_y + 50;
    desktop_draw_text(px + 100, ctrl_y, "<<", CC_TEXT_SECONDARY);
    desktop_draw_text(px + 140, ctrl_y, g_cc.media_playing ? "||" : ">", CC_TEXT_PRIMARY);
    desktop_draw_text(px + 170, ctrl_y, ">>", CC_TEXT_SECONDARY);
    
    section_y += 90 + CC_SECTION_GAP;
    
    /* === Sliders Section === */
    /* Brightness */
    cc_fill_rounded_rect(fb, pitch, px + 16, section_y, pw - 32, 36, 8, CC_BG_SECTION);
    render_slider(fb, pitch, px + 48, section_y + 8, pw - 80, g_cc.brightness, 0, 0);
    
    section_y += 44;
    
    /* Volume */
    cc_fill_rounded_rect(fb, pitch, px + 16, section_y, pw - 32, 36, 8, CC_BG_SECTION);
    render_slider(fb, pitch, px + 48, section_y + 8, pw - 80, g_cc.volume, 1, 0);
}

/* ============ Animation ============ */

void control_center_tick(void) {
    if (!g_cc_initialized) return;
    
    /* Slide-in animation */
    if (g_cc.visible && g_cc.slide_progress < 100) {
        g_cc.slide_progress += 10;
        if (g_cc.slide_progress > 100) g_cc.slide_progress = 100;
    }
}

/* ============ Input Handling ============ */

int control_center_handle_input(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_cc.visible || !g_cc_initialized) return 0;
    
    /* Check if click is outside panel */
    if (mouse_x < g_cc.x || mouse_x > g_cc.x + g_cc.width ||
        mouse_y < g_cc.y || mouse_y > g_cc.y + g_cc.height) {
        /* Click outside - close panel */
        if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
            control_center_hide();
            return 1;
        }
        return 0;
    }
    
    int rel_x = mouse_x - g_cc.x;
    int rel_y = mouse_y - g_cc.y;
    
    /* Update hover state */
    g_cc.hover_item = -1;
    
    int tile_w = (g_cc.width - 40 - CC_GRID_GAP) / 2;
    int tile_h = 48;
    
    /* Network grid hover detection (rows 0-1) */
    if (rel_y >= 16 && rel_y < 16 + tile_h) {
        if (rel_x >= 16 && rel_x < 16 + tile_w) g_cc.hover_item = 0;  /* WiFi */
        else if (rel_x >= 16 + tile_w + CC_GRID_GAP) g_cc.hover_item = 1;  /* Bluetooth */
    } else if (rel_y >= 16 + tile_h + CC_GRID_GAP && rel_y < 16 + 2*tile_h + CC_GRID_GAP) {
        if (rel_x >= 16 && rel_x < 16 + tile_w) g_cc.hover_item = 2;  /* AirDrop */
        else if (rel_x >= 16 + tile_w + CC_GRID_GAP) g_cc.hover_item = 3;  /* Ethernet */
    } else if (rel_y >= CCL_QA_Y && rel_y < CCL_QA_Y + 2 * CCL_QA_H + 8) {
        /* Quick actions 2x2 (items 4..7) */
        int row = (rel_y - CCL_QA_Y) / (CCL_QA_H + 8);
        int col = (rel_x >= 16 + tile_w + CC_GRID_GAP) ? 1 : (rel_x >= 16 ? 0 : -1);
        if (row >= 0 && row < 2 && col >= 0) g_cc.hover_item = 4 + row * 2 + col;
    }
    
    /* Handle slider drags */
    if (buttons & 0x01) {
        /* Brightness slider area */
        int brightness_y = 16 + 2*(tile_h + CC_GRID_GAP) + 8 + 72 + CC_SECTION_GAP + 8 + 90 + CC_SECTION_GAP;
        if (rel_y >= brightness_y && rel_y < brightness_y + 36) {
            int slider_x = rel_x - 48;
            int slider_w = g_cc.width - 80;
            if (slider_x >= 0 && slider_x <= slider_w) {
                g_cc.brightness = (slider_x * 100) / slider_w;
                if (g_cc.brightness > 100) g_cc.brightness = 100;
                return 1;
            }
        }
        
        /* Volume slider area */
        int volume_y = brightness_y + 44;
        if (rel_y >= volume_y && rel_y < volume_y + 36) {
            int slider_x = rel_x - 48;
            int slider_w = g_cc.width - 80;
            if (slider_x >= 0 && slider_x <= slider_w) {
                g_cc.volume = (slider_x * 100) / slider_w;
                if (g_cc.volume > 100) g_cc.volume = 100;
                /* Trigger volume OSD */
                volume_osd_show(g_cc.volume, 0);
                return 1;
            }
        }
    }
    
    /* Handle toggle clicks */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        int tile_right = 16 + tile_w;  /* Right edge of left tile */
        int on_right_tile = (rel_x >= 16 + tile_w + CC_GRID_GAP);
        int click_x_in_tile = on_right_tile ? (rel_x - 16 - tile_w - CC_GRID_GAP) : (rel_x - 16);
        int on_chevron = (click_x_in_tile >= tile_w - 24);  /* Chevron area is last 24px */
        (void)tile_right;  /* Suppress unused warning */
        
        switch (g_cc.hover_item) {
            case 0:  /* WiFi */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_NETWORK);
                } else {
                    g_cc.wifi.enabled = !g_cc.wifi.enabled; 
                    cc_strcpy(g_cc.wifi.sublabel, g_cc.wifi.enabled ? "Connected" : "Off", 32);
                    /* Push notification for WiFi state change */
                    if (g_cc.wifi.enabled) {
                        notification_push("Wi-Fi", "Wi-Fi Connected", "You are now connected to the network", 0xFF30D158);
                    } else {
                        notification_push("Wi-Fi", "Wi-Fi Disconnected", "Wi-Fi has been turned off", 0xFFFF453A);
                    }
                }
                return 1;
            case 1:  /* Bluetooth */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_BLUETOOTH);
                } else {
                    g_cc.bluetooth.enabled = !g_cc.bluetooth.enabled; 
                    cc_strcpy(g_cc.bluetooth.sublabel, g_cc.bluetooth.enabled ? "On" : "Off", 32);
                }
                return 1;
            case 2:  /* NX Share */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_NETWORK);
                } else {
                    g_cc.airdrop.enabled = !g_cc.airdrop.enabled;
                    cc_strcpy(g_cc.airdrop.sublabel, g_cc.airdrop.enabled ? "Everyone" : "Off", 32);
                }
                return 1;
            case 3:  /* Ethernet */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_NETWORK);
                } else {
                    g_cc.ethernet.enabled = !g_cc.ethernet.enabled;
                    cc_strcpy(g_cc.ethernet.sublabel, g_cc.ethernet.enabled ? "Connected" : "Off", 32);
                }
                return 1;
            case 4:  /* Focus Mode - no settings panel yet */
                g_cc.focus_mode.enabled = !g_cc.focus_mode.enabled; 
                return 1;
            case 5:  /* Dark Mode */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_APPEARANCE);
                } else {
                    g_cc.dark_mode.enabled = !g_cc.dark_mode.enabled;
                    nxui_set_appearance(g_cc.dark_mode.enabled ? NXUI_APPEARANCE_DARK : NXUI_APPEARANCE_LIGHT);
                    theme_osd_show_dark_mode(g_cc.dark_mode.enabled);
                }
                return 1;
            case 6:  /* Screen Mirror */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_DISPLAY);
                } else {
                    g_cc.screen_mirror.enabled = !g_cc.screen_mirror.enabled;
                }
                return 1;
            case 7:  /* Keyboard Brightness */
                if (on_chevron) {
                    cc_open_settings_panel(CC_PANEL_KEYBOARD);
                } else {
                    g_cc.keyboard_brightness.enabled = !g_cc.keyboard_brightness.enabled;
                }
                return 1;
        }
    }
    
    return 1;  /* Consume event if in panel */
}

/* ============ State Updates ============ */

void control_center_set_wifi(int connected, const char *ssid) {
    g_cc.wifi.enabled = connected;
    if (ssid) cc_strcpy(g_cc.wifi.sublabel, ssid, 32);
    else cc_strcpy(g_cc.wifi.sublabel, connected ? "Connected" : "Off", 32);
}

void control_center_set_bluetooth(int enabled, const char *device_name) {
    g_cc.bluetooth.enabled = enabled;
    if (device_name) cc_strcpy(g_cc.bluetooth.sublabel, device_name, 32);
    else cc_strcpy(g_cc.bluetooth.sublabel, enabled ? "On" : "Off", 32);
}

void control_center_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_cc.volume = volume;
    /* Trigger volume OSD */
    volume_osd_show(volume, 0);
}

void control_center_set_brightness(int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    g_cc.brightness = brightness;
}

void control_center_set_now_playing(const char *title, const char *artist, int playing) {
    if (title) cc_strcpy(g_cc.media_title, title, 64);
    if (artist) cc_strcpy(g_cc.media_artist, artist, 32);
    g_cc.media_playing = playing;
}

void control_center_set_firewall(int enabled, int blocked_count, int active_conns) {
    g_cc.firewall.enabled = enabled;
    g_cc.blocked_count = blocked_count;
    g_cc.active_connections = active_conns;
    cc_strcpy(g_cc.firewall.sublabel, enabled ? "Active" : "Off", 32);
}

control_center_state_t* control_center_get_state(void) {
    return &g_cc;
}

/* ============ Canvas renderer ============ */

static float cc_anim = 0.0f;   /* 0 hidden .. 1 shown, eased */

/* A tile with a round icon well, label and sublabel; `on` lights it up. */
static void cc_tile(nxc_canvas_t *c, const nxui_theme_t *th, nxc_rect_t r, nxsym_t sym,
                    const char *label, const char *sub, int on, nxc_color_t on_color, int hover,
                    int chevron) {
    nxc_fill_rrect(c, r, 12.0f, hover ? th->control_fill_hover : th->control_fill);
    float cy = r.y + r.h * 0.5f;
    nxc_fill_circle(c, r.x + 24.0f, cy, 15.0f, on ? on_color : NXC_WITH_ALPHA(th->text_primary, 36));
    nxsym_draw(c, sym, r.x + 24.0f, cy, 17.0f, on ? th->text_on_accent : th->text_secondary, 1.0f);
    float tx = r.x + 48.0f, tw = r.w - 48.0f - (chevron ? 22.0f : 10.0f);
    if (sub && sub[0]) {
        nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(tx, r.y + 6.0f, tw, 20.0f), label, th->text_primary, NXC_ALIGN_LEFT);
        nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(tx, r.y + 24.0f, tw, 18.0f), sub, th->text_secondary, NXC_ALIGN_LEFT);
    } else {
        nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(tx, r.y, tw, r.h), label, th->text_primary, NXC_ALIGN_LEFT);
    }
    if (chevron) nxsym_draw(c, NXSYM_CHEVRON_RIGHT, r.x + r.w - 12.0f, cy, 12.0f, th->text_tertiary, 0.0f);
}

static void cc_slider(nxc_canvas_t *c, const nxui_theme_t *th, float x, float y, float w, nxsym_t sym,
                      int value, float sym_value) {
    nxc_rect_t track = nxc_rect(x + 32.0f, y + 12.0f, w - 32.0f, 12.0f);
    nxc_fill_rrect(c, track, 6.0f, th->control_fill);
    float fw = track.w * (float)(value < 0 ? 0 : value > 100 ? 100 : value) / 100.0f;
    if (fw < 12.0f) fw = 12.0f;
    nxc_fill_rrect(c, nxc_rect(track.x, track.y, fw, track.h), 6.0f, NXC_WITH_ALPHA(th->text_primary, 235));
    nxsym_draw(c, sym, x + 12.0f, y + 18.0f, 18.0f, th->text_primary, sym_value);
}

void control_center_render(nxc_canvas_t *c) {
    if (!c || !g_cc_initialized) return;
    float dt = nxui_frame_dt();
    cc_anim = nxui_approach(cc_anim, g_cc.visible ? 1.0f : 0.0f, 0.06f, dt);
    if (cc_anim < 0.01f) return;
    const nxui_theme_t *th = nxui_theme();
    float ease = nxui_ease_out_cubic(cc_anim);
    nxc_rect_t panel = nxc_rect((float)g_cc.x, (float)g_cc.y - (1.0f - ease) * 12.0f, (float)g_cc.width, (float)g_cc.height);

    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(ease * 255.0f));
    nxui_elevation(c, panel, th->radius_popover, 2);
    nxui_material(c, panel, th->radius_popover, &th->mat_popover);
    nxc_stroke_rrect(c, panel, th->radius_popover, 1.0f, th->window_border);

    float px = panel.x, py = panel.y;
    float tile_w = (float)((g_cc.width - 40 - CC_GRID_GAP) / 2);

    /* Network 2x2 */
    const cc_toggle_t *net[4] = { &g_cc.wifi, &g_cc.bluetooth, &g_cc.airdrop, &g_cc.ethernet };
    const nxsym_t nsym[4] = { NXSYM_WIFI, NXSYM_BLUETOOTH, NXSYM_AIRPLANE, NXSYM_ETHERNET };
    const nxc_color_t ncol[4] = { th->success, th->accent, th->accent, th->success };
    for (int i = 0; i < 4; i++) {
        float tx = px + CCL_PAD + (i % 2) * (tile_w + CC_GRID_GAP);
        float ty = py + CCL_NET_Y + (i / 2) * (CCL_TILE_H + CC_GRID_GAP);
        cc_tile(c, th, nxc_rect(tx, ty, tile_w, CCL_TILE_H), nsym[i], net[i]->label, net[i]->sublabel,
                net[i]->enabled, ncol[i], g_cc.hover_item == i, 1);
    }

    /* Security */
    nxc_rect_t sec = nxc_rect(px + CCL_PAD, py + CCL_SEC_Y, (float)g_cc.width - 2 * CCL_PAD, CCL_SEC_H);
    nxc_fill_rrect(c, sec, 12.0f, th->control_fill);
    nxc_fill_circle(c, sec.x + 28.0f, sec.y + sec.h * 0.5f, 18.0f, g_cc.firewall.enabled ? th->success : NXC_WITH_ALPHA(th->text_primary, 36));
    nxsym_draw(c, NXSYM_SHIELD, sec.x + 28.0f, sec.y + sec.h * 0.5f, 20.0f, th->text_on_accent, 0.0f);
    nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(sec.x + 56.0f, sec.y + 12.0f, 200.0f, 20.0f),
                      g_cc.firewall.enabled ? "Firewall active" : "Firewall off", th->text_primary, NXC_ALIGN_LEFT);
    char info[64];
    int n = 0;
    {
        char num[12]; int k = 0; int v = g_cc.blocked_count; char tmp[12]; int t = 0;
        if (v <= 0) tmp[t++] = '0';
        while (v > 0 && t < 11) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
        while (t > 0) num[k++] = tmp[--t];
        num[k] = '\0';
        const char *a = num; while (*a && n < 60) info[n++] = *a++;
        const char *b = " blocked  \xC2\xB7  "; while (*b && n < 60) info[n++] = *b++;
        v = g_cc.active_connections; t = 0; k = 0;
        if (v <= 0) tmp[t++] = '0';
        while (v > 0 && t < 11) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
        while (t > 0) num[k++] = tmp[--t];
        num[k] = '\0';
        a = num; while (*a && n < 60) info[n++] = *a++;
        b = " connections"; while (*b && n < 62) info[n++] = *b++;
        info[n] = '\0';
    }
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(sec.x + 56.0f, sec.y + 36.0f, 240.0f, 18.0f), info,
                      th->text_secondary, NXC_ALIGN_LEFT);

    /* Media */
    nxc_rect_t med = nxc_rect(px + CCL_PAD, py + CCL_MEDIA_Y, (float)g_cc.width - 2 * CCL_PAD, CCL_MEDIA_H);
    nxc_fill_rrect(c, med, 12.0f, th->control_fill);
    nxc_rect_t art = nxc_rect(med.x + 14.0f, med.y + 15.0f, 60.0f, 60.0f);
    nxc_paint_t ap = nxc_paint_linear(art.x, art.y, art.x + art.w, art.y + art.h,
                                      nxc_color_lerp(g_cc.media_artwork_color, 0xFFFFFFFF, 0.25f), g_cc.media_artwork_color);
    nxc_fill_rrect_paint(c, art, 10.0f, &ap);
    nxsym_draw(c, NXSYM_PLAY, art.x + 30.0f, art.y + 30.0f, 22.0f, NXC_ARGB(200, 255, 255, 255), 0.0f);
    nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(art.x + 74.0f, med.y + 20.0f, med.w - 100.0f, 20.0f),
                      g_cc.media_title, th->text_primary, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(art.x + 74.0f, med.y + 40.0f, med.w - 100.0f, 18.0f),
                      g_cc.media_artist[0] ? g_cc.media_artist : "No media", th->text_secondary, NXC_ALIGN_LEFT);
    float mcx = art.x + 74.0f + 20.0f, mcy = med.y + 68.0f;
    nxsym_draw(c, NXSYM_PREV, mcx, mcy, 16.0f, th->text_primary, 0.0f);
    nxsym_draw(c, g_cc.media_playing ? NXSYM_PAUSE : NXSYM_PLAY, mcx + 34.0f, mcy, 16.0f, th->text_primary, 0.0f);
    nxsym_draw(c, NXSYM_NEXT, mcx + 68.0f, mcy, 16.0f, th->text_primary, 0.0f);

    /* Sliders */
    cc_slider(c, th, px + CCL_PAD, py + CCL_BRIGHT_Y, (float)g_cc.width - 2 * CCL_PAD, NXSYM_BRIGHTNESS,
              g_cc.brightness, (float)g_cc.brightness / 100.0f);
    cc_slider(c, th, px + CCL_PAD, py + CCL_VOL_Y, (float)g_cc.width - 2 * CCL_PAD, NXSYM_VOLUME,
              g_cc.volume, (float)g_cc.volume / 100.0f);

    /* Quick actions 2x2 */
    const cc_toggle_t *qa[4] = { &g_cc.focus_mode, &g_cc.dark_mode, &g_cc.screen_mirror, &g_cc.keyboard_brightness };
    const nxsym_t qsym[4] = { NXSYM_MOON, NXSYM_SUN, NXSYM_MAXIMIZE, NXSYM_KEYBOARD };
    const nxc_color_t qcol[4] = { th->warning, th->accent, NXC_RGB(0xAF, 0x52, 0xDE), th->accent };
    for (int i = 0; i < 4; i++) {
        float tx = px + CCL_PAD + (i % 2) * (tile_w + CC_GRID_GAP);
        float ty = py + CCL_QA_Y + (i / 2) * (CCL_QA_H + 8);
        cc_tile(c, th, nxc_rect(tx, ty, tile_w, CCL_QA_H), qsym[i], qa[i]->label, "", qa[i]->enabled, qcol[i],
                g_cc.hover_item == 4 + i, 0);
    }
    nxc_restore(c);
}
