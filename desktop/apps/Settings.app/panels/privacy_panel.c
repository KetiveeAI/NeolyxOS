/*
 * NeolyxOS Settings - Privacy Panel
 * 
 * Privacy and data protection settings
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../settings.h"
#include <stdio.h>

/* Privacy settings state */
static struct {
    bool location_services;
    bool camera_access;
    bool microphone_access;
    bool contacts_access;
    bool calendar_access;
    bool analytics;
    bool crash_reports;
    bool personalized_ads;
    bool app_tracking;
    bool screen_recording;
    bool clipboard_access;
} g_privacy = {
    .location_services = true,
    .camera_access = true,
    .microphone_access = true,
    .contacts_access = false,
    .calendar_access = false,
    .analytics = true,
    .crash_reports = true,
    .personalized_ads = false,
    .app_tracking = false,
    .screen_recording = true,
    .clipboard_access = true
};

/* Create privacy toggle row */
static rx_view* privacy_toggle(const char* label, bool enabled) {
    rx_view* row = hstack_new(8.0f);
    if (!row) return NULL;
    
    rx_text_view* lbl = text_view_new(label);
    if (lbl) {
        lbl->color = NX_COLOR_TEXT;
        view_add_child(row, (rx_view*)lbl);
    }
    
    rx_text_view* status = settings_label(enabled ? "Allowed" : "Denied", true);
    if (status) {
        status->color = enabled ? NX_COLOR_SUCCESS : NX_COLOR_TEXT_DIM;
        view_add_child(row, (rx_view*)status);
    }
    
    return row;
}

/* Create permissions card */
static rx_view* create_permissions_card(void) {
    rx_view* card = settings_card("App Permissions");
    if (!card) return NULL;
    
    rx_text_view* desc = settings_label("Control which apps can access your data", true);
    if (desc) view_add_child(card, (rx_view*)desc);
    
    /* Location */
    rx_view* loc_row = privacy_toggle("Location Services", g_privacy.location_services);
    if (loc_row) view_add_child(card, loc_row);
    
    /* Camera */
    rx_view* cam_row = privacy_toggle("Camera", g_privacy.camera_access);
    if (cam_row) view_add_child(card, cam_row);
    
    /* Microphone */
    rx_view* mic_row = privacy_toggle("Microphone", g_privacy.microphone_access);
    if (mic_row) view_add_child(card, mic_row);
    
    /* Screen recording */
    rx_view* screen_row = privacy_toggle("Screen Recording", g_privacy.screen_recording);
    if (screen_row) view_add_child(card, screen_row);
    
    /* Contacts */
    rx_view* contacts_row = privacy_toggle("Contacts", g_privacy.contacts_access);
    if (contacts_row) view_add_child(card, contacts_row);
    
    /* Calendar */
    rx_view* cal_row = privacy_toggle("Calendar", g_privacy.calendar_access);
    if (cal_row) view_add_child(card, cal_row);
    
    /* Clipboard */
    rx_view* clip_row = privacy_toggle("Clipboard Access", g_privacy.clipboard_access);
    if (clip_row) view_add_child(card, clip_row);
    
    /* Manage per-app permissions */
    rx_button_view* manage_btn = button_view_new("Manage App Permissions...");
    if (manage_btn) {
        manage_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)manage_btn);
    }
    
    return card;
}

/* Create analytics card */
static rx_view* create_analytics_card(void) {
    rx_view* card = settings_card("Analytics & Telemetry");
    if (!card) return NULL;
    
    /* Share analytics */
    rx_view* analytics_row = hstack_new(8.0f);
    if (analytics_row) {
        rx_text_view* label = text_view_new("Share Usage Analytics");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(analytics_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_privacy.analytics ? "On" : "Off", true);
        if (status) view_add_child(analytics_row, (rx_view*)status);
        view_add_child(card, analytics_row);
    }
    
    /* Crash reports */
    rx_view* crash_row = hstack_new(8.0f);
    if (crash_row) {
        rx_text_view* label = text_view_new("Send Crash Reports");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(crash_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_privacy.crash_reports ? "On" : "Off", true);
        if (status) view_add_child(crash_row, (rx_view*)status);
        view_add_child(card, crash_row);
    }
    
    rx_text_view* note = settings_label(
        "Analytics help improve NeolyxOS. No personal data is collected.", true);
    if (note) view_add_child(card, (rx_view*)note);
    
    return card;
}

/* Create advertising card */
static rx_view* create_advertising_card(void) {
    rx_view* card = settings_card("Advertising");
    if (!card) return NULL;
    
    /* Personalized ads */
    rx_view* ads_row = hstack_new(8.0f);
    if (ads_row) {
        rx_text_view* label = text_view_new("Personalized Ads");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(ads_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_privacy.personalized_ads ? "On" : "Off", true);
        if (status) view_add_child(ads_row, (rx_view*)status);
        view_add_child(card, ads_row);
    }
    
    /* App tracking */
    rx_view* track_row = hstack_new(8.0f);
    if (track_row) {
        rx_text_view* label = text_view_new("Allow App Tracking");
        if (label) {
            label->color = NX_COLOR_TEXT;
            view_add_child(track_row, (rx_view*)label);
        }
        rx_text_view* status = settings_label(g_privacy.app_tracking ? "On" : "Off", true);
        if (status) view_add_child(track_row, (rx_view*)status);
        view_add_child(card, track_row);
    }
    
    /* Reset advertising ID */
    rx_button_view* reset_btn = button_view_new("Reset Advertising ID");
    if (reset_btn) {
        reset_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)reset_btn);
    }
    
    return card;
}

/* Create data management card */
static rx_view* create_data_card(void) {
    rx_view* card = settings_card("Data Management");
    if (!card) return NULL;
    
    rx_button_view* export_btn = button_view_new("Export My Data");
    if (export_btn) {
        export_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)export_btn);
    }
    
    rx_button_view* clear_btn = button_view_new("Clear Browsing History");
    if (clear_btn) {
        clear_btn->normal_color = (rx_color){ 60, 60, 62, 255 };
        view_add_child(card, (rx_view*)clear_btn);
    }
    
    rx_button_view* delete_btn = button_view_new("Delete All Data");
    if (delete_btn) {
        delete_btn->normal_color = NX_COLOR_ERROR;
        view_add_child(card, (rx_view*)delete_btn);
    }
    
    return card;
}

/* Main panel creation */
rx_view* privacy_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(16.0f);
    if (!panel) return NULL;
    
    /* Title */
    rx_text_view* title = text_view_new("Privacy");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(panel, (rx_view*)title);
    }
    
    /* Permissions card */
    rx_view* perm_card = create_permissions_card();
    if (perm_card) view_add_child(panel, perm_card);
    
    /* Analytics card */
    rx_view* analytics_card = create_analytics_card();
    if (analytics_card) view_add_child(panel, analytics_card);
    
    /* Advertising card */
    rx_view* ads_card = create_advertising_card();
    if (ads_card) view_add_child(panel, ads_card);
    
    /* Data management card */
    rx_view* data_card = create_data_card();
    if (data_card) view_add_child(panel, data_card);
    
    println("[Privacy] Panel created");
    return panel;
}
