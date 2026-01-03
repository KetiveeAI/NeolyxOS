/*
 * NeolyxOS Settings - Power Panel
 * 
 * Battery and power management settings
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>

/* Power state */
static power_settings_t g_power = {
    .on_battery = false,
    .battery_percent = 100,
    .time_remaining = 0,
    .charging = false,
    .ac_connected = true,
    .power_mode = POWER_MODE_BALANCED,
    .sleep_after_minutes = 15,
    .screen_off_minutes = 5,
    .auto_brightness = true,
    .lid_close_sleep = true,
    .power_button_sleep = true
};

/* Load power settings */
static void load_power_settings(void) {
    power_get_settings(&g_power);
    power_get_battery_status(&g_power.battery_percent, &g_power.charging, &g_power.time_remaining);
}

/* Create battery status card */
static rx_view* create_battery_card(void) {
    rx_view* card = settings_card("Battery Status");
    if (!card) return NULL;
    
    /* Battery level */
    rx_view* level_row = hstack_new(12.0f);
    if (level_row) {
        /* Battery icon */
        rx_view* battery_icon = vstack_new(0);
        if (battery_icon) {
            battery_icon->box.background = g_power.battery_percent > 20 
                ? NX_COLOR_SUCCESS : NX_COLOR_ERROR;
            battery_icon->box.width = 40;
            battery_icon->box.height = 20;
            battery_icon->box.corner_radius = corners_all(4.0f);
            view_add_child(level_row, battery_icon);
        }
        
        char level_str[32];
        if (g_power.charging) {
            snprintf(level_str, 32, "%d%% - Charging", g_power.battery_percent);
        } else if (g_power.ac_connected) {
            snprintf(level_str, 32, "%d%% - Plugged In", g_power.battery_percent);
        } else {
            snprintf(level_str, 32, "%d%%", g_power.battery_percent);
        }
        
        rx_text_view* level = text_view_new(level_str);
        if (level) {
            text_view_set_font_size(level, 20.0f);
            level->font_weight = 600;
            level->color = NX_COLOR_TEXT;
            view_add_child(level_row, (rx_view*)level);
        }
        
        view_add_child(card, level_row);
    }
    
    /* Time remaining */
    if (!g_power.ac_connected && g_power.time_remaining > 0) {
        char time_str[32];
        int hours = g_power.time_remaining / 60;
        int mins = g_power.time_remaining % 60;
        if (hours > 0) {
            snprintf(time_str, 32, "%d hr %d min remaining", hours, mins);
        } else {
            snprintf(time_str, 32, "%d minutes remaining", mins);
        }
        rx_text_view* time_label = settings_label(time_str, true);
        if (time_label) view_add_child(card, (rx_view*)time_label);
    }
    
    /* Power source */
    rx_view* source_row = settings_row("Power Source", 
        g_power.ac_connected ? "AC Power" : "Battery");
    if (source_row) view_add_child(card, source_row);
    
    return card;
}

/* Create power mode card */
static rx_view* create_power_mode_card(void) {
    rx_view* card = settings_card("Power Mode");
    if (!card) return NULL;
    
    /* Mode selector */
    rx_view* modes_row = hstack_new(8.0f);
    if (modes_row) {
        const char* mode_names[] = { "Performance", "Balanced", "Power Saver" };
        power_mode_t modes[] = { POWER_MODE_PERFORMANCE, POWER_MODE_BALANCED, POWER_MODE_POWERSAVER };
        
        for (int i = 0; i < 3; i++) {
            rx_view* mode_box = vstack_new(8.0f);
            if (mode_box) {
                mode_box->box.background = (g_power.power_mode == modes[i]) 
                    ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
                mode_box->box.padding = insets_all(12.0f);
                mode_box->box.corner_radius = corners_all(8.0f);
                
                rx_text_view* label = text_view_new(mode_names[i]);
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    text_view_set_font_size(label, 13.0f);
                    view_add_child(mode_box, (rx_view*)label);
                }
                
                view_add_child(modes_row, mode_box);
            }
        }
        view_add_child(card, modes_row);
    }
    
    /* Mode description */
    const char* descriptions[] = {
        "Maximum performance, higher power usage",
        "Balance between performance and battery life",
        "Extended battery life, reduced performance"
    };
    rx_text_view* desc = settings_label(descriptions[g_power.power_mode], true);
    if (desc) view_add_child(card, (rx_view*)desc);
    
    return card;
}

/* Create sleep settings card */
static rx_view* create_sleep_card(void) {
    rx_view* card = settings_card("Sleep & Display");
    if (!card) return NULL;
    
    /* Turn display off */
    char screen_str[32];
    if (g_power.screen_off_minutes == 0) {
        snprintf(screen_str, 32, "Never");
    } else {
        snprintf(screen_str, 32, "%d minutes", g_power.screen_off_minutes);
    }
    rx_view* screen_row = settings_row("Turn display off after", screen_str);
    if (screen_row) view_add_child(card, screen_row);
    
    /* Sleep after */
    char sleep_str[32];
    if (g_power.sleep_after_minutes == 0) {
        snprintf(sleep_str, 32, "Never");
    } else if (g_power.sleep_after_minutes >= 60) {
        snprintf(sleep_str, 32, "%d hour%s", 
                 g_power.sleep_after_minutes / 60,
                 g_power.sleep_after_minutes >= 120 ? "s" : "");
    } else {
        snprintf(sleep_str, 32, "%d minutes", g_power.sleep_after_minutes);
    }
    rx_view* sleep_row = settings_row("Put computer to sleep after", sleep_str);
    if (sleep_row) view_add_child(card, sleep_row);
    
    /* Lid close action */
    rx_view* lid_row = hstack_new(8.0f);
    if (lid_row) {
        rx_text_view* label = text_view_new("Sleep when lid closed");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(lid_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_power.lid_close_sleep ? "On" : "Off", true);
        if (status) view_add_child(lid_row, (rx_view*)status);
        view_add_child(card, lid_row);
    }
    
    /* Power button action */
    rx_view* btn_row = settings_row("Power button action", "Sleep");
    if (btn_row) view_add_child(card, btn_row);
    
    return card;
}

/* Create schedule card */
static rx_view* create_schedule_card(void) {
    rx_view* card = settings_card("Schedule");
    if (!card) return NULL;
    
    /* Schedule shutdown */
    rx_button_view* shutdown_btn = button_view_new("Schedule Shutdown...");
    if (shutdown_btn) {
        shutdown_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)shutdown_btn);
    }
    
    /* Schedule restart */
    rx_button_view* restart_btn = button_view_new("Schedule Restart...");
    if (restart_btn) {
        restart_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)restart_btn);
    }
    
    return card;
}

/* Main panel creation */
rx_view* power_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    load_power_settings();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Power");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Battery card */
    rx_view* battery_card = create_battery_card();
    if (battery_card) view_add_child(panel, battery_card);
    
    /* Power mode card */
    rx_view* mode_card = create_power_mode_card();
    if (mode_card) view_add_child(panel, mode_card);
    
    /* Sleep card */
    rx_view* sleep_card = create_sleep_card();
    if (sleep_card) view_add_child(panel, sleep_card);
    
    /* Schedule card */
    rx_view* schedule_card = create_schedule_card();
    if (schedule_card) view_add_child(panel, schedule_card);
    
    println("[Power] Panel created");
    return panel;
}
