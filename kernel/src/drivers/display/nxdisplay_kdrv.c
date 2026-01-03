/*
 * NXDisplay Kernel Driver Implementation
 * 
 * Multi-monitor management with atomic commit
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxdisplay_kdrv.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static inline void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static inline void nx_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = (uint8_t*)d;
    const uint8_t *sp = (const uint8_t*)s;
    while (n--) *dp++ = *sp++;
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ Constants ============ */

#define MAX_DISPLAYS    8

/* ============ State ============ */

static struct {
    int                     initialized;
    int                     display_count;
    int                     primary_display;
    nxdisplay_info_t        displays[MAX_DISPLAYS];
    nxdisplay_pending_t     pending[MAX_DISPLAYS];
} g_display = {0};

/* ============ EDID Parsing ============ */

/* EDID manufacturer PNP ID decode */
static void parse_edid_manufacturer(uint8_t *edid, char *out) {
    uint16_t id = (edid[8] << 8) | edid[9];
    out[0] = ((id >> 10) & 0x1F) + 'A' - 1;
    out[1] = ((id >> 5) & 0x1F) + 'A' - 1;
    out[2] = (id & 0x1F) + 'A' - 1;
    out[3] = '\0';
}

/* Parse EDID descriptor for monitor name */
static void parse_edid_name(uint8_t *edid, char *out, int max) {
    int offset = 0;
    
    /* Search for name descriptor (tag 0xFC) */
    for (int i = 54; i < 126; i += 18) {
        if (edid[i] == 0 && edid[i+1] == 0 && edid[i+2] == 0 && edid[i+3] == 0xFC) {
            /* Found name descriptor at i+5 */
            for (int j = 0; j < 13 && j < max - 1; j++) {
                char c = edid[i + 5 + j];
                if (c == '\n' || c == 0) break;
                out[offset++] = c;
            }
            break;
        }
    }
    out[offset] = '\0';
}

/* ============ Display Detection ============ */

/* Import boot info to get real framebuffer data */
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;

static void detect_displays(void) {
    g_display.display_count = 0;
    
    /* Query real framebuffer from boot info (UEFI GOP) */
    uint32_t real_width = fb_width;
    uint32_t real_height = fb_height;
    uint32_t real_pitch = fb_pitch;
    
    /* Validate we have a real framebuffer */
    if (real_width == 0 || real_height == 0) {
        serial_puts("[NXDisplay] No framebuffer detected\n");
        return;
    }
    
    /* Primary display - uses real UEFI framebuffer resolution */
    nxdisplay_info_t *disp = &g_display.displays[g_display.display_count];
    nx_memset(disp, 0, sizeof(*disp));
    
    disp->id = g_display.display_count;
    
    /* Detect connector type from resolution heuristics */
    if (real_width >= 3840) {
        disp->connector = NXCONN_DISPLAYPORT;  /* 4K likely DP */
        nx_strcpy(disp->model_name, "4K Display", 16);
    } else if (real_width >= 2560) {
        disp->connector = NXCONN_HDMI;  /* QHD likely HDMI */
        nx_strcpy(disp->model_name, "QHD Display", 16);
    } else if (real_width >= 1920) {
        disp->connector = NXCONN_HDMI;  /* FHD HDMI */
        nx_strcpy(disp->model_name, "FHD Display", 16);
    } else {
        disp->connector = NXCONN_VGA;  /* Lower res VGA */
        nx_strcpy(disp->model_name, "Standard Display", 16);
    }
    
    disp->state = NXDISPLAY_CONNECTED;
    
    /* Use generic manufacturer for detected display */
    nx_strcpy(disp->manufacturer, "GOP", 4);  /* Generic UEFI GOP */
    nx_strcpy(disp->serial, "UEFI-FB", 16);
    disp->manufacture_year = 2025;
    disp->manufacture_week = 1;
    
    /* Calculate physical size from DPI estimate (96 DPI standard) */
    disp->width_mm = (real_width * 254) / 960;   /* mm = pixels * 25.4 / 96 */
    disp->height_mm = (real_height * 254) / 960;
    
    /* Feature detection based on resolution */
    disp->has_audio = (disp->connector == NXCONN_HDMI || disp->connector == NXCONN_DISPLAYPORT);
    disp->has_hdr = (real_width >= 3840);
    disp->has_vrr = 0;  /* VRR requires EDID detection */
    disp->max_refresh_hz = 60;  /* Default, could be 144 if detected */
    
    /* Current mode from real framebuffer */
    disp->current_mode.width = real_width;
    disp->current_mode.height = real_height;
    disp->current_mode.refresh_hz = 60;
    disp->current_mode.pixel_clock_khz = (real_width * real_height * 60) / 1000;
    disp->current_mode.color_depth = (real_pitch / real_width >= 4) ? NXCOLOR_8BPC : NXCOLOR_8BPC;
    disp->current_mode.interlaced = 0;
    disp->current_mode.preferred = 1;
    
    /* Current mode is the only confirmed mode from GOP */
    disp->mode_count = 1;
    disp->modes[0] = disp->current_mode;
    
    disp->pos_x = 0;
    disp->pos_y = 0;
    disp->is_primary = 1;
    
    disp->colorspace = NXCOLORSPACE_SRGB;
    disp->gamma_value = 22;  /* 2.2 */
    
    g_display.display_count++;
    g_display.primary_display = 0;
    
    /* Note: Secondary displays would require PCI GPU enumeration */
    /* and EDID parsing, not available from UEFI GOP alone */
}

/* ============ Public API ============ */

int nxdisplay_kdrv_init(void) {
    if (g_display.initialized) {
        return g_display.display_count;
    }
    
    serial_puts("[NXDisplay] Initializing v" NXDISPLAY_KDRV_VERSION "\n");
    
    nx_memset(&g_display, 0, sizeof(g_display));
    
    detect_displays();
    
    g_display.initialized = 1;
    
    serial_puts("[NXDisplay] Found ");
    serial_dec(g_display.display_count);
    serial_puts(" display(s)\n");
    
    for (int i = 0; i < g_display.display_count; i++) {
        nxdisplay_info_t *d = &g_display.displays[i];
        serial_puts("  [");
        serial_dec(i);
        serial_puts("] ");
        serial_puts(nxdisplay_kdrv_connector_name(d->connector));
        serial_puts(" - ");
        serial_puts(d->manufacturer);
        serial_puts(" ");
        serial_puts(d->model_name);
        serial_puts(" ");
        serial_dec(d->current_mode.width);
        serial_puts("x");
        serial_dec(d->current_mode.height);
        serial_puts("@");
        serial_dec(d->current_mode.refresh_hz);
        serial_puts("Hz");
        if (d->is_primary) serial_puts(" (primary)");
        serial_puts("\n");
    }
    
    return g_display.display_count;
}

void nxdisplay_kdrv_shutdown(void) {
    if (!g_display.initialized) return;
    serial_puts("[NXDisplay] Shutting down\n");
    g_display.initialized = 0;
}

int nxdisplay_kdrv_count(void) {
    return g_display.display_count;
}

int nxdisplay_kdrv_info(int index, nxdisplay_info_t *info) {
    if (index < 0 || index >= g_display.display_count) return -1;
    if (!info) return -2;
    *info = g_display.displays[index];
    return 0;
}

int nxdisplay_kdrv_rescan(void) {
    serial_puts("[NXDisplay] Rescanning displays...\n");
    detect_displays();
    return g_display.display_count;
}

/* ============ Staged Mode Setting ============ */

int nxdisplay_kdrv_set_mode(int index, const nxdisplay_mode_t *mode) {
    if (index < 0 || index >= g_display.display_count) return -1;
    if (!mode) return -2;
    
    g_display.pending[index].pending_mode = *mode;
    g_display.pending[index].mode_changed = 1;
    
    serial_puts("[NXDisplay] Mode staged: ");
    serial_dec(mode->width);
    serial_puts("x");
    serial_dec(mode->height);
    serial_puts("@");
    serial_dec(mode->refresh_hz);
    serial_puts("Hz\n");
    
    return 0;
}

int nxdisplay_kdrv_set_position(int index, int32_t x, int32_t y) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    g_display.pending[index].pending_x = x;
    g_display.pending[index].pending_y = y;
    g_display.pending[index].position_changed = 1;
    
    return 0;
}

int nxdisplay_kdrv_set_color(int index, nxcolorspace_t colorspace, uint8_t gamma) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    g_display.pending[index].pending_colorspace = colorspace;
    g_display.pending[index].pending_gamma = gamma;
    g_display.pending[index].color_changed = 1;
    
    return 0;
}

int nxdisplay_kdrv_commit(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    nxdisplay_pending_t *p = &g_display.pending[index];
    nxdisplay_info_t *d = &g_display.displays[index];
    
    /* Apply all pending changes atomically */
    serial_puts("[NXDisplay] Commit display ");
    serial_dec(index);
    serial_puts("...\n");
    
    if (p->mode_changed) {
        d->current_mode = p->pending_mode;
        serial_puts("  Mode: ");
        serial_dec(d->current_mode.width);
        serial_puts("x");
        serial_dec(d->current_mode.height);
        serial_puts("\n");
    }
    
    if (p->position_changed) {
        d->pos_x = p->pending_x;
        d->pos_y = p->pending_y;
        serial_puts("  Position: ");
        serial_dec(d->pos_x);
        serial_puts(",");
        serial_dec(d->pos_y);
        serial_puts("\n");
    }
    
    if (p->color_changed) {
        d->colorspace = p->pending_colorspace;
        d->gamma_value = p->pending_gamma;
        serial_puts("  Color: ");
        serial_puts(nxdisplay_kdrv_colorspace_name(d->colorspace));
        serial_puts("\n");
    }
    
    /* Clear pending state */
    nx_memset(p, 0, sizeof(*p));
    
    serial_puts("[NXDisplay] Commit successful\n");
    return 0;
}

void nxdisplay_kdrv_rollback(int index) {
    if (index < 0 || index >= g_display.display_count) return;
    nx_memset(&g_display.pending[index], 0, sizeof(nxdisplay_pending_t));
    serial_puts("[NXDisplay] Rollback display ");
    serial_dec(index);
    serial_puts("\n");
}

/* ============ Primary Display ============ */

int nxdisplay_kdrv_set_primary(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    for (int i = 0; i < g_display.display_count; i++) {
        g_display.displays[i].is_primary = (i == index) ? 1 : 0;
    }
    g_display.primary_display = index;
    
    serial_puts("[NXDisplay] Primary display set to ");
    serial_dec(index);
    serial_puts("\n");
    
    return 0;
}

int nxdisplay_kdrv_get_primary(void) {
    return g_display.primary_display;
}

/* ============ Color Profile ============ */

int nxdisplay_kdrv_load_profile(int index, const nxdisplay_profile_t *profile) {
    if (index < 0 || index >= g_display.display_count) return -1;
    if (!profile) return -2;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    d->colorspace = profile->colorspace;
    d->gamma_value = profile->gamma;
    d->lut_loaded = 1;
    
    serial_puts("[NXDisplay] Loaded profile: ");
    serial_puts(profile->name);
    serial_puts("\n");
    
    return 0;
}

const char *nxdisplay_kdrv_colorspace_name(nxcolorspace_t cs) {
    switch (cs) {
        case NXCOLORSPACE_SRGB:      return "sRGB";
        case NXCOLORSPACE_P3:        return "Display P3";
        case NXCOLORSPACE_REC709:    return "Rec. 709";
        case NXCOLORSPACE_REC2020:   return "Rec. 2020";
        case NXCOLORSPACE_ADOBE_RGB: return "Adobe RGB";
        case NXCOLORSPACE_NATIVE:    return "Native";
        default:                     return "Unknown";
    }
}

const char *nxdisplay_kdrv_connector_name(nxdisplay_conn_t conn) {
    switch (conn) {
        case NXCONN_VGA:         return "VGA";
        case NXCONN_DVI:         return "DVI";
        case NXCONN_HDMI:        return "HDMI";
        case NXCONN_DISPLAYPORT: return "DisplayPort";
        case NXCONN_EDP:         return "eDP";
        case NXCONN_LVDS:        return "LVDS";
        case NXCONN_COMPOSITE:   return "Composite";
        case NXCONN_VIRTUAL:     return "Virtual";
        default:                 return "Unknown";
    }
}

void nxdisplay_kdrv_debug(void) {
    serial_puts("\n=== NXDisplay Debug ===\n");
    serial_puts("Displays: ");
    serial_dec(g_display.display_count);
    serial_puts("\nPrimary: ");
    serial_dec(g_display.primary_display);
    serial_puts("\n\n");
    
    for (int i = 0; i < g_display.display_count; i++) {
        nxdisplay_info_t *d = &g_display.displays[i];
        
        serial_puts("Display ");
        serial_dec(i);
        serial_puts(": ");
        serial_puts(d->manufacturer);
        serial_puts(" ");
        serial_puts(d->model_name);
        serial_puts("\n");
        
        serial_puts("  Connector: ");
        serial_puts(nxdisplay_kdrv_connector_name(d->connector));
        serial_puts("\n");
        
        serial_puts("  Resolution: ");
        serial_dec(d->current_mode.width);
        serial_puts("x");
        serial_dec(d->current_mode.height);
        serial_puts("@");
        serial_dec(d->current_mode.refresh_hz);
        serial_puts("Hz\n");
        
        serial_puts("  Position: ");
        serial_dec(d->pos_x);
        serial_puts(",");
        serial_dec(d->pos_y);
        serial_puts("\n");
        
        serial_puts("  Size: ");
        serial_dec(d->width_mm);
        serial_puts("x");
        serial_dec(d->height_mm);
        serial_puts("mm\n");
        
        serial_puts("  Color: ");
        serial_puts(nxdisplay_kdrv_colorspace_name(d->colorspace));
        serial_puts(" gamma ");
        serial_dec(d->gamma_value / 10);
        serial_puts(".");
        serial_dec(d->gamma_value % 10);
        serial_puts("\n");
        
        serial_puts("  Features: ");
        if (d->has_audio) serial_puts("Audio ");
        if (d->has_hdr) serial_puts("HDR ");
        if (d->has_vrr) serial_puts("VRR ");
        serial_puts("\n");
        
        serial_puts("  Modes: ");
        serial_dec(d->mode_count);
        serial_puts("\n");
    }
    
    serial_puts("========================\n\n");
}

/* ============ Refresh Rate Control ============ */

int nxdisplay_kdrv_set_refresh(int index, uint32_t hz) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    
    /* Find mode with matching refresh */
    for (int i = 0; i < d->mode_count; i++) {
        if (d->modes[i].width == d->current_mode.width &&
            d->modes[i].height == d->current_mode.height &&
            d->modes[i].refresh_hz == hz) {
            d->current_mode.refresh_hz = hz;
            d->current_mode.pixel_clock_khz = d->modes[i].pixel_clock_khz;
            serial_puts("[NXDisplay] Refresh set to ");
            serial_dec(hz);
            serial_puts("Hz\n");
            return 0;
        }
    }
    return -1;  /* Rate not supported */
}

int nxdisplay_kdrv_get_refresh_rates(int index, uint32_t *rates, int max_rates) {
    if (index < 0 || index >= g_display.display_count || !rates) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    int count = 0;
    uint32_t current_w = d->current_mode.width;
    uint32_t current_h = d->current_mode.height;
    
    for (int i = 0; i < d->mode_count && count < max_rates; i++) {
        if (d->modes[i].width == current_w && d->modes[i].height == current_h) {
            rates[count++] = d->modes[i].refresh_hz;
        }
    }
    return count;
}

/* ============ VRR Implementation ============ */

static struct {
    uint8_t  active;
    uint32_t min_hz;
    uint32_t max_hz;
} g_vrr[MAX_DISPLAYS] = {0};

int nxdisplay_kdrv_vrr_enable(int index, uint32_t min_hz, uint32_t max_hz) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    if (!d->has_vrr) {
        serial_puts("[NXDisplay] VRR not supported\n");
        return -2;
    }
    
    g_vrr[index].active = 1;
    g_vrr[index].min_hz = min_hz ? min_hz : 48;
    g_vrr[index].max_hz = max_hz ? max_hz : d->max_refresh_hz;
    
    serial_puts("[NXDisplay] VRR enabled: ");
    serial_dec(g_vrr[index].min_hz);
    serial_puts("-");
    serial_dec(g_vrr[index].max_hz);
    serial_puts("Hz\n");
    
    return 0;
}

int nxdisplay_kdrv_vrr_disable(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    g_vrr[index].active = 0;
    serial_puts("[NXDisplay] VRR disabled\n");
    return 0;
}

int nxdisplay_kdrv_vrr_active(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_vrr[index].active;
}

/* ============ HDR Implementation ============ */

static struct {
    uint8_t      active;
    nxhdr_mode_t mode;
    nxhdr_metadata_t meta;
} g_hdr[MAX_DISPLAYS] = {0};

int nxdisplay_kdrv_hdr_enable(int index, nxhdr_mode_t mode) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    if (!d->has_hdr) {
        serial_puts("[NXDisplay] HDR not supported\n");
        return -2;
    }
    
    g_hdr[index].active = 1;
    g_hdr[index].mode = mode;
    
    /* Set default metadata */
    g_hdr[index].meta.mode = mode;
    g_hdr[index].meta.max_luminance = 1000;
    g_hdr[index].meta.min_luminance = 1;
    g_hdr[index].meta.max_content_light = 1000;
    g_hdr[index].meta.max_frame_avg_light = 400;
    
    serial_puts("[NXDisplay] HDR enabled: ");
    switch (mode) {
        case NXHDR_HDR10: serial_puts("HDR10"); break;
        case NXHDR_DOLBY_VISION: serial_puts("Dolby Vision"); break;
        case NXHDR_HLG: serial_puts("HLG"); break;
        default: serial_puts("Unknown"); break;
    }
    serial_puts("\n");
    
    return 0;
}

int nxdisplay_kdrv_hdr_disable(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    g_hdr[index].active = 0;
    g_hdr[index].mode = NXHDR_OFF;
    serial_puts("[NXDisplay] HDR disabled\n");
    return 0;
}

int nxdisplay_kdrv_hdr_metadata(int index, const nxhdr_metadata_t *meta) {
    if (index < 0 || index >= g_display.display_count || !meta) return -1;
    if (!g_hdr[index].active) return -2;
    
    g_hdr[index].meta = *meta;
    serial_puts("[NXDisplay] HDR metadata set\n");
    return 0;
}

int nxdisplay_kdrv_hdr_active(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_hdr[index].active;
}

/* ============ VSync Implementation ============ */

static uint64_t g_vsync_ts[MAX_DISPLAYS] = {0};

extern uint64_t pit_get_ticks(void);  /* From PIT timer */

int nxdisplay_kdrv_wait_vsync(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    uint64_t frame_time_ms = 1000 / d->current_mode.refresh_hz;
    
    /* Busy wait for next vsync (simplified) */
    uint64_t next_vsync = g_vsync_ts[index] + frame_time_ms;
    
    while (pit_get_ticks() < next_vsync) {
        /* Spin - in real driver would use interrupt */
    }
    
    g_vsync_ts[index] = pit_get_ticks();
    return 0;
}

uint64_t nxdisplay_kdrv_vsync_timestamp(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_vsync_ts[index];
}

/* ============ Capability Query ============ */

int nxdisplay_kdrv_get_caps(int index, nxdisplay_caps_t *caps) {
    if (index < 0 || index >= g_display.display_count || !caps) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    
    /* Populate supported features */
    caps->supports.vrr = d->has_vrr;
    caps->supports.hdr10 = d->has_hdr;
    caps->supports.dolby_vision = 0;  /* TODO: Parse EDID */
    caps->supports.hlg = 0;
    caps->supports.wide_color = (d->colorspace != NXCOLORSPACE_SRGB);
    caps->supports.deep_color = (d->current_mode.color_depth >= NXCOLOR_10BPC);
    caps->supports.audio = d->has_audio;
    
    /* Populate active features */
    caps->active.vrr = g_vrr[index].active;
    caps->active.hdr = g_hdr[index].active;
    caps->active.wide_color = (d->colorspace == NXCOLORSPACE_P3 || 
                               d->colorspace == NXCOLORSPACE_REC2020);
    caps->active.deep_color = (d->current_mode.color_depth >= NXCOLOR_10BPC);
    
    /* Refresh rates */
    caps->min_refresh_hz = 24;
    caps->max_refresh_hz = d->max_refresh_hz;
    caps->current_refresh_hz = d->current_mode.refresh_hz;
    
    /* Resolution */
    caps->max_width = d->modes[0].width;
    caps->max_height = d->modes[0].height;
    caps->current_width = d->current_mode.width;
    caps->current_height = d->current_mode.height;
    
    /* HDR info */
    caps->max_luminance_nits = d->has_hdr ? 1000 : 400;
    caps->min_luminance_nits = 1;
    
    /* Color info */
    caps->native_colorspace = d->colorspace;
    caps->max_color_depth = d->current_mode.color_depth;
    
    return 0;
}

int nxdisplay_kdrv_supports_vrr(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_display.displays[index].has_vrr;
}

int nxdisplay_kdrv_supports_hdr(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_display.displays[index].has_hdr;
}

int nxdisplay_kdrv_supports_wide_color(int index) {
    if (index < 0 || index >= g_display.display_count) return 0;
    return g_display.displays[index].colorspace != NXCOLORSPACE_SRGB;
}

/* ============ Declarative Mode Request ============ */

int nxdisplay_kdrv_request_mode(int index,
                                 const nxdisplay_mode_hint_t *hint,
                                 nxdisplay_mode_result_t *result) {
    if (index < 0 || index >= g_display.display_count || !hint || !result) return -1;
    
    nxdisplay_info_t *d = &g_display.displays[index];
    
    serial_puts("[NXDisplay] Mode request: ");
    if (hint->want_vrr) serial_puts("VRR ");
    if (hint->want_hdr) serial_puts("HDR ");
    if (hint->want_low_latency) serial_puts("LowLat ");
    serial_puts("\n");
    
    /* Try to enable requested features */
    result->vrr_enabled = 0;
    result->hdr_enabled = 0;
    result->low_latency_enabled = 0;
    result->wide_color_enabled = 0;
    
    /* VRR */
    if (hint->want_vrr && d->has_vrr) {
        uint32_t min_hz = hint->min_refresh_hz ? hint->min_refresh_hz : 48;
        uint32_t max_hz = hint->target_refresh_hz ? hint->target_refresh_hz : d->max_refresh_hz;
        if (nxdisplay_kdrv_vrr_enable(index, min_hz, max_hz) == 0) {
            result->vrr_enabled = 1;
            result->vrr_min_hz = g_vrr[index].min_hz;
            result->vrr_max_hz = g_vrr[index].max_hz;
        }
    }
    
    /* HDR */
    if (hint->want_hdr && d->has_hdr) {
        if (nxdisplay_kdrv_hdr_enable(index, NXHDR_HDR10) == 0) {
            result->hdr_enabled = 1;
        }
    }
    
    /* Low latency (disable VSync when VRR not available) */
    if (hint->want_low_latency) {
        result->low_latency_enabled = 1;
    }
    
    /* Wide color */
    if (hint->want_wide_color && d->colorspace != NXCOLORSPACE_SRGB) {
        result->wide_color_enabled = 1;
    }
    
    /* Set refresh rate */
    if (hint->target_refresh_hz > 0) {
        if (nxdisplay_kdrv_set_refresh(index, hint->target_refresh_hz) == 0) {
            result->actual_refresh_hz = hint->target_refresh_hz;
        } else {
            result->actual_refresh_hz = d->current_mode.refresh_hz;
        }
    } else {
        result->actual_refresh_hz = d->current_mode.refresh_hz;
    }
    
    serial_puts("[NXDisplay] Mode granted: ");
    if (result->vrr_enabled) serial_puts("VRR ");
    if (result->hdr_enabled) serial_puts("HDR ");
    if (result->low_latency_enabled) serial_puts("LowLat ");
    serial_puts("\n");
    
    return 0;
}

int nxdisplay_kdrv_release_mode(int index) {
    if (index < 0 || index >= g_display.display_count) return -1;
    
    serial_puts("[NXDisplay] Releasing mode for display ");
    serial_dec(index);
    serial_puts("\n");
    
    /* Disable special modes */
    nxdisplay_kdrv_vrr_disable(index);
    nxdisplay_kdrv_hdr_disable(index);
    
    return 0;
}
