/*
 * NeolyxOS Settings - Driver Interface
 * 
 * Real driver integration for display, audio, network, power controls
 * Interfaces with kernel drivers via syscalls
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef SETTINGS_DRIVERS_H
#define SETTINGS_DRIVERS_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Display Driver Interface (nxdisplay)
 * ============================================================================ */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
    bool supported;
} display_mode_t;

typedef struct {
    char name[64];
    uint32_t id;
    bool connected;
    bool primary;
    uint32_t current_width;
    uint32_t current_height;
    uint32_t refresh_rate;
    uint32_t dpi;
    display_mode_t modes[32];
    int mode_count;
} display_info_t;

typedef struct {
    int brightness;              /* 0-100 */
    bool night_mode;
    int night_warmth;            /* 0-100 (color temperature) */
    bool hdr_enabled;
    bool vsync_enabled;
    char color_profile[32];      /* sRGB, Display P3, etc */
    int scaling;                 /* 100, 125, 150, 175, 200 */
    int orientation;             /* 0, 90, 180, 270 degrees */
} display_settings_t;

/* Display functions */
int display_get_count(void);
int display_get_info(int index, display_info_t* info);
int display_set_mode(int index, uint32_t width, uint32_t height, uint32_t refresh);
int display_set_brightness(int index, int brightness);
int display_set_night_mode(bool enabled, int warmth);
int display_set_hdr(int index, bool enabled);
int display_set_vsync(bool enabled);
int display_set_scaling(int index, int percent);
int display_set_orientation(int index, int degrees);
int display_get_settings(display_settings_t* settings);
int display_apply_settings(const display_settings_t* settings);

/* DDC/CI Hardware Control - Monitor OSD via I2C */
int display_ddc_supported(int index);
int display_ddc_get_brightness(int index);
int display_ddc_set_brightness(int index, int brightness);
int display_ddc_set_contrast(int index, int contrast);
int display_ddc_set_input(int index, int input_id);

/* Adaptive Refresh Rate - Power Saving */
int display_is_laptop(void);
int display_adaptive_set_mode(int index, int mode);   /* 0=Off, 1=PowerSave, 2=Balanced, 3=Performance */
int display_adaptive_notify_workload(int index, int workload);

/* Gaming Mode - VRR, HDR, Low Latency */
int display_gaming_mode_enable(int index);
int display_gaming_mode_disable(int index);
int display_gaming_mode_active(int index);
int display_register_game(const char* exe_name);

/* Night Light - Software Blue Light Filter */
int display_night_light_enable(int index, int kelvin);  /* 2700K-6500K */
int display_night_light_disable(int index);
int display_night_light_active(int index);

/* ============================================================================
 * Laptop Brightness (nxbacklight)
 * PWM/ACPI backlight for internal displays
 * ============================================================================ */

int laptop_is_laptop(void);
int laptop_get_brightness(void);
int laptop_set_brightness(int percent);
int laptop_set_brightness_smooth(int percent, int duration_ms);
int laptop_ambient_available(void);
int laptop_auto_brightness_enable(bool enabled);
int laptop_keyboard_backlight_set(int percent);
int laptop_keyboard_backlight_available(void);

/* Device form factor */
int laptop_get_device_type(void);  /* 0=desktop, 1=laptop, 2=tablet, 3=2-in-1 */
int laptop_has_battery(void);
int laptop_is_on_battery(void);

/* ============================================================================
 * Touch Gestures (nxtouch)
 * Multi-touch for trackpad and touchscreen
 * ============================================================================ */

int touch_available(void);
int touch_is_touchscreen(void);
int touch_is_touchpad(void);
int touch_set_natural_scroll(bool enabled);
int touch_set_scroll_speed(int speed);  /* 1-10 */
int touch_set_gesture_sensitivity(int level);  /* 1-10 */
int touch_set_three_finger_action(int direction, int action);
int touch_set_four_finger_action(int direction, int action);
int touch_set_edge_gesture(int edge, int action);
int touch_enable_app_switcher(bool enabled);
int touch_set_palm_rejection(bool enabled);

/* ============================================================================
 * Audio Driver Interface (nxaudio)
 * ============================================================================ */

typedef struct {
    char name[64];
    char id[128];
    uint32_t type;              /* 0=output, 1=input */
    bool active;
    bool is_default;
    int volume;                 /* 0-100 */
    bool muted;
} audio_device_t;

typedef struct {
    int master_volume;          /* 0-100 */
    int mic_volume;             /* 0-100 */
    bool master_muted;
    bool mic_muted;
    bool sound_effects;
    uint32_t sample_rate;       /* 44100, 48000, 96000, 192000 */
    uint32_t bit_depth;         /* 16, 24, 32 */
    char output_device[128];
    char input_device[128];
    bool spatial_audio;
    bool noise_cancellation;
} audio_settings_t;

/* Audio functions */
int audio_get_output_devices(audio_device_t* devices, int max_count);
int audio_get_input_devices(audio_device_t* devices, int max_count);
int audio_set_master_volume(int volume);
int audio_set_mic_volume(int volume);
int audio_set_mute(bool output_muted, bool input_muted);
int audio_set_output_device(const char* device_id);
int audio_set_input_device(const char* device_id);
int audio_set_sample_rate(uint32_t rate);
int audio_set_bit_depth(uint32_t depth);
int audio_get_settings(audio_settings_t* settings);
int audio_apply_settings(const audio_settings_t* settings);
int audio_test_speakers(void);
int audio_test_microphone(void);

/* ============================================================================
 * Network Driver Interface (nxnet)
 * ============================================================================ */

typedef struct {
    char name[32];
    char mac[18];
    char ipv4[16];
    char ipv6[46];
    char gateway[16];
    char dns[64];
    uint32_t type;              /* 0=ethernet, 1=wifi, 2=vpn, 3=virtual */
    bool connected;
    bool dhcp_enabled;
    int signal_strength;        /* WiFi only: 0-100 */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
} network_interface_t;

typedef struct {
    char ssid[64];
    int signal;
    bool secured;
    bool saved;
} wifi_network_t;

typedef struct {
    bool wifi_enabled;
    bool ethernet_enabled;
    bool bluetooth_enabled;
    bool airplane_mode;
    bool vpn_connected;
    char vpn_name[64];
    bool hotspot_enabled;
    char proxy_host[128];
    uint16_t proxy_port;
} network_settings_t;

/* Network functions */
int network_get_interfaces(network_interface_t* ifaces, int max_count);
int network_set_ip_config(const char* iface, const char* ip, const char* gateway, const char* dns);
int network_enable_dhcp(const char* iface, bool enabled);
int settings_wifi_scan(wifi_network_t* networks, int max_count);
int settings_wifi_connect(const char* ssid, const char* password);
int settings_wifi_disconnect(void);
int settings_wifi_enable(bool enabled);
int settings_bluetooth_enable(bool enabled);
int network_set_airplane_mode(bool enabled);
int network_get_settings(network_settings_t* settings);
int network_apply_settings(const network_settings_t* settings);

/* ============================================================================
 * Power Driver Interface
 * ============================================================================ */

typedef enum {
    POWER_MODE_PERFORMANCE,
    POWER_MODE_BALANCED,
    POWER_MODE_POWERSAVER,
    POWER_MODE_CUSTOM
} power_mode_t;

typedef struct {
    bool on_battery;
    int battery_percent;
    int time_remaining;         /* minutes */
    bool charging;
    bool ac_connected;
    power_mode_t power_mode;
    int sleep_after_minutes;    /* 0 = never */
    int screen_off_minutes;
    bool auto_brightness;
    bool lid_close_sleep;
    bool power_button_sleep;
} power_settings_t;

/* Power functions */
int power_get_battery_status(int* percent, bool* charging, int* time_remaining);
int power_set_mode(power_mode_t mode);
int power_set_sleep_timeout(int minutes);
int power_set_screen_timeout(int minutes);
int power_get_settings(power_settings_t* settings);
int power_apply_settings(const power_settings_t* settings);
int power_schedule_shutdown(int minutes);
int power_schedule_restart(int minutes);
int power_hibernate(void);
int power_suspend(void);

/* ============================================================================
 * System Performance Interface
 * ============================================================================ */

typedef struct {
    char cpu_model[128];
    int cpu_cores;
    int cpu_threads;
    float cpu_usage;            /* 0-100 */
    float cpu_temp;             /* Celsius */
    int cpu_freq_mhz;
    int cpu_freq_max_mhz;
} cpu_info_t;

typedef struct {
    uint64_t total_mb;
    uint64_t used_mb;
    uint64_t cached_mb;
    uint64_t swap_total_mb;
    uint64_t swap_used_mb;
} memory_info_t;

typedef struct {
    char governor[32];          /* performance, powersave, schedutil */
    int swappiness;             /* 0-100 */
    int max_processes;
    char io_scheduler[32];      /* mq-deadline, bfq, kyber */
    bool turbo_boost;
    int fan_mode;               /* 0=auto, 1=quiet, 2=balanced, 3=performance */
} performance_settings_t;

/* Performance functions */
int perf_get_cpu_info(cpu_info_t* info);
int perf_get_memory_info(memory_info_t* info);
int perf_set_cpu_governor(const char* governor);
int perf_set_swappiness(int value);
int perf_set_io_scheduler(const char* scheduler);
int perf_set_turbo_boost(bool enabled);
int perf_get_settings(performance_settings_t* settings);
int perf_apply_settings(const performance_settings_t* settings);

/* ============================================================================
 * Process Manager Interface
 * ============================================================================ */

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    char name[64];
    char user[32];
    float cpu_percent;
    uint64_t memory_kb;
    int priority;
    int threads;
    char state;                 /* R, S, D, Z, T */
    uint64_t start_time;
} process_info_t;

/* Process functions */
int process_list(process_info_t* procs, int max_count, int* total);
int process_kill(uint32_t pid, int signal);
int process_set_priority(uint32_t pid, int priority);
int process_get_info(uint32_t pid, process_info_t* info);

/* ============================================================================
 * Startup Apps Interface
 * ============================================================================ */

typedef struct {
    char id[64];
    char name[64];
    char command[256];
    bool enabled;
    char impact[16];            /* low, medium, high */
    int delay_seconds;
} startup_app_t;

/* Startup functions */
int startup_list(startup_app_t* apps, int max_count);
int startup_enable(const char* id, bool enabled);
int startup_add(const char* name, const char* command);
int startup_remove(const char* id);
int startup_set_delay(const char* id, int seconds);

/* ============================================================================
 * Appearance Settings
 * ============================================================================ */

typedef struct {
    char theme[32];             /* light, dark, system */
    char accent_color[8];       /* hex color */
    bool transparency;
    bool animations;
    float animation_speed;      /* 0.5 - 2.0 */
    char icon_theme[64];
    char cursor_theme[64];
    char window_decoration[32]; /* modern, classic */
    char font_family[64];
    int font_size;
    bool subpixel_aa;
} appearance_settings_t;

/* Appearance functions */
int appearance_get_settings(appearance_settings_t* settings);
int appearance_apply_settings(const appearance_settings_t* settings);
int appearance_get_available_themes(char themes[][64], int max_count);
int appearance_get_icon_themes(char themes[][64], int max_count);
int appearance_get_cursor_themes(char themes[][64], int max_count);

/* ============================================================================
 * Bootloader Settings (NeolyxOS Boot Manager)
 * ============================================================================ */

typedef struct {
    char entry_name[64];
    char kernel_path[256];
    char initrd_path[256];
    char params[512];
    bool is_default;
} boot_entry_t;

typedef struct {
    int timeout_seconds;
    bool show_menu;
    bool secure_boot;
    char default_entry[64];
} bootloader_settings_t;

/* Bootloader functions (requires admin) */
int boot_get_entries(boot_entry_t* entries, int max_count);
int boot_set_default(const char* entry_name);
int boot_set_timeout(int seconds);
int boot_add_entry(const boot_entry_t* entry);
int boot_remove_entry(const char* entry_name);
int boot_set_kernel_params(const char* entry_name, const char* params);
int boot_get_settings(bootloader_settings_t* settings);

/* ============================================================================
 * Kernel Settings
 * ============================================================================ */

typedef struct {
    char version[64];
    char release[64];
    char machine[32];
    uint64_t uptime_seconds;
    int load_1min;
    int load_5min;
    int load_15min;
} kernel_info_t;

/* Kernel functions */
int kernel_get_info(kernel_info_t* info);
int kernel_set_param(const char* param, const char* value);
int kernel_get_param(const char* param, char* value, int max_len);
int kernel_module_load(const char* module);
int kernel_module_unload(const char* module);
int kernel_list_modules(char modules[][64], int max_count);

#endif /* SETTINGS_DRIVERS_H */
