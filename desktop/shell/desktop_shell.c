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
#include "../include/vm_detect.h"
#include "../include/volume_osd.h"
#include "../include/keyboard_osd.h"
#include "../include/theme_osd.h"
#include "../include/battery_warning.h"
#include "../include/network_indicator.h"

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
        nx_debug_print("[DESKTOP] NXRender init failed - using fallback\n");
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

static int get_cached_rtc_time(rtc_time_t *out) {
    uint64_t now_ms = nx_gettime();
    
    /* Update cache if invalid or 1 second has passed */
    if (!g_rtc_cache_valid || (now_ms - g_rtc_cache_last_update >= 1000)) {
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
        /* Fallback: Simple gradient wallpaper */
        for (uint32_t y = 0; y < g_desktop.height; y++) {
            uint32_t t = (y * 256) / g_desktop.height;
            
            /* Top: Deep navy, Bottom: Dark purple */
            uint32_t r = (0x0A * (256 - t) + 0x1A * t) >> 8;
            uint32_t g = (0x0B * (256 - t) + 0x0F * t) >> 8;
            uint32_t b = (0x1A * (256 - t) + 0x20 * t) >> 8;
            
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            
            for (uint32_t x = 0; x < g_desktop.width; x++) {
                g_desktop.framebuffer[y * (g_desktop.pitch / 4) + x] = color;
            }
        }
    }
}

/* Navigation Menu Bar - top of screen */
static void render_navigation_bar(void) {
    int bar_h = g_nx_config.ui.navbar_height;  /* From config, not hardcoded */
    
    /* Translucent glass bar background */
    for (uint32_t y = 0; y < (uint32_t)bar_h; y++) {
        for (uint32_t x = 0; x < g_desktop.width; x++) {
            uint32_t idx = y * (g_desktop.pitch / 4) + x;
            uint32_t bg = g_desktop.framebuffer[idx];
            /* Blend with semi-transparent dark overlay */
            uint8_t br = ((bg >> 16) & 0xFF);
            uint8_t bgg = ((bg >> 8) & 0xFF);
            uint8_t bb = (bg & 0xFF);
            uint8_t nr = (br * 6 + 0x10 * 4) / 10;
            uint8_t ng = (bgg * 6 + 0x10 * 4) / 10;
            uint8_t nb = (bb * 6 + 0x18 * 4) / 10;
            g_desktop.framebuffer[idx] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
    
    /* Left: NeolyxOS logo/text */
    desktop_draw_text(12, 8, "NeolyxOS", COLOR_TEXT_WHITE);
    
    /* Center: Clock display - use real RTC time */
    rtc_time_t now;
    if (get_cached_rtc_time(&now) == 0) {
        /* Use real RTC time from CMOS */
        char time_str[16];
        time_str[0] = '0' + (now.hour / 10);
        time_str[1] = '0' + (now.hour % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (now.minute / 10);
        time_str[4] = '0' + (now.minute % 10);
        time_str[5] = '\0';
        
        int clock_x = (g_desktop.width - 40) / 2;
        desktop_draw_text(clock_x, 8, time_str, COLOR_TEXT_WHITE);
    } else {
        /* Fallback to boot ticks if RTC fails */
        uint64_t ticks = pit_get_ticks();
        uint32_t secs = (uint32_t)(ticks / 1000);
        uint32_t hours = ((secs / 3600) + 11) % 24;
        uint32_t mins = (secs / 60) % 60;
        
        char time_str[16];
        time_str[0] = '0' + (hours / 10);
        time_str[1] = '0' + (hours % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (mins / 10);
        time_str[4] = '0' + (mins % 10);
        time_str[5] = '\0';
        
        int clock_x = (g_desktop.width - 40) / 2;
        desktop_draw_text(clock_x, 8, time_str, COLOR_TEXT_WHITE);
    }
    
    /* Right side: Control Center icons (PC-style status menu) */
    int rx = g_desktop.width - 120;  /* Moved left to make room for network */
    
    /* Network status indicator (uses NXI icons) */
    network_indicator_render(rx, 6);
    rx += 24;
    
    /* Volume icon (speaker shape) - PC has speakers, not mobile signal */
    fill_rounded_rect(rx, 8, 6, 10, 1, COLOR_TEXT_WHITE);  /* Speaker body */
    fill_rounded_rect(rx + 6, 6, 3, 14, 1, COLOR_TEXT_WHITE);  /* Speaker cone */
    /* Sound waves */
    for (int wave = 0; wave < 2; wave++) {
        int woff = 12 + wave * 4;
        fill_rounded_rect(rx + woff, 10 + wave, 2, 6 - wave * 2, 1, 
                          wave == 0 ? COLOR_TEXT_WHITE : 0x80FFFFFF);
    }
    rx += 30;
    
    /* Bluetooth icon (simplified B shape) */
    fill_rounded_rect(rx, 7, 12, 14, 2, 0x80007AFF);  /* Blue pill */
    desktop_draw_text(rx + 2, 8, "B", COLOR_TEXT_WHITE);
    rx += 20;
    
    /* Notification bell icon - left of grid */
    fill_rounded_rect(rx, 7, 14, 14, 3, notification_center_is_visible() ? 0xFF007AFF : 0x60FFFFFF);
    /* Bell shape: top dome + bottom */
    fill_circle(rx + 7, 11, 4, COLOR_TEXT_WHITE);
    fill_rounded_rect(rx + 4, 14, 6, 4, 1, COLOR_TEXT_WHITE);
    /* Bell clapper dot */
    fill_circle(rx + 7, 19, 1, COLOR_TEXT_WHITE);
    rx += 22;
    
    /* Control Center toggle (grid icon) - clicking shows settings panel */
    fill_rounded_rect(rx, 7, 14, 14, 3, control_center_is_visible() ? 0xFF007AFF : 0x60FFFFFF);
    /* 2x2 grid dots */
    fill_circle(rx + 5, 11, 2, COLOR_TEXT_WHITE);
    fill_circle(rx + 11, 11, 2, COLOR_TEXT_WHITE);
    fill_circle(rx + 5, 17, 2, COLOR_TEXT_WHITE);
    fill_circle(rx + 11, 17, 2, COLOR_TEXT_WHITE);
}

/* Globe Clock Widget - top left */
static void render_globe_clock_widget(void) {
    int wx = 30, wy = 45;
    int ww = 220, wh = 95;
    
    /* Glassmorphic rounded background */
    fill_rounded_rect(wx, wy, ww, wh, WIDGET_RADIUS, GLASS_WIDGET);
    
    /* Earth globe (simplified colored circle) */
    int globe_cx = wx + 48;
    int globe_cy = wy + 47;
    int globe_r = 32;
    
    /* Ocean blue */
    fill_circle(globe_cx, globe_cy, globe_r, 0xFF1A4A6A);
    /* Landmass hints */
    fill_circle(globe_cx - 8, globe_cy - 8, 12, 0xFF2A6A5A);
    fill_circle(globe_cx + 10, globe_cy + 5, 8, 0xFF2A6A5A);
    /* Globe highlight */
    fill_circle(globe_cx - 10, globe_cy - 12, 5, 0x40FFFFFF);
    
    /* Get real date/time from RTC */
    /* Use Calendar.app's arrays instead of duplicate definitions */
    /* day_names_short[] and month_names_full[] from calendar.h */
    
    rtc_time_t now;
    uint32_t hours, minutes, day, month, year, weekday;
    
    if (get_cached_rtc_time(&now) == 0) {
        /* Use real RTC time */
        hours = now.hour;
        minutes = now.minute;
        day = now.day;
        month = now.month;  /* 1-12 */
        year = now.year;
        
        /* Use Calendar.app's calendar_day_of_week() - no duplicate algorithm */
        weekday = calendar_day_of_week(year, month, day);
        
        month = month - 1;  /* 0-indexed for months array */
    } else {
        /* Fallback to boot ticks if RTC fails */
        uint64_t ticks_ms = nx_gettime();
        uint64_t total_secs = ticks_ms / 1000;
        uint32_t secs_in_day = total_secs % 86400;
        hours = (secs_in_day / 3600) % 24;
        minutes = (secs_in_day / 60) % 60;
        day = 9;
        month = 0;  /* January */
        year = 2026;
        weekday = 5;  /* Friday (Jan 9 2026) */
    }
    
    /* Format date string: "Fri, 9. January" */
    char date_str[40];
    int pos = 0;
    const char *wd = day_names_short[weekday % 7];  /* From Calendar.app */
    while (*wd) date_str[pos++] = *wd++;
    date_str[pos++] = ','; date_str[pos++] = ' ';
    if (day >= 10) date_str[pos++] = '0' + (day / 10);
    date_str[pos++] = '0' + (day % 10);
    date_str[pos++] = '.'; date_str[pos++] = ' ';
    const char *mn = month_names_full[month % 12];  /* From Calendar.app */
    while (*mn) date_str[pos++] = *mn++;
    date_str[pos] = '\0';
    
    /* Format time string: "HH:MM" */
    char time_str[8];
    time_str[0] = '0' + (hours / 10);
    time_str[1] = '0' + (hours % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (minutes / 10);
    time_str[4] = '0' + (minutes % 10);
    time_str[5] = '\0';
    
    /* Date text */
    desktop_draw_text(wx + 90, wy + 12, date_str, COLOR_TEXT_BRIGHT);
    
    /* Large time - using 2x scale */
    draw_text_large(wx + 90, wy + 28, time_str, COLOR_TEXT_WHITE);
    
    /* Weather/location icon (small circle) */
    fill_circle(wx + 95, wy + 72, 6, COLOR_TEXT_DIM);
}

/* Anveshan Search Bar - center top */
static void render_search_bar(void) {
    int bar_w = 320;
    int bar_h = 36;
    int bar_x = (g_desktop.width - bar_w) / 2;
    int bar_y = 85;
    
    /* Pill-shaped glass background */
    fill_pill(bar_x, bar_y, bar_w, bar_h, GLASS_SEARCH);
    draw_pill_border(bar_x, bar_y, bar_w, bar_h, GLASS_BORDER);
    
    /* Search icon (magnifying glass - circle + line) */
    draw_circle(bar_x + 22, bar_y + bar_h/2, 7, COLOR_TEXT_DIM);
    for (int i = 0; i < 4; i++) {
        put_pixel(bar_x + 28 + i, bar_y + bar_h/2 + 5 + i, COLOR_TEXT_DIM);
    }
    
    /* Placeholder text */
    desktop_draw_text(bar_x + 42, bar_y + 12, "Anveshan | search that need", COLOR_TEXT_DIM);
    
    /* Microphone icon (simplified) */
    fill_rounded_rect(bar_x + bar_w - 28, bar_y + 8, 8, 14, 3, COLOR_TEXT_DIM);
    fill_circle(bar_x + bar_w - 24, bar_y + 8, 4, COLOR_TEXT_DIM);
}

/* External storage connected flag - set by kernel when USB/storage detected */
static int g_external_storage_connected = 0;

/* Desktop Icons - only show connected devices
 * External storage icon appears only when USB/external drive is detected */
static void render_desktop_icons(void) {
    /* Only render external storage icon when actually connected */
    if (!g_external_storage_connected) {
        return;  /* No connected devices - clean desktop */
    }
    
    /* External storage icon when connected */
    int icon_x = g_desktop.width - 85;
    int icon_y = 45;
    int icon_size = 48;
    
    /* Icon background */
    fill_rounded_rect(icon_x, icon_y, icon_size, icon_size, 8, 0x60506080);
    /* Storage device representation */
    fill_rounded_rect(icon_x + 8, icon_y + 16, 32, 18, 3, 0xFF4A5A6A);
    /* Activity LED */
    desktop_fill_rect(icon_x + 12, icon_y + 28, 4, 4, 0xFF88FF88);
    /* Labels */
    desktop_draw_text(icon_x - 8, icon_y + icon_size + 5, "External", COLOR_TEXT_WHITE);
    desktop_draw_text(icon_x + 2, icon_y + icon_size + 15, "Storage", COLOR_TEXT_WHITE);
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

/* Main application state */
static void render_window(desktop_window_t *win) {
    if (!win->visible) return;
    
    /* Check for animation state */
    int32_t wx = win->x;
    int32_t wy = win->y;
    uint32_t ww = win->width;
    uint32_t wh = win->height;
    float opacity = 1.0f;
    
    if (wm_is_animating(win->id)) {
        if (!wm_get_animated_geometry(win->id, &wx, &wy, &ww, &wh, &opacity)) {
            /* If minimized and not animating, skip render */
            if (win->minimized) return;
        }
    } else if (win->minimized) {
        return;
    }
    
    /* Apply PiP mode sizing */
    wm_window_state_t *wstate = wm_get_window_state(win->id);
    if (wstate) {
        if (wstate->mode == WIN_MODE_PIP) {
            /* Use PiP dimensions */
            ww = wstate->pip_w > 0 ? wstate->pip_w : 300;
            wh = wstate->pip_h > 0 ? wstate->pip_h : 200;
            opacity = wstate->opacity;
        } else if (wstate->mode == WIN_MODE_SIDEBAR) {
            if (!wstate->sidebar_expanded) {
                /* Slid to right edge - only show tab */
                wx = g_desktop.width - 40;
                ww = 36;
                wh = 80;
            }
        }
        if (wstate->opacity < 1.0f && !wm_is_animating(win->id)) {
            opacity = wstate->opacity;
        }
    }
    
    /* Skip if nearly invisible */
    if (opacity < 0.05f) return;
    
    int title_height = 24;
    
    /* Calculate alpha multiplier */
    uint8_t alpha = (uint8_t)(opacity * 255.0f);
    
    /* Window shadow (simplified) */
    uint32_t shadow_color = (alpha / 4) << 24;  /* Fade shadow with window */
    desktop_fill_rect(wx + 3, wy + 3, ww, wh + title_height, shadow_color);
    
    /* Window title bar */
    uint32_t title_color = (WINDOW_TITLE_COLOR & 0x00FFFFFF) | ((uint32_t)alpha << 24);
    desktop_fill_rect(wx, wy, ww, title_height, title_color);
    
    /* Window buttons (close, minimize, maximize) */
    /* Only show buttons if not too small (during minimize) */
    if (ww > 60 && wh > 30) {
        uint32_t btn_alpha = (uint32_t)alpha << 24;
        /* Close - red */
        desktop_fill_rect(wx + 8, wy + 6, 12, 12, (0x00FF5555 | btn_alpha));
        /* Minimize - yellow */
        desktop_fill_rect(wx + 24, wy + 6, 12, 12, (0x00FFCC00 | btn_alpha));
        /* Maximize - green */
        desktop_fill_rect(wx + 40, wy + 6, 12, 12, (0x0055CC55 | btn_alpha));
        
        /* Window title */
        desktop_draw_text(wx + 60, wy + 6, win->title, WINDOW_TEXT_COLOR);
    }
    
    /* Window body */
    uint32_t body_color = (WINDOW_BG_COLOR & 0x00FFFFFF) | ((uint32_t)alpha << 24);
    desktop_fill_rect(wx, wy + title_height, ww, wh, body_color);
    
    /* Window border */
    desktop_draw_rect(wx, wy, ww, wh + title_height, 
                     win->focused ? 0xFF6666FF : WINDOW_BORDER_COLOR);
    
    /* Custom content callback (only if not too small) */
    if (win->render_content && ww > 100 && wh > 50) {
        win->render_content(win, win->render_ctx);
    }
}

/* Render sidebar tab for collapsed sidebar windows */
static void render_sidebar_tab(desktop_window_t *win, wm_window_state_t *wstate) {
    if (!wstate || wstate->mode != WIN_MODE_SIDEBAR || wstate->sidebar_expanded) return;
    
    int tab_x = g_desktop.width - 40;
    int tab_y = win->y;
    int tab_w = 36;
    int tab_h = 80;
    
    /* Tab background with rounded corners */
    fill_rounded_rect(tab_x - 4, tab_y, tab_w + 8, tab_h, 8, 0xE0303050);
    
    /* Border highlight */
    desktop_draw_rect(tab_x - 4, tab_y, tab_w + 8, tab_h, 0xFF505070);
    
    /* Window title abbreviation (first letter) */
    char abbr[2];
    abbr[0] = win->title[0] ? win->title[0] : '?';
    abbr[1] = '\0';
    
    /* Center the letter */
    int text_x = tab_x + (tab_w - 8) / 2;
    int text_y = tab_y + 20;
    desktop_draw_text(text_x, text_y, abbr, COLOR_TEXT_WHITE);
    
    /* Second letter if available */
    if (win->title[1]) {
        abbr[0] = win->title[1];
        desktop_draw_text(text_x, text_y + 12, abbr, COLOR_TEXT_DIM);
    }
    
    /* Grip lines (indicate clickable) */
    for (int i = 0; i < 3; i++) {
        desktop_fill_rect(tab_x + 6, tab_y + 52 + i * 6, 20, 2, 0x60FFFFFF);
    }
    
    /* Small arrow indicator pointing left */
    for (int i = 0; i < 4; i++) {
        put_pixel_alpha(tab_x + 2 + i, tab_y + 40 - i, 0xA0FFFFFF);
        put_pixel_alpha(tab_x + 2 + i, tab_y + 40 + i, 0xA0FFFFFF);
    }
}

static void render_windows(void) {
    /* First pass: Render normal windows in z-order */
    for (int z = 0; z < MAX_WINDOWS; z++) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].id != 0 && g_windows[i].z_order == z) {
                /* Skip PiP windows - they render last */
                if (wm_is_pip(g_windows[i].id)) continue;
                
                /* Handle sidebar mode windows */
                if (wm_is_sidebar(g_windows[i].id)) {
                    wm_window_state_t *wstate = wm_get_window_state(g_windows[i].id);
                    if (wstate && !wstate->sidebar_expanded) {
                        /* Render collapsed sidebar as tab only */
                        render_sidebar_tab(&g_windows[i], wstate);
                        continue;
                    }
                }
                
                render_window(&g_windows[i]);
            }
        }
    }
    
    /* Second pass: Render PiP windows (always on top) */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && wm_is_pip(g_windows[i].id)) {
            render_window(&g_windows[i]);
        }
    }
}

static void render_desktop(void) {
    /* Only log once every 60 frames to avoid serial spam */
    static int frame_count = 0;
    static uint64_t last_time = 0;
    
    if (frame_count++ % 60 == 0) {
        serial_puts("[DESKTOP] Frame\n");
    }
    
    uint64_t now = pit_get_ticks();
    float dt = (last_time > 0) ? (float)(now - last_time) / 1000.0f : 0.016f;
    if (dt > 0.1f) dt = 0.016f;  /* Cap delta time */
    last_time = now;
    
    /* Update window manager animations */
    wm_tick(dt);
    
    /*
     * TEMPORARILY DISABLED: Static layer caching was causing missing UI issues.
     * Always render static elements every frame for now.
     */
    render_background();
    render_navigation_bar();
    render_globe_clock_widget();
    render_search_bar();
    render_desktop_icons();
    
    /* Render tile preview overlay (below windows) */
    int32_t tile_x, tile_y;
    uint32_t tile_w, tile_h;
    if (wm_get_tile_preview(&tile_x, &tile_y, &tile_w, &tile_h)) {
        /* Semi-transparent blue overlay */
        for (uint32_t py = 0; py < tile_h; py++) {
            for (uint32_t px = 0; px < tile_w; px++) {
                int32_t sx = tile_x + px;
                int32_t sy = tile_y + py;
                if (sx >= 0 && sx < (int32_t)g_desktop.width &&
                    sy >= 0 && sy < (int32_t)g_desktop.height) {
                    uint32_t idx = sy * (g_desktop.pitch / 4) + sx;
                    uint32_t bg = g_desktop.framebuffer[idx];
                    /* Blend with 40% blue overlay */
                    uint8_t r = ((bg >> 16) & 0xFF) * 6 / 10 + 0x40 * 4 / 10;
                    uint8_t g = ((bg >> 8) & 0xFF) * 6 / 10 + 0x60 * 4 / 10;
                    uint8_t b = (bg & 0xFF) * 6 / 10 + 0xFF * 4 / 10;
                    g_desktop.framebuffer[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
            }
        }
        /* Border */
        for (uint32_t px = 0; px < tile_w; px++) {
            put_pixel_alpha(tile_x + px, tile_y, 0xFF6690FF);
            put_pixel_alpha(tile_x + px, tile_y + tile_h - 1, 0xFF6690FF);
        }
        for (uint32_t py = 0; py < tile_h; py++) {
            put_pixel_alpha(tile_x, tile_y + py, 0xFF6690FF);
            put_pixel_alpha(tile_x + tile_w - 1, tile_y + py, 0xFF6690FF);
        }
    }
    
    render_windows();
    
    /* Modular dock rendering */
    dock_tick();  /* Update animations */
    dock_render(g_desktop.framebuffer, g_desktop.pitch, 
                g_desktop.mouse_x, g_desktop.mouse_y);
    
    /* Render App Drawer overlay (above everything except cursor) */
    app_drawer_render();
    
    /* Render Control Center overlay */
    control_center_tick();
    control_center_render(g_desktop.framebuffer, g_desktop.pitch);
    
    /* Render Notification Center overlay */
    notification_center_tick();
    notification_center_render(g_desktop.framebuffer, g_desktop.pitch);
    
    /* Render OSD overlays (above notification center) */
    volume_osd_tick();
    volume_osd_render(g_desktop.framebuffer, g_desktop.pitch);
    keyboard_osd_tick();
    keyboard_osd_render(g_desktop.framebuffer, g_desktop.pitch);
    theme_osd_tick();
    theme_osd_render(g_desktop.framebuffer, g_desktop.pitch);
    battery_warning_tick();
    battery_warning_render(g_desktop.framebuffer, g_desktop.pitch);
    
    /* Render keyboard shortcut help overlay (full-screen, above everything) */
    keyboard_osd_help_render(g_desktop.framebuffer, g_desktop.pitch,
                             g_desktop.width, g_desktop.height);
    
    /* Render Quick Access Menu if visible */
    wm_quick_menu_t *qm = wm_get_quick_menu();
    if (qm && qm->visible) {
        int mx = qm->x;
        int my = qm->y;
        int mw = 3 * 32 + 2 * 8 + 16;  /* 3 icons + gaps + padding */
        int mh = 32 + 16;
        
        /* Menu background */
        fill_rounded_rect(mx, my, mw, mh, 8, 0xE0303040);
        
        /* 3 icons: Pop (PiP), Maximize, Sidebar */
        int ix = mx + 8;
        int iy = my + 8;
        
        /* Icon 0: Pop (PiP) - small window icon */
        uint32_t pop_color = (qm->hover_idx == 0) ? 0xFF89B4FA : 0xFFCDD6F4;
        fill_rounded_rect(ix + 4, iy + 4, 24, 18, 3, pop_color);
        fill_rounded_rect(ix + 6, iy + 6, 20, 14, 2, 0xFF303040);
        ix += 40;
        
        /* Icon 1: Maximize - full square */
        uint32_t max_color = (qm->hover_idx == 1) ? 0xFFA6E3A1 : 0xFFCDD6F4;
        fill_rounded_rect(ix + 2, iy + 2, 28, 22, 4, max_color);
        fill_rounded_rect(ix + 4, iy + 4, 24, 18, 3, 0xFF303040);
        ix += 40;
        
        /* Icon 2: Sidebar - slide to edge */
        uint32_t side_color = (qm->hover_idx == 2) ? 0xFFF9E2AF : 0xFFCDD6F4;
        fill_rounded_rect(ix + 18, iy + 2, 12, 22, 3, side_color);
        fill_rounded_rect(ix + 2, iy + 6, 14, 14, 2, 0x80CDD6F4);
    }
    
    /* Cursor is drawn by KERNEL after flip - tell kernel cursor position via syscall */
    /* This mirrors macOS/Windows hardware cursor isolation - never overwritten by userspace */
    nx_cursor_set(g_desktop.mouse_x, g_desktop.mouse_y, 1);
    
    /* Flip back buffer to front buffer - kernel draws cursor AFTER this copy */
    flip_buffer();
}

/* ============ Window Management ============ */

uint32_t desktop_create_window(const char *title, int32_t x, int32_t y,
                               uint32_t width, uint32_t height) {
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
    
    desktop_window_t *win = &g_windows[slot];
    win->id = g_next_window_id++;
    
    /* Validate and copy title safely */
    if (title) {
        shell_strcpy(win->title, title, WINDOW_TITLE_MAX);
    } else {
        win->title[0] = '\0';  /* Empty title if NULL passed */
    }
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->visible = 1;
    win->focused = 1;
    win->minimized = 0;
    win->dragging = 0;
    win->z_order = g_desktop.window_count;
    win->render_content = NULL;
    win->render_ctx = NULL;
    win->key_handler = NULL;
    win->key_ctx = NULL;
    
    g_desktop.window_count++;
    
    /* Unfocus other windows */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0 && g_windows[i].id != win->id) {
            g_windows[i].focused = 0;
        }
    }
    
    serial_puts("[DESKTOP] Created window: ");
    serial_puts(win->title);  /* Use copied title, not raw pointer */
    serial_puts(" id=");
    serial_dec(win->id);
    serial_puts("\n");
    
    return win->id;
}

int desktop_destroy_window(uint32_t window_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].id = 0;
            g_desktop.window_count--;
            serial_puts("[DESKTOP] Destroyed window id=");
            serial_dec(window_id);
            serial_puts("\n");
            return 0;
        }
    }
    return -1;
}

void desktop_focus_window(uint32_t window_id) {
    desktop_window_t *target = NULL;
    
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].id == window_id) {
            target = &g_windows[i];
        }
        if (g_windows[i].id != 0) {
            g_windows[i].focused = 0;
        }
    }
    
    if (target) {
        target->focused = 1;
        target->z_order = g_desktop.window_count;
    }
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

/* ============ Input Handling ============ */

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
    
    /* Initialize smooth position on first use */
    if (!g_mouse_smooth_init) {
        g_smooth_mouse_x = target_x * 256;  /* Fixed-point 8.8 */
        g_smooth_mouse_y = target_y * 256;
        g_mouse_smooth_init = 1;
    }
    
    /* Exponential moving average for smooth cursor movement
     * new_pos = old_pos + (target - old_pos) / smooth_factor
     * Using fixed-point arithmetic (8-bit fractional) */
    int32_t target_fp_x = target_x * 256;
    int32_t target_fp_y = target_y * 256;
    
    g_smooth_mouse_x += (target_fp_x - g_smooth_mouse_x) >> MOUSE_SMOOTH_FACTOR;
    g_smooth_mouse_y += (target_fp_y - g_smooth_mouse_y) >> MOUSE_SMOOTH_FACTOR;
    
    /* Convert back to integer position */
    g_desktop.mouse_x = g_smooth_mouse_x >> 8;
    g_desktop.mouse_y = g_smooth_mouse_y >> 8;
    
    uint8_t prev_buttons = g_desktop.mouse_buttons;
    g_desktop.mouse_buttons = buttons;
    
    /* If App Drawer is open, forward mouse to it first */
    if (app_drawer_is_open()) {
        app_drawer_handle_mouse(g_desktop.mouse_x, g_desktop.mouse_y, buttons);
        return;  /* Drawer consumes all mouse events when open */
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
    
    /* Update quick menu hover state */
    wm_quick_menu_hit_test(g_desktop.mouse_x, g_desktop.mouse_y);
    
    /* Handle left click */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        /* Check quick menu click first */
        wm_quick_menu_t *qm = wm_get_quick_menu();
        if (qm && qm->visible) {
            int hit = wm_quick_menu_hit_test(g_desktop.mouse_x, g_desktop.mouse_y);
            if (hit >= 0) {
                wm_quick_menu_click(g_desktop.mouse_x, g_desktop.mouse_y);
                return;
            } else {
                /* Click outside menu - hide it */
                wm_hide_quick_menu();
            }
        }
        
        /* Left click - check window hit */
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            desktop_window_t *win = &g_windows[i];
            if (win->id == 0 || !win->visible) continue;
            
            wm_window_state_t *wstate = wm_get_window_state(win->id);
            
            /* Check sidebar tab click */
            if (wstate && wstate->mode == WIN_MODE_SIDEBAR && !wstate->sidebar_expanded) {
                /* Collapsed sidebar - check tab click */
                int tab_x = g_desktop.width - 40;
                int tab_y = win->y;
                if (g_desktop.mouse_x >= tab_x && g_desktop.mouse_x < tab_x + 36 &&
                    g_desktop.mouse_y >= tab_y && g_desktop.mouse_y < tab_y + 80) {
                    wm_toggle_sidebar_expand(win->id);
                    break;
                }
                continue;
            }
            
            /* Check expanded sidebar click outside - collapse it */
            if (wstate && wstate->mode == WIN_MODE_SIDEBAR && wstate->sidebar_expanded) {
                if (g_desktop.mouse_x < win->x || 
                    g_desktop.mouse_x >= win->x + (int32_t)win->width ||
                    g_desktop.mouse_y < win->y ||
                    g_desktop.mouse_y >= win->y + (int32_t)win->height + 24) {
                    wm_toggle_sidebar_expand(win->id);
                    break;
                }
            }
            
            int title_height = 24;
            
            /* Hit test */
            if (g_desktop.mouse_x >= win->x && 
                g_desktop.mouse_x < win->x + (int32_t)win->width &&
                g_desktop.mouse_y >= win->y &&
                g_desktop.mouse_y < win->y + (int32_t)win->height + title_height) {
                
                /* Check close button */
                if (g_desktop.mouse_x >= win->x + 8 &&
                    g_desktop.mouse_x < win->x + 20 &&
                    g_desktop.mouse_y >= win->y + 6 &&
                    g_desktop.mouse_y < win->y + 18) {
                    wm_close_window_animated(win->id);
                    /* Mark for delayed destruction after animation */
                    break;
                }
                
                /* Check minimize button */
                if (g_desktop.mouse_x >= win->x + 24 &&
                    g_desktop.mouse_x < win->x + 36 &&
                    g_desktop.mouse_y >= win->y + 6 &&
                    g_desktop.mouse_y < win->y + 18) {
                    /* Start minimize animation towards dock center */
                    int dock_center_x = g_desktop.width / 2;
                    int dock_center_y = g_desktop.height - 50;
                    
                    wm_window_state_t *state = wm_get_window_state(win->id);
                    if (state) {
                        state->animation.start_x = win->x;
                        state->animation.start_y = win->y;
                        state->animation.start_w = win->width;
                        state->animation.start_h = win->height;
                        state->animation.end_x = dock_center_x;
                        state->animation.end_y = dock_center_y;
                        state->animation.end_w = 48;
                        state->animation.end_h = 36;
                    }
                    wm_minimize_window(win->id, dock_center_x, dock_center_y);
                    wm_add_minimized(win->id, 0, win->title, dock_center_x, dock_center_y);
                    win->minimized = 1;
                    break;
                }
                
                /* Check maximize button */
                if (g_desktop.mouse_x >= win->x + 40 &&
                    g_desktop.mouse_x < win->x + 52 &&
                    g_desktop.mouse_y >= win->y + 6 &&
                    g_desktop.mouse_y < win->y + 18) {
                    wm_window_state_t *state = wm_get_window_state(win->id);
                    if (state) {
                        if (state->is_maximized) {
                            /* Restore from maximized */
                            win->x = state->pre_max_x;
                            win->y = state->pre_max_y;
                            win->width = state->pre_max_w;
                            win->height = state->pre_max_h;
                            state->is_maximized = 0;
                        } else {
                            /* Save current position and maximize */
                            state->pre_max_x = win->x;
                            state->pre_max_y = win->y;
                            state->pre_max_w = win->width;
                            state->pre_max_h = win->height;
                            
                            state->animation.start_x = win->x;
                            state->animation.start_y = win->y;
                            state->animation.start_w = win->width;
                            state->animation.start_h = win->height;
                            wm_maximize_window(win->id);
                            
                            /* Apply immediately for now */
                            win->x = 8;
                            win->y = 28 + 8;
                            win->width = g_desktop.width - 16;
                            win->height = g_desktop.height - 28 - 80 - 16;
                        }
                    }
                    break;
                }
                
                /* Focus window */
                desktop_focus_window(win->id);
                
                /* PiP windows can be dragged from anywhere */
                if (wstate && wstate->mode == WIN_MODE_PIP) {
                    win->dragging = 1;
                    break;
                }
                
                /* Start dragging if in title bar (normal windows) */
                if (g_desktop.mouse_y < win->y + title_height) {
                    win->dragging = 1;
                }
                
                break;
            }
        }
    }
    
    /* Handle right click - show quick menu on title bar */
    if ((buttons & 0x02) && !(prev_buttons & 0x02)) {
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            desktop_window_t *win = &g_windows[i];
            if (win->id == 0 || !win->visible || win->minimized) continue;
            
            int title_height = 24;
            
            /* Check if clicking in title bar area */
            if (g_desktop.mouse_x >= win->x && 
                g_desktop.mouse_x < win->x + (int32_t)win->width &&
                g_desktop.mouse_y >= win->y &&
                g_desktop.mouse_y < win->y + title_height) {
                
                /* Show quick menu centered on title bar */
                int menu_w = 3 * 32 + 2 * 8 + 16;
                int menu_x = win->x + (win->width - menu_w) / 2;
                int menu_y = win->y + title_height + 4;
                
                wm_show_quick_menu(win->id, menu_x, menu_y);
                break;
            }
        }
    }
    
    /* Handle drag with tile preview */
    if (buttons & 0x01) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].dragging) {
                g_windows[i].x += dx;
                g_windows[i].y += dy;
                
                /* Check for tile snap */
                wm_tile_pos_t tile = wm_suggest_tile(
                    g_desktop.mouse_x, g_desktop.mouse_y,
                    g_windows[i].x, g_windows[i].y);
                wm_set_tile_preview(tile);
            }
        }
    } else {
        /* Release drag - apply tile if preview was active */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].dragging) {
                int32_t tx, ty;
                uint32_t tw, th;
                if (wm_get_tile_preview(&tx, &ty, &tw, &th)) {
                    /* Apply tile position */
                    wm_window_state_t *state = wm_get_window_state(g_windows[i].id);
                    if (state) {
                        state->pre_tile_x = g_windows[i].x;
                        state->pre_tile_y = g_windows[i].y;
                        state->pre_tile_w = g_windows[i].width;
                        state->pre_tile_h = g_windows[i].height;
                    }
                    g_windows[i].x = tx;
                    g_windows[i].y = ty;
                    g_windows[i].width = tw;
                    g_windows[i].height = th;
                }
            }
            g_windows[i].dragging = 0;
        }
        wm_clear_tile_preview();
    }
}

/* ============ NX Key Long-Press Tracking (File Scope) ============ */

static uint64_t g_nx_key_press_time = 0;
static int g_nx_key_held = 0;
static int g_nx_help_shown = 0;
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

void desktop_handle_key(uint8_t scancode, uint8_t pressed) {
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
            /* Key pressed - start tracking for long-press */
            g_nx_key_press_time = nx_gettime();
            g_nx_key_held = 1;
            g_nx_help_shown = 0;
        } else {
            /* Key released */
            if (g_nx_help_shown) {
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
        
        if (init_framebuffer() != 0) {
            serial_puts("[DESKTOP] Failed to init framebuffer via syscall\n");
            return -1;
        }
        /* Use back buffer for rendering */
        g_desktop.framebuffer = g_backbuffer;
        g_desktop.width = g_fb_info.width;
        g_desktop.height = g_fb_info.height;
        /* SINGLE BUFFER MODE: Using hardware framebuffer directly - use hardware pitch */
        g_desktop.pitch = g_fb_info.pitch;
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
    
    nx_debug_print("[DESKTOP] Calling WM init...\n");
    
    /* Initialize Window Manager */
    wm_init(g_desktop.width, g_desktop.height);
    nx_debug_print("[DESKTOP] WM initialized, booting to clean desktop...\n");
    
    /* NOTE: No auto terminal window - user launches apps manually from dock */
    
    /* Initialize Event Bus */
    nxevent_init();
    nx_debug_print("[DESKTOP] Event bus done, dock init...\n");
    
    /* Initialize Dock */
    dock_init(g_desktop.width, g_desktop.height);
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
    
    int y = win->y + 28 + 10;
    int x = win->x + 10;
    
    desktop_draw_text(x, y, "Available Fonts:", 0xFFFFFFFF);
    y += 16;
    
    const char *fonts[] = {"Consolas", "Monaco", "Menlo", "Noto Mono", "Terminus"};
    for (int i = 0; i < 5; i++) {
        desktop_draw_text(x + 10, y, fonts[i], 0xFFCCCCCC);
        y += 12;
    }
    
    y += 10;
    desktop_draw_text(x, y, "Current: Consolas 14pt", 0xFF88FF88);
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
    render_desktop();
    serial_puts("[DESKTOP] Initial render complete\n");
    
    uint32_t frame_count = 0;
    uint32_t input_count = 0;
    uint32_t last_mouse_x = g_desktop.mouse_x;
    uint32_t last_mouse_y = g_desktop.mouse_y;
    
    while (g_desktop.running) {
        frame_count++;
        
        /* Poll ALL input events */
        input_event_t event;
        int had_input = 0;
        int mouse_moved = 0;
        
        while (nx_input_poll(&event) > 0) {
            had_input = 1;
            input_count++;
            
            if (event.type == INPUT_TYPE_MOUSE_MOVE || event.type == INPUT_TYPE_MOUSE_BUTTON) {
                /* Mouse event */
                desktop_handle_mouse(event.mouse.dx, event.mouse.dy, event.mouse.buttons);
                mouse_moved = 1;
                
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
                desktop_handle_key(event.keyboard.scancode, event.keyboard.pressed);
            }
        }
        
        /* Process pending events from event bus */
        nxevent_process();
        
        /* Check NX key long-press for help overlay */
        nx_key_tick();
        
        /* Always render every frame */
        render_desktop();
        
        /* Frame rate limiting - cap at ~60 FPS to reduce flicker
         * This gives stable frame timing and reduces partial frame visibility
         */
        nx_sleep(16);  /* 16ms ≈ 60 FPS */
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
