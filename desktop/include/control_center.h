/*
 * NeolyxOS Control Center
 * 
 * Modern glassmorphic quick settings panel.
 * Sections: Network Grid, Quick Actions, Media, Sliders.
 * Uses nxevent for live state updates.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <stdint.h>

/* ============ Toggle State ============ */

typedef struct {
    int enabled;
    char label[24];
    char sublabel[32];
    uint32_t icon_color;       /* Color when active */
} cc_toggle_t;

/* ============ Control Center State ============ */

typedef struct {
    int visible;
    int hover_section;          /* 0=Network, 1=Security, 2=QuickActions, 3=Media, 4=Sliders */
    int hover_item;             /* Item index within section */
    
    /* Network Grid (2x2 top section) */
    cc_toggle_t wifi;
    cc_toggle_t bluetooth;
    cc_toggle_t airdrop;
    cc_toggle_t ethernet;
    
    /* Security Section (new) */
    cc_toggle_t firewall;
    int blocked_count;          /* Packets blocked since boot */
    int active_connections;     /* Active tracked connections */
    
    /* Quick Actions (2x2 grid) */
    cc_toggle_t focus_mode;
    cc_toggle_t dark_mode;
    cc_toggle_t screen_mirror;
    cc_toggle_t keyboard_brightness;
    
    /* Media / Now Playing */
    int media_playing;
    char media_title[64];
    char media_artist[32];
    uint32_t media_artwork_color;  /* Placeholder color for album art */
    
    /* Sliders */
    int brightness;             /* 0-100 */
    int volume;                 /* 0-100 */
    int mic_enabled;
    char output_device[32];
    
    /* Geometry (computed on render) */
    int x, y, width, height;
    uint32_t screen_width, screen_height;
    
    /* Animation */
    int slide_progress;         /* 0-100 for slide-in animation */
} control_center_state_t;

/* ============ Modern Color Palette ============ */

#define CC_BG_PRIMARY       0xE81E1E2E  /* Deep purple-gray glass (91% opacity) */
#define CC_BG_SECTION       0x8028283A  /* Section card background (50% opacity) */
#define CC_BG_SECTION_HOVER 0x9038384A  /* Hovered section card (56% opacity) */
#define CC_BORDER           0x30FFFFFF  /* Subtle border */

#define CC_ACTIVE_BLUE      0xFF007AFF  /* Active toggle blue */
#define CC_ACTIVE_GREEN     0xFF30D158  /* WiFi/AirDrop green */
#define CC_ACTIVE_ORANGE    0xFFFF9500  /* Focus mode orange */
#define CC_ACTIVE_PURPLE    0xFFAF52DE  /* Screen mirror purple */

#define CC_TOGGLE_OFF       0x60808080  /* Inactive toggle (38% opacity) */
#define CC_SLIDER_BG        0x60404050  /* Slider track background (38% opacity, darker) */
#define CC_SLIDER_FILL      0xFFFFFFFF  /* Slider filled portion */

#define CC_TEXT_PRIMARY     0xFFFFFFFF  /* White text */
#define CC_TEXT_SECONDARY   0x99FFFFFF  /* 60% white */
#define CC_TEXT_DIM         0x60FFFFFF  /* 38% white */

/* ============ Panel Dimensions ============ */

#define CC_WIDTH            340
#define CC_HEIGHT           520
#define CC_MARGIN           20
#define CC_SECTION_GAP      12
#define CC_CORNER_R         16
#define CC_TOGGLE_R         10
#define CC_GRID_GAP         10

/* ============ API Functions ============ */

void control_center_init(uint32_t screen_w, uint32_t screen_h);
void control_center_toggle(void);
void control_center_show(void);
void control_center_hide(void);
int control_center_is_visible(void);

void control_center_render(uint32_t *fb, uint32_t pitch);
int control_center_handle_input(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons);

/* Tick for animations */
void control_center_tick(void);

/* State updates (from nxevent or services) */
void control_center_set_wifi(int connected, const char *ssid);
void control_center_set_bluetooth(int enabled, const char *device_name);
void control_center_set_volume(int volume);
void control_center_set_brightness(int brightness);
void control_center_set_now_playing(const char *title, const char *artist, int playing);

/* Security state updates */
void control_center_set_firewall(int enabled, int blocked_count, int active_conns);

control_center_state_t* control_center_get_state(void);

/* Get requested Settings panel (for Settings app to use on launch) */
int control_center_get_pending_panel(void);

#endif /* CONTROL_CENTER_H */
