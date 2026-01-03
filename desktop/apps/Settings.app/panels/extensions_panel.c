/*
 * NeolyxOS Settings - Extensions Panel
 * 
 * Manage system extensions and plugins
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>

typedef struct {
    char name[64];
    char desc[128];
    char version[16];
    bool enabled;
} extension_t;

static extension_t g_extensions[] = {
    { "Global Menu", "Moves menu bar to top of screen", "1.0", true },
    { "Window Snapping", "Advanced window tiling and snapping", "1.2", true },
    { "Night Light", "Blue light reduction filter", "0.9", false },
    { "Desktop Widgets", "Interactive widgets on desktop", "2.0", true }
};

static rx_view* create_ext_list_card(void) {
    rx_view* card = settings_card("Installed Extensions");
    if (!card) return NULL;
    
    int count = sizeof(g_extensions) / sizeof(g_extensions[0]);
    for (int i = 0; i < count; i++) {
        rx_view* row = hstack_new(12.0f);
        
        rx_view* info = vstack_new(2.0f);
        info->box.width = 300;
        
        rx_text_view* name = text_view_new(g_extensions[i].name);
        name->color = NX_COLOR_TEXT;
        view_add_child(info, (rx_view*)name);
        
        rx_text_view* desc = settings_label(g_extensions[i].desc, true);
        view_add_child(info, (rx_view*)desc);
        
        view_add_child(row, info);
        
        rx_button_view* btn = button_view_new(g_extensions[i].enabled ? "On" : "Off");
        btn->normal_color = g_extensions[i].enabled ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
        view_add_child(row, (rx_view*)btn);
        
        view_add_child(card, row);
    }
    
    return card;
}

rx_view* extensions_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    rx_text_view* title = text_view_new("Extensions");
    text_view_set_font_size(title, 28.0f);
    title->font_weight = 700;
    title->color = NX_COLOR_TEXT;
    view_add_child(panel, (rx_view*)title);
    
    /* Store link */
    rx_view* store_card = settings_card("Get More");
    rx_button_view* store_btn = button_view_new("Browse Extension Store");
    store_btn->normal_color = NX_COLOR_PRIMARY;
    view_add_child(store_card, (rx_view*)store_btn);
    view_add_child(panel, store_card);
    
    /* List */
    view_add_child(panel, create_ext_list_card());
    
    println("[Extensions] Panel created");
    return panel;
}
