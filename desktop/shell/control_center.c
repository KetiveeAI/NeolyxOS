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
#include "../include/nxglass.h"
/* Layout (relative to the panel) shared with control_center_handle_input.
 * Every block is separated by CC_GRID_GAP on a 16 px inset. */
#define CCL_PAD        16
#define CCL_TILE_H     52
#define CCL_NET_Y      16
#define CCL_SEC_Y      (CCL_NET_Y + 2 * (CCL_TILE_H + CC_GRID_GAP))             /* 140 */
#define CCL_SEC_H      56
#define CCL_MEDIA_Y    (CCL_SEC_Y + CCL_SEC_H + CC_GRID_GAP)                    /* 206 */
#define CCL_MEDIA_H    90
#define CCL_BRIGHT_Y   (CCL_MEDIA_Y + CCL_MEDIA_H + CC_GRID_GAP - 2)            /* 304 */
#define CCL_VOL_Y      (CCL_BRIGHT_Y + 40)                                      /* 344 */
#define CCL_QA_Y       (CCL_VOL_Y + 42)                                         /* 386 */
#define CCL_QA_H       44
#define CC_TILE_W      ((CC_WIDTH - 2 * CCL_PAD - CC_GRID_GAP) / 2)          /* 149 */
#define CCL_SLIDER_H   28

/* Brightness (0) / volume (1) slider track, absolute coordinates. */
static nxc_rect_t cc_slider_rect(float px, float py, int which) {
    float y = (float)(which == 0 ? CCL_BRIGHT_Y : CCL_VOL_Y) + 4.0f;
    return nxc_rect(px + CCL_PAD, py + y, (float)(CC_WIDTH - 2 * CCL_PAD), (float)CCL_SLIDER_H);
}

/* Inverse of nxglass_slider_large's fill: fill width = h + (w - h) * v. */
static int cc_slider_value_at(nxc_rect_t r, int x) {
    float span = r.w - r.h;
    if (!(span > 0.0f)) return 0;
    float v = ((float)x - (r.x + r.h * 0.5f)) / span;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (int)(v * 100.0f + 0.5f);
}

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
static int g_cc_drag = -1;          /* slider being dragged: 0 brightness, 1 volume */

/* ============ String Helpers ============ */

static void cc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


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

/* ============ Main Render ============ */

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
    
    int tile_w = CC_TILE_W;
    int tile_h = CCL_TILE_H;
    int right_x = CCL_PAD + tile_w + CC_GRID_GAP;
    int col = (rel_x >= right_x && rel_x < right_x + tile_w) ? 1
            : (rel_x >= CCL_PAD && rel_x < CCL_PAD + tile_w) ? 0 : -1;
    
    /* Network grid (items 0..3), firewall (8), quick actions (4..7) */
    if (rel_y >= CCL_NET_Y && rel_y < CCL_NET_Y + tile_h) {
        if (col >= 0) g_cc.hover_item = col;
    } else if (rel_y >= CCL_NET_Y + tile_h + CC_GRID_GAP && rel_y < CCL_NET_Y + 2 * tile_h + CC_GRID_GAP) {
        if (col >= 0) g_cc.hover_item = 2 + col;
    } else if (rel_y >= CCL_SEC_Y && rel_y < CCL_SEC_Y + CCL_SEC_H &&
               rel_x >= CCL_PAD && rel_x < g_cc.width - CCL_PAD) {
        g_cc.hover_item = 8;
    } else if (rel_y >= CCL_QA_Y && rel_y < CCL_QA_Y + 2 * CCL_QA_H + CC_GRID_GAP) {
        int row = (rel_y - CCL_QA_Y) / (CCL_QA_H + CC_GRID_GAP);
        if (row >= 0 && row < 2 && col >= 0) g_cc.hover_item = 4 + row * 2 + col;
    }
    
    /* Slider drags: a press on a track captures it until release */
    if (!(buttons & 0x01)) g_cc_drag = -1;
    if (buttons & 0x01) {
        if (!(prev_buttons & 0x01)) {
            for (int i = 0; i < 2; i++) {
                if (nxglass_hit(cc_slider_rect((float)g_cc.x, (float)g_cc.y, i), mouse_x, mouse_y)) g_cc_drag = i;
            }
        }
        if (g_cc_drag >= 0) {
            int v = cc_slider_value_at(cc_slider_rect((float)g_cc.x, (float)g_cc.y, g_cc_drag), mouse_x);
            if (g_cc_drag == 0) {
                g_cc.brightness = v;
            } else {
                g_cc.volume = v;
                volume_osd_show(g_cc.volume, 0);   /* Trigger volume OSD */
            }
            return 1;
        }
    }
    
    /* Handle toggle clicks */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        int on_right_tile = (rel_x >= right_x);
        int click_x_in_tile = on_right_tile ? (rel_x - right_x) : (rel_x - CCL_PAD);
        int on_chevron = (g_cc.hover_item >= 0 && g_cc.hover_item <= 3 &&
                          click_x_in_tile >= tile_w - 24);  /* Chevron: last 24px of network tiles */
        
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
            case 8:  /* Firewall: details live in Settings > Security */
                cc_open_settings_panel(CC_PANEL_SECURITY);
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

static nxc_status_t cc_draw_count(char *out, int cap, int value) {
    if (!out || cap < 2) return NXC_ERR_INVALID_ARG;
    char tmp[12];
    int t = 0, n = 0;
    if (value < 0) value = 0;
    if (value == 0) tmp[t++] = '0';
    while (value > 0 && t < 11) { tmp[t++] = (char)('0' + value % 10); value /= 10; }
    while (t > 0 && n < cap - 1) out[n++] = tmp[--t];
    out[n] = '\0';
    return NXC_OK;
}

void control_center_render(nxc_canvas_t *c) {
    if (!c || !g_cc_initialized) return;
    float dt = nxui_frame_dt();
    cc_anim = nxui_approach(cc_anim, g_cc.visible ? 1.0f : 0.0f, 0.06f, dt);
    if (cc_anim < 0.01f) return;
    const nxglass_tokens_t *t = nxglass_tokens();
    float ease = nxui_ease_out_cubic(cc_anim);
    /* Keep the Dark Mode tile in sync with appearance changed elsewhere. */
    g_cc.dark_mode.enabled = nxui_appearance() == NXUI_APPEARANCE_DARK;

    /* The panel is drawn at its resting rect and slid with a translation,
     * so the cached glass is reused on every frame of the animation. */
    nxc_rect_t panel = nxc_rect((float)g_cc.x, (float)g_cc.y, (float)g_cc.width, (float)g_cc.height);
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(ease * 255.0f));
    nxc_translate(c, 0.0f, -(1.0f - ease) * 12.0f);
    nxglass_panel(c, panel, t->radius_panel + 4.0f, NXGLASS_REGULAR, 2, 0);

    float px = panel.x, py = panel.y;
    float tile_w = (float)CC_TILE_W;

    /* Network 2x2 */
    const cc_toggle_t *net[4] = { &g_cc.wifi, &g_cc.bluetooth, &g_cc.airdrop, &g_cc.ethernet };
    const nxsym_t nsym[4] = { NXSYM_WIFI, NXSYM_BLUETOOTH, NXSYM_AIRPLANE, NXSYM_ETHERNET };
    const nxc_color_t ncol[4] = { t->info, t->info, t->accent, t->success };
    for (int i = 0; i < 4; i++) {
        float tx = px + CCL_PAD + (float)(i % 2) * (tile_w + CC_GRID_GAP);
        float ty = py + CCL_NET_Y + (float)(i / 2) * (CCL_TILE_H + CC_GRID_GAP);
        uint32_t st = (net[i]->enabled ? NXGLASS_SELECTED : 0) | (g_cc.hover_item == i ? NXGLASS_HOVER : 0);
        nxglass_tile(c, nxc_rect(tx, ty, tile_w, CCL_TILE_H), nsym[i], net[i]->label, net[i]->sublabel,
                     ncol[i], 1, st);
    }

    /* Security */
    nxc_rect_t sec = nxc_rect(px + CCL_PAD, py + CCL_SEC_Y, (float)g_cc.width - 2 * CCL_PAD, CCL_SEC_H);
    {
        char blocked[12], conns[12], info[64];
        cc_draw_count(blocked, (int)sizeof(blocked), g_cc.blocked_count);
        cc_draw_count(conns, (int)sizeof(conns), g_cc.active_connections);
        int n = 0;
        const char *parts[4] = { blocked, " blocked  \xC2\xB7  ", conns, " connections" };
        for (int k = 0; k < 4; k++)
            for (const char *q = parts[k]; *q && n < (int)sizeof(info) - 1; q++) info[n++] = *q;
        info[n] = '\0';
        nxglass_tile(c, sec, NXSYM_SHIELD, "Firewall", info, t->success, 0,
                     (g_cc.firewall.enabled ? NXGLASS_SELECTED : 0) | (g_cc.hover_item == 8 ? NXGLASS_HOVER : 0));
        const char *badge = g_cc.firewall.enabled ? "Active" : "Off";
        float bw = nxui_text_width(NXUI_TEXT_CAPTION, badge) + 16.0f;
        nxglass_badge(c, sec.x + sec.w - bw - 14.0f, sec.y + sec.h * 0.5f - 9.0f, badge,
                      g_cc.firewall.enabled ? t->success : t->label_secondary);
    }

    /* Now Playing */
    nxc_rect_t med = nxc_rect(px + CCL_PAD, py + CCL_MEDIA_Y, (float)g_cc.width - 2 * CCL_PAD, CCL_MEDIA_H);
    nxglass_card(c, med, t->radius_card);
    nxc_rect_t art = nxc_rect(med.x + 14.0f, med.y + 15.0f, 60.0f, 60.0f);
    nxc_paint_t ap = nxc_paint_linear(art.x, art.y, art.x + art.w, art.y + art.h,
                                      nxc_color_lerp(t->accent, NXC_RGB(0xFF, 0xFF, 0xFF), 0.15f),
                                      nxc_color_lerp(t->accent, NXC_RGB(0x6D, 0x28, 0xD9), 0.75f));
    nxc_draw_shadow_rrect(c, art, 10.0f, 4.0f, 0.0f, 2.0f, 0.0f, NXC_ARGB(70, 0, 0, 0x08));
    nxc_fill_rrect_paint(c, art, 10.0f, &ap);
    nxglass_rim(c, art, 10.0f, NXGLASS_ULTRATHIN);
    nxsym_draw(c, NXSYM_PLAY, art.x + 30.0f, art.y + 30.0f, 22.0f, NXC_ARGB(220, 255, 255, 255), 0.0f);
    float mx = art.x + art.w + 14.0f, mw = med.x + med.w - mx - 12.0f;
    nxui_text_in_rect(c, NXUI_TEXT_BODY_MEDIUM, nxc_rect(mx, med.y + 16.0f, mw, 18.0f), g_cc.media_title, t->label, NXC_ALIGN_LEFT);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(mx, med.y + 34.0f, mw, 16.0f),
                      g_cc.media_artist[0] ? g_cc.media_artist : "No media", t->label_secondary, NXC_ALIGN_LEFT);
    float mcy = med.y + 66.0f;
    nxglass_icon_button(c, nxc_rect(mx, mcy - 13.0f, 26.0f, 26.0f), NXSYM_PREV, 0);
    nxglass_icon_button(c, nxc_rect(mx + 36.0f, mcy - 15.0f, 30.0f, 30.0f), g_cc.media_playing ? NXSYM_PAUSE : NXSYM_PLAY,
                        NXGLASS_SELECTED);
    nxglass_icon_button(c, nxc_rect(mx + 76.0f, mcy - 13.0f, 26.0f, 26.0f), NXSYM_NEXT, 0);

    /* Sliders */
    nxglass_slider_large(c, cc_slider_rect(px, py, 0), (float)g_cc.brightness / 100.0f, NXSYM_BRIGHTNESS,
                         (float)g_cc.brightness / 100.0f, g_cc_drag == 0 ? NXGLASS_PRESSED : 0);
    nxglass_slider_large(c, cc_slider_rect(px, py, 1), (float)g_cc.volume / 100.0f, NXSYM_VOLUME,
                         (float)g_cc.volume / 100.0f, g_cc_drag == 1 ? NXGLASS_PRESSED : 0);

    /* Quick actions 2x2 */
    const cc_toggle_t *qa[4] = { &g_cc.focus_mode, &g_cc.dark_mode, &g_cc.screen_mirror, &g_cc.keyboard_brightness };
    const nxsym_t qsym[4] = { NXSYM_MOON, NXSYM_SUN, NXSYM_MAXIMIZE, NXSYM_KEYBOARD };
    const nxc_color_t qcol[4] = { NXC_RGB(0x6D, 0x28, 0xD9), t->accent, t->info, t->accent };
    for (int i = 0; i < 4; i++) {
        float tx = px + CCL_PAD + (float)(i % 2) * (tile_w + CC_GRID_GAP);
        float ty = py + CCL_QA_Y + (float)(i / 2) * (CCL_QA_H + CC_GRID_GAP);
        uint32_t st = (qa[i]->enabled ? NXGLASS_SELECTED : 0) | (g_cc.hover_item == 4 + i ? NXGLASS_HOVER : 0);
        nxglass_tile(c, nxc_rect(tx, ty, tile_w, CCL_QA_H), qsym[i], qa[i]->label, 0, qcol[i], 0, st);
    }
    nxc_restore(c);
}
