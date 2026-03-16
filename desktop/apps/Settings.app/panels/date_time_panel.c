/*
 * NeolyxOS Settings - Date & Time Panel
 * 
 * Manages timezone, date format, and time display settings.
 * Writes to /System/Config/locale.nlc
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "../settings.h"
#include "panel_common.h"
#include <stdio.h>
#include <string.h>

/* Timezone database (subset of common timezones) */
typedef struct {
    const char *name;
    const char *display;
    int16_t offset_minutes;
} timezone_entry_t;

static const timezone_entry_t g_timezones[] = {
    {"Pacific/Honolulu", "Hawaii (HST)", -600},
    {"America/Los_Angeles", "Pacific Time (PST/PDT)", -480},
    {"America/Denver", "Mountain Time (MST/MDT)", -420},
    {"America/Chicago", "Central Time (CST/CDT)", -360},
    {"America/New_York", "Eastern Time (EST/EDT)", -300},
    {"America/Sao_Paulo", "Brasilia (BRT)", -180},
    {"Atlantic/Azores", "Azores (AZOT)", -60},
    {"Europe/London", "London (GMT/BST)", 0},
    {"Europe/Paris", "Central European (CET/CEST)", 60},
    {"Europe/Athens", "Eastern European (EET/EEST)", 120},
    {"Europe/Moscow", "Moscow (MSK)", 180},
    {"Asia/Dubai", "Gulf (GST)", 240},
    {"Asia/Karachi", "Pakistan (PKT)", 300},
    {"Asia/Kolkata", "India (IST)", 330},
    {"Asia/Dhaka", "Bangladesh (BST)", 360},
    {"Asia/Bangkok", "Indochina (ICT)", 420},
    {"Asia/Singapore", "Singapore (SGT)", 480},
    {"Asia/Tokyo", "Japan (JST)", 540},
    {"Australia/Sydney", "Sydney (AEST/AEDT)", 600},
    {"Pacific/Auckland", "New Zealand (NZST/NZDT)", 720},
};

#define TIMEZONE_COUNT (sizeof(g_timezones) / sizeof(g_timezones[0]))

/* Current settings (mirrors g_nx_config.locale) */
static int g_selected_tz = 13;  /* Default: India */
static bool g_time_24h = true;
static int g_date_format = 0;   /* 0=DD/MM, 1=MM/DD, 2=YYYY-MM-DD */

/* Format offset as string like "+5:30" or "-8:00" */
static void format_offset(int16_t minutes, char *buf, int buflen) {
    int sign = (minutes >= 0) ? 1 : -1;
    int abs_min = (minutes >= 0) ? minutes : -minutes;
    int hours = abs_min / 60;
    int mins = abs_min % 60;
    snprintf(buf, buflen, "%c%d:%02d", sign > 0 ? '+' : '-', hours, mins);
}

/* Create timezone selector card */
static rx_view* create_timezone_card(void) {
    rx_view* card = settings_card("Time Zone");
    if (!card) return NULL;
    
    /* Current timezone display */
    char tz_display[128];
    char offset_str[16];
    format_offset(g_timezones[g_selected_tz].offset_minutes, offset_str, 16);
    snprintf(tz_display, 128, "%s (UTC%s)", 
             g_timezones[g_selected_tz].display, offset_str);
    
    rx_view* current_row = settings_row("Current", tz_display);
    if (current_row) view_add_child(card, current_row);
    
    /* Timezone dropdown */
    rx_view* tz_section = vstack_new(8.0f);
    if (tz_section) {
        rx_text_view* label = settings_label("Select Time Zone", false);
        if (label) view_add_child(tz_section, (rx_view*)label);
        
        /* Scrollable timezone list */
        rx_view* list = vstack_new(4.0f);
        if (list) {
            list->box.height = 200;
            list->box.background = (rx_color){58, 58, 60, 255};
            list->box.corner_radius = corners_all(8.0f);
            list->box.padding = insets_all(8.0f);
            
            for (int i = 0; i < (int)TIMEZONE_COUNT; i++) {
                char entry[128];
                format_offset(g_timezones[i].offset_minutes, offset_str, 16);
                snprintf(entry, 128, "%s (UTC%s)", 
                         g_timezones[i].display, offset_str);
                
                rx_view* row = hstack_new(8.0f);
                if (row) {
                    /* Selection indicator */
                    rx_view* indicator = vstack_new(0);
                    if (indicator) {
                        indicator->box.width = 8;
                        indicator->box.height = 8;
                        indicator->box.corner_radius = corners_all(4.0f);
                        indicator->box.background = (i == g_selected_tz) 
                            ? NX_COLOR_PRIMARY 
                            : (rx_color){80, 80, 85, 255};
                        view_add_child(row, indicator);
                    }
                    
                    rx_text_view* tz_label = text_view_new(entry);
                    if (tz_label) {
                        tz_label->color = (i == g_selected_tz) 
                            ? NX_COLOR_PRIMARY 
                            : NX_COLOR_TEXT;
                        text_view_set_font_size(tz_label, 14.0f);
                        view_add_child(row, (rx_view*)tz_label);
                    }
                    
                    /* Store index for click handler */
                    row->tag = i;
                    view_add_child(list, row);
                }
            }
            view_add_child(tz_section, list);
        }
        view_add_child(card, tz_section);
    }
    
    /* Note about auto-detection */
    rx_text_view* note = settings_label(
        "Automatic time zone detection coming soon", true);
    if (note) {
        text_view_set_font_size(note, 12.0f);
        view_add_child(card, (rx_view*)note);
    }
    
    return card;
}

/* Create time format card */
static rx_view* create_time_format_card(void) {
    rx_view* card = settings_card("Time Format");
    if (!card) return NULL;
    
    /* 24-hour toggle */
    rx_view* format_row = hstack_new(12.0f);
    if (format_row) {
        rx_text_view* label = settings_label("24-Hour Time", false);
        if (label) view_add_child(format_row, (rx_view*)label);
        
        /* Spacer */
        rx_view* spacer = vstack_new(0);
        if (spacer) {
            spacer->box.width = -1;
            view_add_child(format_row, spacer);
        }
        
        /* Toggle switch visual */
        rx_view* toggle = hstack_new(0);
        if (toggle) {
            toggle->box.width = 51;
            toggle->box.height = 31;
            toggle->box.corner_radius = corners_all(15.5f);
            toggle->box.background = g_time_24h 
                ? NX_COLOR_PRIMARY 
                : (rx_color){120, 120, 128, 255};
            
            /* Knob */
            rx_view* knob = vstack_new(0);
            if (knob) {
                knob->box.width = 27;
                knob->box.height = 27;
                knob->box.corner_radius = corners_all(13.5f);
                knob->box.background = NX_COLOR_TEXT;
                knob->box.margin = insets_symmetric(2.0f, g_time_24h ? 22.0f : 2.0f);
                view_add_child(toggle, knob);
            }
            view_add_child(format_row, toggle);
        }
        view_add_child(card, format_row);
    }
    
    /* Preview */
    char preview[32];
    if (g_time_24h) {
        snprintf(preview, 32, "14:30");
    } else {
        snprintf(preview, 32, "2:30 PM");
    }
    rx_view* preview_row = settings_row("Preview", preview);
    if (preview_row) view_add_child(card, preview_row);
    
    return card;
}

/* Create date format card */
static rx_view* create_date_format_card(void) {
    rx_view* card = settings_card("Date Format");
    if (!card) return NULL;
    
    const char* formats[] = {
        "DD/MM/YYYY (21/01/2026)",
        "MM/DD/YYYY (01/21/2026)",
        "YYYY-MM-DD (2026-01-21)"
    };
    
    for (int i = 0; i < 3; i++) {
        rx_view* row = hstack_new(12.0f);
        if (row) {
            /* Radio button */
            rx_view* radio = vstack_new(0);
            if (radio) {
                radio->box.width = 20;
                radio->box.height = 20;
                radio->box.corner_radius = corners_all(10.0f);
                radio->box.border_width = 2;
                radio->box.border_color = (i == g_date_format) 
                    ? NX_COLOR_PRIMARY 
                    : (rx_color){100, 100, 105, 255};
                
                if (i == g_date_format) {
                    /* Inner dot */
                    rx_view* dot = vstack_new(0);
                    if (dot) {
                        dot->box.width = 10;
                        dot->box.height = 10;
                        dot->box.corner_radius = corners_all(5.0f);
                        dot->box.background = NX_COLOR_PRIMARY;
                        dot->box.margin = insets_all(3.0f);
                        view_add_child(radio, dot);
                    }
                }
                view_add_child(row, radio);
            }
            
            rx_text_view* label = settings_label(formats[i], false);
            if (label) view_add_child(row, (rx_view*)label);
            
            row->tag = i;
            view_add_child(card, row);
        }
    }
    
    return card;
}

/* Create first day of week card */
static rx_view* create_week_start_card(void) {
    rx_view* card = settings_card("Calendar");
    if (!card) return NULL;
    
    rx_view* row = settings_row("Week Starts On", 
        g_nx_config.locale.first_day_of_week == 0 ? "Sunday" : "Monday");
    if (row) view_add_child(card, row);
    
    return card;
}

/* Main panel creation */
rx_view* date_time_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    /* Load current settings */
    /* Find matching timezone */
    for (int i = 0; i < (int)TIMEZONE_COUNT; i++) {
        if (g_timezones[i].offset_minutes == g_nx_config.locale.offset_minutes) {
            g_selected_tz = i;
            break;
        }
    }
    g_time_24h = g_nx_config.locale.time_24h;
    
    /* Determine date format from string */
    if (strcmp(g_nx_config.locale.date_format, "MM/DD/YYYY") == 0) {
        g_date_format = 1;
    } else if (strcmp(g_nx_config.locale.date_format, "YYYY-MM-DD") == 0) {
        g_date_format = 2;
    } else {
        g_date_format = 0;
    }
    
    /* Create scrollable content */
    rx_view* scroll = vstack_new(16.0f);
    if (!scroll) return NULL;
    
    scroll->box.padding = insets_all(24.0f);
    
    /* Header */
    rx_text_view* title = text_view_new("Date & Time");
    if (title) {
        text_view_set_font_size(title, 28.0f);
        title->font_weight = 700;
        title->color = NX_COLOR_TEXT;
        view_add_child(scroll, (rx_view*)title);
    }
    
    rx_text_view* subtitle = settings_label(
        "Set your timezone and date/time display preferences", true);
    if (subtitle) view_add_child(scroll, (rx_view*)subtitle);
    
    /* Cards */
    rx_view* tz_card = create_timezone_card();
    if (tz_card) view_add_child(scroll, tz_card);
    
    rx_view* time_card = create_time_format_card();
    if (time_card) view_add_child(scroll, time_card);
    
    rx_view* date_card = create_date_format_card();
    if (date_card) view_add_child(scroll, date_card);
    
    rx_view* week_card = create_week_start_card();
    if (week_card) view_add_child(scroll, week_card);
    
    /* Note about clock sync */
    rx_view* info_card = settings_card(NULL);
    if (info_card) {
        rx_text_view* info = settings_label(
            "Clock synchronization with network time servers "
            "will be available in a future update.", true);
        if (info) {
            text_view_set_font_size(info, 13.0f);
            view_add_child(info_card, (rx_view*)info);
        }
        view_add_child(scroll, info_card);
    }
    
    return scroll;
}
