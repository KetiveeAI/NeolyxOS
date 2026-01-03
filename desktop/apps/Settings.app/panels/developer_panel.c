/*
 * NeolyxOS Settings - Developer Panel
 * 
 * Developer options and kernel parameters
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* Developer state */
static struct {
    bool debug_mode;
    bool developer_tools;
    char kernel_params[512];
    bool usb_debugging;
    bool show_layout_bounds;
    bool show_fps;
    bool force_gpu_rendering;
    bool strict_mode;
    int animation_scale;  /* 0=off, 5=0.5x, 10=1x, 15=1.5x, 20=2x */
} g_dev_settings = {
    .debug_mode = false,
    .developer_tools = false,
    .kernel_params = "",
    .usb_debugging = false,
    .show_layout_bounds = false,
    .show_fps = false,
    .force_gpu_rendering = true,
    .strict_mode = false,
    .animation_scale = 10
};

static kernel_info_t g_kernel_info;

/* Load developer settings */
static void load_dev_settings(void) {
    kernel_get_info(&g_kernel_info);
}

/* Create kernel info card */
static rx_view* create_kernel_card(void) {
    rx_view* card = settings_card("Kernel Information");
    if (!card) return NULL;
    
    /* Kernel version */
    rx_view* ver_row = settings_row("Kernel Version", g_kernel_info.version);
    if (ver_row) view_add_child(card, ver_row);
    
    /* Release */
    rx_view* rel_row = settings_row("Release", g_kernel_info.release);
    if (rel_row) view_add_child(card, rel_row);
    
    /* Architecture */
    rx_view* arch_row = settings_row("Architecture", g_kernel_info.machine);
    if (arch_row) view_add_child(card, arch_row);
    
    /* Uptime */
    char uptime_str[64];
    uint64_t days = g_kernel_info.uptime_seconds / 86400;
    uint64_t hours = (g_kernel_info.uptime_seconds % 86400) / 3600;
    uint64_t mins = (g_kernel_info.uptime_seconds % 3600) / 60;
    snprintf(uptime_str, 64, "%lu days, %lu hours, %lu minutes", 
             (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
    rx_view* uptime_row = settings_row("Uptime", uptime_str);
    if (uptime_row) view_add_child(card, uptime_row);
    
    return card;
}

/* Create developer options card */
static rx_view* create_dev_options_card(void) {
    rx_view* card = settings_card("Developer Options");
    if (!card) return NULL;
    
    /* Warning */
    rx_text_view* warning = text_view_new("⚠️ These options are for developers only");
    if (warning) {
        warning->color = NX_COLOR_WARNING;
        text_view_set_font_size(warning, 12.0f);
        view_add_child(card, (rx_view*)warning);
    }
    
    /* Debug mode */
    rx_view* debug_row = hstack_new(8.0f);
    if (debug_row) {
        rx_text_view* label = text_view_new("Debug Mode");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(debug_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_dev_settings.debug_mode ? "On" : "Off", true);
        if (status) view_add_child(debug_row, (rx_view*)status);
        view_add_child(card, debug_row);
    }
    
    /* Developer tools */
    rx_view* tools_row = hstack_new(8.0f);
    if (tools_row) {
        rx_text_view* label = text_view_new("Developer Tools");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(tools_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_dev_settings.developer_tools ? "Enabled" : "Disabled", true);
        if (status) view_add_child(tools_row, (rx_view*)status);
        view_add_child(card, tools_row);
    }
    
    /* USB debugging */
    rx_view* usb_row = hstack_new(8.0f);
    if (usb_row) {
        rx_text_view* label = text_view_new("USB Debugging");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(usb_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_dev_settings.usb_debugging ? "On" : "Off", true);
        if (status) view_add_child(usb_row, (rx_view*)status);
        view_add_child(card, usb_row);
    }
    
    return card;
}

/* Create debug visualization card */
static rx_view* create_debug_viz_card(void) {
    rx_view* card = settings_card("Debug Visualization");
    if (!card) return NULL;
    
    /* Show layout bounds */
    rx_view* bounds_row = hstack_new(8.0f);
    if (bounds_row) {
        rx_text_view* label = text_view_new("Show Layout Bounds");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(bounds_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_dev_settings.show_layout_bounds ? "On" : "Off", true);
        if (status) view_add_child(bounds_row, (rx_view*)status);
        view_add_child(card, bounds_row);
    }
    
    /* Show FPS */
    rx_view* fps_row = hstack_new(8.0f);
    if (fps_row) {
        rx_text_view* label = text_view_new("Show FPS Counter");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(fps_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_dev_settings.show_fps ? "On" : "Off", true);
        if (status) view_add_child(fps_row, (rx_view*)status);
        view_add_child(card, fps_row);
    }
    
    /* Animation scale */
    const char* scales[] = { "Off", "0.5x", "1x", "1.5x", "2x" };
    int scale_idx = g_dev_settings.animation_scale / 5;
    if (scale_idx > 4) scale_idx = 2;
    rx_view* anim_row = settings_row("Animation Scale", scales[scale_idx]);
    if (anim_row) view_add_child(card, anim_row);
    
    return card;
}

/* Create kernel params card */
static rx_view* create_kernel_params_card(void) {
    rx_view* card = settings_card("Kernel Parameters");
    if (!card) return NULL;
    
    /* Warning */
    rx_text_view* warning = settings_label("Changes require reboot to take effect", true);
    if (warning) view_add_child(card, (rx_view*)warning);
    
    /* Current params */
    rx_text_view* params_label = settings_label("Boot Parameters:", false);
    if (params_label) view_add_child(card, (rx_view*)params_label);
    
    rx_view* params_box = vstack_new(0);
    if (params_box) {
        params_box->box.background = (rx_color){ 30, 30, 32, 255 };
        params_box->box.padding = insets_all(12.0f);
        params_box->box.corner_radius = corners_all(6.0f);
        
        rx_text_view* params = text_view_new(
            strlen(g_dev_settings.kernel_params) > 0 
                ? g_dev_settings.kernel_params 
                : "(none)");
        if (params) {
            params->color = NX_COLOR_TEXT_DIM;
            text_view_set_font_size(params, 12.0f);
            view_add_child(params_box, (rx_view*)params);
        }
        
        view_add_child(card, params_box);
    }
    
    /* Edit button */
    rx_button_view* edit_btn = button_view_new("Edit Kernel Parameters...");
    if (edit_btn) {
        edit_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)edit_btn);
    }
    
    return card;
}

/* Create modules card */
static rx_view* create_modules_card(void) {
    rx_view* card = settings_card("Kernel Modules");
    if (!card) return NULL;
    
    /* List some common modules */
    const char* modules[] = { "nxgfx", "nxaudio", "nxnet", "nxstor", "nxusb" };
    for (int i = 0; i < 5; i++) {
        rx_view* mod_row = hstack_new(8.0f);
        if (mod_row) {
            rx_text_view* name = text_view_new(modules[i]);
            if (name) {
                name->color = NX_COLOR_TEXT;
                view_add_child(mod_row, (rx_view*)name);
            }
            
            rx_text_view* status = settings_label("loaded", true);
            if (status) {
                status->color = NX_COLOR_SUCCESS;
                view_add_child(mod_row, (rx_view*)status);
            }
            
            view_add_child(card, mod_row);
        }
    }
    
    /* Manage modules button */
    rx_button_view* manage_btn = button_view_new("Manage Modules...");
    if (manage_btn) {
        manage_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)manage_btn);
    }
    
    return card;
}

/* Main panel creation */
rx_view* developer_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    load_dev_settings();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Developer Options");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Kernel info card */
    rx_view* kernel_card = create_kernel_card();
    if (kernel_card) view_add_child(panel, kernel_card);
    
    /* Developer options card */
    rx_view* dev_card = create_dev_options_card();
    if (dev_card) view_add_child(panel, dev_card);
    
    /* Debug visualization card */
    rx_view* debug_card = create_debug_viz_card();
    if (debug_card) view_add_child(panel, debug_card);
    
    /* Kernel params card */
    rx_view* params_card = create_kernel_params_card();
    if (params_card) view_add_child(panel, params_card);
    
    /* Modules card */
    rx_view* modules_card = create_modules_card();
    if (modules_card) view_add_child(panel, modules_card);
    
    println("[Developer] Panel created");
    return panel;
}
