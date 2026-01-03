/*
 * NeolyxOS RTC Driver Implementation
 * 
 * CMOS Real-Time Clock driver for x86_64.
 * Reads time from motherboard CMOS via ports 0x70/0x71.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "drivers/rtc.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* CMOS RTC ports */
#define CMOS_ADDR_PORT  0x70
#define CMOS_DATA_PORT  0x71

/* CMOS register addresses */
#define CMOS_REG_SECONDS    0x00
#define CMOS_REG_MINUTES    0x02
#define CMOS_REG_HOURS      0x04
#define CMOS_REG_WEEKDAY    0x06
#define CMOS_REG_DAY        0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_CENTURY    0x32  /* May not be present on all systems */
#define CMOS_REG_STATUS_A   0x0A
#define CMOS_REG_STATUS_B   0x0B

/* Boot time (captured at init) */
static rtc_time_t g_boot_time;
static uint64_t g_boot_ticks = 0;

/* Helper to print decimal */
static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 11;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

/* Read CMOS register */
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR_PORT, reg | 0x80);  /* Disable NMI (bit 7) */
    return inb(CMOS_DATA_PORT);
}

/* Write CMOS register */
static void cmos_write(uint8_t reg, uint8_t val) {
    outb(CMOS_ADDR_PORT, reg | 0x80);
    outb(CMOS_DATA_PORT, val);
}

/* Check if RTC update in progress */
static int rtc_is_updating(void) {
    return (cmos_read(CMOS_REG_STATUS_A) & 0x80) != 0;
}

/* Convert BCD to binary */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/* Convert binary to BCD */
static uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

/* Read time from CMOS with BCD handling */
void rtc_read_time(rtc_time_t *time) {
    if (!time) return;
    
    /*
     * Double-read until stable (handles update-in-progress race condition).
     * The RTC can update between reading seconds and minutes, causing
     * invalid time values. We detect this by comparing seconds before/after.
     */
    uint8_t seconds, minutes, hours, weekday, day, month, year;
    uint8_t last_seconds;
    
    do {
        /* Wait for any in-progress update to finish */
        while (rtc_is_updating());
        
        /* Read all registers */
        last_seconds = cmos_read(CMOS_REG_SECONDS);
        seconds = last_seconds;
        minutes = cmos_read(CMOS_REG_MINUTES);
        hours   = cmos_read(CMOS_REG_HOURS);
        weekday = cmos_read(CMOS_REG_WEEKDAY);
        day     = cmos_read(CMOS_REG_DAY);
        month   = cmos_read(CMOS_REG_MONTH);
        year    = cmos_read(CMOS_REG_YEAR);
        
        /* Re-read seconds to detect if time rolled during read */
    } while (cmos_read(CMOS_REG_SECONDS) != last_seconds);
    
    uint8_t century = 20;  /* Default to 21st century */
    
    /* Read Status B to check data format */
    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    int is_bcd = !(status_b & 0x04);
    int is_12h = !(status_b & 0x02);
    
    /* Extract PM flag BEFORE any conversion (bit 7 of hours) */
    int is_pm = (hours & 0x80) != 0;
    uint8_t hours_raw = hours & 0x7F;  /* Strip PM bit for processing */
    
    /* Convert BCD to binary if needed */
    if (is_bcd) {
        seconds   = bcd_to_bin(seconds);
        minutes   = bcd_to_bin(minutes);
        hours_raw = bcd_to_bin(hours_raw);  /* Convert BCD portion only */
        weekday   = bcd_to_bin(weekday);
        day       = bcd_to_bin(day);
        month     = bcd_to_bin(month);
        year      = bcd_to_bin(year);
    }
    
    /* Handle 12-hour format → 24-hour */
    if (is_12h) {
        if (hours_raw == 12) {
            hours_raw = is_pm ? 12 : 0;  /* 12 PM = 12:00, 12 AM = 00:00 */
        } else if (is_pm) {
            hours_raw += 12;  /* 1-11 PM → 13-23 */
        }
        /* 1-11 AM stays as-is */
    }
    
    /* Fill structure */
    time->second  = seconds;
    time->minute  = minutes;
    time->hour    = hours_raw;
    time->weekday = weekday;
    time->day     = day;
    time->month   = month;
    time->year    = (century * 100) + year;
}


/* Write time to CMOS */
void rtc_write_time(const rtc_time_t *time) {
    if (!time) return;
    
    /* Wait for update to complete */
    while (rtc_is_updating());
    
    /* Read Status B for format info */
    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    int is_bcd = !(status_b & 0x04);
    
    uint8_t seconds = time->second;
    uint8_t minutes = time->minute;
    uint8_t hours   = time->hour;
    uint8_t day     = time->day;
    uint8_t month   = time->month;
    uint8_t year    = time->year % 100;
    
    /* Convert to BCD if needed */
    if (is_bcd) {
        seconds = bin_to_bcd(seconds);
        minutes = bin_to_bcd(minutes);
        hours   = bin_to_bcd(hours);
        day     = bin_to_bcd(day);
        month   = bin_to_bcd(month);
        year    = bin_to_bcd(year);
    }
    
    /* Write registers */
    cmos_write(CMOS_REG_SECONDS, seconds);
    cmos_write(CMOS_REG_MINUTES, minutes);
    cmos_write(CMOS_REG_HOURS, hours);
    cmos_write(CMOS_REG_DAY, day);
    cmos_write(CMOS_REG_MONTH, month);
    cmos_write(CMOS_REG_YEAR, year);
}

/* Days in each month */
static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* Check leap year */
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* Convert RTC time to Unix timestamp */
uint64_t rtc_to_unix(const rtc_time_t *time) {
    if (!time) return 0;
    
    uint64_t days = 0;
    
    /* Years since 1970 */
    for (int y = 1970; y < time->year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    /* Months in current year */
    for (int m = 0; m < time->month - 1; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap_year(time->year)) {
            days++;  /* Feb in leap year */
        }
    }
    
    /* Days in current month */
    days += time->day - 1;
    
    /* Convert to seconds and add time of day */
    uint64_t secs = days * 86400ULL;
    secs += (uint64_t)time->hour * 3600;
    secs += (uint64_t)time->minute * 60;
    secs += time->second;
    
    return secs;
}

/* Convert Unix timestamp to RTC time */
void rtc_from_unix(uint64_t timestamp, rtc_time_t *time) {
    if (!time) return;
    
    /* Seconds of day */
    uint64_t days = timestamp / 86400;
    uint32_t day_secs = timestamp % 86400;
    
    time->hour   = day_secs / 3600;
    time->minute = (day_secs % 3600) / 60;
    time->second = day_secs % 60;
    
    /* Find year */
    int year = 1970;
    while (days >= (is_leap_year(year) ? 366ULL : 365ULL)) {
        days -= is_leap_year(year) ? 366 : 365;
        year++;
    }
    time->year = year;
    
    /* Find month */
    int month = 0;
    while (month < 12) {
        int mdays = days_in_month[month];
        if (month == 1 && is_leap_year(year)) mdays++;
        if (days < (uint64_t)mdays) break;
        days -= mdays;
        month++;
    }
    time->month = month + 1;
    time->day = days + 1;
    
    /* Calculate weekday (Zeller's algorithm simplified) */
    time->weekday = ((days + 4) % 7);  /* Jan 1, 1970 was Thursday */
}

/* Get current time as Unix timestamp */
uint64_t rtc_get_unix_time(void) {
    uint64_t boot_unix = rtc_to_unix(&g_boot_time);
    uint64_t elapsed_ms = pit_get_ticks() - g_boot_ticks;
    return boot_unix + (elapsed_ms / 1000);
}

/* Get boot time */
const rtc_time_t *rtc_get_boot_time(void) {
    return &g_boot_time;
}

/* Initialize RTC driver */
void rtc_init(void) {
    serial_puts("[RTC] Initializing CMOS RTC driver...\n");
    
    /* Record boot ticks */
    g_boot_ticks = pit_get_ticks();
    
    /* Read initial time */
    rtc_read_time(&g_boot_time);
    
    /* Log boot time */
    serial_puts("[RTC] Boot time: ");
    serial_dec(g_boot_time.year);
    serial_putc('-');
    if (g_boot_time.month < 10) serial_putc('0');
    serial_dec(g_boot_time.month);
    serial_putc('-');
    if (g_boot_time.day < 10) serial_putc('0');
    serial_dec(g_boot_time.day);
    serial_putc(' ');
    if (g_boot_time.hour < 10) serial_putc('0');
    serial_dec(g_boot_time.hour);
    serial_putc(':');
    if (g_boot_time.minute < 10) serial_putc('0');
    serial_dec(g_boot_time.minute);
    serial_putc(':');
    if (g_boot_time.second < 10) serial_putc('0');
    serial_dec(g_boot_time.second);
    serial_puts("\n");
    
    serial_puts("[RTC] Ready\n");
}
