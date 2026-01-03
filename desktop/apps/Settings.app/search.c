/*
 * NeolyxOS Settings.app - Search Functionality
 * 
 * Search across all settings panels.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: LicenseRef-KetiveeAI-Proprietary
 */

#include <stdint.h>
#include <stddef.h>
#include "settings.h"

/* ============ Search Index ============ */

typedef struct {
    const char *keyword;
    const char *title;
    const char *description;
    panel_id_t panel;
    const char *sub_page;
} search_entry_t;

/* Search index - keywords mapped to settings */
static const search_entry_t search_index[] = {
    /* System */
    { "hostname",    "Computer Name",       "Set device hostname",         PANEL_SYSTEM, NULL },
    { "timezone",    "Date & Time",         "Set timezone and clock",      PANEL_SYSTEM, "datetime" },
    { "language",    "Language & Region",   "Set system language",         PANEL_SYSTEM, "language" },
    
    /* Network */
    { "wifi",        "Wi-Fi",               "Wireless network settings",   PANEL_NETWORK, "wifi" },
    { "ethernet",    "Ethernet",            "Wired network settings",      PANEL_NETWORK, "ethernet" },
    { "proxy",       "Proxy",               "Network proxy configuration", PANEL_NETWORK, "proxy" },
    { "vpn",         "VPN",                 "VPN connections",             PANEL_NETWORK, "vpn" },
    { "dns",         "DNS",                 "DNS server settings",         PANEL_NETWORK, "dns" },
    
    /* Display */
    { "resolution",  "Resolution",          "Screen resolution",           PANEL_DISPLAY, NULL },
    { "brightness",  "Brightness",          "Display brightness",          PANEL_DISPLAY, NULL },
    { "night",       "Night Shift",         "Reduce blue light",           PANEL_DISPLAY, "nightshift" },
    { "scale",       "Display Scale",       "UI scaling",                  PANEL_DISPLAY, NULL },
    
    /* Sound */
    { "volume",      "Volume",              "System volume",               PANEL_SOUND, NULL },
    { "audio",       "Audio Output",        "Output device selection",     PANEL_SOUND, "output" },
    { "microphone",  "Microphone",          "Input device selection",      PANEL_SOUND, "input" },
    
    /* Security */
    { "firewall",    "Firewall",            "Network firewall",            PANEL_SECURITY, "firewall" },
    { "password",    "Password",            "Account password",            PANEL_SECURITY, "password" },
    { "encryption",  "FileVault",           "Disk encryption",             PANEL_SECURITY, "filevault" },
    
    /* Privacy */
    { "location",    "Location Services",   "App location access",         PANEL_PRIVACY, "location" },
    { "camera",      "Camera",              "App camera access",           PANEL_PRIVACY, "camera" },
    { "microphone",  "Microphone",          "App microphone access",       PANEL_PRIVACY, "microphone" },
    
    /* Appearance */
    { "theme",       "Theme",               "Light/Dark mode",             PANEL_APPEARANCE, NULL },
    { "accent",      "Accent Color",        "System accent color",         PANEL_APPEARANCE, "accent" },
    { "wallpaper",   "Wallpaper",           "Desktop wallpaper",           PANEL_APPEARANCE, "wallpaper" },
    { "dock",        "Dock",                "Dock settings",               PANEL_APPEARANCE, "dock" },
    
    /* Power */
    { "battery",     "Battery",             "Battery status",              PANEL_POWER, NULL },
    { "sleep",       "Sleep",               "Sleep settings",              PANEL_POWER, "sleep" },
    { "shutdown",    "Shutdown",            "Power options",               PANEL_POWER, NULL },
    
    /* Updates */
    { "update",      "Software Update",     "System updates",              PANEL_UPDATES, NULL },
    { "beta",        "Beta Updates",        "Beta program",                PANEL_UPDATES, "beta" },
    
    /* Developer */
    { "debug",       "Debug Mode",          "Enable debugging",            PANEL_DEVELOPER, NULL },
    { "terminal",    "Terminal",            "Terminal settings",           PANEL_DEVELOPER, "terminal" },
    
    /* About */
    { "version",     "Version",             "OS version info",             PANEL_ABOUT, NULL },
    { "serial",      "Serial Number",       "Device serial",               PANEL_ABOUT, NULL },
};

#define SEARCH_INDEX_SIZE (sizeof(search_index) / sizeof(search_index[0]))

/* ============ Search Functions ============ */

static int str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        
        while (*h && *n) {
            char hc = (*h >= 'A' && *h <= 'Z') ? *h + 32 : *h;
            char nc = (*n >= 'A' && *n <= 'Z') ? *n + 32 : *n;
            if (hc != nc) break;
            h++; n++;
        }
        
        if (!*n) return 1;  /* Found */
        haystack++;
    }
    
    return 0;
}

void settings_search_init(settings_ctx_t *ctx) {
    ctx->search.query[0] = '\0';
    ctx->search.result_count = 0;
    ctx->search.active = 0;
}

void settings_search_query(settings_ctx_t *ctx, const char *query) {
    if (!query || !query[0]) {
        settings_search_clear(ctx);
        return;
    }
    
    /* Copy query */
    int i = 0;
    while (query[i] && i < 255) {
        ctx->search.query[i] = query[i];
        i++;
    }
    ctx->search.query[i] = '\0';
    ctx->search.active = 1;
    ctx->search.result_count = 0;
    
    /* Search through index */
    for (int j = 0; j < (int)SEARCH_INDEX_SIZE && ctx->search.result_count < MAX_SEARCH_RESULTS; j++) {
        const search_entry_t *entry = &search_index[j];
        
        int score = 0;
        
        /* Check keyword match */
        if (str_contains(entry->keyword, query)) {
            score += 100;
        }
        
        /* Check title match */
        if (str_contains(entry->title, query)) {
            score += 50;
        }
        
        /* Check description match */
        if (str_contains(entry->description, query)) {
            score += 25;
        }
        
        if (score > 0) {
            search_result_t *result = &ctx->search.results[ctx->search.result_count++];
            result->title = entry->title;
            result->description = entry->description;
            result->panel = entry->panel;
            result->sub_page = entry->sub_page;
            result->score = score;
        }
    }
    
    /* Sort by score (simple bubble sort) */
    for (int a = 0; a < ctx->search.result_count - 1; a++) {
        for (int b = a + 1; b < ctx->search.result_count; b++) {
            if (ctx->search.results[b].score > ctx->search.results[a].score) {
                search_result_t temp = ctx->search.results[a];
                ctx->search.results[a] = ctx->search.results[b];
                ctx->search.results[b] = temp;
            }
        }
    }
}

void settings_search_clear(settings_ctx_t *ctx) {
    ctx->search.query[0] = '\0';
    ctx->search.result_count = 0;
    ctx->search.active = 0;
}
