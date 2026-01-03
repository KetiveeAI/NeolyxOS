/*
 * Settings - Color Management Panel
 * Display calibration and color profiles
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <nxcolor.h>
#include <string.h>
#include <stdlib.h>

/* ============ Panel State ============ */

typedef struct {
    nxcolor_display_t *displays;
    int display_count;
    int selected_display;
    
    nxcolor_profile_t **profiles;
    int profile_count;
    int selected_profile;
    
    nxcolor_prefs_t prefs;
    
    bool calibrating;
    int calibration_step;
} color_panel_state_t;

static color_panel_state_t state = {0};

/* ============ Forward Declarations ============ */

static void refresh_displays(void);
static void refresh_profiles(void);
static void draw_display_section(settings_panel_t *panel, int y);
static void draw_profile_section(settings_panel_t *panel, int y);
static void draw_adjustments_section(settings_panel_t *panel, int y);
static void draw_advanced_section(settings_panel_t *panel, int y);

/* ============ Panel Callbacks ============ */

void color_panel_init(settings_panel_t *panel) {
    panel->title = "Color";
    panel->icon = "color";
    
    /* Initialize color management */
    nxcolor_init();
    
    /* Load preferences */
    nxcolor_prefs_load(&state.prefs);
    
    /* Scan displays and profiles */
    refresh_displays();
    refresh_profiles();
}

void color_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    
    /* Header */
    panel_draw_header(panel->x + 20, y, "Color Management");
    y += 45;
    
    /* Display Selection */
    draw_display_section(panel, y);
    y += 180;
    
    /* Separator */
    panel_draw_line(panel->x + 20, y, panel->x + panel->width - 20, y, 0x3A3A3A);
    y += 20;
    
    /* Color Profile */
    draw_profile_section(panel, y);
    y += 200;
    
    /* Separator */
    panel_draw_line(panel->x + 20, y, panel->x + panel->width - 20, y, 0x3A3A3A);
    y += 20;
    
    /* Display Adjustments */
    draw_adjustments_section(panel, y);
    y += 180;
    
    /* Advanced Options */
    draw_advanced_section(panel, y);
}

/* ============ Display Section ============ */

static void draw_display_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_subheader(x, y, "Display");
    y += 30;
    
    /* Display dropdown */
    if (state.display_count > 0) {
        nxcolor_display_t *disp = &state.displays[state.selected_display];
        
        panel_draw_label(x, y, "Select Display:");
        panel_draw_dropdown(x + 120, y, 200, disp->display_name, 
                           (const char **)NULL, state.display_count);
        y += 35;
        
        /* Display info */
        char info[128];
        snprintf(info, sizeof(info), "Profile: %s", 
                 disp->profile ? disp->profile->name : "None");
        panel_draw_text(x, y, info, 0x888888);
        y += 25;
        
        /* Night Shift toggle */
        panel_draw_label(x, y, "Night Shift:");
        panel_draw_toggle(x + 120, y, disp->night_shift);
        y += 35;
        
        if (disp->night_shift) {
            panel_draw_label(x + 20, y, "Schedule:");
            char sched[32];
            snprintf(sched, sizeof(sched), "%02d:00 - %02d:00",
                     disp->night_shift_start, disp->night_shift_end);
            panel_draw_text(x + 120, y, sched, 0xCCCCCC);
            y += 25;
            
            panel_draw_label(x + 20, y, "Warmth:");
            panel_draw_slider(x + 120, y, 150, disp->night_shift_warmth, 0, 1);
        }
        
        /* True Tone */
        y += 35;
        panel_draw_label(x, y, "True Tone:");
        panel_draw_toggle(x + 120, y, disp->true_tone);
        panel_draw_text(x + 180, y, "(Match ambient lighting)", 0x666666);
    } else {
        panel_draw_text(x, y, "No displays detected", 0xFF3B30);
    }
}

/* ============ Profile Section ============ */

static void draw_profile_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_subheader(x, y, "Color Profile");
    y += 30;
    
    /* Profile list */
    for (int i = 0; i < state.profile_count && i < 8; i++) {
        nxcolor_profile_t *prof = state.profiles[i];
        bool selected = (i == state.selected_profile);
        
        uint32_t bg = selected ? 0x0A84FF : 0x2A2A2A;
        panel_fill_rect(x, y, panel->width - 60, 32, bg);
        
        /* Profile name */
        panel_draw_text(x + 10, y + 8, prof->name, 0xFFFFFF);
        
        /* Color space badge */
        const char *space_name = "sRGB";
        switch (prof->colorspace) {
            case COLORSPACE_DISPLAY_P3: space_name = "Display P3"; break;
            case COLORSPACE_ADOBE_RGB: space_name = "Adobe RGB"; break;
            case COLORSPACE_REC2020: space_name = "Rec. 2020"; break;
            default: break;
        }
        panel_draw_badge(x + panel->width - 150, y + 6, space_name, 0x34C759);
        
        y += 36;
    }
    
    y += 10;
    
    /* Buttons */
    panel_draw_button(x, y, "Calibrate Display...", 0x007AFF);
    panel_draw_button(x + 160, y, "Open Profile...", 0x48484A);
}

/* ============ Adjustments Section ============ */

static void draw_adjustments_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_subheader(x, y, "Display Adjustments");
    y += 30;
    
    if (state.display_count > 0) {
        nxcolor_display_t *disp = &state.displays[state.selected_display];
        
        /* Brightness */
        panel_draw_label(x, y, "Brightness:");
        panel_draw_slider(x + 120, y, 200, disp->brightness, 0, 1);
        char val[16];
        snprintf(val, sizeof(val), "%.0f%%", disp->brightness * 100);
        panel_draw_text(x + 330, y, val, 0x888888);
        y += 35;
        
        /* Contrast */
        panel_draw_label(x, y, "Contrast:");
        panel_draw_slider(x + 120, y, 200, disp->contrast / 2.0f, 0, 1);
        snprintf(val, sizeof(val), "%.0f%%", disp->contrast * 50);
        panel_draw_text(x + 330, y, val, 0x888888);
        y += 35;
        
        /* Saturation */
        panel_draw_label(x, y, "Saturation:");
        panel_draw_slider(x + 120, y, 200, disp->saturation / 2.0f, 0, 1);
        snprintf(val, sizeof(val), "%.0f%%", disp->saturation * 50);
        panel_draw_text(x + 330, y, val, 0x888888);
        y += 35;
        
        /* Temperature */
        panel_draw_label(x, y, "Temperature:");
        panel_draw_slider(x + 120, y, 200, (disp->temperature + 100) / 200.0f, 0, 1);
        if (disp->temperature < 0) {
            snprintf(val, sizeof(val), "Cool %d", -disp->temperature);
        } else if (disp->temperature > 0) {
            snprintf(val, sizeof(val), "Warm +%d", disp->temperature);
        } else {
            strcpy(val, "Neutral");
        }
        panel_draw_text(x + 330, y, val, 0x888888);
    }
}

/* ============ Advanced Section ============ */

static void draw_advanced_section(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_subheader(x, y, "Advanced");
    y += 30;
    
    /* Working Space */
    panel_draw_label(x, y, "RGB Working Space:");
    const char *rgb_space = "sRGB";
    switch (state.prefs.rgb_working_space) {
        case COLORSPACE_DISPLAY_P3: rgb_space = "Display P3"; break;
        case COLORSPACE_ADOBE_RGB: rgb_space = "Adobe RGB"; break;
        case COLORSPACE_PROPHOTO_RGB: rgb_space = "ProPhoto RGB"; break;
        default: break;
    }
    panel_draw_dropdown(x + 160, y, 150, rgb_space, NULL, 0);
    y += 35;
    
    /* Rendering Intent */
    panel_draw_label(x, y, "Rendering Intent:");
    const char *intent = "Perceptual";
    switch (state.prefs.default_intent) {
        case INTENT_RELATIVE: intent = "Relative Colorimetric"; break;
        case INTENT_SATURATION: intent = "Saturation"; break;
        case INTENT_ABSOLUTE: intent = "Absolute Colorimetric"; break;
        default: break;
    }
    panel_draw_dropdown(x + 160, y, 180, intent, NULL, 0);
    y += 35;
    
    /* Black Point Compensation */
    panel_draw_label(x, y, "Black Point Compensation:");
    panel_draw_toggle(x + 220, y, state.prefs.black_point_compensation);
    y += 35;
    
    /* Soft Proofing */
    panel_draw_label(x, y, "Soft Proofing:");
    panel_draw_toggle(x + 160, y, state.prefs.soft_proofing_enabled);
    if (state.prefs.soft_proofing_enabled && state.prefs.proof_profile) {
        panel_draw_text(x + 220, y, state.prefs.proof_profile->name, 0x888888);
    }
    y += 35;
    
    /* Gamut Warning */
    panel_draw_label(x, y, "Out-of-Gamut Warning:");
    panel_draw_toggle(x + 200, y, state.prefs.gamut_warning_enabled);
}

/* ============ Event Handling ============ */

void color_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    if (event->type == EVT_CLICK) {
        /* Handle display selection */
        /* Handle profile selection */
        /* Handle slider drags */
        /* Handle button clicks */
        
        panel->needs_redraw = true;
    }
}

void color_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
    
    /* Save preferences */
    nxcolor_prefs_save(&state.prefs);
    
    /* Free profiles */
    for (int i = 0; i < state.profile_count; i++) {
        nxcolor_profile_free(state.profiles[i]);
    }
    free(state.profiles);
    
    /* Shutdown */
    nxcolor_shutdown();
}

/* ============ Internal Functions ============ */

static void refresh_displays(void) {
    state.display_count = nxcolor_display_count();
    state.displays = malloc(sizeof(nxcolor_display_t) * state.display_count);
    
    for (int i = 0; i < state.display_count; i++) {
        nxcolor_display_t *d = nxcolor_display_get(i);
        if (d) {
            state.displays[i] = *d;
        }
    }
    
    state.selected_display = 0;
}

static void refresh_profiles(void) {
    int count = nxcolor_profiles_system_count() + nxcolor_profiles_user_count();
    state.profiles = malloc(sizeof(nxcolor_profile_t *) * count);
    state.profile_count = nxcolor_profiles_list(state.profiles, count);
    state.selected_profile = 0;
}
