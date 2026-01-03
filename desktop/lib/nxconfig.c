/*
 * NeolyxOS Configuration System Implementation
 * 
 * Parses .nlc (INI-style) config files for desktop shell.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxconfig.h"
#include "../include/nxsyscall.h"
#include <stddef.h>

/* ============ String Helpers ============ */

static int nxc_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void nxc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int nxc_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int nxc_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n && *a && *b; i++, a++, b++) {
        if (*a != *b) return *a - *b;
    }
    return 0;
}

static void nxc_trim(char *s) {
    /* Trim leading whitespace */
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) {
        char *dst = s;
        while (*p) *dst++ = *p++;
        *dst = '\0';
    }
    /* Trim trailing whitespace and newlines */
    int len = nxc_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || 
                        s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

static int nxc_atoi(const char *s) {
    int result = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

static uint32_t nxc_parse_hex(const char *s) {
    uint32_t result = 0;
    /* Skip "0x" or "0X" prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (*s) {
        char c = *s++;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') digit = 10 + c - 'A';
        else break;
        result = (result << 4) | digit;
    }
    return result;
}

/* ============ Config Loading ============ */

/* Simple static config storage (no malloc in freestanding) */
static nxc_config_t g_configs[4];
static int g_config_count = 0;

nxc_config_t* nxc_load(const char *path) {
    if (g_config_count >= 4) return NULL;
    
    nxc_config_t *config = &g_configs[g_config_count++];
    nxc_strcpy(config->filepath, path, 256);
    config->section_count = 0;
    config->modified = 0;
    
    /* For now, load default values since we can't read files in freestanding */
    /* TODO: Implement VFS read when available */
    
    /* Create default [desktop] section */
    nxc_section_t *sec = &config->sections[0];
    nxc_strcpy(sec->name, "desktop", NXC_MAX_KEY_LEN);
    sec->entry_count = 0;
    
    /* Add default desktop values */
    nxc_strcpy(sec->entries[sec->entry_count].key, "theme", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "dark", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "accent_color", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "0x0000B4D8", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "dock_position", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "bottom", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "menubar_transparency", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "true", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    config->section_count = 1;
    
    /* Create default [dock] section */
    sec = &config->sections[1];
    nxc_strcpy(sec->name, "dock", NXC_MAX_KEY_LEN);
    sec->entry_count = 0;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "icon_size", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "48", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "autohide", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "false", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "minimize_effect", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "scale", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "bounce_on_launch", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "true", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    config->section_count = 2;
    
    /* Create default [window] section */
    sec = &config->sections[2];
    nxc_strcpy(sec->name, "window", NXC_MAX_KEY_LEN);
    sec->entry_count = 0;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "animations_enabled", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "true", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "animation_speed", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "normal", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    nxc_strcpy(sec->entries[sec->entry_count].key, "snap_enabled", NXC_MAX_KEY_LEN);
    nxc_strcpy(sec->entries[sec->entry_count].value, "true", NXC_MAX_VALUE_LEN);
    sec->entry_count++;
    
    config->section_count = 3;
    
    return config;
}

int nxc_save(nxc_config_t *config) {
    if (!config) return -1;
    /* TODO: Implement VFS write when available */
    config->modified = 0;
    return 0;
}

void nxc_free(nxc_config_t *config) {
    /* Static storage, nothing to free */
    (void)config;
}

/* ============ Getters ============ */

static nxc_section_t* find_section(nxc_config_t *config, const char *section) {
    for (int i = 0; i < config->section_count; i++) {
        if (nxc_strcmp(config->sections[i].name, section) == 0) {
            return &config->sections[i];
        }
    }
    return NULL;
}

static nxc_entry_t* find_entry(nxc_section_t *sec, const char *key) {
    for (int i = 0; i < sec->entry_count; i++) {
        if (nxc_strcmp(sec->entries[i].key, key) == 0) {
            return &sec->entries[i];
        }
    }
    return NULL;
}

const char* nxc_get_string(nxc_config_t *config, const char *section, 
                            const char *key, const char *default_val) {
    if (!config) return default_val;
    nxc_section_t *sec = find_section(config, section);
    if (!sec) return default_val;
    nxc_entry_t *entry = find_entry(sec, key);
    if (!entry) return default_val;
    return entry->value;
}

int nxc_get_int(nxc_config_t *config, const char *section, 
                const char *key, int default_val) {
    const char *val = nxc_get_string(config, section, key, NULL);
    if (!val) return default_val;
    return nxc_atoi(val);
}

int nxc_get_bool(nxc_config_t *config, const char *section, 
                 const char *key, int default_val) {
    const char *val = nxc_get_string(config, section, key, NULL);
    if (!val) return default_val;
    return (nxc_strcmp(val, "true") == 0 || nxc_strcmp(val, "1") == 0 ||
            nxc_strcmp(val, "yes") == 0);
}

uint32_t nxc_get_color(nxc_config_t *config, const char *section, 
                       const char *key, uint32_t default_val) {
    const char *val = nxc_get_string(config, section, key, NULL);
    if (!val) return default_val;
    return nxc_parse_hex(val);
}

/* ============ Setters ============ */

int nxc_set_string(nxc_config_t *config, const char *section, 
                   const char *key, const char *value) {
    if (!config) return -1;
    
    nxc_section_t *sec = find_section(config, section);
    if (!sec) {
        /* Create new section */
        if (config->section_count >= NXC_MAX_SECTIONS) return -1;
        sec = &config->sections[config->section_count++];
        nxc_strcpy(sec->name, section, NXC_MAX_KEY_LEN);
        sec->entry_count = 0;
    }
    
    nxc_entry_t *entry = find_entry(sec, key);
    if (!entry) {
        /* Create new entry */
        if (sec->entry_count >= NXC_MAX_KEYS) return -1;
        entry = &sec->entries[sec->entry_count++];
        nxc_strcpy(entry->key, key, NXC_MAX_KEY_LEN);
    }
    
    nxc_strcpy(entry->value, value, NXC_MAX_VALUE_LEN);
    config->modified = 1;
    return 0;
}

int nxc_set_int(nxc_config_t *config, const char *section, 
                const char *key, int value) {
    char buf[32];
    int i = 0;
    int v = value < 0 ? -value : value;
    if (value < 0) buf[i++] = '-';
    int start = i;
    do {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    } while (v > 0);
    buf[i] = '\0';
    /* Reverse digits */
    for (int j = start; j < (start + i) / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j + start];
        buf[i - 1 - j + start] = tmp;
    }
    return nxc_set_string(config, section, key, buf);
}

int nxc_set_bool(nxc_config_t *config, const char *section, 
                 const char *key, int value) {
    return nxc_set_string(config, section, key, value ? "true" : "false");
}

int nxc_set_color(nxc_config_t *config, const char *section, 
                  const char *key, uint32_t color) {
    char buf[12] = "0x";
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(color >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
    return nxc_set_string(config, section, key, buf);
}

/* ============ Desktop Settings Helpers ============ */

int nxc_load_desktop_settings(nxc_desktop_settings_t *settings) {
    if (!settings) return -1;
    
    nxc_config_t *config = nxc_load(NXC_USER_DEFAULTS);
    if (!config) return -1;
    
    /* Desktop section */
    nxc_strcpy(settings->theme, 
               nxc_get_string(config, "desktop", "theme", "dark"), 32);
    settings->accent_color = nxc_get_color(config, "desktop", "accent_color", 0xFF0000B4);
    nxc_strcpy(settings->wallpaper,
               nxc_get_string(config, "desktop", "wallpaper", ""), 256);
    nxc_strcpy(settings->dock_position,
               nxc_get_string(config, "desktop", "dock_position", "bottom"), 16);
    settings->menubar_transparent = nxc_get_bool(config, "desktop", "menubar_transparency", 1);
    
    /* Dock section */
    settings->dock_icon_size = nxc_get_int(config, "dock", "icon_size", 48);
    settings->dock_autohide = nxc_get_bool(config, "dock", "autohide", 0);
    settings->dock_magnification = nxc_get_bool(config, "dock", "magnification", 0);
    nxc_strcpy(settings->minimize_effect,
               nxc_get_string(config, "dock", "minimize_effect", "scale"), 16);
    settings->bounce_on_launch = nxc_get_bool(config, "dock", "bounce_on_launch", 1);
    
    /* Window section */
    settings->animations_enabled = nxc_get_bool(config, "window", "animations_enabled", 1);
    nxc_strcpy(settings->animation_speed,
               nxc_get_string(config, "window", "animation_speed", "normal"), 16);
    settings->snap_enabled = nxc_get_bool(config, "window", "snap_enabled", 1);
    
    /* Mouse section */
    settings->cursor_speed = nxc_get_int(config, "mouse", "tracking_speed", 5);
    settings->natural_scrolling = nxc_get_bool(config, "mouse", "natural_scrolling", 0);
    nxc_strcpy(settings->cursor_size,
               nxc_get_string(config, "mouse", "cursor_size", "normal"), 16);
    
    return 0;
}

int nxc_save_desktop_settings(const nxc_desktop_settings_t *settings) {
    if (!settings) return -1;
    
    nxc_config_t *config = nxc_load(NXC_USER_DEFAULTS);
    if (!config) return -1;
    
    /* Desktop section */
    nxc_set_string(config, "desktop", "theme", settings->theme);
    nxc_set_color(config, "desktop", "accent_color", settings->accent_color);
    nxc_set_string(config, "desktop", "wallpaper", settings->wallpaper);
    nxc_set_string(config, "desktop", "dock_position", settings->dock_position);
    nxc_set_bool(config, "desktop", "menubar_transparency", settings->menubar_transparent);
    
    /* Dock section */
    nxc_set_int(config, "dock", "icon_size", settings->dock_icon_size);
    nxc_set_bool(config, "dock", "autohide", settings->dock_autohide);
    nxc_set_bool(config, "dock", "magnification", settings->dock_magnification);
    nxc_set_string(config, "dock", "minimize_effect", settings->minimize_effect);
    nxc_set_bool(config, "dock", "bounce_on_launch", settings->bounce_on_launch);
    
    /* Window section */
    nxc_set_bool(config, "window", "animations_enabled", settings->animations_enabled);
    nxc_set_string(config, "window", "animation_speed", settings->animation_speed);
    nxc_set_bool(config, "window", "snap_enabled", settings->snap_enabled);
    
    return nxc_save(config);
}

void nxc_apply_desktop_settings(const nxc_desktop_settings_t *settings) {
    /* TODO: Publish events to components when event bus is implemented */
    (void)settings;
}
