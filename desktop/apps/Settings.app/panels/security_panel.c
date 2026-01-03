/*
 * NeolyxOS Settings - Security Panel
 * 
 * Security settings: firewall, SIP, system lock.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "panel_common.h"

rx_view* security_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(SECTION_GAP);
    if (!panel) return NULL;
    
    /* Header */
    rx_text_view* header = panel_header("Security & Privacy");
    if (header) view_add_child(panel, (rx_view*)header);
    
    /* System Integrity Protection Card */
    rx_view* sip_card = settings_card("System Integrity Protection");
    if (sip_card) {
        rx_view* sip_status = hstack_new(8.0f);
        if (sip_status) {
            rx_text_view* sip_icon = text_view_new("[protected]");
            rx_text_view* sip_text = text_view_new("SIP is enabled");
            if (sip_icon) {
                sip_icon->color = NX_COLOR_SUCCESS;
                view_add_child(sip_status, (rx_view*)sip_icon);
            }
            if (sip_text) {
                sip_text->color = NX_COLOR_TEXT;
                view_add_child(sip_status, (rx_view*)sip_text);
            }
            view_add_child(sip_card, sip_status);
        }
        
        rx_text_view* sip_info = settings_label("Protects system files. Disable in Recovery Mode.", true);
        if (sip_info) view_add_child(sip_card, (rx_view*)sip_info);
        
        view_add_child(panel, sip_card);
    }
    
    /* Firewall Card */
    rx_view* fw_card = settings_card("Firewall");
    if (fw_card) {
        rx_view* fw_toggle = setting_toggle("Enable Firewall", true);
        if (fw_toggle) view_add_child(fw_card, fw_toggle);
        
        rx_view* fw_status = hstack_new(8.0f);
        if (fw_status) {
            rx_text_view* fw_rules = settings_label("Active Rules:", true);
            rx_text_view* fw_count = text_view_new("12");
            if (fw_rules) view_add_child(fw_status, (rx_view*)fw_rules);
            if (fw_count) {
                fw_count->color = NX_COLOR_PRIMARY;
                view_add_child(fw_status, (rx_view*)fw_count);
            }
            view_add_child(fw_card, fw_status);
        }
        
        rx_button_view* fw_btn = button_view_new("Configure Firewall Rules");
        if (fw_btn) view_add_child(fw_card, (rx_view*)fw_btn);
        
        view_add_child(panel, fw_card);
    }
    
    /* System Lock Card */
    rx_view* lock_card = settings_card("System Lock");
    if (lock_card) {
        rx_view* auto_lock = setting_toggle("Auto-lock on Sleep", true);
        if (auto_lock) view_add_child(lock_card, auto_lock);
        
        rx_view* lock_row = hstack_new(8.0f);
        if (lock_row) {
            rx_text_view* timeout_label = settings_label("Lock after:", true);
            rx_text_view* timeout_value = text_view_new("5 minutes");
            if (timeout_label) view_add_child(lock_row, (rx_view*)timeout_label);
            if (timeout_value) {
                timeout_value->color = NX_COLOR_TEXT;
                view_add_child(lock_row, (rx_view*)timeout_value);
            }
            view_add_child(lock_card, lock_row);
        }
        
        rx_button_view* change_pin = button_view_new("Change PIN");
        if (change_pin) view_add_child(lock_card, (rx_view*)change_pin);
        
        view_add_child(panel, lock_card);
    }
    
    /* Privacy Card */
    rx_view* privacy_card = settings_card("Privacy");
    if (privacy_card) {
        rx_view* telemetry = setting_toggle("Send Usage Statistics", false);
        if (telemetry) view_add_child(privacy_card, telemetry);
        
        rx_view* location = setting_toggle("Location Services", false);
        if (location) view_add_child(privacy_card, location);
        
        view_add_child(panel, privacy_card);
    }
    
    /* Security Scanner Card */
    rx_view* scanner_card = settings_card("Security Scanner");
    if (scanner_card) {
        rx_view* last_scan = hstack_new(8.0f);
        if (last_scan) {
            rx_text_view* scan_label = settings_label("Last Scan:", true);
            rx_text_view* scan_value = text_view_new("Never");
            if (scan_label) view_add_child(last_scan, (rx_view*)scan_label);
            if (scan_value) {
                scan_value->color = NX_COLOR_TEXT;
                view_add_child(last_scan, (rx_view*)scan_value);
            }
            view_add_child(scanner_card, last_scan);
        }
        
        rx_button_view* scan_btn = button_view_new("Scan Now");
        if (scan_btn) {
            scan_btn->normal_color = NX_COLOR_PRIMARY;
            view_add_child(scanner_card, (rx_view*)scan_btn);
        }
        
        view_add_child(panel, scanner_card);
    }
    
    return panel;
}
