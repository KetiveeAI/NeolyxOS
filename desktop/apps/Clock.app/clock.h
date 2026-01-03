/*
 * NeolyxOS Clock Application
 * 
 * Full-screen clock with NTP time sync using Indian time pool.
 * Displays time in IST (UTC+5:30) timezone.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>

/* Time structure */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    int16_t  utc_offset_mins;  /* IST = +330 */
} clock_time_t;

/* NTP configuration */
typedef struct {
    const char *server;       /* NTP server hostname */
    uint16_t    port;         /* NTP port (123) */
    uint32_t    sync_interval;/* Sync interval in seconds */
    bool        enabled;
} ntp_config_t;

/* Clock application context */
typedef struct {
    clock_time_t current_time;
    ntp_config_t ntp_config;
    uint64_t     last_sync;
    uint64_t     boot_ticks;
    bool         ntp_synced;
    bool         running;
} clock_ctx_t;

/* NTP client functions */
int  ntp_init(clock_ctx_t *ctx);
int  ntp_sync(clock_ctx_t *ctx);
void ntp_shutdown(clock_ctx_t *ctx);

/* Clock functions */
void clock_init(clock_ctx_t *ctx);
void clock_update(clock_ctx_t *ctx);
void clock_render(clock_ctx_t *ctx, uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch);

/* Time display helpers */
void clock_get_time_string(clock_ctx_t *ctx, char *buf, size_t len);
void clock_get_date_string(clock_ctx_t *ctx, char *buf, size_t len);
const char *clock_get_day_name(clock_ctx_t *ctx);
const char *clock_get_month_name(clock_ctx_t *ctx);

/* IST offset constant */
#define IST_OFFSET_MINS  330   /* UTC+5:30 */

/* NTP constants */
#define NTP_PORT         123
#define NTP_PACKET_SIZE  48
#define NTP_SYNC_INTERVAL 300  /* 5 minutes */

/* Indian NTP server pool */
#define NTP_SERVER_PRIMARY   "in.pool.ntp.org"
#define NTP_SERVER_FALLBACK  "time.google.com"

#endif /* CLOCK_H */
