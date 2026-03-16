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
#include "../include/nxsyscall.h"
#include "../include/vm_detect.h"
#include "../include/nxi_render.h"
#include "../include/volume_osd.h"
#include "../include/theme_osd.h"
#include "../include/notification_center.h"
#include <stddef.h>

/* Config for security settings */
#include "nx_config.h"

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

void control_center_render(uint32_t *fb, uint32_t pitch) {
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

