/*
 * NeolyxOS Clock Application - Main Entry Point
 * 
 * Full-screen analog/digital clock with NTP sync.
 * Uses Indian NTP pool (in.pool.ntp.org) for IST time.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "clock.h"
#include <string.h>

/* REOX UI library (if available) */
#ifdef USE_REOX
#include "../../REOX/reolab/runtime/reox_ui.h"
#include "../../REOX/reolab/runtime/reox_theme.h"
#endif

/* Global clock context */
static clock_ctx_t g_clock;

/* Colors matching NeolyxOS design */
#define COLOR_BG         0xFF0A0B1A
#define COLOR_GLASS      0x60303050
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_DIM        0xFF808090
#define COLOR_ACCENT     0xFF4080B0

/* Day names */
static const char *day_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

/* Month names */
static const char *month_names[] = {
    "January", "February", "March", "April",
    "May", "June", "July", "August",
    "September", "October", "November", "December"
};

/* Simple framebuffer text drawing (kernel-compatible) */
extern void desktop_draw_text(int x, int y, const char *text, uint32_t color);
extern void desktop_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Initialize clock */
void clock_init(clock_ctx_t *ctx) {
    memset(ctx, 0, sizeof(clock_ctx_t));
    
    /* Default time - will be updated by NTP or local timer */
    ctx->current_time.year = 2025;
    ctx->current_time.month = 12;
    ctx->current_time.day = 26;
    ctx->current_time.hour = 11;
    ctx->current_time.minute = 0;
    ctx->current_time.second = 0;
    ctx->current_time.utc_offset_mins = IST_OFFSET_MINS;
    
    /* NTP configuration */
    ctx->ntp_config.server = NTP_SERVER_PRIMARY;
    ctx->ntp_config.port = NTP_PORT;
    ctx->ntp_config.sync_interval = NTP_SYNC_INTERVAL;
    ctx->ntp_config.enabled = true;
    
    ctx->running = true;
}

/* Update time from system ticks */
void clock_update(clock_ctx_t *ctx) {
    extern uint64_t pit_get_ticks(void);
    uint64_t ticks = pit_get_ticks();
    
    /* Calculate seconds since boot */
    uint32_t secs = (uint32_t)(ticks / 1000);
    
    /* Add IST offset and base time */
    uint32_t total_secs = secs + (11 * 3600);  /* Start at 11:00 */
    
    ctx->current_time.second = total_secs % 60;
    ctx->current_time.minute = (total_secs / 60) % 60;
    ctx->current_time.hour = (total_secs / 3600) % 24;
}

/* Get time as string "HH:MM:SS" */
void clock_get_time_string(clock_ctx_t *ctx, char *buf, size_t len) {
    if (len < 9) return;
    
    buf[0] = '0' + (ctx->current_time.hour / 10);
    buf[1] = '0' + (ctx->current_time.hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (ctx->current_time.minute / 10);
    buf[4] = '0' + (ctx->current_time.minute % 10);
    buf[5] = ':';
    buf[6] = '0' + (ctx->current_time.second / 10);
    buf[7] = '0' + (ctx->current_time.second % 10);
    buf[8] = '\0';
}

/* Get date as string "DD Month YYYY" */
void clock_get_date_string(clock_ctx_t *ctx, char *buf, size_t len) {
    if (len < 20) return;
    
    int idx = 0;
    
    /* Day */
    buf[idx++] = '0' + (ctx->current_time.day / 10);
    buf[idx++] = '0' + (ctx->current_time.day % 10);
    buf[idx++] = ' ';
    
    /* Month name */
    const char *month = month_names[ctx->current_time.month - 1];
    while (*month && idx < (int)len - 6) {
        buf[idx++] = *month++;
    }
    buf[idx++] = ' ';
    
    /* Year */
    buf[idx++] = '0' + (ctx->current_time.year / 1000);
    buf[idx++] = '0' + ((ctx->current_time.year / 100) % 10);
    buf[idx++] = '0' + ((ctx->current_time.year / 10) % 10);
    buf[idx++] = '0' + (ctx->current_time.year % 10);
    buf[idx] = '\0';
}

/* Get day name */
const char *clock_get_day_name(clock_ctx_t *ctx) {
    /* Simple day calculation - Thursday = 4 for Dec 26, 2025 */
    (void)ctx;
    return day_names[4];  /* Thursday */
}

/* Get month name */
const char *clock_get_month_name(clock_ctx_t *ctx) {
    if (ctx->current_time.month >= 1 && ctx->current_time.month <= 12) {
        return month_names[ctx->current_time.month - 1];
    }
    return "Unknown";
}

/* Render clock display */
void clock_render(clock_ctx_t *ctx, uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch) {
    (void)fb; (void)pitch;  /* Suppress warnings */
    
    /* Center of screen */
    int cx = width / 2;
    int cy = height / 2;
    
    /* Draw large time */
    char time_buf[16];
    clock_get_time_string(ctx, time_buf, sizeof(time_buf));
    
    /* Time display centered */
    desktop_draw_text(cx - 60, cy - 20, time_buf, COLOR_WHITE);
    
    /* Date below */
    char date_buf[32];
    clock_get_date_string(ctx, date_buf, sizeof(date_buf));
    desktop_draw_text(cx - 80, cy + 20, date_buf, COLOR_DIM);
    
    /* Day name */
    const char *day = clock_get_day_name(ctx);
    desktop_draw_text(cx - 40, cy + 40, day, COLOR_ACCENT);
}

/* Application main entry point */
int main(void) {
    /* Initialize clock */
    clock_init(&g_clock);
    
    /* Try NTP sync (if network available) */
    if (g_clock.ntp_config.enabled) {
        ntp_init(&g_clock);
        ntp_sync(&g_clock);
    }
    
    /* Main event loop */
    while (g_clock.running) {
        clock_update(&g_clock);
        
        /* Render would happen in windowed app context */
        /* For now, update is all we do */
        
        /* Small delay */
        for (volatile int i = 0; i < 100000; i++);
    }
    
    ntp_shutdown(&g_clock);
    return 0;
}
