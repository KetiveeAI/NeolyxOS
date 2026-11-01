/*
 * nxlibc - <time.h>.
 *
 * Calendar arithmetic uses the proleptic Gregorian calendar with days
 * counted from 1970-01-01, valid for every time_t whose year fits in an int
 * (gmtime fails with EOVERFLOW beyond that).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <time.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <nx/sys.h>
#include "../internal.h"

uint64_t __nxc_start_ms;

/*
 * Seconds since 1970 from the kernel's real-time clock. rtc.unix returns
 * an unsigned count; a value with the top bit set cannot be a date and
 * means the kernel's clock was never set up. ISO C's answer for "no
 * calendar time available" is (time_t)-1, so that is what callers get
 * (logged once, so the cause is visible in the serial log).
 */
static int rtc_now(time_t *out) {
    static int logged;
    uint64_t v = nxsys_rtc_unix();
    if (v > (uint64_t)INT64_MAX) {
        if (!logged) {
            logged = 1;
            __nxc_log("nxlibc: time: the kernel real-time clock is not set (rtc.unix gave a negative value); "
                      "time() returns -1\n");
        }
        return -1;
    }
    *out = (time_t)v;
    return 0;
}

time_t time(time_t *out) {
    time_t t;
    if (rtc_now(&t) != 0) t = (time_t)-1;
    if (out) *out = t;
    return t;
}

clock_t clock(void) {
    uint64_t now = nxsys_time();
    if (now < __nxc_start_ms) return (clock_t)-1;
    uint64_t ms = now - __nxc_start_ms;
    if (ms > (uint64_t)LONG_MAX) return (clock_t)-1;
    return (clock_t)ms;
}

double difftime(time_t end, time_t start) {
    return (double)end - (double)start;
}

int timespec_get(struct timespec *ts, int base) {
    if (!ts || base != TIME_UTC) return 0;
    time_t t;
    if (rtc_now(&t) != 0) return 0;
    ts->tv_sec = t;
    ts->tv_nsec = 0;            /* the clock has one-second resolution */
    return base;
}

/* ============ Calendar ============ */

static int is_leap(int64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* Leap years in [1, y) of the proleptic Gregorian calendar (y may be <= 0) */
static int64_t leaps_before(int64_t y) {
    int64_t p = y - 1;
    return floor_div(p, 4) - floor_div(p, 100) + floor_div(p, 400);
}

/* Days before the first of each month in a common year */
static const int16_t g_cum_days[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

/* Days from 1970-01-01 to y-m-d (m 1-12, d may be outside the month) */
static int64_t days_from_civil(int64_t y, int m, int d) {
    int64_t days = 365 * (y - 1970) + (leaps_before(y) - leaps_before(1970));
    days += g_cum_days[m - 1] + ((m > 2 && is_leap(y)) ? 1 : 0);
    return days + d - 1;
}

/* Inverse of days_from_civil for d within the month */
static void civil_from_days(int64_t z, int64_t *y_out, int *m_out, int *d_out) {
    /* 146097 days per 400 years: estimate the year, then correct it */
    int64_t y = 1970 + floor_div(z * 400, 146097);
    while (days_from_civil(y, 1, 1) > z) y--;
    while (days_from_civil(y + 1, 1, 1) <= z) y++;
    int yday = (int)(z - days_from_civil(y, 1, 1));
    int leap = is_leap(y);
    int m = 12;
    while (m > 1 && yday < g_cum_days[m - 1] + ((m > 2 && leap) ? 1 : 0)) m--;
    *y_out = y;
    *m_out = m;
    *d_out = yday - g_cum_days[m - 1] - ((m > 2 && leap) ? 1 : 0) + 1;
}

struct tm *gmtime_r(const time_t *restrict t, struct tm *restrict out) {
    if (!t || !out) {
        errno = EINVAL;
        return NULL;
    }
    int64_t secs = *t;
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    /* about 2.2 billion years either way: any year an int can hold */
    if (days > 800000000000LL || days < -800000000000LL) {
        errno = EOVERFLOW;
        return NULL;
    }
    int64_t y;
    int m, d;
    civil_from_days(days, &y, &m, &d);
    if (y - 1900 > INT_MAX || y - 1900 < INT_MIN) {
        errno = EOVERFLOW;
        return NULL;
    }
    out->tm_year = (int)(y - 1900);
    out->tm_mon = m - 1;
    out->tm_mday = d;
    out->tm_hour = (int)(rem / 3600);
    out->tm_min = (int)(rem / 60 % 60);
    out->tm_sec = (int)(rem % 60);
    int64_t wd = (days + 4) % 7;            /* 1970-01-01 was a Thursday */
    out->tm_wday = (int)(wd < 0 ? wd + 7 : wd);
    out->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    out->tm_isdst = 0;
    return out;
}

struct tm *localtime_r(const time_t *restrict t, struct tm *restrict out) {
    return gmtime_r(t, out);    /* no time zones: local time is UTC */
}

static struct tm g_tm;

struct tm *gmtime(const time_t *t) { return gmtime_r(t, &g_tm); }
struct tm *localtime(const time_t *t) { return gmtime_r(t, &g_tm); }

/* Normalises out-of-range fields (e.g. tm_mday = 40) like ISO C requires */
time_t mktime(struct tm *tm) {
    if (!tm) {
        errno = EINVAL;
        return (time_t)-1;
    }
    int64_t year = (int64_t)tm->tm_year + 1900;
    int64_t mon = tm->tm_mon;
    year += mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        year--;
    }
    int64_t days = days_from_civil(year, (int)mon + 1, 1) + (int64_t)tm->tm_mday - 1;
    int64_t secs = days * 86400 + (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 +
                   (int64_t)tm->tm_sec;
    time_t t = (time_t)secs;
    struct tm norm;
    if (!gmtime_r(&t, &norm)) return (time_t)-1;
    *tm = norm;
    return t;
}

/* ============ Text ============ */

static const char *const g_wday_abbr[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const g_wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const g_mon_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *const g_mon_full[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};

static const char *name_or_q(const char *const *table, int i, int n) {
    return (i >= 0 && i < n) ? table[i] : "?";
}

char *asctime(const struct tm *tm) {
    static char buf[64];
    if (!tm) {
        errno = EINVAL;
        return NULL;
    }
    /* "Sun Sep 16 01:03:52 1973\n" */
    if (strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Y\n", tm) == 0) {
        errno = EOVERFLOW;
        return NULL;
    }
    /* asctime pads the day with a space, not a zero: %e does that */
    return buf;
}

char *ctime(const time_t *t) {
    struct tm tmp;
    if (!localtime_r(t, &tmp)) return NULL;
    return asctime(&tmp);
}

/* ============ strftime ============ */

typedef struct {
    char  *buf;
    size_t size;
    size_t len;
    int    overflow;
} out_t;

static void out_str(out_t *o, const char *s) {
    for (; *s; s++) {
        if (o->len + 1 >= o->size) {
            o->overflow = 1;
            return;
        }
        o->buf[o->len++] = *s;
    }
}

static void out_num(out_t *o, long long v, int width, char pad) {
    char tmp[24];
    int n = 0, neg = v < 0;
    unsigned long long a = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
    do {
        tmp[n++] = (char)('0' + a % 10);
        a /= 10;
    } while (a && n < 22);
    char s[32];
    int k = 0;
    if (neg) s[k++] = '-';
    for (int i = n; i < width - neg; i++) s[k++] = pad;
    while (n > 0) s[k++] = tmp[--n];
    s[k] = '\0';
    out_str(o, s);
}

/* ISO 8601 week-based year and week number */
static void iso_week(const struct tm *tm, long long *iso_year, int *week) {
    long long year = (long long)tm->tm_year + 1900;
    int wday = (tm->tm_wday + 6) % 7;                   /* Monday = 0 */
    int yday = tm->tm_yday;
    int w = (yday - wday + 10) / 7;                      /* 1-53 */
    if (w < 1) {
        year--;
        int prev_days = is_leap(year) ? 366 : 365;
        int py = yday + prev_days;
        w = (py - wday + 10) / 7;
    } else {
        int days = is_leap(year) ? 366 : 365;
        if (w == 53 && (yday - wday + 3) >= days) {     /* Thursday of this week is next year */
            year++;
            w = 1;
        }
    }
    *iso_year = year;
    *week = w;
}

static void format(out_t *o, const char *fmt, const struct tm *tm) {
    for (const char *p = fmt; *p && !o->overflow; p++) {
        if (*p != '%') {
            char c[2] = { *p, '\0' };
            out_str(o, c);
            continue;
        }
        p++;
        if (*p == 'E' || *p == 'O') p++;    /* locale modifiers: ignored in "C" */
        long long year = (long long)tm->tm_year + 1900;
        int hour12 = tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12;
        switch (*p) {
        case 'a': out_str(o, name_or_q(g_wday_abbr, tm->tm_wday, 7)); break;
        case 'A': out_str(o, name_or_q(g_wday_full, tm->tm_wday, 7)); break;
        case 'b':
        case 'h': out_str(o, name_or_q(g_mon_abbr, tm->tm_mon, 12)); break;
        case 'B': out_str(o, name_or_q(g_mon_full, tm->tm_mon, 12)); break;
        case 'c': format(o, "%a %b %e %H:%M:%S %Y", tm); break;
        case 'C': out_num(o, year / 100 - (year % 100 < 0 ? 1 : 0), 2, '0'); break;
        case 'd': out_num(o, tm->tm_mday, 2, '0'); break;
        case 'D': format(o, "%m/%d/%y", tm); break;
        case 'e': out_num(o, tm->tm_mday, 2, ' '); break;
        case 'F': format(o, "%Y-%m-%d", tm); break;
        case 'g': {
            long long iy;
            int w;
            iso_week(tm, &iy, &w);
            out_num(o, ((iy % 100) + 100) % 100, 2, '0');
            break;
        }
        case 'G': {
            long long iy;
            int w;
            iso_week(tm, &iy, &w);
            out_num(o, iy, 4, '0');
            break;
        }
        case 'H': out_num(o, tm->tm_hour, 2, '0'); break;
        case 'I': out_num(o, hour12, 2, '0'); break;
        case 'j': out_num(o, tm->tm_yday + 1, 3, '0'); break;
        case 'm': out_num(o, tm->tm_mon + 1, 2, '0'); break;
        case 'M': out_num(o, tm->tm_min, 2, '0'); break;
        case 'n': out_str(o, "\n"); break;
        case 'p': out_str(o, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'r': format(o, "%I:%M:%S %p", tm); break;
        case 'R': format(o, "%H:%M", tm); break;
        case 'S': out_num(o, tm->tm_sec, 2, '0'); break;
        case 't': out_str(o, "\t"); break;
        case 'T': format(o, "%H:%M:%S", tm); break;
        case 'u': out_num(o, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0'); break;
        case 'U': out_num(o, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0'); break;
        case 'V': {
            long long iy;
            int w;
            iso_week(tm, &iy, &w);
            out_num(o, w, 2, '0');
            break;
        }
        case 'w': out_num(o, tm->tm_wday, 1, '0'); break;
        case 'W': out_num(o, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0'); break;
        case 'x': format(o, "%m/%d/%y", tm); break;
        case 'X': format(o, "%H:%M:%S", tm); break;
        case 'y': out_num(o, ((year % 100) + 100) % 100, 2, '0'); break;
        case 'Y': out_num(o, year, 4, '0'); break;
        case 'z': out_str(o, "+0000"); break;
        case 'Z': out_str(o, "UTC"); break;
        case '%': out_str(o, "%"); break;
        case '\0': return;
        default: {
            /* unknown: copy it through so the mistake is visible */
            char c[3] = { '%', *p, '\0' };
            out_str(o, c);
            break;
        }
        }
    }
}

size_t strftime(char *restrict buf, size_t size, const char *restrict fmt, const struct tm *restrict tm) {
    if (!buf || size == 0 || !fmt || !tm) return 0;
    out_t o = { buf, size, 0, 0 };
    format(&o, fmt, tm);
    if (o.overflow) {
        buf[0] = '\0';
        return 0;
    }
    buf[o.len] = '\0';
    return o.len;
}
