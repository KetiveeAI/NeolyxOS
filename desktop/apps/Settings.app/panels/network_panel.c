/*
 * Settings - Network Panel
 * WiFi, Ethernet, VPN, Bluetooth management
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <nxnetwork.h>
#include <string.h>
#include <stdio.h>

/* ============ Panel State ============ */

static struct {
    /* WiFi */
    bool wifi_enabled;
    wifi_network_t wifi_networks[32];
    int wifi_count;
    int selected_wifi;
    
    /* Ethernet */
    nxnetif_t eth_interface;
    bool eth_connected;
    
    /* VPN */
    vpn_connection_t vpns[16];
    int vpn_count;
    
    /* Bluetooth */
    bool bt_enabled;
    bt_device_t bt_devices[32];
    int bt_count;
    
    /* View state */
    int current_tab;  /* 0=WiFi, 1=Ethernet, 2=VPN, 3=Bluetooth */
} state = {0};

/* ============ Tab Drawing ============ */

static void draw_tabs(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    const char *tabs[] = {"Wi-Fi", "Ethernet", "VPN", "Bluetooth"};
    
    for (int i = 0; i < 4; i++) {
        bool selected = (state.current_tab == i);
        uint32_t bg = selected ? 0x007AFF : 0x2A2A2A;
        
        panel_fill_rect(x, y, 80, 30, bg);
        panel_draw_text(x + 15, y + 8, tabs[i], 0xFFFFFF);
        x += 85;
    }
}

/* ============ WiFi Section ============ */

static void draw_wifi_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    /* WiFi toggle */
    panel_draw_label(x, y, "Wi-Fi");
    panel_draw_toggle(x + 100, y, state.wifi_enabled);
    y += 40;
    
    if (!state.wifi_enabled) {
        panel_draw_text(x, y, "Wi-Fi is turned off", 0x888888);
        return;
    }
    
    /* Current network */
    wifi_network_t *current = nxwifi_current();
    if (current && current->is_connected) {
        panel_draw_subheader(x, y, "Current Network");
        y += 30;
        
        panel_fill_rect(x, y, panel->width - 60, 60, 0x2A2A2A);
        panel_draw_icon(x + 10, y + 15, "wifi", 30);
        panel_draw_text(x + 50, y + 10, current->ssid, 0xFFFFFF);
        
        char signal_str[32];
        snprintf(signal_str, sizeof(signal_str), "Signal: %d dBm", current->signal_strength);
        panel_draw_text(x + 50, y + 35, signal_str, 0x888888);
        
        panel_draw_badge(x + panel->width - 150, y + 20, "Connected", 0x34C759);
        y += 80;
    }
    
    /* Available networks */
    panel_draw_subheader(x, y, "Available Networks");
    y += 30;
    
    for (int i = 0; i < state.wifi_count && i < 8; i++) {
        wifi_network_t *net = &state.wifi_networks[i];
        if (net->is_connected) continue;
        
        bool selected = (i == state.selected_wifi);
        uint32_t bg = selected ? 0x0A84FF : 0x2A2A2A;
        
        panel_fill_rect(x, y, panel->width - 60, 40, bg);
        
        /* Signal icon */
        const char *signal_icon = "wifi_weak";
        if (net->signal_strength > -50) signal_icon = "wifi_full";
        else if (net->signal_strength > -70) signal_icon = "wifi_good";
        panel_draw_icon(x + 10, y + 8, signal_icon, 24);
        
        /* SSID */
        panel_draw_text(x + 45, y + 12, net->ssid, 0xFFFFFF);
        
        /* Security */
        if (net->security != WIFI_SECURITY_OPEN) {
            panel_draw_icon(x + panel->width - 100, y + 10, "lock", 20);
        }
        
        /* Known network indicator */
        if (net->is_known) {
            panel_draw_text(x + panel->width - 70, y + 12, "Saved", 0x888888);
        }
        
        y += 44;
    }
}

/* ============ Ethernet Section ============ */

static void draw_ethernet_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    /* Status */
    panel_draw_subheader(x, y, "Ethernet");
    y += 35;
    
    if (!state.eth_connected) {
        panel_draw_text(x, y, "No Ethernet cable connected", 0x888888);
        return;
    }
    
    /* Connection info */
    panel_fill_rect(x, y, panel->width - 60, 120, 0x2A2A2A);
    
    int info_y = y + 10;
    panel_draw_label(x + 10, info_y, "Status:");
    panel_draw_text(x + 120, info_y, "Connected", 0x34C759);
    info_y += 25;
    
    panel_draw_label(x + 10, info_y, "IP Address:");
    panel_draw_text(x + 120, info_y, state.eth_interface.ip_address, 0xCCCCCC);
    info_y += 25;
    
    panel_draw_label(x + 10, info_y, "Link Speed:");
    char speed_str[16];
    snprintf(speed_str, sizeof(speed_str), "%d Mbps", state.eth_interface.link_speed);
    panel_draw_text(x + 120, info_y, speed_str, 0xCCCCCC);
    info_y += 25;
    
    panel_draw_label(x + 10, info_y, "MAC:");
    panel_draw_text(x + 120, info_y, state.eth_interface.hardware_addr, 0xCCCCCC);
    y += 140;
    
    /* Configure button */
    panel_draw_button(x, y, "Configure IPv4...", 0x007AFF);
    panel_draw_button(x + 150, y, "Advanced...", 0x48484A);
}

/* ============ VPN Section ============ */

static void draw_vpn_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_subheader(x, y, "VPN");
    y += 35;
    
    if (state.vpn_count == 0) {
        panel_draw_text(x, y, "No VPN configurations", 0x888888);
        y += 30;
        panel_draw_button(x, y, "Add VPN Configuration...", 0x007AFF);
        return;
    }
    
    for (int i = 0; i < state.vpn_count; i++) {
        vpn_connection_t *vpn = &state.vpns[i];
        
        panel_fill_rect(x, y, panel->width - 60, 50, 0x2A2A2A);
        
        panel_draw_icon(x + 10, y + 12, "vpn", 26);
        panel_draw_text(x + 50, y + 8, vpn->name, 0xFFFFFF);
        panel_draw_text(x + 50, y + 28, vpn->server, 0x888888);
        
        if (vpn->status == NET_STATUS_CONNECTED) {
            panel_draw_badge(x + panel->width - 150, y + 15, "Connected", 0x34C759);
        } else {
            panel_draw_button(x + panel->width - 120, y + 10, "Connect", 0x007AFF);
        }
        
        y += 55;
    }
    
    y += 10;
    panel_draw_button(x, y, "Add VPN Configuration...", 0x48484A);
}

/* ============ Bluetooth Section ============ */

static void draw_bluetooth_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    /* Bluetooth toggle */
    panel_draw_label(x, y, "Bluetooth");
    panel_draw_toggle(x + 100, y, state.bt_enabled);
    y += 40;
    
    if (!state.bt_enabled) {
        panel_draw_text(x, y, "Bluetooth is turned off", 0x888888);
        return;
    }
    
    /* Paired devices */
    panel_draw_subheader(x, y, "My Devices");
    y += 30;
    
    int paired_count = 0;
    for (int i = 0; i < state.bt_count; i++) {
        bt_device_t *dev = &state.bt_devices[i];
        if (!dev->paired) continue;
        
        panel_fill_rect(x, y, panel->width - 60, 50, 0x2A2A2A);
        
        /* Device icon */
        const char *icon = "bluetooth";
        switch (dev->type) {
            case BT_DEVICE_HEADPHONES:
            case BT_DEVICE_EARBUDS: icon = "headphones"; break;
            case BT_DEVICE_SPEAKER: icon = "speaker"; break;
            case BT_DEVICE_KEYBOARD: icon = "keyboard"; break;
            case BT_DEVICE_MOUSE: icon = "mouse"; break;
            default: break;
        }
        panel_draw_icon(x + 10, y + 12, icon, 26);
        
        panel_draw_text(x + 50, y + 8, dev->name, 0xFFFFFF);
        
        if (dev->battery_level >= 0) {
            char bat_str[16];
            snprintf(bat_str, sizeof(bat_str), "Battery: %d%%", dev->battery_level);
            panel_draw_text(x + 50, y + 28, bat_str, 0x888888);
        }
        
        if (dev->connected) {
            panel_draw_badge(x + panel->width - 150, y + 15, "Connected", 0x34C759);
        } else {
            panel_draw_text(x + panel->width - 130, y + 18, "Not Connected", 0x888888);
        }
        
        y += 55;
        paired_count++;
    }
    
    if (paired_count == 0) {
        panel_draw_text(x, y, "No paired devices", 0x888888);
        y += 30;
    }
    
    /* Nearby devices */
    y += 20;
    panel_draw_subheader(x, y, "Nearby Devices");
    y += 30;
    
    for (int i = 0; i < state.bt_count; i++) {
        bt_device_t *dev = &state.bt_devices[i];
        if (dev->paired) continue;
        
        panel_fill_rect(x, y, panel->width - 60, 40, 0x2A2A2A);
        panel_draw_icon(x + 10, y + 8, "bluetooth", 24);
        panel_draw_text(x + 45, y + 12, dev->name, 0xFFFFFF);
        panel_draw_button(x + panel->width - 120, y + 5, "Pair", 0x007AFF);
        
        y += 44;
    }
}

/* ============ Panel Callbacks ============ */

void network_panel_init(settings_panel_t *panel) {
    panel->title = "Network";
    panel->icon = "network";
    
    state.wifi_enabled = nxwifi_is_enabled();
    state.wifi_count = nxwifi_scan(state.wifi_networks, 32);
    
    state.bt_enabled = nxbluetooth_is_enabled();
    state.bt_count = nxbluetooth_paired_devices(state.bt_devices, 16);
    state.bt_count += nxbluetooth_scan(&state.bt_devices[state.bt_count], 16);
    
    state.vpn_count = nxvpn_list(state.vpns, 16);
    
    nxnetif_t *primary = nxnetwork_primary();
    if (primary && primary->type == NETIF_ETHERNET) {
        state.eth_interface = *primary;
        state.eth_connected = (primary->status == NET_STATUS_CONNECTED);
    }
}

void network_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    
    /* Header */
    panel_draw_header(panel->x + 20, y, "Network");
    y += 45;
    
    /* Tabs */
    draw_tabs(panel, y);
    y += 50;
    
    /* Separator */
    panel_draw_line(panel->x + 20, y, panel->x + panel->width - 20, y, 0x3A3A3A);
    y += 20;
    
    /* Tab content */
    switch (state.current_tab) {
        case 0: draw_wifi_section(panel, y); break;
        case 1: draw_ethernet_section(panel, y); break;
        case 2: draw_vpn_section(panel, y); break;
        case 3: draw_bluetooth_section(panel, y); break;
    }
}

void network_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (event->type == EVT_CLICK) {
        /* Tab selection */
        int tab_y = panel->y + 65;
        int tab_x = panel->x + 20;
        
        for (int i = 0; i < 4; i++) {
            if (event->y >= tab_y && event->y <= tab_y + 30 &&
                event->x >= tab_x && event->x <= tab_x + 80) {
                state.current_tab = i;
                panel->needs_redraw = true;
                break;
            }
            tab_x += 85;
        }
    }
}

void network_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
}
