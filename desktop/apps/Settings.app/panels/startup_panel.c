/*
 * NeolyxOS Settings - Startup Apps Panel
 * 
 * Manage applications that launch at boot
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* Startup apps state */
static startup_app_t g_startup_apps[32];
static int g_startup_count = 0;

/* Load startup apps from system */
static void load_startup_apps(void) {
    /* Get apps from system startup service */
    g_startup_count = startup_list(g_startup_apps, 32);
    
    /* If no registered startup apps, that's valid state */
    /* Do NOT add fake simulation data */
}

/* Create stats card */
static rx_view* create_stats_card(void) {
    rx_view* card = settings_card("Startup Impact");
    if (!card) return NULL;
    
    rx_view* time_row = hstack_new(8.0f);
    if (time_row) {
        rx_text_view* label = text_view_new("Last BIOS Time:");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(time_row, (rx_view*)label);
        }
        
        rx_text_view* time = settings_label("3.2 seconds", true);
        if (time) view_add_child(time_row, (rx_view*)time);
        
        view_add_child(card, time_row);
    }
    
    return card;
}

/* Create app list card */
static rx_view* create_app_list_card(void) {
    rx_view* card = settings_card("Startup Applications");
    if (!card) return NULL;
    
    /* Header */
    rx_view* header = hstack_new(8.0f);
    if (header) {
        rx_view* name_col = vstack_new(0);
        name_col->box.width = 200;
        rx_text_view* name = settings_label("Name", false);
        name->font_weight = 600;
        view_add_child(name_col, (rx_view*)name);
        view_add_child(header, name_col);
        
        rx_view* impact_col = vstack_new(0);
        impact_col->box.width = 100;
        rx_text_view* impact = settings_label("Impact", false);
        impact->font_weight = 600;
        view_add_child(impact_col, (rx_view*)impact);
        view_add_child(header, impact_col);
        
        rx_text_view* status = settings_label("Status", false);
        status->font_weight = 600;
        view_add_child(header, (rx_view*)status);
        
        view_add_child(card, header);
    }
    
    /* Divider */
    rx_view* div = hstack_new(0);
    div->box.background = (rx_color){ 60, 60, 62, 255 };
    div->box.height = 1;
    view_add_child(card, div);
    
    /* Apps */
    for (int i = 0; i < g_startup_count; i++) {
        rx_view* row = hstack_new(8.0f);
        if (!row) continue;
        
        /* Name */
        rx_view* name_col = vstack_new(0);
        name_col->box.width = 200;
        rx_text_view* name = text_view_new(g_startup_apps[i].name);
        name->color = NX_COLOR_TEXT;
        view_add_child(name_col, (rx_view*)name);
        view_add_child(row, name_col);
        
        /* Impact */
        rx_view* impact_col = vstack_new(0);
        impact_col->box.width = 100;
        rx_text_view* impact = settings_label(g_startup_apps[i].impact, true);
        /* Color code impact */
        if (strcmp(g_startup_apps[i].impact, "High") == 0) impact->color = NX_COLOR_ERROR;
        else if (strcmp(g_startup_apps[i].impact, "Medium") == 0) impact->color = NX_COLOR_WARNING;
        else impact->color = NX_COLOR_SUCCESS;
        view_add_child(impact_col, (rx_view*)impact);
        view_add_child(row, impact_col);
        
        /* Toggle (state-based on/off button) */
        rx_button_view* toggle = button_view_new(g_startup_apps[i].enabled ? "On" : "Off");
        if (toggle) {
            toggle->normal_color = g_startup_apps[i].enabled ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
            rx_view* toggle_view = (rx_view*)toggle;
            toggle_view->box.width = 60;
            toggle_view->box.height = 24;
            view_add_child(row, toggle_view);
        }
        
        view_add_child(card, row);
    }
    
    /* Add button */
    rx_view* add_row = hstack_new(0);
    rx_button_view* add_btn = button_view_new("+ Add Application");
    add_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
    view_add_child(add_row, (rx_view*)add_btn);
    view_add_child(card, add_row);
    
    return card;
}

/* Main panel creation */
rx_view* startup_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    load_startup_apps();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Startup Applications");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    rx_view* stats = create_stats_card();
    if (stats) view_add_child(panel, stats);
    
    rx_view* list = create_app_list_card();
    if (list) view_add_child(panel, list);
    
    println("[Startup] Panel created");
    return panel;
}
