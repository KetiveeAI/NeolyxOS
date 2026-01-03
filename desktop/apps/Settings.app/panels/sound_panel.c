/*
 * NeolyxOS Settings - Enhanced Sound Panel
 * 
 * Advanced audio settings with real driver integration
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include "../drivers/settings_drivers.h"
#include <stdio.h>
#include <string.h>

/* Audio state */
static audio_settings_t g_audio_settings = {
    .master_volume = 60,
    .mic_volume = 50,
    .master_muted = false,
    .mic_muted = false,
    .sound_effects = true,
    .sample_rate = 48000,
    .bit_depth = 24,
    .output_device = "Built-in Speakers",
    .input_device = "Built-in Microphone",
    .spatial_audio = false,
    .noise_cancellation = false
};

static audio_device_t g_output_devices[8];
static audio_device_t g_input_devices[8];
static int g_output_count = 0;
static int g_input_count = 0;

/* Load current audio settings */
static void load_audio_settings(void) {
    audio_get_settings(&g_audio_settings);
    g_output_count = audio_get_output_devices(g_output_devices, 8);
    g_input_count = audio_get_input_devices(g_input_devices, 8);
}

/* Create output volume card */
static rx_view* create_output_card(void) {
    rx_view* card = settings_card("Output");
    if (!card) return NULL;
    
    /* Volume slider */
    rx_view* vol_row = hstack_new(12.0f);
    if (vol_row) {
        rx_text_view* label = text_view_new("Volume");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(vol_row, (rx_view*)label);
        }
        
        char vol_val[16];
        snprintf(vol_val, 16, "%d%%", g_audio_settings.master_volume);
        rx_text_view* value = settings_label(g_audio_settings.master_muted ? "Muted" : vol_val, true);
        if (value) {
            if (g_audio_settings.master_muted) value->color = NX_COLOR_ERROR;
            view_add_child(vol_row, (rx_view*)value);
        }
        
        view_add_child(card, vol_row);
    }
    
    /* Volume slider bar */
    rx_view* slider = hstack_new(0);
    if (slider) {
        slider->box.background = (rx_color){ 60, 60, 62, 255 };
        slider->box.height = 8;
        slider->box.corner_radius = corners_all(4.0f);
        view_add_child(card, slider);
    }
    
    /* Output device selector */
    rx_view* device_section = settings_section("OUTPUT DEVICE");
    if (device_section) view_add_child(card, device_section);
    
    for (int i = 0; i < g_output_count && i < 4; i++) {
        rx_view* device_row = hstack_new(8.0f);
        if (device_row) {
            rx_text_view* name = text_view_new(g_output_devices[i].name);
            if (name) {
                name->color = g_output_devices[i].is_default ? NX_COLOR_PRIMARY : NX_COLOR_TEXT;
                view_add_child(device_row, (rx_view*)name);
            }
            
            if (g_output_devices[i].is_default) {
                rx_text_view* check = text_view_new("✓");
                if (check) {
                    check->color = NX_COLOR_PRIMARY;
                    view_add_child(device_row, (rx_view*)check);
                }
            }
            
            view_add_child(card, device_row);
        }
    }
    
    /* Balance */
    rx_view* balance_row = settings_row("Balance", "Center");
    if (balance_row) view_add_child(card, balance_row);
    
    return card;
}

/* Create input card */
static rx_view* create_input_card(void) {
    rx_view* card = settings_card("Input");
    if (!card) return NULL;
    
    /* Mic volume slider */
    rx_view* vol_row = hstack_new(12.0f);
    if (vol_row) {
        rx_text_view* label = text_view_new("Microphone Volume");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(vol_row, (rx_view*)label);
        }
        
        char vol_val[16];
        snprintf(vol_val, 16, "%d%%", g_audio_settings.mic_volume);
        rx_text_view* value = settings_label(g_audio_settings.mic_muted ? "Muted" : vol_val, true);
        if (value) {
            if (g_audio_settings.mic_muted) value->color = NX_COLOR_ERROR;
            view_add_child(vol_row, (rx_view*)value);
        }
        
        view_add_child(card, vol_row);
    }
    
    /* Slider bar */
    rx_view* slider = hstack_new(0);
    if (slider) {
        slider->box.background = (rx_color){ 60, 60, 62, 255 };
        slider->box.height = 8;
        slider->box.corner_radius = corners_all(4.0f);
        view_add_child(card, slider);
    }
    
    /* Input device selector */
    rx_view* device_section = settings_section("INPUT DEVICE");
    if (device_section) view_add_child(card, device_section);
    
    for (int i = 0; i < g_input_count && i < 4; i++) {
        rx_view* device_row = hstack_new(8.0f);
        if (device_row) {
            rx_text_view* name = text_view_new(g_input_devices[i].name);
            if (name) {
                name->color = g_input_devices[i].is_default ? NX_COLOR_PRIMARY : NX_COLOR_TEXT;
                view_add_child(device_row, (rx_view*)name);
            }
            
            if (g_input_devices[i].is_default) {
                rx_text_view* check = text_view_new("✓");
                if (check) {
                    check->color = NX_COLOR_PRIMARY;
                    view_add_child(device_row, (rx_view*)check);
                }
            }
            
            view_add_child(card, device_row);
        }
    }
    
    /* Input level meter placeholder */
    rx_text_view* meter_label = settings_label("Input Level:", false);
    if (meter_label) view_add_child(card, (rx_view*)meter_label);
    
    rx_view* meter = hstack_new(0);
    if (meter) {
        meter->box.background = NX_COLOR_SUCCESS;
        meter->box.width = 100;
        meter->box.height = 6;
        meter->box.corner_radius = corners_all(3.0f);
        view_add_child(card, meter);
    }
    
    /* Noise cancellation */
    rx_view* noise_row = hstack_new(8.0f);
    if (noise_row) {
        rx_text_view* label = text_view_new("Noise Cancellation");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(noise_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_audio_settings.noise_cancellation ? "On" : "Off", true);
        if (status) view_add_child(noise_row, (rx_view*)status);
        view_add_child(card, noise_row);
    }
    
    return card;
}

/* Create sound effects card */
static rx_view* create_effects_card(void) {
    rx_view* card = settings_card("Sound Effects");
    if (!card) return NULL;
    
    /* System sounds toggle */
    rx_view* sys_row = hstack_new(8.0f);
    if (sys_row) {
        rx_text_view* label = text_view_new("System Sounds");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(sys_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_audio_settings.sound_effects ? "On" : "Off", true);
        if (status) view_add_child(sys_row, (rx_view*)status);
        view_add_child(card, sys_row);
    }
    
    /* Alert sound selector */
    rx_view* alert_row = settings_row("Alert Sound", "Neolyx Chime");
    if (alert_row) view_add_child(card, alert_row);
    
    /* Startup sound */
    rx_view* startup_row = hstack_new(8.0f);
    if (startup_row) {
        rx_text_view* label = text_view_new("Play Startup Sound");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(startup_row, (rx_view*)label);
        }
        view_add_child(card, startup_row);
    }
    
    /* Spatial audio */
    rx_view* spatial_row = hstack_new(8.0f);
    if (spatial_row) {
        rx_text_view* label = text_view_new("Spatial Audio");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(spatial_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_audio_settings.spatial_audio ? "On" : "Off", true);
        if (status) view_add_child(spatial_row, (rx_view*)status);
        view_add_child(card, spatial_row);
    }
    
    return card;
}

/* Create advanced audio card */
static rx_view* create_advanced_audio_card(void) {
    rx_view* card = settings_card("Advanced");
    if (!card) return NULL;
    
    /* Sample rate */
    char rate_str[32];
    snprintf(rate_str, 32, "%d Hz", g_audio_settings.sample_rate);
    rx_view* rate_row = settings_row("Sample Rate", rate_str);
    if (rate_row) view_add_child(card, rate_row);
    
    /* Bit depth */
    char depth_str[16];
    snprintf(depth_str, 16, "%d-bit", g_audio_settings.bit_depth);
    rx_view* depth_row = settings_row("Bit Depth", depth_str);
    if (depth_row) view_add_child(card, depth_row);
    
    /* Test buttons */
    rx_view* test_row = hstack_new(12.0f);
    if (test_row) {
        rx_button_view* test_speakers = button_view_new("Test Speakers");
        if (test_speakers) {
            test_speakers->normal_color = (rx_color){ 60, 60, 62, 255 };
            view_add_child(test_row, (rx_view*)test_speakers);
        }
        
        rx_button_view* test_mic = button_view_new("Test Microphone");
        if (test_mic) {
            test_mic->normal_color = (rx_color){ 60, 60, 62, 255 };
            view_add_child(test_row, (rx_view*)test_mic);
        }
        
        view_add_child(card, test_row);
    }
    
    return card;
}

/* Main panel creation - replaces old sound_panel.c */
rx_view* sound_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    /* Load settings from driver */
    load_audio_settings();
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Sound");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Output card */
    rx_view* output_card = create_output_card();
    if (output_card) view_add_child(panel, output_card);
    
    /* Input card */
    rx_view* input_card = create_input_card();
    if (input_card) view_add_child(panel, input_card);
    
    /* Sound effects card */
    rx_view* effects_card = create_effects_card();
    if (effects_card) view_add_child(panel, effects_card);
    
    /* Advanced card */
    rx_view* adv_card = create_advanced_audio_card();
    if (adv_card) view_add_child(panel, adv_card);
    
    println("[Sound] Enhanced panel created with driver integration");
    return panel;
}
