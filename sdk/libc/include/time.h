/*
 * nxlibc - <time.h>
 *
 * Calendar time comes from the real-time clock (sys rtc.unix): seconds since
 * 1970-01-01 00:00:00 UTC, one-second resolution. NeolyxOS keeps the clock
 * in UTC and has no time zones yet, so localtime() equals gmtime() and
 * mktime() reads its argument as UTC.
 *
 * clock() returns milliseconds of wall time since the program started
 * (there is no per-process CPU time accounting yet); CLOCKS_PER_SEC is 1000.
 *
 * asctime, ctime, gmtime and localtime return pointers to static storage
 * that the next call overwrites; gmtime_r and localtime_r (C23) do not.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_TIME_H
#define NXLIBC_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long long time_t;
typedef long      clock_t;

#define CLOCKS_PER_SEC  ((clock_t)1000)
#define TIME_UTC        1

struct tm {
    int tm_sec;     /* 0-60 */
    int tm_min;     /* 0-59 */
    int tm_hour;    /* 0-23 */
    int tm_mday;    /* 1-31 */
    int tm_mon;     /* 0-11 */
    int tm_year;    /* years since 1900 */
    int tm_wday;    /* 0-6, Sunday = 0 */
    int tm_yday;    /* 0-365 */
    int tm_isdst;   /* always 0: no daylight saving time */
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

clock_t    clock(void);
double     difftime(time_t end, time_t start);
time_t     mktime(struct tm *tm);
time_t     time(time_t *out);
int        timespec_get(struct timespec *ts, int base);

char      *asctime(const struct tm *tm);
char      *ctime(const time_t *t);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
struct tm *gmtime_r(const time_t *restrict t, struct tm *restrict out);
struct tm *localtime_r(const time_t *restrict t, struct tm *restrict out);
size_t     strftime(char *restrict buf, size_t size, const char *restrict fmt,
                    const struct tm *restrict tm);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_TIME_H */
