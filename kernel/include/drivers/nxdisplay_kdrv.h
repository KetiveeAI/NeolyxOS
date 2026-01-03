/*
 * NXDisplay Kernel Driver (nxdisplay.kdrv)
 * 
 * NeolyxOS Display Management Driver
 * 
 * Architecture:
 *   [ NXDisplay Kernel Driver ]
 *        ↕ Display Ports
 *   [ HDMI | DP | VGA | eDP | LVDS ]
 *        ↕ EDID, Mode Setting
 *   [ Desktop/Compositor ]
 * 
 * Features:
 *   - Multi-monitor detection
 *   - EDID parsing (manufacturer, model)
 *   - Resolution and refresh rate control
 *   - Atomic commit (mode + position + color)
 *   - Color profile management
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXDISPLAY_KDRV_H
#define NXDISPLAY_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXDISPLAY_KDRV_VERSION    "1.0.0"
#define NXDISPLAY_KDRV_ABI        1

/* ============ Connector Types ============ */

typedef enum {
    NXCONN_UNKNOWN = 0,
    NXCONN_VGA,
    NXCONN_DVI,
    NXCONN_HDMI,
    NXCONN_DISPLAYPORT,
    NXCONN_EDP,         /* Embedded DisplayPort (laptop) */
    NXCONN_LVDS,        /* Legacy laptop panel */
    NXCONN_COMPOSITE,
    NXCONN_VIRTUAL,     /* Virtual display */
} nxdisplay_conn_t;

/* ============ Display State ============ */

typedef enum {
    NXDISPLAY_DISCONNECTED = 0,
    NXDISPLAY_CONNECTED,
    NXDISPLAY_CONNECTED_NO_EDID,
    NXDISPLAY_DISABLED,
} nxdisplay_state_t;

/* ============ Color Depth ============ */

typedef enum {
    NXCOLOR_8BPC = 8,   /* 24-bit color */
    NXCOLOR_10BPC = 10, /* 30-bit color (HDR) */
    NXCOLOR_12BPC = 12, /* 36-bit color */
} nxcolor_depth_t;

/* ============ Color Space ============ */

typedef enum {
    NXCOLORSPACE_SRGB = 0,
    NXCOLORSPACE_P3,
    NXCOLORSPACE_REC709,
    NXCOLORSPACE_REC2020,
    NXCOLORSPACE_ADOBE_RGB,
    NXCOLORSPACE_NATIVE,
} nxcolorspace_t;

/* ============ Display Capability Descriptor ============ */

/**
 * Complete capability descriptor - what monitor supports vs what's active
 * Query once, cache in NXRender/NXGame
 */
typedef struct {
    /* Supported features (hardware capability) */
    struct {
        uint8_t  vrr;           /* VRR/FreeSync/G-Sync capable */
        uint8_t  hdr10;         /* HDR10 */
        uint8_t  dolby_vision;  /* Dolby Vision */
        uint8_t  hlg;           /* Hybrid Log Gamma */
        uint8_t  wide_color;    /* P3/Rec2020 */
        uint8_t  deep_color;    /* 10-bit+ */
        uint8_t  audio;         /* Audio over display */
    } supports;
    
    /* Currently active features */
    struct {
        uint8_t  vrr;
        uint8_t  hdr;
        uint8_t  wide_color;
        uint8_t  deep_color;
    } active;
    
    /* Refresh rate range */
    uint32_t min_refresh_hz;
    uint32_t max_refresh_hz;
    uint32_t current_refresh_hz;
    
    /* Resolution range */
    uint32_t max_width;
    uint32_t max_height;
    uint32_t current_width;
    uint32_t current_height;
    
    /* HDR capabilities */
    uint16_t max_luminance_nits;
    uint16_t min_luminance_nits;
    
    /* Color capabilities */
    nxcolorspace_t native_colorspace;
    nxcolor_depth_t max_color_depth;
} nxdisplay_caps_t;

/* ============ Declarative Mode Hints (for NXGame) ============ */

/**
 * Apps request features declaratively, OS arbitrates
 * NXGame says: "I want VRR + HDR if possible"
 * OS enables what's available, returns actual state
 */
typedef struct {
    uint8_t want_vrr;           /* Request VRR if available */
    uint8_t want_hdr;           /* Request HDR if available */
    uint8_t want_low_latency;   /* Minimize input lag */
    uint8_t want_wide_color;    /* Request P3/Rec2020 */
    uint32_t target_refresh_hz; /* Preferred refresh (0 = max available) */
    uint32_t min_refresh_hz;    /* VRR min (0 = display default) */
} nxdisplay_mode_hint_t;

/**
 * Result of mode request - what OS actually enabled
 */
typedef struct {
    uint8_t  vrr_enabled;
    uint8_t  hdr_enabled;
    uint8_t  low_latency_enabled;
    uint8_t  wide_color_enabled;
    uint32_t actual_refresh_hz;
    uint32_t vrr_min_hz;
    uint32_t vrr_max_hz;
} nxdisplay_mode_result_t;

/* ============ Display Mode ============ */

typedef struct {
    uint32_t        width;
    uint32_t        height;
    uint32_t        refresh_hz;         /* e.g., 60, 120, 144 */
    uint32_t        pixel_clock_khz;
    nxcolor_depth_t color_depth;
    uint8_t         interlaced;
    uint8_t         preferred;          /* EDID preferred mode */
} nxdisplay_mode_t;

/* ============ Display Info ============ */

#define NXDISPLAY_MAX_MODES    64

typedef struct {
    uint32_t        id;
    nxdisplay_conn_t connector;
    nxdisplay_state_t state;
    
    /* EDID info (manufacturer, model) */
    char            manufacturer[4];    /* 3-letter PNP ID */
    char            model_name[16];
    char            serial[16];
    uint16_t        manufacture_year;
    uint8_t         manufacture_week;
    
    /* Physical size (mm) */
    uint32_t        width_mm;
    uint32_t        height_mm;
    
    /* Capabilities */
    uint8_t         has_audio;
    uint8_t         has_hdr;
    uint8_t         has_vrr;            /* Variable refresh rate */
    uint32_t        max_refresh_hz;
    
    /* Current mode */
    nxdisplay_mode_t current_mode;
    
    /* Supported modes */
    uint8_t         mode_count;
    nxdisplay_mode_t modes[NXDISPLAY_MAX_MODES];
    
    /* Position in multi-monitor setup */
    int32_t         pos_x;
    int32_t         pos_y;
    uint8_t         is_primary;
    
    /* Color profile */
    nxcolorspace_t  colorspace;
    uint8_t         gamma_value;        /* x10, e.g., 22 = 2.2 */
    
    /* Hardware gamma LUT applied */
    uint8_t         lut_loaded;
} nxdisplay_info_t;

/* ============ Pending State (for atomic commit) ============ */

typedef struct {
    uint8_t         mode_changed;
    uint8_t         position_changed;
    uint8_t         color_changed;
    
    nxdisplay_mode_t pending_mode;
    int32_t         pending_x;
    int32_t         pending_y;
    nxcolorspace_t  pending_colorspace;
    uint8_t         pending_gamma;
} nxdisplay_pending_t;

/* ============ Color Profile ============ */

typedef struct {
    char            name[32];
    nxcolorspace_t  colorspace;
    uint8_t         gamma;              /* x10 */
    
    /* Hardware LUT (kernel stores ID, userland owns ICC) */
    uint16_t        red_lut[256];
    uint16_t        green_lut[256];
    uint16_t        blue_lut[256];
} nxdisplay_profile_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize display subsystem
 */
int nxdisplay_kdrv_init(void);

/**
 * Shutdown
 */
void nxdisplay_kdrv_shutdown(void);

/**
 * Get number of display outputs
 */
int nxdisplay_kdrv_count(void);

/**
 * Get display info by index
 */
int nxdisplay_kdrv_info(int index, nxdisplay_info_t *info);

/**
 * Rescan for displays (hotplug)
 */
int nxdisplay_kdrv_rescan(void);

/* ============ Mode Setting (Staged) ============ */

/**
 * Set pending mode (not applied until commit)
 */
int nxdisplay_kdrv_set_mode(int index, const nxdisplay_mode_t *mode);

/**
 * Set pending position (not applied until commit)
 */
int nxdisplay_kdrv_set_position(int index, int32_t x, int32_t y);

/**
 * Set pending color profile (not applied until commit)
 */
int nxdisplay_kdrv_set_color(int index, nxcolorspace_t colorspace, uint8_t gamma);

/**
 * Atomic commit - apply all pending changes
 * Returns 0 on success, -1 on failure (rolls back)
 */
int nxdisplay_kdrv_commit(int index);

/**
 * Discard pending changes
 */
void nxdisplay_kdrv_rollback(int index);

/* ============ Primary Display ============ */

/**
 * Set primary display
 */
int nxdisplay_kdrv_set_primary(int index);

/**
 * Get primary display index
 */
int nxdisplay_kdrv_get_primary(void);

/* ============ Color Profile ============ */

/**
 * Load color profile (LUT)
 */
int nxdisplay_kdrv_load_profile(int index, const nxdisplay_profile_t *profile);

/**
 * Get colorspace name
 */
const char *nxdisplay_kdrv_colorspace_name(nxcolorspace_t cs);

/**
 * Get connector type name
 */
const char *nxdisplay_kdrv_connector_name(nxdisplay_conn_t conn);

/**
 * Debug output
 */
void nxdisplay_kdrv_debug(void);

/* ============ Refresh Rate Control ============ */

/**
 * Set refresh rate for display
 * Returns 0 on success, -1 if rate not supported
 */
int nxdisplay_kdrv_set_refresh(int index, uint32_t hz);

/**
 * Get list of supported refresh rates
 */
int nxdisplay_kdrv_get_refresh_rates(int index, uint32_t *rates, int max_rates);

/* ============ Variable Refresh Rate (VRR/FreeSync/G-Sync) ============ */

/**
 * Enable VRR on display
 * min_hz/max_hz define the range (0 = use display defaults)
 */
int nxdisplay_kdrv_vrr_enable(int index, uint32_t min_hz, uint32_t max_hz);

/**
 * Disable VRR
 */
int nxdisplay_kdrv_vrr_disable(int index);

/**
 * Check if VRR is active
 */
int nxdisplay_kdrv_vrr_active(int index);

/* ============ HDR Control ============ */

typedef enum {
    NXHDR_OFF = 0,
    NXHDR_HDR10,
    NXHDR_DOLBY_VISION,
    NXHDR_HLG
} nxhdr_mode_t;

typedef struct {
    nxhdr_mode_t mode;
    uint16_t max_luminance;      /* cd/m² */
    uint16_t min_luminance;      /* 0.0001 cd/m² scaled */
    uint16_t max_content_light;
    uint16_t max_frame_avg_light;
} nxhdr_metadata_t;

/**
 * Enable HDR mode
 */
int nxdisplay_kdrv_hdr_enable(int index, nxhdr_mode_t mode);

/**
 * Disable HDR
 */
int nxdisplay_kdrv_hdr_disable(int index);

/**
 * Set HDR metadata
 */
int nxdisplay_kdrv_hdr_metadata(int index, const nxhdr_metadata_t *meta);

/**
 * Check if HDR is active
 */
int nxdisplay_kdrv_hdr_active(int index);

/* ============ VSync Control ============ */

/**
 * Wait for VSync on display
 */
int nxdisplay_kdrv_wait_vsync(int index);

/**
 * Get VSync timestamp (for frame pacing)
 */
uint64_t nxdisplay_kdrv_vsync_timestamp(int index);

/* ============ Capability Query (for NXRender) ============ */

/**
 * Get complete capability descriptor - call once, cache result
 * Returns all supported and active features
 */
int nxdisplay_kdrv_get_caps(int index, nxdisplay_caps_t *caps);

/**
 * Check if specific capability is supported
 */
int nxdisplay_kdrv_supports_vrr(int index);
int nxdisplay_kdrv_supports_hdr(int index);
int nxdisplay_kdrv_supports_wide_color(int index);

/* ============ Declarative Mode Request (for NXGame) ============ */

/**
 * Request display mode declaratively
 * App says what it wants, OS arbitrates
 * 
 * Example:
 *   hint.want_vrr = 1;
 *   hint.want_hdr = 1;
 *   hint.target_refresh_hz = 120;
 *   nxdisplay_kdrv_request_mode(0, &hint, &result);
 *   // Check result.vrr_enabled, result.hdr_enabled, etc.
 */
int nxdisplay_kdrv_request_mode(int index, 
                                 const nxdisplay_mode_hint_t *hint,
                                 nxdisplay_mode_result_t *result);

/**
 * Release special mode (return to desktop defaults)
 */
int nxdisplay_kdrv_release_mode(int index);

/* ============================================================================
 * DDC/CI Monitor Hardware Control
 * 
 * For real hardware communication with monitor OSD settings.
 * Works on: External monitors (HDMI/DP), NOT internal laptop panels
 * 
 * DDC/CI (Display Data Channel / Command Interface) sends commands
 * directly to monitor's built-in controller to adjust:
 * - Brightness (backlight)
 * - Contrast
 * - Color temperature
 * - Input source
 * - Power state
 * ============================================================================ */

typedef enum {
    NXDDC_BRIGHTNESS      = 0x10,  /* VCP code for brightness */
    NXDDC_CONTRAST        = 0x12,
    NXDDC_COLOR_TEMP      = 0x14,
    NXDDC_INPUT_SOURCE    = 0x60,
    NXDDC_POWER_MODE      = 0xD6,
    NXDDC_RED_GAIN        = 0x16,
    NXDDC_GREEN_GAIN      = 0x18,
    NXDDC_BLUE_GAIN       = 0x1A,
    NXDDC_VOLUME          = 0x62,
    NXDDC_MUTE            = 0x8D,
} nxddc_vcp_code_t;

typedef struct {
    uint8_t  ddc_supported;      /* Monitor supports DDC/CI */
    uint16_t brightness_current;
    uint16_t brightness_max;
    uint16_t contrast_current;
    uint16_t contrast_max;
    uint16_t color_temp;         /* Kelvin: 4000K-10000K */
} nxddc_caps_t;

/**
 * Check if display supports DDC/CI
 */
int nxdisplay_kdrv_ddc_supported(int index);

/**
 * Get DDC/CI capabilities and current values
 */
int nxdisplay_kdrv_ddc_get_caps(int index, nxddc_caps_t *caps);

/**
 * Set monitor brightness via DDC/CI (0-100)
 * This controls the MONITOR'S OSD brightness, not software gamma
 */
int nxdisplay_kdrv_ddc_set_brightness(int index, int brightness);

/**
 * Get monitor brightness
 */
int nxdisplay_kdrv_ddc_get_brightness(int index);

/**
 * Set monitor contrast (0-100)
 */
int nxdisplay_kdrv_ddc_set_contrast(int index, int contrast);

/**
 * Set color temperature in Kelvin (4000K-10000K)
 */
int nxdisplay_kdrv_ddc_set_color_temp(int index, uint16_t kelvin);

/**
 * Switch monitor input source
 */
typedef enum {
    NXDDC_INPUT_DP1 = 0x0F,
    NXDDC_INPUT_DP2 = 0x10,
    NXDDC_INPUT_HDMI1 = 0x11,
    NXDDC_INPUT_HDMI2 = 0x12,
    NXDDC_INPUT_USB_C = 0x13,
    NXDDC_INPUT_VGA = 0x01,
    NXDDC_INPUT_DVI = 0x03,
} nxddc_input_t;

int nxdisplay_kdrv_ddc_set_input(int index, nxddc_input_t input);

/**
 * Raw DDC/CI VCP command
 */
int nxdisplay_kdrv_ddc_set_vcp(int index, uint8_t vcp_code, uint16_t value);
int nxdisplay_kdrv_ddc_get_vcp(int index, uint8_t vcp_code, uint16_t *value, uint16_t *max);

/* ============================================================================
 * Night Light / Blue Light Filter
 * 
 * Software-based color temperature adjustment for eye comfort
 * Works on all displays (including laptops without DDC)
 * ============================================================================ */

typedef struct {
    uint8_t  enabled;
    uint16_t color_temp_kelvin;  /* 2700K (warm) - 6500K (normal) */
    uint8_t  auto_schedule;      /* Auto on/off at sunset/sunrise */
    uint8_t  hour_start;
    uint8_t  minute_start;
    uint8_t  hour_end;
    uint8_t  minute_end;
    uint8_t  transition_minutes; /* Fade in/out duration */
} nxnight_config_t;

int nxdisplay_kdrv_night_enable(int index, uint16_t kelvin);
int nxdisplay_kdrv_night_disable(int index);
int nxdisplay_kdrv_night_set_config(int index, const nxnight_config_t *config);
int nxdisplay_kdrv_night_get_config(int index, nxnight_config_t *config);
int nxdisplay_kdrv_night_active(int index);

/* ============================================================================
 * Adaptive Refresh Rate (Power Saving)
 * 
 * For LAPTOPS: Automatically adjusts refresh rate based on workload
 *   - Video playback: Lower Hz (30-60 Hz) to save battery
 *   - Desktop/browsing: Standard Hz (60 Hz)
 *   - Heavy work/gaming: Full Hz (144/165 Hz)
 * 
 * For DESKTOPS: Optional (in Advanced settings)
 * 
 * Uses VRR if available, otherwise switches between fixed rates
 * ============================================================================ */

typedef enum {
    NXADAPT_OFF = 0,            /* Always use fixed refresh rate */
    NXADAPT_POWER_SAVE,         /* Aggressive: lower Hz when possible */
    NXADAPT_BALANCED,           /* Default: lower for video, full for work */
    NXADAPT_PERFORMANCE,        /* Always high Hz, disable only for video */
} nxadapt_mode_t;

typedef enum {
    NXWORKLOAD_IDLE = 0,        /* Desktop, nothing happening */
    NXWORKLOAD_VIDEO,           /* Video playback (can lower Hz) */
    NXWORKLOAD_BROWSING,        /* Light browsing, documents */
    NXWORKLOAD_PRODUCTIVITY,    /* Heavy apps, compilers, etc */
    NXWORKLOAD_GAMING,          /* Game detected (max Hz) */
    NXWORKLOAD_FULL_SCREEN,     /* Full-screen app */
} nxworkload_t;

typedef struct {
    nxadapt_mode_t mode;
    uint8_t        is_laptop;           /* Auto-detected */
    uint8_t        has_touch_screen;    /* Touch-screen laptops */
    
    /* Refresh rate thresholds */
    uint32_t       idle_hz;             /* When idle (default: 30) */
    uint32_t       video_hz;            /* Video playback (default: 60) */
    uint32_t       browser_hz;          /* Browsing (default: 60) */
    uint32_t       work_hz;             /* Heavy work (default: max/2) */
    uint32_t       gaming_hz;           /* Gaming (default: max) */
    
    /* VRR usage */
    uint8_t        use_vrr_for_video;   /* Use VRR to match video frame rate */
    uint8_t        use_vrr_for_gaming;  /* Use VRR for games */
    
    /* Battery threshold */
    uint8_t        battery_threshold;   /* Apply power save below this % */
    
    /* Current state */
    nxworkload_t   current_workload;
    uint32_t       current_hz;
    uint8_t        on_battery;
} nxadapt_state_t;

/**
 * Check if this is a laptop (eDP connection)
 */
int nxdisplay_kdrv_is_laptop(void);

/**
 * Set adaptive refresh mode
 */
int nxdisplay_kdrv_adaptive_set_mode(int index, nxadapt_mode_t mode);

/**
 * Get current adaptive refresh state
 */
int nxdisplay_kdrv_adaptive_get_state(int index, nxadapt_state_t *state);

/**
 * Notify workload change (called by compositor/window manager)
 * Driver will adjust refresh rate accordingly
 */
int nxdisplay_kdrv_adaptive_workload(int index, nxworkload_t workload);

/**
 * Force temporary refresh rate (overrides adaptive)
 * Use for apps that need specific Hz (e.g., video editing)
 * Pass 0 to release override
 */
int nxdisplay_kdrv_adaptive_override(int index, uint32_t hz);

/**
 * Configure adaptive refresh thresholds
 */
int nxdisplay_kdrv_adaptive_configure(int index, const nxadapt_state_t *config);

/* ============================================================================
 * Gaming Mode
 * 
 * One-click optimization for games:
 * - Enable VRR (FreeSync/G-Sync)
 * - Max refresh rate
 * - Low latency mode
 * - Disable compositor effects
 * - HDR if available
 * ============================================================================ */

typedef enum {
    NXGAME_MODE_OFF = 0,
    NXGAME_MODE_AUTO,           /* Auto-detect games, enable when needed */
    NXGAME_MODE_ON,             /* Always on */
} nxgame_mode_t;

typedef struct {
    nxgame_mode_t mode;
    uint8_t       vrr_enabled;
    uint8_t       hdr_enabled;
    uint8_t       low_latency;
    uint8_t       disable_compositor;  /* Bypass compositor for fullscreen */
    uint32_t      target_hz;           /* 0 = max available */
    uint32_t      vrr_min_hz;          /* VRR floor */
} nxgame_config_t;

typedef struct {
    uint8_t       active;              /* Gaming mode currently active */
    char          game_name[64];       /* Detected game name */
    uint32_t      game_pid;            /* Game process ID */
    uint32_t      current_fps;         /* Measured FPS */
    uint32_t      vrr_current_hz;      /* Actual VRR refresh */
    uint8_t       gpu_usage;           /* GPU utilization % */
} nxgame_status_t;

/**
 * Enable/disable Gaming Mode
 */
int nxdisplay_kdrv_game_mode_enable(int index, const nxgame_config_t *config);
int nxdisplay_kdrv_game_mode_disable(int index);

/**
 * Get Gaming Mode status
 */
int nxdisplay_kdrv_game_mode_status(int index, nxgame_status_t *status);

/**
 * Register game process (auto-detect when running)
 */
int nxdisplay_kdrv_game_register(const char *exe_name);
int nxdisplay_kdrv_game_unregister(const char *exe_name);

/**
 * Check if Gaming Mode is active
 */
int nxdisplay_kdrv_game_mode_active(int index);

/* ============================================================================
 * Screen Sleep / Power States
 * ============================================================================ */

typedef enum {
    NXSCREEN_ON = 0,
    NXSCREEN_DIMMED,            /* Screen dimmed but on */
    NXSCREEN_OFF,               /* Screen off (DPMS off) */
    NXSCREEN_LOCKED,            /* Screen off + locked */
} nxscreen_power_t;

/**
 * Set screen power state
 */
int nxdisplay_kdrv_screen_power(int index, nxscreen_power_t state);

/**
 * Get screen power state
 */
nxscreen_power_t nxdisplay_kdrv_screen_get_power(int index);

/**
 * Configure sleep timeouts (0 = never)
 */
int nxdisplay_kdrv_screen_set_timeout(int index, 
                                       uint16_t dim_seconds, 
                                       uint16_t off_seconds);

#endif /* NXDISPLAY_KDRV_H */
