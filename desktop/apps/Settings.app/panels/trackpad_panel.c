/*
 * NeolyxOS Settings - Trackpad & Touch Panel
 * 
 * Settings for touch screen and trackpad gestures:
 * - 2 fingers: Scroll
 * - 3 fingers: Volume/Brightness control
 * - 4 fingers: App switching
 * - Edge swipes: App switcher, Control Center
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Gesture Settings
 * ============================================================================ */

static struct {
    bool natural_scroll;
    int scroll_speed;
    int gesture_sensitivity;
    
    /* 2-finger actions */
    int two_finger_scroll;
    bool two_finger_tap_rightclick;
    bool pinch_zoom;
    
    /* 3-finger actions */
    int three_up;       /* 0=volume up, 1=brightness up, 2=custom */
    int three_down;     /* 0=volume down, 1=brightness down */
    int three_left;
    int three_right;
    int three_tap;      /* 0=play/pause, 1=mute, 2=middle-click */
    
    /* 4-finger actions */
    int four_up;        /* 0=mission control, 1=show desktop */
    int four_down;      /* 0=app expose, 1=notification center */
    int four_left;      /* 0=switch app left */
    int four_right;     /* 0=switch app right */
    int four_tap;       /* 0=show desktop */
    
    /* Edge swipes (touch screen only) */
    bool edge_enabled;
    int edge_left;      /* 0=back, 1=none */
    int edge_right;     /* 0=control center, 1=none */
    int edge_top;       /* 0=notifications, 1=none */
    int edge_bottom;    /* 0=app drawer, 1=app switcher */
    
    /* App switcher (Android-style) */
    bool app_switcher_enabled;
    bool swipe_up_close;
    bool swipe_hold_float;
    
    /* Hardware */
    bool tap_to_click;
    bool palm_rejection;
    bool force_touch;
} g_touch_settings = {
    .natural_scroll = true,
    .scroll_speed = 5,
    .gesture_sensitivity = 5,
    .two_finger_scroll = 0,
    .two_finger_tap_rightclick = true,
    .pinch_zoom = true,
    .three_up = 0,
    .three_down = 0,
    .three_left = 1,
    .three_right = 1,
    .three_tap = 0,
    .four_up = 0,
    .four_down = 0,
    .four_left = 0,
    .four_right = 0,
    .four_tap = 0,
    .edge_enabled = true,
    .edge_left = 0,
    .edge_right = 0,
    .edge_top = 0,
    .edge_bottom = 1,
    .app_switcher_enabled = true,
    .swipe_up_close = true,
    .swipe_hold_float = true,
    .tap_to_click = true,
    .palm_rejection = true,
    .force_touch = false
};

/* ============================================================================
 * Panel UI Creation
 * ============================================================================ */

/* Create 2-finger gestures card */
static rx_view* create_two_finger_card(void) {
    rx_view* card = settings_card("Two-Finger Gestures");
    if (!card) return NULL;
    
    /* Scroll */
    rx_view* scroll_row = hstack_new(8.0f);
    if (scroll_row) {
        rx_text_view* label = text_view_new("Two-Finger Scroll");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(scroll_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Enabled", true);
        if (status) view_add_child(scroll_row, (rx_view*)status);
        view_add_child(card, scroll_row);
    }
    
    /* Natural scrolling */
    rx_view* natural_row = hstack_new(8.0f);
    if (natural_row) {
        rx_text_view* label = text_view_new("Natural Scrolling");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(natural_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_touch_settings.natural_scroll ? "On" : "Off", true);
        if (status) view_add_child(natural_row, (rx_view*)status);
        view_add_child(card, natural_row);
    }
    
    /* Scroll speed */
    rx_view* speed_row = settings_row("Scroll Speed", "Medium");
    if (speed_row) view_add_child(card, speed_row);
    
    /* Two-finger tap = right click */
    rx_view* tap_row = hstack_new(8.0f);
    if (tap_row) {
        rx_text_view* label = text_view_new("Two-Finger Tap");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(tap_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Right-Click", true);
        if (status) view_add_child(tap_row, (rx_view*)status);
        view_add_child(card, tap_row);
    }
    
    /* Pinch to zoom */
    rx_view* pinch_row = hstack_new(8.0f);
    if (pinch_row) {
        rx_text_view* label = text_view_new("Pinch to Zoom");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(pinch_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_touch_settings.pinch_zoom ? "On" : "Off", true);
        if (status) view_add_child(pinch_row, (rx_view*)status);
        view_add_child(card, pinch_row);
    }
    
    return card;
}

/* Create 3-finger gestures card */
static rx_view* create_three_finger_card(void) {
    rx_view* card = settings_card("Three-Finger Gestures");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new("Control system settings with three-finger swipes.");
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Swipe up */
    rx_view* up_row = settings_row("Swipe Up", "Volume Up");
    if (up_row) view_add_child(card, up_row);
    
    /* Swipe down */
    rx_view* down_row = settings_row("Swipe Down", "Volume Down");
    if (down_row) view_add_child(card, down_row);
    
    /* Swipe left */
    rx_view* left_row = settings_row("Swipe Left", "Brightness Down");
    if (left_row) view_add_child(card, left_row);
    
    /* Swipe right */
    rx_view* right_row = settings_row("Swipe Right", "Brightness Up");
    if (right_row) view_add_child(card, right_row);
    
    /* Three-finger tap */
    rx_view* tap_row = settings_row("Tap (3 fingers)", "Play/Pause");
    if (tap_row) view_add_child(card, tap_row);
    
    return card;
}

/* Create 4-finger gestures card */
static rx_view* create_four_finger_card(void) {
    rx_view* card = settings_card("Four-Finger Gestures");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new("Navigate between apps and windows with four fingers.");
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Swipe up */
    rx_view* up_row = settings_row("Swipe Up", "Mission Control");
    if (up_row) view_add_child(card, up_row);
    
    /* Swipe down */
    rx_view* down_row = settings_row("Swipe Down", "App Exposé");
    if (down_row) view_add_child(card, down_row);
    
    /* Swipe left */
    rx_view* left_row = settings_row("Swipe Left", "Previous App");
    if (left_row) view_add_child(card, left_row);
    
    /* Swipe right */
    rx_view* right_row = settings_row("Swipe Right", "Next App");
    if (right_row) view_add_child(card, right_row);
    
    /* Four-finger tap */
    rx_view* tap_row = settings_row("Tap (4 fingers)", "Show Desktop");
    if (tap_row) view_add_child(card, tap_row);
    
    return card;
}

/* Create edge swipe card (touch screen only) */
static rx_view* create_edge_swipe_card(void) {
    rx_view* card = settings_card("Edge Swipes (Touch Screen)");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Swipe from screen edges to access system features. Like Android and iOS."
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Enable toggle */
    rx_view* enable_row = hstack_new(8.0f);
    if (enable_row) {
        rx_text_view* label = text_view_new("Enable Edge Gestures");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(enable_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_touch_settings.edge_enabled ? "On" : "Off", true);
        if (status) view_add_child(enable_row, (rx_view*)status);
        view_add_child(card, enable_row);
    }
    
    /* Edge actions */
    rx_view* left_row = settings_row("Left Edge", "← Back Navigation");
    if (left_row) view_add_child(card, left_row);
    
    rx_view* right_row = settings_row("Right Edge", "Control Center");
    if (right_row) view_add_child(card, right_row);
    
    rx_view* top_row = settings_row("Top Edge", "Notifications");
    if (top_row) view_add_child(card, top_row);
    
    rx_view* bottom_row = settings_row("Bottom Edge", "App Switcher");
    if (bottom_row) view_add_child(card, bottom_row);
    
    return card;
}

/* Create app switcher card */
static rx_view* create_app_switcher_card(void) {
    rx_view* card = settings_card("App Switcher (Touch Screen)");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Single-finger swipe from bottom edge to switch apps. Like Android gesture navigation."
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Enable */
    rx_view* enable_row = hstack_new(8.0f);
    if (enable_row) {
        rx_text_view* label = text_view_new("App Switcher Gestures");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(enable_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Enabled", true);
        if (status) view_add_child(enable_row, (rx_view*)status);
        view_add_child(card, enable_row);
    }
    
    rx_view* section = settings_section("SWIPE ACTIONS");
    if (section) view_add_child(card, section);
    
    /* Swipe up to close */
    rx_view* close_row = hstack_new(8.0f);
    if (close_row) {
        rx_text_view* label = text_view_new("Swipe Up on App Card");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(close_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Close App", true);
        if (status) view_add_child(close_row, (rx_view*)status);
        view_add_child(card, close_row);
    }
    
    /* Swipe left/right */
    rx_view* switch_row = hstack_new(8.0f);
    if (switch_row) {
        rx_text_view* label = text_view_new("Swipe Left/Right");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(switch_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Switch Apps", true);
        if (status) view_add_child(switch_row, (rx_view*)status);
        view_add_child(card, switch_row);
    }
    
    /* Hold to float */
    rx_view* float_row = hstack_new(8.0f);
    if (float_row) {
        rx_text_view* label = text_view_new("Hold + Swipe Up");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(float_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label("Floating Window", true);
        if (status) view_add_child(float_row, (rx_view*)status);
        view_add_child(card, float_row);
    }
    
    return card;
}

/* Create hardware settings card */
static rx_view* create_hardware_card(void) {
    rx_view* card = settings_card("Hardware");
    if (!card) return NULL;
    
    /* Tap to click */
    rx_view* tap_row = hstack_new(8.0f);
    if (tap_row) {
        rx_text_view* label = text_view_new("Tap to Click");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(tap_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_touch_settings.tap_to_click ? "On" : "Off", true);
        if (status) view_add_child(tap_row, (rx_view*)status);
        view_add_child(card, tap_row);
    }
    
    /* Palm rejection */
    rx_view* palm_row = hstack_new(8.0f);
    if (palm_row) {
        rx_text_view* label = text_view_new("Palm Rejection");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(palm_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_touch_settings.palm_rejection ? "On" : "Off", true);
        if (status) view_add_child(palm_row, (rx_view*)status);
        view_add_child(card, palm_row);
    }
    
    /* Sensitivity */
    rx_view* sens_row = settings_row("Gesture Sensitivity", "Medium");
    if (sens_row) view_add_child(card, sens_row);
    
    return card;
}

/* ============================================================================
 * Main Panel
 * ============================================================================ */

rx_view* trackpad_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Trackpad & Touch");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Device info */
    rx_text_view* desc = text_view_new("Configure multi-touch gestures for trackpad and touch screen.");
    if (desc) {
        text_view_set_font_size(desc, 14.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(panel, (rx_view*)desc);
    }
    
    /* Cards */
    rx_view* two_card = create_two_finger_card();
    if (two_card) view_add_child(panel, two_card);
    
    rx_view* three_card = create_three_finger_card();
    if (three_card) view_add_child(panel, three_card);
    
    rx_view* four_card = create_four_finger_card();
    if (four_card) view_add_child(panel, four_card);
    
    /* Touch screen specific (only if has touch screen) */
    rx_view* edge_card = create_edge_swipe_card();
    if (edge_card) view_add_child(panel, edge_card);
    
    rx_view* app_card = create_app_switcher_card();
    if (app_card) view_add_child(panel, app_card);
    
    rx_view* hw_card = create_hardware_card();
    if (hw_card) view_add_child(panel, hw_card);
    
    println("[Trackpad] Touch gesture panel created");
    return panel;
}
