/*
 * NeolyxOS Desktop Shell Implementation
 * 
 * macOS-inspired desktop with menu bar, dock, and window compositor.
 * Now runs in USERSPACE using syscalls instead of kernel functions.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/desktop_shell.h"
#include "../include/app_drawer.h"
#include "../include/nxsyscall.h"
#include "../include/dock.h"
#include "../include/control_center.h"
#include "../include/notification_center.h"
#include "../include/nxevent.h"
#include "../include/anveshan.h"
#include "../include/window_manager.h"
#include "../include/nxrender_bridge.h"

/* Import Calendar.app functions for time handling (no duplicate code) */
#include "../apps/Calendar.app/calendar.h"

#include "../include/wallpaper.h"
#include "../include/yantra_grid.h"
#include "../include/vm_detect.h"
#include "../include/volume_osd.h"
#include "../include/keyboard_osd.h"
#include "../include/theme_osd.h"
#include "../include/battery_warning.h"
#include "../include/network_indicator.h"
#include "../include/nxui.h"
#include "../include/nxglass.h"
#include "../include/nxframe.h"
#include "../include/desktop_items.h"
#include "../include/menubar.h"
#include "../include/app_view.h"
#include "../include/setup_assistant.h"
#include "../include/overview.h"
#include "../include/stage.h"
#include "../include/switcher.h"
#include "../lib/nxicons/nxicons_internal.h"
#include "../include/nxicon_pack.h"
#include "nxserver/nxserver.h"
#include "nxserver/nxs_damage.h"
#define nxc_cosf_shell nxc_cosf_pub
#define nxc_sinf_shell nxc_sinf_pub

/* Centralized config system */
#include "nx_config.h"

/* ============ Forward Declarations ============ */
static void serial_dec(int64_t val);
static void serial_hex64(uint64_t val);
static inline void serial_puts(const char *s);
static inline void serial_putc(char c);

/* ============ Userspace Syscall Wrappers ============ */

/* Framebuffer info (obtained via syscall on init) */
static fb_info_t g_fb_info;
static volatile uint32_t *g_framebuffer = 0;  /* Front buffer (visible) */
static uint32_t *g_backbuffer = 0;            /* Back buffer (render target) */
static uint32_t *g_static_cache = 0;          /* Static layer cache (background + UI widgets) */
static uint64_t g_buffer_size = 0;            /* Size in bytes */
static int g_fb_initialized = 0;
static int g_static_cache_valid = 0;          /* Set to 1 after static layer is rendered */

/* Initialize framebuffer via syscall */
static int init_framebuffer(void) {
    if (g_fb_initialized) return 0;
    
    /* Get framebuffer info */
    if (nx_fb_info(&g_fb_info) != 0) {
        nx_debug_print("[DESKTOP] fb_info failed\n");
        return -1;
    }
    
    /* Map framebuffer (front buffer) to our address space */
    g_framebuffer = (volatile uint32_t*)nx_fb_map();
    if (!g_framebuffer) {
        nx_debug_print("[DESKTOP] fb_map failed\n");
        return -1;
    }
    
    /*
     * CRITICAL FIX: For backbuffer (allocated linear memory), use width*4 as stride.
     * The hardware pitch may have padding that doesn't apply to our linear buffer.
     * The kernel's fb_flip will handle the pitch conversion.
     */
    uint32_t bb_stride = g_fb_info.width * 4;  /* Bytes per row for linear backbuffer */
    g_buffer_size = (uint64_t)bb_stride * g_fb_info.height;
    
    /*
     * Double buffering: Allocate backbuffer from userspace heap.
     * Desktop renders to backbuffer, then kernel copies to framebuffer.
     * This eliminates flicker by showing complete frames only.
     */
    nx_debug_print("[DESKTOP] Allocating backbuffer...\n");
    
    /* TEMPORARY: Force single-buffer mode to bypass pitch issues
     * Renders directly to hardware framebuffer (may have tearing but should work)
     */
    g_backbuffer = NULL;  /* Force fallback to framebuffer */
    nx_debug_print("[DESKTOP] nx_alloc skipped (forcing single buffer)\n");
    
    if (!g_backbuffer) {
        nx_debug_print("[DESKTOP] Using single buffer mode (direct to HW FB)\n");
        g_backbuffer = (uint32_t*)g_framebuffer;
        /* For direct framebuffer access, use hardware pitch */
        g_buffer_size = (uint64_t)g_fb_info.pitch * g_fb_info.height;
    } else {
        nx_debug_print("[DESKTOP] Double buffering enabled\n");
        /* Clear backbuffer to black */
        for (uint64_t i = 0; i < g_buffer_size / 4; i++) {
            g_backbuffer[i] = 0xFF000000;
        }
    }
    
    /*
     * Static layer cache: Stores rendered background + unchanging UI widgets.
     * This is rendered once and copied to backbuffer each frame, avoiding
     * the need to redraw static elements and eliminating flicker.
     */
    g_static_cache = (uint32_t*)nx_alloc(g_buffer_size);
    if (!g_static_cache) {
        nx_debug_print("[DESKTOP] static cache alloc failed - no static caching\n");
    } else {
        nx_debug_print("[DESKTOP] Static layer caching enabled\n");
    }
    g_static_cache_valid = 0;
    
    /* Initialize NXRender graphics library */
    if (nxr_bridge_init(g_backbuffer, g_fb_info.width, g_fb_info.height, g_fb_info.pitch) < 0) {
        nx_debug_print("[DESKTOP] NXRender init failed - using fallback rendering\n");
        /* Continue without NXRender - use direct pixel rendering */
    } else {
        nx_debug_print("[DESKTOP] NXRender initialized successfully\n");
    }
    
    g_fb_initialized = 1;
    return 0;
}

/* Flip back buffer to front buffer via kernel (VSync synchronized) */
static void flip_buffer(void) {
    /* Debug: trace first few flips to verify syscall is called */
    static int flip_cnt = 0;
    flip_cnt++;
    if (flip_cnt <= 5) {
        nx_debug_print("[FLIP] called\n");
    }
    
    if (g_backbuffer == (uint32_t*)g_framebuffer) {
        /* Single buffer mode - still need to trigger kernel cursor draw! */
        /* Pass NULL to signal single-buffer mode, kernel still draws cursor */
        nx_fb_flip(0);
        return;
    }
    
    /* Use kernel syscall for VSync-synchronized flip */
    nx_fb_flip(g_backbuffer);
}

/*
 * Restore the static layer cache to the backbuffer.
 * This is called at the start of each frame to provide a clean
 * background (with nav bar, clock, search, icons already rendered)
 * before drawing dynamic content (windows, dock hover state, cursor).
 */
static void restore_static_layer(void) {
    if (!g_static_cache || !g_static_cache_valid) return;
    
    /* Fast copy using optimized loop */
    uint64_t count = g_buffer_size / 4;  /* Number of 32-bit pixels */
    for (uint64_t i = 0; i < count; i++) {
        g_backbuffer[i] = g_static_cache[i];
    }
}

/* Memory allocation wrappers */
#define kmalloc(size)   nx_alloc(size)
#define kfree(ptr)      nx_free(ptr)

/* Time wrapper */
#define pit_get_ticks() nx_gettime()

/* Serial output - forward to nx_debug_print syscall */
static inline void serial_puts(const char *s) { nx_debug_print(s); }
static inline void serial_putc(char c) { char buf[2] = {c, 0}; nx_debug_print(buf); }

/* ============ RTC Time Cache ============ */
/* 
 * Cache RTC time to avoid syscall overhead on every frame.
 * Desktop runs at ~60 FPS, but clock only needs 1 Hz updates.
 */
static rtc_time_t g_cached_rtc_time;
static uint64_t g_rtc_cache_last_update = 0;
static int g_rtc_cache_valid = 0;

/* Use Calendar.app's calendar_days_in_month() instead of duplicate array */

static uint64_t g_clock_snapshot_ms = 0;   /* uptime at the last clock_tick */

/* may_refresh: re-read the RTC once the cache is a second old. Only the
 * frame tick does (clock_tick); drawing reads the cache, so every pass of a
 * frame, and every redraw of the same frame, shows the same time. */
static int get_cached_rtc_time(rtc_time_t *out, int may_refresh) {
    uint64_t now_ms = nx_gettime();
    
    /* Update cache if invalid or 1 second has passed */
    if (!g_rtc_cache_valid || (may_refresh && now_ms - g_rtc_cache_last_update >= 1000)) {
        if (nx_rtc_get(&g_cached_rtc_time) == 0) {
            /* Apply timezone offset from config (not hardcoded) */
            int16_t tz_offset = nx_config_tz_offset();  /* Minutes from UTC */
            int16_t offset_hours = tz_offset / 60;
            int16_t offset_mins = tz_offset % 60;
            
            g_cached_rtc_time.minute += offset_mins;
            if (g_cached_rtc_time.minute >= 60) {
                g_cached_rtc_time.minute -= 60;
                offset_hours += 1;
            } else if (g_cached_rtc_time.minute < 0) {
                g_cached_rtc_time.minute += 60;
                offset_hours -= 1;
            }
            g_cached_rtc_time.hour += offset_hours;
            if (g_cached_rtc_time.hour >= 24) {
                g_cached_rtc_time.hour -= 24;
                /* Day rollover - increment date properly */
                uint8_t max_day = (uint8_t)calendar_days_in_month(g_cached_rtc_time.year, g_cached_rtc_time.month);
                g_cached_rtc_time.day += 1;
                if (g_cached_rtc_time.day > max_day) {
                    g_cached_rtc_time.day = 1;
                    g_cached_rtc_time.month += 1;
                    if (g_cached_rtc_time.month > 12) {
                        g_cached_rtc_time.month = 1;
                        g_cached_rtc_time.year += 1;
                    }
                }
            } else if (g_cached_rtc_time.hour < 0) {
                g_cached_rtc_time.hour += 24;
                /* Day rollback */
                g_cached_rtc_time.day -= 1;
                if (g_cached_rtc_time.day < 1) {
                    g_cached_rtc_time.month -= 1;
                    if (g_cached_rtc_time.month < 1) {
                        g_cached_rtc_time.month = 12;
                        g_cached_rtc_time.year -= 1;
                    }
                    g_cached_rtc_time.day = (uint8_t)calendar_days_in_month(g_cached_rtc_time.year, g_cached_rtc_time.month);
                }
            }
            
            g_rtc_cache_valid = 1;
            g_rtc_cache_last_update = now_ms;
        } else {
            return -1;
        }
    }
    
    *out = g_cached_rtc_time;
    return 0;
}


/* ============ Input Handling via Syscall ============ */

static input_event_t g_last_input;
static int g_input_registered = 0;

/* Poll for mouse state using syscalls */
int mouse_get_state(int32_t *dx, int32_t *dy, uint8_t *buttons) {
    if (!g_input_registered) {
        nx_input_register(2);  /* Register for mouse events */
        g_input_registered = 1;
    }
    
    if (nx_input_poll(&g_last_input) == 0 && 
        (g_last_input.type == INPUT_TYPE_MOUSE_MOVE || g_last_input.type == INPUT_TYPE_MOUSE_BUTTON)) {
        *dx = g_last_input.mouse.dx;
        *dy = g_last_input.mouse.dy;
        *buttons = g_last_input.mouse.buttons;
        return 0;
    }
    
    *dx = 0;
    *dy = 0;
    *buttons = 0;
    return -1;
}

/* Check for keyboard input */
static int kbd_check_key(void) {
    if (nx_input_poll(&g_last_input) == 0 && g_last_input.type == INPUT_TYPE_KEYBOARD) {
        return 1;
    }
    return 0;
}

/* Get keyboard character */
static uint8_t kbd_getchar(void) {
    if (g_last_input.type == INPUT_TYPE_KEYBOARD && g_last_input.keyboard.pressed) {
        return g_last_input.keyboard.ascii;
    }
    return 0;
}

/* ============ Static State ============ */

static desktop_state_t g_desktop;
static desktop_window_t g_windows[MAX_WINDOWS];
static uint32_t g_next_window_id = 1;
static float g_night_mode_strength = 0.0f;  /* 0.0=off, 1.0=full warmth */

/* Complete 8x8 bitmap font for all printable ASCII (32-126) */
static const uint8_t font_8x8[96][8] = {
    /* 32 Space */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 33 ! */     {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* 34 " */     {0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 35 # */     {0x6C, 0xFE, 0x6C, 0x6C, 0xFE, 0x6C, 0x00, 0x00},
    /* 36 $ */     {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00},
    /* 37 % */     {0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00, 0x00},
    /* 38 & */     {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    /* 39 ' */     {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 40 ( */     {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    /* 41 ) */     {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    /* 42 * */     {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    /* 43 + */     {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    /* 44 , */     {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    /* 45 - */     {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    /* 46 . */     {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* 47 / */     {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00, 0x00},
    /* 48 0 */     {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    /* 49 1 */     {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* 50 2 */     {0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00},
    /* 51 3 */     {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    /* 52 4 */     {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00},
    /* 53 5 */     {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    /* 54 6 */     {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
    /* 55 7 */     {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    /* 56 8 */     {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    /* 57 9 */     {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
    /* 58 : */     {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00},
    /* 59 ; */     {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00},
    /* 60 < */     {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    /* 61 = */     {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    /* 62 > */     {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00},
    /* 63 ? */     {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    /* 64 @ */     {0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00},
    /* 65 A */     {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00},
    /* 66 B */     {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    /* 67 C */     {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    /* 68 D */     {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    /* 69 E */     {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
    /* 70 F */     {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00},
    /* 71 G */     {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00},
    /* 72 H */     {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    /* 73 I */     {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* 74 J */     {0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    /* 75 K */     {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    /* 76 L */     {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    /* 77 M */     {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00},
    /* 78 N */     {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00},
    /* 79 O */     {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 80 P */     {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    /* 81 Q */     {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00},
    /* 82 R */     {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00},
    /* 83 S */     {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
    /* 84 T */     {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* 85 U */     {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 86 V */     {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    /* 87 W */     {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},
    /* 88 X */     {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    /* 89 Y */     {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    /* 90 Z */     {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    /* 91 [ */     {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    /* 92 \ */     {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00},
    /* 93 ] */     {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    /* 94 ^ */     {0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 95 _ */     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00},
    /* 96 ` */     {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 97 a */     {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
    /* 98 b */     {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    /* 99 c */     {0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00},
    /* 100 d */    {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    /* 101 e */    {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    /* 102 f */    {0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00},
    /* 103 g */    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    /* 104 h */    {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    /* 105 i */    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 106 j */    {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38},
    /* 107 k */    {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
    /* 108 l */    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 109 m */    {0x00, 0x00, 0x6C, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    /* 110 n */    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    /* 111 o */    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 112 p */    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
    /* 113 q */    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
    /* 114 r */    {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00},
    /* 115 s */    {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    /* 116 t */    {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00},
    /* 117 u */    {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
    /* 118 v */    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    /* 119 w */    {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00},
    /* 120 x */    {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
    /* 121 y */    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    /* 122 z */    {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    /* 123 { */    {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    /* 124 | */    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* 125 } */    {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    /* 126 ~ */    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 127 DEL - blank */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/* ============ Utility Functions ============ */

static void serial_dec(int64_t val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static void serial_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_puts("."); // Debug: verify loop runs
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static int shell_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void shell_strcpy(char *dst, const char *src, int max) {
    if (!dst || max <= 0) return;
    if (!src) { dst[0] = '\0'; return; }  /* NULL src = empty string */
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ Drawing Primitives ============ */

static inline void put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && x < (int32_t)g_desktop.width && 
        y >= 0 && y < (int32_t)g_desktop.height) {
        g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x] = color;
    }
}

void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            put_pixel(x + i, y + j, color);
        }
    }
}

void desktop_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    /* Top and bottom */
    for (uint32_t i = 0; i < w; i++) {
        put_pixel(x + i, y, color);
        put_pixel(x + i, y + h - 1, color);
    }
    /* Left and right */
    for (uint32_t j = 0; j < h; j++) {
        put_pixel(x, y + j, color);
        put_pixel(x + w - 1, y + j, color);
    }
}

void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    while (*text) {
        uint8_t ch = *text;
        if (ch >= 32 && ch < 128) {
            int idx = ch - 32;
            if (idx < 96) {
                for (int row = 0; row < 8; row++) {
                    uint8_t bits = font_8x8[idx][row];
                    for (int col = 0; col < 8; col++) {
                        if (bits & (0x80 >> col)) {
                            put_pixel(x + col, y + row, color);
                        }
                    }
                }
            }
        }
        x += 8;
        text++;
    }
}

void desktop_draw_cursor(int32_t x, int32_t y) {
    /*
     * NeolyxOS Arrow Cursor - High Contrast Design
     * WHITE fill + BLACK outline = visible on ANY background
     */
    static const uint32_t CURSOR_FILL    = 0xFFFFFFFF;  /* WHITE fill */
    static const uint32_t CURSOR_OUTLINE = 0xFF000000;  /* BLACK outline */
    
    /* High-contrast arrow cursor bitmap (21 rows, 17 cols) */
    /* 0=transparent, 1=white shadow, 2=black outline, 3=purple fill */
    static const uint8_t cursor_data[21][17] = {
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},  /* Row 0 */
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},  /* Row 1 */
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},  /* Row 2 */
        {1,2,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0},  /* Row 3 */
        {1,2,3,3,2,1,0,0,0,0,0,0,0,0,0,0,0},  /* Row 4 */
        {1,2,3,3,3,2,1,0,0,0,0,0,0,0,0,0,0},  /* Row 5 */
        {1,2,3,3,3,3,2,1,0,0,0,0,0,0,0,0,0},  /* Row 6 */
        {1,2,3,3,3,3,3,2,1,0,0,0,0,0,0,0,0},  /* Row 7 */
        {1,2,3,3,3,3,3,3,2,1,0,0,0,0,0,0,0},  /* Row 8 */
        {1,2,3,3,3,3,3,3,3,2,1,0,0,0,0,0,0},  /* Row 9 */
        {1,2,3,3,3,3,3,3,3,3,2,1,0,0,0,0,0},  /* Row 10 */
        {1,2,3,3,3,3,3,3,3,3,3,2,1,0,0,0,0},  /* Row 11 */
        {1,2,3,3,3,3,3,2,2,2,2,2,1,0,0,0,0},  /* Row 12 */
        {1,2,3,3,3,2,3,3,2,1,0,0,0,0,0,0,0},  /* Row 13 */
        {1,2,3,3,2,1,2,3,3,2,1,0,0,0,0,0,0},  /* Row 14 */
        {1,2,3,2,1,0,1,2,3,3,2,1,0,0,0,0,0},  /* Row 15 */
        {1,2,2,1,0,0,0,1,2,3,3,2,1,0,0,0,0},  /* Row 16 */
        {1,2,1,0,0,0,0,1,2,3,3,2,1,0,0,0,0},  /* Row 17 */
        {1,1,0,0,0,0,0,0,1,2,3,2,1,0,0,0,0},  /* Row 18 */
        {0,0,0,0,0,0,0,0,1,2,2,1,0,0,0,0,0},  /* Row 19 */
        {0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},  /* Row 20 */
    };
    
    /* Draw the cursor using bitmap data */
    for (int row = 0; row < 21; row++) {
        int py = y + row;
        if (py < 0 || py >= (int32_t)g_desktop.height) continue;
        
        for (int col = 0; col < 17; col++) {
            int px = x + col;
            if (px < 0 || px >= (int32_t)g_desktop.width) continue;
            
            uint8_t pixel = cursor_data[row][col];
            uint32_t color = 0;
            switch (pixel) {
                case 1:  /* outer edge */
                case 2: color = CURSOR_OUTLINE; break;  /* BLACK */
                case 3: color = CURSOR_FILL;    break;  /* WHITE */
                default: continue;
            }
            put_pixel(px, py, color);
        }
    }
}

/* ============ Advanced Drawing Primitives ============ */

/* Alpha blending: blend foreground over existing pixel */
static inline void put_pixel_alpha(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= (int32_t)g_desktop.width || 
        y < 0 || y >= (int32_t)g_desktop.height) return;
    
    uint8_t a = (color >> 24) & 0xFF;
    if (a == 0xFF) {
        g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x] = color;
        return;
    }
    if (a == 0x00) return;
    
    uint32_t bg = g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x];
    uint8_t br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint8_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fb = color & 0xFF;
    
    uint8_t r = (fr * a + br * (255 - a)) / 255;
    uint8_t g = (fg * a + bg_ * (255 - a)) / 255;
    uint8_t b = (fb * a + bb * (255 - a)) / 255;
    
    g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* Fill rectangle with alpha blending */
static void fill_rect_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            put_pixel_alpha(x + i, y + j, color);
        }
    }
}

/* Filled circle */
static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t y = -r; y <= r; y++) {
        for (int32_t x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                put_pixel_alpha(cx + x, cy + y, color);
            }
        }
    }
}

/* Circle outline */
static void draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    int32_t x = 0, y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        put_pixel_alpha(cx + x, cy + y, color);
        put_pixel_alpha(cx - x, cy + y, color);
        put_pixel_alpha(cx + x, cy - y, color);
        put_pixel_alpha(cx - x, cy - y, color);
        put_pixel_alpha(cx + y, cy + x, color);
        put_pixel_alpha(cx - y, cy + x, color);
        put_pixel_alpha(cx + y, cy - x, color);
        put_pixel_alpha(cx - y, cy - x, color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

/* Rounded rectangle with alpha */
static void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color) {
    if (r > (int32_t)(h / 2)) r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    
    /* Main body */
    fill_rect_alpha(x + r, y, w - 2*r, h, color);
    fill_rect_alpha(x, y + r, r, h - 2*r, color);
    fill_rect_alpha(x + w - r, y + r, r, h - 2*r, color);
    
    /* Corners */
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t dist_sq = (r - dx - 1) * (r - dx - 1) + (r - dy - 1) * (r - dy - 1);
            if (dist_sq <= r * r) {
                put_pixel_alpha(x + dx, y + dy, color);                    /* Top-left */
                put_pixel_alpha(x + w - 1 - dx, y + dy, color);            /* Top-right */
                put_pixel_alpha(x + dx, y + h - 1 - dy, color);            /* Bot-left */
                put_pixel_alpha(x + w - 1 - dx, y + h - 1 - dy, color);    /* Bot-right */
            }
        }
    }
}

/* Pill shape (fully rounded ends) */
static void fill_pill(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    int32_t r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    fill_rounded_rect(x, y, w, h, r, color);
}

/* Pill border */
static void draw_pill_border(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    int32_t r = h / 2;
    /* Top and bottom edges */
    for (uint32_t i = r; i < w - r; i++) {
        put_pixel_alpha(x + i, y, color);
        put_pixel_alpha(x + i, y + h - 1, color);
    }
    /* Left and right semicircle outlines */
    draw_circle(x + r, y + r, r, color);
    draw_circle(x + w - r - 1, y + r, r, color);
}

/* Large text (2x scale) */
static void draw_text_large(int32_t x, int32_t y, const char *text, uint32_t color) {
    while (*text) {
        uint8_t ch = *text;
        if (ch >= 32 && ch < 128) {
            int idx = ch - 32;
            if (idx < 96) {
                for (int row = 0; row < 8; row++) {
                    uint8_t bits = font_8x8[idx][row];
                    for (int col = 0; col < 8; col++) {
                        if (bits & (0x80 >> col)) {
                            /* 2x2 pixel blocks */
                            put_pixel(x + col*2, y + row*2, color);
                            put_pixel(x + col*2 + 1, y + row*2, color);
                            put_pixel(x + col*2, y + row*2 + 1, color);
                            put_pixel(x + col*2 + 1, y + row*2 + 1, color);
                        }
                    }
                }
            }
        }
        x += 16;
        text++;
    }
}

/* ============ Desktop Rendering ============ */

static void render_background(void) {
    if (!g_desktop.framebuffer) {
        return;
    }
    
    /* Try to render wallpaper image first */
    if (wallpaper_is_loaded()) {
        wallpaper_render(g_desktop.framebuffer, g_desktop.pitch, 
                        g_desktop.width, g_desktop.height);
    } else {
        /* Fallback: Dark navy to blue-gray gradient wallpaper */
        for (uint32_t y = 0; y < g_desktop.height; y++) {
            uint32_t t = (y * 256) / g_desktop.height;
            
            /* Top: Deep navy #070B14, Bottom: Dark blue-gray #142038 */
            uint32_t r = (0x07 * (256 - t) + 0x14 * t) >> 8;
            uint32_t g = (0x0B * (256 - t) + 0x20 * t) >> 8;
            uint32_t b = (0x14 * (256 - t) + 0x38 * t) >> 8;
            
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            
            for (uint32_t x = 0; x < g_desktop.width; x++) {
                g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x] = color;
            }
        }
    }
}

/* Navigation Menu Bar - top of screen */
/* Format "HH:MM" into out (6 bytes). */
static void fmt_hhmm(char *out, uint32_t h, uint32_t m) {
    out[0] = (char)('0' + (h / 10) % 10); out[1] = (char)('0' + h % 10); out[2] = ':';
    out[3] = (char)('0' + (m / 10) % 10); out[4] = (char)('0' + m % 10); out[5] = '\0';
}

/* Append src to dst at pos (bounded), returning the new pos. */
static int str_app(char *dst, int pos, int cap, const char *src) {
    while (src && *src && pos + 1 < cap) dst[pos++] = *src++;
    dst[pos] = '\0';
    return pos;
}

/* Once per frame tick, before anything reads the time. */
static void clock_tick(void) {
    rtc_time_t t;
    g_clock_snapshot_ms = nx_gettime();
    (void)get_cached_rtc_time(&t, 1);
}

/* Current wall-clock time: RTC when available, else boot ticks. Reads the
 * values clock_tick took this tick. */
static void current_time(uint32_t *hour, uint32_t *minute, uint32_t *second,
                         uint32_t *day, uint32_t *month, uint32_t *year, uint32_t *weekday) {
    rtc_time_t now;
    if (get_cached_rtc_time(&now, 0) == 0) {
        *hour = now.hour; *minute = now.minute; *second = now.second;
        *day = now.day; *month = now.month ? now.month : 1; *year = now.year;
        *weekday = calendar_day_of_week(*year, *month, *day);
        return;
    }
    uint64_t secs = (g_clock_snapshot_ms ? g_clock_snapshot_ms : nx_gettime()) / 1000;
    *hour = (uint32_t)((secs / 3600) % 24); *minute = (uint32_t)((secs / 60) % 60);
    *second = (uint32_t)(secs % 60);
    *day = 1; *month = 1; *year = 2026; *weekday = 4;
}

/* Status item slots in the menu bar, measured from the right edge. The
 * click zones in desktop_handle_mouse use the same centers. */
#define NAV_CC_CX      35.0f
#define NAV_BELL_CX    67.0f
#define NAV_ITEM_STEP  30.0f

static void render_navigation_bar(void) {
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    /* Clock: "Sun 25 Oct   22:10"; the menu bar module draws everything else */
    uint32_t hh, mm, ss, dd, mo, yy, wd;
    current_time(&hh, &mm, &ss, &dd, &mo, &yy, &wd);
    char line[48], hm[8];
    int pos = 0;
    pos = str_app(line, pos, sizeof(line), day_names_short[wd % 7]);
    pos = str_app(line, pos, sizeof(line), " ");
    char dbuf[4] = { (char)('0' + (dd / 10) % 10), (char)('0' + dd % 10), '\0', '\0' };
    pos = str_app(line, pos, sizeof(line), dd >= 10 ? dbuf : dbuf + 1);
    pos = str_app(line, pos, sizeof(line), " ");
    const char *mn = month_names_full[(mo - 1) % 12];
    char m3[4] = { mn[0], mn[1] ? mn[1] : ' ', mn[1] && mn[2] ? mn[2] : ' ', '\0' };
    pos = str_app(line, pos, sizeof(line), m3);
    pos = str_app(line, pos, sizeof(line), "   ");
    fmt_hhmm(hm, hh, mm);
    pos = str_app(line, pos, sizeof(line), hm);
    (void)pos;
    menubar_render_bar(c, (float)g_desktop.width, th->menubar_height, line);
}

/* Clock widget - top left: analog face beside the date and digital time. */
static void render_globe_clock_widget(void) {
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t box = nxc_rect(24.0f, 44.0f, 240.0f, 112.0f);
    nxui_panel(c, box, th->radius_widget, &th->mat_widget, 2);

    uint32_t hh, mm, ss, dd, mo, yy, wd;
    current_time(&hh, &mm, &ss, &dd, &mo, &yy, &wd);

    /* Analog face */
    float cx = box.x + 56.0f, cy = box.y + 56.0f, r = 38.0f;
    int dark = nxui_appearance() == NXUI_APPEARANCE_DARK;
    nxc_fill_circle(c, cx, cy + 1.5f, r, NXC_ARGB(70, 0, 0, 0));
    nxc_paint_t face = nxc_paint_linear(cx, cy - r, cx, cy + r,
                                        dark ? NXC_RGB(0x2A, 0x30, 0x40) : NXC_RGB(0xFF, 0xFF, 0xFF),
                                        dark ? NXC_RGB(0x14, 0x18, 0x24) : NXC_RGB(0xEC, 0xEE, 0xF3));
    nxc_fill_circle_paint(c, cx, cy, r, &face);
    nxc_stroke_circle(c, cx, cy, r - 0.5f, 1.0f, th->highlight);
    nxc_color_t tick = dark ? NXC_ARGB(200, 0xF0, 0xF4, 0xFF) : NXC_ARGB(220, 0x10, 0x14, 0x1E);
    for (int i = 0; i < 12; i++) {
        float a = (float)i * (NXC_PI / 6.0f);
        float ca = nxc_cosf_shell(a), sa = nxc_sinf_shell(a);
        float len = (i % 3 == 0) ? 5.0f : 2.5f;
        nxc_draw_line(c, cx + ca * (r - 4.0f), cy + sa * (r - 4.0f), cx + ca * (r - 4.0f - len),
                      cy + sa * (r - 4.0f - len), (i % 3 == 0) ? 2.0f : 1.2f, nxc_color_fade(tick, i % 3 ? 0.55f : 1.0f));
    }
    float ah = ((float)(hh % 12) + (float)mm / 60.0f) * (NXC_PI / 6.0f) - NXC_PI * 0.5f;
    float am = ((float)mm + (float)ss / 60.0f) * (NXC_PI / 30.0f) - NXC_PI * 0.5f;
    float as = (float)ss * (NXC_PI / 30.0f) - NXC_PI * 0.5f;
    nxc_draw_line(c, cx, cy, cx + nxc_cosf_shell(ah) * (r * 0.5f), cy + nxc_sinf_shell(ah) * (r * 0.5f), 3.2f, tick);
    nxc_draw_line(c, cx, cy, cx + nxc_cosf_shell(am) * (r * 0.74f), cy + nxc_sinf_shell(am) * (r * 0.74f), 2.4f, tick);
    nxc_draw_line(c, cx - nxc_cosf_shell(as) * 6.0f, cy - nxc_sinf_shell(as) * 6.0f,
                  cx + nxc_cosf_shell(as) * (r * 0.8f), cy + nxc_sinf_shell(as) * (r * 0.8f), 1.2f, th->accent);
    nxc_fill_circle(c, cx, cy, 2.6f, th->accent);
    nxc_fill_circle(c, cx, cy, 1.0f, dark ? NXC_RGB(0x14, 0x18, 0x24) : NXC_RGB(0xFF, 0xFF, 0xFF));

    /* Date and digital time */
    char date[48];
    int pos = 0;
    pos = str_app(date, pos, sizeof(date), day_names_full[wd % 7]);
    pos = str_app(date, pos, sizeof(date), ", ");
    char dbuf[4] = { (char)('0' + (dd / 10) % 10), (char)('0' + dd % 10), '\0', '\0' };
    pos = str_app(date, pos, sizeof(date), dd >= 10 ? dbuf : dbuf + 1);
    pos = str_app(date, pos, sizeof(date), " ");
    pos = str_app(date, pos, sizeof(date), month_names_full[(mo - 1) % 12]);
    (void)pos;
    float tx = box.x + 108.0f;
    nxc_text_opts_t o = { 0.0f, NXC_ALIGN_LEFT, box.x + box.w - tx - 12.0f, 0 };
    nxui_text(c, NXUI_TEXT_FOOTNOTE, tx, box.y + 34.0f, date, th->text_secondary, &o);
    char hm[8];
    fmt_hhmm(hm, hh, mm);
    nxc_text_opts_t big = { -1.0f, NXC_ALIGN_LEFT, 0.0f, 0 };
    nxc_draw_text(c, nxui_font(NXUI_FONT_DISPLAY), 40.0f, tx - 1.0f, box.y + 76.0f, hm, -1, th->text_primary, &big);
    /* Seconds progress: a thin accent bar under the time */
    float bw = box.w - (tx - box.x) - 16.0f;
    nxc_fill_rrect(c, nxc_rect(tx, box.y + 90.0f, bw, 3.0f), 1.5f, th->control_fill);
    nxc_fill_rrect(c, nxc_rect(tx, box.y + 90.0f, bw * ((float)ss + 0.5f) / 60.0f, 3.0f), 1.5f, th->accent);
}

/* Anveshan search field - top center (drawn by anveshan.c) */
static void render_search_bar(void) {
    anveshan_render(nxui_canvas());
}

/* External storage connected flag - set by kernel when USB/storage detected */
static int g_external_storage_connected = 0;

/* Desktop icons: only connected devices appear. */
static void render_desktop_icons(void) {
    desktop_items_render(nxui_canvas(), (int32_t)g_desktop.width, (int32_t)g_desktop.height,
                         g_external_storage_connected);
    if (!g_external_storage_connected) return;
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    float sz = 56.0f;
    nxc_rect_t tile = nxc_rect((float)g_desktop.width - 100.0f, 170.0f, sz, sz);
    /* NeolyxOS device art from the icon pack; a plain symbol tile only if
     * the pack entry is missing or damaged. */
    if (nxicon_pack_draw(c, "device-external", tile.x, tile.y, sz, 255) != NXC_OK) {
        nxui_elevation(c, tile, 12.0f, 1);
        nxui_material(c, tile, 12.0f, &th->mat_widget);
        nxsym_draw(c, NXSYM_FOLDER, tile.x + sz * 0.5f, tile.y + sz * 0.5f, 30.0f, th->text_primary, 0.0f);
    }
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(tile.x - 20.0f, tile.y + sz + 4.0f, sz + 40.0f, 16.0f),
                      "External", th->text_primary, NXC_ALIGN_CENTER);
}

/* Legacy dock rendering - REPLACED by modular dock.c */
/* Kept for reference, not called */
static void render_dock_legacy(void) {
    if (!g_desktop.dock_visible) return;
    
    /* Get appearance settings */
    appearance_settings_t *app = nxappearance_get();
    
    /* ============ Dock Animation State ============ */
    static int hover_idx = -1;         /* Currently hovered icon */
    static int minimize_anim_idx = -1; /* Icon being minimized to */
    static int minimize_progress = 0;  /* 0-100 animation progress */
    
    int icon_count = DOCK_ICON_COLOR_COUNT + 2;  /* 8 apps + 2 empty slots */
    int base_size = app->dock_icon_size;         /* From preferences */
    int dock_w = (base_size + 8) * icon_count + 40;
    int dock_h = base_size + 18;                 /* Dynamic height based on icon size */
    int dock_x = (g_desktop.width - dock_w) / 2;
    int dock_margin = 15;  /* TODO: Read from g_nx_config when dock config added */
    int dock_y = g_desktop.height - dock_h - dock_margin;
    
    /* Detect hover */
    hover_idx = -1;
    if (g_desktop.mouse_y >= dock_y && g_desktop.mouse_y <= dock_y + dock_h) {
        int mx = g_desktop.mouse_x - dock_x - 20;
        if (mx >= 0 && mx < icon_count * (base_size + 8)) {
            hover_idx = mx / (base_size + 8);
        }
    }
    
    /* Frosted glass pill background - use glass opacity from preferences */
    uint32_t dock_glass = GLASS_COLOR(0x303050, app);
    fill_pill(dock_x, dock_y, dock_w, dock_h, dock_glass);
    draw_pill_border(dock_x, dock_y, dock_w, dock_h, GLASS_BORDER);
    
    /* App icons - Colors from nxappearance.h dock_icon_colors array */
    struct { uint32_t color; int active; } apps[12];
    for (int i = 0; i < DOCK_ICON_COLOR_COUNT && i < 10; i++) {
        apps[i].color = dock_icon_colors[i];
        apps[i].active = (i == 0) ? 1 : 0;  /* First app is active */
    }
    /* Empty slots */
    for (int i = DOCK_ICON_COLOR_COUNT; i < icon_count; i++) {
        apps[i].color = 0x40505070;
        apps[i].active = 0;
    }
    
    int ix = dock_x + 20;
    int iy = dock_y + (dock_h - base_size) / 2;
    
    for (int i = 0; i < icon_count; i++) {
        /* Calculate magnification based on hover distance */
        int size = base_size;
        int y_offset = 0;
        
        if (hover_idx >= 0) {
            int dist = i - hover_idx;
            if (dist < 0) dist = -dist;
            
            if (dist == 0) {
                size = base_size + 12;  /* Hovered icon: larger */
                y_offset = -8;          /* Float up */
            } else if (dist == 1) {
                size = base_size + 6;   /* Adjacent icons: slightly larger */
                y_offset = -4;
            } else if (dist == 2) {
                size = base_size + 2;
                y_offset = -1;
            }
        }
        
        /* Minimize animation (bounce effect) */
        if (i == minimize_anim_idx && minimize_progress > 0) {
            int bounce = (minimize_progress < 50) ? minimize_progress : (100 - minimize_progress);
            y_offset = -bounce / 5;
            minimize_progress += 5;
            if (minimize_progress >= 100) {
                minimize_anim_idx = -1;
                minimize_progress = 0;
            }
        }
        
        int cx = ix + size/2;
        int cy = iy + y_offset + size/2;
        
        /* Round icon background */
        fill_circle(cx, cy, size/2 - 2, apps[i].color);
        
        /* Highlight on hover */
        if (i == hover_idx) {
            draw_circle(cx, cy, size/2 - 1, 0x60FFFFFF);
        }
        
        /* Active indicator dot (white dot below icon) */
        if (apps[i].active) {
            fill_circle(cx, dock_y + dock_h - 5, 2, COLOR_TEXT_WHITE);
        }
        
        ix += base_size + 8;
    }
}

/* Forward declarations */
static inline void serial_puts(const char *s);
static inline void serial_putc(char c);
static void serial_hex64(uint64_t val);

/* Title bar height shared with the hit tests in desktop_handle_mouse. */
#define WIN_TITLE_H WM_TITLEBAR_H
#define WIN_SHADOW_MARGIN   116     /* deepest window shadow reach (nxui level 4) */

/* ============ Windows: z-order, animation and drawing ============ */
/*
 * Z-order is an ordered list of window ids (bottom -> top). Every window's
 * z_order field mirrors its index in that list, so values are unique and
 * comparisons in hit tests stay correct however many windows come and go.
 *
 * Window motion uses NXUI springs:
 *   - frame springs move/resize the live window (tiling, zoom, Stage);
 *   - visual phases fade a new window in, and scale a closing, minimizing
 *     or restoring window from its cached snapshot (no full re-render).
 */

static desktop_window_t *window_by_id(uint32_t id);   /* input section */
static uint32_t g_mouse_capture_id;                    /* input section (tentative definition) */

static uint32_t g_zorder[MAX_WINDOWS];   /* bottom -> top */
static int g_zcount = 0;

typedef enum { WA_NONE = 0, WA_OPEN, WA_CLOSE, WA_MINIMIZE, WA_RESTORE } win_phase_t;

typedef struct {
    /* Frame spring (live geometry) */
    int geo;
    float fx0, fy0, fw0, fh0, fx1, fy1, fw1, fh1;
    nxui_spring_t gs;
    /* Visual phase */
    int phase;
    nxui_spring_t vs;
    nxc_rect_t dock;        /* minimize target */
} win_anim_t;

static win_anim_t g_wanim[MAX_WINDOWS];

/* Pointer gesture on window chrome: move by title bar, resize by edges. */
#define EDGE_L 0x1u
#define EDGE_R 0x2u
#define EDGE_T 0x4u
#define EDGE_B 0x8u
typedef enum { GES_NONE = 0, GES_MOVE, GES_RESIZE } ges_kind_t;
static struct {
    int kind;
    uint32_t id;
    uint32_t edges;
    int32_t mx0, my0;
    int32_t x0, y0;
    uint32_t w0, h0;
    int moved;
    wm_tile_pos_t tile;     /* snap target under the pointer while moving */
} g_ges;
static uint32_t g_resize_hover = 0;          /* edges under the pointer (cursor hint) */

/* Traffic light being pressed (window id, index 0..2), for the pressed tint. */
static uint32_t g_light_press_id = 0;
static int g_light_press_idx = -1;

/* Set while a window is drawn for the snapshot cache (no hover, no shadow). */
static int g_snapshot_pass = 0;

/* Tile preview rect, sprung toward the suggested tile. */
static nxc_rect_t g_tile_prev_rect;
static float g_tile_prev_alpha = 0.0f;

static win_anim_t *anim_of(const desktop_window_t *w) {
    if (!w) return NULL;
    long idx = (long)(w - g_windows);
    if (idx < 0 || idx >= MAX_WINDOWS) return NULL;
    return &g_wanim[idx];
}

static void z_reindex(void) {
    for (int i = 0; i < g_zcount; i++) {
        desktop_window_t *w = window_by_id(g_zorder[i]);
        if (w) w->z_order = (uint8_t)i;
    }
}

static void z_remove(uint32_t id) {
    int o = 0;
    for (int i = 0; i < g_zcount; i++) {
        if (g_zorder[i] != id) g_zorder[o++] = g_zorder[i];
    }
    g_zcount = o;
}

static void z_push_top(uint32_t id) {
    if (id == 0) return;
    z_remove(id);
    if (g_zcount < MAX_WINDOWS) g_zorder[g_zcount++] = id;
    z_reindex();
}

/* Frame rect of a window (title bar included). */
static nxc_rect_t window_frame_rect(const desktop_window_t *w) {
    return nxc_rect((float)w->x, (float)w->y, (float)w->width, (float)w->height + (float)WIN_TITLE_H);
}

/* Title bar strip of a window (window controls sit at its right end). */
static nxc_rect_t window_titlebar_rect(const desktop_window_t *w) {
    return nxc_rect((float)w->x, (float)w->y, (float)w->width, (float)WIN_TITLE_H);
}

/* Which window control (0 close, 1 minimize, 2 zoom) contains (x, y), or -1. */
static int traffic_light_at(const desktop_window_t *w, int32_t x, int32_t y) {
    if (!w || w->width <= 60) return -1;
    return nxglass_winctl_at(window_titlebar_rect(w), x, y);
}

/* ============ Pass culling ============ */
/*
 * Each frame pass draws every component clipped to one damaged rectangle.
 * Clipping keeps the pixels right but not the work: a window's content
 * callback still lays out and draws everything. Before a component is
 * drawn, the pass checks whether any of it can show:
 *   - its bounds (frame plus deepest shadow) miss the pass rectangle, or
 *   - the part inside the pass rectangle lies within the opaque interior
 *     of a window drawn later (above it): it would be painted over.
 * A window is opaque when it is drawn normally (no open/close/minimize
 * animation, full opacity, not PiP or a sidebar tab) and the theme's window
 * background is opaque: draw_window_body fills its rounded frame with that
 * background before anything else. Its interior is the frame inset past the
 * rounded corners. The debug self-check (DESKTOP_DAMAGE_VERIFY) draws its
 * reference frame with culling off, so a wrong skip shows as a mismatch.
 */
static int g_cull_off = 0;                  /* draw everything (verify reference) */

static int irect_empty(nxc_irect_t r) { return r.w <= 0 || r.h <= 0; }

static nxc_irect_t irect_and(nxc_irect_t a, nxc_irect_t b) {
    int64_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int64_t x1 = (int64_t)a.x + a.w, y1 = (int64_t)a.y + a.h;
    int64_t bx1 = (int64_t)b.x + b.w, by1 = (int64_t)b.y + b.h;
    if (bx1 < x1) x1 = bx1;
    if (by1 < y1) y1 = by1;
    if (x1 <= x0 || y1 <= y0) return nxc_irect(0, 0, 0, 0);
    return nxc_irect((int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0));
}

static int irect_inside(nxc_irect_t in, nxc_irect_t out) {
    return !irect_empty(out) && in.x >= out.x && in.y >= out.y &&
           (int64_t)in.x + in.w <= (int64_t)out.x + out.w && (int64_t)in.y + in.h <= (int64_t)out.y + out.h;
}

/* The pass rectangle: the canvas clip set by nxui_pass_begin. */
static nxc_irect_t pass_rect(void) {
    nxc_canvas_t *c = nxui_canvas();
    return c ? c->st.clip : nxc_irect(0, 0, 0, 0);
}

/* Corner radius draw_window_body uses for w. */
static float window_radius(const desktop_window_t *w) {
    wm_window_state_t *ws = wm_get_window_state(w->id);
    int flush = ws && (ws->is_tiled || ws->is_maximized);
    return flush ? 8.0f : nxui_theme()->radius_window;
}

/* Interior of the rounded frame: inset past the corners' curve (a circle
 * of radius r stays inside its square inset by r(1 - 1/sqrt 2) < 0.3r),
 * plus a pixel for the anti-aliased edge. */
static nxc_irect_t window_interior(const desktop_window_t *w) {
    int32_t in = (int32_t)(window_radius(w) * 0.3f) + 2;
    int64_t ww = (int64_t)w->width - 2 * in, hh = (int64_t)w->height + WIN_TITLE_H - 2 * in;
    if (ww <= 0 || hh <= 0) return nxc_irect(0, 0, 0, 0);
    return nxc_irect(w->x + in, w->y + in, (int32_t)ww, (int32_t)hh);
}

/* 1 if w is drawn as an opaque rounded frame this frame (see above). */
static int window_opaque(const desktop_window_t *w) {
    if (!w || w->id == 0 || !w->visible || w->minimized) return 0;
    if (w->width < 8 || w->height < 2) return 0;
    win_anim_t *a = anim_of(w);
    if (a && a->phase != WA_NONE) return 0;
    if (wm_is_pip(w->id)) return 0;
    wm_window_state_t *ws = wm_get_window_state(w->id);
    if (ws && ws->opacity < 1.0f) return 0;
    if (ws && ws->mode == WIN_MODE_SIDEBAR && !ws->sidebar_expanded) return 0;
    return (nxui_theme()->window_bg >> 24) == 0xFFu;
}

/* Screen area window w can touch this frame, or the whole screen when its
 * animation moves it outside its frame (close, minimize, restore). */
static nxc_irect_t window_reach(const desktop_window_t *w) {
    win_anim_t *a = anim_of(w);
    int phase = a ? a->phase : WA_NONE;
    if (phase != WA_NONE && phase != WA_OPEN) {
        return nxc_irect(0, 0, (int32_t)g_desktop.width, (int32_t)g_desktop.height);
    }
    int64_t open_slide = (phase == WA_OPEN) ? 16 : 0;      /* opening slides up 14 px */
    int64_t x = (int64_t)w->x - WIN_SHADOW_MARGIN, y = (int64_t)w->y - WIN_SHADOW_MARGIN;
    int64_t ww = (int64_t)w->width + 2 * WIN_SHADOW_MARGIN;
    int64_t hh = (int64_t)w->height + WIN_TITLE_H + 2 * WIN_SHADOW_MARGIN + open_slide;
    if (x < -0x40000000LL || y < -0x40000000LL || ww > 0x40000000LL || hh > 0x40000000LL) {
        return nxc_irect(0, 0, (int32_t)g_desktop.width, (int32_t)g_desktop.height);
    }
    return nxc_irect((int32_t)x, (int32_t)y, (int32_t)ww, (int32_t)hh);
}

/* 1 if area (already inside the pass) is painted over by an opaque normal
 * window above z-order position zi (zi = -1: above everything drawn before
 * the windows). */
static int covered_above(nxc_irect_t area, int zi) {
    if (irect_empty(area)) return 1;
    for (int k = g_zcount - 1; k > zi; k--) {
        desktop_window_t *o = window_by_id(g_zorder[k]);
        if (!o || !window_opaque(o)) continue;
        if (irect_inside(area, window_interior(o))) return 1;
    }
    return 0;
}

/* Should the component with bounds b, drawn before the windows, be skipped
 * in this pass? */
static int pass_skips_below_windows(nxc_irect_t b) {
    if (g_cull_off || overview_active()) return 0;
    nxc_irect_t vis = irect_and(b, pass_rect());
    if (irect_empty(vis)) return 1;
    return covered_above(vis, -1);
}

/* Should the normal window at z-order position zi be skipped in this pass? */
static int pass_skips_window(const desktop_window_t *w, int zi) {
    if (g_cull_off) return 0;
    nxc_irect_t vis = irect_and(window_reach(w), pass_rect());
    if (irect_empty(vis)) return 1;
    return covered_above(vis, zi);
}

/*
 * A window's drop shadow. The shadow is drawn under the whole frame, but an
 * opaque window body paints over its interior right away, so when the body
 * is opaque (theme background opaque, no fade) only the four bands around
 * the interior are drawn: each band is a clip, the shadow itself is the
 * same cached mask, and every pixel that stays visible gets the same value.
 */
static void window_shadow(nxc_canvas_t *c, const desktop_window_t *win, nxc_rect_t frame,
                          float radius, int32_t level) {
    nxc_irect_t in = window_interior(win);
    if (g_cull_off || c->st.alpha != 255u || (nxui_theme()->window_bg >> 24) != 0xFFu ||
        irect_empty(in) || frame.x != (float)win->x || frame.y != (float)win->y) {
        nxui_elevation(c, frame, radius, level);
        return;
    }
    nxc_irect_t reach = window_reach(win);
    int64_t rx1 = (int64_t)reach.x + reach.w, ry1 = (int64_t)reach.y + reach.h;
    int64_t ix1 = (int64_t)in.x + in.w, iy1 = (int64_t)in.y + in.h;
    nxc_irect_t band[4] = {
        nxc_irect(reach.x, reach.y, reach.w, in.y - reach.y),                          /* above */
        nxc_irect(reach.x, (int32_t)iy1, reach.w, (int32_t)(ry1 - iy1)),               /* below */
        nxc_irect(reach.x, in.y, in.x - reach.x, in.h),                                /* left */
        nxc_irect((int32_t)ix1, in.y, (int32_t)(rx1 - ix1), in.h),                     /* right */
    };
    nxc_irect_t pass = c->st.clip;
    for (int i = 0; i < 4; i++) {
        if (irect_empty(band[i]) || irect_empty(irect_and(band[i], pass))) continue;
        nxc_save(c);
        nxc_clip_rect(c, band[i]);
        nxui_elevation(c, frame, radius, level);
        nxc_restore(c);
    }
}

/*
 * Draw a window's chrome and content at its current position.
 * shadow: draw the drop shadow; hover_ok: traffic light hover glyphs.
 */
static void draw_window_body(desktop_window_t *win, int shadow, int hover_ok) {
    nxc_canvas_t *c = nxui_canvas();
    if (!c || !win) return;
    const nxui_theme_t *th = nxui_theme();
    wm_window_state_t *wstate = wm_get_window_state(win->id);
    int flush = wstate && (wstate->is_tiled || wstate->is_maximized);
    float radius = flush ? 8.0f : th->radius_window;
    nxc_rect_t frame = window_frame_rect(win);
    int focused = win->focused || g_snapshot_pass;

    if (shadow) window_shadow(c, win, frame, radius, focused ? 4 : 3);

    /* Body and title bar, clipped to the rounded frame */
    nxc_fill_rrect(c, frame, radius, th->window_bg);
    nxc_save(c);
    nxc_clip_rrect(c, frame, radius);
    nxc_fill_rect(c, nxc_rect(frame.x, frame.y, frame.w, WIN_TITLE_H),
                  focused ? th->window_titlebar : th->window_titlebar_inactive);
    nxc_fill_rect(c, nxc_rect(frame.x, frame.y + WIN_TITLE_H - 1.0f, frame.w, 1.0f), th->separator);
    nxc_fill_rect(c, nxc_rect(frame.x, frame.y, frame.w, 1.0f), th->highlight);   /* top sheen */
    if (focused && !g_snapshot_pass) nxframe_crest(c, frame);                     /* focus crest */
    nxc_restore(c);

    if (win->width > 60 && win->height > 6) {
        /* Window controls at the right end of the title bar */
        nxc_rect_t tb = window_titlebar_rect(win);
        int hover = -1;
        if (hover_ok && !g_snapshot_pass) hover = traffic_light_at(win, g_desktop.mouse_x, g_desktop.mouse_y);
        int pressed = g_light_press_id == win->id ? g_light_press_idx : -1;
        nxglass_winctl_draw(c, tb, focused || hover >= 0, pressed, hover);
        /* Neolyx frame: "App / Place" anchored left, controls on the right */
        nxframe_title(c, tb, win->title, win->place,
                      NXGLASS_WINCTL_GROUP_W + NXGLASS_WINCTL_MARGIN + 8.0f, focused);
    }

    /* Content callback: draws in absolute coordinates inside the content rect */
    if (win->render_content && win->width > 40 && win->height > 20) {
        nxc_save(c);
        nxc_clip_rrect(c, nxc_rect(frame.x, frame.y + WIN_TITLE_H, frame.w, frame.h - WIN_TITLE_H), radius);
        win->render_content(win, win->render_ctx);
        nxc_restore(c);
    }

    /* Hairline border keeps the edge crisp on any wallpaper */
    nxc_stroke_rrect(c, frame, radius, 1.0f, th->window_border);
}

/* Snapshot renderer registered with the window manager: draws the window
 * at (0, 0) into the canvas the snapshot service hands over. */
static int shell_snapshot_renderer(uint32_t id, nxc_canvas_t *c, int32_t *out_w, int32_t *out_h) {
    desktop_window_t *win = window_by_id(id);
    if (!win || !c || !out_w || !out_h || c != nxui_canvas()) return -1;
    if (win->width == 0 || win->height == 0) return -1;
    int32_t sx = win->x, sy = win->y;
    win->x = 0;
    win->y = 0;
    g_snapshot_pass = 1;
    nxc_fill_rect(c, nxc_rect(0.0f, 0.0f, (float)win->width, (float)win->height + WIN_TITLE_H), nxui_theme()->window_bg);
    draw_window_body(win, 0, 0);
    g_snapshot_pass = 0;
    win->x = sx;
    win->y = sy;
    *out_w = (int32_t)win->width;
    *out_h = (int32_t)win->height + WIN_TITLE_H;
    return 0;
}

void wm_shell_draw_window_image(nxc_canvas_t *c, uint32_t window_id, nxc_rect_t r, uint8_t alpha) {
    if (!c || r.w < 2.0f || r.h < 2.0f || alpha == 0) return;
    const nxc_surface_t *surf = NULL;
    nxc_irect_t src;
    if (wm_snapshot_get(window_id, &surf, &src)) {
        nxc_draw_surface(c, surf, src, r, alpha, NXC_FILTER_BILINEAR);
        return;
    }
    /* Placeholder card: chrome, app icon and title (snapshot not available) */
    const nxui_theme_t *th = nxui_theme();
    desktop_window_t *win = window_by_id(window_id);
    float sc = r.w / (win && win->width ? (float)win->width : r.w);
    float rad = th->radius_window * sc;
    if (rad < 3.0f) rad = 3.0f;
    nxc_save(c);
    nxc_set_alpha(c, alpha);
    nxc_fill_rrect(c, r, rad, th->window_bg);
    nxc_save(c);
    nxc_clip_rrect(c, r, rad);
    nxc_fill_rect(c, nxc_rect(r.x, r.y, r.w, (float)WIN_TITLE_H * sc), th->window_titlebar);
    nxc_restore(c);
    nxc_stroke_rrect(c, r, rad, 1.0f, th->window_border);
    if (win) {
        float isz = r.h * 0.3f;
        if (isz > 64.0f) isz = 64.0f;
        if (isz >= 12.0f) {
            nxapp_icon_draw(c, nxapp_icon_from_name(win->title), r.x + (r.w - isz) * 0.5f,
                            r.y + (r.h - isz) * 0.5f, (float)(int)isz, 255);
        }
    }
    nxc_restore(c);
}

/* Draw one window with its open/close/minimize/restore phase applied. */
static void render_window(desktop_window_t *win) {
    if (!win || win->id == 0) return;
    win_anim_t *a = anim_of(win);
    int phase = a ? a->phase : WA_NONE;
    int animating_out = (phase == WA_CLOSE || phase == WA_MINIMIZE || phase == WA_RESTORE);
    if (!animating_out && (!win->visible || win->minimized)) return;
    if (win->width < 8 || win->height < 2) return;
    nxc_canvas_t *c = nxui_canvas();
    if (!c) return;

    wm_window_state_t *wstate = wm_get_window_state(win->id);
    float opacity = 1.0f;
    if (wstate && wstate->opacity < 1.0f) opacity = wstate->opacity;   /* PiP */

    nxc_rect_t frame = window_frame_rect(win);
    if (animating_out && a) {
        nxc_rect_t r = frame;
        float alpha = opacity;
        float v = a->vs.value;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (phase == WA_CLOSE) {
            float s = 0.90f + 0.10f * v;
            r = nxc_rect(frame.x + frame.w * (1.0f - s) * 0.5f, frame.y + frame.h * (1.0f - s) * 0.5f,
                         frame.w * s, frame.h * s);
            alpha *= v;
        } else {
            /* Minimize (v: 0 -> 1) and restore (v: 1 -> 0) share the path */
            float p = nxui_ease_in_out_cubic(v);
            r = nxc_rect(frame.x + (a->dock.x - frame.x) * p, frame.y + (a->dock.y - frame.y) * p,
                         frame.w + (a->dock.w - frame.w) * p, frame.h + (a->dock.h - frame.h) * p);
            alpha *= 1.0f - 0.7f * p * p;
        }
        if (alpha < 0.02f || r.w < 2.0f) return;
        uint8_t a8 = (uint8_t)(alpha * 255.0f + 0.5f);
        if (wm_snapshot_get(win->id, NULL, NULL)) {
            nxc_save(c);
            nxc_set_alpha(c, a8);
            nxui_elevation(c, r, nxui_theme()->radius_window * (r.w / frame.w), 3);
            nxc_restore(c);
            wm_shell_draw_window_image(c, win->id, r, a8);
            return;
        }
        if (phase != WA_CLOSE) return;   /* no snapshot: minimize just vanishes */
        opacity = alpha;                   /* close without snapshot: plain fade */
    }

    float yoff = 0.0f;
    if (phase == WA_OPEN && a) {
        float v = a->vs.value;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        opacity *= v;
        yoff = (1.0f - v) * 14.0f;
    }
    if (opacity < 0.02f) return;

    int32_t saved_y = win->y;
    win->y += (int32_t)yoff;
    nxc_save(c);
    nxc_set_alpha(c, (uint8_t)(opacity * 255.0f + 0.5f));
    draw_window_body(win, 1, 1);
    nxc_restore(c);
    win->y = saved_y;
}

/* Collapsed sidebar window: a tab hugging the right screen edge. */
static void render_sidebar_tab(desktop_window_t *win, wm_window_state_t *wstate) {
    if (!wstate || wstate->mode != WIN_MODE_SIDEBAR || wstate->sidebar_expanded) return;
    if (!win->visible || win->minimized) return;
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t tab = nxc_rect((float)g_desktop.width - 40.0f, (float)win->y, 36.0f + 14.0f, 80.0f);
    nxui_elevation(c, tab, 12.0f, 2);
    nxui_material(c, tab, 12.0f, &th->mat_popover);
    nxsym_draw(c, NXSYM_CHEVRON_LEFT, tab.x + 10.0f, tab.y + 40.0f, 12.0f, th->text_secondary, 0.0f);
    char abbr[3] = { win->title[0] ? win->title[0] : '?', win->title[0] && win->title[1] ? win->title[1] : '\0', '\0' };
    nxui_text_in_rect(c, NXUI_TEXT_TITLE3, nxc_rect(tab.x + 14.0f, tab.y, 24.0f, 80.0f), abbr, th->text_primary,
                      NXC_ALIGN_CENTER);
}

/* Normal windows bottom to top, then PiP windows (always on top). */
static void render_windows(void) {
    for (int zi = 0; zi < g_zcount; zi++) {
        desktop_window_t *w = window_by_id(g_zorder[zi]);
        if (!w || wm_is_pip(w->id)) continue;
        wm_window_state_t *ws = wm_get_window_state(w->id);
        if (ws && ws->mode == WIN_MODE_SIDEBAR && !ws->sidebar_expanded) {
            render_sidebar_tab(w, ws);      /* drawn at the screen edge, not its frame */
            continue;
        }
        if (pass_skips_window(w, zi)) continue;
        render_window(w);
    }
    for (int zi = 0; zi < g_zcount; zi++) {
        desktop_window_t *w = window_by_id(g_zorder[zi]);
        if (!w || !wm_is_pip(w->id)) continue;
        /* PiP windows are drawn over every normal window: only the bounds test */
        if (!g_cull_off && irect_empty(irect_and(window_reach(w), pass_rect()))) continue;
        render_window(w);
    }
}

/* Visible = not hidden by space, Stage or a close in progress. */
static void windows_sync_visibility(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        desktop_window_t *w = &g_windows[i];
        if (w->id == 0) continue;
        w->visible = wm_window_hidden(w->id) == 0;
        if (!w->visible && w->focused) w->focused = 0;
    }
}

/* Focus the topmost shown window when nothing visible holds focus. */
static void focus_top_visible(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id && g_windows[i].visible && !g_windows[i].minimized && g_windows[i].focused) return;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) g_windows[i].focused = 0;
    for (int zi = g_zcount - 1; zi >= 0; zi--) {
        desktop_window_t *w = window_by_id(g_zorder[zi]);
        if (w && w->visible && !w->minimized) {
            win_anim_t *a = anim_of(w);
            if (a && a->phase == WA_CLOSE) continue;
            w->focused = 1;
            return;
        }
    }
}

/* Start a frame spring from the window's current frame to the target. */
static void window_animate_frame(desktop_window_t *w, int32_t x, int32_t y, uint32_t ww, uint32_t wh) {
    win_anim_t *a = anim_of(w);
    if (!a) return;
    if (ww < WM_MIN_W / 2) ww = WM_MIN_W / 2;
    if (wh < WM_MIN_H / 2) wh = WM_MIN_H / 2;
    a->fx0 = (float)w->x; a->fy0 = (float)w->y; a->fw0 = (float)w->width; a->fh0 = (float)w->height;
    a->fx1 = (float)x; a->fy1 = (float)y; a->fw1 = (float)ww; a->fh1 = (float)wh;
    a->gs.value = 0.0f;
    a->gs.velocity = 0.0f;
    a->geo = 1;
}

static void window_destroy_now(desktop_window_t *w);

/* Advance every window spring and finish completed phases. */
static void windows_tick(float dt) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        desktop_window_t *w = &g_windows[i];
        win_anim_t *a = &g_wanim[i];
        if (w->id == 0) continue;
        if (a->geo) {
            nxui_spring_step(&a->gs, 1.0f, 320.0f, 34.0f, dt);
            float t = a->gs.value;
            if (t > 0.998f && a->gs.velocity < 0.05f && a->gs.velocity > -0.05f) {
                t = 1.0f;
                a->geo = 0;
            }
            w->x = (int32_t)(a->fx0 + (a->fx1 - a->fx0) * t + 0.5f);
            w->y = (int32_t)(a->fy0 + (a->fy1 - a->fy0) * t + 0.5f);
            float fw = a->fw0 + (a->fw1 - a->fw0) * t, fh = a->fh0 + (a->fh1 - a->fh0) * t;
            w->width = fw > 1.0f ? (uint32_t)(fw + 0.5f) : 1u;
            w->height = fh > 1.0f ? (uint32_t)(fh + 0.5f) : 1u;
        }
        switch (a->phase) {
            case WA_OPEN:
                nxui_spring_step(&a->vs, 1.0f, 260.0f, 32.0f, dt);
                if (a->vs.value > 0.995f) a->phase = WA_NONE;
                break;
            case WA_CLOSE:
                nxui_spring_step(&a->vs, 0.0f, 300.0f, 35.0f, dt);
                if (a->vs.value < 0.02f) {
                    a->phase = WA_NONE;
                    window_destroy_now(w);
                }
                break;
            case WA_MINIMIZE:
                nxui_spring_step(&a->vs, 1.0f, 170.0f, 26.0f, dt);
                if (a->vs.value > 0.995f) a->phase = WA_NONE;
                break;
            case WA_RESTORE:
                nxui_spring_step(&a->vs, 0.0f, 200.0f, 28.5f, dt);
                if (a->vs.value < 0.005f) a->phase = WA_NONE;
                break;
            default:
                break;
        }
    }
}

/* Tile preview: springs toward the suggested tile, fades in and out. */
static void render_tile_preview(nxc_canvas_t *frame, float dt) {
    int32_t tx, ty;
    uint32_t tw, th_;
    int on = wm_get_tile_preview(&tx, &ty, &tw, &th_);
    if (on) {
        nxc_rect_t target = nxc_rect((float)tx, (float)ty, (float)tw, (float)th_);
        if (g_tile_prev_alpha < 0.01f) {
            /* Grow out of the pointer */
            g_tile_prev_rect = nxc_rect((float)g_desktop.mouse_x - 20.0f, (float)g_desktop.mouse_y - 20.0f, 40.0f, 40.0f);
        }
        g_tile_prev_rect.x = nxui_approach(g_tile_prev_rect.x, target.x, 0.05f, dt);
        g_tile_prev_rect.y = nxui_approach(g_tile_prev_rect.y, target.y, 0.05f, dt);
        g_tile_prev_rect.w = nxui_approach(g_tile_prev_rect.w, target.w, 0.05f, dt);
        g_tile_prev_rect.h = nxui_approach(g_tile_prev_rect.h, target.h, 0.05f, dt);
    }
    g_tile_prev_alpha = nxui_approach(g_tile_prev_alpha, on ? 1.0f : 0.0f, 0.06f, dt);
    if (g_tile_prev_alpha < 0.02f) return;
    const nxui_theme_t *th = nxui_theme();
    nxc_rect_t r = g_tile_prev_rect;
    nxc_save(frame);
    nxc_set_alpha(frame, (uint8_t)(g_tile_prev_alpha * 255.0f));
    nxui_material(frame, r, th->radius_window, &th->mat_hud);
    nxc_fill_rrect(frame, r, th->radius_window, NXC_WITH_ALPHA(th->accent, 46));
    nxc_stroke_rrect(frame, r, th->radius_window, 1.5f, NXC_WITH_ALPHA(th->accent, 190));
    nxc_restore(frame);
}

/* Resize cursor hint: the system cursor has no shapes yet, so a small
 * double-arrow badge rides next to the pointer over a resize edge. */
static void render_resize_hint(nxc_canvas_t *c) {
    uint32_t e = (g_ges.kind == GES_RESIZE) ? g_ges.edges : g_resize_hover;
    if (!e || g_ges.kind == GES_MOVE) return;
    const nxui_theme_t *th = nxui_theme();
    float cx = (float)g_desktop.mouse_x + 18.0f, cy = (float)g_desktop.mouse_y + 20.0f;
    float dx = 0.0f, dy = 0.0f;
    int h = (e & (EDGE_L | EDGE_R)) != 0, v = (e & (EDGE_T | EDGE_B)) != 0;
    if (h && v) {
        int main_diag = ((e & EDGE_L) && (e & EDGE_T)) || ((e & EDGE_R) && (e & EDGE_B));
        dx = 4.6f; dy = main_diag ? 4.6f : -4.6f;
    } else if (h) {
        dx = 6.5f;
    } else {
        dy = 6.5f;
    }
    nxc_fill_circle(c, cx, cy, 10.0f, NXC_WITH_ALPHA(th->window_bg, 230));
    nxc_stroke_circle(c, cx, cy, 10.0f, 1.0f, th->window_border);
    nxc_color_t ink = th->text_primary;
    nxc_draw_line(c, cx - dx, cy - dy, cx + dx, cy + dy, 1.6f, ink);
    /* Arrow heads: the shaft direction rotated +-45 degrees at each end
     * (dx, dy always have length 6.5, so dividing normalizes it). */
    const float len = 3.2f;
    for (int s = -1; s <= 1; s += 2) {
        float ex = cx + (float)s * dx, ey = cy + (float)s * dy;
        float bx = -(float)s * dx / 6.5f, by = -(float)s * dy / 6.5f;
        float r1x = (bx - by) * 0.7071f, r1y = (bx + by) * 0.7071f;
        float r2x = (bx + by) * 0.7071f, r2y = (by - bx) * 0.7071f;
        nxc_draw_line(c, ex, ey, ex + r1x * len, ey + r1y * len, 1.6f, ink);
        nxc_draw_line(c, ex, ey, ex + r2x * len, ey + r2y * len, 1.6f, ink);
    }
}

/* ============ Damage-driven frames ============ */
/*
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md, Decision 3
 * (approved 2026-11-01). Every 16 ms frame tick:
 *
 *  1. tick: window springs, Stage/Overview/switcher motion, visibility,
 *     snapshot captures (frame_tick);
 *  2. damage: what changed since the last drawn frame (frame_collect_damage)
 *     - shell activity (input, an open overlay, a running animation, a
 *       window opened/closed/refocused/retitled, a snapshot capture) damages
 *       the whole screen, and keeps doing so for SETTLE_MS after the last
 *       activity so delayed effects (tooltips, auto-hide) are drawn;
 *     - client commits damage their rectangles (desktop_damage_window_content);
 *     - a window that only moved or resized damages its old and new frame
 *       plus shadow; windows drawn by the shell's own views damage their
 *       frame when they ask (desktop_window_redraw_in: caret blink,
 *       timers) and at least once a second;
 *     - the cursor's old and new rectangles; the clock widget each second,
 *       the menu bar strip each second (clock, network, volume, battery);
 *  3. draw: nothing when the damage is empty; else one pass per damaged
 *     rectangle (at most 8, merged), each with the canvas clipped to it, or
 *     one full pass when more than half the screen is damaged (frame_draw);
 *  4. present: fb.flip_rects with the damaged rectangles, or fb.flip.
 *
 * Correctness first: when in doubt, the whole screen is redrawn. A debug
 * build with -DDESKTOP_DAMAGE_VERIFY redraws the whole frame into a second
 * buffer every tick and logs any pixel that differs from what was
 * presented (missed damage, or drawing that ignores the clip).
 */
#define FRAME_TICK_MS       16
#define SETTLE_MS           1000u   /* full frames after the last shell activity */
#define CLOCK_WIDGET_X      24
#define CLOCK_WIDGET_Y      44
#define CLOCK_WIDGET_W      240
#define CLOCK_WIDGET_H      112
#define CLOCK_SHADOW_REACH  80      /* panel elevation 2 reaches 3 sigma + offset = 64 px */
#define FRAME_LOG_TICKS     300u    /* summary line every ~5 s */
#define FRAME_LOG_MS        5000u   /* ... and at least every 5 s */
#define CURSOR_NOT_DRAWN    (-0x7FFFFFFF - 1)
#define BBOX_NONE           0x7FFFFFFF

static nxs_damage_t g_damage;
static int      g_damage_ready = 0;
static uint64_t g_settle_until = 0;         /* ms: full frames until then */
static int      g_motion_pending = 0;       /* the last drawn frame left an animation unsettled */
static int32_t  g_cursor_drawn_x = CURSOR_NOT_DRAWN, g_cursor_drawn_y = CURSOR_NOT_DRAWN;

/* What a window looked like in the last tick, to damage its changes. */
typedef struct {
    uint32_t id;
    int32_t  x, y;
    uint32_t w, h;
    uint8_t  visible, focused, minimized, z, phase, mode, sidebar_open, pip;
    uint8_t  opacity8;
    uint32_t text_hash;     /* title and place */
} win_sig_t;
static win_sig_t g_win_sig[MAX_WINDOWS];

static uint32_t g_seen_second = 0xFFFFFFFFu, g_seen_minute = 0xFFFFFFFFu;
static int      g_force_full = 0;           /* redraw everything next tick (night mode, verify repair) */
static uint32_t g_items_sig = 0;

/* Frame log (over FRAME_LOG_TICKS ticks) */
static struct {
    uint32_t ticks, full, partial, skipped, passes;
    uint64_t partial_px, partial_px_max;
    uint64_t draw_ms, present_ms;
    uint32_t verify_bad_frames;
    uint64_t verify_bad_px;
    uint64_t window_start;
} g_fs;

/* Per-stage profile of drawn frames in TSC cycles (k-cycles per frame). */
enum { PF_BG, PF_NAV, PF_CLOCK, PF_SEARCH, PF_WINDOWS, PF_DOCK, PF_OVERLAYS, PF_END, PF_INPUT, PF_SLEEP, PF_COUNT };
static uint64_t g_pf_loop_t = 0;   /* TSC at the end of the previous tick */
static uint64_t g_pf[PF_COUNT];
static inline uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#define PF_MARK(stage) do { uint64_t _t = rdtsc_now(); g_pf[stage] += _t - pf_t; pf_t = _t; } while (0)

/* The whole screen changed: draw it all now and for the settle period. */
static void shell_activity(void) {
    if (!g_damage_ready) return;
    nxs_damage_add_full(&g_damage);
    g_settle_until = pit_get_ticks() + SETTLE_MS;
}

static void damage_rect(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (g_damage_ready) nxs_damage_add(&g_damage, x, y, w, h);
}

/* Frame rect plus the deepest shadow around it. */
static void damage_window_frame(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    damage_rect((int64_t)x - WIN_SHADOW_MARGIN, (int64_t)y - WIN_SHADOW_MARGIN,
                (int64_t)w + 2 * WIN_SHADOW_MARGIN, (int64_t)h + WIN_TITLE_H + 2 * WIN_SHADOW_MARGIN);
}

void desktop_damage_window_content(uint32_t window_id, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    desktop_window_t *win = window_by_id(window_id);
    if (!win || !win->visible || win->minimized || w == 0 || h == 0) return;
    win_anim_t *a = anim_of(win);
    if ((a && (a->phase != WA_NONE || a->geo)) || overview_active()) {
        shell_activity();           /* drawn transformed: redraw everything */
        return;
    }
    /* Clip to the content area in 64 bits, then move to the screen */
    int64_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int64_t x1 = (int64_t)x + (int64_t)w, y1 = (int64_t)y + (int64_t)h;
    if (x1 > (int64_t)win->width) x1 = (int64_t)win->width;
    if (y1 > (int64_t)win->height) y1 = (int64_t)win->height;
    if (x0 >= x1 || y0 >= y1) return;
    damage_rect((int64_t)win->x + x0, (int64_t)win->y + WIN_TITLE_H + y0, x1 - x0, y1 - y0);
}

/*
 * App views (windows whose content the shell draws) are redrawn on demand
 * (desktop_window_redraw_in in desktop_shell.h): when they ask, and at
 * least every DESKTOP_VIEW_REFRESH_MS as a safety net for content that
 * changes without asking. Indexed like g_windows; 0 = no request.
 */
static uint64_t g_view_redraw_at[MAX_WINDOWS];
static uint64_t g_view_refreshed_at[MAX_WINDOWS];

void desktop_window_redraw_in(uint32_t window_id, uint32_t delay_ms) {
    desktop_window_t *w = window_by_id(window_id);
    if (!w || nxserver_owns_window(window_id)) return;
    long i = (long)(w - g_windows);
    if (i < 0 || i >= MAX_WINDOWS) return;
    uint64_t at = pit_get_ticks() + delay_ms;
    if (at == 0) at = 1;
    if (g_view_redraw_at[i] == 0 || at < g_view_redraw_at[i]) g_view_redraw_at[i] = at;
}

/* Slot i is an app view that is shown: does it need drawing this tick?
 * drawn_anyway: the whole screen is redrawn this tick. A redraw uses up a
 * due request and restarts the safety-net period. */
static int view_redraw_due(int i, uint64_t now, int drawn_anyway) {
    int due = drawn_anyway ||
              (g_view_redraw_at[i] != 0 && now >= g_view_redraw_at[i]) ||
              now - g_view_refreshed_at[i] >= DESKTOP_VIEW_REFRESH_MS ||
              now < g_view_refreshed_at[i];
    if (due) {
        if (g_view_redraw_at[i] != 0 && now >= g_view_redraw_at[i]) g_view_redraw_at[i] = 0;
        g_view_refreshed_at[i] = now;
    }
    return due;
}

static uint32_t text_hash(const char *a, int na, const char *b, int nb) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < na && a[i]; i++) h = (h ^ (uint8_t)a[i]) * 16777619u;
    h = (h ^ 0x1Fu) * 16777619u;
    for (int i = 0; i < nb && b[i]; i++) h = (h ^ (uint8_t)b[i]) * 16777619u;
    return h;
}

static void window_signature(const desktop_window_t *w, const win_anim_t *a, win_sig_t *out) {
    for (uint32_t k = 0; k < sizeof(*out); k++) ((uint8_t *)out)[k] = 0;
    if (!w || w->id == 0) return;
    wm_window_state_t *ws = wm_get_window_state(w->id);
    out->id = w->id;
    out->x = w->x; out->y = w->y; out->w = w->width; out->h = w->height;
    out->visible = w->visible; out->focused = w->focused; out->minimized = w->minimized;
    out->z = w->z_order;
    out->phase = a ? (uint8_t)a->phase : 0;
    out->mode = ws ? (uint8_t)ws->mode : 0;
    out->sidebar_open = ws ? (uint8_t)(ws->sidebar_expanded != 0) : 0;
    out->pip = (uint8_t)(wm_is_pip(w->id) != 0);
    float op = ws ? ws->opacity : 1.0f;
    if (!(op >= 0.0f)) op = 0.0f;
    if (op > 1.0f) op = 1.0f;
    out->opacity8 = (uint8_t)(op * 255.0f + 0.5f);
    out->text_hash = text_hash(w->title, WINDOW_TITLE_MAX, w->place, WINDOW_PLACE_MAX);
}

/* A shell component that is open or animating redraws everything. */
static int shell_busy(void) {
    wm_quick_menu_t *qm = wm_get_quick_menu();
    if (setup_assistant_active() || app_drawer_is_open() || switcher_active() || overview_active() ||
        menubar_is_open() || dock_menu_is_open() || anveshan_is_active() || control_center_is_visible() ||
        notification_center_is_visible() || volume_osd_is_visible() || keyboard_osd_is_visible() ||
        theme_osd_is_visible() || battery_warning_is_visible() || keyboard_osd_help_visible() ||
        (qm && qm->visible) || g_ges.kind != GES_NONE || dock_is_animating()) {
        return 1;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id && wm_is_animating(g_windows[i].id)) return 1;
    }
    return 0;
}

/* Once per tick, before drawing: animations that are not drawn by the
 * components themselves, then window visibility and snapshot captures.
 * Returns 1 if a snapshot was captured (the backbuffer was drawn over). */
static int frame_tick(void) {
    static uint64_t last_time = 0;
    uint64_t now = pit_get_ticks();
    float dt = (last_time > 0) ? (float)(now - last_time) / 1000.0f : 0.016f;
    if (!(dt > 0.0f) || dt > 0.1f) dt = 0.016f;   /* cap delta time */
    last_time = now;

    wm_tick(dt);
    /* Window springs, Stage/Overview/switcher motion, then visibility */
    windows_tick(dt);
    stage_tick(dt);
    overview_tick(dt);
    switcher_tick(dt);
    windows_sync_visibility();
    focus_top_visible();

    /* Window snapshots requested by Overview/Stage/minimize are drawn into
     * the backbuffer here, before the wallpaper overwrites it. No requests,
     * no work. */
    return wm_snapshot_service(nxui_canvas()) > 0;
}

/* Everything that changed since the last tick, into g_damage. */
static void frame_collect_damage(int had_input, int snapshot_drawn) {
    uint64_t now = pit_get_ticks();
    if (had_input || snapshot_drawn || g_motion_pending || nxui_motion_take() || shell_busy()) shell_activity();
    g_motion_pending = 0;
    if (now < g_settle_until || g_force_full) nxs_damage_add_full(&g_damage);
    g_force_full = 0;

    /* Windows: opened/closed/restacked/refocused/retitled redraw everything
     * (title bars, menu bar app name, dock indicators); a move or resize
     * damages the old and new frame. Client windows are redrawn when they
     * commit; windows of the shell's own views when they ask (blinking
     * caret, timers) and at least once a second (view_redraw_due). */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        desktop_window_t *w = &g_windows[i];
        win_sig_t cur;
        window_signature(w, &g_wanim[i], &cur);
        win_sig_t *old = &g_win_sig[i];
        uint32_t old_id = old->id;
        if (cur.id != old->id || cur.visible != old->visible || cur.focused != old->focused ||
            cur.minimized != old->minimized || cur.z != old->z || cur.phase != old->phase ||
            cur.mode != old->mode || cur.sidebar_open != old->sidebar_open || cur.pip != old->pip ||
            cur.opacity8 != old->opacity8 || cur.text_hash != old->text_hash) {
            shell_activity();
        } else if (cur.id && (cur.x != old->x || cur.y != old->y || cur.w != old->w || cur.h != old->h)) {
            damage_window_frame(old->x, old->y, old->w, old->h);
            damage_window_frame(cur.x, cur.y, cur.w, cur.h);
        }
        *old = cur;
        if (cur.id != old_id) {
            g_view_redraw_at[i] = 0;            /* a new window in this slot */
            g_view_refreshed_at[i] = now;
        }
        if (cur.id && cur.visible && !cur.minimized && !nxserver_owns_window(cur.id)) {
            if (cur.mode == WIN_MODE_SIDEBAR && !cur.sidebar_open) continue;   /* a static tab */
            if (view_redraw_due(i, now, g_damage.full)) {
                damage_rect(cur.x, cur.y, cur.w, (int64_t)cur.h + WIN_TITLE_H);
            }
        }
    }

    /* Clock: the widget each second, the menu bar strip with it (its clock,
     * network, volume and battery items refresh about once a second while
     * drawn), everything when the minute changes (date and time strings). */
    uint32_t hh, mm, ss, dd, mo, yy, wd;
    clock_tick();
    current_time(&hh, &mm, &ss, &dd, &mo, &yy, &wd);
    uint32_t minute_key = ((yy * 13u + mo) * 32u + dd) * 1440u + hh * 60u + mm;
    if (minute_key != g_seen_minute) {
        if (g_seen_minute != 0xFFFFFFFFu) nxs_damage_add_full(&g_damage);
        g_seen_minute = minute_key;
    }
    if (ss != g_seen_second) {
        g_seen_second = ss;
        network_indicator_update();     /* drawn in the menu bar strip below */
        damage_rect(CLOCK_WIDGET_X - 2, CLOCK_WIDGET_Y - 2, CLOCK_WIDGET_W + 4, CLOCK_WIDGET_H + 4);
        damage_rect(0, 0, g_desktop.width, WM_MENUBAR_H + 2);
        uint32_t sig = desktop_items_poll();
        if (sig != g_items_sig) {
            if (g_items_sig != 0) nxs_damage_add_full(&g_damage);
            g_items_sig = sig;
        }
    }

    /* Cursor: where it was drawn and where it is now */
    if (g_desktop.mouse_x != g_cursor_drawn_x || g_desktop.mouse_y != g_cursor_drawn_y) {
        if (g_cursor_drawn_x != CURSOR_NOT_DRAWN) {
            nxc_irect_t o = nxui_cursor_bounds(g_cursor_drawn_x, g_cursor_drawn_y);
            damage_rect(o.x, o.y, o.w, o.h);
        }
        nxc_irect_t n = nxui_cursor_bounds(g_desktop.mouse_x, g_desktop.mouse_y);
        damage_rect(n.x, n.y, n.w, n.h);
    }
}

/* Night Mode blue-light filter on the current pass (last step before the
 * cursor). Each pixel is filtered once, right after it is drawn. */
static void night_mode_pass(void) {
    if (!(g_night_mode_strength > 0.001f)) return;
    nxc_canvas_t *c = nxui_canvas();
    if (!c || !c->target || !c->target->pixels) return;
    float str = g_night_mode_strength;
    if (str > 1.0f) str = 1.0f;
    /* Fixed-point multipliers avoid per-pixel float */
    uint32_t blue_mult = (uint32_t)((1.0f - str * 0.45f) * 256.0f);
    uint32_t red_mult  = (uint32_t)((1.0f + str * 0.08f) * 256.0f);
    if (red_mult > 256) red_mult = 256;
    nxc_irect_t r = c->st.clip;
    if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 ||
        r.x + r.w > c->target->width || r.y + r.h > c->target->height) return;
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = c->target->pixels + (int64_t)y * c->target->stride;
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            uint32_t px = row[x];
            uint32_t rr = (((px >> 16) & 0xFF) * red_mult) >> 8;
            uint32_t b = ((px & 0xFF) * blue_mult) >> 8;
            if (rr > 255) rr = 255;
            row[x] = (px & 0xFF00FF00) | (rr << 16) | b;
        }
    }
}

/*
 * Draw every component into the current pass (the canvas is clipped to
 * it). first_pass runs the components' own per-frame ticks; later passes
 * of the same frame draw the same state (nxui_frame_dt() is 0).
 */
static void frame_draw(int first_pass) {
    uint64_t pf_t = rdtsc_now();
    nxc_canvas_t *frame = nxui_canvas();
    float dt = nxui_frame_dt();
    PF_MARK(PF_BG);

    /* Below the windows: skipped where this pass cannot show them (pass
     * culling above). Components without fixed bounds are skipped only
     * when an opaque window covers the whole pass. */
    int hidden = pass_skips_below_windows(pass_rect());
    nxc_irect_t bar = nxc_irect(0, 0, (int32_t)g_desktop.width, (int32_t)nxui_theme()->menubar_height + 4);
    if (!hidden && !pass_skips_below_windows(bar)) render_navigation_bar();
    PF_MARK(PF_NAV);
    nxc_irect_t clock = nxc_irect(CLOCK_WIDGET_X - CLOCK_SHADOW_REACH, CLOCK_WIDGET_Y - CLOCK_SHADOW_REACH,
                                  CLOCK_WIDGET_W + 2 * CLOCK_SHADOW_REACH, CLOCK_WIDGET_H + 2 * CLOCK_SHADOW_REACH);
    if (!hidden && !pass_skips_below_windows(clock)) render_globe_clock_widget();
    PF_MARK(PF_CLOCK);
    if (!hidden) {
        if (!anveshan_is_active()) render_search_bar();   /* active: drawn above windows */
        render_desktop_icons();
    }
    PF_MARK(PF_SEARCH);

    /* Tile preview (below windows): sprung glass panel with accent tint.
     * Always run: it steps its own animation. */
    render_tile_preview(frame, dt);

    /* Windows: Overview replaces the normal pass; Stage adds its strip
     * below and its flying windows above. */
    if (overview_active()) {
        overview_render(frame);
    } else {
        stage_render(frame);                /* always: it also keeps its snapshots fresh */
        render_windows();
        stage_render_overlay(frame);
    }
    PF_MARK(PF_WINDOWS);

    /* Modular dock rendering */
    if (first_pass) dock_tick();  /* launch bounce */
    dock_render(frame, g_desktop.mouse_x, g_desktop.mouse_y);
    PF_MARK(PF_DOCK);

    /* Active search field and its results float above windows */
    if (anveshan_is_active()) render_search_bar();

    /* Menu bar dropdown (above windows and the dock) */
    menubar_render(frame);

    /* NX+Tab app switcher HUD */
    switcher_render(frame);
    render_resize_hint(frame);

    /* App Drawer overlay (above everything except cursor) */
    if (first_pass) app_drawer_update((uint32_t)(nxui_frame_dt() * 1000.0f));
    app_drawer_render(frame);

    /* Control Center, Notification Center, then the OSDs above them */
    if (first_pass) control_center_tick();
    control_center_render(frame);
    if (first_pass) notification_center_tick();
    notification_center_render(frame);
    if (first_pass) volume_osd_tick();
    volume_osd_render(frame);
    if (first_pass) keyboard_osd_tick();
    keyboard_osd_render(frame);
    if (first_pass) theme_osd_tick();
    theme_osd_render(frame);
    if (first_pass) battery_warning_tick();
    battery_warning_render(frame);

    /* Render keyboard shortcut help overlay (full-screen, above everything) */
    keyboard_osd_help_render(frame);

    /* Quick Access Menu (window mode switcher) */
    wm_quick_menu_t *qm = wm_get_quick_menu();
    if (qm && qm->visible) {
        const nxui_theme_t *th = nxui_theme();
        float mw = 3.0f * 40.0f + 16.0f, mh = 48.0f;
        nxc_rect_t menu = nxc_rect((float)qm->x, (float)qm->y, mw, mh);
        nxui_panel(frame, menu, 12.0f, &th->mat_popover, 2);
        static const nxsym_t syms[3] = { NXSYM_GRID, NXSYM_MAXIMIZE, NXSYM_CHEVRON_RIGHT };
        for (int i = 0; i < 3; i++) {
            nxc_rect_t cell = nxc_rect(menu.x + 8.0f + 40.0f * (float)i, menu.y + 8.0f, 32.0f, 32.0f);
            if (qm->hover_idx == i) nxc_fill_rrect(frame, cell, 8.0f, th->control_fill_hover);
            nxsym_draw(frame, syms[i], cell.x + 16.0f, cell.y + 16.0f, 18.0f,
                       qm->hover_idx == i ? th->accent : th->text_primary, 0.0f);
        }
    }

    /* First-run Setup Assistant covers the whole desktop while active */
    if (setup_assistant_active()) {
        if (first_pass) setup_assistant_tick(nxui_frame_dt());
        setup_assistant_render(frame);
    }

    /* Night Mode, last before the cursor */
    night_mode_pass();
    PF_MARK(PF_OVERLAYS);
}

#ifdef DESKTOP_DAMAGE_VERIFY
/*
 * Debug self-check: redraw the whole frame into a second buffer (with
 * nxui_frame_dt() 0, so nothing animates twice) and compare it with the
 * backbuffer before the cursor goes on. Any difference outside the cursor
 * is a bug: damage that was missed, or drawing that ignored the clip.
 * Differences inside client windows are counted apart: a client may draw
 * its next frame into its buffer before it commits it. So are differences
 * inside windows of the shell's own views, which are redrawn every tick.
 */
static uint32_t *g_verify_px = 0;

static void frame_verify(uint64_t tick_no, int drew) {
    uint32_t w = g_desktop.width, h = g_desktop.height;
    if (!g_verify_px) {
        g_verify_px = (uint32_t *)nx_malloc((uint64_t)w * h * 4u);
        if (!g_verify_px) { serial_puts("[DAMAGE] verify: no memory, self-check off\n"); return; }
        serial_puts("[DAMAGE] verify: self-check on (debug build)\n");
    }
    int motion_before = nxui_motion_take();
    uint32_t *back = nxui_retarget(g_verify_px);
    if (!back) return;
    nxui_pass_begin(0, 0, (int32_t)w, (int32_t)h, 0);
    g_cull_off = 1;                 /* the reference draws everything, the slow way */
    frame_draw(0);
    g_cull_off = 0;
    nxui_retarget(back);
    int motion_verify = nxui_motion_take();
    if (motion_before) nxui_motion_note();
    if (motion_verify && !drew && !motion_before) {
        serial_puts("[DAMAGE] verify: an animation is unsettled but the tick drew nothing\n");
    }

    nxc_irect_t cur = nxui_cursor_bounds(g_desktop.mouse_x, g_desktop.mouse_y);
    uint64_t bad = 0, client_bad = 0, view_bad = 0;
    int32_t bx0 = BBOX_NONE, by0 = BBOX_NONE, bx1 = -1, by1 = -1;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t *a = back + (uint64_t)y * w, *b = g_verify_px + (uint64_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            if (a[x] == b[x]) continue;
            if ((int32_t)x >= cur.x && (int32_t)x < cur.x + cur.w && (int32_t)y >= cur.y && (int32_t)y < cur.y + cur.h)
                continue;
            /* Attribute the pixel to the topmost window whose frame holds it */
            int in_window = 0;
            for (int k = g_zcount - 1; k >= 0 && !in_window; k--) {
                desktop_window_t *cw = window_by_id(g_zorder[k]);
                if (!cw || !cw->id || !cw->visible || cw->minimized) continue;
                if ((int32_t)x < cw->x || (int32_t)x >= cw->x + (int32_t)cw->width ||
                    (int32_t)y < cw->y || (int32_t)y >= cw->y + WIN_TITLE_H + (int32_t)cw->height) continue;
                if (!nxserver_owns_window(cw->id)) in_window = 2;
                else if ((int32_t)y >= cw->y + WIN_TITLE_H) in_window = 1;   /* client content */
                else break;                                                  /* its title bar */
            }
            if (in_window == 1) { client_bad++; continue; }
            if (in_window == 2) { view_bad++; continue; }
            bad++;
            if ((int32_t)x < bx0) bx0 = (int32_t)x;
            if ((int32_t)y < by0) by0 = (int32_t)y;
            if ((int32_t)x > bx1) bx1 = (int32_t)x;
            if ((int32_t)y > by1) by1 = (int32_t)y;
        }
    }
    if (view_bad) {
        /* Windows of the shell's own views are redrawn when they ask and
         * at least once a second, so a difference there is a view whose
         * content changed without asking (shown within a second; add a
         * desktop_window_redraw_in call if it should be sooner) or one
         * that draws differently the second time in one frame (e.g. it
         * set its title while drawing), not missed damage. */
        serial_puts("[DAMAGE] verify: tick ");
        serial_dec((int64_t)tick_no);
        serial_puts(": ");
        serial_dec((int64_t)view_bad);
        serial_puts(" px differ inside a shell view window (redrawn on request)\n");
    }
    if (bad || client_bad) {
        g_fs.verify_bad_frames++;
        g_fs.verify_bad_px += bad;
        serial_puts("[DAMAGE] VERIFY MISMATCH tick ");
        serial_dec((int64_t)tick_no);
        serial_puts(drew ? " (drawn): " : " (skipped): ");
        serial_dec((int64_t)bad);
        serial_puts(" px in ");
        serial_dec(bx0 == BBOX_NONE ? 0 : bx0); serial_puts(","); serial_dec(by0 == BBOX_NONE ? 0 : by0);
        serial_puts(" ");
        serial_dec(bx1 < 0 ? 0 : bx1 - bx0 + 1); serial_puts("x"); serial_dec(by1 < 0 ? 0 : by1 - by0 + 1);
        serial_puts(", client windows ");
        serial_dec((int64_t)client_bad);
        serial_puts(" px\n");
        /* Repair: the next tick redraws everything, so one bug is one line */
        g_force_full = 1;
    }
}
#endif

/* Log one line of frame statistics every FRAME_LOG_TICKS ticks, or every
 * FRAME_LOG_MS when slow frames stretch the ticks out. */
static void frame_log(void) {
    uint64_t now = pit_get_ticks();
    if (g_fs.ticks < FRAME_LOG_TICKS && now - g_fs.window_start < FRAME_LOG_MS) return;
    uint32_t drawn = g_fs.full + g_fs.partial;
    serial_puts("[DESKTOP] Frames: ");
    serial_dec(g_fs.ticks);
    serial_puts(" ticks in ");
    serial_dec((int64_t)(now - g_fs.window_start));
    serial_puts(" ms: ");
    serial_dec(g_fs.full);
    serial_puts(" full, ");
    serial_dec(g_fs.partial);
    serial_puts(" partial (");
    serial_dec(g_fs.passes);
    serial_puts(" passes, avg ");
    serial_dec(g_fs.partial ? (int64_t)(g_fs.partial_px / g_fs.partial) : 0);
    serial_puts(" px, max ");
    serial_dec((int64_t)g_fs.partial_px_max);
    serial_puts(" px), ");
    serial_dec(g_fs.skipped);
    serial_puts(" skipped; per drawn frame: draw ");
    serial_dec(drawn ? (int64_t)(g_fs.draw_ms / drawn) : 0);
    serial_puts(" ms, present ");
    serial_dec(drawn ? (int64_t)(g_fs.present_ms / drawn) : 0);
    serial_puts(" ms, kcyc bg/nav/clock/search/win/dock/ovl/end/input/sleep:");
    for (int i = 0; i < PF_COUNT; i++) {
        serial_puts(" ");
        serial_dec(drawn ? (int64_t)(g_pf[i] / drawn / 1000) : 0);
        g_pf[i] = 0;
    }
#ifdef DESKTOP_DAMAGE_VERIFY
    serial_puts("; verify mismatches: ");
    serial_dec(g_fs.verify_bad_frames);
    serial_puts(" frames, ");
    serial_dec((int64_t)g_fs.verify_bad_px);
    serial_puts(" px");
#endif
    serial_puts("\n");
    for (uint32_t k = 0; k < sizeof(g_fs); k++) ((uint8_t *)&g_fs)[k] = 0;
    g_fs.window_start = now;
}

/*
 * One frame tick: tick, collect damage, draw the damaged rectangles (or
 * nothing), present them. had_input: the shell handled input this tick.
 */
static void render_desktop_tick(int had_input) {
    if (!g_damage_ready) {
        nxs_damage_init(&g_damage, (int32_t)g_desktop.width, (int32_t)g_desktop.height);
        g_damage_ready = 1;
        g_fs.window_start = pit_get_ticks();
        shell_activity();           /* the first frames draw everything */
    }
    static uint64_t tick_no = 0;
    tick_no++;
    g_fs.ticks++;

    int snapshot_drawn = frame_tick();
    frame_collect_damage(had_input, snapshot_drawn);

    int drew = 0;
    if (nxs_damage_empty(&g_damage)) {
        nxui_frame_skipped();
        g_fs.skipped++;
    } else {
        uint64_t t0 = pit_get_ticks();
        nx_fb_rect_t rects[NXS_DAMAGE_RECTS];
        uint32_t n = 0;
        if (g_damage.full) {
            nxui_pass_begin(0, 0, (int32_t)g_desktop.width, (int32_t)g_desktop.height, 1);
            frame_draw(1);
            g_fs.full++;
            g_fs.passes++;
        } else {
            for (uint32_t i = 0; i < g_damage.count; i++) {
                const nxs_rect_t *r = &g_damage.rects[i];
                if (!nxui_pass_begin(r->x, r->y, r->w, r->h, n == 0)) continue;
                frame_draw(n == 0);
                rects[n].x = r->x; rects[n].y = r->y;
                rects[n].w = (uint32_t)r->w; rects[n].h = (uint32_t)r->h;
                n++;
            }
            uint64_t area = nxs_damage_area(&g_damage);
            g_fs.partial++;
            g_fs.passes += n;
            g_fs.partial_px += area;
            if (area > g_fs.partial_px_max) g_fs.partial_px_max = area;
        }
        /* The passes stepped animations: one still moving needs the next tick */
        if (nxui_motion_take()) g_motion_pending = 1;
#ifdef DESKTOP_DAMAGE_VERIFY
        frame_verify(tick_no, 1);
#endif
        /* Cursor on top, clipped to every damaged rectangle */
        if (g_damage.full) {
            nxui_draw_cursor(0, 0, (int32_t)g_desktop.width, (int32_t)g_desktop.height,
                             g_desktop.mouse_x, g_desktop.mouse_y);
        } else {
            for (uint32_t i = 0; i < n; i++) {
                nxui_draw_cursor(rects[i].x, rects[i].y, (int32_t)rects[i].w, (int32_t)rects[i].h,
                                 g_desktop.mouse_x, g_desktop.mouse_y);
            }
        }
        uint64_t t1 = pit_get_ticks();
        nxui_present(g_damage.full ? 0 : rects, g_damage.full ? 0 : n);
        uint64_t t2 = pit_get_ticks();
        g_fs.draw_ms += t1 - t0;
        g_fs.present_ms += t2 - t1;
        g_cursor_drawn_x = g_desktop.mouse_x;
        g_cursor_drawn_y = g_desktop.mouse_y;
        drew = 1;
    }
#ifdef DESKTOP_DAMAGE_VERIFY
    if (!drew) frame_verify(tick_no, 0);
#endif
    (void)drew;
    nxs_damage_clear(&g_damage);
    frame_log();
}

/* ============ Window Management ============ */

/* Usable desktop area for windows (title bar top, between menu bar and dock). */
static void work_area(int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    *x = 0;
    *y = WM_MENUBAR_H;
    *w = (int32_t)g_desktop.width;
    *h = (int32_t)g_desktop.height - WM_MENUBAR_H - WM_DOCK_RESERVE;
}

void desktop_fit_window_size(uint32_t *width, uint32_t *height) {
    if (!width || !height) return;
    int32_t ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    (void)ax; (void)ay;
    if (*width > (uint32_t)aw - 16u) *width = (uint32_t)aw - 16u;
    if (*height > (uint32_t)(ah - WIN_TITLE_H - 8)) *height = (uint32_t)(ah - WIN_TITLE_H - 8);
    if (*width < 120u) *width = 120u;
    if (*height < 60u) *height = 60u;
}

uint32_t desktop_create_window(const char *title, int32_t x, int32_t y,
                               uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        serial_puts("[DESKTOP] Error: window with zero size refused\n");
        return 0;
    }
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        serial_puts("[DESKTOP] Error: Max windows reached\n");
        return 0;
    }

    /* Fit the screen: never larger than the work area, title bar below
     * the menu bar. */
    int32_t ax, ay, aw, ah;
    work_area(&ax, &ay, &aw, &ah);
    desktop_fit_window_size(&width, &height);

    /* Cascade away from a window already sitting at the same spot */
    for (int tries = 0; tries < 12; tries++) {
        int clash = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            const desktop_window_t *o = &g_windows[i];
            if (o->id == 0 || !o->visible || o->minimized) continue;
            int32_t ddx = o->x - x, ddy = o->y - y;
            if (ddx > -12 && ddx < 12 && ddy > -12 && ddy < 12) { clash = 1; break; }
        }
        if (!clash) break;
        x += 30;
        y += 26;
        if (x + (int32_t)width > ax + aw - 8) x = ax + 40;
        if (y + (int32_t)height + WIN_TITLE_H > ay + ah) y = ay + 12;
    }
    if (x < ax - (int32_t)width + 80) x = ax - (int32_t)width + 80;
    if (x > ax + aw - 80) x = ax + aw - 80;
    if (y < ay) y = ay;
    if (y > ay + ah - WIN_TITLE_H) y = ay + ah - WIN_TITLE_H;

    desktop_window_t *win = &g_windows[slot];
    win->id = g_next_window_id++;
    if (g_next_window_id == 0) g_next_window_id = 1;   /* 0 means "no window" */

    /* Validate and copy title safely */
    if (title) {
        shell_strcpy(win->title, title, WINDOW_TITLE_MAX);
    } else {
        win->title[0] = '\0';  /* Empty title if NULL passed */
    }
    win->place[0] = '\0';
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->visible = 1;
    win->focused = 1;
    win->minimized = 0;
    win->dragging = 0;
    win->no_resize = 0;
    win->render_content = NULL;
    win->render_ctx = NULL;
    win->key_handler = NULL;
    win->key_ctx = NULL;
    win->mouse_handler = NULL;
    win->mouse_ctx = NULL;
    win->scroll_handler = NULL;
    win->scroll_ctx = NULL;

    g_desktop.window_count++;

    /* Window manager state (space, tiling) and an opening fade */
    wm_register_window(win->id);
    win_anim_t *a = &g_wanim[slot];
    a->geo = 0;
    a->phase = WA_OPEN;
    a->vs.value = 0.0f;
    a->vs.velocity = 0.0f;

    /* Unfocus other windows, put the new one on top */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && g_windows[i].id != win->id) {
            g_windows[i].focused = 0;
        }
    }
    z_push_top(win->id);
    stage_on_window_created(win->id);

    serial_puts("[DESKTOP] Created window: ");
    serial_puts(win->title);  /* Use copied title, not raw pointer */
    serial_puts(" id=");
    serial_dec(win->id);
    serial_puts("\n");

    return win->id;
}

/* Remove a window everywhere it is referenced. */
static void window_destroy_now(desktop_window_t *w) {
    if (!w || w->id == 0) return;
    uint32_t id = w->id;
    nxserver_window_destroyed(id);   /* a client window: unmap its buffers */
    stage_on_window_destroyed(id);
    overview_on_window_destroyed(id);
    switcher_on_window_destroyed(id);
    wm_unregister_window(id);
    z_remove(id);
    if (g_ges.id == id) {
        g_ges.kind = GES_NONE;
        g_ges.id = 0;
        wm_clear_tile_preview();
    }
    if (g_light_press_id == id) g_light_press_id = 0;
    if (g_mouse_capture_id == id) g_mouse_capture_id = 0;   /* no content capture on a dead window */
    win_anim_t *a = anim_of(w);
    if (a) {
        a->geo = 0;
        a->phase = WA_NONE;
    }
    w->id = 0;
    w->focused = 0;
    if (g_desktop.window_count > 0) g_desktop.window_count--;
    z_reindex();
    focus_top_visible();
    serial_puts("[DESKTOP] Destroyed window id=");
    serial_dec(id);
    serial_puts("\n");
}

int desktop_destroy_window(uint32_t window_id) {
    if (window_id == 0) return -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            window_destroy_now(&g_windows[i]);
            return 0;
        }
    }
    return -1;
}

void desktop_focus_window(uint32_t window_id) {
    desktop_window_t *target = NULL;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id && window_id != 0) {
            target = &g_windows[i];
        }
    }
    if (!target) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0) g_windows[i].focused = 0;
    }
    target->focused = 1;
    z_push_top(target->id);
}

/* ---- Window actions: minimize, close, zoom, tile ---- */

/* Where a minimizing window flies to: the dock's minimized section. */
static nxc_rect_t dock_target_for(const desktop_window_t *w) {
    /* The dock's minimized section (or the taskbar) */
    int tx = (int)g_desktop.width / 2, ty = (int)g_desktop.height - 46;
    dock_minimize_target(&tx, &ty);
    float cx = (float)tx, cy = (float)ty;
    float tw = 56.0f, th = 56.0f * ((float)w->height + WIN_TITLE_H) / (float)(w->width ? w->width : 1u);
    if (th > 56.0f) th = 56.0f;
    return nxc_rect(cx - tw * 0.5f, cy - th * 0.5f, tw, th);
}

static int window_minimize(desktop_window_t *w) {
    if (!w || w->id == 0 || w->minimized) return -1;
    win_anim_t *a = anim_of(w);
    if (a && a->phase == WA_CLOSE) return -1;
    if (a) {
        a->dock = dock_target_for(w);
        a->phase = WA_MINIMIZE;
        a->vs.value = 0.0f;
        a->vs.velocity = 0.0f;
    }
    wm_snapshot_request(w->id);
    wm_add_minimized(w->id, 0, w->title, (int32_t)(a ? a->dock.x : 0), (int32_t)(a ? a->dock.y : 0));
    w->minimized = 1;
    w->focused = 0;
    focus_top_visible();
    serial_puts("[WM] minimize id=");
    serial_dec(w->id);
    serial_puts("\n");
    return 0;
}

/* Dock "Hide": minimize one window (dock_set_hide_handler). */
static int desktop_dock_hide_window(uint32_t id) {
    for (int k = 0; k < MAX_WINDOWS; k++) {
        if (g_windows[k].id == id && id != 0) return window_minimize(&g_windows[k]);
    }
    return -1;
}

static void window_restore_from_dock(desktop_window_t *w) {
    if (!w || !w->minimized) return;
    win_anim_t *a = anim_of(w);
    w->minimized = 0;
    wm_remove_minimized(w->id);
    if (a) {
        if (a->phase != WA_MINIMIZE) a->dock = dock_target_for(w);
        a->phase = WA_RESTORE;
        if (a->vs.value <= 0.0f || a->vs.value > 1.0f) a->vs.value = 1.0f;
        a->vs.velocity = 0.0f;
    }
    wm_snapshot_request(w->id);
}

static int window_close(desktop_window_t *w) {
    if (!w || w->id == 0) return -1;
    win_anim_t *a = anim_of(w);
    if (a && a->phase == WA_CLOSE) return 0;
    /* A live app window: the app decides (CLOSE_REQUEST), then destroys it */
    if (nxserver_close_requested(w->id)) return 0;
    if (w->minimized || !w->visible || !a) {
        window_destroy_now(w);
        return 0;
    }
    a->phase = WA_CLOSE;
    a->vs.value = 1.0f;
    a->vs.velocity = 0.0f;
    a->geo = 0;
    wm_snapshot_request(w->id);
    wm_window_set_hidden(w->id, WM_HIDE_CLOSING, 1);
    w->visible = 0;
    w->focused = 0;
    focus_top_visible();
    serial_puts("[WM] close id=");
    serial_dec(w->id);
    serial_puts("\n");
    return 0;
}

/* Remember the user's own frame before the first tile/zoom. */
static void remember_user_frame(desktop_window_t *w, wm_window_state_t *ws) {
    if (!ws || ws->is_tiled || ws->is_maximized) return;
    ws->pre_tile_x = w->x;
    ws->pre_tile_y = w->y;
    ws->pre_tile_w = w->width;
    ws->pre_tile_h = w->height;
}

static void window_tile(desktop_window_t *w, wm_tile_pos_t tile) {
    if (!w || tile == TILE_NONE || w->no_resize) return;
    wm_window_state_t *ws = wm_get_window_state(w->id);
    if (!ws) return;
    int32_t tx = 0, ty = 0;
    uint32_t tw = 0, th = 0;
    wm_get_tile_geometry(tile, &tx, &ty, &tw, &th);
    if (tw < 40 || th < (uint32_t)WIN_TITLE_H + 20) return;
    remember_user_frame(w, ws);
    ws->tile_pos = tile;
    ws->is_tiled = (tile != TILE_MAXIMIZE);
    ws->is_maximized = (tile == TILE_MAXIMIZE);
    window_animate_frame(w, tx, ty, tw, th - WIN_TITLE_H);   /* tile height includes the title bar */
    wm_window_content_changed(w->id);
}

/* Back to the user's frame after a tile or zoom. Returns 1 if it moved. */
static int window_untile(desktop_window_t *w, int animate) {
    wm_window_state_t *ws = w ? wm_get_window_state(w->id) : NULL;
    if (!ws || !(ws->is_tiled || ws->is_maximized)) return 0;
    ws->is_tiled = 0;
    ws->is_maximized = 0;
    ws->tile_pos = TILE_NONE;
    if (ws->pre_tile_w == 0 || ws->pre_tile_h == 0) return 0;
    if (animate) {
        window_animate_frame(w, ws->pre_tile_x, ws->pre_tile_y, ws->pre_tile_w, ws->pre_tile_h);
    } else {
        w->width = ws->pre_tile_w;
        w->height = ws->pre_tile_h;
    }
    wm_window_content_changed(w->id);
    return 1;
}

/* Zoom (green light, title bar double-click, NX+Shift+Up): fill the work
 * area or go back to the user's frame. */
static void window_zoom(desktop_window_t *w) {
    wm_window_state_t *ws = w ? wm_get_window_state(w->id) : NULL;
    if (!ws || w->no_resize) return;
    if (ws->is_maximized) window_untile(w, 1);
    else window_tile(w, TILE_MAXIMIZE);
}

/* Bring a window (and its space / Stage group) forward, restoring it. */
static void window_activate(desktop_window_t *w) {
    if (!w || w->id == 0) return;
    int sp = wm_window_space(w->id);
    if (sp >= 0 && sp != wm_space_current()) wm_space_switch(sp);
    if (w->minimized) window_restore_from_dock(w);
    stage_on_window_activated(w->id);
    windows_sync_visibility();
    desktop_focus_window(w->id);
}

/* ---- Shell hooks for Overview, Stage and the switcher (window_manager.h) ---- */

int wm_shell_window_order(uint32_t *ids, int max) {
    if (!ids || max <= 0) return 0;
    int n = 0;
    for (int zi = g_zcount - 1; zi >= 0 && n < max; zi--) {
        desktop_window_t *w = window_by_id(g_zorder[zi]);
        if (!w) continue;
        win_anim_t *a = anim_of(w);
        if (a && a->phase == WA_CLOSE) continue;
        ids[n++] = w->id;
    }
    return n;
}

int wm_shell_get_frame(uint32_t window_id, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h) {
    desktop_window_t *win = window_by_id(window_id);
    if (!win) return -1;
    win_anim_t *a = anim_of(win);
    if (a && a->geo) {
        /* Report where the window is going, not a mid-flight frame */
        if (x) *x = (int32_t)a->fx1;
        if (y) *y = (int32_t)a->fy1;
        if (w) *w = (uint32_t)a->fw1;
        if (h) *h = (uint32_t)a->fh1;
        return 0;
    }
    if (x) *x = win->x;
    if (y) *y = win->y;
    if (w) *w = win->width;
    if (h) *h = win->height;
    return 0;
}

int wm_shell_set_frame(uint32_t window_id, int32_t x, int32_t y, uint32_t w, uint32_t h, int animate) {
    desktop_window_t *win = window_by_id(window_id);
    if (!win || w == 0 || h == 0 || w > 8192u || h > 8192u) return -1;
    if (animate) {
        window_animate_frame(win, x, y, w, h);
    } else {
        win_anim_t *a = anim_of(win);
        if (a) a->geo = 0;
        win->x = x;
        win->y = y;
        win->width = w;
        win->height = h;
    }
    wm_window_content_changed(window_id);
    return 0;
}

void wm_shell_raise(uint32_t window_id) {
    desktop_window_t *win = window_by_id(window_id);
    if (!win) return;
    windows_sync_visibility();
    desktop_focus_window(window_id);
}

int wm_shell_minimize(uint32_t window_id) {
    return window_minimize(window_by_id(window_id));
}

void desktop_set_window_place(uint32_t window_id, const char *place) {
    desktop_window_t *w = window_by_id(window_id);
    if (!w) return;
    if (place) shell_strcpy(w->place, place, WINDOW_PLACE_MAX);
    else w->place[0] = '\0';
}

void desktop_set_window_render(uint32_t window_id,
                               void (*callback)(desktop_window_t *win, void *ctx),
                               void *ctx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].render_content = callback;
            g_windows[i].render_ctx = ctx;
            break;
        }
    }
}

void desktop_set_window_key_handler(uint32_t window_id,
                                    void (*handler)(desktop_window_t *win, 
                                                    uint8_t scancode, 
                                                    uint8_t pressed, 
                                                    void *ctx),
                                    void *ctx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].key_handler = handler;
            g_windows[i].key_ctx = ctx;
            break;
        }
    }
}

void desktop_set_window_scroll_handler(uint32_t window_id,
                                       void (*handler)(desktop_window_t *win,
                                                       int32_t lx, int32_t ly,
                                                       int32_t notches,
                                                       void *ctx),
                                       void *ctx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].scroll_handler = handler;
            g_windows[i].scroll_ctx = ctx;
            break;
        }
    }
}

void desktop_set_window_mouse_handler(uint32_t window_id,
                                      void (*handler)(desktop_window_t *win,
                                                      int32_t lx, int32_t ly,
                                                      uint8_t buttons,
                                                      uint8_t prev_buttons,
                                                      void *ctx),
                                      void *ctx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].mouse_handler = handler;
            g_windows[i].mouse_ctx = ctx;
            break;
        }
    }
}

const desktop_window_t *desktop_window_slot(int i) {
    if (i < 0 || i >= MAX_WINDOWS || g_windows[i].id == 0) return NULL;
    return &g_windows[i];
}

uint32_t desktop_focused_window_id(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && g_windows[i].visible && g_windows[i].focused) return g_windows[i].id;
    }
    return 0;
}

int desktop_activate_window(uint32_t window_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id && window_id != 0) {
            /* Restores (animated), switches space / Stage group, focuses */
            window_activate(&g_windows[i]);
            return 0;
        }
    }
    return -1;
}

int desktop_close_window(uint32_t window_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id && window_id != 0) {
            return window_close(&g_windows[i]);
        }
    }
    return -1;
}

void desktop_set_night_mode(float strength) {
    if (!(strength >= 0.0f)) strength = 0.0f;   /* also rejects NaN */
    if (strength > 1.0f) strength = 1.0f;
    g_night_mode_strength = strength;
    g_force_full = 1;               /* every pixel is filtered differently */
    serial_puts("[DESKTOP] Night mode strength changed\n");
}

float desktop_get_night_mode(void) { return g_night_mode_strength; }

/* ============ Input Handling ============ */

/* Content mouse routing: the window whose content took a button press
 * keeps receiving the pointer until every button is released. */
static uint32_t g_mouse_capture_id = 0;

static desktop_window_t *window_by_id(uint32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == id) return &g_windows[i];
    }
    return NULL;
}

/* Topmost visible window whose frame (title bar + content) contains (x, y). */
static desktop_window_t *window_at_point(int32_t x, int32_t y) {
    desktop_window_t *best = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        desktop_window_t *w = &g_windows[i];
        if (w->id == 0 || !w->visible || w->minimized) continue;
        if (x < w->x || x >= w->x + (int32_t)w->width) continue;
        if (y < w->y || y >= w->y + WIN_TITLE_H + (int32_t)w->height) continue;
        if (!best || w->z_order >= best->z_order) best = w;
    }
    return best;
}

static void deliver_content_mouse(desktop_window_t *w, uint8_t buttons, uint8_t prev_buttons) {
    if (!w || !w->mouse_handler) return;
    w->mouse_handler(w, g_desktop.mouse_x - w->x, g_desktop.mouse_y - (w->y + WIN_TITLE_H),
                     buttons, prev_buttons, w->mouse_ctx);
}

/* Returns 1 if a window content area consumed this pointer event. */
static int route_content_mouse(uint8_t buttons, uint8_t prev_buttons) {
    desktop_window_t *cap = window_by_id(g_mouse_capture_id);
    if (cap) {
        if (cap->minimized || !cap->visible) {
            g_mouse_capture_id = 0;
        } else {
            deliver_content_mouse(cap, buttons, prev_buttons);
            if ((buttons & 0x07) == 0) g_mouse_capture_id = 0;
            return 1;
        }
    }
    desktop_window_t *w = window_at_point(g_desktop.mouse_x, g_desktop.mouse_y);
    if (!w || !w->mouse_handler) return 0;
    if (g_desktop.mouse_y < w->y + WIN_TITLE_H) return 0;   /* title bar: shell handles */
    uint8_t pressed_now = (uint8_t)(buttons & ~prev_buttons & 0x07);
    if (pressed_now) {
        if (!w->focused) desktop_focus_window(w->id);
        g_mouse_capture_id = w->id;
        deliver_content_mouse(w, buttons, prev_buttons);
        return 1;
    }
    if (w->focused) deliver_content_mouse(w, buttons, prev_buttons);   /* hover */
    return 0;
}

/* ============ Scroll wheel ============ */

extern int sprefs_get_bool(const char *sec, const char *key, int def);

/*
 * Scroll goes to the window under the pointer, focused or not (as on
 * macOS), never to a window hidden behind a shell overlay. A button drag
 * in progress keeps it on the captured window.
 */
/* A shell overlay covers the windows (pointer input is not theirs). */
static int shell_overlay_open(void) {
    return setup_assistant_active() || app_drawer_is_open() || switcher_active() ||
           overview_active() || menubar_is_open() || anveshan_is_active() ||
           control_center_is_visible() || notification_center_is_visible();
}

void desktop_handle_scroll(int32_t notches) {
    if (notches == 0) return;
    static int s_logged;
    if (!s_logged) { s_logged = 1; serial_puts("[DESKTOP] Scroll wheel input received\n"); }
    if (notches > 127) notches = 127;
    if (notches < -127) notches = -127;

    if (shell_overlay_open()) return;

    if (sprefs_get_bool("mouse", "natural_scrolling", 0)) notches = -notches;

    desktop_window_t *w = window_by_id(g_mouse_capture_id);
    if (!w || w->minimized || !w->visible)
        w = window_at_point(g_desktop.mouse_x, g_desktop.mouse_y);
    if (!w || !w->scroll_handler) return;
    if (g_desktop.mouse_y < w->y + WIN_TITLE_H) return;   /* title bar */
    w->scroll_handler(w, g_desktop.mouse_x - w->x, g_desktop.mouse_y - (w->y + WIN_TITLE_H),
                      notches, w->scroll_ctx);
}

uint32_t desktop_pointer_window(int32_t *mx, int32_t *my, int32_t *lx, int32_t *ly) {
    if (mx) *mx = g_desktop.mouse_x;
    if (my) *my = g_desktop.mouse_y;
    if (lx) *lx = 0;
    if (ly) *ly = 0;
    if (shell_overlay_open()) return 0;
    desktop_window_t *w = window_at_point(g_desktop.mouse_x, g_desktop.mouse_y);
    if (!w || g_desktop.mouse_y < w->y + WIN_TITLE_H) return 0;
    if (lx) *lx = g_desktop.mouse_x - w->x;
    if (ly) *ly = g_desktop.mouse_y - (w->y + WIN_TITLE_H);
    return w->id;
}

uint8_t desktop_mouse_buttons(void) { return g_desktop.mouse_buttons; }

int desktop_window_info(uint32_t window_id, desktop_window_info_t *out) {
    desktop_window_t *w = window_by_id(window_id);
    if (!w || !out) return -1;
    win_anim_t *a = anim_of(w);
    wm_window_state_t *ws = wm_get_window_state(w->id);
    out->width = w->width;
    out->height = w->height;
    out->content_x = w->x;
    out->content_y = w->y + WIN_TITLE_H;
    out->focused = w->focused;
    out->minimized = w->minimized;
    out->visible = w->visible;
    out->maximized = (ws && ws->is_maximized) ? 1 : 0;
    out->tiled = (ws && ws->is_tiled) ? 1 : 0;
    out->settled = (!a || !a->geo) && !(g_ges.kind == GES_RESIZE && g_ges.id == w->id);
    return 0;
}

int desktop_set_window_title(uint32_t window_id, const char *title) {
    desktop_window_t *w = window_by_id(window_id);
    if (!w) return -1;
    if (title) shell_strcpy(w->title, title, WINDOW_TITLE_MAX);
    else w->title[0] = '\0';
    return 0;
}

void desktop_set_window_resizable(uint32_t window_id, int resizable) {
    desktop_window_t *w = window_by_id(window_id);
    if (w) {
        w->no_resize = resizable ? 0 : 1;
        g_force_full = 1;           /* the zoom control may change */
    }
}

/* ============ Window move / resize gestures ============ */

/* Resize edges of w under (x, y): a band 6 px outside to 3 px inside the
 * frame, with 14 px corner zones. Sets *inside when the point is within
 * the frame but not on an edge (the window covers the point). */
static uint32_t resize_edges_at(const desktop_window_t *w, int32_t x, int32_t y, int *inside) {
    *inside = 0;
    int32_t fx = w->x, fy = w->y, fw = (int32_t)w->width, fh = (int32_t)w->height + WIN_TITLE_H;
    if (x < fx - 6 || x >= fx + fw + 6 || y < fy - 6 || y >= fy + fh + 6) return 0;
    uint32_t e = 0;
    if (x < fx + 3) e |= EDGE_L;
    if (x >= fx + fw - 3) e |= EDGE_R;
    if (y < fy + 3) e |= EDGE_T;
    if (y >= fy + fh - 3) e |= EDGE_B;
    if (e & (EDGE_L | EDGE_R)) {
        if (y < fy + 14) e |= EDGE_T;
        if (y >= fy + fh - 14) e |= EDGE_B;
    }
    if (e & (EDGE_T | EDGE_B)) {
        if (x < fx + 14) e |= EDGE_L;
        if (x >= fx + fw - 14) e |= EDGE_R;
    }
    if (!e) *inside = (x >= fx && x < fx + fw && y >= fy && y < fy + fh);
    return e;
}

/* Topmost resizable window with an edge under (x, y). */
static desktop_window_t *resize_target_at(int32_t x, int32_t y, uint32_t *edges) {
    for (int zi = g_zcount - 1; zi >= 0; zi--) {
        desktop_window_t *w = window_by_id(g_zorder[zi]);
        if (!w || !w->visible || w->minimized) continue;
        wm_window_state_t *ws = wm_get_window_state(w->id);
        if (ws && ws->mode != WIN_MODE_NORMAL && ws->mode != WIN_MODE_MAXIMIZED) continue;
        int inside = 0;
        uint32_t e = resize_edges_at(w, x, y, &inside);
        if (e && w->no_resize) return NULL;     /* fixed-size window covers the point */
        if (e) {
            /* Corners and edges over the traffic lights stay with the lights */
            if (traffic_light_at(w, x, y) >= 0) return NULL;
            *edges = e;
            return w;
        }
        if (inside) return NULL;
    }
    return NULL;
}

static void gesture_update(desktop_window_t *w, int32_t mx, int32_t my) {
    int32_t dx = mx - g_ges.mx0, dy = my - g_ges.my0;
    int32_t sw = (int32_t)g_desktop.width, sh = (int32_t)g_desktop.height;
    if (g_ges.kind == GES_MOVE) {
        if (!g_ges.moved && dx < 3 && dx > -3 && dy < 3 && dy > -3) return;
        if (!g_ges.moved) {
            g_ges.moved = 1;
            /* Pulling a tiled/zoomed window away restores its own size,
             * keeping the pointer at the same relative spot on the title. */
            wm_window_state_t *ws = wm_get_window_state(w->id);
            if (ws && (ws->is_tiled || ws->is_maximized) && ws->pre_tile_w > 0) {
                float rel = (float)(g_ges.mx0 - w->x) / (float)(w->width ? w->width : 1u);
                window_untile(w, 0);
                g_ges.w0 = w->width;
                g_ges.h0 = w->height;
                g_ges.x0 = g_ges.mx0 - (int32_t)(rel * (float)w->width);
            }
        }
        int32_t nx = g_ges.x0 + dx, ny = g_ges.y0 + dy;
        if (ny < WM_MENUBAR_H) ny = WM_MENUBAR_H;              /* title bar stays below the menu bar */
        if (ny > sh - WIN_TITLE_H - 8) ny = sh - WIN_TITLE_H - 8;
        if (nx < 80 - (int32_t)w->width) nx = 80 - (int32_t)w->width;   /* keep 80 px on screen */
        if (nx > sw - 80) nx = sw - 80;
        w->x = nx;
        w->y = ny;
        g_ges.tile = wm_suggest_tile(mx, my, nx, ny);
        wm_set_tile_preview(g_ges.tile);
        return;
    }
    /* Resize: move the grabbed edges, honoring the minimum and the screen */
    int32_t L = g_ges.x0, T = g_ges.y0;
    int32_t R = g_ges.x0 + (int32_t)g_ges.w0, B = g_ges.y0 + (int32_t)g_ges.h0 + WIN_TITLE_H;
    const int32_t min_w = WM_MIN_W, min_h = WM_MIN_H + WIN_TITLE_H;
    if (g_ges.edges & EDGE_L) {
        L += dx;
        if (L > R - min_w) L = R - min_w;
        if (L < 0) L = 0;
    }
    if (g_ges.edges & EDGE_R) {
        R += dx;
        if (R < L + min_w) R = L + min_w;
        if (R > sw) R = sw;
    }
    if (g_ges.edges & EDGE_T) {
        T += dy;
        if (T > B - min_h) T = B - min_h;
        if (T < WM_MENUBAR_H) T = WM_MENUBAR_H;
    }
    if (g_ges.edges & EDGE_B) {
        B += dy;
        if (B < T + min_h) B = T + min_h;
        if (B > sh) B = sh;
    }
    if (R - L < 1 || B - T <= WIN_TITLE_H) return;
    w->x = L;
    w->y = T;
    w->width = (uint32_t)(R - L);
    w->height = (uint32_t)(B - T - WIN_TITLE_H);
    wm_window_content_changed(w->id);
}

static void gesture_finish(desktop_window_t *w) {
    if (w && g_ges.kind == GES_MOVE && g_ges.moved && g_ges.tile != TILE_NONE) {
        /* Remember the size the window had before this drag */
        wm_window_state_t *ws = wm_get_window_state(w->id);
        if (ws) {
            if (!ws->is_tiled && !ws->is_maximized) {
                ws->pre_tile_x = g_ges.x0;
                ws->pre_tile_y = g_ges.y0;
                ws->pre_tile_w = w->width;
                ws->pre_tile_h = w->height;
                ws->is_tiled = 1;   /* window_tile keeps an existing saved frame */
            }
            window_tile(w, g_ges.tile);
        }
    }
    if (w) w->dragging = 0;
    g_ges.kind = GES_NONE;
    g_ges.id = 0;
    g_ges.tile = TILE_NONE;
    wm_clear_tile_preview();
}

/* Continue a move/resize, or start a resize from an edge press.
 * Returns 1 when the event was used. */
static int window_gesture_mouse(uint8_t buttons, uint8_t prev_buttons) {
    int32_t mx = g_desktop.mouse_x, my = g_desktop.mouse_y;
    if (g_ges.kind != GES_NONE) {
        desktop_window_t *w = window_by_id(g_ges.id);
        if (!w || w->minimized || !w->visible) {
            gesture_finish(w);
            return 0;
        }
        if (buttons & 0x01) gesture_update(w, mx, my);
        else gesture_finish(w);
        return 1;
    }
    g_resize_hover = 0;
    if (g_mouse_capture_id != 0) return 0;          /* content drag in progress */
    uint32_t edges = 0;
    desktop_window_t *w = resize_target_at(mx, my, &edges);
    if (!w) return 0;
    g_resize_hover = edges;
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        desktop_focus_window(w->id);
        wm_window_state_t *ws = wm_get_window_state(w->id);
        if (ws) { ws->is_tiled = 0; ws->is_maximized = 0; ws->tile_pos = TILE_NONE; }
        win_anim_t *a = anim_of(w);
        if (a) a->geo = 0;
        g_ges.kind = GES_RESIZE;
        g_ges.id = w->id;
        g_ges.edges = edges;
        g_ges.mx0 = mx;
        g_ges.my0 = my;
        g_ges.x0 = w->x;
        g_ges.y0 = w->y;
        g_ges.w0 = w->width;
        g_ges.h0 = w->height;
        g_ges.moved = 1;
        serial_puts("[WM] resize start\n");
        return 1;
    }
    return 0;
}

/* Mouse smoothing state - exponential moving average */
static int32_t g_smooth_mouse_x = 0;
static int32_t g_smooth_mouse_y = 0;
static int g_mouse_smooth_init = 0;
#define MOUSE_SMOOTH_FACTOR 1  /* Higher = smoother but slower (1-4 range, 1 is fastest) */

void desktop_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    
    /* Calculate target position */
    int32_t target_x = g_desktop.mouse_x + dx;
    int32_t target_y = g_desktop.mouse_y + dy;
    
    /* Clamp target to screen */
    if (target_x < 0) target_x = 0;
    if (target_y < 0) target_y = 0;
    if (target_x >= (int32_t)g_desktop.width) target_x = g_desktop.width - 1;
    if (target_y >= (int32_t)g_desktop.height) target_y = g_desktop.height - 1;
    
    /* Pointer tracks hardware deltas 1:1 (the old half-step smoothing
     * halved pointer speed because deltas are relative). */
    g_desktop.mouse_x = target_x;
    g_desktop.mouse_y = target_y;
    
    uint8_t prev_buttons = g_desktop.mouse_buttons;
    g_desktop.mouse_buttons = buttons;
    
    /* First-run Setup Assistant owns all input while active */
    if (setup_assistant_active()) {
        setup_assistant_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons);
        return;
    }

    /* If App Drawer is open, forward mouse to it first */
    if (app_drawer_is_open()) {
        app_drawer_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons);
        return;  /* Drawer consumes all mouse events when open */
    }

    /* App switcher HUD and Overview own the pointer while shown */
    if (switcher_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;
    }
    if (overview_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;
    }
    
    /* Menu bar menus take the pointer while open or when a title is hit */
    if (menubar_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;
    }
    /* Anveshan search field and its results */
    if (anveshan_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;
    }
    
    /* Handle Control Center input */
    if (control_center_handle_input(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;  /* Control Center consumed the event */
    }
    
    /* Handle Notification Center input */
    if (notification_center_handle_input(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;  /* Notification Center consumed the event */
    }
    
    /* Check for navbar icon clicks - top-right area */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        if (g_desktop.mouse_y < 28) {  /* Menu bar height */
            /* Grid icon (Control Center toggle) - rightmost */
            if (g_desktop.mouse_x >= (int32_t)g_desktop.width - 50 &&
                g_desktop.mouse_x <= (int32_t)g_desktop.width - 20) {
                control_center_toggle();
                notification_center_hide();  /* Close NC when opening CC */
                return;
            }
            /* Bell icon (Notification Center toggle) - left of grid */
            if (g_desktop.mouse_x >= (int32_t)g_desktop.width - 80 &&
                g_desktop.mouse_x <= (int32_t)g_desktop.width - 55) {
                notification_center_toggle();
                control_center_hide();  /* Close CC when opening NC */
                return;
            }
        }
    }
    
    /* Check dock clicks FIRST - dock is above most things */
    if (dock_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;  /* Dock consumed the click */
    }

    /* Stage strip (thumbnails, drags onto the stage) */
    if (stage_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons, prev_buttons)) {
        return;
    }

    /* Window move/resize in progress, or a press on a resize edge */
    if (window_gesture_mouse(buttons, prev_buttons)) {
        return;
    }

    /* Shell-hosted app content (Settings, Task Manager, ...) */
    if (route_content_mouse(buttons, prev_buttons)) {
        return;
    }
    
    /* Update quick menu hover state */
    wm_quick_menu_hit_test(g_desktop.mouse_x, g_desktop.mouse_y);

    int32_t mx = g_desktop.mouse_x, my = g_desktop.mouse_y;

    /* Traffic light released: clear the pressed tint */
    if (!(buttons & 0x01) && g_light_press_id) {
        g_light_press_id = 0;
        g_light_press_idx = -1;
    }

    /* Handle left click */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Check quick menu click first */
        wm_quick_menu_t *qm = wm_get_quick_menu();
        if (qm && qm->visible) {
            int hit = wm_quick_menu_hit_test(mx, my);
            if (hit >= 0) {
                wm_quick_menu_click(mx, my);
                return;
            } else {
                /* Click outside menu - hide it */
                wm_hide_quick_menu();
            }
        }

        /* Collapsed sidebar tabs sit outside their window frame */
        for (int zi = g_zcount - 1; zi >= 0; zi--) {
            desktop_window_t *win = window_by_id(g_zorder[zi]);
            if (!win || !win->visible || win->minimized) continue;
            wm_window_state_t *wstate = wm_get_window_state(win->id);
            if (!wstate || wstate->mode != WIN_MODE_SIDEBAR) continue;
            if (!wstate->sidebar_expanded) {
                int tab_x = (int)g_desktop.width - 40;
                if (mx >= tab_x && mx < tab_x + 36 && my >= win->y && my < win->y + 80) {
                    wm_toggle_sidebar_expand(win->id);
                    return;
                }
            } else if (mx < win->x || mx >= win->x + (int32_t)win->width ||
                       my < win->y || my >= win->y + (int32_t)win->height + WIN_TITLE_H) {
                wm_toggle_sidebar_expand(win->id);   /* click outside collapses it */
            }
        }

        /* Topmost window under the pointer */
        desktop_window_t *win = window_at_point(mx, my);
        if (win) {
            wm_window_state_t *wstate = wm_get_window_state(win->id);
            if (wstate && wstate->mode == WIN_MODE_SIDEBAR && !wstate->sidebar_expanded) return;

            /* Traffic lights work on inactive windows too */
            int light = traffic_light_at(win, mx, my);
            if (light >= 0) {
                g_light_press_id = win->id;
                g_light_press_idx = light;
                if (light == 0) window_close(win);
                else if (light == 1) window_minimize(win);
                else { desktop_focus_window(win->id); window_zoom(win); }
                return;
            }

            desktop_focus_window(win->id);

            int in_title = my < win->y + WIN_TITLE_H;
            int pip = wstate && wstate->mode == WIN_MODE_PIP;
            if (in_title || pip) {
                /* Double-click on the title bar zooms */
                static uint32_t last_id = 0;
                static uint64_t last_ms = 0;
                static int32_t last_x = 0, last_y = 0;
                uint64_t now = nx_gettime();
                int dbl = in_title && last_id == win->id && now - last_ms < 400 &&
                          mx - last_x < 5 && last_x - mx < 5 && my - last_y < 5 && last_y - my < 5;
                last_id = win->id;
                last_ms = now;
                last_x = mx;
                last_y = my;
                if (dbl) {
                    last_id = 0;
                    window_zoom(win);
                    return;
                }
                /* Start a move; it begins once the pointer travels 3 px */
                win_anim_t *a = anim_of(win);
                if (a) a->geo = 0;
                g_ges.kind = GES_MOVE;
                g_ges.id = win->id;
                g_ges.edges = 0;
                g_ges.mx0 = mx;
                g_ges.my0 = my;
                g_ges.x0 = win->x;
                g_ges.y0 = win->y;
                g_ges.w0 = win->width;
                g_ges.h0 = win->height;
                g_ges.moved = 0;
                g_ges.tile = TILE_NONE;
                win->dragging = 1;
            }
        }
    }

    /* Desktop items (files in ~/Desktop, served by the Path relay) */
    if ((buttons & 0x01) && !(prev_buttons & 0x01) && !window_at_point(mx, my))
        desktop_items_click(mx, my, (int32_t)g_desktop.width, (int32_t)g_desktop.height);

    /* Handle right click - show quick menu on title bar */
    if ((buttons & 0x02) && !(prev_buttons & 0x02)) {
        desktop_window_t *win = window_at_point(mx, my);
        if (win && my < win->y + WIN_TITLE_H) {
            /* Show quick menu centered below the title bar */
            int menu_w = 3 * 40 + 16;
            int menu_x = win->x + ((int32_t)win->width - menu_w) / 2;
            int menu_y = win->y + WIN_TITLE_H + 4;
            wm_show_quick_menu(win->id, menu_x, menu_y);
        }
    }
}

/* ============ NX Key Long-Press Tracking (File Scope) ============ */

static uint64_t g_nx_key_press_time = 0;
static int g_nx_key_held = 0;     /* NX down and still a tap/long-press candidate */
static int g_nx_help_shown = 0;
static int g_nx_down = 0;         /* NX physically held (chords use this) */
static int g_shift_down = 0;      /* either Shift held */
static int g_ctrl_down = 0;       /* either Ctrl held */
static int g_alt_down = 0;        /* either Alt held */

uint8_t desktop_key_modifiers(void) {
    return (uint8_t)((g_shift_down ? 0x1u : 0u) | (g_ctrl_down ? 0x2u : 0u) |
                     (g_alt_down ? 0x4u : 0u) | (g_nx_down ? 0x8u : 0u));
}
#define NX_LONGPRESS_MS 500  /* 500ms threshold */

/* Called every frame to check if NX key was held long enough */
void nx_key_tick(void) {
    if (g_nx_key_held && !g_nx_help_shown) {
        uint64_t held_time = nx_gettime() - g_nx_key_press_time;
        if (held_time >= NX_LONGPRESS_MS) {
            keyboard_osd_help_show();
            g_nx_help_shown = 1;
            serial_puts("[KBD] NX key held 500ms, showing help overlay\n");
        }
    }
}

int desktop_launch_app(const char *name);   /* defined below */
static void render_empty_content(desktop_window_t *win, void *ctx);

/* Shell-side handling of app launches (dock, app drawer, search). Apps
 * without a registered window get a cascaded window with the empty state
 * until a process launcher exists. Re-launching focuses the open window. */
static int shell_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void desktop_on_app_launched(const nx_event_t *event, void *userdata) {
    (void)userdata;
    if (!event || !event->data || event->data_size < sizeof(nx_app_data_t)) return;
    const nx_app_data_t *app = (const nx_app_data_t *)event->data;
    const char *name = app->app_name;
    if (!name[0]) return;
    /* Dock labels map to registered startup apps where one exists */
    if (shell_streq_ci(name, "Files") || shell_streq_ci(name, "File Manager")) name = "Path";
    else if (shell_streq_ci(name, "Settings")) name = "System Settings";
    else if (shell_streq_ci(name, "Fonts")) name = "Font Manager";
    else if (shell_streq_ci(name, "Activity") || shell_streq_ci(name, "Tasks")) name = "Task Manager";
    else if (shell_streq_ci(name, "Terminal") || shell_streq_ci(name, "Console")) name = "Shell";
    /* Already open: focus and restore */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && shell_streq_ci(g_windows[i].title, name)) {
            /* Restore (animated), switch space / Stage group, focus */
            window_activate(&g_windows[i]);
            dock_set_running(app->app_id, 1);
            return;
        }
    }
    if (desktop_launch_app(name) != 0) {
        /* No registered window: cascade a generic one */
        static int cascade = 0;
        int32_t x = 180 + (cascade % 6) * 32, y = 110 + (cascade % 6) * 28;
        cascade++;
        uint32_t id = desktop_create_window(app->app_name, x, y, 640, 440);
        if (id) desktop_set_window_render(id, render_empty_content, NULL);
    }
    dock_set_running(app->app_id, 1);
}


/* ASCII of the key being dispatched (set by the event loop before desktop_handle_key). */
static char g_key_ascii = 0;

char desktop_key_ascii(void) { return g_key_ascii; }

/* Menu bar actions (menubar.c publishes them; the shell decides). */
static desktop_window_t *focused_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && g_windows[i].visible && g_windows[i].focused) return &g_windows[i];
    }
    return NULL;
}

static void desktop_menu_action(int menu, int action) {
    (void)menu;
    desktop_window_t *fw = focused_window();
    switch (action) {
        case 1:  /* New Window: a Path window */
            if (desktop_launch_app("Path") != 0) serial_puts("[DESKTOP] Path: focused the open window\n");
            break;
        case 4: case 5:  /* Close Window, Quit */
            if (fw) window_close(fw);
            break;
        case 15: /* Toggle Appearance */
            nxui_set_appearance(nxui_appearance() == NXUI_APPEARANCE_DARK ? NXUI_APPEARANCE_LIGHT : NXUI_APPEARANCE_DARK);
            theme_osd_show_dark_mode(nxui_appearance() == NXUI_APPEARANCE_DARK);
            break;
        case 17: /* Minimize */
            if (fw) window_minimize(fw);
            break;
        case 18: /* Zoom */
            if (fw) window_zoom(fw);
            break;
        case 22: /* Keyboard Shortcuts */
            keyboard_osd_help_show();
            break;
        case 23: /* About This System -> Settings */
            if (settings_view_show_pane("about") != 0) serial_puts("[DESKTOP] About This System failed to open\n");
            break;
        default:
            serial_puts("[MENU] action not wired yet\n");
            break;
    }
}

#ifdef DESKTOP_MEM_SELFTEST
/*
 * Test build only (EXTRA_CFLAGS=-DDESKTOP_MEM_SELFTEST): F12 makes the
 * desktop take up to DESKTOP_MEM_SELFTEST_MB of heap, 1 MiB at a time,
 * touching every page, then give it all back. Used in QEMU to run the
 * desktop short of memory and check K2 part 3 (the kernel ends the largest
 * background app). Never built into release images.
 */
#define DESKTOP_MEM_SELFTEST_MB 32

static void dmst_log(const char *what, uint64_t v, const char *tail) {
    char buf[128];
    int pos = 0;
    while (*what && pos < 100) buf[pos++] = *what++;
    char d[21];
    int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 20);
    while (n > 0 && pos < 120) buf[pos++] = d[--n];
    while (*tail && pos < 126) buf[pos++] = *tail++;
    buf[pos] = '\0';
    nx_debug_print(buf);
}

static void desktop_mem_selftest(void) {
    void *chunks[DESKTOP_MEM_SELFTEST_MB];
    nx_mem_stats_t st;
    st.free_bytes = 0;
    int got = 0;
    if (nx_mem_stats(&st) == 0) dmst_log("[DESKTOP] mem selftest: start, free MB ", st.free_bytes >> 20, "\n");
    for (int i = 0; i < DESKTOP_MEM_SELFTEST_MB; i++) {
        uint8_t *p = (uint8_t *)nx_malloc(1024 * 1024);
        if (!p) break;
        for (uint32_t off = 0; off < 1024 * 1024; off += 4096) p[off] = (uint8_t)(i + 1);
        chunks[got++] = p;
    }
    int bad = 0;
    for (int i = 0; i < got; i++) {
        const uint8_t *p = (const uint8_t *)chunks[i];
        for (uint32_t off = 0; off < 1024 * 1024; off += 4096) if (p[off] != (uint8_t)(i + 1)) bad = 1;
    }
    if (nx_mem_stats(&st) == 0) dmst_log("[DESKTOP] mem selftest: holding MB ", (uint64_t)got, "");
    dmst_log(", free MB ", st.free_bytes >> 20, bad ? ", FAIL: content mismatch\n" : "\n");
    for (int i = got - 1; i >= 0; i--) nx_mfree(chunks[i]);
    uint64_t back = nx_malloc_trim();
    dmst_log("[DESKTOP] mem selftest: released, trim returned KB ", back / 1024, "\n");
}
#endif

void desktop_handle_key(uint8_t scancode, uint8_t pressed) {
#ifdef DESKTOP_MEM_SELFTEST
    if (scancode == 0x58 && pressed) {         /* F12 */
        desktop_mem_selftest();
        return;
    }
#endif
    /* First-run Setup Assistant owns all input while active */
    if (setup_assistant_active()) {
        setup_assistant_handle_key(scancode, pressed, g_key_ascii);
        return;
    }
    /* Modifier state for window shortcuts */
    if (scancode == 0x2A || scancode == 0x36) g_shift_down = pressed ? 1 : 0;
    if (scancode == 0x1D) g_ctrl_down = pressed ? 1 : 0;
    if (scancode == 0x38) g_alt_down = pressed ? 1 : 0;
    /* Ctrl+Shift+Esc opens (or focuses) Task Manager */
    if (scancode == 0x01 && pressed && g_ctrl_down && g_shift_down) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].id != 0 && shell_streq_ci(g_windows[i].title, "Task Manager")) {
                desktop_activate_window(g_windows[i].id);
                return;
            }
        }
        if (desktop_launch_app("Task Manager") != 0) serial_puts("[DESKTOP] Task Manager failed to open\n");
        return;
    }
    /* Switcher: Esc cancels while it is open */
    if (switcher_open_now() && scancode == 0x01) {
        if (pressed) switcher_cancel();
        return;
    }
    /* Overview takes the keyboard (Esc/Return exit) except NX chords */
    if (overview_active() && !g_nx_down && scancode != 0x5B && scancode != 0x5C) {
        overview_handle_key(scancode, pressed);
        return;
    }
    /* Open menu or active search field take the keyboard first */
    if (menubar_handle_key(scancode, pressed)) return;
    if (dock_handle_key(scancode, pressed)) return;
    if (anveshan_handle_key(scancode, pressed, g_key_ascii)) return;
    /* NX + Space opens the search field */
    if (scancode == 0x39 && pressed && g_nx_key_held) {
        anveshan_get_state()->active = 1;
        g_nx_help_shown = 0;
        g_nx_key_held = 0;   /* release of NX must not open the drawer */
        return;
    }
    /* Track Caps Lock state */
    static int caps_lock_on = 0;
    
    /* Caps Lock key (scancode 0x3A) - toggle and show OSD */
    if (scancode == 0x3A && pressed) {
        caps_lock_on = !caps_lock_on;
        keyboard_osd_caps_lock(caps_lock_on);
        return;
    }
    
    /* Num Lock key (scancode 0x45) - toggle and show OSD */
    static int num_lock_on = 1;  /* Usually starts ON */
    if (scancode == 0x45 && pressed) {
        num_lock_on = !num_lock_on;
        keyboard_osd_num_lock(num_lock_on);
        return;
    }
    
    /* NX key (left: 0x5B, right: 0x5C) - quick tap for App Drawer, long-press for help */
    if (scancode == 0x5B || scancode == 0x5C) {
        if (pressed) {
            /* Key pressed - start tracking for long-press (auto-repeat
             * presses while held do not restart it) */
            if (!g_nx_down) {
                g_nx_key_press_time = nx_gettime();
                g_nx_key_held = 1;
                g_nx_help_shown = 0;
            }
            g_nx_down = 1;
        } else {
            /* Key released */
            g_nx_down = 0;
            if (switcher_open_now()) {
                /* NX+Tab: releasing NX activates the selected app */
                switcher_commit();
            } else if (g_nx_help_shown) {
                /* Was showing help overlay - just hide it */
                keyboard_osd_help_hide();
                serial_puts("[KBD] NX key released, hiding help overlay\n");
            } else if (g_nx_key_held) {
                /* Quick tap - toggle App Drawer */
                app_drawer_toggle();
            }
            g_nx_key_held = 0;
            g_nx_help_shown = 0;
        }
        return;
    }
    
    /* If App Drawer is open, forward to it first */
    if (app_drawer_is_open()) {
        if (app_drawer_handle_key(scancode, pressed)) {
            return;  /* Event consumed by drawer */
        }
    }
    
    /* NX chords: window management and multitasking (see
     * docs/desktop/multitasking.md for the full list). A chord cancels the
     * NX tap (App Drawer) and the long-press help. */
    if (g_nx_down && pressed) {
        desktop_window_t *fw = NULL;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].id != 0 && g_windows[i].visible &&
                g_windows[i].focused && !g_windows[i].minimized) {
                fw = &g_windows[i];
                break;
            }
        }
        int used = 1;
        switch (scancode) {
            case 0x0F:  /* Tab: app switcher (Shift+Tab goes back) */
                if (switcher_open_now()) switcher_step(g_shift_down);
                else {
                    if (overview_active()) overview_toggle();   /* switching apps leaves Overview */
                    switcher_open(g_shift_down);
                }
                break;
            case 0x48:  /* Up: Overview; Shift+Up zooms the window */
                if (g_shift_down) { if (fw) window_zoom(fw); }
                else overview_toggle();
                break;
            case 0x4B:  /* Left half */
                if (fw) window_tile(fw, TILE_LEFT_HALF);
                break;
            case 0x4D:  /* Right half */
                if (fw) window_tile(fw, TILE_RIGHT_HALF);
                break;
            case 0x50:  /* Down: back to the window's own frame */
                if (fw) window_untile(fw, 1);
                break;
            case 0x32:  /* M: minimize */
                if (fw) window_minimize(fw);
                break;
            case 0x11:  /* W: close */
                if (fw) window_close(fw);
                break;
            case 0x1F:  /* S: Stage */
                stage_toggle();
                break;
            case 0x29: { /* `: next window of the same app */
                if (!fw) break;
                for (int zi = 0; zi < g_zcount; zi++) {   /* bottom-most first */
                    desktop_window_t *o = window_by_id(g_zorder[zi]);
                    if (!o || o == fw || o->minimized) continue;
                    if (wm_window_space(o->id) != wm_space_current()) continue;
                    if (!shell_streq_ci(o->title, fw->title)) continue;
                    window_activate(o);
                    break;
                }
                break;
            }
            default:
                used = 0;
                break;
        }
        if (used) {
            g_nx_key_held = 0;
            g_nx_help_shown = 0;
            return;
        }
    }

    /* Forward to focused window */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        desktop_window_t *win = &g_windows[i];
        if (win->id != 0 && win->visible && win->focused && 
            !win->minimized && win->key_handler) {
            win->key_handler(win, scancode, pressed, win->key_ctx);
            return;
        }
    }
}

/* ============ Main Entry Points ============ */

int desktop_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    serial_puts("\n[DESKTOP] Initializing desktop shell (userspace)...\n");
    
    /* For userspace mode, use syscalls to get framebuffer */
    /* If parameters are 0, use syscall to get FB info */
    if (fb_addr == 0 || width == 0) {
        /* Initialize config system first */
        nx_config_init();
        
        /* NXUI maps the framebuffer, owns the double-buffered frame
         * surface and initializes the NXCanvas engine, fonts and icons. */
        nxc_status_t ui_st = nxui_init();
        if (ui_st != NXC_OK) {
            serial_puts("[DESKTOP] NXUI init failed: ");
            serial_puts(nxc_status_str(ui_st));
            serial_puts("\n");
            return -1;
        }
        nxc_canvas_t *frame = nxui_canvas();
        /* Legacy pixel code renders into the same backbuffer (linear stride). */
        g_desktop.framebuffer = frame->target->pixels;
        g_desktop.width = (uint32_t)frame->target->width;
        g_desktop.height = (uint32_t)frame->target->height;
        g_desktop.pitch = (uint32_t)frame->target->stride * 4u;
    } else {
        /* Fallback: use passed parameters (kernel mode, no double buffering) */
        g_desktop.framebuffer = (uint32_t *)fb_addr;
        g_desktop.width = width;
        g_desktop.height = height;
        g_desktop.pitch = pitch;
    }
    
    /* Back buffer is now the render target */
    nx_debug_print("[DESKTOP] FB init success, setting up mouse...\n");
    g_fb_initialized = 1;
    
    g_desktop.mouse_x = g_desktop.width / 2;
    g_desktop.mouse_y = g_desktop.height / 2;
    g_desktop.mouse_buttons = 0;
    g_desktop.menubar_visible = 1;
    g_desktop.dock_visible = 1;
    g_desktop.window_count = 0;
    g_desktop.running = 1;
    
    /* Clear windows */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].id = 0;
    }
    
    /* Menu bar menus and the Anveshan search field */
    menubar_init();
    network_indicator_init();
    menubar_set_handler(desktop_menu_action);
    anveshan_init(g_desktop.width, g_desktop.height);
    
    nx_debug_print("[DESKTOP] Calling WM init...\n");
    
    /* Initialize Window Manager */
    wm_init(g_desktop.width, g_desktop.height);
    wm_snapshot_set_renderer(shell_snapshot_renderer);
    g_zcount = 0;
    g_ges.kind = GES_NONE;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_wanim[i].geo = 0;
        g_wanim[i].phase = WA_NONE;
    }
    yantra_grid_init(g_desktop.width, g_desktop.height);
    nx_debug_print("[DESKTOP] WM + grid initialized, booting to clean desktop...\n");
    
    /* NOTE: No auto terminal window - user launches apps manually from dock */
    
    /* Initialize Event Bus */
    nxevent_init();
    /* Launch requests from the dock, drawer and search open windows here */
    nxevent_subscribe(NX_EVENT_APP_LAUNCHED, desktop_on_app_launched, NULL);
    nx_debug_print("[DESKTOP] Event bus done, dock init...\n");
    
    /* Initialize Dock */
    dock_init(g_desktop.width, g_desktop.height);
    dock_set_hide_handler(desktop_dock_hide_window);
    nx_debug_print("[DESKTOP] Dock done, control center init...\n");
    
    /* Initialize Control Center */
    control_center_init(g_desktop.width, g_desktop.height);
    nx_debug_print("[DESKTOP] Control center done, notification center init...\n");
    
    /* Initialize Notification Center */
    notification_center_init(g_desktop.width, g_desktop.height);
    nx_debug_print("[DESKTOP] Notification center done, app drawer init...\n");
    
    /* Initialize App Drawer */
    app_drawer_init(g_desktop.width, g_desktop.height);
    
    /* Initialize Wallpaper (loads image via libnximage if available) */
    wallpaper_init(g_desktop.width, g_desktop.height);
    
    /* Initialize OSD components */
    volume_osd_init(g_desktop.width, g_desktop.height);
    keyboard_osd_init(g_desktop.width, g_desktop.height);
    theme_osd_init(g_desktop.width, g_desktop.height);
    battery_warning_init(g_desktop.width, g_desktop.height);
    nx_debug_print("[DESKTOP] OSD components initialized\n");

    /* First-run Setup Assistant (active only until setup completes) */
    setup_assistant_init();

    /* Window server: apps open windows through port "nx.display". A failure
     * is logged by nxserver_init; the shell keeps running without it. */
    nxserver_init(g_desktop.width, g_desktop.height);
    
    nx_debug_print("[DESKTOP] All init done! Starting main loop...\n");
    
    /* Call desktop_run directly instead of returning to avoid stack corruption issue */
    extern void desktop_run(void);
    desktop_run();
    
    /* Should never reach here */
    return 0;
}

/* ============ Startup Apps Framework ============ */

/* Startup app entry */
typedef struct {
    const char *name;
    const char *path;
    int auto_start;
    int create_window;
    int32_t win_x, win_y;
    uint32_t win_w, win_h;
} startup_app_t;

/* Registered startup apps */
static startup_app_t g_startup_apps[] = {
    {"Font Manager", "/apps/fontmanager.sys", 0, 1, 400, 150, 350, 280},
    {"System Settings", "/apps/settings.sys", 0, 1, 200, 100, 400, 350},
    {"File Manager", "/apps/files.sys", 0, 1, 150, 80, 500, 400},
    {NULL, NULL, 0, 0, 0, 0, 0, 0}
};

/* Font Manager window content renderer */
static void render_fontmanager_content(desktop_window_t *win, void *ctx) {
    (void)ctx;
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    float x = (float)win->x + 16.0f, y = (float)win->y + 24.0f + 14.0f;
    nxui_text(c, NXUI_TEXT_HEADLINE, x, y + 12.0f, "Installed typefaces", th->text_primary, NULL);
    static const char *const fonts[] = { "NXSans Text", "NXSans UI", "NXSans Bold", "NXSans Display", "NXSans Mono" };
    static const nxui_text_style_t styles[] = { NXUI_TEXT_BODY, NXUI_TEXT_BODY_MEDIUM, NXUI_TEXT_HEADLINE, NXUI_TEXT_TITLE3, NXUI_TEXT_MONO };
    y += 34.0f;
    for (int i = 0; i < 5; i++) {
        nxui_text(c, styles[i], x, y + 12.0f, fonts[i], th->text_primary, NULL);
        nxui_text(c, NXUI_TEXT_CAPTION, x + 150.0f, y + 12.0f, "Hamburgefonstiv 0123", th->text_secondary, NULL);
        y += 26.0f;
    }
    y += 8.0f;
    nxui_text(c, NXUI_TEXT_FOOTNOTE, x, y + 12.0f, "System font: NXSans UI 13 pt", th->success, NULL);
}

/* Windows without a content renderer show a quiet empty state. */
static void render_empty_content(desktop_window_t *win, void *ctx) {
    (void)ctx;
    nxc_canvas_t *c = nxui_canvas();
    const nxui_theme_t *th = nxui_theme();
    float cx = (float)win->x + (float)win->width * 0.5f, cy = (float)win->y + 24.0f + (float)win->height * 0.5f;
    nxapp_icon_draw(c, nxapp_icon_from_name(win->title), cx - 32.0f, cy - 48.0f, 64.0f, 110);
    nxui_text_in_rect(c, NXUI_TEXT_BODY, nxc_rect(cx - 150.0f, cy + 24.0f, 300.0f, 20.0f), win->title, th->text_secondary, NXC_ALIGN_CENTER);
    nxui_text_in_rect(c, NXUI_TEXT_CAPTION, nxc_rect(cx - 150.0f, cy + 44.0f, 300.0f, 18.0f), "Nothing to show yet", th->text_tertiary, NXC_ALIGN_CENTER);
}

/* Start registered startup applications */
void desktop_start_apps(void) {
    serial_puts("[DESKTOP] Starting startup applications...\n");
    
    for (int i = 0; g_startup_apps[i].name != NULL; i++) {
        startup_app_t *app = &g_startup_apps[i];
        
        if (!app->auto_start) {
            serial_puts("[DESKTOP] Skip (not auto-start): ");
            serial_puts(app->name);
            serial_puts("\n");
            continue;
        }
        
        serial_puts("[DESKTOP] Starting: ");
        serial_puts(app->name);
        serial_puts("\n");
        
        if (app->create_window) {
            uint32_t win_id = desktop_create_window(
                app->name, 
                app->win_x, app->win_y,
                app->win_w, app->win_h
            );
            
            if (win_id != 0) {
                serial_puts("[DESKTOP] Created window for ");
                serial_puts(app->name);
                serial_puts("\n");
            }
        }
    }
    
    serial_puts("[DESKTOP] Startup apps complete\n");
}

/* Launch a specific app by name */
int desktop_launch_app(const char *name) {
    /* Shell-hosted app views (app_view.h) */
    const app_view_t *view = app_view_find(name);
    if (view) {
        uint32_t w = view->default_w, h = view->default_h;
        if (w > g_desktop.width - 40) w = g_desktop.width - 40;
        if (h > g_desktop.height - 160) h = g_desktop.height - 160;
        int32_t x = ((int32_t)g_desktop.width - (int32_t)w) / 2;
        int32_t y = 28 + ((int32_t)g_desktop.height - 28 - 90 - (int32_t)h - WIN_TITLE_H) / 2;
        if (y < 36) y = 36;
        uint32_t win_id = desktop_create_window(view->name, x, y, w, h);
        if (win_id == 0) return -1;
        if (view->open && view->open(win_id) != 0) {
            serial_puts("[DESKTOP] App view failed to open: ");
            serial_puts(view->name);
            serial_puts("\n");
            desktop_destroy_window(win_id);
            return -1;
        }
        desktop_set_window_render(win_id, view->render, NULL);
        if (view->key) desktop_set_window_key_handler(win_id, view->key, NULL);
        if (view->mouse) desktop_set_window_mouse_handler(win_id, view->mouse, NULL);
        if (view->scroll) desktop_set_window_scroll_handler(win_id, view->scroll, NULL);
        return 0;
    }
    for (int i = 0; g_startup_apps[i].name != NULL; i++) {
        startup_app_t *app = &g_startup_apps[i];
        
        /* Simple string compare */
        const char *a = name;
        const char *b = app->name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (*a || *b) match = 0;
        
        if (match && app->create_window) {
            uint32_t win_id = desktop_create_window(
                app->name,
                app->win_x, app->win_y,
                app->win_w, app->win_h
            );
            
            /* Set app-specific renderer */
            if (win_id != 0) {
                /* Check if Font Manager to set custom renderer */
                if (app->name[0] == 'F' && app->name[1] == 'o') {
                    desktop_set_window_render(win_id, render_fontmanager_content, NULL);
                } else {
                    desktop_set_window_render(win_id, render_empty_content, NULL);
                }
                return 0;
            }
            return -1;
        }
    }
    return -2;  /* App not found */
}

void desktop_run(void) {
    serial_puts("[DESKTOP] Starting desktop event loop\n");
    
    /* Initial render */
    serial_puts("[DR] About to call render_desktop\n");
    render_desktop_tick(1);
    serial_puts("[DESKTOP] Initial render complete\n");
    
    uint32_t input_count = 0;
    int had_input = 0;              /* input since the last frame tick */
    uint64_t next_tick = pit_get_ticks() + FRAME_TICK_MS;
    
    while (g_desktop.running) {
        /* Poll ALL input events */
        if (g_pf_loop_t) g_pf[PF_SLEEP] += rdtsc_now() - g_pf_loop_t;
        uint64_t pf_in0 = rdtsc_now();
        input_event_t event;
        
        while (nx_input_poll(&event) > 0) {
            had_input = 1;
            input_count++;
            
            if (event.type == INPUT_TYPE_MOUSE_MOVE || event.type == INPUT_TYPE_MOUSE_BUTTON) {
                /* Mouse event */
                desktop_handle_mouse(event.mouse.dx, event.mouse.dy, event.mouse.buttons);
                if (event.mouse.wheel != 0) desktop_handle_scroll(event.mouse.wheel);
                
                /* Debug: log first 10 mouse events */
                if (input_count <= 10) {
                    serial_puts("[DESKTOP] Mouse: dx=");
                    serial_dec(event.mouse.dx < 0 ? (uint32_t)(-event.mouse.dx) : (uint32_t)event.mouse.dx);
                    serial_puts(" dy=");
                    serial_dec(event.mouse.dy < 0 ? (uint32_t)(-event.mouse.dy) : (uint32_t)event.mouse.dy);
                    serial_puts(" pos=");
                    serial_dec(g_desktop.mouse_x);
                    serial_puts(",");
                    serial_dec(g_desktop.mouse_y);
                    serial_puts("\n");
                }
            } else if (event.type == INPUT_TYPE_KEYBOARD) {
                /* Keyboard event */
                g_key_ascii = (char)event.keyboard.ascii;
                desktop_handle_key(event.keyboard.scancode, event.keyboard.pressed);
            }
        }
        
        /* Process pending events from event bus */
        nxevent_process();
        
        /* Check NX key long-press for help overlay */
        nx_key_tick();

        /* App windows: requests (commits add damage), CONFIGURE, pointer
         * motion (never blocks) */
        nxserver_poll();
        
        g_pf[PF_INPUT] += rdtsc_now() - pf_in0;
        
        /* Frame tick: draw what changed (or nothing), then FRAME_DONE */
        uint64_t now = pit_get_ticks();
        if (now >= next_tick) {
            render_desktop_tick(had_input);
            had_input = 0;
            nxserver_frame_presented();
            /* Steady 16 ms ticks; after a slow frame, restart from now
             * instead of drawing a burst of late frames. */
            next_tick += FRAME_TICK_MS;
            now = pit_get_ticks();
            if (next_tick <= now) next_tick = now + 1;
        }
        g_pf_loop_t = rdtsc_now();
        
        /* Wait for the next tick; a client message wakes the loop at once
         * so requests are answered without waiting for the tick. Input
         * devices are polled, so the wait never exceeds one tick. */
        now = pit_get_ticks();
        uint32_t wait_ms = next_tick > now ? (uint32_t)(next_tick - now) : 0;
        if (wait_ms > FRAME_TICK_MS) wait_ms = FRAME_TICK_MS;
        if (wait_ms > 0 && nxserver_wait(wait_ms) < 0) nx_sleep(wait_ms);
    }
    
    /* This should NEVER be reached */
    serial_puts("[DESKTOP] BUG: Event loop exited!\n");
    while(1) { __asm__ volatile("pause"); }
}

void desktop_shutdown(void) {
    g_desktop.running = 0;
    
    /* Destroy all windows */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0) {
            desktop_destroy_window(g_windows[i].id);
        }
    }
    
    serial_puts("[DESKTOP] Desktop shell shutdown\n");
}
