/*
 * NeolyxOS Settings - Bootloader Panel
 * 
 * Manage boot entries and timeout
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

static bootloader_settings_t g_boot_settings;
static boot_entry_t g_boot_entries[8];
static int g_boot_count = 0;

static void load_boot_settings(void) {
    /* Get boot configuration from bootloader service */
    boot_get_settings(&g_boot_settings);
    
    /* Get boot entries from bootloader */
    g_boot_count = boot_get_entries(g_boot_entries, 8);
    
    /* If no entries returned, show actual empty state */
    /* Do NOT hardcode fake entries */
}

static rx_view* create_boot_config_card(void) {
    rx_view* card = settings_card("Configuration");
    if (!card) return NULL;
    
    /* Timeout */
    char timeout_str[16];
    snprintf(timeout_str, 16, "%d seconds", g_boot_settings.timeout_seconds);
    rx_view* timeout_row = settings_row("Menu Timeout", timeout_str);
    if (timeout_row) view_add_child(card, timeout_row);
    
    /* Menu toggle */
    rx_view* menu_row = hstack_new(8.0f);
    rx_text_view* menu_lbl = text_view_new("Always Show Menu");
    menu_lbl->color = NX_COLOR_TEXT;
    view_add_child(menu_row, (rx_view*)menu_lbl);
    rx_text_view* menu_status = settings_label(g_boot_settings.show_menu ? "On" : "Off", true);
    view_add_child(menu_row, (rx_view*)menu_status);
    view_add_child(card, menu_row);
    
    /* Secure Boot */
    rx_view* sb_row = hstack_new(8.0f);
    rx_text_view* sb_lbl = text_view_new("Secure Boot");
    sb_lbl->color = NX_COLOR_TEXT;
    view_add_child(sb_row, (rx_view*)sb_lbl);
    rx_text_view* sb_status = settings_label(g_boot_settings.secure_boot ? "Enabled" : "Disabled", true);
    sb_status->color = g_boot_settings.secure_boot ? NX_COLOR_SUCCESS : NX_COLOR_WARNING;
    view_add_child(sb_row, (rx_view*)sb_status);
    view_add_child(card, sb_row);
    
    return card;
}

static rx_view* create_entries_card(void) {
    rx_view* card = settings_card("Boot Entries");
    if (!card) return NULL;
    
    for (int i = 0; i < g_boot_count; i++) {
        rx_view* row = hstack_new(8.0f);
        
        rx_text_view* name = text_view_new(g_boot_entries[i].entry_name);
        name->color = NX_COLOR_TEXT;
        view_add_child(row, (rx_view*)name);
        
        if (g_boot_entries[i].is_default) {
            rx_text_view* def = settings_label("(Default)", true);
            def->color = NX_COLOR_PRIMARY;
            view_add_child(row, (rx_view*)def);
        }
        
        view_add_child(card, row);
    }
    
    rx_button_view* edit_btn = button_view_new("Edit Entries...");
    edit_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
    view_add_child(card, (rx_view*)edit_btn);
    
    return card;
}

rx_view* bootloader_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    load_boot_settings();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    rx_text_view* title = text_view_new("Bootloader");
    text_view_set_font_size(title, 28.0f);
    title->font_weight = 700;
    title->color = NX_COLOR_TEXT;
    view_add_child(panel, (rx_view*)title);
    
    view_add_child(panel, create_boot_config_card());
    view_add_child(panel, create_entries_card());
    
    println("[Bootloader] Panel created");
    return panel;
}
