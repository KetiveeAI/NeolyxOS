/*
 * NeolyxOS Centralized Configuration Implementation
 * 
 * Parses .nlc config files and provides runtime config values.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include "nx_config.h"

/* Global config instance */
nx_config_t g_nx_config = {0};

/* ============================================================================
 * Freestanding String Helpers (no stdlib dependency)
 * ============================================================================ */

static int cfg_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static char* cfg_strchr(const char *s, int c) {
    if (!s) return (char*)0;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (char*)0;
}

/* ============================================================================
 * NLC Parser Helpers
 * ============================================================================ */

/* Simple string comparison */
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Copy string safely */
static void str_copy(char *dst, const char *src, int max) {
    if (!dst || max <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Parse hex color from string like "0x007AFF" or "0xFF007AFF" */
static uint32_t parse_hex_color(const char *str) {
    if (!str) return 0;
    
    /* Skip 0x prefix if present */
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    
    uint32_t result = 0;
    for (int i = 0; str[i] && i < 8; i++) {
        char c = str[i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else break;
        result = (result << 4) | digit;
    }
    
    /* If only 6 hex digits (no alpha), add full alpha */
    if (result <= 0xFFFFFF) {
        result |= 0xFF000000;
    }
    
    return result;
}

/* Parse integer from string */
static int parse_int(const char *str) {
    if (!str) return 0;
    int result = 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result * sign;
}

/* Parse float from string */
static float parse_float(const char *str) {
    if (!str) return 0.0f;
    float result = 0.0f;
    float div = 1.0f;
    int decimal = 0;
    int sign = 1;
    
    if (*str == '-') { sign = -1; str++; }
    
    while (*str) {
        if (*str == '.') { decimal = 1; str++; continue; }
        if (*str < '0' || *str > '9') break;
        if (decimal) {
            div *= 10.0f;
            result += (*str - '0') / div;
        } else {
            result = result * 10.0f + (*str - '0');
        }
        str++;
    }
    return result * sign;
}

/* Parse boolean from string */
static bool parse_bool(const char *str) {
    if (!str) return false;
    return str_eq(str, "true") || str_eq(str, "1") || str_eq(str, "yes");
}

/* ============================================================================
 * Default Config Initialization
 * ============================================================================ */

static void nx_config_set_defaults(void) {
    /* Colors */
    g_nx_config.colors.accent = NX_DEFAULT_ACCENT;
    g_nx_config.colors.accent_hover = 0xFF0A84FF;
    g_nx_config.colors.glass_bg = NX_DEFAULT_GLASS_BG;
    g_nx_config.colors.navbar_bg = 0xFF101018;
    g_nx_config.colors.dock_bg = 0xC0202030;
    g_nx_config.colors.text = NX_DEFAULT_TEXT;
    g_nx_config.colors.text_dim = NX_DEFAULT_TEXT_DIM;
    g_nx_config.colors.surface = 0xFF2C2C2E;
    g_nx_config.colors.border = 0xFF52525B;
    g_nx_config.colors.success = 0xFF22C55E;
    g_nx_config.colors.warning = 0xFFFBBF24;
    g_nx_config.colors.error = 0xFFEF4444;
    
    /* Locale */
    str_copy(g_nx_config.locale.timezone_name, "Asia/Kolkata", 64);
    g_nx_config.locale.offset_minutes = NX_DEFAULT_TZ_OFFSET;
    g_nx_config.locale.time_24h = true;
    str_copy(g_nx_config.locale.date_format, "DD/MM/YYYY", 16);
    g_nx_config.locale.first_day_of_week = 1; /* Monday */
    str_copy(g_nx_config.locale.language, "en_IN", 8);
    
    /* UI */
    g_nx_config.ui.scale = NX_DEFAULT_SCALE;
    g_nx_config.ui.navbar_height = NX_DEFAULT_NAVBAR_H;
    g_nx_config.ui.dock_height = NX_DEFAULT_DOCK_H;
    g_nx_config.ui.dock_icon_size = 48;
    g_nx_config.ui.widget_radius = 12;
    g_nx_config.ui.titlebar_height = 28;
    g_nx_config.ui.target_fps = NX_DEFAULT_TARGET_FPS;
    g_nx_config.ui.animations_enabled = true;
    str_copy(g_nx_config.ui.animation_speed, "normal", 16);
    
    /* Mouse */
    g_nx_config.mouse.tracking_speed = 5;
    g_nx_config.mouse.smooth_factor = 1;
    g_nx_config.mouse.natural_scrolling = false;
    g_nx_config.mouse.double_click_ms = 500;
    str_copy(g_nx_config.mouse.cursor_size, "normal", 16);
    
    /* Keyboard */
    g_nx_config.keyboard.repeat_rate = 30;
    g_nx_config.keyboard.repeat_delay_ms = 400;
    g_nx_config.keyboard.caps_lock_osd = true;
    g_nx_config.keyboard.num_lock_osd = true;
    
    /* Security */
    g_nx_config.security.firewall_enabled = NX_DEFAULT_FIREWALL;
    g_nx_config.security.default_input_policy = 0;  /* ALLOW */
    g_nx_config.security.default_output_policy = 0; /* ALLOW */
    g_nx_config.security.rate_limit_enabled = true;
    g_nx_config.security.rate_limit_pps = NX_DEFAULT_RATE_LIMIT;
    g_nx_config.security.rate_limit_burst = NX_DEFAULT_RATE_BURST;
    g_nx_config.security.conn_tracking_enabled = true;
    g_nx_config.security.conn_timeout_tcp = NX_DEFAULT_CONN_TCP_TO;
    g_nx_config.security.conn_timeout_udp = NX_DEFAULT_CONN_UDP_TO;
    g_nx_config.security.ids_enabled = NX_DEFAULT_IDS_ENABLED;
    g_nx_config.security.ids_auto_block = false;
    g_nx_config.security.ids_sensitivity = NX_DEFAULT_IDS_SENS;
    g_nx_config.security.scan_protection_enabled = true;
    g_nx_config.security.scan_threshold_ports = NX_DEFAULT_SCAN_PORTS;
    g_nx_config.security.scan_window_seconds = NX_DEFAULT_SCAN_WINDOW;
    g_nx_config.security.syn_cookies_enabled = true;
    g_nx_config.security.spoofing_protection = true;
    g_nx_config.security.icmp_rate_limit = NX_DEFAULT_ICMP_LIMIT;
}

/* ============================================================================
 * NLC File Parser
 * Simple line-by-line parser for .nlc config format
 * ============================================================================ */

/* Trim whitespace from string */
static char* str_trim(char *str) {
    if (!str) return str;
    while (*str == ' ' || *str == '\t') str++;
    char *end = str + cfg_strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end-- = '\0';
    }
    return str;
}

/* Remove quotes from value */
static char* str_unquote(char *str) {
    if (!str) return str;
    int len = cfg_strlen(str);
    if (len >= 2 && str[0] == '"' && str[len-1] == '"') {
        str[len-1] = '\0';
        return str + 1;
    }
    return str;
}

/* Parse a single .nlc file into config struct */
typedef void (*nlc_handler_fn)(const char *section, const char *key, const char *value);

static void nlc_parse_buffer(const char *buffer, nlc_handler_fn handler) {
    if (!buffer || !handler) return;
    
    char section[64] = "";
    char line[256];
    int line_idx = 0;
    
    while (*buffer) {
        /* Read line */
        line_idx = 0;
        while (*buffer && *buffer != '\n' && line_idx < 255) {
            line[line_idx++] = *buffer++;
        }
        line[line_idx] = '\0';
        if (*buffer == '\n') buffer++;
        
        /* Trim and skip empty/comments */
        char *trimmed = str_trim(line);
        if (!trimmed[0] || trimmed[0] == '#') continue;
        
        /* Section header [name] */
        if (trimmed[0] == '[') {
            char *end = cfg_strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                str_copy(section, trimmed + 1, 64);
            }
            continue;
        }
        
        /* Key = value */
        char *eq = cfg_strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = str_trim(trimmed);
            char *value = str_trim(eq + 1);
            value = str_unquote(value);
            handler(section, key, value);
        }
    }
}

/* ============================================================================
 * Config Handlers
 * ============================================================================ */

static void handle_locale(const char *section, const char *key, const char *value) {
    if (str_eq(section, "timezone")) {
        if (str_eq(key, "name")) {
            str_copy(g_nx_config.locale.timezone_name, value, 64);
        } else if (str_eq(key, "offset_minutes")) {
            g_nx_config.locale.offset_minutes = (int16_t)parse_int(value);
        }
    } else if (str_eq(section, "time")) {
        if (str_eq(key, "format")) {
            g_nx_config.locale.time_24h = str_eq(value, "24h");
        }
    } else if (str_eq(section, "date")) {
        if (str_eq(key, "format")) {
            str_copy(g_nx_config.locale.date_format, value, 16);
        } else if (str_eq(key, "first_day_of_week")) {
            g_nx_config.locale.first_day_of_week = str_eq(value, "sunday") ? 0 : 1;
        }
    } else if (str_eq(section, "region")) {
        if (str_eq(key, "language")) {
            str_copy(g_nx_config.locale.language, value, 8);
        }
    }
}

static void handle_theme(const char *section, const char *key, const char *value) {
    if (str_eq(section, "colors")) {
        if (str_eq(key, "accent")) {
            g_nx_config.colors.accent = parse_hex_color(value);
        } else if (str_eq(key, "accent_hover")) {
            g_nx_config.colors.accent_hover = parse_hex_color(value);
        } else if (str_eq(key, "glass_bg")) {
            g_nx_config.colors.glass_bg = parse_hex_color(value);
        } else if (str_eq(key, "navbar_bg")) {
            g_nx_config.colors.navbar_bg = parse_hex_color(value);
        } else if (str_eq(key, "dock_bg")) {
            g_nx_config.colors.dock_bg = parse_hex_color(value);
        } else if (str_eq(key, "text")) {
            g_nx_config.colors.text = parse_hex_color(value);
        } else if (str_eq(key, "text_dim")) {
            g_nx_config.colors.text_dim = parse_hex_color(value);
        } else if (str_eq(key, "surface")) {
            g_nx_config.colors.surface = parse_hex_color(value);
        } else if (str_eq(key, "border")) {
            g_nx_config.colors.border = parse_hex_color(value);
        } else if (str_eq(key, "success")) {
            g_nx_config.colors.success = parse_hex_color(value);
        } else if (str_eq(key, "warning")) {
            g_nx_config.colors.warning = parse_hex_color(value);
        } else if (str_eq(key, "error")) {
            g_nx_config.colors.error = parse_hex_color(value);
        }
    } else if (str_eq(section, "ui")) {
        if (str_eq(key, "scale")) {
            g_nx_config.ui.scale = parse_float(value);
        } else if (str_eq(key, "navbar_height")) {
            g_nx_config.ui.navbar_height = (uint16_t)parse_int(value);
        } else if (str_eq(key, "dock_height")) {
            g_nx_config.ui.dock_height = (uint16_t)parse_int(value);
        } else if (str_eq(key, "dock_icon_size")) {
            g_nx_config.ui.dock_icon_size = (uint16_t)parse_int(value);
        } else if (str_eq(key, "widget_radius")) {
            g_nx_config.ui.widget_radius = (uint16_t)parse_int(value);
        } else if (str_eq(key, "titlebar_height")) {
            g_nx_config.ui.titlebar_height = (uint16_t)parse_int(value);
        } else if (str_eq(key, "target_fps")) {
            g_nx_config.ui.target_fps = (uint16_t)parse_int(value);
        }
    } else if (str_eq(section, "animation")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.ui.animations_enabled = parse_bool(value);
        } else if (str_eq(key, "speed")) {
            str_copy(g_nx_config.ui.animation_speed, value, 16);
        }
    }
}

static void handle_user_defaults(const char *section, const char *key, const char *value) {
    if (str_eq(section, "mouse")) {
        if (str_eq(key, "tracking_speed")) {
            g_nx_config.mouse.tracking_speed = (uint8_t)parse_int(value);
        } else if (str_eq(key, "natural_scrolling")) {
            g_nx_config.mouse.natural_scrolling = parse_bool(value);
        } else if (str_eq(key, "double_click_speed")) {
            g_nx_config.mouse.double_click_ms = (uint16_t)parse_int(value);
        } else if (str_eq(key, "cursor_size")) {
            str_copy(g_nx_config.mouse.cursor_size, value, 16);
        }
    } else if (str_eq(section, "keyboard")) {
        if (str_eq(key, "key_repeat_rate")) {
            g_nx_config.keyboard.repeat_rate = (uint16_t)parse_int(value);
        } else if (str_eq(key, "key_repeat_delay")) {
            g_nx_config.keyboard.repeat_delay_ms = (uint16_t)parse_int(value);
        }
    }
}

/* ============================================================================
 * Embedded Config Fallbacks
 * When running in early boot or ramfs, use embedded defaults
 * ============================================================================ */

static const char *embedded_locale_nlc = 
    "[timezone]\n"
    "name = \"Asia/Kolkata\"\n"
    "offset_minutes = \"330\"\n"
    "[time]\n"
    "format = \"24h\"\n"
    "[date]\n"
    "format = \"DD/MM/YYYY\"\n"
    "first_day_of_week = \"monday\"\n"
    "[region]\n"
    "language = \"en_IN\"\n";

static const char *embedded_theme_nlc =
    "[colors]\n"
    "accent = \"0x007AFF\"\n"
    "glass_bg = \"0xE0303050\"\n"
    "text = \"0xFFFFFFFF\"\n"
    "text_dim = \"0xFFA1A1AA\"\n"
    "[ui]\n"
    "scale = \"1.0\"\n"
    "navbar_height = \"28\"\n"
    "dock_height = \"64\"\n"
    "target_fps = \"60\"\n"
    "[animation]\n"
    "enabled = \"true\"\n"
    "speed = \"normal\"\n";

static const char *embedded_security_nlc =
    "[firewall]\n"
    "enabled = \"true\"\n"
    "default_input_policy = \"allow\"\n"
    "default_output_policy = \"allow\"\n"
    "[rate_limit]\n"
    "enabled = \"true\"\n"
    "packets_per_second = \"1000\"\n"
    "burst = \"50\"\n"
    "[connection_tracking]\n"
    "enabled = \"true\"\n"
    "tcp_timeout = \"300\"\n"
    "udp_timeout = \"60\"\n"
    "[ids]\n"
    "enabled = \"true\"\n"
    "auto_block = \"false\"\n"
    "sensitivity = \"medium\"\n"
    "[scan_protection]\n"
    "enabled = \"true\"\n"
    "threshold_ports = \"10\"\n"
    "window_seconds = \"60\"\n"
    "[tcp_hardening]\n"
    "syn_cookies = \"true\"\n"
    "spoofing_protection = \"true\"\n"
    "icmp_rate_limit = \"10\"\n";

static void handle_security(const char *section, const char *key, const char *value) {
    if (str_eq(section, "firewall")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.security.firewall_enabled = parse_bool(value);
        } else if (str_eq(key, "default_input_policy")) {
            g_nx_config.security.default_input_policy = str_eq(value, "deny") ? 1 : 0;
        } else if (str_eq(key, "default_output_policy")) {
            g_nx_config.security.default_output_policy = str_eq(value, "deny") ? 1 : 0;
        }
    } else if (str_eq(section, "rate_limit")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.security.rate_limit_enabled = parse_bool(value);
        } else if (str_eq(key, "packets_per_second")) {
            g_nx_config.security.rate_limit_pps = (uint32_t)parse_int(value);
        } else if (str_eq(key, "burst")) {
            g_nx_config.security.rate_limit_burst = (uint16_t)parse_int(value);
        }
    } else if (str_eq(section, "connection_tracking")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.security.conn_tracking_enabled = parse_bool(value);
        } else if (str_eq(key, "tcp_timeout")) {
            g_nx_config.security.conn_timeout_tcp = (uint16_t)parse_int(value);
        } else if (str_eq(key, "udp_timeout")) {
            g_nx_config.security.conn_timeout_udp = (uint16_t)parse_int(value);
        }
    } else if (str_eq(section, "ids")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.security.ids_enabled = parse_bool(value);
        } else if (str_eq(key, "auto_block")) {
            g_nx_config.security.ids_auto_block = parse_bool(value);
        } else if (str_eq(key, "sensitivity")) {
            if (str_eq(value, "low")) g_nx_config.security.ids_sensitivity = 1;
            else if (str_eq(value, "high")) g_nx_config.security.ids_sensitivity = 3;
            else g_nx_config.security.ids_sensitivity = 2; /* medium default */
        }
    } else if (str_eq(section, "scan_protection")) {
        if (str_eq(key, "enabled")) {
            g_nx_config.security.scan_protection_enabled = parse_bool(value);
        } else if (str_eq(key, "threshold_ports")) {
            g_nx_config.security.scan_threshold_ports = (uint8_t)parse_int(value);
        } else if (str_eq(key, "window_seconds")) {
            g_nx_config.security.scan_window_seconds = (uint16_t)parse_int(value);
        }
    } else if (str_eq(section, "tcp_hardening")) {
        if (str_eq(key, "syn_cookies")) {
            g_nx_config.security.syn_cookies_enabled = parse_bool(value);
        } else if (str_eq(key, "spoofing_protection")) {
            g_nx_config.security.spoofing_protection = parse_bool(value);
        } else if (str_eq(key, "icmp_rate_limit")) {
            g_nx_config.security.icmp_rate_limit = (uint16_t)parse_int(value);
        }
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int nx_config_init(void) {
    /* Set defaults first */
    nx_config_set_defaults();
    
    /* Parse embedded config (always available) */
    nlc_parse_buffer(embedded_locale_nlc, handle_locale);
    nlc_parse_buffer(embedded_theme_nlc, handle_theme);
    nlc_parse_buffer(embedded_security_nlc, handle_security);
    
    /* TODO: Load from filesystem when VFS is ready */
    /* For now, embedded config is the source of truth */
    
    g_nx_config.loaded = true;
    return 0;
}

int nx_config_reload(void) {
    /* Re-parse from embedded (or filesystem when available) */
    nlc_parse_buffer(embedded_locale_nlc, handle_locale);
    nlc_parse_buffer(embedded_theme_nlc, handle_theme);
    nlc_parse_buffer(embedded_security_nlc, handle_security);
    return 0;
}

int nx_config_save_locale(void) {
    /* TODO: Write to /System/Config/locale.nlc */
    return 0;
}

int nx_config_save_theme(void) {
    /* TODO: Write to /System/Config/theme.nlc */
    return 0;
}

int nx_config_save_security(void) {
    /* TODO: Write to /System/Config/security.nlc */
    return 0;
}

int nx_config_save_user_defaults(void) {
    /* TODO: Write to user preferences */
    return 0;
}

