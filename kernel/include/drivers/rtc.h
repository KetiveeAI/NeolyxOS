/*
 * NeolyxOS RTC Driver
 * 
 * CMOS Real-Time Clock driver for x86_64.
 * Reads/writes time from motherboard RTC via ports 0x70/0x71.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* RTC time structure */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
} rtc_time_t;

/* Initialize RTC driver */
void rtc_init(void);

/* Read current time from CMOS RTC */
void rtc_read_time(rtc_time_t *time);

/* Write time to CMOS RTC (requires privilege) */
void rtc_write_time(const rtc_time_t *time);

/* Get boot time (captured at kernel init) */
const rtc_time_t *rtc_get_boot_time(void);

/* Convert RTC time to Unix timestamp (seconds since 1970-01-01) */
uint64_t rtc_to_unix(const rtc_time_t *time);

/* Convert Unix timestamp to RTC time */
void rtc_from_unix(uint64_t timestamp, rtc_time_t *time);

/* Get current time as Unix timestamp (boot_time + ticks) */
uint64_t rtc_get_unix_time(void);

#endif /* RTC_H */
