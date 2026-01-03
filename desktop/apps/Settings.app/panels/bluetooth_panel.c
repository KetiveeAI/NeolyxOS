/*
 * NeolyxOS Settings - Bluetooth Panel
 * 
 * Bluetooth device pairing and management
 * Uses real kernel bluetooth API and NLC config
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>
#include <string.h>

/* Kernel APIs */
#include "drivers/bluetooth.h"
#include "config/nlc_config.h"

/* Panel-local state */
static struct {
    bool                enabled;
    bool                discoverable;
    char                device_name[64];
    bt_controller_t    *controller;
    bt_device_t         paired_devices[16];
    int                 paired_count;
    bt_device_t         nearby_devices[16];
    int                 nearby_count;
} g_bluetooth = {0};

/* Load bluetooth state from kernel and NLC config */
static void load_bluetooth_state(void) {
    /* Load saved preferences from bluetooth.nlc */
    static nlc_config_t bt_cfg = {0};
    if (nlc_load(&bt_cfg, "/System/Config/bluetooth.nlc") == 0) {
        g_bluetooth.enabled = nlc_get_bool(&bt_cfg, "bluetooth", "enabled", 1);
        g_bluetooth.discoverable = nlc_get_bool(&bt_cfg, "bluetooth", "discoverable", 0);
        const char *name = nlc_get(&bt_cfg, "bluetooth", "device_name");
        if (name) {
            strncpy(g_bluetooth.device_name, name, 63);
        } else {
            strcpy(g_bluetooth.device_name, "NeolyxOS");
        }
    } else {
        /* Defaults if no config */
        g_bluetooth.enabled = true;
        g_bluetooth.discoverable = false;
        strcpy(g_bluetooth.device_name, "NeolyxOS");
    }
    
    /* Get kernel bluetooth controller */
    int ctrl_count = bluetooth_controller_count();
    if (ctrl_count > 0) {
        g_bluetooth.controller = bluetooth_get_controller(0);
    }
    
    if (g_bluetooth.controller) {
        /* Sync with actual hardware state */
        g_bluetooth.enabled = g_bluetooth.controller->powered;
        g_bluetooth.discoverable = g_bluetooth.controller->discoverable;
        
        /* Get paired devices from kernel */
        g_bluetooth.paired_count = bluetooth_get_paired(
            g_bluetooth.controller,
            g_bluetooth.paired_devices,
            16
        );
        
        /* Get nearby devices if scanning */
        if (g_bluetooth.controller->scanning) {
            g_bluetooth.nearby_count = bluetooth_get_devices(
                g_bluetooth.controller,
                g_bluetooth.nearby_devices,
                16
            );
        }
    }
}

/* Save bluetooth settings to NLC config */
static void save_bluetooth_settings(void) {
    static nlc_config_t bt_cfg = {0};
    strncpy(bt_cfg.filepath, "/System/Config/bluetooth.nlc", 255);
    
    nlc_set(&bt_cfg, "bluetooth", "enabled", g_bluetooth.enabled ? "true" : "false");
    nlc_set(&bt_cfg, "bluetooth", "discoverable", g_bluetooth.discoverable ? "true" : "false");
    nlc_set(&bt_cfg, "bluetooth", "device_name", g_bluetooth.device_name);
    
    nlc_save(&bt_cfg);
}

/* Create bluetooth status card */
static rx_view* create_status_card(void) {
    rx_view* card = settings_card("Bluetooth");
    if (!card) return NULL;
    
    /* Enable toggle */
    rx_view* enable_row = hstack_new(8.0f);
    if (enable_row) {
        rx_text_view* label = text_view_new("Bluetooth");
        if (label) {
            text_view_set_font_size(label, 18.0f);
            label->font_weight = 500;
            label->color = NX_COLOR_TEXT;
            view_add_child(enable_row, (rx_view*)label);
        }
        
        rx_text_view* status = settings_label(g_bluetooth.enabled ? "On" : "Off", true);
        if (status) {
            status->color = g_bluetooth.enabled ? NX_COLOR_SUCCESS : NX_COLOR_TEXT_DIM;
            view_add_child(enable_row, (rx_view*)status);
        }
        
        view_add_child(card, enable_row);
    }
    
    if (!g_bluetooth.enabled) {
        rx_text_view* hint = settings_label("Turn on Bluetooth to connect devices", true);
        if (hint) view_add_child(card, (rx_view*)hint);
        return card;
    }
    
    /* Device name */
    rx_view* name_row = settings_row("Device Name", g_bluetooth.device_name);
    if (name_row) view_add_child(card, name_row);
    
    /* Discoverable */
    rx_view* discover_row = hstack_new(8.0f);
    if (discover_row) {
        rx_text_view* label = text_view_new("Discoverable");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(discover_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_bluetooth.discoverable ? "On" : "Off", true);
        if (status) view_add_child(discover_row, (rx_view*)status);
        view_add_child(card, discover_row);
    }
    
    return card;
}

/* Create paired devices card */
static rx_view* create_paired_card(void) {
    rx_view* card = settings_card("My Devices");
    if (!card) return NULL;
    
    if (g_bluetooth.paired_count == 0) {
        rx_text_view* empty = settings_label("No paired devices", true);
        if (empty) view_add_child(card, (rx_view*)empty);
        return card;
    }
    
    for (int i = 0; i < g_bluetooth.paired_count; i++) {
        bt_device_t* dev = &g_bluetooth.paired_devices[i];
        
        rx_view* device_row = hstack_new(12.0f);
        if (device_row) {
            /* Name and status */
            rx_view* info = vstack_new(2.0f);
            if (info) {
                rx_text_view* name = text_view_new(dev->name);
                if (name) {
                    name->color = NX_COLOR_TEXT;
                    view_add_child(info, (rx_view*)name);
                }
                
                const char* status_str = (dev->state == BT_STATE_CONNECTED) ? 
                                         "Connected" : "Not Connected";
                rx_text_view* status = settings_label(status_str, true);
                if (status) {
                    if (dev->state == BT_STATE_CONNECTED) status->color = NX_COLOR_SUCCESS;
                    view_add_child(info, (rx_view*)status);
                }
                
                view_add_child(device_row, info);
            }
            
            /* Battery if available */
            if (dev->has_battery && dev->battery_level <= 100) {
                char bat_str[16];
                snprintf(bat_str, 16, "%d%%", dev->battery_level);
                rx_text_view* bat = settings_label(bat_str, true);
                if (bat) {
                    if (dev->battery_level < 20) bat->color = NX_COLOR_ERROR;
                    view_add_child(device_row, (rx_view*)bat);
                }
            }
            
            view_add_child(card, device_row);
        }
    }
    
    return card;
}

/* Create nearby devices card */
static rx_view* create_nearby_card(void) {
    rx_view* card = settings_card("Other Devices");
    if (!card) return NULL;
    
    if (g_bluetooth.nearby_count == 0) {
        rx_text_view* scanning = settings_label("Scanning for devices...", true);
        if (scanning) view_add_child(card, (rx_view*)scanning);
    } else {
        for (int i = 0; i < g_bluetooth.nearby_count; i++) {
            bt_device_t* dev = &g_bluetooth.nearby_devices[i];
            
            rx_view* device_row = hstack_new(12.0f);
            if (device_row) {
                rx_text_view* name = text_view_new(dev->name);
                if (name) {
                    name->color = NX_COLOR_TEXT;
                    view_add_child(device_row, (rx_view*)name);
                }
                
                rx_button_view* pair_btn = button_view_new("Connect");
                if (pair_btn) {
                    pair_btn->normal_color = NX_COLOR_PRIMARY;
                    view_add_child(device_row, (rx_view*)pair_btn);
                }
                
                view_add_child(card, device_row);
            }
        }
    }
    
    return card;
}

/* Main panel creation */
rx_view* bluetooth_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    load_bluetooth_state();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Bluetooth");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Status card */
    rx_view* status_card = create_status_card();
    if (status_card) view_add_child(panel, status_card);
    
    if (g_bluetooth.enabled) {
        /* Paired devices card */
        rx_view* paired_card = create_paired_card();
        if (paired_card) view_add_child(panel, paired_card);
        
        /* Nearby devices card */
        rx_view* nearby_card = create_nearby_card();
        if (nearby_card) view_add_child(panel, nearby_card);
    }
    
    println("[Bluetooth] Panel created - using kernel bluetooth.h + nlc_config.h");
    return panel;
}
