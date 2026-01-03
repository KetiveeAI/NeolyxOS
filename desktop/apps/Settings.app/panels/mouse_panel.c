/*
 * NeolyxOS Settings - Mouse & Cursor Panel
 * 
 * Cursor customization:
 * - Tracking speed (1-10)
 * - Acceleration (off/low/medium/high)
 * - Cursor size (small/normal/large/extra)
 * - Cursor theme selection
 * - Double-click speed
 * - Natural scrolling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "panel_common.h"
#include <stdio.h>

/* ============================================================================
 * Mouse & Cursor Settings State
 * ============================================================================ */

static struct {
    int tracking_speed;       /* 1-10, default 5 */
    int acceleration_level;   /* 0=off, 1=low, 2=medium, 3=high */
    int cursor_size;          /* 0=small, 1=normal, 2=large, 3=extra */
    int double_click_ms;      /* 200-900ms */
    bool natural_scrolling;
    bool smooth_tracking;
    int scroll_speed;         /* 1-10 */
    int scroll_direction;     /* 0=normal, 1=natural */
    char cursor_theme[32];    /* Theme name */
} g_mouse_settings = {
    .tracking_speed = 5,
    .acceleration_level = 2,
    .cursor_size = 1,
    .double_click_ms = 500,
    .natural_scrolling = false,
    .smooth_tracking = true,
    .scroll_speed = 5,
    .scroll_direction = 0,
    .cursor_theme = "NeolyxOS"
};

/* Acceleration level names */
static const char* acceleration_names[] = {
    "Off", "Low", "Medium", "High"
};

/* Cursor size names */
static const char* cursor_size_names[] = {
    "Small", "Normal", "Large", "Extra Large"
};

/* Available cursor themes */
static const char* cursor_themes[] = {
    "NeolyxOS",         /* Default purple gradient */
    "NeolyxOS Dark",    /* Black outline version */
    "NeolyxOS Contrast",/* Purple + black outline */
    "Classic Arrow",
    "Minimal",
    "Large Print"
};
static const int theme_count = 6;

/* ============================================================================
 * Settings Change Callbacks
 * ============================================================================ */

/* Apply tracking speed to cursor service */
static void apply_tracking_speed(int speed) {
    g_mouse_settings.tracking_speed = speed;
    /* TODO: Call cursor_set_speed(speed) */
    println("[Mouse] Tracking speed set to %d", speed);
}

/* Apply acceleration level */
static void apply_acceleration(int level) {
    g_mouse_settings.acceleration_level = level;
    /* TODO: Call cursor_set_acceleration(level) */
    println("[Mouse] Acceleration set to %s", acceleration_names[level]);
}

/* Apply cursor size */
static void apply_cursor_size(int size) {
    g_mouse_settings.cursor_size = size;
    /* TODO: Update cursor_config_t size */
    println("[Mouse] Cursor size set to %s", cursor_size_names[size]);
}

/* Apply cursor theme */
static void apply_cursor_theme(const char* theme) {
    snprintf(g_mouse_settings.cursor_theme, 32, "%s", theme);
    /* TODO: cursor_load_theme(theme) */
    println("[Mouse] Cursor theme set to %s", theme);
}

/* ============================================================================
 * Panel UI Cards
 * ============================================================================ */

/* Create tracking speed card with slider */
static rx_view* create_tracking_card(void) {
    rx_view* card = settings_card("Tracking Speed");
    if (!card) return NULL;
    
    /* Description */
    rx_text_view* desc = text_view_new(
        "Adjust how fast the cursor moves relative to mouse/trackpad movement."
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Slow --- (slider) --- Fast */
    rx_view* speed_row = hstack_new(12.0f);
    if (speed_row) {
        rx_text_view* slow = text_view_new("Slow");
        if (slow) {
            slow->color = NX_COLOR_TEXT_DIM;
            view_add_child(speed_row, (rx_view*)slow);
        }
        
        /* Display current value */
        char speed_text[16];
        snprintf(speed_text, sizeof(speed_text), "[ %d ]", g_mouse_settings.tracking_speed);
        rx_text_view* val = text_view_new(speed_text);
        if (val) {
            val->color = NX_COLOR_PRIMARY;
            view_add_child(speed_row, (rx_view*)val);
        }
        
        rx_text_view* fast = text_view_new("Fast");
        if (fast) {
            fast->color = NX_COLOR_TEXT_DIM;
            view_add_child(speed_row, (rx_view*)fast);
        }
        
        view_add_child(card, speed_row);
    }
    
    /* - and + buttons for adjustment */
    rx_view* btn_row = hstack_new(16.0f);
    if (btn_row) {
        rx_button_view* minus = button_view_new("−");
        if (minus) {
            minus->normal_color = NX_COLOR_SECONDARY;
            view_add_child(btn_row, (rx_view*)minus);
        }
        
        rx_button_view* plus = button_view_new("+");
        if (plus) {
            plus->normal_color = NX_COLOR_SECONDARY;
            view_add_child(btn_row, (rx_view*)plus);
        }
        
        view_add_child(card, btn_row);
    }
    
    return card;
}

/* Create acceleration settings card */
static rx_view* create_acceleration_card(void) {
    rx_view* card = settings_card("Pointer Acceleration");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Acceleration makes the cursor move faster when you move the mouse quickly."
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Current selection */
    rx_view* level_row = settings_row("Level", 
        acceleration_names[g_mouse_settings.acceleration_level]);
    if (level_row) view_add_child(card, level_row);
    
    /* Options */
    for (int i = 0; i < 4; i++) {
        rx_view* opt_row = hstack_new(8.0f);
        if (opt_row) {
            /* Radio indicator */
            const char* indicator = (i == g_mouse_settings.acceleration_level) ? "●" : "○";
            rx_text_view* radio = text_view_new(indicator);
            if (radio) {
                radio->color = (i == g_mouse_settings.acceleration_level) ? 
                    NX_COLOR_PRIMARY : NX_COLOR_TEXT_DIM;
                view_add_child(opt_row, (rx_view*)radio);
            }
            
            rx_text_view* label = text_view_new(acceleration_names[i]);
            if (label) {
                label->color = NX_COLOR_TEXT;
                view_add_child(opt_row, (rx_view*)label);
            }
            
            view_add_child(card, opt_row);
        }
    }
    
    return card;
}

/* Create cursor size selector card */
static rx_view* create_size_card(void) {
    rx_view* card = settings_card("Cursor Size");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Choose cursor size for better visibility."
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Current selection */
    rx_view* size_row = settings_row("Size", 
        cursor_size_names[g_mouse_settings.cursor_size]);
    if (size_row) view_add_child(card, size_row);
    
    /* Size options as buttons */
    rx_view* btn_row = hstack_new(8.0f);
    if (btn_row) {
        for (int i = 0; i < 4; i++) {
            rx_button_view* btn = button_view_new(cursor_size_names[i]);
            if (btn) {
                if (i == g_mouse_settings.cursor_size) {
                    btn->normal_color = NX_COLOR_PRIMARY;
                } else {
                    btn->normal_color = NX_COLOR_SECONDARY;
                }
                view_add_child(btn_row, (rx_view*)btn);
            }
        }
        view_add_child(card, btn_row);
    }
    
    return card;
}

/* Create cursor theme selector card */
static rx_view* create_theme_card(void) {
    rx_view* card = settings_card("Cursor Theme");
    if (!card) return NULL;
    
    rx_text_view* desc = text_view_new(
        "Choose a cursor style. Custom themes can be installed in /Library/Cursors/"
    );
    if (desc) {
        text_view_set_font_size(desc, 12.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(card, (rx_view*)desc);
    }
    
    /* Current theme */
    rx_view* current_row = settings_row("Current", g_mouse_settings.cursor_theme);
    if (current_row) view_add_child(card, current_row);
    
    /* Theme list */
    rx_view* list = settings_section("AVAILABLE THEMES");
    if (list) view_add_child(card, list);
    
    for (int i = 0; i < theme_count; i++) {
        rx_view* theme_row = hstack_new(8.0f);
        if (theme_row) {
            /* Selected indicator */
            bool selected = (strcmp(cursor_themes[i], g_mouse_settings.cursor_theme) == 0);
            const char* indicator = selected ? "✓" : " ";
            rx_text_view* check = text_view_new(indicator);
            if (check) {
                check->color = NX_COLOR_PRIMARY;
                view_add_child(theme_row, (rx_view*)check);
            }
            
            rx_text_view* name = text_view_new(cursor_themes[i]);
            if (name) {
                name->color = selected ? NX_COLOR_PRIMARY : NX_COLOR_TEXT;
                view_add_child(theme_row, (rx_view*)name);
            }
            
            view_add_child(card, theme_row);
        }
    }
    
    return card;
}

/* Create double-click and scrolling settings card */
static rx_view* create_behavior_card(void) {
    rx_view* card = settings_card("Clicking & Scrolling");
    if (!card) return NULL;
    
    /* Double-click speed */
    char dbl_str[32];
    snprintf(dbl_str, sizeof(dbl_str), "%d ms", g_mouse_settings.double_click_ms);
    rx_view* dbl_row = settings_row("Double-Click Speed", dbl_str);
    if (dbl_row) view_add_child(card, dbl_row);
    
    /* Natural scrolling toggle */
    rx_view* natural_row = setting_toggle("Natural Scrolling", g_mouse_settings.natural_scrolling);
    if (natural_row) view_add_child(card, natural_row);
    
    /* Smooth tracking */
    rx_view* smooth_row = setting_toggle("Smooth Tracking", g_mouse_settings.smooth_tracking);
    if (smooth_row) view_add_child(card, smooth_row);
    
    /* Scroll speed */
    char scroll_str[16];
    snprintf(scroll_str, sizeof(scroll_str), "%d", g_mouse_settings.scroll_speed);
    rx_view* scroll_row = settings_row("Scroll Speed", scroll_str);
    if (scroll_row) view_add_child(card, scroll_row);
    
    return card;
}

/* ============================================================================
 * Main Panel Creation
 * ============================================================================ */

rx_view* mouse_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = panel_header("Mouse & Cursor");
    if (title) view_add_child(panel, (rx_view*)title);
    
    /* Subtitle */
    rx_text_view* desc = text_view_new(
        "Customize cursor appearance, tracking speed, and mouse behavior."
    );
    if (desc) {
        text_view_set_font_size(desc, 14.0f);
        desc->color = NX_COLOR_TEXT_DIM;
        view_add_child(panel, (rx_view*)desc);
    }
    
    /* Cards */
    rx_view* tracking = create_tracking_card();
    if (tracking) view_add_child(panel, tracking);
    
    rx_view* accel = create_acceleration_card();
    if (accel) view_add_child(panel, accel);
    
    rx_view* size = create_size_card();
    if (size) view_add_child(panel, size);
    
    rx_view* theme = create_theme_card();
    if (theme) view_add_child(panel, theme);
    
    rx_view* behavior = create_behavior_card();
    if (behavior) view_add_child(panel, behavior);
    
    println("[Mouse] Cursor settings panel created");
    return panel;
}
