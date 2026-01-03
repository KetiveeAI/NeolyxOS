/*
 * NeolyxOS Settings - Enhanced Display Panel
 * 
 * Advanced display settings with real driver integration
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* Display state */
static display_settings_t g_display_settings = {
    .brightness = 75,
    .night_mode = false,
    .night_warmth = 50,
    .hdr_enabled = false,
    .vsync_enabled = true,
    .color_profile = "sRGB",
    .scaling = 100,
    .orientation = 0
};

static display_info_t g_displays[4];
static int g_display_count = 0;

/* Load current display settings from driver */
static void load_display_settings(void) {
    g_display_count = display_get_count();
    if (g_display_count > 4) g_display_count = 4;
    
    for (int i = 0; i < g_display_count; i++) {
        display_get_info(i, &g_displays[i]);
    }
    
    display_get_settings(&g_display_settings);
}

/* Create resolution picker */
static rx_view* create_resolution_card(void) {
    rx_view* card = settings_card("Resolution & Display");
    if (!card) return NULL;
    
    /* Display selector (for multi-monitor) */
    if (g_display_count > 1) {
        rx_view* section = settings_section("SELECT DISPLAY");
        if (section) view_add_child(card, section);
        
        for (int i = 0; i < g_display_count; i++) {
            char display_label[128];
            snprintf(display_label, 128, "%s%s", 
                     g_displays[i].name,
                     g_displays[i].primary ? " (Primary)" : "");
            
            rx_button_view* btn = button_view_new(display_label);
            if (btn) {
                btn->normal_color = g_displays[i].primary ? NX_COLOR_PRIMARY : NX_COLOR_SURFACE;
                view_add_child(card, (rx_view*)btn);
            }
        }
    }
    
    /* Current resolution */
    char res_str[32];
    if (g_display_count > 0) {
        snprintf(res_str, 32, "%dx%d @ %dHz", 
                 g_displays[0].current_width,
                 g_displays[0].current_height,
                 g_displays[0].refresh_rate);
    } else {
        snprintf(res_str, 32, "1920x1080 @ 60Hz");
    }
    rx_view* res_row = settings_row("Resolution", res_str);
    if (res_row) view_add_child(card, res_row);
    
    /* Resolution picker button */
    rx_button_view* res_btn = button_view_new("Change Resolution...");
    if (res_btn) {
        res_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)res_btn);
    }
    
    /* DPI info */
    char dpi_str[16];
    snprintf(dpi_str, 16, "%d", g_display_count > 0 ? g_displays[0].dpi : 96);
    rx_view* dpi_row = settings_row("DPI", dpi_str);
    if (dpi_row) view_add_child(card, dpi_row);
    
    return card;
}

/* Create brightness & night mode card */
static rx_view* create_brightness_card(void) {
    rx_view* card = settings_card("Brightness & Color");
    if (!card) return NULL;
    
    /* Brightness slider row */
    rx_view* bright_row = hstack_new(12.0f);
    if (bright_row) {
        rx_text_view* label = text_view_new("Brightness");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(bright_row, (rx_view*)label);
        }
        
        char bright_val[16];
        snprintf(bright_val, 16, "%d%%", g_display_settings.brightness);
        rx_text_view* value = settings_label(bright_val, true);
        if (value) view_add_child(bright_row, (rx_view*)value);
        
        view_add_child(card, bright_row);
    }
    
    /* Slider placeholder - actual slider would use view */
    rx_view* slider_bg = hstack_new(0);
    if (slider_bg) {
        slider_bg->box.background = (rx_color){ 60, 60, 62, 255 };
        slider_bg->box.height = 8;
        slider_bg->box.corner_radius = corners_all(4.0f);
        view_add_child(card, slider_bg);
    }
    
    /* Auto-brightness toggle */
    rx_view* auto_row = hstack_new(8.0f);
    if (auto_row) {
        rx_text_view* label = text_view_new("Auto-brightness");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(auto_row, (rx_view*)label);
        }
        view_add_child(card, auto_row);
    }
    
    /* Night mode section */
    rx_view* night_section = settings_section("NIGHT MODE");
    if (night_section) view_add_child(card, night_section);
    
    rx_view* night_row = hstack_new(8.0f);
    if (night_row) {
        rx_text_view* label = text_view_new("Night Shift");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(night_row, (rx_view*)label);
        }
        
        rx_text_view* status = settings_label(g_display_settings.night_mode ? "On" : "Off", true);
        if (status) view_add_child(night_row, (rx_view*)status);
        
        view_add_child(card, night_row);
    }
    
    /* Color warmth slider (when night mode on) */
    if (g_display_settings.night_mode) {
        rx_view* warmth_row = settings_row("Color Warmth", "Less Warm                More Warm");
        if (warmth_row) view_add_child(card, warmth_row);
    }
    
    /* Schedule */
    rx_view* schedule_row = settings_row("Schedule", "Sunset to Sunrise");
    if (schedule_row) view_add_child(card, schedule_row);
    
    return card;
}

/* Create advanced display settings card */
static rx_view* create_advanced_card(void) {
    rx_view* card = settings_card("Advanced Display");
    if (!card) return NULL;
    
    /* Scaling */
    char scale_str[16];
    snprintf(scale_str, 16, "%d%%", g_display_settings.scaling);
    rx_view* scale_row = settings_row("Display Scaling", scale_str);
    if (scale_row) view_add_child(card, scale_row);
    
    /* Scale options */
    rx_view* scale_options = hstack_new(8.0f);
    if (scale_options) {
        const int scales[] = { 100, 125, 150, 175, 200 };
        for (int i = 0; i < 5; i++) {
            char label[8];
            snprintf(label, 8, "%d%%", scales[i]);
            rx_button_view* btn = button_view_new(label);
            if (btn) {
                btn->normal_color = (scales[i] == g_display_settings.scaling) 
                    ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
                view_add_child(scale_options, (rx_view*)btn);
            }
        }
        view_add_child(card, scale_options);
    }
    
    /* Orientation */
    const char* orientations[] = { "Landscape", "Portrait", "Landscape (flipped)", "Portrait (flipped)" };
    int orient_idx = g_display_settings.orientation / 90;
    if (orient_idx < 0 || orient_idx > 3) orient_idx = 0;
    rx_view* orient_row = settings_row("Orientation", orientations[orient_idx]);
    if (orient_row) view_add_child(card, orient_row);
    
    /* Color profile */
    rx_view* profile_row = settings_row("Color Profile", g_display_settings.color_profile);
    if (profile_row) view_add_child(card, profile_row);
    
    rx_button_view* profile_btn = button_view_new("Manage Color Profiles...");
    if (profile_btn) {
        profile_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)profile_btn);
    }
    
    /* HDR */
    rx_view* hdr_row = hstack_new(8.0f);
    if (hdr_row) {
        rx_text_view* label = text_view_new("HDR Mode");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(hdr_row, (rx_view*)label);
        }
        
        rx_text_view* status = settings_label(g_display_settings.hdr_enabled ? "Enabled" : "Disabled", true);
        if (status) view_add_child(hdr_row, (rx_view*)status);
        
        view_add_child(card, hdr_row);
    }
    
    /* VSync */
    rx_view* vsync_row = hstack_new(8.0f);
    if (vsync_row) {
        rx_text_view* label = text_view_new("VSync");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(vsync_row, (rx_view*)label);
        }
        
        rx_text_view* status = settings_label(g_display_settings.vsync_enabled ? "On" : "Off", true);
        if (status) view_add_child(vsync_row, (rx_view*)status);
        
        view_add_child(card, vsync_row);
    }
    
    return card;
}

/* Create arrangement card for multi-monitor */
static rx_view* create_arrangement_card(void) {
    if (g_display_count < 2) return NULL;
    
    rx_view* card = settings_card("Display Arrangement");
    if (!card) return NULL;
    
    rx_text_view* hint = settings_label("Drag displays to rearrange. Click to select.", true);
    if (hint) view_add_child(card, (rx_view*)hint);
    
    /* Visual display arrangement placeholder */
    rx_view* arrangement = hstack_new(16.0f);
    if (arrangement) {
        for (int i = 0; i < g_display_count; i++) {
            rx_view* display_box = vstack_new(4.0f);
            if (display_box) {
                display_box->box.background = g_displays[i].primary 
                    ? NX_COLOR_PRIMARY : (rx_color){ 80, 80, 82, 255 };
                display_box->box.width = 120;
                display_box->box.height = 80;
                display_box->box.corner_radius = corners_all(8.0f);
                display_box->box.padding = insets_all(8.0f);
                
                char num[4];
                snprintf(num, 4, "%d", i + 1);
                rx_text_view* label = text_view_new(num);
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    text_view_set_font_size(label, 20.0f);
                    view_add_child(display_box, (rx_view*)label);
                }
                
                view_add_child(arrangement, display_box);
            }
        }
        view_add_child(card, arrangement);
    }
    
    /* Mirror displays option */
    rx_view* mirror_row = hstack_new(8.0f);
    if (mirror_row) {
        rx_text_view* label = text_view_new("Mirror Displays");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(mirror_row, (rx_view*)label);
        }
        view_add_child(card, mirror_row);
    }
    
    return card;
}

/* Main panel creation - replaces old display_panel.c */
rx_view* display_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    /* Load settings from driver */
    load_display_settings();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Display");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Resolution & display card */
    rx_view* res_card = create_resolution_card();
    if (res_card) view_add_child(panel, res_card);
    
    /* Brightness & night mode card */
    rx_view* bright_card = create_brightness_card();
    if (bright_card) view_add_child(panel, bright_card);
    
    /* Advanced card */
    rx_view* adv_card = create_advanced_card();
    if (adv_card) view_add_child(panel, adv_card);
    
    /* ═══════════════════════════════════════════════════════════════════════
     * ADAPTIVE REFRESH RATE CARD (Laptop Power Saving)
     * On laptops: Enabled by default
     * On desktops: Hidden in Advanced Settings
     * ═══════════════════════════════════════════════════════════════════════ */
    {
        int is_laptop = 0;  /* Would call nxdisplay_kdrv_is_laptop() */
        
        rx_view* adapt_card = settings_card("Adaptive Refresh Rate");
        if (adapt_card) {
            /* Description */
            rx_text_view* desc = text_view_new(
                is_laptop ? 
                "Automatically adjusts refresh rate to save battery." :
                "Automatically adjusts refresh rate based on activity."
            );
            if (desc) {
                text_view_set_font_size(desc, 12.0f);
                desc->color = NX_COLOR_TEXT_DIM;
                view_add_child(adapt_card, (rx_view*)desc);
            }
            
            /* Mode selector */
            rx_view* mode_section = settings_section("POWER MODE");
            if (mode_section) view_add_child(adapt_card, mode_section);
            
            const char* modes[] = { "Off", "Power Save", "Balanced", "Performance" };
            rx_view* mode_row = hstack_new(8.0f);
            if (mode_row) {
                for (int i = 0; i < 4; i++) {
                    rx_button_view* btn = button_view_new(modes[i]);
                    if (btn) {
                        /* Balanced selected by default on laptop */
                        btn->normal_color = (i == 2 && is_laptop) ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
                        btn->hover_color = (rx_color){ 80, 80, 82, 255 };
                        view_add_child(mode_row, (rx_view*)btn);
                    }
                }
                view_add_child(adapt_card, mode_row);
            }
            
            /* Current state info */
            rx_view* state_row = settings_row("Current Refresh", "60 Hz (Browsing)");
            if (state_row) view_add_child(adapt_card, state_row);
            
            /* Thresholds */
            rx_view* thresh_section = settings_section("THRESHOLDS");
            if (thresh_section) view_add_child(adapt_card, thresh_section);
            
            rx_view* idle_row = settings_row("Idle", "30 Hz");
            if (idle_row) view_add_child(adapt_card, idle_row);
            
            rx_view* video_row = settings_row("Video Playback", "60 Hz");
            if (video_row) view_add_child(adapt_card, video_row);
            
            rx_view* work_row = settings_row("Heavy Work", "90 Hz");
            if (work_row) view_add_child(adapt_card, work_row);
            
            rx_view* gaming_row = settings_row("Gaming", "Max (144 Hz)");
            if (gaming_row) view_add_child(adapt_card, gaming_row);
            
            /* Battery threshold (laptop only) */
            if (is_laptop) {
                rx_view* batt_row = settings_row("Enable below", "50%");
                if (batt_row) view_add_child(adapt_card, batt_row);
            }
            
            view_add_child(panel, adapt_card);
        }
    }
    
    /* ═══════════════════════════════════════════════════════════════════════
     * GAMING MODE CARD
     * One-click optimization for games
     * ═══════════════════════════════════════════════════════════════════════ */
    {
        rx_view* game_card = settings_card("Gaming Mode");
        if (game_card) {
            /* Description */
            rx_text_view* desc = text_view_new(
                "Optimizes display for gaming: enables VRR, HDR, max refresh rate, and low latency mode."
            );
            if (desc) {
                text_view_set_font_size(desc, 12.0f);
                desc->color = NX_COLOR_TEXT_DIM;
                view_add_child(game_card, (rx_view*)desc);
            }
            
            /* Enable toggle */
            rx_view* enable_section = settings_section("MODE");
            if (enable_section) view_add_child(game_card, enable_section);
            
            const char* game_modes[] = { "Off", "Auto", "Always On" };
            rx_view* mode_row = hstack_new(8.0f);
            if (mode_row) {
                for (int i = 0; i < 3; i++) {
                    rx_button_view* btn = button_view_new(game_modes[i]);
                    if (btn) {
                        btn->normal_color = (i == 1) ? NX_COLOR_PRIMARY : (rx_color){ 60, 60, 62, 255 };
                        btn->hover_color = (rx_color){ 80, 80, 82, 255 };
                        view_add_child(mode_row, (rx_view*)btn);
                    }
                }
                view_add_child(game_card, mode_row);
            }
            
            /* Current status */
            rx_view* status_section = settings_section("STATUS");
            if (status_section) view_add_child(game_card, status_section);
            
            rx_view* active_row = settings_row("Gaming Mode", "Inactive");
            if (active_row) view_add_child(game_card, active_row);
            
            /* Features toggles */
            rx_view* features_section = settings_section("FEATURES");
            if (features_section) view_add_child(game_card, features_section);
            
            rx_view* vrr_row = hstack_new(8.0f);
            if (vrr_row) {
                rx_text_view* label = text_view_new("Variable Refresh Rate (VRR)");
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    view_add_child(vrr_row, (rx_view*)label);
                }
                rx_text_view* status = settings_label("Enabled", true);
                if (status) view_add_child(vrr_row, (rx_view*)status);
                view_add_child(game_card, vrr_row);
            }
            
            rx_view* hdr_row = hstack_new(8.0f);
            if (hdr_row) {
                rx_text_view* label = text_view_new("Auto HDR");
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    view_add_child(hdr_row, (rx_view*)label);
                }
                rx_text_view* status = settings_label("If supported", true);
                if (status) view_add_child(hdr_row, (rx_view*)status);
                view_add_child(game_card, hdr_row);
            }
            
            rx_view* latency_row = hstack_new(8.0f);
            if (latency_row) {
                rx_text_view* label = text_view_new("Low Latency Mode");
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    view_add_child(latency_row, (rx_view*)label);
                }
                rx_text_view* status = settings_label("Enabled", true);
                if (status) view_add_child(latency_row, (rx_view*)status);
                view_add_child(game_card, latency_row);
            }
            
            rx_view* compositor_row = hstack_new(8.0f);
            if (compositor_row) {
                rx_text_view* label = text_view_new("Bypass Compositor");
                if (label) {
                    label->color = NX_COLOR_TEXT;
                    view_add_child(compositor_row, (rx_view*)label);
                }
                rx_text_view* status = settings_label("Fullscreen only", true);
                if (status) view_add_child(compositor_row, (rx_view*)status);
                view_add_child(game_card, compositor_row);
            }
            
            /* Registered games */
            rx_view* games_section = settings_section("REGISTERED GAMES");
            if (games_section) view_add_child(game_card, games_section);
            
            rx_button_view* add_game_btn = button_view_new("+ Add Game...");
            if (add_game_btn) {
                add_game_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
                add_game_btn->hover_color = (rx_color){ 80, 80, 82, 255 };
                view_add_child(game_card, (rx_view*)add_game_btn);
            }
            
            view_add_child(panel, game_card);
        }
    }
    
    /* DDC/CI Hardware Control (external monitors only) */
    {
        rx_view* ddc_card = settings_card("Monitor Hardware Control (DDC/CI)");
        if (ddc_card) {
            rx_text_view* desc = text_view_new(
                "Control monitor OSD settings directly. Works on external monitors via HDMI/DisplayPort."
            );
            if (desc) {
                text_view_set_font_size(desc, 12.0f);
                desc->color = NX_COLOR_TEXT_DIM;
                view_add_child(ddc_card, (rx_view*)desc);
            }
            
            /* DDC/CI status */
            rx_view* status_row = settings_row("DDC/CI", "Supported");
            if (status_row) view_add_child(ddc_card, status_row);
            
            /* Hardware brightness */
            rx_view* hw_bright_row = settings_row("Hardware Brightness", "70%");
            if (hw_bright_row) view_add_child(ddc_card, hw_bright_row);
            
            /* Hardware contrast */
            rx_view* hw_contrast_row = settings_row("Hardware Contrast", "50%");
            if (hw_contrast_row) view_add_child(ddc_card, hw_contrast_row);
            
            /* Input source */
            rx_view* input_row = settings_row("Input Source", "HDMI 1");
            if (input_row) view_add_child(ddc_card, input_row);
            
            rx_button_view* switch_input_btn = button_view_new("Switch Input...");
            if (switch_input_btn) {
                switch_input_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
                view_add_child(ddc_card, (rx_view*)switch_input_btn);
            }
            
            view_add_child(panel, ddc_card);
        }
    }
    
    /* Arrangement card (multi-monitor) */
    rx_view* arr_card = create_arrangement_card();
    if (arr_card) view_add_child(panel, arr_card);
    
    println("[Display] Enhanced panel with Adaptive Refresh, Gaming Mode, DDC/CI");
    return panel;
}
