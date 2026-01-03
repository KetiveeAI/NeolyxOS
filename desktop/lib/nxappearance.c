/*
 * NeolyxOS Appearance Preferences Implementation
 * 
 * Simple in-memory preferences with defaults.
 * File persistence to be added when filesystem is available.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxappearance.h"
#include <stddef.h>

/* Local memory copy - no libc in freestanding userspace */
static void app_memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

/* Global appearance settings */
static appearance_settings_t g_appearance = APPEARANCE_DEFAULTS;
static int g_initialized = 0;

/* Get current settings */
appearance_settings_t* nxappearance_get(void) {
    if (!g_initialized) {
        nxappearance_load(&g_appearance);
    }
    return &g_appearance;
}

/* Load settings - uses defaults for now */
int nxappearance_load(appearance_settings_t *out) {
    if (!out) return -1;
    
    /* Initialize with defaults */
    appearance_settings_t defaults = APPEARANCE_DEFAULTS;
    app_memcpy(out, &defaults, sizeof(appearance_settings_t));
    
    g_initialized = 1;
    
    /* TODO: Load from /System/Preferences/appearance.conf when filesystem available */
    
    return 0;
}

/* Save settings - stub for now */
int nxappearance_save(const appearance_settings_t *settings) {
    if (!settings) return -1;
    
    /* Copy to global */
    app_memcpy(&g_appearance, settings, sizeof(appearance_settings_t));
    
    /* TODO: Save to /System/Preferences/appearance.conf when filesystem available */
    
    return 0;
}

/* Apply settings - copy to global and update desktop */
void nxappearance_apply(const appearance_settings_t *settings) {
    if (!settings) return;
    
    app_memcpy(&g_appearance, settings, sizeof(appearance_settings_t));
    
    /* TODO: Send signal to desktop to reload appearance */
}
