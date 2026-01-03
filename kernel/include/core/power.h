/*
 * NeolyxOS Power Management Subsystem
 * 
 * Features:
 *   - Device type detection (desktop/laptop/tablet)
 *   - Power button handling
 *   - Battery monitoring
 *   - Power profiles (performance/balanced/powersave)
 *   - CPU frequency scaling
 *   - Display brightness
 *   - Sleep/hibernate states
 *   - Lid switch (laptops)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_POWER_H
#define NEOLYX_POWER_H

#include <stdint.h>

/* ============ Device Types ============ */

typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_DESKTOP,
    DEVICE_TYPE_LAPTOP,
    DEVICE_TYPE_TABLET,
    DEVICE_TYPE_SERVER,
    DEVICE_TYPE_VM,             /* Virtual machine */
} device_type_t;

/* ============ Power Source ============ */

typedef enum {
    POWER_SOURCE_AC = 0,        /* Plugged in */
    POWER_SOURCE_BATTERY,
    POWER_SOURCE_UPS,
} power_source_t;

/* ============ Power States ============ */

typedef enum {
    POWER_STATE_RUNNING = 0,    /* S0 - Normal operation */
    POWER_STATE_IDLE,           /* S0ix - Low power idle */
    POWER_STATE_STANDBY,        /* S1 - CPU stopped */
    POWER_STATE_SUSPEND,        /* S3 - Suspend to RAM */
    POWER_STATE_HIBERNATE,      /* S4 - Suspend to disk */
    POWER_STATE_SHUTDOWN,       /* S5 - Soft off */
} power_state_t;

/* ============ Power Profiles ============ */

typedef enum {
    POWER_PROFILE_PERFORMANCE = 0,  /* Max performance */
    POWER_PROFILE_BALANCED,         /* Balance perf/power */
    POWER_PROFILE_POWERSAVE,        /* Max battery life */
    POWER_PROFILE_CUSTOM,
} power_profile_t;

/* ============ Power Button Actions ============ */

typedef enum {
    BUTTON_ACTION_NOTHING = 0,
    BUTTON_ACTION_SUSPEND,
    BUTTON_ACTION_HIBERNATE,
    BUTTON_ACTION_SHUTDOWN,
    BUTTON_ACTION_ASK,          /* Show dialog */
} button_action_t;

/* ============ Battery State ============ */

typedef enum {
    BATTERY_STATE_UNKNOWN = 0,
    BATTERY_STATE_CHARGING,
    BATTERY_STATE_DISCHARGING,
    BATTERY_STATE_FULL,
    BATTERY_STATE_NOT_CHARGING,
    BATTERY_STATE_CRITICAL,
} battery_state_t;

/* ============ Battery Info ============ */

typedef struct {
    int present;
    battery_state_t state;
    
    /* Capacity */
    uint8_t percent;            /* 0-100% */
    uint32_t capacity_mwh;      /* Current capacity mWh */
    uint32_t design_mwh;        /* Design capacity mWh */
    uint32_t full_mwh;          /* Full charge capacity mWh */
    
    /* Time estimates */
    uint32_t time_to_empty;     /* Minutes */
    uint32_t time_to_full;      /* Minutes */
    
    /* Health */
    uint8_t health_percent;     /* Battery health 0-100% */
    uint32_t cycle_count;
    
    /* Power draw */
    int32_t power_mw;           /* Positive = charging */
    int32_t voltage_mv;
    int32_t current_ma;
    
    /* Temperature */
    int16_t temp_celsius;
    
    /* Info */
    char manufacturer[32];
    char model[32];
    char serial[32];
} battery_info_t;

/* ============ AC Adapter Info ============ */

typedef struct {
    int present;
    int online;
    
    uint32_t power_mw;          /* Max power delivery */
    char type[16];              /* "USB-C PD", "Barrel", etc. */
} ac_adapter_t;

/* ============ CPU Power Info ============ */

typedef struct {
    uint32_t cur_freq_mhz;
    uint32_t min_freq_mhz;
    uint32_t max_freq_mhz;
    uint32_t base_freq_mhz;
    uint32_t turbo_freq_mhz;
    
    int turbo_enabled;
    int hyperthreading;
    
    /* Governor */
    char governor[32];          /* "performance", "powersave", etc. */
    
    /* Package power */
    uint32_t tdp_mw;
    uint32_t power_mw;
    int16_t temp_celsius;
} cpu_power_info_t;

/* ============ Power Settings ============ */

typedef struct {
    /* Profile */
    power_profile_t profile;
    
    /* Button actions */
    button_action_t power_button;
    button_action_t lid_close;
    button_action_t lid_open;
    
    /* Display */
    uint8_t brightness_ac;      /* 0-100% on AC */
    uint8_t brightness_battery; /* 0-100% on battery */
    uint32_t dim_after_sec;     /* Dim display after X seconds */
    uint32_t off_after_sec;     /* Turn off display after */
    
    /* Sleep */
    uint32_t sleep_after_sec;   /* Auto sleep after */
    int sleep_when_lid_closed;
    
    /* Battery */
    uint8_t critical_threshold; /* Critical battery % */
    button_action_t critical_action;
    uint8_t low_threshold;      /* Low battery warning % */
    
    /* CPU */
    int allow_turbo;
    uint32_t max_freq_mhz;      /* 0 = unlimited */
    
    /* Wake */
    int wake_on_lan;
    int wake_on_usb;
} power_settings_t;

/* ============ ACPI Events ============ */

typedef enum {
    ACPI_EVENT_POWER_BUTTON = 0,
    ACPI_EVENT_SLEEP_BUTTON,
    ACPI_EVENT_LID_OPEN,
    ACPI_EVENT_LID_CLOSE,
    ACPI_EVENT_AC_ONLINE,
    ACPI_EVENT_AC_OFFLINE,
    ACPI_EVENT_BATTERY_LOW,
    ACPI_EVENT_BATTERY_CRITICAL,
    ACPI_EVENT_THERMAL_CRITICAL,
} acpi_event_t;

typedef void (*power_event_callback_t)(acpi_event_t event, void *data);

/* ============ Power Management API ============ */

/**
 * Initialize power management.
 */
int power_init(void);

/**
 * Detect device type.
 */
device_type_t power_detect_device_type(void);

/**
 * Get current power source.
 */
power_source_t power_get_source(void);

/**
 * Get current power state.
 */
power_state_t power_get_state(void);

/* ============ Power Profiles ============ */

/**
 * Set power profile.
 */
int power_set_profile(power_profile_t profile);

/**
 * Get current profile.
 */
power_profile_t power_get_profile(void);

/**
 * Get profile name.
 */
const char *power_profile_name(power_profile_t profile);

/* ============ Battery ============ */

/**
 * Get battery info.
 */
int power_get_battery(battery_info_t *info);

/**
 * Check if on battery.
 */
int power_on_battery(void);

/**
 * Get battery percentage.
 */
uint8_t power_battery_percent(void);

/* ============ AC Adapter ============ */

/**
 * Get AC adapter info.
 */
int power_get_ac(ac_adapter_t *info);

/* ============ CPU Power ============ */

/**
 * Get CPU power info.
 */
int power_get_cpu_info(cpu_power_info_t *info);

/**
 * Set CPU governor.
 */
int power_set_cpu_governor(const char *governor);

/**
 * Enable/disable turbo boost.
 */
int power_set_turbo(int enabled);

/**
 * Set CPU frequency limits.
 */
int power_set_cpu_freq(uint32_t min_mhz, uint32_t max_mhz);

/* ============ Display ============ */

/**
 * Get display brightness (0-100).
 */
int power_get_brightness(void);

/**
 * Set display brightness (0-100).
 */
int power_set_brightness(int percent);

/* ============ Power State Control ============ */

/**
 * Suspend to RAM.
 */
int power_suspend(void);

/**
 * Hibernate to disk.
 */
int power_hibernate(void);

/**
 * Shutdown system.
 */
int power_shutdown(void);

/**
 * Reboot system.
 */
int power_reboot(void);

/* ============ Settings ============ */

/**
 * Get power settings.
 */
power_settings_t *power_get_settings(void);

/**
 * Apply power settings.
 */
int power_apply_settings(power_settings_t *settings);

/**
 * Save settings to disk.
 */
int power_save_settings(void);

/**
 * Load settings from disk.
 */
int power_load_settings(void);

/* ============ Events ============ */

/**
 * Register power event callback.
 */
int power_register_callback(power_event_callback_t callback);

/**
 * Handle ACPI event.
 */
void power_handle_event(acpi_event_t event);

/* ============ Lid Switch ============ */

/**
 * Check if lid is open (laptops).
 */
int power_lid_is_open(void);

/* ============ Thermal ============ */

/**
 * Get CPU temperature.
 */
int power_get_cpu_temp(void);

/**
 * Get fan speed (0-100%).
 */
int power_get_fan_speed(void);

/**
 * Set fan mode (0=auto, 1=silent, 2=balanced, 3=performance).
 */
int power_set_fan_mode(int mode);

#endif /* NEOLYX_POWER_H */
