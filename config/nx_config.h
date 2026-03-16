/*
 * NeolyxOS Centralized Configuration API
 * 
 * Single source of truth for all runtime config values.
 * Settings.app panels modify these, desktop_shell reads them.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef NX_CONFIG_H
#define NX_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Color Configuration
 * ============================================================================ */

typedef struct {
    uint32_t accent;          /* Primary accent (buttons, highlights) */
    uint32_t accent_hover;    /* Accent on hover */
    uint32_t glass_bg;        /* Glass/blur overlay background */
    uint32_t navbar_bg;       /* Navigation bar background */
    uint32_t dock_bg;         /* Dock background */
    uint32_t text;            /* Primary text color */
    uint32_t text_dim;        /* Secondary/dimmed text */
    uint32_t surface;         /* Card/panel surfaces */
    uint32_t border;          /* Borders and dividers */
    uint32_t success;         /* Success indicators */
    uint32_t warning;         /* Warning indicators */
    uint32_t error;           /* Error indicators */
} nx_colorset_t;

/* ============================================================================
 * Timezone / Locale Configuration
 * ============================================================================ */

typedef struct {
    char timezone_name[64];       /* e.g. "Asia/Kolkata" */
    int16_t offset_minutes;       /* UTC offset in minutes (e.g. 330 for IST) */
    bool time_24h;                /* true = 24h, false = 12h AM/PM */
    char date_format[16];         /* "DD/MM/YYYY", "MM/DD/YYYY", etc. */
    uint8_t first_day_of_week;    /* 0=Sunday, 1=Monday, etc. */
    char language[8];             /* e.g. "en_IN" */
} nx_locale_t;

/* ============================================================================
 * UI Dimensions Configuration
 * ============================================================================ */

typedef struct {
    float scale;              /* UI scale factor (1.0 = 100%) */
    uint16_t navbar_height;   /* Navigation bar height in pixels */
    uint16_t dock_height;     /* Dock height in pixels */
    uint16_t dock_icon_size;  /* Dock icon size */
    uint16_t widget_radius;   /* Default corner radius */
    uint16_t titlebar_height; /* Window title bar height */
    uint16_t target_fps;      /* Target frame rate */
    bool animations_enabled;  /* Enable/disable animations */
    char animation_speed[16]; /* "slow", "normal", "fast" */
} nx_ui_config_t;

/* ============================================================================
 * Network Security Configuration
 * ============================================================================ */

typedef struct {
    /* Firewall */
    bool firewall_enabled;
    uint8_t default_input_policy;   /* 0=ALLOW, 1=DENY */
    uint8_t default_output_policy;  /* 0=ALLOW, 1=DENY */
    
    /* Rate Limiting */
    bool rate_limit_enabled;
    uint32_t rate_limit_pps;        /* Max packets per second per IP */
    uint16_t rate_limit_burst;      /* Burst allowance */
    
    /* Connection Tracking */
    bool conn_tracking_enabled;
    uint16_t conn_timeout_tcp;      /* TCP idle timeout (seconds) */
    uint16_t conn_timeout_udp;      /* UDP timeout (seconds) */
    
    /* Intrusion Detection */
    bool ids_enabled;
    bool ids_auto_block;            /* Auto-block detected threats */
    uint8_t ids_sensitivity;        /* 1=low, 2=medium, 3=high */
    
    /* Port Scan Protection */
    bool scan_protection_enabled;
    uint8_t scan_threshold_ports;   /* Ports in window = scan */
    uint16_t scan_window_seconds;   /* Time window */
    
    /* TCP Hardening */
    bool syn_cookies_enabled;
    bool spoofing_protection;
    uint16_t icmp_rate_limit;       /* ICMP per second (0=unlimited) */
} nx_security_config_t;

/* ============================================================================
 * Input Configuration
 * ============================================================================ */

typedef struct {
    uint8_t tracking_speed;   /* 1-10 mouse speed */
    uint8_t smooth_factor;    /* Mouse smoothing (0 = off) */
    bool natural_scrolling;
    uint16_t double_click_ms; /* Double-click interval */
    char cursor_size[16];     /* "small", "normal", "large" */
} nx_mouse_config_t;

typedef struct {
    uint16_t repeat_rate;     /* Keys per second */
    uint16_t repeat_delay_ms; /* Initial delay before repeat */
    bool caps_lock_osd;       /* Show OSD on Caps Lock */
    bool num_lock_osd;        /* Show OSD on Num Lock */
} nx_keyboard_config_t;

/* ============================================================================
 * Master Config Structure
 * ============================================================================ */

typedef struct {
    nx_colorset_t colors;
    nx_locale_t locale;
    nx_ui_config_t ui;
    nx_security_config_t security;
    nx_mouse_config_t mouse;
    nx_keyboard_config_t keyboard;
    
    /* Metadata */
    bool loaded;
    uint64_t last_modified;
} nx_config_t;

/* ============================================================================
 * Global Config Instance
 * ============================================================================ */

extern nx_config_t g_nx_config;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/* Initialize config system, load all .nlc files */
int nx_config_init(void);

/* Reload config from disk (after Settings.app changes) */
int nx_config_reload(void);

/* Save specific config section */
int nx_config_save_locale(void);
int nx_config_save_theme(void);
int nx_config_save_security(void);
int nx_config_save_user_defaults(void);

/* Get timezone offset in minutes (convenience) */
static inline int16_t nx_config_tz_offset(void) {
    return g_nx_config.locale.offset_minutes;
}

/* Get accent color (convenience) */
static inline uint32_t nx_config_accent(void) {
    return g_nx_config.colors.accent;
}

/* Get UI scale (convenience) */
static inline float nx_config_scale(void) {
    return g_nx_config.ui.scale;
}

/* Get firewall state (convenience) */
static inline bool nx_config_firewall_enabled(void) {
    return g_nx_config.security.firewall_enabled;
}

/* Scale a pixel value by UI scale */
static inline int nx_scale(int pixels) {
    return (int)(pixels * g_nx_config.ui.scale);
}

/* ============================================================================
 * Config File Paths
 * ============================================================================ */

#define NX_CONFIG_DIR        "/System/Config"
#define NX_CONFIG_SYSTEM     NX_CONFIG_DIR "/system.nlc"
#define NX_CONFIG_LOCALE     NX_CONFIG_DIR "/locale.nlc"
#define NX_CONFIG_THEME      NX_CONFIG_DIR "/theme.nlc"
#define NX_CONFIG_SECURITY   NX_CONFIG_DIR "/security.nlc"
#define NX_CONFIG_USER       NX_CONFIG_DIR "/user_defaults.nlc"

/* ============================================================================
 * Default Values (fallback if config missing)
 * ============================================================================ */

/* UI Defaults */
#define NX_DEFAULT_TZ_OFFSET      330       /* IST = +5:30 */
#define NX_DEFAULT_ACCENT         0xFF007AFF
#define NX_DEFAULT_GLASS_BG       0xE0303050
#define NX_DEFAULT_TEXT           0xFFFFFFFF
#define NX_DEFAULT_TEXT_DIM       0xFFA1A1AA
#define NX_DEFAULT_NAVBAR_H       28
#define NX_DEFAULT_DOCK_H         64
#define NX_DEFAULT_SCALE          1.0f
#define NX_DEFAULT_TARGET_FPS     60

/* Security Defaults */
#define NX_DEFAULT_FIREWALL       true
#define NX_DEFAULT_RATE_LIMIT     1000      /* pps per IP */
#define NX_DEFAULT_RATE_BURST     50        /* burst allowance */
#define NX_DEFAULT_CONN_TCP_TO    300       /* 5 minutes */
#define NX_DEFAULT_CONN_UDP_TO    60        /* 1 minute */
#define NX_DEFAULT_IDS_ENABLED    true
#define NX_DEFAULT_IDS_SENS       2         /* medium */
#define NX_DEFAULT_SCAN_PORTS     10        /* ports in window */
#define NX_DEFAULT_SCAN_WINDOW    60        /* seconds */
#define NX_DEFAULT_ICMP_LIMIT     10        /* per second */

#endif /* NX_CONFIG_H */
