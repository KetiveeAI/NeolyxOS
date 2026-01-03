/*
 * NXBacklight - Laptop Display Backlight Driver
 * 
 * Controls internal panel brightness via:
 * - ACPI backlight interface (/sys/class/backlight)
 * - PWM controller (direct hardware)
 * - Intel/AMD specific interfaces
 * 
 * For LAPTOPS ONLY - eDP/LVDS internal displays
 * DDC/CI is for external monitors
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXBACKLIGHT_H
#define NXBACKLIGHT_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Backlight Types
 * ============================================================================ */

typedef enum {
    NXBL_TYPE_ACPI = 0,     /* ACPI backlight (most common) */
    NXBL_TYPE_PWM,          /* Direct PWM control */
    NXBL_TYPE_INTEL,        /* Intel graphics backlight */
    NXBL_TYPE_AMD,          /* AMD graphics backlight */
    NXBL_TYPE_NVIDIA,       /* NVIDIA Optimus backlight */
    NXBL_TYPE_PLATFORM,     /* Platform-specific (Dell, HP, Lenovo) */
} nxbl_type_t;

/* ============================================================================
 * Backlight Device
 * ============================================================================ */

typedef struct {
    char name[32];              /* "intel_backlight", "acpi_video0", etc */
    nxbl_type_t type;
    
    uint32_t brightness;        /* Current brightness level */
    uint32_t max_brightness;    /* Maximum brightness level */
    uint32_t min_brightness;    /* Minimum (usually 0 or 1) */
    
    /* Percentage interface (0-100) */
    uint8_t percent;
    
    /* Hardware info */
    bool is_internal;           /* Internal panel (not external) */
    bool supports_ambient;      /* Has ambient light sensor */
    
    /* Power state */
    bool panel_on;
    bool keyboard_backlight;    /* Has keyboard backlight */
} nxbl_device_t;

/* ============================================================================
 * Ambient Light Sensor
 * ============================================================================ */

typedef struct {
    bool available;
    uint32_t lux;               /* Current ambient light (lux) */
    uint32_t target_brightness; /* Suggested brightness based on ambient */
} nxbl_ambient_t;

/* ============================================================================
 * Keyboard Backlight
 * ============================================================================ */

typedef struct {
    bool available;
    uint8_t brightness;         /* 0-100 or 0-3 levels */
    uint8_t max_levels;         /* Number of brightness levels */
    bool auto_off;              /* Auto-off after idle */
    uint16_t timeout_seconds;   /* Idle timeout */
    uint32_t color;             /* RGB color (if RGB keyboard) */
    bool rgb_supported;
} nxkb_backlight_t;

/* ============================================================================
 * Backlight Driver API
 * ============================================================================ */

/**
 * Initialize backlight subsystem
 * Auto-detects laptop displays and backlight interface
 */
int nxbl_init(void);
void nxbl_shutdown(void);

/**
 * Check if running on a laptop with internal display
 */
int nxbl_is_laptop(void);

/**
 * Get backlight device info
 */
int nxbl_get_device(nxbl_device_t *dev);

/**
 * Set brightness (0-100 percent)
 */
int nxbl_set_brightness(int percent);

/**
 * Get brightness (0-100 percent)
 */
int nxbl_get_brightness(void);

/**
 * Set brightness with smooth transition
 * duration_ms: transition time in milliseconds
 */
int nxbl_set_brightness_smooth(int percent, int duration_ms);

/**
 * Set raw hardware brightness level
 */
int nxbl_set_raw_brightness(uint32_t level);
uint32_t nxbl_get_raw_brightness(void);
uint32_t nxbl_get_max_brightness(void);

/* ============================================================================
 * Auto-Brightness (Ambient Light Sensor)
 * ============================================================================ */

/**
 * Check if ambient light sensor is available
 */
int nxbl_ambient_available(void);

/**
 * Get ambient light reading
 */
int nxbl_get_ambient(nxbl_ambient_t *ambient);

/**
 * Enable/disable auto-brightness
 */
int nxbl_auto_brightness_enable(bool enabled);
int nxbl_auto_brightness_active(void);

/**
 * Set auto-brightness curve
 * Maps ambient lux to brightness percent
 */
typedef struct {
    uint32_t lux;
    uint8_t brightness;
} nxbl_curve_point_t;

int nxbl_set_brightness_curve(const nxbl_curve_point_t *points, int count);

/* ============================================================================
 * Keyboard Backlight
 * ============================================================================ */

int nxkb_backlight_available(void);
int nxkb_get_backlight(nxkb_backlight_t *kb);
int nxkb_set_brightness(int percent);
int nxkb_set_color(uint32_t rgb);  /* For RGB keyboards */
int nxkb_set_timeout(uint16_t seconds);

/* ============================================================================
 * Power Management
 * ============================================================================ */

/**
 * Power profiles for laptop displays
 */
typedef enum {
    NXBL_PROFILE_NORMAL = 0,
    NXBL_PROFILE_POWER_SAVE,    /* Reduce brightness on battery */
    NXBL_PROFILE_MOVIE,         /* Optimize for video (auto-dim disabled) */
    NXBL_PROFILE_READING,       /* Eye comfort (warmer, moderate brightness) */
    NXBL_PROFILE_MAX,           /* Maximum brightness */
} nxbl_profile_t;

int nxbl_set_profile(nxbl_profile_t profile);
nxbl_profile_t nxbl_get_profile(void);

/**
 * Power state notifications
 */
int nxbl_on_battery(bool on_battery);
int nxbl_on_lid_close(void);
int nxbl_on_lid_open(void);

/* ============================================================================
 * Display Driver Integration (nxdisplay_kdrv)
 * 
 * The backlight driver communicates directly with the display driver
 * for unified brightness control across internal and external displays.
 * ============================================================================ */

#include "nxdisplay_kdrv.h"

/**
 * Sync backlight with display driver
 * Called on init to register internal panel with nxdisplay_kdrv
 */
int nxbl_display_sync(void);

/**
 * Get display index for internal panel
 * Returns: nxdisplay_kdrv index for eDP/LVDS panel, or -1 if not found
 */
int nxbl_get_display_index(void);

/**
 * Set brightness via unified interface
 * If DDC/CI available (external): uses nxdisplay_kdrv_ddc_set_brightness
 * If internal panel: uses nxbl_set_brightness (PWM/ACPI)
 * This is the recommended brightness API for Settings apps
 */
int nxbl_unified_set_brightness(int display_index, int percent);
int nxbl_unified_get_brightness(int display_index);

/**
 * Detect if display is internal (laptop) or external (DDC/CI)
 */
int nxbl_is_internal_display(int display_index);

/* ============================================================================
 * Device Form Factor Detection
 * ============================================================================ */

typedef enum {
    NXDEVICE_DESKTOP = 0,           /* Desktop PC */
    NXDEVICE_LAPTOP,                /* Laptop/Notebook */
    NXDEVICE_TABLET,                /* Tablet (touch only) */
    NXDEVICE_CONVERTIBLE,           /* 2-in-1 laptop/tablet */
    NXDEVICE_PHONE,                 /* Mobile phone (future) */
    NXDEVICE_ALL_IN_ONE,            /* All-in-one PC */
    NXDEVICE_SERVER,                /* Server (no display) */
} nxdevice_type_t;

/**
 * Get device form factor
 * Used to auto-configure gesture and display settings
 */
nxdevice_type_t nxbl_get_device_type(void);

/**
 * Check device capabilities
 */
int nxbl_has_lid(void);             /* Has laptop lid sensor */
int nxbl_has_ambient_sensor(void);  /* Has ambient light sensor */
int nxbl_has_accelerometer(void);   /* Has accelerometer (tablets) */
int nxbl_has_gyroscope(void);       /* Has gyroscope */
int nxbl_has_battery(void);         /* Has battery (portable device) */

/* ============================================================================
 * Battery Integration
 * ============================================================================ */

typedef struct {
    bool present;               /* Battery present */
    bool charging;              /* Currently charging */
    uint8_t percent;            /* Charge level (0-100) */
    uint32_t time_remaining;    /* Minutes remaining */
    uint32_t full_capacity_mwh; /* Full capacity in mWh */
    uint32_t current_mwh;       /* Current charge in mWh */
    char name[32];              /* Battery name */
} nxbl_battery_t;

int nxbl_get_battery(nxbl_battery_t *battery);
int nxbl_is_on_battery(void);

/**
 * Set brightness reduction on battery
 * reduction_percent: 0-50 (how much to reduce brightness on battery)
 */
int nxbl_set_battery_reduction(int reduction_percent);

#endif /* NXBACKLIGHT_H */
