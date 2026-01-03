/*
 * NXDisplay Kernel Driver - DDC/CI, Adaptive Refresh, Gaming Mode
 * 
 * Implementation of advanced display control features:
 * - DDC/CI: Monitor OSD brightness, contrast, color temp via I2C
 * - Night Light: Software color temperature adjustment
 * - Adaptive Refresh: Workload-based Hz for laptop power saving
 * - Gaming Mode: VRR, HDR, low latency optimization
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "drivers/nxdisplay_kdrv.h"
#include "drivers/i2c.h"
#include "core/timer.h"
#include "core/printk.h"
#include <string.h>

/* ============================================================================
 * DDC/CI Implementation
 * 
 * DDC/CI (Display Data Channel / Command Interface) communicates with
 * monitors via I2C bus embedded in HDMI/DP cables.
 * Standard address: 0x37 (shifted: 0x6E)
 * ============================================================================ */

#define DDC_ADDR            0x37    /* DDC/CI slave address */
#define DDC_ADDR_WRITE      0x6E    /* 0x37 << 1 */
#define DDC_ADDR_READ       0x6F    /* 0x37 << 1 | 1 */
#define DDC_HOST_ADDR       0x51    /* Host address */

/* DDC/CI message types */
#define DDC_VCP_REQUEST     0x01    /* Get VCP Feature */
#define DDC_VCP_REPLY       0x02    /* VCP Feature Reply */
#define DDC_VCP_SET         0x03    /* Set VCP Feature */
#define DDC_CAPABILITIES    0xF3    /* Get Capabilities String */

/* i2c_adapter per display */
static struct i2c_adapter *g_display_i2c[8] = { NULL };

/* Calculate DDC checksum */
static uint8_t ddc_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = DDC_ADDR_WRITE;  /* Include address in checksum */
    for (size_t i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

/* Send DDC/CI command */
static int ddc_send(int index, const uint8_t *data, size_t len) {
    if (index < 0 || index >= 8 || !g_display_i2c[index]) {
        return -1;
    }
    
    /* Build DDC message */
    uint8_t msg[16];
    msg[0] = DDC_HOST_ADDR;
    msg[1] = 0x80 | (len & 0x7F);  /* Length with flag */
    memcpy(&msg[2], data, len);
    msg[2 + len] = ddc_checksum(msg, 2 + len);
    
    /* Send via I2C */
    return i2c_write(g_display_i2c[index], DDC_ADDR, msg, 3 + len);
}

/* Read DDC/CI response */
static int ddc_recv(int index, uint8_t *data, size_t max_len) {
    if (index < 0 || index >= 8 || !g_display_i2c[index]) {
        return -1;
    }
    
    uint8_t buf[32];
    int ret = i2c_read(g_display_i2c[index], DDC_ADDR, buf, max_len + 3);
    if (ret < 3) return -1;
    
    size_t len = buf[1] & 0x7F;
    if (len > max_len) len = max_len;
    memcpy(data, &buf[2], len);
    
    return (int)len;
}

/* DDC/CI: Check if supported */
int nxdisplay_kdrv_ddc_supported(int index) {
    uint8_t cmd[] = { DDC_VCP_REQUEST, NXDDC_BRIGHTNESS };
    if (ddc_send(index, cmd, 2) != 0) {
        return 0;  /* Not supported */
    }
    
    /* Try to read response */
    uint8_t resp[8];
    if (ddc_recv(index, resp, 8) < 4) {
        return 0;
    }
    
    return 1;  /* Supported */
}

/* DDC/CI: Get VCP value */
int nxdisplay_kdrv_ddc_get_vcp(int index, uint8_t vcp_code, 
                                uint16_t *value, uint16_t *max) {
    uint8_t cmd[] = { DDC_VCP_REQUEST, vcp_code };
    if (ddc_send(index, cmd, 2) != 0) {
        return -1;
    }
    
    /* Small delay for monitor to process */
    timer_delay_ms(40);
    
    /* Read response */
    uint8_t resp[8];
    int len = ddc_recv(index, resp, 8);
    if (len < 7 || resp[0] != DDC_VCP_REPLY) {
        return -1;
    }
    
    /* Parse response: [type, result, code, type_code, max_hi, max_lo, val_hi, val_lo] */
    if (max) *max = ((uint16_t)resp[4] << 8) | resp[5];
    if (value) *value = ((uint16_t)resp[6] << 8) | resp[7];
    
    return 0;
}

/* DDC/CI: Set VCP value */
int nxdisplay_kdrv_ddc_set_vcp(int index, uint8_t vcp_code, uint16_t value) {
    uint8_t cmd[] = { 
        DDC_VCP_SET, 
        vcp_code, 
        (uint8_t)(value >> 8), 
        (uint8_t)(value & 0xFF) 
    };
    return ddc_send(index, cmd, 4);
}

/* DDC/CI: Get capabilities */
int nxdisplay_kdrv_ddc_get_caps(int index, nxddc_caps_t *caps) {
    if (!caps) return -1;
    
    memset(caps, 0, sizeof(nxddc_caps_t));
    caps->ddc_supported = nxdisplay_kdrv_ddc_supported(index);
    
    if (!caps->ddc_supported) {
        return 0;  /* Not an error, just no DDC */
    }
    
    /* Get brightness */
    nxdisplay_kdrv_ddc_get_vcp(index, NXDDC_BRIGHTNESS, 
                               &caps->brightness_current, &caps->brightness_max);
    
    /* Get contrast */
    nxdisplay_kdrv_ddc_get_vcp(index, NXDDC_CONTRAST,
                               &caps->contrast_current, &caps->contrast_max);
    
    /* Get color temp */
    nxdisplay_kdrv_ddc_get_vcp(index, NXDDC_COLOR_TEMP,
                               &caps->color_temp, NULL);
    
    return 0;
}

/* DDC/CI: Set brightness (0-100) */
int nxdisplay_kdrv_ddc_set_brightness(int index, int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    return nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_BRIGHTNESS, (uint16_t)brightness);
}

/* DDC/CI: Get brightness */
int nxdisplay_kdrv_ddc_get_brightness(int index) {
    uint16_t value = 0;
    if (nxdisplay_kdrv_ddc_get_vcp(index, NXDDC_BRIGHTNESS, &value, NULL) != 0) {
        return -1;
    }
    return (int)value;
}

/* DDC/CI: Set contrast */
int nxdisplay_kdrv_ddc_set_contrast(int index, int contrast) {
    if (contrast < 0) contrast = 0;
    if (contrast > 100) contrast = 100;
    return nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_CONTRAST, (uint16_t)contrast);
}

/* DDC/CI: Set color temperature */
int nxdisplay_kdrv_ddc_set_color_temp(int index, uint16_t kelvin) {
    /* Standard color temp values for DDC/CI:
     * 0x01 = 4000K, 0x02 = 5000K, 0x03 = 6500K, 0x04 = 7500K, etc */
    uint16_t ddc_value;
    if (kelvin <= 4000) ddc_value = 1;
    else if (kelvin <= 5000) ddc_value = 2;
    else if (kelvin <= 6500) ddc_value = 3;
    else if (kelvin <= 7500) ddc_value = 4;
    else if (kelvin <= 8200) ddc_value = 5;
    else if (kelvin <= 9300) ddc_value = 6;
    else ddc_value = 7;  /* 10000K+ */
    
    return nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_COLOR_TEMP, ddc_value);
}

/* DDC/CI: Set input source */
int nxdisplay_kdrv_ddc_set_input(int index, nxddc_input_t input) {
    return nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_INPUT_SOURCE, (uint16_t)input);
}

/* ============================================================================
 * Night Light Implementation
 * 
 * Software-based blue light filter using gamma LUT
 * Works on all displays including laptops
 * ============================================================================ */

static nxnight_config_t g_night_config[8] = { 0 };

int nxdisplay_kdrv_night_enable(int index, uint16_t kelvin) {
    if (index < 0 || index >= 8) return -1;
    
    g_night_config[index].enabled = 1;
    g_night_config[index].color_temp_kelvin = kelvin;
    
    /* Apply via gamma LUT - warm colors reduce blue channel */
    nxdisplay_profile_t profile;
    strcpy(profile.name, "Night Light");
    profile.colorspace = NXCOLORSPACE_SRGB;
    
    /* Calculate RGB multipliers for color temperature */
    float blue_mult = 1.0f;
    float green_mult = 1.0f;
    
    /* Lower kelvin = warmer = less blue */
    if (kelvin < 6500) {
        float warmth = (6500.0f - kelvin) / 3800.0f;  /* 0.0-1.0 */
        blue_mult = 1.0f - (warmth * 0.5f);           /* Reduce blue up to 50% */
        green_mult = 1.0f - (warmth * 0.1f);          /* Slight green reduction */
    }
    
    /* Build gamma LUT */
    for (int i = 0; i < 256; i++) {
        profile.red_lut[i] = (uint16_t)(i * 256);
        profile.green_lut[i] = (uint16_t)(i * 256 * green_mult);
        profile.blue_lut[i] = (uint16_t)(i * 256 * blue_mult);
    }
    
    return nxdisplay_kdrv_load_profile(index, &profile);
}

int nxdisplay_kdrv_night_disable(int index) {
    if (index < 0 || index >= 8) return -1;
    
    g_night_config[index].enabled = 0;
    
    /* Reset to default gamma */
    nxdisplay_profile_t profile;
    strcpy(profile.name, "Default");
    profile.colorspace = NXCOLORSPACE_SRGB;
    
    for (int i = 0; i < 256; i++) {
        profile.red_lut[i] = (uint16_t)(i * 256);
        profile.green_lut[i] = (uint16_t)(i * 256);
        profile.blue_lut[i] = (uint16_t)(i * 256);
    }
    
    return nxdisplay_kdrv_load_profile(index, &profile);
}

int nxdisplay_kdrv_night_set_config(int index, const nxnight_config_t *config) {
    if (index < 0 || index >= 8 || !config) return -1;
    
    g_night_config[index] = *config;
    
    if (config->enabled) {
        return nxdisplay_kdrv_night_enable(index, config->color_temp_kelvin);
    } else {
        return nxdisplay_kdrv_night_disable(index);
    }
}

int nxdisplay_kdrv_night_get_config(int index, nxnight_config_t *config) {
    if (index < 0 || index >= 8 || !config) return -1;
    
    *config = g_night_config[index];
    return 0;
}

int nxdisplay_kdrv_night_active(int index) {
    if (index < 0 || index >= 8) return 0;
    return g_night_config[index].enabled;
}

/* ============================================================================
 * Adaptive Refresh Rate Implementation
 * 
 * Automatically adjusts refresh rate based on detected workload
 * For laptops: Enabled by default (power saving)
 * For desktops: Optional (advanced settings)
 * ============================================================================ */

static nxadapt_state_t g_adapt_state[8] = { 0 };

int nxdisplay_kdrv_is_laptop(void) {
    /* Check if primary display uses eDP (embedded DisplayPort) */
    nxdisplay_info_t info;
    if (nxdisplay_kdrv_info(0, &info) != 0) {
        return 0;
    }
    
    return (info.connector == NXCONN_EDP || info.connector == NXCONN_LVDS);
}

int nxdisplay_kdrv_adaptive_set_mode(int index, nxadapt_mode_t mode) {
    if (index < 0 || index >= 8) return -1;
    
    g_adapt_state[index].mode = mode;
    g_adapt_state[index].is_laptop = nxdisplay_kdrv_is_laptop();
    
    /* Set default thresholds */
    if (g_adapt_state[index].idle_hz == 0) {
        nxdisplay_info_t info;
        nxdisplay_kdrv_info(index, &info);
        
        g_adapt_state[index].idle_hz = 30;
        g_adapt_state[index].video_hz = 60;
        g_adapt_state[index].browser_hz = 60;
        g_adapt_state[index].work_hz = info.max_refresh_hz / 2;
        g_adapt_state[index].gaming_hz = info.max_refresh_hz;
        g_adapt_state[index].battery_threshold = 50;  /* Power save below 50% */
    }
    
    printk("[nxdisplay] Adaptive refresh mode %d for display %d (laptop=%d)\n",
           mode, index, g_adapt_state[index].is_laptop);
    
    return 0;
}

int nxdisplay_kdrv_adaptive_get_state(int index, nxadapt_state_t *state) {
    if (index < 0 || index >= 8 || !state) return -1;
    
    *state = g_adapt_state[index];
    return 0;
}

int nxdisplay_kdrv_adaptive_workload(int index, nxworkload_t workload) {
    if (index < 0 || index >= 8) return -1;
    
    nxadapt_state_t *state = &g_adapt_state[index];
    
    if (state->mode == NXADAPT_OFF) {
        return 0;  /* Adaptive disabled */
    }
    
    state->current_workload = workload;
    
    /* Determine target Hz based on workload and mode */
    uint32_t target_hz = 60;  /* Default */
    
    switch (workload) {
        case NXWORKLOAD_IDLE:
            target_hz = (state->mode == NXADAPT_POWER_SAVE) ? state->idle_hz : state->browser_hz;
            break;
        case NXWORKLOAD_VIDEO:
            target_hz = state->video_hz;
            break;
        case NXWORKLOAD_BROWSING:
            target_hz = state->browser_hz;
            break;
        case NXWORKLOAD_PRODUCTIVITY:
            target_hz = (state->mode == NXADAPT_PERFORMANCE) ? state->gaming_hz : state->work_hz;
            break;
        case NXWORKLOAD_GAMING:
        case NXWORKLOAD_FULL_SCREEN:
            target_hz = state->gaming_hz;
            break;
    }
    
    /* Battery override - force lower Hz when low */
    if (state->on_battery && state->mode != NXADAPT_PERFORMANCE) {
        /* Get battery level from power driver */
        /* For now, use work_hz as fallback on battery */
        if (target_hz > state->work_hz && workload != NXWORKLOAD_GAMING) {
            target_hz = state->work_hz;
        }
    }
    
    /* Apply if changed */
    if (target_hz != state->current_hz) {
        nxdisplay_kdrv_set_refresh(index, target_hz);
        state->current_hz = target_hz;
        
        printk("[nxdisplay] Adaptive: workload %d -> %d Hz\n", workload, target_hz);
    }
    
    return 0;
}

int nxdisplay_kdrv_adaptive_override(int index, uint32_t hz) {
    if (index < 0 || index >= 8) return -1;
    
    if (hz == 0) {
        /* Release override, return to adaptive */
        nxadapt_state_t *state = &g_adapt_state[index];
        return nxdisplay_kdrv_adaptive_workload(index, state->current_workload);
    }
    
    return nxdisplay_kdrv_set_refresh(index, hz);
}

int nxdisplay_kdrv_adaptive_configure(int index, const nxadapt_state_t *config) {
    if (index < 0 || index >= 8 || !config) return -1;
    
    g_adapt_state[index] = *config;
    return 0;
}

/* ============================================================================
 * Gaming Mode Implementation
 * 
 * One-click optimization:
 * - VRR (FreeSync/G-Sync)
 * - Max refresh rate
 * - Low latency mode
 * - HDR if available
 * ============================================================================ */

static nxgame_config_t g_game_config[8] = { 0 };
static nxgame_status_t g_game_status[8] = { 0 };

/* List of registered game executables for auto-detection */
#define MAX_REGISTERED_GAMES 64
static char g_registered_games[MAX_REGISTERED_GAMES][64] = { 0 };
static int g_registered_game_count = 0;

int nxdisplay_kdrv_game_mode_enable(int index, const nxgame_config_t *config) {
    if (index < 0 || index >= 8) return -1;
    
    nxdisplay_info_t info;
    if (nxdisplay_kdrv_info(index, &info) != 0) {
        return -1;
    }
    
    g_game_config[index] = config ? *config : (nxgame_config_t){
        .mode = NXGAME_MODE_ON,
        .vrr_enabled = 1,
        .hdr_enabled = 1,
        .low_latency = 1,
        .disable_compositor = 1,
        .target_hz = 0,  /* Max available */
        .vrr_min_hz = 48
    };
    
    nxgame_config_t *cfg = &g_game_config[index];
    
    /* Enable VRR if available and requested */
    if (cfg->vrr_enabled && info.has_vrr) {
        uint32_t vrr_max = cfg->target_hz ? cfg->target_hz : info.max_refresh_hz;
        nxdisplay_kdrv_vrr_enable(index, cfg->vrr_min_hz, vrr_max);
        printk("[nxdisplay] Gaming Mode: VRR enabled (%d-%d Hz)\n", 
               cfg->vrr_min_hz, vrr_max);
    }
    
    /* Enable HDR if available and requested */
    if (cfg->hdr_enabled && info.has_hdr) {
        nxdisplay_kdrv_hdr_enable(index, NXHDR_HDR10);
        printk("[nxdisplay] Gaming Mode: HDR enabled\n");
    }
    
    /* Set max refresh rate */
    uint32_t target = cfg->target_hz ? cfg->target_hz : info.max_refresh_hz;
    nxdisplay_kdrv_set_refresh(index, target);
    
    g_game_status[index].active = 1;
    g_game_status[index].vrr_current_hz = target;
    
    printk("[nxdisplay] Gaming Mode ENABLED on display %d\n", index);
    
    return 0;
}

int nxdisplay_kdrv_game_mode_disable(int index) {
    if (index < 0 || index >= 8) return -1;
    
    /* Disable VRR */
    nxdisplay_kdrv_vrr_disable(index);
    
    /* Disable HDR */
    nxdisplay_kdrv_hdr_disable(index);
    
    /* Restore defaults */
    nxdisplay_kdrv_set_refresh(index, 60);
    
    memset(&g_game_status[index], 0, sizeof(nxgame_status_t));
    
    printk("[nxdisplay] Gaming Mode DISABLED on display %d\n", index);
    
    return 0;
}

int nxdisplay_kdrv_game_mode_status(int index, nxgame_status_t *status) {
    if (index < 0 || index >= 8 || !status) return -1;
    
    *status = g_game_status[index];
    return 0;
}

int nxdisplay_kdrv_game_register(const char *exe_name) {
    if (!exe_name || g_registered_game_count >= MAX_REGISTERED_GAMES) {
        return -1;
    }
    
    strncpy(g_registered_games[g_registered_game_count], exe_name, 63);
    g_registered_game_count++;
    
    printk("[nxdisplay] Registered game: %s\n", exe_name);
    return 0;
}

int nxdisplay_kdrv_game_unregister(const char *exe_name) {
    if (!exe_name) return -1;
    
    for (int i = 0; i < g_registered_game_count; i++) {
        if (strcmp(g_registered_games[i], exe_name) == 0) {
            /* Shift remaining entries */
            for (int j = i; j < g_registered_game_count - 1; j++) {
                strcpy(g_registered_games[j], g_registered_games[j + 1]);
            }
            g_registered_game_count--;
            return 0;
        }
    }
    
    return -1;  /* Not found */
}

int nxdisplay_kdrv_game_mode_active(int index) {
    if (index < 0 || index >= 8) return 0;
    return g_game_status[index].active;
}

/* ============================================================================
 * Screen Power States
 * ============================================================================ */

static nxscreen_power_t g_screen_power[8] = { NXSCREEN_ON };

int nxdisplay_kdrv_screen_power(int index, nxscreen_power_t state) {
    if (index < 0 || index >= 8) return -1;
    
    g_screen_power[index] = state;
    
    /* Apply DPMS state */
    switch (state) {
        case NXSCREEN_ON:
            /* Power on display */
            nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_POWER_MODE, 0x01);
            break;
        case NXSCREEN_DIMMED:
            /* Reduce brightness to 30% */
            nxdisplay_kdrv_ddc_set_brightness(index, 30);
            break;
        case NXSCREEN_OFF:
        case NXSCREEN_LOCKED:
            /* DPMS standby */
            nxdisplay_kdrv_ddc_set_vcp(index, NXDDC_POWER_MODE, 0x02);
            break;
    }
    
    printk("[nxdisplay] Screen power state: %d\n", state);
    return 0;
}

nxscreen_power_t nxdisplay_kdrv_screen_get_power(int index) {
    if (index < 0 || index >= 8) return NXSCREEN_ON;
    return g_screen_power[index];
}

int nxdisplay_kdrv_screen_set_timeout(int index, uint16_t dim_seconds, uint16_t off_seconds) {
    /* Store timeout config - actual timer handled by power manager */
    (void)index;
    (void)dim_seconds;
    (void)off_seconds;
    return 0;
}
